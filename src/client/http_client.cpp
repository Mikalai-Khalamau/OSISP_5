#include "http_client.h"
#include "../common/net_io.h"
#include "../common/text_utils.h"
#include "../common/timing.h"
#include "../common/winsock_utils.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

const size_t kMaxResponseHead = 64 * 1024;

// Разбор статусной строки и Content-Length.
struct ParsedHead {
    bool ok = false;
    int status = 0;
    long long contentLength = -1;  // -1 — заголовка нет
};

ParsedHead ParseResponseHead(const std::string& head) {
    ParsedHead r;
    size_t eol = head.find("\r\n");
    if (eol == std::string::npos) return r;
    std::string line = head.substr(0, eol);
    // «HTTP/1.1 200 OK»
    if (line.size() < 12 || line.compare(0, 5, "HTTP/") != 0) return r;
    size_t sp = line.find(' ');
    if (sp == std::string::npos || sp + 4 > line.size()) return r;
    std::string code = line.substr(sp + 1, 3);
    for (size_t i = 0; i < 3; ++i) if (code[i] < '0' || code[i] > '9') return r;
    r.status = std::stoi(code);

    size_t pos = eol + 2;
    while (pos < head.size()) {
        size_t e = head.find("\r\n", pos);
        if (e == std::string::npos || e == pos) break;
        std::string h = head.substr(pos, e - pos);
        pos = e + 2;
        size_t colon = h.find(':');
        if (colon == std::string::npos) continue;
        if (ToLowerAscii(h.substr(0, colon)) == "content-length") {
            std::string v = h.substr(colon + 1);
            size_t b = v.find_first_not_of(" \t");
            if (b == std::string::npos) return r;
            v = v.substr(b);
            for (size_t i = 0; i < v.size(); ++i) if (v[i] < '0' || v[i] > '9') return r;
            if (v.empty() || v.size() > 18) return r;
            r.contentLength = std::stoll(v);
        }
    }
    r.ok = true;
    return r;
}

std::string CsvSafe(std::string s) {
    for (size_t i = 0; i < s.size(); ++i)
        if (s[i] == ',' || s[i] == '"' || s[i] == '\r' || s[i] == '\n') s[i] = '_';
    return s;
}

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

bool UnescapeRaw(const std::string& in, std::string& out, std::string& err) {
    out.clear();
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '\\') { out.push_back(in[i]); continue; }
        if (i + 1 >= in.size()) { err = "--raw: обратная косая черта в конце строки"; return false; }
        char c = in[++i];
        if (c == 'r') out.push_back('\r');
        else if (c == 'n') out.push_back('\n');
        else if (c == 't') out.push_back('\t');
        else if (c == '0') out.push_back('\0');
        else if (c == '\\') out.push_back('\\');
        else if (c == 'x') {
            if (i + 2 >= in.size()) { err = "--raw: после \\x нужно две hex-цифры"; return false; }
            int hi = HexDigit(in[i + 1]), lo = HexDigit(in[i + 2]);
            if (hi < 0 || lo < 0) { err = "--raw: после \\x нужно две hex-цифры"; return false; }
            out.push_back(static_cast<char>(hi * 16 + lo));
            i += 2;
        } else {
            err = std::string("--raw: неизвестная escape-последовательность \\") + c;
            return false;
        }
    }
    return true;
}

HttpClient::HttpClient(const ClientConfig& cfg, Logger& log) : cfg_(cfg), log_(log) {
    std::memset(&server_, 0, sizeof(server_));
}

void HttpClient::Fail(RequestMetrics& m, const char* func, int wsaErr) {
    m.netError = true;
    if (wsaErr == WSAETIMEDOUT) m.timeout = true;
    m.error = std::string(func) + " " + WsaErrorName(wsaErr) + " " + std::to_string(wsaErr);
    log_.LogWsaError(func, wsaErr, "requestId=" + std::to_string(m.reqNo));
}

RequestMetrics HttpClient::DoOneRequest(int reqNo, const std::string& request, bool headMethod) {
    RequestMetrics m;
    m.reqNo = reqNo;
    m.method = cfg_.useRaw ? "RAW" : cfg_.method;
    m.path = cfg_.useRaw ? "(raw)" : cfg_.path;
    const std::string rid = "requestId=" + std::to_string(reqNo);

    const double t0 = NowMs();
    // socket(): новый TCP-сокет на каждый запрос (сервер закрывает соединение после ответа).
    SocketHandle sock(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (!sock.Valid()) { Fail(m, "socket", WSAGetLastError()); m.totalMs = NowMs() - t0; return m; }
    SetSocketTimeouts(sock.Get(), cfg_.timeoutMs, cfg_.timeoutMs);

    // connect(): трёхэтапное рукопожатие TCP. connect_ms — время его выполнения.
    int err = 0;
    const double tc0 = NowMs();
    bool connected = ConnectWithTimeout(sock.Get(), server_, cfg_.timeoutMs, err);
    m.connectMs = NowMs() - tc0;
    if (!connected) { Fail(m, "connect", err); m.totalMs = NowMs() - t0; return m; }
    log_.Log("CONNECTED", rid + " connect_ms=" + FormatDouble(m.connectMs, 3));

    // send(): цикл до полной отправки запроса.
    size_t sent = 0;
    IoStatus st = SendAll(sock.Get(), request.data(), request.size(), sent, err);
    m.bytesSent = sent;
    if (st != IoStatus::Ok) { Fail(m, "send", err); m.totalMs = NowMs() - t0; return m; }
    log_.Log("SEND", rid + " bytes=" + std::to_string(sent));
    const double tSendDone = NowMs();

    // recv(): читаем, пока не получим весь ответ (по Content-Length) либо сервер не закроет соединение.
    std::string resp;
    char buf[16384];
    bool gotFirst = false, headDone = false, closed = false;
    size_t headEnd = 0;
    ParsedHead ph;
    for (;;) {
        int got = 0;
        st = RecvSome(sock.Get(), buf, sizeof(buf), got, err);
        if (st == IoStatus::Closed) { closed = true; break; }
        if (st != IoStatus::Ok) { Fail(m, "recv", err); break; }
        if (!gotFirst) { m.ttfbMs = NowMs() - tSendDone; gotFirst = true; }
        resp.append(buf, static_cast<size_t>(got));
        if (!headDone) {
            size_t p = resp.find("\r\n\r\n");
            if (p != std::string::npos) {
                headDone = true;
                headEnd = p + 4;
                ph = ParseResponseHead(resp.substr(0, headEnd));
                if (!ph.ok) { m.netError = true; m.error = "bad_response_head"; break; }
            } else if (resp.size() > kMaxResponseHead) {
                m.netError = true;
                m.error = "response_head_too_large";
                break;
            }
        }
        if (headDone) {
            const size_t bodyNow = resp.size() - headEnd;
            if (headMethod) break;  // у ответа на HEAD тела нет — не ждём
            if (ph.contentLength >= 0 && bodyNow >= static_cast<size_t>(ph.contentLength)) break;
        }
    }

    // Итоговые проверки ответа.
    m.bytesRecv = resp.size();
    if (!m.netError) {
        if (!headDone) {
            m.netError = true;
            m.error = resp.empty() ? "empty_response_connection_closed" : "incomplete_response_head";
            if (resp.empty()) log_.LogImportant("ERROR", rid + " what=connection closed by server without response");
        } else {
            const size_t body = resp.size() - headEnd;
            m.status = ph.status;
            m.bodyBytes = headMethod ? 0 : body;
            if (headMethod && body > 0) {
                m.netError = true;
                m.error = "head_response_has_body";
            } else if (!headMethod && ph.contentLength >= 0 && body != static_cast<size_t>(ph.contentLength)) {
                m.netError = true;
                m.error = body < static_cast<size_t>(ph.contentLength) ? "truncated_body" : "body_longer_than_content_length";
            } else if (!headMethod && ph.contentLength < 0 && !closed) {
                m.netError = true;
                m.error = "no_content_length";
            }
            if (m.netError) log_.LogImportant("ERROR", rid + " what=" + m.error + " body=" + std::to_string(body) +
                                                           " content_length=" + std::to_string(ph.contentLength));
        }
    }

    if (headDone && !cfg_.quiet) {
        // Статусная строка и заголовки (по умолчанию), тело — только с --print-body.
        std::string head = resp.substr(0, headEnd);
        std::fwrite(head.data(), 1, head.size(), stdout);
        if (cfg_.printBody && !headMethod) {
            std::fwrite(resp.data() + headEnd, 1, resp.size() - headEnd, stdout);
            std::fputs("\n", stdout);
        }
        std::fflush(stdout);
    }

    sock.Reset();  // closesocket()
    m.totalMs = NowMs() - t0;
    return m;
}

int HttpClient::Run() {
    WsaSession wsa;
    if (!wsa.ok()) { log_.LogWsaError("WSAStartup", wsa.error()); return 1; }

    // getaddrinfo(): разрешаем имя/адрес ОДИН раз, чтобы время DNS не входило в connect_ms.
    addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* res = nullptr;
    int gai = getaddrinfo(cfg_.host.c_str(), std::to_string(cfg_.port).c_str(), &hints, &res);
    if (gai != 0 || res == nullptr) {
        log_.LogWsaError("getaddrinfo", gai != 0 ? gai : WSAHOST_NOT_FOUND, "host=" + cfg_.host);
        return 3;
    }
    std::memcpy(&server_, res->ai_addr, std::min<size_t>(sizeof(server_), static_cast<size_t>(res->ai_addrlen)));
    freeaddrinfo(res);

    // Подготовка запроса (один и тот же для всех повторов).
    std::string request;
    bool headMethod = false;
    if (cfg_.useRaw) {
        request = cfg_.rawRequest;
        headMethod = request.compare(0, 5, "HEAD ") == 0;
    } else {
        request = cfg_.method + " " + cfg_.path + " HTTP/1.1\r\n" +
                  "Host: " + cfg_.host + ":" + std::to_string(cfg_.port) + "\r\n" +
                  "User-Agent: Lab5-Client/1.0\r\nAccept: */*\r\nConnection: close\r\n\r\n";
        headMethod = (cfg_.method == "HEAD");
    }

    // CSV-файл (дописывание; заголовок — если файл новый/пустой).
    FILE* csv = nullptr;
    if (!cfg_.csvFile.empty()) {
        csv = std::fopen(cfg_.csvFile.c_str(), "ab");
        if (!csv) { log_.LogImportant("ERROR", "what=csv_open file=" + cfg_.csvFile); return 2; }
        std::fseek(csv, 0, SEEK_END);
        if (std::ftell(csv) == 0)
            std::fputs("run_id,client_id,req_no,method,path,status,bytes_sent,bytes_recv,body_bytes,connect_ms,ttfb_ms,total_ms,error\n", csv);
    }

    log_.Log("START", "host=" + cfg_.host + " port=" + std::to_string(cfg_.port) + " count=" + std::to_string(cfg_.count) +
                          " run_id=" + std::to_string(cfg_.runId));

    long long responses = 0, okCount = 0, errCount = 0, timeoutCount = 0, unexpected = 0;
    double sumTotal = 0, minTotal = 0, maxTotal = 0;
    unsigned long long sumSent = 0, sumRecv = 0;
    const double tStart = NowMs();

    for (int i = 1; i <= cfg_.count; ++i) {
        RequestMetrics m = DoOneRequest(i, request, headMethod);
        if (m.status > 0) ++responses;
        if (m.netError) ++errCount; else ++okCount;
        if (m.timeout) ++timeoutCount;
        if (!m.netError && cfg_.expect >= 0 && m.status != cfg_.expect) ++unexpected;
        if (i == 1 || m.totalMs < minTotal) minTotal = m.totalMs;
        if (i == 1 || m.totalMs > maxTotal) maxTotal = m.totalMs;
        sumTotal += m.totalMs;
        sumSent += m.bytesSent;
        sumRecv += m.bytesRecv;

        log_.Log("RESULT", "requestId=" + std::to_string(i) + " status=" + std::to_string(m.status) +
                               " bytes_recv=" + std::to_string(m.bytesRecv) + " body_bytes=" + std::to_string(m.bodyBytes) +
                               " connect_ms=" + FormatDouble(m.connectMs, 3) + " ttfb_ms=" + FormatDouble(m.ttfbMs, 3) +
                               " total_ms=" + FormatDouble(m.totalMs, 3) + (m.error.empty() ? "" : " error=" + m.error));
        if (csv) {
            std::fprintf(csv, "%d,%d,%d,%s,%s,%d,%zu,%zu,%zu,%.4f,%.4f,%.4f,%s\n", cfg_.runId, cfg_.id, i,
                         CsvSafe(m.method).c_str(), CsvSafe(m.path).c_str(), m.status, m.bytesSent, m.bytesRecv,
                         m.bodyBytes, m.connectMs, m.ttfbMs, m.totalMs, CsvSafe(m.error).c_str());
        }
    }
    const double elapsedMs = NowMs() - tStart;
    if (csv) std::fclose(csv);

    // Сводка. Пропускная способность = все принятые байты (заголовки + тело) / время работы клиента.
    const double sec = elapsedMs / 1000.0;
    const double bps = sec > 0 ? static_cast<double>(sumRecv) / sec : 0;
    const double mbit = bps * 8.0 / 1e6;
    const double avg = cfg_.count > 0 ? sumTotal / cfg_.count : 0;
    log_.LogImportant("SUMMARY",
        "sent_requests=" + std::to_string(cfg_.count) + " responses=" + std::to_string(responses) +
        " ok=" + std::to_string(okCount) + " errors=" + std::to_string(errCount) + " timeouts=" + std::to_string(timeoutCount) +
        (cfg_.expect >= 0 ? " unexpected_status=" + std::to_string(unexpected) : std::string()) +
        " avg_total_ms=" + FormatDouble(avg, 3) + " min_total_ms=" + FormatDouble(minTotal, 3) +
        " max_total_ms=" + FormatDouble(maxTotal, 3) + " bytes_sent=" + std::to_string(sumSent) +
        " bytes_recv=" + std::to_string(sumRecv) + " elapsed_ms=" + FormatDouble(elapsedMs, 1) +
        " throughput_Bps=" + FormatDouble(bps, 0) + " throughput_Mbit_s=" + FormatDouble(mbit, 3));

    if (errCount > 0) return 1;
    if (unexpected > 0) return 4;
    return 0;
}

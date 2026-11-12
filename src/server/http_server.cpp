// http_server.cpp — реализация сервера. Каждый шаг Winsock прокомментирован.
#include "http_server.h"
#include "http_parser.h"
#include "../common/net_io.h"
#include "../common/text_utils.h"
#include "../common/timing.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace {

const int kRecvChunk = 4096;       // размер одного recv() при чтении заголовков
const int kStopGraceMs = 2000;     // сколько ждём завершения активных запросов при остановке
const int kStopForceMs = 3000;     // ещё сколько ждём после принудительного shutdown()

const char* ReasonPhrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        default:  return "Unknown";
    }
}

// Короткое тело для ошибок 400/404/405.
std::string ErrorBody(int status) {
    std::string t = std::to_string(status) + " " + ReasonPhrase(status);
    return "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>" + t + "</title></head><body><h1>" + t +
           "</h1></body></html>\n";
}

// Content-Type по расширению. Для .txt и .html/.htm — по заданию; для ВСЕХ прочих расширений
// отдаём application/octet-stream (нейтральный «неизвестный бинарный тип»).
std::string ContentTypeFor(const std::wstring& path) {
    size_t slash = path.find_last_of(L'\\');
    size_t dot = path.find_last_of(L'.');
    std::string ext;
    if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash))
        ext = ToLowerAscii(WideToUtf8(path.substr(dot)));
    if (ext == ".txt") return "text/plain; charset=utf-8";
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    return "application/octet-stream";
}

// Заголовки ответа. Content-Length — точный размер тела в байтах (для HEAD — размер тела, которое
// отдал бы GET). Connection: close — keep-alive не поддерживается.
std::string BuildHeaders(int status, const std::string& contentType, unsigned long long length, bool withAllow) {
    std::string h = "HTTP/1.1 " + std::to_string(status) + " " + ReasonPhrase(status) + "\r\n";
    h += "Server: Lab5-HTTP/1.0\r\n";
    h += "Date: " + HttpDateNow() + "\r\n";
    h += "Content-Type: " + contentType + "\r\n";
    h += "Content-Length: " + std::to_string(length) + "\r\n";
    if (withAllow) h += "Allow: GET, HEAD\r\n";
    h += "Connection: close\r\n\r\n";
    return h;
}

// Отправка файла кусками по 64 КБ: читаем кусок с диска -> SendAll (цикл до полной отправки).
IoStatus SendFileBody(SOCKET s, HANDLE file, unsigned long long size, size_t& sentTotal, int& wsaErr, bool& readFailed) {
    std::vector<char> buf(64 * 1024);
    unsigned long long remaining = size;
    readFailed = false;
    while (remaining > 0) {
        DWORD want = static_cast<DWORD>(std::min<unsigned long long>(buf.size(), remaining));
        DWORD got = 0;
        if (!ReadFile(file, buf.data(), want, &got, nullptr) || got == 0) {
            readFailed = true;  // файл изменился/недоступен посреди отправки
            return IoStatus::Error;
        }
        size_t sent = 0;
        IoStatus st = SendAll(s, buf.data(), got, sent, wsaErr);
        sentTotal += sent;
        if (st != IoStatus::Ok) return st;
        remaining -= got;
    }
    return IoStatus::Ok;
}

}  // namespace

HttpServer::HttpServer(const ServerConfig& cfg, Logger& log, const std::atomic<bool>& stopFlag)
    : cfg_(cfg), log_(log), stop_(stopFlag) {}

bool HttpServer::Setup(int& exitCode) {
    std::string err;
    if (!InitWwwRoot(cfg_.www, root_, err)) {
        log_.LogImportant("ERROR", "what=www_root " + err);
        exitCode = 2;
        return false;
    }

    // inet_pton — современная замена inet_addr: текст "127.0.0.1" -> бинарный IPv4-адрес.
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(cfg_.port));  // htons: порядок байт хоста -> сетевой
    if (inet_pton(AF_INET, cfg_.host.c_str(), &addr.sin_addr) != 1) {
        log_.LogImportant("ERROR", "what=bad_host value=" + cfg_.host + " (нужен IPv4-адрес вида 127.0.0.1)");
        exitCode = 2;
        return false;
    }

    // socket(): создаём TCP-сокет (AF_INET + SOCK_STREAM + IPPROTO_TCP).
    listen_.Reset(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (!listen_.Valid()) {
        log_.LogWsaError("socket", WSAGetLastError());
        exitCode = 1;
        return false;
    }

    // ВАЖНО: SO_REUSEADDR НЕ включаем. В Windows он позволяет второму процессу «подсесть» на уже
    // занятый порт, и проверка «порт занят» перестала бы работать. Без него повторный bind на
    // занятый порт честно даёт WSAEADDRINUSE. (TIME_WAIT в Windows перезапуск сервера не блокирует.)

    // bind(): привязываем сокет к адресу и порту.
    if (bind(listen_.Get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        int e = WSAGetLastError();
        log_.LogWsaError("bind", e, "host=" + cfg_.host + " port=" + std::to_string(cfg_.port));
        if (e == WSAEADDRINUSE)
            std::fprintf(stderr, "Ошибка bind: порт %d уже занят другой программой (WSAEADDRINUSE, код %d). "
                                 "Выберите другой порт: --port <N>.\n", cfg_.port, e);
        else
            std::fprintf(stderr, "Ошибка bind (код %d). Подробности выше.\n", e);
        exitCode = 3;
        return false;
    }

    // listen(): переводим сокет в режим прослушивания; SOMAXCONN — максимальная очередь ожидающих.
    if (listen(listen_.Get(), SOMAXCONN) == SOCKET_ERROR) {
        log_.LogWsaError("listen", WSAGetLastError());
        exitCode = 1;
        return false;
    }
    return true;
}

int HttpServer::Run() {
    WsaSession wsa;  // WSAStartup здесь, WSACleanup — в деструкторе при ЛЮБОМ выходе из Run()
    if (!wsa.ok()) {
        log_.LogWsaError("WSAStartup", wsa.error());
        return 1;
    }
    int exitCode = 0;
    if (!Setup(exitCode)) return exitCode;

    log_.LogImportant("START", "host=" + cfg_.host + " port=" + std::to_string(cfg_.port) +
                                   " www=" + WideToUtf8(root_.prefix) + " timeout_ms=" + std::to_string(cfg_.timeoutMs) +
                                   " max_header=" + std::to_string(cfg_.maxHeader) + " max_conn=" + std::to_string(cfg_.maxConn));
    AcceptLoop();

    // ---- Штатное завершение ----
    log_.LogImportant("STOPPING", "active=" + std::to_string(active_.load()));
    listen_.Reset();  // closesocket(слушающий сокет): новые соединения больше не принимаются

    // Ждём завершения активных запросов с ограничением по времени.
    for (int waited = 0; active_.load() > 0 && waited < kStopGraceMs; waited += 50) Sleep(50);
    if (active_.load() > 0) {
        // Оставшиеся (молчащие/медленные) клиенты: shutdown() будит заблокированный recv();
        // сам closesocket() вызовет поток-владелец сокета.
        std::lock_guard<std::mutex> lock(socketsMutex_);
        for (std::set<SOCKET>::iterator it = activeSockets_.begin(); it != activeSockets_.end(); ++it)
            shutdown(*it, SD_BOTH);
    }
    for (int waited = 0; active_.load() > 0 && waited < kStopForceMs; waited += 50) Sleep(50);
    if (active_.load() > 0) log_.LogImportant("WARN", "active_connections_left=" + std::to_string(active_.load()));

    PrintStats();
    log_.LogImportant("STOP", "exit_code=0");
    return 0;
}

void HttpServer::AcceptLoop() {
    while (!stop_.load()) {
        // select() с таймаутом 200 мс: так цикл регулярно проверяет флаг остановки,
        // а accept() вызывается только когда соединение уже ждёт — он не заблокируется.
        fd_set rset;
        FD_ZERO(&rset);
        FD_SET(listen_.Get(), &rset);
        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200 * 1000;
        int sr = select(0, &rset, nullptr, nullptr, &tv);
        if (sr == SOCKET_ERROR) {
            log_.LogWsaError("select", WSAGetLastError());
            Sleep(50);
            continue;
        }
        if (sr == 0) continue;  // таймаут — снова проверить флаг остановки

        sockaddr_in peer;
        int peerLen = sizeof(peer);
        SOCKET c = accept(listen_.Get(), reinterpret_cast<sockaddr*>(&peer), &peerLen);
        if (c == INVALID_SOCKET) {
            int e = WSAGetLastError();
            if (e != WSAECONNRESET && e != WSAEINTR) log_.LogWsaError("accept", e);
            Sleep(10);  // не крутимся «горячим» циклом при повторяющейся ошибке
            continue;
        }

        // Лимит одновременных соединений: при превышении просто закрываем новое соединение.
        if (active_.load() >= cfg_.maxConn) {
            ++rejected_;
            log_.Log("LIMIT_EXCEEDED", "peer=" + EndpointToString(peer) + " max_conn=" + std::to_string(cfg_.maxConn));
            closesocket(c);
            continue;
        }

        ++active_;  // увеличиваем ДО запуска потока, чтобы лимит был точным
        long connId = ++connCounter_;
        try {
            std::thread(&HttpServer::HandleConnection, this, c, peer, connId).detach();
        } catch (const std::exception& ex) {
            --active_;
            closesocket(c);
            log_.LogImportant("ERROR", std::string("what=thread_create ") + ex.what());
        }
    }
}

void HttpServer::HandleConnection(SOCKET raw, sockaddr_in peer, long connId) {
    // Порядок объявления важен (деструкторы идут в обратном порядке):
    // 1) CountGuard — самым последним уменьшит счётчик active_;
    // 2) SocketHandle — закроет сокет;
    // 3) Registration — первым уберёт сокет из реестра активных.
    struct CountGuard {
        std::atomic<int>& n;
        explicit CountGuard(std::atomic<int>& c) : n(c) {}
        ~CountGuard() { --n; }
    } countGuard(active_);
    SocketHandle sock(raw);
    struct Registration {
        HttpServer* self;
        SOCKET s;
        Registration(HttpServer* sv, SOCKET sk) : self(sv), s(sk) {
            std::lock_guard<std::mutex> lock(self->socketsMutex_);
            self->activeSockets_.insert(s);
        }
        ~Registration() {
            std::lock_guard<std::mutex> lock(self->socketsMutex_);
            self->activeSockets_.erase(s);
        }
    } registration(this, raw);

    const std::string peerStr = EndpointToString(peer);
    ++connections_;
    log_.Log("CLIENT_CONNECTED", "conn=" + std::to_string(connId) + " peer=" + peerStr);
    try {
        ServeOne(sock.Get(), peerStr, connId);
        GracefulClose(sock.Get());
    } catch (const std::exception& ex) {  // ошибка одного клиента не должна ронять сервер
        log_.LogImportant("ERROR", "what=exception conn=" + std::to_string(connId) + " msg=" + ex.what());
    } catch (...) {
        log_.LogImportant("ERROR", "what=unknown_exception conn=" + std::to_string(connId));
    }
    log_.Log("CLIENT_DISCONNECTED", "conn=" + std::to_string(connId) + " peer=" + peerStr);
}

// Чтение запроса: recv() в цикле до \r\n\r\n. Буфер ограничен maxHeader (+ один кусок recv),
// общий лимит времени — timeoutMs (защита от медленных клиентов: «по байту в секунду» не пройдёт).
HttpServer::ReadResult HttpServer::ReadRequestHead(SOCKET s, std::string& head, int& wsaErr) {
    const size_t limit = static_cast<size_t>(cfg_.maxHeader);
    const double start = NowMs();
    std::string buf;
    size_t scanFrom = 0;
    char tmp[kRecvChunk];
    for (;;) {
        size_t pos = buf.find("\r\n\r\n", scanFrom);
        if (pos != std::string::npos) {
            const size_t headEnd = pos + 4;
            if (headEnd > limit) { head = buf.substr(0, std::min(buf.size(), limit)); return ReadResult::TooLarge; }
            head = buf.substr(0, headEnd);  // всё, что после — тело/мусор, не обрабатываем
            return ReadResult::Ok;
        }
        // Терминатора нет, а накопили уже >= лимита: блок заголовков заведомо длиннее лимита.
        if (buf.size() >= limit) { head = buf; return ReadResult::TooLarge; }
        scanFrom = buf.size() >= 3 ? buf.size() - 3 : 0;  // терминатор мог разорваться между порциями

        const double left = cfg_.timeoutMs - (NowMs() - start);
        if (left <= 0) { head = buf; return ReadResult::Timeout; }
        SetRecvTimeout(s, static_cast<int>(left));

        int got = 0;
        IoStatus st = RecvSome(s, tmp, kRecvChunk, got, wsaErr);
        if (st == IoStatus::Ok) { buf.append(tmp, static_cast<size_t>(got)); continue; }
        head = buf;
        if (st == IoStatus::Closed) return ReadResult::ClosedEarly;
        if (st == IoStatus::Timeout) return ReadResult::Timeout;
        return ReadResult::Error;
    }
}

void HttpServer::RecordStatus(int status) {
    ++total_;
    if (status == 200) ++s200_;
    else if (status == 400) ++s400_;
    else if (status == 404) ++s404_;
    else if (status == 405) ++s405_;
}

void HttpServer::ServeOne(SOCKET s, const std::string& peerStr, long connId) {
    const std::string conn = "conn=" + std::to_string(connId);
    SetSocketTimeouts(s, cfg_.timeoutMs, std::max(cfg_.timeoutMs, 5000));  // отдельный таймаут отправки

    std::string head;
    int wsaErr = 0;
    ReadResult rr = ReadRequestHead(s, head, wsaErr);

    // Неполный запрос / таймаут / обрыв: ВЫБРАННОЕ РЕШЕНИЕ — ответ не отправляем, просто логируем
    // и закрываем соединение (клиент, не приславший запрос целиком, ответа не заслуживает).
    if (rr == ReadResult::Timeout) {
        ++timeouts_;
        log_.Log("TIMEOUT", conn + " peer=" + peerStr + " bytes_received=" + std::to_string(head.size()));
        return;
    }
    if (rr == ReadResult::ClosedEarly) {
        log_.Log("CLIENT_CLOSED_EARLY", conn + " peer=" + peerStr + " bytes_received=" + std::to_string(head.size()));
        return;
    }
    if (rr == ReadResult::Error) {
        log_.LogWsaError("recv", wsaErr, conn + " peer=" + peerStr);
        return;
    }

    // Полный запрос получен — с этого момента считаем время обработки (QueryPerformanceCounter).
    Stopwatch sw;
    const long long requestId = ++requestCounter_;
    const std::string rid = "requestId=" + std::to_string(requestId);

    int status = 400;
    std::string reason;
    bool headOnly = false;
    bool allowHeader = false;
    std::string contentType = "text/html; charset=utf-8";
    ScopedHandle file;
    unsigned long long fileSize = 0;
    std::string errorBody;
    bool isFile = false;

    if (rr == ReadResult::TooLarge) {
        // Лимит блока заголовков превышен -> 400 и закрытие соединения (хвост запроса не читаем).
        reason = "header block exceeds limit of " + std::to_string(cfg_.maxHeader) + " bytes";
        ++securityEvents_;
        log_.Log("RECV", rid + " peer=" + peerStr + " size=" + std::to_string(head.size()) + " method=? target=?");
        log_.Log("LIMIT_EXCEEDED", rid + " peer=" + peerStr + " reason=\"" + reason + "\"");
    } else {
        ParseResult pr = ParseRequestHead(head);
        const HttpRequest& rq = pr.request;
        log_.Log("RECV", rid + " peer=" + peerStr + " size=" + std::to_string(head.size()) +
                             " method=" + (rq.method.empty() ? "-" : SanitizeForLog(rq.method, 32)) +
                             " target=" + (rq.target.empty() ? "-" : SanitizeForLog(rq.target)));
        headOnly = (rq.method == "HEAD");
        if (pr.status == ParseStatus::BadRequest) {
            status = 400;
            reason = pr.reason;
        } else if (pr.status == ParseStatus::MethodNotAllowed) {
            status = 405;
            allowHeader = true;
            reason = "method " + SanitizeForLog(rq.method, 32) + " not allowed";
        } else {
            // Запрос с телом (Content-Length > 0): для GET/HEAD тело не обрабатываем и не читаем;
            // ответ отправляем как обычно, затем GracefulClose() вычитывает остаток и закрывает соединение.
            if (rq.contentLength > 0) log_.Log("NOTE", rid + " request body ignored, content_length=" + std::to_string(rq.contentLength));

            PathResult path = ResolveTarget(root_, rq.target);
            if (path.verdict == PathVerdict::BadRequest) {
                status = 400;
                reason = path.reason;
            } else if (path.verdict == PathVerdict::NotFound) {
                status = 404;
                reason = path.reason;
            } else {
                std::string why;
                OpenVerdict ov = OpenGuardedFile(root_, path.fullPath, file, fileSize, why);
                if (ov == OpenVerdict::Ok) {
                    status = 200;
                    isFile = true;
                    contentType = ContentTypeFor(path.fullPath);
                } else {
                    status = 404;
                    reason = why;
                    path.security = (ov == OpenVerdict::Escape);
                }
            }
            if (path.security) {
                ++securityEvents_;
                log_.Log("SECURITY", rid + " peer=" + peerStr + " reason=\"" + reason + "\" target=" + SanitizeForLog(rq.target));
            }
        }
    }

    // ---- Формирование и отправка ответа ----
    if (!isFile) errorBody = ErrorBody(status);
    const unsigned long long bodyLen = isFile ? fileSize : errorBody.size();
    std::string headers = BuildHeaders(status, contentType, bodyLen, allowHeader);

    size_t sentTotal = 0;
    size_t part = 0;
    int err = 0;
    bool readFailed = false;
    // 1) Заголовки (SendAll — цикл send() до полной отправки).
    IoStatus st = SendAll(s, headers.data(), headers.size(), part, err);
    sentTotal += part;
    // 2) Тело (для HEAD не отправляем вообще).
    if (st == IoStatus::Ok && !headOnly) {
        if (isFile) {
            st = SendFileBody(s, file.Get(), fileSize, sentTotal, err, readFailed);
        } else {
            st = SendAll(s, errorBody.data(), errorBody.size(), part, err);
            sentTotal += part;
        }
    }
    bytesSent_ += static_cast<long long>(sentTotal);
    RecordStatus(status);

    const std::string proc = FormatDouble(sw.ElapsedMs(), 3);
    if (st == IoStatus::Ok) {
        log_.Log("SEND", rid + " peer=" + peerStr + " status=" + std::to_string(status) + " bytes=" + std::to_string(sentTotal) +
                             (headOnly ? " head=1" : "") + " proc_ms=" + proc + (reason.empty() ? "" : " reason=\"" + reason + "\""));
    } else if (readFailed) {
        log_.LogImportant("ERROR", rid + " what=file_read_failed bytes_sent=" + std::to_string(sentTotal));
    } else {
        log_.LogWsaError("send", err, rid + " peer=" + peerStr + " bytes_sent=" + std::to_string(sentTotal));
        if (st == IoStatus::Timeout) ++timeouts_;
    }
}

// Корректное закрытие: shutdown(SD_SEND) отправляет клиенту FIN («я закончил передачу»), затем
// вычитываем всё, что клиент ещё шлёт (иначе при closesocket с непрочитанными данными Windows отправит RST
// и клиент может потерять наш ответ), ждём recv()==0 с коротким таймаутом. closesocket() — в деструкторе SocketHandle.
void HttpServer::GracefulClose(SOCKET s) {
    if (shutdown(s, SD_SEND) == SOCKET_ERROR) {
        int e = WSAGetLastError();
        if (e != WSAENOTCONN && e != WSAECONNRESET && e != WSAECONNABORTED && e != WSAESHUTDOWN) log_.LogWsaError("shutdown", e);
        return;
    }
    SetRecvTimeout(s, 1000);
    char tmp[4096];
    size_t drained = 0;
    while (drained < 256 * 1024) {
        int got = 0, e = 0;
        if (RecvSome(s, tmp, sizeof(tmp), got, e) != IoStatus::Ok) break;  // 0 = клиент закрыл, или таймаут/ошибка
        drained += static_cast<size_t>(got);
    }
}

void HttpServer::PrintStats() {
    log_.LogImportant("STATS", "requests=" + std::to_string(total_.load()) + " 200=" + std::to_string(s200_.load()) +
                                   " 400=" + std::to_string(s400_.load()) + " 404=" + std::to_string(s404_.load()) +
                                   " 405=" + std::to_string(s405_.load()) + " bytes_sent=" + std::to_string(bytesSent_.load()) +
                                   " connections=" + std::to_string(connections_.load()) +
                                   " timeouts=" + std::to_string(timeouts_.load()) +
                                   " security_events=" + std::to_string(securityEvents_.load()) +
                                   " rejected_conn=" + std::to_string(rejected_.load()));
}

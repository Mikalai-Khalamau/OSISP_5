#include "http_parser.h"
#include "../common/text_utils.h"
#include <cstring>

bool IsTokenChar(unsigned char c) {
    if (c >= '0' && c <= '9') return true;
    if (c >= 'a' && c <= 'z') return true;
    if (c >= 'A' && c <= 'Z') return true;
    // Остальные разрешённые знаки token: !#$%&'*+-.^_`|~
    return c != 0 && std::strchr("!#$%&'*+-.^_`|~", c) != nullptr;
}

static ParseResult Bad(ParseResult& r, const char* why) {
    r.status = ParseStatus::BadRequest;
    r.reason = why;
    return r;
}

static bool IsDigits(const std::string& s) {
    if (s.empty()) return false;
    for (size_t i = 0; i < s.size(); ++i) if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

ParseResult ParseRequestHead(const std::string& head) {
    ParseResult r;

    // 1) Проверка всех байт: NUL и управляющие символы запрещены (кроме CR, LF, HTAB).
    //    CR и LF допустимы только парой \r\n (голый LF или голый CR -> 400).
    for (size_t i = 0; i < head.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(head[i]);
        if (c == '\r') {
            if (i + 1 >= head.size() || head[i + 1] != '\n') return Bad(r, "bare CR");
        } else if (c == '\n') {
            if (i == 0 || head[i - 1] != '\r') return Bad(r, "bare LF");
        } else if ((c < 0x20 && c != '\t') || c == 0x7F) {
            return Bad(r, "control character or NUL byte in request");
        }
    }

    // 2) Делим на строки по \r\n; пустая строка — конец заголовков.
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos < head.size()) {
        size_t e = head.find("\r\n", pos);
        if (e == std::string::npos) return Bad(r, "no CRLF");
        std::string line = head.substr(pos, e - pos);
        pos = e + 2;
        if (line.empty()) break;
        lines.push_back(line);
    }
    if (lines.empty()) return Bad(r, "empty request line");

    // 3) Request-line: «METHOD SP request-target SP HTTP-version». Ровно два пробела.
    const std::string& rl = lines[0];
    size_t sp1 = rl.find(' ');
    if (sp1 == std::string::npos) return Bad(r, "request line has no spaces");
    size_t sp2 = rl.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) return Bad(r, "request line has no HTTP version");
    if (rl.find(' ', sp2 + 1) != std::string::npos) return Bad(r, "too many spaces in request line");
    r.request.method = rl.substr(0, sp1);
    r.request.target = rl.substr(sp1 + 1, sp2 - sp1 - 1);
    r.request.version = rl.substr(sp2 + 1);

    if (r.request.method.empty()) return Bad(r, "empty method");
    for (size_t i = 0; i < r.request.method.size(); ++i)
        if (!IsTokenChar(static_cast<unsigned char>(r.request.method[i]))) return Bad(r, "method is not a token");

    // Принимаем только origin-form: путь, начинающийся с '/'. (absolute-form, '*', authority-form -> 400)
    if (r.request.target.empty() || r.request.target[0] != '/') return Bad(r, "target is not origin-form");
    if (r.request.target.find('\t') != std::string::npos) return Bad(r, "tab in target");

    // Версия: только HTTP/1.1 и HTTP/1.0 (HTTP/1.0 допускаем: Host для него не обязателен).
    if (r.request.version != "HTTP/1.1" && r.request.version != "HTTP/1.0") return Bad(r, "unsupported HTTP version");

    // 4) Заголовки.
    bool hasHost = false, hasLength = false;
    for (size_t i = 1; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (line[0] == ' ' || line[0] == '\t') return Bad(r, "obsolete line folding");
        size_t colon = line.find(':');
        if (colon == std::string::npos || colon == 0) return Bad(r, "header line without ':'");
        std::string name = line.substr(0, colon);
        // Пробел перед двоеточием — не token-символ, поэтому "Host : x" отвергается здесь.
        for (size_t k = 0; k < name.size(); ++k)
            if (!IsTokenChar(static_cast<unsigned char>(name[k]))) return Bad(r, "invalid header name");
        std::string value = line.substr(colon + 1);
        size_t b = value.find_first_not_of(" \t");
        size_t e = value.find_last_not_of(" \t");
        value = (b == std::string::npos) ? std::string() : value.substr(b, e - b + 1);
        name = ToLowerAscii(name);

        if (name == "host") {
            if (hasHost) return Bad(r, "duplicate Host");
            if (value.empty()) return Bad(r, "empty Host");
            hasHost = true;
        } else if (name == "content-length") {
            if (hasLength) return Bad(r, "duplicate Content-Length");
            if (!IsDigits(value) || value.size() > 18) return Bad(r, "invalid Content-Length");
            r.request.contentLength = std::stoll(value);
            hasLength = true;
        }
        r.request.headers.push_back(std::make_pair(name, value));
    }

    // 5) В HTTP/1.1 заголовок Host обязателен (RFC 7230, 5.4).
    if (r.request.version == "HTTP/1.1" && !hasHost) return Bad(r, "missing Host header in HTTP/1.1");

    // 6) Метод. Синтаксис уже проверен, поэтому всё, кроме GET/HEAD, — 405.
    if (r.request.method != "GET" && r.request.method != "HEAD") {
        r.status = ParseStatus::MethodNotAllowed;
        r.reason = "method not allowed";
        return r;
    }
    r.status = ParseStatus::Ok;
    return r;
}

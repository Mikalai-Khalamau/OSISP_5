// http_parser.h — строгий разбор заголовочной части HTTP-запроса (до \r\n\r\n).
#pragma once
#include <string>
#include <utility>
#include <vector>

struct HttpRequest {
    std::string method;
    std::string target;   // request-target как пришёл (origin-form: путь, начинающийся с '/')
    std::string version;  // "HTTP/1.1" или "HTTP/1.0"
    std::vector<std::pair<std::string, std::string> > headers;  // имена в нижнем регистре
    long long contentLength = 0;  // из заголовка Content-Length (0, если его нет)
};

enum class ParseStatus {
    Ok,                 // запрос синтаксически верен и метод GET/HEAD
    BadRequest,         // -> 400
    MethodNotAllowed    // -> 405 (метод — корректный token, но не GET/HEAD)
};

struct ParseResult {
    ParseStatus status = ParseStatus::BadRequest;
    std::string reason;   // причина (для журнала)
    HttpRequest request;  // заполняется по мере разбора (даже при ошибке — что успели разобрать)
};

// token по RFC 7230: допустимые символы в имени метода и имени заголовка.
bool IsTokenChar(unsigned char c);

// head — всё от начала запроса до \r\n\r\n включительно.
ParseResult ParseRequestHead(const std::string& head);

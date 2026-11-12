// http_client.h — простой HTTP-клиент на «сырых» сокетах Winsock (без WinHTTP/curl).
#pragma once
#include "../common/log.h"
#include "../common/platform.h"
#include <string>
#include <vector>

struct ClientConfig {
    std::string host = "127.0.0.1";
    int port = 20113;
    int id = 1;                 // идентификатор клиента
    int timeoutMs = 5000;       // таймаут connect/send/recv
    int count = 1;              // число запросов
    std::string method = "GET";
    std::string path = "/";
    bool useRaw = false;        // отправить --raw как есть
    std::string rawRequest;     // уже раскодированные escape-последовательности
    int runId = 0;
    std::string csvFile;
    bool quiet = false;
    bool printBody = false;
    int expect = -1;            // ожидаемый HTTP-статус (-1 — не проверять)
};

// Метрики одного запроса.
struct RequestMetrics {
    int reqNo = 0;
    std::string method, path;
    int status = 0;                  // 0 — ответа нет
    size_t bytesSent = 0, bytesRecv = 0, bodyBytes = 0;
    double connectMs = 0, ttfbMs = 0, totalMs = 0;
    std::string error;               // коротко, для CSV (без запятых)
    bool netError = false;           // сетевая ошибка/некорректный ответ
    bool timeout = false;
};

// Раскодирует \r \n \t \\ \0 \xHH. false + err при неверной последовательности.
bool UnescapeRaw(const std::string& in, std::string& out, std::string& err);

class HttpClient {
public:
    HttpClient(const ClientConfig& cfg, Logger& log);
    // Выполняет cfg.count запросов, печатает сводку. Возвращает код завершения процесса.
    int Run();
private:
    RequestMetrics DoOneRequest(int reqNo, const std::string& request, bool headMethod);
    void Fail(RequestMetrics& m, const char* func, int wsaErr);
    ClientConfig cfg_;
    Logger& log_;
    sockaddr_in server_;
};

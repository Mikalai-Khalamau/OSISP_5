// http_server.h — упрощённый HTTP/1.1-сервер: один поток accept() + по потоку на соединение.
#pragma once
#include "../common/log.h"
#include "../common/platform.h"
#include "../common/winsock_utils.h"
#include "path_guard.h"
#include <atomic>
#include <mutex>
#include <set>
#include <string>

struct ServerConfig {
    std::string host = "127.0.0.1";  // по умолчанию слушаем ТОЛЬКО локальный интерфейс
    int port = 20113;
    std::wstring www = L"www";
    int timeoutMs = 5000;            // общий лимит времени на получение запроса
    int maxHeader = 8192;            // лимит блока заголовков, байт
    int maxConn = 64;                // лимит одновременных соединений
    int id = 0;                      // идентификатор участника (для журнала)
};

class HttpServer {
public:
    // stopFlag — внешний флаг остановки (его выставляют Ctrl+C / ввод «q»).
    HttpServer(const ServerConfig& cfg, Logger& log, const std::atomic<bool>& stopFlag);
    // Основной цикл. Возвращает код завершения процесса (0 — штатно).
    int Run();

private:
    enum class ReadResult { Ok, TooLarge, Timeout, ClosedEarly, Error };

    bool Setup(int& exitCode);
    void AcceptLoop();
    void HandleConnection(SOCKET raw, sockaddr_in peer, long connId);
    void ServeOne(SOCKET s, const std::string& peerStr, long connId);
    ReadResult ReadRequestHead(SOCKET s, std::string& head, int& wsaErr);
    void GracefulClose(SOCKET s);
    void RecordStatus(int status);
    void PrintStats();

    ServerConfig cfg_;
    Logger& log_;
    const std::atomic<bool>& stop_;
    WwwRoot root_;
    SocketHandle listen_;

    std::atomic<int> active_{0};                // число обслуживаемых соединений
    std::atomic<long long> requestCounter_{0};  // сквозной requestId
    std::atomic<long> connCounter_{0};

    // Статистика
    std::atomic<long long> total_{0}, s200_{0}, s400_{0}, s404_{0}, s405_{0};
    std::atomic<long long> bytesSent_{0}, connections_{0}, timeouts_{0}, securityEvents_{0}, rejected_{0};

    std::mutex socketsMutex_;
    std::set<SOCKET> activeSockets_;  // чтобы при остановке принудительно разбудить зависшие recv()
};

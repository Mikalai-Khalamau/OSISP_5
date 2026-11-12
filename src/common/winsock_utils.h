// winsock_utils.h — инициализация Winsock, RAII-обёртка сокета, описание ошибок.
#pragma once
#include "platform.h"
#include <string>

// Текст системной ошибки (FormatMessage), в UTF-8, без перевода строки.
std::string SystemErrorText(int code);
// Символьное имя кода Winsock (WSAECONNREFUSED и т.д.); "WSA_UNKNOWN", если код не в таблице.
const char* WsaErrorName(int code);
// Единый формат описания ошибки: func=<имя функции> code=<WSAGetLastError> name=<имя> text="<FormatMessage>".
std::string DescribeWsaError(const char* func, int code);

// RAII для WSAStartup/WSACleanup: WSACleanup вызывается автоматически на ЛЮБОМ пути выхода.
class WsaSession {
public:
    WsaSession();
    ~WsaSession();
    bool ok() const { return error_ == 0; }
    int error() const { return error_; }
    WsaSession(const WsaSession&) = delete;
    WsaSession& operator=(const WsaSession&) = delete;
private:
    int error_;
};

// RAII-обёртка SOCKET: деструктор вызывает closesocket(), утечки сокетов невозможны.
class SocketHandle {
public:
    SocketHandle() : s_(INVALID_SOCKET) {}
    explicit SocketHandle(SOCKET s) : s_(s) {}
    ~SocketHandle() { Reset(); }
    SocketHandle(const SocketHandle&) = delete;
    SocketHandle& operator=(const SocketHandle&) = delete;
    SocketHandle(SocketHandle&& o) noexcept : s_(o.s_) { o.s_ = INVALID_SOCKET; }
    SocketHandle& operator=(SocketHandle&& o) noexcept {
        if (this != &o) { Reset(); s_ = o.s_; o.s_ = INVALID_SOCKET; }
        return *this;
    }
    SOCKET Get() const { return s_; }
    bool Valid() const { return s_ != INVALID_SOCKET; }
    // Закрыть текущий сокет (если есть) и, при необходимости, принять новый.
    void Reset(SOCKET s = INVALID_SOCKET);
private:
    SOCKET s_;
};

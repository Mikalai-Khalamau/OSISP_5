#include "winsock_utils.h"
#include "text_utils.h"
#include <cstdio>

std::string SystemErrorText(int code) {
    wchar_t buf[512];
    // FORMAT_MESSAGE_FROM_SYSTEM — взять текст из системной таблицы сообщений Windows.
    DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                             static_cast<DWORD>(code), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                             buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])), nullptr);
    if (n == 0) return "(описание недоступно)";
    std::wstring w(buf, n);
    while (!w.empty() && (w.back() == L'\r' || w.back() == L'\n' || w.back() == L' ' || w.back() == L'.')) w.pop_back();
    for (size_t i = 0; i < w.size(); ++i) {
        if (w[i] == L'"') w[i] = L'\'';
        if (w[i] == L'\r' || w[i] == L'\n') w[i] = L' ';
    }
    return WideToUtf8(w);
}

const char* WsaErrorName(int code) {
    switch (code) {
        case WSAEINTR: return "WSAEINTR";
        case WSAEACCES: return "WSAEACCES";
        case WSAEINVAL: return "WSAEINVAL";
        case WSAEMFILE: return "WSAEMFILE";
        case WSAEWOULDBLOCK: return "WSAEWOULDBLOCK";
        case WSAENOTSOCK: return "WSAENOTSOCK";
        case WSAEMSGSIZE: return "WSAEMSGSIZE";
        case WSAEADDRINUSE: return "WSAEADDRINUSE";
        case WSAEADDRNOTAVAIL: return "WSAEADDRNOTAVAIL";
        case WSAENETDOWN: return "WSAENETDOWN";
        case WSAENETUNREACH: return "WSAENETUNREACH";
        case WSAECONNABORTED: return "WSAECONNABORTED";
        case WSAECONNRESET: return "WSAECONNRESET";
        case WSAENOBUFS: return "WSAENOBUFS";
        case WSAENOTCONN: return "WSAENOTCONN";
        case WSAESHUTDOWN: return "WSAESHUTDOWN";
        case WSAETIMEDOUT: return "WSAETIMEDOUT";
        case WSAECONNREFUSED: return "WSAECONNREFUSED";
        case WSAEHOSTUNREACH: return "WSAEHOSTUNREACH";
        case WSANOTINITIALISED: return "WSANOTINITIALISED";
        case WSAHOST_NOT_FOUND: return "WSAHOST_NOT_FOUND";
        case WSASYSNOTREADY: return "WSASYSNOTREADY";
        case WSAVERNOTSUPPORTED: return "WSAVERNOTSUPPORTED";
        default: return "WSA_UNKNOWN";
    }
}

std::string DescribeWsaError(const char* func, int code) {
    char head[160];
    std::snprintf(head, sizeof(head), "func=%s code=%d name=%s text=\"", func, code, WsaErrorName(code));
    return std::string(head) + SystemErrorText(code) + "\"";
}

WsaSession::WsaSession() : error_(0) {
    WSADATA wsa;
    // WSAStartup(2.2) — обязательная инициализация библиотеки Winsock до любых вызовов сокетов.
    error_ = WSAStartup(MAKEWORD(2, 2), &wsa);
}

WsaSession::~WsaSession() {
    // WSACleanup — освобождение ресурсов Winsock; вызывается только если WSAStartup удался.
    if (error_ == 0) WSACleanup();
}

void SocketHandle::Reset(SOCKET s) {
    if (s_ != INVALID_SOCKET) {
        closesocket(s_);  // закрыть сокет; результат здесь ни на что не влияет
    }
    s_ = s;
}

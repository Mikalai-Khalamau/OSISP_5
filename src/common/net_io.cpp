#include "net_io.h"
#include <algorithm>

bool SetSocketTimeouts(SOCKET s, int recvMs, int sendMs) {
    // В Windows SO_RCVTIMEO/SO_SNDTIMEO задаются как DWORD в миллисекундах.
    DWORD r = static_cast<DWORD>(recvMs < 0 ? 0 : recvMs);
    DWORD w = static_cast<DWORD>(sendMs < 0 ? 0 : sendMs);
    bool ok = setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&r), sizeof(r)) == 0;
    ok = (setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&w), sizeof(w)) == 0) && ok;
    return ok;
}

bool SetRecvTimeout(SOCKET s, int recvMs) {
    DWORD r = static_cast<DWORD>(recvMs < 1 ? 1 : recvMs);  // 0 означал бы «ждать вечно»
    return setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&r), sizeof(r)) == 0;
}

IoStatus SendAll(SOCKET s, const char* data, size_t len, size_t& sent, int& wsaErr) {
    sent = 0;
    wsaErr = 0;
    while (sent < len) {
        const size_t chunk = std::min<size_t>(len - sent, 1u << 20);
        int n = send(s, data + sent, static_cast<int>(chunk), 0);
        if (n == SOCKET_ERROR) {
            wsaErr = WSAGetLastError();
            return wsaErr == WSAETIMEDOUT ? IoStatus::Timeout : IoStatus::Error;
        }
        if (n <= 0) {  // теоретически невозможно, но защищаемся от бесконечного цикла
            wsaErr = WSAECONNRESET;
            return IoStatus::Error;
        }
        sent += static_cast<size_t>(n);  // продолжаем с того места, где остановился send()
    }
    return IoStatus::Ok;
}

IoStatus RecvSome(SOCKET s, char* buf, int cap, int& got, int& wsaErr) {
    got = 0;
    wsaErr = 0;
    int n = recv(s, buf, cap, 0);
    if (n == SOCKET_ERROR) {
        wsaErr = WSAGetLastError();
        return wsaErr == WSAETIMEDOUT ? IoStatus::Timeout : IoStatus::Error;
    }
    if (n == 0) return IoStatus::Closed;  // собеседник корректно закрыл соединение
    got = n;
    return IoStatus::Ok;
}

bool ConnectWithTimeout(SOCKET s, const sockaddr_in& addr, int timeoutMs, int& wsaErr) {
    wsaErr = 0;
    u_long nonBlocking = 1;
    if (ioctlsocket(s, FIONBIO, &nonBlocking) != 0) { wsaErr = WSAGetLastError(); return false; }

    int rc = connect(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    bool connected = false;
    if (rc == 0) {
        connected = true;
    } else {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) {
            wsaErr = err;
        } else {
            // Ждём завершения соединения: сокет станет «пишущим» (успех) или попадёт в except-набор (отказ).
            fd_set wset, eset;
            FD_ZERO(&wset);
            FD_ZERO(&eset);
            FD_SET(s, &wset);
            FD_SET(s, &eset);
            timeval tv;
            tv.tv_sec = timeoutMs / 1000;
            tv.tv_usec = (timeoutMs % 1000) * 1000;
            int sr = select(0, nullptr, &wset, &eset, &tv);
            if (sr == 0) {
                wsaErr = WSAETIMEDOUT;
            } else if (sr == SOCKET_ERROR) {
                wsaErr = WSAGetLastError();
            } else {
                int soErr = 0;
                int soLen = sizeof(soErr);
                getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soErr), &soLen);
                if (soErr != 0) wsaErr = soErr;
                else if (FD_ISSET(s, &eset)) wsaErr = WSAECONNREFUSED;
                else connected = true;
            }
        }
    }
    u_long blocking = 0;
    ioctlsocket(s, FIONBIO, &blocking);  // возвращаем блокирующий режим
    return connected;
}

std::string EndpointToString(const sockaddr_in& a) {
    char ip[INET_ADDRSTRLEN] = {0};
    if (inet_ntop(AF_INET, &a.sin_addr, ip, sizeof(ip)) == nullptr) return "?:0";
    return std::string(ip) + ":" + std::to_string(ntohs(a.sin_port));
}

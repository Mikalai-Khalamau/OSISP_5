// net_io.h — надёжные обёртки над send/recv/connect (учёт частичной отправки и таймаутов).
#pragma once
#include "platform.h"
#include <string>

enum class IoStatus {
    Ok,       // операция выполнена
    Closed,   // собеседник закрыл соединение (recv вернул 0)
    Timeout,  // истёк таймаут (WSAETIMEDOUT)
    Error     // другая ошибка Winsock, код в wsaErr
};

// Таймауты приёма/отправки (мс) через SO_RCVTIMEO / SO_SNDTIMEO. 0 = без таймаута.
bool SetSocketTimeouts(SOCKET s, int recvMs, int sendMs);
// Только таймаут приёма (мс, минимум 1).
bool SetRecvTimeout(SOCKET s, int recvMs);

// Отправить ВСЕ len байт. send() может отправить меньше, чем просили (частичная отправка),
// поэтому вызываем его в цикле, пока не уйдёт всё. sent — сколько реально ушло.
IoStatus SendAll(SOCKET s, const char* data, size_t len, size_t& sent, int& wsaErr);

// Один вызов recv(). TCP — поток байтов: за один вызов может прийти любая часть данных.
IoStatus RecvSome(SOCKET s, char* buf, int cap, int& got, int& wsaErr);

// connect() с таймаутом: неблокирующий connect + select. Блокирующий connect() в Windows
// не подчиняется SO_SNDTIMEO и может висеть ~20 секунд.
bool ConnectWithTimeout(SOCKET s, const sockaddr_in& addr, int timeoutMs, int& wsaErr);

// "127.0.0.1:50321"
std::string EndpointToString(const sockaddr_in& a);

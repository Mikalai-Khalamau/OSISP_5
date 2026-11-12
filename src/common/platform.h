// platform.h — единая точка подключения Windows-заголовков.
// ВАЖНО: <winsock2.h> обязан идти ДО <windows.h>, иначе возникнет конфликт со старым winsock.h.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN   // не тянуть из windows.h лишнее (в т.ч. старый winsock.h)
#endif
#ifndef NOMINMAX
#define NOMINMAX              // чтобы макросы min/max не ломали std::min/std::max
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

// Подключаем библиотеку Winsock 2 (дублируется в AdditionalDependencies проекта).
#pragma comment(lib, "ws2_32.lib")

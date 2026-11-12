// log.h — потокобезопасный журнал. Формат: [HH:MM:SS.mmm] РОЛЬ СОБЫТИЕ id=N поле=значение ...
#pragma once
#include "platform.h"
#include <cstdio>
#include <mutex>
#include <string>

class Logger {
public:
    Logger(const std::string& role, int id);
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // Дополнительно писать журнал в файл (дописывание). false — файл открыть не удалось.
    bool OpenFile(const std::string& path);
    // Тихий режим: на консоль выводятся только важные события (старт, стоп, ошибки, статистика).
    void SetQuiet(bool quiet) { quiet_ = quiet; }

    // Обычное событие (в тихом режиме на консоль не выводится, в файл пишется всегда).
    void Log(const char* event, const std::string& fields = std::string());
    // Важное событие (выводится всегда).
    void LogImportant(const char* event, const std::string& fields = std::string());
    // Ошибка Winsock: имя функции, код WSAGetLastError(), имя и текст FormatMessage.
    void LogWsaError(const char* func, int code, const std::string& extra = std::string());

private:
    void Write(const char* event, const std::string& fields, bool important);
    std::string role_;
    int id_;
    bool quiet_;
    std::mutex mutex_;
    FILE* file_;
};

// server.exe — упрощённый HTTP/1.1-сервер (лабораторная №5, вариант 27).
#include "../common/args.h"
#include "../common/log.h"
#include "../common/text_utils.h"
#include "http_server.h"
#include <atomic>
#include <cstdio>
#include <iostream>
#include <thread>

static std::atomic<bool> g_stop(false);  // флаг остановки (Ctrl+C / Ctrl+Break / закрытие окна / «q»)
static HANDLE g_doneEvent = nullptr;     // сигнал «сервер завершил работу» (для CTRL_CLOSE_EVENT)

// Обработчик консольных событий. Выполняется в отдельном потоке Windows: только выставляем флаг.
static BOOL WINAPI CtrlHandler(DWORD type) {
    switch (type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
            g_stop = true;
            return TRUE;
        case CTRL_CLOSE_EVENT:
            g_stop = true;
            // После возврата из обработчика Windows убивает процесс — даём main время завершиться.
            if (g_doneEvent) WaitForSingleObject(g_doneEvent, 8000);
            return TRUE;
        default:
            return FALSE;
    }
}

static void PrintHelp() {
    std::puts(
        "server.exe — упрощённый HTTP/1.1-сервер (GET, HEAD) для статических файлов\n"
        "\n"
        "Параметры:\n"
        "  --host <IP>        адрес прослушивания, по умолчанию 127.0.0.1 (только локально)\n"
        "  --port <N>         порт, по умолчанию 20113 (по заданию 20000-29999)\n"
        "  --www <каталог>    корневой каталог файлов, по умолчанию www\n"
        "  --timeout <мс>     лимит времени на получение запроса, по умолчанию 5000\n"
        "  --max-header <б>   лимит блока заголовков, по умолчанию 8192\n"
        "  --max-conn <N>     лимит одновременных соединений, по умолчанию 64\n"
        "  --id <N>           идентификатор участника для журнала, по умолчанию 0\n"
        "  --log <файл>       дополнительно писать журнал в файл\n"
        "  --quiet            минимум вывода в консоль\n"
        "  --help             эта справка\n"
        "\n"
        "Остановка: Ctrl+C / Ctrl+Break, либо ввод q + Enter.\n"
        "Пример: server.exe --host 127.0.0.1 --port 20113 --www www\n");
}

// Если каталог www не задан явно и не найден рядом с рабочим каталогом,
// пробуем <папка exe>\..\..\..\www (то есть корень проекта при запуске из bin\x64\Release).
static std::wstring DefaultWwwDir() {
    DWORD a = GetFileAttributesW(L"www");
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) return L"www";
    wchar_t exe[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"www";
    std::wstring p(exe, n);
    size_t slash = p.find_last_of(L'\\');
    if (slash == std::wstring::npos) return L"www";
    return p.substr(0, slash) + L"\\..\\..\\..\\www";
}

static int RealMain(int argc, char** argv) {
    EnableUtf8Console();

    Args args({"quiet", "help"}, {"host", "port", "www", "timeout", "max-header", "max-conn", "id", "log"});
    std::string err;
    if (!args.Parse(argc, argv, err)) {
        std::fprintf(stderr, "Ошибка параметров: %s\nСправка: server.exe --help\n", err.c_str());
        return 2;
    }
    if (args.Has("help")) { PrintHelp(); return 0; }

    ServerConfig cfg;
    long long v = 0;
    if (!args.GetInt("port", 20113, 1, 65535, v, err)) { std::fprintf(stderr, "Ошибка: %s\n", err.c_str()); return 2; }
    cfg.port = static_cast<int>(v);
    if (cfg.port < 20000 || cfg.port > 29999)
        std::fprintf(stderr, "Предупреждение: порт %d вне диапазона 20000-29999, указанного в задании.\n", cfg.port);
    if (!args.GetInt("timeout", 5000, 100, 600000, v, err)) { std::fprintf(stderr, "Ошибка: %s\n", err.c_str()); return 2; }
    cfg.timeoutMs = static_cast<int>(v);
    if (!args.GetInt("max-header", 8192, 128, 1048576, v, err)) { std::fprintf(stderr, "Ошибка: %s\n", err.c_str()); return 2; }
    cfg.maxHeader = static_cast<int>(v);
    if (!args.GetInt("max-conn", 64, 1, 10000, v, err)) { std::fprintf(stderr, "Ошибка: %s\n", err.c_str()); return 2; }
    cfg.maxConn = static_cast<int>(v);
    if (!args.GetInt("id", 0, 0, 1000000, v, err)) { std::fprintf(stderr, "Ошибка: %s\n", err.c_str()); return 2; }
    cfg.id = static_cast<int>(v);
    cfg.host = args.Get("host", "127.0.0.1");

    if (args.Has("www")) {
        if (!AcpToWide(args.Get("www", ""), cfg.www) || cfg.www.empty()) {
            std::fprintf(stderr, "Ошибка: некорректный путь --www\n");
            return 2;
        }
    } else {
        cfg.www = DefaultWwwDir();
    }

    Logger logger("SERVER", cfg.id);
    logger.SetQuiet(args.Has("quiet"));
    if (args.Has("log") && !logger.OpenFile(args.Get("log", ""))) {
        std::fprintf(stderr, "Ошибка: не удалось открыть файл журнала %s\n", args.Get("log", "").c_str());
        return 2;
    }

    g_doneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!SetConsoleCtrlHandler(CtrlHandler, TRUE))
        logger.LogImportant("WARN", "SetConsoleCtrlHandler failed, GetLastError=" + std::to_string(GetLastError()));

    // Дополнительный способ остановки: ввод "q" или "quit" + Enter. Если stdin закрыт (EOF) — поток
    // просто завершается и сервер продолжает работать.
    std::thread([] {
        std::string line;
        while (std::getline(std::cin, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (line == "q" || line == "quit") { g_stop = true; break; }
        }
    }).detach();

    HttpServer server(cfg, logger, g_stop);
    int rc = server.Run();
    if (g_doneEvent) SetEvent(g_doneEvent);
    return rc;
}

int main(int argc, char** argv) {
    try {
        return RealMain(argc, argv);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "Необработанное исключение: %s\n", ex.what());
    } catch (...) {
        std::fprintf(stderr, "Необработанное неизвестное исключение\n");
    }
    return 1;
}

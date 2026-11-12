// client.exe — HTTP-клиент на сокетах Winsock (лабораторная №5, вариант 27).
#include "../common/args.h"
#include "../common/log.h"
#include "../common/text_utils.h"
#include "http_client.h"
#include <cstdio>

static void PrintHelp() {
    std::puts(
        "client.exe — HTTP-клиент на сокетах (каждый запрос = новое TCP-соединение)\n"
        "\n"
        "Параметры:\n"
        "  --host <имя|IP>    адрес сервера, по умолчанию 127.0.0.1\n"
        "  --port <N>         порт, по умолчанию 20113\n"
        "  --id <N>           идентификатор клиента, по умолчанию 1\n"
        "  --timeout <мс>     таймаут connect/send/recv, по умолчанию 5000\n"
        "  --count <N>        число запросов, по умолчанию 1\n"
        "  --method <M>       GET | HEAD | любая строка (например POST), по умолчанию GET\n"
        "  --path <путь>      путь, по умолчанию /\n"
        "  --raw \"<запрос>\"   отправить произвольные байты как есть; escape: \\r \\n \\t \\\\ \\0 \\xHH\n"
        "  --run-id <N>       номер запуска (попадает в CSV), по умолчанию 0\n"
        "  --csv <файл>       дописывать метрики каждого запроса в CSV\n"
        "  --expect <код>     ожидаемый HTTP-статус; иное -> код возврата 4\n"
        "  --quiet            не печатать ответы и построчный журнал, только сводку\n"
        "  --print-body       печатать тело ответа (по умолчанию — только статус и заголовки)\n"
        "  --help             эта справка\n"
        "\n"
        "Коды возврата: 0 — без сетевых ошибок; 1 — были сетевые ошибки; 2 — неверные параметры;\n"
        "               3 — не удалось разрешить адрес; 4 — статус не равен --expect.\n"
        "Пример: client.exe --host 127.0.0.1 --port 20113 --id 2 --method GET --path /1kb.txt --count 100 --timeout 5000\n");
}

static int RealMain(int argc, char** argv) {
    EnableUtf8Console();
    Args args({"quiet", "print-body", "help"},
              {"host", "port", "id", "timeout", "count", "method", "path", "raw", "run-id", "csv", "expect"});
    std::string err;
    if (!args.Parse(argc, argv, err)) {
        std::fprintf(stderr, "Ошибка параметров: %s\nСправка: client.exe --help\n", err.c_str());
        return 2;
    }
    if (args.Has("help")) { PrintHelp(); return 0; }

    ClientConfig cfg;
    long long v = 0;
    if (!args.GetInt("port", 20113, 1, 65535, v, err)) { std::fprintf(stderr, "Ошибка: %s\n", err.c_str()); return 2; }
    cfg.port = static_cast<int>(v);
    if (!args.GetInt("id", 1, 0, 1000000, v, err)) { std::fprintf(stderr, "Ошибка: %s\n", err.c_str()); return 2; }
    cfg.id = static_cast<int>(v);
    if (!args.GetInt("timeout", 5000, 1, 600000, v, err)) { std::fprintf(stderr, "Ошибка: %s\n", err.c_str()); return 2; }
    cfg.timeoutMs = static_cast<int>(v);
    if (!args.GetInt("count", 1, 1, 10000000, v, err)) { std::fprintf(stderr, "Ошибка: %s\n", err.c_str()); return 2; }
    cfg.count = static_cast<int>(v);
    if (!args.GetInt("run-id", 0, 0, 1000000, v, err)) { std::fprintf(stderr, "Ошибка: %s\n", err.c_str()); return 2; }
    cfg.runId = static_cast<int>(v);
    if (!args.GetInt("expect", -1, 100, 599, v, err)) { std::fprintf(stderr, "Ошибка: %s\n", err.c_str()); return 2; }
    cfg.expect = static_cast<int>(v);
    cfg.host = args.Get("host", "127.0.0.1");
    cfg.method = args.Get("method", "GET");
    cfg.path = args.Get("path", "/");
    cfg.csvFile = args.Get("csv", "");
    cfg.quiet = args.Has("quiet");
    cfg.printBody = args.Has("print-body");
    if (cfg.method.empty() || cfg.path.empty()) { std::fprintf(stderr, "Ошибка: --method и --path не могут быть пустыми\n"); return 2; }
    if (args.Has("raw")) {
        if (!UnescapeRaw(args.Get("raw", ""), cfg.rawRequest, err)) { std::fprintf(stderr, "Ошибка: %s\n", err.c_str()); return 2; }
        cfg.useRaw = true;
    }

    Logger logger("CLIENT", cfg.id);
    logger.SetQuiet(cfg.quiet);
    HttpClient client(cfg, logger);
    return client.Run();
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

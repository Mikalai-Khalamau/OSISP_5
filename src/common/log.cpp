#include "log.h"
#include "winsock_utils.h"

Logger::Logger(const std::string& role, int id) : role_(role), id_(id), quiet_(false), file_(nullptr) {}

Logger::~Logger() {
    if (file_) std::fclose(file_);
}

bool Logger::OpenFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    file_ = std::fopen(path.c_str(), "ab");
    return file_ != nullptr;
}

void Logger::Write(const char* event, const std::string& fields, bool important) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char stamp[48];
    std::snprintf(stamp, sizeof(stamp), "[%02u:%02u:%02u.%03u] ", static_cast<unsigned>(st.wHour),
                  static_cast<unsigned>(st.wMinute), static_cast<unsigned>(st.wSecond),
                  static_cast<unsigned>(st.wMilliseconds));
    std::string line = stamp + role_ + " " + event + " id=" + std::to_string(id_);
    if (!fields.empty()) line += " " + fields;
    line += "\n";

    // Одна строка = один fwrite под мьютексом, поэтому строки разных потоков не перемешиваются.
    std::lock_guard<std::mutex> lock(mutex_);
    if (important || !quiet_) {
        std::fwrite(line.data(), 1, line.size(), stdout);
        std::fflush(stdout);
    }
    if (file_) {
        std::fwrite(line.data(), 1, line.size(), file_);
        std::fflush(file_);
    }
}

void Logger::Log(const char* event, const std::string& fields) { Write(event, fields, false); }
void Logger::LogImportant(const char* event, const std::string& fields) { Write(event, fields, true); }

void Logger::LogWsaError(const char* func, int code, const std::string& extra) {
    std::string f = DescribeWsaError(func, code);
    if (!extra.empty()) f += " " + extra;
    Write("ERROR", f, true);
}

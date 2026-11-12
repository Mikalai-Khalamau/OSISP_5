// path_guard.h — безопасное преобразование URL-пути в файл внутри каталога www.
#pragma once
#include "../common/platform.h"
#include <string>

// RAII-обёртка HANDLE файла.
class ScopedHandle {
public:
    ScopedHandle() : h_(INVALID_HANDLE_VALUE) {}
    explicit ScopedHandle(HANDLE h) : h_(h) {}
    ~ScopedHandle() { Reset(); }
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    HANDLE Get() const { return h_; }
    bool Valid() const { return h_ != INVALID_HANDLE_VALUE && h_ != nullptr; }
    void Reset(HANDLE h = INVALID_HANDLE_VALUE) {
        if (Valid()) CloseHandle(h_);
        h_ = h;
    }
private:
    HANDLE h_;
};

// Корень www в двух видах: обычный канонический путь и «финальный» (после раскрытия ссылок).
struct WwwRoot {
    std::wstring prefix;       // например C:\proj\www\   (всегда с завершающим '\')
    std::wstring finalPrefix;  // например \\?\C:\proj\www\  (из GetFinalPathNameByHandle)
};

// Канонизирует каталог www. false + текст ошибки, если каталога нет.
bool InitWwwRoot(const std::wstring& dir, WwwRoot& root, std::string& error);

enum class PathVerdict {
    Ok,          // путь безопасен, fullPath заполнен
    BadRequest,  // синтаксически некорректный путь -> 400
    NotFound     // путь недопустим/небезопасен -> 404
};

struct PathResult {
    PathVerdict verdict = PathVerdict::NotFound;
    std::wstring fullPath;  // канонический путь файла (только при Ok)
    std::string reason;     // причина (для журнала)
    bool security = false;  // true -> писать в журнал событие SECURITY
};

// Один раз декодирует %XX. false при «%» без двух hex-цифр.
bool PercentDecodeOnce(const std::string& in, std::string& out);

// Весь конвейер проверок (см. комментарий в path_guard.cpp).
PathResult ResolveTarget(const WwwRoot& root, const std::string& target);

enum class OpenVerdict { Ok, NotFound, Escape };

// Открывает файл и ПОСЛЕ открытия проверяет по GetFinalPathNameByHandleW, что он реально лежит внутри www
// (ловит символические ссылки и junction, ведущие наружу).
OpenVerdict OpenGuardedFile(const WwwRoot& root, const std::wstring& fullPath, ScopedHandle& file,
                            unsigned long long& size, std::string& reason);

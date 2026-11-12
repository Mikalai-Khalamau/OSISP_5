// path_guard.cpp — защита от выхода за пределы каталога www (ключевое условие варианта).
//
// РЕШЕНИЕ ПО КОДАМ ОТВЕТА (документируется здесь, как требует задание):
//  * 400 Bad Request — путь СИНТАКСИЧЕСКИ некорректен: плохое %-кодирование («%zz», «%» в конце),
//    NUL-байт (%00) и управляющие символы, невалидный UTF-8.
//  * 404 Not Found   — путь корректен по синтаксису, но ЗАПРЕЩЁН политикой безопасности:
//    «..», «.», пустые сегменты («//»), «\», «:» (диски, NTFS-потоки ::$DATA), имена устройств
//    (CON, NUL, COM1...), имя с пробелом/точкой на конце, слишком длинный путь, выход за www
//    после канонизации, ссылка/junction наружу. Клиент не должен отличать «нельзя» от «нет файла»,
//    чтобы не узнавать структуру диска. В журнал пишется событие SECURITY с причиной.
//
// АЛГОРИТМ: отбросить ?query и #fragment -> percent-decode РОВНО ОДИН РАЗ -> проверить символы и
// сегменты -> склеить с корнем www -> GetFullPathNameW -> проверить префикс «корень + \» ->
// (в OpenGuardedFile) открыть и сверить GetFinalPathNameByHandleW.
// Двойное кодирование (%252e%252e) после одного декодирования превращается в обычное имя «%2e%2e»,
// которое второй раз НЕ декодируется и просто не находится на диске.
#include "path_guard.h"
#include "../common/text_utils.h"
#include <cwchar>
#include <vector>

static std::wstring FinalPathOfHandle(HANDLE h) {
    DWORD n = GetFinalPathNameByHandleW(h, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (n == 0) return std::wstring();
    std::vector<wchar_t> buf(static_cast<size_t>(n) + 2);
    DWORD m = GetFinalPathNameByHandleW(h, buf.data(), static_cast<DWORD>(buf.size()),
                                        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (m == 0 || m >= buf.size()) return std::wstring();
    return std::wstring(buf.data(), m);
}

// Сравнение префикса без учёта регистра (в Windows пути нечувствительны к регистру).
// Префикс заканчивается на '\', поэтому «www_evil» НЕ принимается за «www».
static bool StartsWithNoCase(const std::wstring& s, const std::wstring& prefix) {
    return s.size() >= prefix.size() && _wcsnicmp(s.c_str(), prefix.c_str(), prefix.size()) == 0;
}

bool InitWwwRoot(const std::wstring& dir, WwwRoot& root, std::string& error) {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetFullPathNameW(dir.c_str(), static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])), buf, nullptr);
    if (n == 0 || n >= sizeof(buf) / sizeof(buf[0])) {
        error = "не удалось получить полный путь каталога www";
        return false;
    }
    std::wstring full(buf, n);
    DWORD attr = GetFileAttributesW(full.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        error = "каталог www не найден: " + WideToUtf8(full);
        return false;
    }
    root.prefix = full;
    if (root.prefix.back() != L'\\') root.prefix += L'\\';

    // Открываем каталог (FILE_FLAG_BACKUP_SEMANTICS обязателен для каталогов), чтобы узнать «финальный» путь.
    HANDLE h = CreateFileW(full.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        error = "не удалось открыть каталог www";
        return false;
    }
    ScopedHandle guard(h);
    std::wstring fin = FinalPathOfHandle(h);
    if (fin.empty()) {
        error = "GetFinalPathNameByHandle не сработал для каталога www";
        return false;
    }
    root.finalPrefix = fin;
    if (root.finalPrefix.back() != L'\\') root.finalPrefix += L'\\';
    return true;
}

static int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool PercentDecodeOnce(const std::string& in, std::string& out) {
    out.clear();
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '%') { out.push_back(in[i]); continue; }
        // Нужны два символа после '%': индексы i+1 и i+2 должны лежать внутри строки.
        if (i + 2 >= in.size()) return false;
        int hi = HexVal(in[i + 1]);
        int lo = HexVal(in[i + 2]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<char>(hi * 16 + lo));
        i += 2;
    }
    return true;
}

static bool IsReservedDeviceName(const std::wstring& seg) {
    // Имя устройства определяется частью до первой точки: «con.txt» — тоже устройство CON.
    std::wstring base = seg.substr(0, seg.find(L'.'));
    while (!base.empty() && base.back() == L' ') base.pop_back();
    for (size_t i = 0; i < base.size(); ++i)
        if (base[i] >= L'a' && base[i] <= L'z') base[i] = static_cast<wchar_t>(base[i] - L'a' + L'A');
    if (base == L"CON" || base == L"PRN" || base == L"AUX" || base == L"NUL" ||
        base == L"CONIN$" || base == L"CONOUT$") return true;
    if (base.size() == 4 && (base.compare(0, 3, L"COM") == 0 || base.compare(0, 3, L"LPT") == 0) &&
        base[3] >= L'1' && base[3] <= L'9') return true;
    return false;
}

static PathResult Fail(PathVerdict v, const char* reason, bool security) {
    PathResult r;
    r.verdict = v;
    r.reason = reason;
    r.security = security;
    return r;
}

PathResult ResolveTarget(const WwwRoot& root, const std::string& target) {
    // 1) Query (?...) и fragment (#...) к файлу отношения не имеют — отбрасываем ДО декодирования
    //    (закодированный %3F — это уже часть имени и отсеется проверкой запрещённых символов).
    std::string raw = target;
    size_t cut = raw.find_first_of("?#");
    if (cut != std::string::npos) raw.resize(cut);
    if (raw.empty() || raw[0] != '/') return Fail(PathVerdict::BadRequest, "path must start with '/'", false);

    // 2) Percent-decode ровно один раз.
    std::string decoded;
    if (!PercentDecodeOnce(raw, decoded)) return Fail(PathVerdict::BadRequest, "bad percent-encoding", true);

    // 3) NUL и управляющие символы после декодирования (%00, %0d, %0a ...).
    for (size_t i = 0; i < decoded.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(decoded[i]);
        if (c < 0x20 || c == 0x7F) return Fail(PathVerdict::BadRequest, "control character or NUL in path", true);
    }
    if (decoded.size() > 1024) return Fail(PathVerdict::NotFound, "path too long", true);

    // 4) Символы, недопустимые в пути: '\' (разделитель Windows), ':' (диск, NTFS ADS), маски и т.п.
    if (decoded.find_first_of("\\:*?\"<>|") != std::string::npos)
        return Fail(PathVerdict::NotFound, "forbidden character in path (\\ : * ? \" < > |)", true);

    std::wstring wide;
    if (!Utf8ToWide(decoded, wide)) return Fail(PathVerdict::BadRequest, "invalid UTF-8 in path", false);

    // 5) Разбор по сегментам. «/» -> index.html.
    std::vector<std::wstring> segs;
    if (decoded == "/") {
        segs.push_back(L"index.html");
    } else {
        std::wstring rest = wide.substr(1);
        size_t start = 0;
        for (;;) {
            size_t slash = rest.find(L'/', start);
            std::wstring seg = rest.substr(start, slash == std::wstring::npos ? std::wstring::npos : slash - start);
            const bool last = (slash == std::wstring::npos);
            if (seg.empty())
                return Fail(PathVerdict::NotFound, last ? "path ends with '/' (directory)" : "empty path segment ('//')", !last);
            if (seg == L"." || seg == L"..") return Fail(PathVerdict::NotFound, "'.' or '..' segment", true);
            if (seg.size() > 255) return Fail(PathVerdict::NotFound, "segment too long", true);
            if (seg.back() == L' ' || seg.back() == L'.')
                return Fail(PathVerdict::NotFound, "segment ends with space or dot", true);
            if (IsReservedDeviceName(seg)) return Fail(PathVerdict::NotFound, "reserved Windows device name", true);
            segs.push_back(seg);
            if (last) break;
            start = slash + 1;
        }
    }

    // 6) Склейка с корнем и канонизация.
    std::wstring joined = root.prefix;
    for (size_t i = 0; i < segs.size(); ++i) {
        joined += segs[i];
        if (i + 1 < segs.size()) joined += L'\\';
    }
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetFullPathNameW(joined.c_str(), static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])), buf, nullptr);
    if (n == 0 || n >= sizeof(buf) / sizeof(buf[0])) return Fail(PathVerdict::NotFound, "cannot canonicalize path", true);
    std::wstring full(buf, n);

    // 7) Главная проверка: канонический путь обязан начинаться с «корень\».
    if (!StartsWithNoCase(full, root.prefix))
        return Fail(PathVerdict::NotFound, "path escapes www root after canonicalization", true);

    PathResult ok;
    ok.verdict = PathVerdict::Ok;
    ok.fullPath = full;
    return ok;
}

OpenVerdict OpenGuardedFile(const WwwRoot& root, const std::wstring& fullPath, ScopedHandle& file,
                            unsigned long long& size, std::string& reason) {
    HANDLE h = CreateFileW(fullPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        reason = "open failed, GetLastError=" + std::to_string(GetLastError());
        return OpenVerdict::NotFound;
    }
    file.Reset(h);
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(h, &info)) { reason = "GetFileInformationByHandle failed"; return OpenVerdict::NotFound; }
    if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { reason = "is a directory"; return OpenVerdict::NotFound; }
    if (GetFileType(h) != FILE_TYPE_DISK) { reason = "not a disk file"; return OpenVerdict::NotFound; }

    // Защита от символических ссылок/junction: реальное место файла должно быть внутри www.
    std::wstring fin = FinalPathOfHandle(h);
    if (fin.empty() || !StartsWithNoCase(fin, root.finalPrefix)) {
        reason = "final path is outside www (symlink/junction?)";
        return OpenVerdict::Escape;
    }
    size = (static_cast<unsigned long long>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    return OpenVerdict::Ok;
}

#include "text_utils.h"
#include <cstdio>

bool Utf8ToWide(const std::string& s, std::wstring& out) {
    out.clear();
    if (s.empty()) return true;
    // MB_ERR_INVALID_CHARS — не «чинить» плохой UTF-8 молча, а вернуть ошибку.
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return false;
    out.resize(static_cast<size_t>(n));
    int m = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), &out[0], n);
    return m == n;
}

bool AcpToWide(const std::string& s, std::wstring& out) {
    out.clear();
    if (s.empty()) return true;
    int n = MultiByteToWideChar(CP_ACP, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return false;
    out.resize(static_cast<size_t>(n));
    int m = MultiByteToWideChar(CP_ACP, 0, s.data(), static_cast<int>(s.size()), &out[0], n);
    return m == n;
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &out[0], n, nullptr, nullptr);
    return out;
}

void EnableUtf8Console() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

std::string SanitizeForLog(const std::string& s, size_t maxLen) {
    std::string out;
    for (size_t i = 0; i < s.size() && out.size() < maxLen; ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c > 0x20 && c < 0x7F && c != '\\') {
            out.push_back(static_cast<char>(c));
        } else {
            char tmp[8];
            std::snprintf(tmp, sizeof(tmp), "\\x%02X", c);
            out += tmp;
        }
    }
    if (s.size() > maxLen) out += "...";
    return out;
}

std::string ToLowerAscii(std::string s) {
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] >= 'A' && s[i] <= 'Z') s[i] = static_cast<char>(s[i] - 'A' + 'a');
    }
    return s;
}

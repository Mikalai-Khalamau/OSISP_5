#include "timing.h"
#include <cstdio>

double NowMs() {
    // Частота счётчика не меняется во время работы системы — читаем один раз.
    static const double kFreq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) * 1000.0 / kFreq;
}

std::string HttpDateNow() {
    static const char* const kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    SYSTEMTIME st;
    GetSystemTime(&st);  // UTC (GMT)
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s, %02u %s %04u %02u:%02u:%02u GMT",
                  kDays[st.wDayOfWeek % 7], static_cast<unsigned>(st.wDay),
                  kMonths[(st.wMonth + 11) % 12], static_cast<unsigned>(st.wYear),
                  static_cast<unsigned>(st.wHour), static_cast<unsigned>(st.wMinute),
                  static_cast<unsigned>(st.wSecond));
    return buf;
}

std::string FormatDouble(double v, int precision) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", precision, v);  // printf в локали "C" — точка
    return buf;
}

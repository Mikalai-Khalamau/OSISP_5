// timing.h — измерение времени через QueryPerformanceCounter и форматирование дат.
#pragma once
#include "platform.h"
#include <string>

// Монотонное время в миллисекундах (с дробной частью).
// QueryPerformanceCounter возвращает «тики»; их частота — QueryPerformanceFrequency (тиков/с),
// поэтому мс = тики * 1000 / частота.
double NowMs();

// Секундомер на базе NowMs().
class Stopwatch {
public:
    Stopwatch() : start_(NowMs()) {}
    void Restart() { start_ = NowMs(); }
    double ElapsedMs() const { return NowMs() - start_; }
private:
    double start_;
};

// Текущее время в формате RFC 7231 (IMF-fixdate), всегда GMT: "Sun, 06 Nov 1994 08:49:37 GMT".
std::string HttpDateNow();

// Число -> строка с заданным количеством знаков после точки (десятичная ТОЧКА всегда).
std::string FormatDouble(double v, int precision);

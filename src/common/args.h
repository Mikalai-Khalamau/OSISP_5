// args.h — простой ручной разбор аргументов командной строки вида --имя значение / --имя=значение / --флаг.
#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>

class Args {
public:
    // flagNames — опции без значения (например "quiet"), valueNames — опции со значением (например "port").
    Args(const std::vector<std::string>& flagNames, const std::vector<std::string>& valueNames);
    // false — ошибка (неизвестная опция, нет значения); текст ошибки в err.
    bool Parse(int argc, char** argv, std::string& err);
    bool Has(const std::string& name) const;
    std::string Get(const std::string& name, const std::string& def) const;
    // Целое с проверкой диапазона. Если опция не задана — out = def и true.
    bool GetInt(const std::string& name, long long def, long long minV, long long maxV,
                long long& out, std::string& err) const;
private:
    std::set<std::string> flags_;
    std::set<std::string> values_;
    std::map<std::string, std::string> parsed_;
};

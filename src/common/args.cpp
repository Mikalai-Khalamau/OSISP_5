#include "args.h"
#include <cerrno>
#include <cstdlib>

Args::Args(const std::vector<std::string>& flagNames, const std::vector<std::string>& valueNames)
    : flags_(flagNames.begin(), flagNames.end()), values_(valueNames.begin(), valueNames.end()) {}

bool Args::Parse(int argc, char** argv, std::string& err) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.size() < 3 || a[0] != '-' || a[1] != '-') {
            err = "неожиданный аргумент: " + a + " (все опции начинаются с --)";
            return false;
        }
        std::string name = a.substr(2);
        std::string value;
        bool hasInline = false;
        size_t eq = name.find('=');
        if (eq != std::string::npos) {
            value = name.substr(eq + 1);
            name = name.substr(0, eq);
            hasInline = true;
        }
        if (flags_.count(name)) {
            if (hasInline) { err = "опция --" + name + " не принимает значение"; return false; }
            parsed_[name] = "1";
        } else if (values_.count(name)) {
            if (!hasInline) {
                if (i + 1 >= argc) { err = "для опции --" + name + " не указано значение"; return false; }
                value = argv[++i];
            }
            parsed_[name] = value;
        } else {
            err = "неизвестная опция: --" + name;
            return false;
        }
    }
    return true;
}

bool Args::Has(const std::string& name) const { return parsed_.count(name) != 0; }

std::string Args::Get(const std::string& name, const std::string& def) const {
    std::map<std::string, std::string>::const_iterator it = parsed_.find(name);
    return it == parsed_.end() ? def : it->second;
}

bool Args::GetInt(const std::string& name, long long def, long long minV, long long maxV,
                  long long& out, std::string& err) const {
    out = def;
    std::map<std::string, std::string>::const_iterator it = parsed_.find(name);
    if (it == parsed_.end()) return true;
    const std::string& s = it->second;
    char* end = nullptr;
    errno = 0;
    long long v = std::strtoll(s.c_str(), &end, 10);
    if (s.empty() || end == nullptr || *end != '\0' || errno == ERANGE) {
        err = "опция --" + name + ": '" + s + "' — не целое число";
        return false;
    }
    if (v < minV || v > maxV) {
        err = "опция --" + name + ": значение " + s + " вне допустимого диапазона [" +
              std::to_string(minV) + ", " + std::to_string(maxV) + "]";
        return false;
    }
    out = v;
    return true;
}

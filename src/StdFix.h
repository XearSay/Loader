#pragma once
#include <string>
#include <cstdio>

namespace std {

// Эмуляция std::format
template <typename... Args> std::string format(const char* fmt, Args... args) {
    char buffer[2048];
    snprintf(buffer, sizeof(buffer), fmt, args...);
    return std::string(buffer);
}

} // namespace std

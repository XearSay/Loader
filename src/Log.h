#pragma once
#include <string_view>
#include <string>
#include <cstdio>
#include "StdInclude.h"
#include "App.h"
#include "AppWindow.h"

namespace nl {

void LogWrite(std::string_view line);

// Простой лог
inline void Log(std::string_view message) {
    LogWrite(message);
}

// Форматированный лог (замена std::format)
template <typename... Args> inline void Log(const char* format, Args... args) {
    char buffer[2048];
    snprintf(buffer, sizeof(buffer), format, args...);
    LogWrite(std::string_view(buffer));
}

#ifdef _WIN32
void LogHr(std::string_view what, long hr);
#endif

// Форматирование строк (замена std::format)
template <typename... Args> inline std::string FormatString(const char* format, Args... args) {
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), format, args...);
    return std::string(buffer);
}

} // namespace nl

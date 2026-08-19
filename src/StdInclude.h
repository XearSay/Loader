#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <functional>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <optional>
#include <variant>
#include <filesystem>

// Эмуляция std::format
#include "StdFix.h"

// Windows
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>

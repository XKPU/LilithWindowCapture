#include "log.h"

#include <windows.h>
#include <cstdarg>
#include <cwchar>
#include <mutex>

namespace lilithwindowcapture {

namespace {

LogCallback g_callback = nullptr;
std::mutex g_mutex;

void WriteLine(const wchar_t* level, const wchar_t* text) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_callback) {
        g_callback(level, text);
    }
}

void FormatAndWrite(const wchar_t* level, const wchar_t* fmt, va_list args) {
    wchar_t text[1536];
    _vsnwprintf_s(text, _TRUNCATE, fmt, args);
    WriteLine(level, text);
}

} // namespace

void LogInit(LogCallback callback) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_callback = callback;
}

void LogShutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_callback = nullptr;
}

void LogMsg(const wchar_t* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    FormatAndWrite(L"INFO", fmt, args);
    va_end(args);
}

void LogVerbose(const wchar_t* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    FormatAndWrite(L"DEBUG", fmt, args);
    va_end(args);
}

void LogWarn(const wchar_t* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    FormatAndWrite(L"WARN", fmt, args);
    va_end(args);
}

} // namespace lilithwindowcapture

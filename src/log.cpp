#include "log.h"

#include <windows.h>
#include <cstdarg>
#include <cwchar>
#include <cstdio>
#include <mutex>
#include <string>

namespace lilithwindowcapture {

namespace {

std::mutex g_mutex;
HANDLE g_logFile = INVALID_HANDLE_VALUE;

const wchar_t* kLogFileName = L"LilithWindowCapture.log";

// 打开（或复用）游戏根目录下的日志文件。
void EnsureLogFile() {
    if (g_logFile != INVALID_HANDLE_VALUE) {
        return;
    }
    wchar_t dllPath[MAX_PATH] = {};
    DWORD n = GetModuleFileNameW(nullptr, dllPath, MAX_PATH);
    std::wstring dir;
    if (n > 0) {
        std::wstring p(dllPath, n);
        auto pos = p.find_last_of(L"\\/");
        dir = (pos == std::wstring::npos) ? L"." : p.substr(0, pos);
    } else {
        dir = L".";
    }
    std::wstring path = dir + L"\\" + kLogFileName;
    g_logFile = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void WriteLine(const wchar_t* level, const wchar_t* text) {
    std::lock_guard<std::mutex> lock(g_mutex);
    EnsureLogFile();
    if (g_logFile != INVALID_HANDLE_VALUE) {
        wchar_t ts[64] = {};
        SYSTEMTIME st = {};
        GetLocalTime(&st);
        _snwprintf_s(ts, _TRUNCATE, L"[%04d-%02d-%02d %02d:%02d:%02d]",
                     st.wYear, st.wMonth, st.wDay,
                     st.wHour, st.wMinute, st.wSecond);
        std::wstring line = ts;
        line += L" [";
        line += level;
        line += L"] ";
        line += text;
        line += L"\r\n";
        DWORD written = 0;
        WriteFile(g_logFile, line.c_str(),
                  static_cast<DWORD>(line.size() * sizeof(wchar_t)),
                  &written, nullptr);
    }
    OutputDebugStringW((std::wstring(L"[LilithWindowCapture] [") + level + L"] " + text + L"\n").c_str());
}

void FormatAndWrite(const wchar_t* level, const wchar_t* fmt, va_list args) {
    wchar_t text[1536];
    _vsnwprintf_s(text, _TRUNCATE, fmt, args);
    WriteLine(level, text);
}

} // namespace

void LogShutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_logFile != INVALID_HANDLE_VALUE) {
        CloseHandle(g_logFile);
        g_logFile = INVALID_HANDLE_VALUE;
    }
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

#include "log.h"

#include <windows.h>
#include <cstdarg>
#include <cwchar>
#include <cstdio>
#include <string>

namespace lilithwindowcapture {

namespace {

// 统一格式：[LilithWindowCapture] [级别] 正文
void WriteLine(const wchar_t* level, const wchar_t* text) {
    std::wstring line = L"[LilithWindowCapture] [";
    line += level;
    line += L"] ";
    line += text;
    line += L"\n";
    OutputDebugStringW(line.c_str());
}

void FormatAndWrite(const wchar_t* level, const wchar_t* fmt, va_list args) {
    wchar_t text[1536];
    _vsnwprintf_s(text, _TRUNCATE, fmt, args);
    WriteLine(level, text);
}

} // namespace

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

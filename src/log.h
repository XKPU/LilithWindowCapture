#pragma once

namespace lilithwindowcapture {

// 日志直接写入游戏根目录的 LilithWindowCapture.log 并输出到调试器（OutputDebugStringW），
void LogShutdown();

void LogMsg(const wchar_t* fmt, ...);
void LogVerbose(const wchar_t* fmt, ...);
void LogWarn(const wchar_t* fmt, ...);

} // namespace lilithwindowcapture

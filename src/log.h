#pragma once

#include <string>

namespace lilithwindowcapture {

// 日志回调：可选，由外部调用方通过 LilithWindowCapture_SetLogCallback 注册，
// 把本机日志额外转发到其日志系统。签名为 (level, message)，level 为 "INFO"/"WARN"/"DEBUG"。
// 若不注册，日志仍会写入游戏根目录的 LilithWindowCapture.log 并输出到调试器。
using LogCallback = void (*)(const wchar_t* level, const wchar_t* message);

// 注册可选的托管日志回调（可为 nullptr）。日志始终写文件，不受此影响。
void LogInit(LogCallback callback);
void LogShutdown();

void LogMsg(const wchar_t* fmt, ...);
void LogVerbose(const wchar_t* fmt, ...);
void LogWarn(const wchar_t* fmt, ...);

} // namespace lilithwindowcapture

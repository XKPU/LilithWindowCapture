#pragma once

#include <string>

namespace lilithwindowcapture {

// 日志回调：由 BepInEx 托管插件注册，把本机日志转发到 BepInEx 日志。
// 签名为 (level, message)，level 为 "INFO"/"WARN"/"DEBUG"。
using LogCallback = void (*)(const wchar_t* level, const wchar_t* message);

// 注册日志回调；不注册时日志仍通过 OutputDebugString 输出到调试器。
void LogInit(LogCallback callback);
void LogShutdown();

void LogMsg(const wchar_t* fmt, ...);
void LogVerbose(const wchar_t* fmt, ...);
void LogWarn(const wchar_t* fmt, ...);

} // namespace lilithwindowcapture

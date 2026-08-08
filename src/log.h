#pragma once

namespace lilithwindowcapture {

// 日志仅输出到调试器（OutputDebugStringW），不写入任何本地文件，
// 也不依赖任何外部组件或回调。可用 DebugView 等工具查看。
void LogMsg(const wchar_t* fmt, ...);
void LogVerbose(const wchar_t* fmt, ...);
void LogWarn(const wchar_t* fmt, ...);

} // namespace lilithwindowcapture

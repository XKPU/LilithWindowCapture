#include <windows.h>
#include <string>

#include "log.h"
#include "window_controller.h"
#include "version.h"

// 把 winmm 的全部 180 个导出转发到 winmm_orig.dll
#include "winmm_exports.h"

namespace lilithwindowcapture {

// 日志回调：由 BepInEx 托管插件通过 LilithWindowCapture_SetLogCallback 注册。
LogCallback g_logCallback = nullptr;

namespace {

HMODULE g_selfModule = nullptr;
HANDLE g_thread = nullptr;
volatile LONG g_stopRequested = 0;

DWORD WINAPI WorkerThread(LPVOID) {
    LogInit(g_logCallback);
    LogMsg(L"LilithWindowCapture v" LILITHWINDOWCAPTURE_VERSION_WSTR L" 已加载（日志转发至 BepInEx）");

    WindowController controller;
    WindowControllerAccessor::SetInstance(&controller);

    // 等待 Unity 主窗口创建完成（最多等 60 秒）
    const int kWaitStepMs = 200;
    int waited = 0;
    while (!InterlockedCompareExchange(&g_stopRequested, 0, 0)) {
        if (controller.EnsureWindow()) {
            break;
        }
        Sleep(kWaitStepMs);
        waited += kWaitStepMs;
        if (waited >= 60000) {
            LogWarn(L"等待 60 秒仍未找到游戏窗口");
            break;
        }
    }

    // 窗口出现后再等待 1 秒
    if (controller.EnsureWindow()) {
        Sleep(1000);
        controller.EnsureWindow();
    }

    // 直到收到停止信号（进程卸载）才还原样式并清空单例。
    while (!InterlockedCompareExchange(&g_stopRequested, 0, 0)) {
        Sleep(500);
    }

    controller.Shutdown();
    WindowControllerAccessor::SetInstance(nullptr);
    LogMsg(L"Mod 已卸载");
    LogShutdown();
    return 0;
}

} // namespace

void Startup(HMODULE self) {
    g_selfModule = self;
    g_thread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
}

void Cleanup() {
    InterlockedExchange(&g_stopRequested, 1);
    if (g_thread) {
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
}

// 供 BepInEx UI 插件（托管代码）跨模块调用的导出接口。
// 这些函数由 C# 侧通过 [DllImport("winmm")] 调用，实现对捕获模式的切换/查询。
extern "C" {

__declspec(dllexport) void LilithWindowCapture_SetCaptureMode(int enable) {
    auto* c = WindowControllerAccessor::Instance();
    if (c) {
        c->SetCaptureMode(enable != 0);
    }
}

__declspec(dllexport) int LilithWindowCapture_GetCaptureMode() {
    auto* c = WindowControllerAccessor::Instance();
    return (c && c->IsCaptureMode()) ? 1 : 0;
}

__declspec(dllexport) int LilithWindowCapture_IsReady() {
    return WindowControllerAccessor::IsReady() ? 1 : 0;
}

// 由 BepInEx 托管插件注册日志回调，把本机日志转发到 BepInEx 日志。
__declspec(dllexport) void LilithWindowCapture_SetLogCallback(LogCallback callback) {
    g_logCallback = callback;
    // 把回调同步给 log.cpp 内部的 g_callback
    LogInit(callback);
}

}

} // namespace lilithwindowcapture

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        lilithwindowcapture::Startup(hModule);
        break;
    case DLL_PROCESS_DETACH:
        lilithwindowcapture::Cleanup();
        break;
    default:
        break;
    }
    return TRUE;
}

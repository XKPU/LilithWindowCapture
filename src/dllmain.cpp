#include <windows.h>
#include <string>
#include <dbghelp.h>

#include "log.h"
#include "window_controller.h"
#include "version.h"

// 占位转发 stub：运行时由 PatchForwarding() 改写为跳板，转发到
// C:\Windows\System32\winmm.dll，因此不再需要随附 winmm_orig.dll。
#include "winmm_exports.h"

namespace lilithwindowcapture {

// 真实系统 winmm.dll（System32 副本）的句柄，由 PatchForwarding() 加载。
HMODULE g_origWinmm = nullptr;

// 日志回调：可选，由外部调用方通过 LilithWindowCapture_SetLogCallback 注册。
// 不注册时日志仍写文件，不影响诊断。
LogCallback g_logCallback = nullptr;

// 把本模块导出的 winmm 原生函数（占位 stub）改写为跳板：
//   mov rax, <realAddr>; jmp rax
// 从而把调用原样转发到真实系统 winmm.dll。本模块自身的
// LilithWindowCapture_* 导出不受影响。
// x64 无内联汇编，故在运行时把这段代码写入各 stub 的函数体。
void PatchForwarding() {
    if (g_origWinmm) {
        return; // 已 patch
    }
    // 优先加载 System32 副本，找不到则退而求其次加载 SysWOW64 副本。
    g_origWinmm = LoadLibraryW(L"C:\\Windows\\System32\\winmm.dll");
    if (!g_origWinmm) {
        g_origWinmm = LoadLibraryW(L"C:\\Windows\\SysWOW64\\winmm.dll");
    }
    if (!g_origWinmm) {
        return; // 两个目录均加载失败：占位 stub 不会被调用（游戏尚未 import），仅记录
    }

    HMODULE self = GetModuleHandleW(L"winmm.dll");
    if (!self) {
        return;
    }

    // 遍历本模块导出表，按名字取得每个导出 stub 的地址并写入跳板。
    ULONG sz = 0;
    auto* pExportDir = reinterpret_cast<PIMAGE_EXPORT_DIRECTORY>(
        ImageDirectoryEntryToData(self, TRUE, IMAGE_DIRECTORY_ENTRY_EXPORT, &sz));
    if (!pExportDir) {
        return;
    }

    auto* names = reinterpret_cast<ULONG*>(
        reinterpret_cast<BYTE*>(self) + pExportDir->AddressOfNames);
    auto* ordinals = reinterpret_cast<USHORT*>(
        reinterpret_cast<BYTE*>(self) + pExportDir->AddressOfNameOrdinals);
    auto* functions = reinterpret_cast<ULONG*>(
        reinterpret_cast<BYTE*>(self) + pExportDir->AddressOfFunctions);

    for (ULONG i = 0; i < pExportDir->NumberOfNames; ++i) {
        const char* name = reinterpret_cast<const char*>(
            reinterpret_cast<BYTE*>(self) + names[i]);

        // 跳过本模块自己的导出接口，只 patch winmm 原生转发 stub。
        if (strncmp(name, "LilithWindowCapture_", 19) == 0) {
            continue;
        }

        FARPROC real = GetProcAddress(g_origWinmm, name);
        if (!real) {
            continue; // 真实 dll 无此导出（理论上不应发生）
        }

        // stub 在本模块内的绝对地址（pExportDir->AddressOfFunctions 指向 stub 代码）
        ULONG ordinal = ordinals[i];
        BYTE* stub = reinterpret_cast<BYTE*>(self) + functions[ordinal];

        // trampoline: 48 B8 <8 字节绝对地址> FF E0  (mov rax, imm64; jmp rax)
        BYTE trampoline[13] = {
            0x48, 0xB8,
            static_cast<BYTE>(reinterpret_cast<UINT_PTR>(real) & 0xFF),
            static_cast<BYTE>((reinterpret_cast<UINT_PTR>(real) >> 8) & 0xFF),
            static_cast<BYTE>((reinterpret_cast<UINT_PTR>(real) >> 16) & 0xFF),
            static_cast<BYTE>((reinterpret_cast<UINT_PTR>(real) >> 24) & 0xFF),
            static_cast<BYTE>((reinterpret_cast<UINT_PTR>(real) >> 32) & 0xFF),
            static_cast<BYTE>((reinterpret_cast<UINT_PTR>(real) >> 40) & 0xFF),
            static_cast<BYTE>((reinterpret_cast<UINT_PTR>(real) >> 48) & 0xFF),
            static_cast<BYTE>((reinterpret_cast<UINT_PTR>(real) >> 56) & 0xFF),
            0xFF, 0xE0
        };

        DWORD oldProtect = 0;
        if (VirtualProtect(stub, sizeof(trampoline), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            CopyMemory(stub, trampoline, sizeof(trampoline));
            VirtualProtect(stub, sizeof(trampoline), oldProtect, &oldProtect);
            FlushInstructionCache(GetCurrentProcess(), stub, sizeof(trampoline));
        }
    }
}

namespace {

HANDLE g_thread = nullptr;
volatile LONG g_stopRequested = 0;

DWORD WINAPI WorkerThread(LPVOID) {
    LogInit(g_logCallback);
    LogMsg(L"LilithWindowCapture v" LILITHWINDOWCAPTURE_VERSION_WSTR L" 已加载（日志写入 LilithWindowCapture.log）");

    WindowController controller;
    WindowControllerAccessor::SetInstance(&controller);

    // 代理窗口在 worker 线程创建，其消息必须由本线程的消息泵分发。
    // 因此所有等待都用 MsgWaitForMultipleObjects，在等待间隙泵走窗口消息，
    // 否则 DwmRegisterThumbnail 向代理窗口发送的消息无人处理会挂起本线程，导致游戏无响应。
    auto PumpMessages = []() {
        MSG msg = {};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    };

    const DWORD kWaitStepMs = 200;

    // 等待 Unity 主窗口创建完成（最多等 60 秒）
    int waited = 0;
    while (!InterlockedCompareExchange(&g_stopRequested, 0, 0)) {
        if (controller.EnsureWindow()) {
            break;
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, kWaitStepMs, QS_ALLINPUT);
        PumpMessages();
        waited += kWaitStepMs;
        if (waited >= 60000) {
            LogWarn(L"等待 60 秒仍未找到游戏窗口");
            break;
        }
    }

    // 窗口已定位。永久开启窗口采集模式（无托盘/UI 开关）。
    // 代理窗口需等 DWM 缩略图源就绪才能安全显示，故开启后轮询重试，
    // 直到代理窗口真正可见或超时，避免过早创建导致灰屏/置顶。
    if (controller.EnsureWindow()) {
        controller.SetCaptureMode(true);

        const int kRetryStepMs = 500;
        int retryWaited = 0;
        while (!InterlockedCompareExchange(&g_stopRequested, 0, 0)) {
            if (controller.IsProxyVisible()) {
                break;
            }
            MsgWaitForMultipleObjects(0, nullptr, FALSE, kRetryStepMs, QS_ALLINPUT);
            PumpMessages();
            retryWaited += kRetryStepMs;
            if (retryWaited >= 30000) {
                LogWarn(L"窗口采集代理窗口在 30 秒内未能就绪，已停止重试");
                break;
            }
            // 重新定位窗口并重试应用（应对 Unity 尚未渲染完成的情况）
            controller.EnsureWindow();
            controller.SetCaptureMode(true);
        }

        // 启动阶段游戏窗口矩形仍可能变化（无边框→全屏、尺寸调整等），
        // 在代理窗口可见后持续 8 秒轮询其位置/大小并同步到代理窗口，校正错位。
        const int kTrackMs = 8000;
        int trackWaited = 0;
        while (!InterlockedCompareExchange(&g_stopRequested, 0, 0) && trackWaited < kTrackMs) {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, kRetryStepMs, QS_ALLINPUT);
            PumpMessages();
            controller.SyncProxyToGame();
            trackWaited += kRetryStepMs;
        }
        LogMsg(L"启动阶段窗口位置/大小跟踪已完成（8 秒）");
    }

    // 常驻：持续泵消息（保持代理窗口响应），直到收到停止信号（进程卸载）。
    while (!InterlockedCompareExchange(&g_stopRequested, 0, 0)) {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 200, QS_ALLINPUT);
        PumpMessages();
    }

    controller.Shutdown();
    WindowControllerAccessor::SetInstance(nullptr);
    LogMsg(L"Mod 已卸载");
    LogShutdown();
    return 0;
}

} // namespace

void Startup(HMODULE self) {
    (void)self;
    g_thread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
}

void Cleanup() {
    InterlockedExchange(&g_stopRequested, 1);
    if (g_thread) {
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
}

// 供外部调用方跨模块使用的接口。捕获模式现已永久开启（见 WorkerThread），
// 仅保留就绪查询与可选日志回调导出。
extern "C" {

__declspec(dllexport) int LilithWindowCapture_IsReady() {
    return WindowControllerAccessor::IsReady() ? 1 : 0;
}

// 由可选调用方注册日志回调，把本机日志额外转发到其日志系统。
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
        lilithwindowcapture::PatchForwarding();
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

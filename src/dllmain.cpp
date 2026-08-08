#include <windows.h>
#include <string>
#include <dbghelp.h>

#include "log.h"
#include "window_controller.h"
#include "version.h"

// 占位转发 stub：运行时由 PatchForwarding() 改写为跳板，转发到真实系统 winmm.dll。
#include "winmm_exports.h"

namespace lilithwindowcapture {

// 真实系统 winmm.dll 的句柄，由 PatchForwarding() 加载。
HMODULE g_origWinmm = nullptr;

// 把本模块导出的 winmm 原生函数（占位 stub）改写为跳板。
void PatchForwarding() {
    if (g_origWinmm) {
        return; // 已 patch
    }
    // 优先加载 System32 副本，找不到则加载 SysWOW64 副本。
    g_origWinmm = LoadLibraryW(L"C:\\Windows\\System32\\winmm.dll");
    if (!g_origWinmm) {
        g_origWinmm = LoadLibraryW(L"C:\\Windows\\SysWOW64\\winmm.dll");
    }
    if (!g_origWinmm) {
        return; // 两个目录均加载失败
    }

    HMODULE self = GetModuleHandleW(L"winmm.dll");
    if (!self) {
        return;
    }

    // 遍历本模块导出表
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
    LogMsg(L"LilithWindowCapture v" LILITHWINDOWCAPTURE_VERSION_WSTR L" 已加载（日志仅输出到调试器）");

    WindowController controller;
    WindowControllerAccessor::SetInstance(&controller);

    auto PumpMessages = []() {
        MSG msg = {};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    };

    const DWORD kWaitStepMs = 200;

    // 等待 Unity 主窗口创建完成（最多 60 秒）
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
            // 重新定位窗口并重试应用
            controller.EnsureWindow();
            controller.SetCaptureMode(true);
        }

        // 启动阶段游戏窗口矩形仍可能变化，
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

    while (!InterlockedCompareExchange(&g_stopRequested, 0, 0)) {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 200, QS_ALLINPUT);
        PumpMessages();
    }

    controller.Shutdown();
    WindowControllerAccessor::SetInstance(nullptr);
    LogMsg(L"Mod 已卸载");
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

extern "C" {

__declspec(dllexport) int LilithWindowCapture_IsReady() {
    return WindowControllerAccessor::IsReady() ? 1 : 0;
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

#include "window_controller.h"
#include "log.h"

#include <objbase.h>
#include <vector>

namespace lilithwindowcapture {

WindowController* WindowControllerAccessor::instance_ = nullptr;

namespace {

const wchar_t* kUnityClassName = L"UnityWndClass";

struct EnumContext {
    DWORD pid;
    std::vector<HWND> candidates;
};

BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<EnumContext*>(lParam);

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ctx->pid) {
        return TRUE;
    }

    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }

    wchar_t cls[256] = {0};
    GetClassNameW(hwnd, cls, ARRAYSIZE(cls));
    if (wcscmp(cls, kUnityClassName) != 0) {
        return TRUE;
    }

    RECT rc;
    if (!GetWindowRect(hwnd, &rc)) {
        return TRUE;
    }

    // 过滤零尺寸的辅助窗口
    if ((rc.right - rc.left) <= 1 || (rc.bottom - rc.top) <= 1) {
        return TRUE;
    }

    ctx->candidates.push_back(hwnd);
    return TRUE;
}

} // namespace

HWND FindGameWindow() {
    EnumContext ctx;
    ctx.pid = GetCurrentProcessId();

    EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&ctx));

    if (ctx.candidates.empty()) {
        return nullptr;
    }

    // 多个候选时取面积最大的
    HWND best = nullptr;
    long long bestArea = -1;
    for (HWND h : ctx.candidates) {
        RECT rc;
        if (!GetWindowRect(h, &rc)) {
            continue;
        }
        long long area = static_cast<long long>(rc.right - rc.left) *
                         static_cast<long long>(rc.bottom - rc.top);
        if (area > bestArea) {
            bestArea = area;
            best = h;
        }
    }

    return best;
}

WindowController::WindowController() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    comInitialized_ = SUCCEEDED(hr);
    if (!comInitialized_ && hr != RPC_E_CHANGED_MODE) {
        LogWarn(L"CoInitializeEx 失败: 0x%08X", hr);
    }
}

WindowController::~WindowController() {
    if (taskbarList_) {
        taskbarList_->Release();
        taskbarList_ = nullptr;
    }
    if (comInitialized_) {
        CoUninitialize();
    }
}

bool WindowController::EnsureWindow() {
    if (hwnd_ && IsWindow(hwnd_)) {
        return true;
    }

    if (hwnd_) {
    LogMsg(L"窗口句柄失效，重新定位...");
        snapshotTaken_ = false;
        hwnd_ = nullptr;
    }

    HWND found = FindGameWindow();
    if (!found) {
        return false;
    }

    hwnd_ = found;
    LogWindowState(L"已定位游戏窗口");

    // 窗口重建后若之前处于捕获模式，重新应用
    if (captureMode_) {
        TakeSnapshot();
        ApplyCaptureMode();
    }

    return true;
}

void WindowController::TakeSnapshot() {
    if (snapshotTaken_ || !hwnd_) {
        return;
    }

    originalStyle_ = GetWindowLongPtrW(hwnd_, GWL_STYLE);
    originalExStyle_ = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);

    originalLayeredValid_ = false;
    if (originalExStyle_ & WS_EX_LAYERED) {
        originalLayeredValid_ = GetLayeredWindowAttributes(
            hwnd_, &originalColorKey_, &originalAlpha_, &originalLayeredFlags_) != FALSE;
    }

    snapshotTaken_ = true;

    LogVerbose(L"样式快照: Style=0x%08llX ExStyle=0x%08llX "
               L"LayeredValid=%d ColorKey=0x%08X Alpha=%u Flags=0x%X",
               static_cast<unsigned long long>(originalStyle_),
               static_cast<unsigned long long>(originalExStyle_),
               originalLayeredValid_ ? 1 : 0,
               originalColorKey_, originalAlpha_, originalLayeredFlags_);
}

void WindowController::Toggle() {
    SetCaptureMode(!captureMode_);
}

void WindowController::SetCaptureMode(bool enabled) {
    if (!EnsureWindow()) {
        LogWarn(L"未找到游戏窗口，无法切换");
        return;
    }

    if (enabled == captureMode_) {
        return;
    }

    TakeSnapshot();

    if (enabled) {
        ApplyCaptureMode();
    } else {
        RestoreOriginalMode();
    }

    captureMode_ = enabled;

    if (enabled) {
        LogMsg(L">>> 窗口采集模式已开启：在任意窗口采集工具中选择 [Lilith.exe]: Lilith");
    } else {
        LogMsg(L">>> 窗口采集模式已关闭：已恢复桌宠原生样式");
    }
}

void WindowController::ApplyCaptureMode() {
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);

    // 去掉 TOOLWINDOW
    exStyle &= ~static_cast<LONG_PTR>(WS_EX_TOOLWINDOW);

    // 显式加 APPWINDOW
    exStyle |= static_cast<LONG_PTR>(WS_EX_APPWINDOW);

    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, exStyle);

    // 强制刷新非客户区让样式生效
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                 SWP_NOACTIVATE | SWP_FRAMECHANGED);

    // AddTab
    AddTaskbarTab();

    LogWindowState(L"捕获模式已应用");
}

void WindowController::RestoreOriginalMode() {
    if (!snapshotTaken_) {
        return;
    }

    RemoveTaskbarTab();

    SetWindowLongPtrW(hwnd_, GWL_STYLE, originalStyle_);
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, originalExStyle_);

    if (originalLayeredValid_) {
        SetLayeredWindowAttributes(hwnd_, originalColorKey_,
                                   originalAlpha_, originalLayeredFlags_);
    }

    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                 SWP_NOACTIVATE | SWP_FRAMECHANGED);

    LogWindowState(L"已还原原始样式");
}

void WindowController::AddTaskbarTab() {
    if (!hwnd_ || !IsWindow(hwnd_)) {
        return;
    }
    if (!taskbarList_) {
        HRESULT hr = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_ITaskbarList,
                                      reinterpret_cast<void**>(&taskbarList_));
        if (FAILED(hr) || !taskbarList_) {
        LogWarn(L"创建 ITaskbarList 失败: 0x%08X", hr);
            taskbarList_ = nullptr;
            return;
        }
        hr = taskbarList_->HrInit();
        if (FAILED(hr)) {
        LogWarn(L"ITaskbarList::HrInit 失败: 0x%08X", hr);
        }
    }

    // 重试机制
    const int kMaxTries = 20;
    for (int i = 0; i < kMaxTries; ++i) {
        HRESULT hr = taskbarList_->AddTab(hwnd_);
        if (SUCCEEDED(hr)) {
            return;
        }
        Sleep(500);
    }
    LogWarn(L"AddTaskbarTab 重试 %d 次仍失败",
            kMaxTries);
}

void WindowController::RemoveTaskbarTab() {
    if (!hwnd_ || !IsWindow(hwnd_) || !taskbarList_) {
        return;
    }
    // 同样重试
    const int kMaxTries = 10;
    for (int i = 0; i < kMaxTries; ++i) {
        HRESULT hr = taskbarList_->DeleteTab(hwnd_);
        if (SUCCEEDED(hr)) {
            return;
        }
        Sleep(300);
    }
    LogWarn(L"RemoveTaskbarTab 重试失败");
}

void WindowController::Shutdown() {
    if (captureMode_) {
        SetCaptureMode(false);
    }
}

void WindowController::LogWindowState(const wchar_t* prefix) {
    if (!hwnd_ || !IsWindow(hwnd_)) {
        LogVerbose(L"%s: <invalid>", prefix);
        return;
    }

    wchar_t cls[256] = {0};
    wchar_t title[256] = {0};
    GetClassNameW(hwnd_, cls, ARRAYSIZE(cls));
    GetWindowTextW(hwnd_, title, ARRAYSIZE(title));

    RECT rc = {0, 0, 0, 0};
    GetWindowRect(hwnd_, &rc);

    LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_STYLE);
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);

    LogMsg(L"%s: HWND=0x%llX Class=%s Title=%s Rect=(%d,%d,%dx%d) "
           L"Style=0x%08llX ExStyle=0x%08llX",
           prefix,
           reinterpret_cast<unsigned long long>(hwnd_),
           cls, title,
           rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
           static_cast<unsigned long long>(style),
           static_cast<unsigned long long>(exStyle));
}

} // namespace lilithwindowcapture

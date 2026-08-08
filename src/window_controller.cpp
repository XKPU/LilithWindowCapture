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

    // 多个候选时取面积最大的（主渲染窗口）
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
        // 原文：CoInitializeEx 失败
        LogWarn(L"CoInitializeEx \xE5\xA4\xB1\xE8\xB4\xA5: 0x%08X", hr);
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
        // 原文：窗口句柄失效，重新定位...
    LogMsg(L"\xE7\xAA\x97\xE5\x8F\xA3\xE5\x8F\xA5\xE6\x9F\x84\xE5\xA4\xB1\xE6\x95\x88\xEF\xBC\x8C\xE9\x87\x8D\xE6\x96\xB0\xE5\xAE\x9A\xE4\xBD\x8D...");
        snapshotTaken_ = false;
        hwnd_ = nullptr;
    }

    HWND found = FindGameWindow();
    if (!found) {
        return false;
    }

    hwnd_ = found;
    // 原文：已定位游戏窗口
    LogWindowState(L"\xE5\xB7\xB2\xE5\xAE\x9A\xE4\xBD\x8D\xE6\xB8\xB8\xE6\x88\x8F\xE7\xAA\x97\xE5\x8F\xA3");

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

    // 原文：样式快照: Style=... ExStyle=... LayeredValid=... ColorKey=... Alpha=... Flags=...
    LogVerbose(L"\xE6\xA0\xB7\xE5\xBC\x8F\xE5\xBF\xAB\xE7\x85\xA7: Style=0x%08llX ExStyle=0x%08llX "
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
        // 原文：未找到游戏窗口，无法切换
        LogWarn(L"\xE6\x9C\xAA\xE6\x89\xBE\xE5\x88\xB0\xE6\xB8\xB8\xE6\x88\x8F\xE7\xAA\x97\xE5\x8F\xA3\xEF\xBC\x8C\xE6\x97\xA0\xE6\xB3\x95\xE5\x88\x87\xE6\x8D%A2");
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
        // 原文：>>> OBS 捕获模式已开启：在 OBS 窗口捕获中选择 [Lilith.exe]: Lilith
        LogMsg(L">>> OBS \xE6\x8D\x95\xE8\x8E\xB7\xE6\xA8\xA1\xE5\xBC\x8F\xE5\xB7\xB2\xE5\xBC\x80\xE5\x90\xAF"
               L"\xEF\xBC\x9A\xE5\x9C\xA8 OBS \xE7\xAA\x97\xE5\x8F\xA3\xE6\x8D\x95\xE8\x8E\xB7\xE4\xB8\xAD\xE9\x80\x89\xE6\x8B\xA9 [Lilith.exe]: Lilith");
    } else {
        // 原文：>>> OBS 捕获模式已关闭：已恢复桌宠原生样式
        LogMsg(L">>> OBS \xE6\x8D\x95\xE8\x8E\xB7\xE6\xA8\xA1\xE5\xBC\x8F\xE5\xB7\xB2\xE5\x85\xB3\xE9\x97\xAD"
               L"\xEF\xBC\x9A\xE5\xB7\xB2\xE6\x81\xA2\xE5\xA4\x8D\xE6\xA1\x8C\xE5\xAE\xA0\xE5\x8E\x9F\xE7\x94\x9F\xE6\xA0\xB7\xE5\xBC\x8F");
    }
}

void WindowController::ApplyCaptureMode() {
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);

    // 核心：去掉 TOOLWINDOW，OBS 才能在窗口列表里看到它
    exStyle &= ~static_cast<LONG_PTR>(WS_EX_TOOLWINDOW);

    // 显式加 APPWINDOW：Explorer 只为"无 owner 且带 APPWINDOW 的顶层窗口"建任务栏按钮。
    // 仅去 TOOLWINDOW 实测 AddTab 返回成功但按钮不出现，必须配合 APPWINDOW。
    exStyle |= static_cast<LONG_PTR>(WS_EX_APPWINDOW);

    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, exStyle);

    // 强制刷新非客户区让样式生效
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                 SWP_NOACTIVATE | SWP_FRAMECHANGED);

    // 强制让 Explorer 在任务栏创建本窗口按钮
    AddTaskbarTab();

    // 原文：捕获模式已应用
    LogWindowState(L"\xE6\x8D\x95\xE8\x8E\xB7\xE6\xA8\xA1\xE5\xBC%8F%E5%B7%B2%E5%BA%94%E7%94%A8");
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

    // 原文：已还原原始样式
    LogWindowState(L"\xE5\xB7\xB2\xE8\xBF\x98\xE5\x8E\x9F\xE5\x8E\x9F\xE5\xA7\x8B\xE6\xA0\xB7\xE5\xBC\x8F");
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
            // 原文：创建 ITaskbarList 失败
        LogWarn(L"\xE5\x88\x9B\xE5\xBB\xBA ITaskbarList \xE5\xA4\xB1\xE8\xB4\xA5: 0x%08X", hr);
            taskbarList_ = nullptr;
            return;
        }
        hr = taskbarList_->HrInit();
        if (FAILED(hr)) {
            // 原文：ITaskbarList::HrInit 失败
        LogWarn(L"ITaskbarList::HrInit \xE5\xA4\xB1\xE8\xB4\xA5: 0x%08X", hr);
        }
    }

    // Explorer 在窗口/任务栏尚未完全就绪时 AddTab 会失败（hr!=S_OK），
    // 且刚启动时尤为明显。失败则延时重试，最多约 10 秒，直到成功。
    const int kMaxTries = 20;
    for (int i = 0; i < kMaxTries; ++i) {
        HRESULT hr = taskbarList_->AddTab(hwnd_);
        if (SUCCEEDED(hr)) {
            return;
        }
        Sleep(500);
    }
    LogWarn(L"AddTaskbarTab \xE9\x87%8D\xE8%AF\x95 %d \xE6%AC%A1\xE4%BB%8D\xE5%A4%B1\xE8%B4%A5",
            kMaxTries);
}

void WindowController::RemoveTaskbarTab() {
    if (!hwnd_ || !IsWindow(hwnd_) || !taskbarList_) {
        return;
    }
    // 同样重试，避免关闭时 Explorer 尚未就绪导致按钮残留
    const int kMaxTries = 10;
    for (int i = 0; i < kMaxTries; ++i) {
        HRESULT hr = taskbarList_->DeleteTab(hwnd_);
        if (SUCCEEDED(hr)) {
            return;
        }
        Sleep(300);
    }
    LogWarn(L"RemoveTaskbarTab \xE9\x87%8D\xE8%AF\x95\xE5%A4%B1\xE8%B4%A5");
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

#include "window_controller.h"
#include "log.h"

#include <dwmapi.h>
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

WindowController::WindowController() = default;

WindowController::~WindowController() = default;

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

void WindowController::SetCaptureMode(bool enabled) {
    if (!EnsureWindow()) {
        LogWarn(L"未找到游戏窗口，无法切换");
        return;
    }

    // enabled 时即使已处于捕获模式也重新应用，以支持 DWM 源未就绪后的重试；
    // disabled 时若状态一致则跳过。
    if (enabled != captureMode_) {
        TakeSnapshot();
        if (enabled) {
            ApplyCaptureMode();
        } else {
            RestoreOriginalMode();
        }
        captureMode_ = enabled;

        if (enabled) {
            LogMsg(L">>> 窗口采集模式已开启：在任意窗口采集工具中选择窗口 [Lilith Capture Proxy]");
        } else {
            LogMsg(L">>> 窗口采集模式已关闭：已销毁代理窗口，恢复桌宠原生样式");
        }
    } else if (enabled) {
        // 已开启但代理窗口可能尚未显示（DWM 源未就绪），重试应用。
        ApplyCaptureMode();
    }
}

void WindowController::ApplyCaptureMode() {
    // 创建一个独立代理窗口
    CreateProxyWindow();
    if (UpdateThumbnail()) {
        if (proxyHwnd_ && IsWindow(proxyHwnd_)) {
            ShowWindow(proxyHwnd_, SW_SHOW);
            // 窗口位于屏幕右侧外。
            SetWindowPos(proxyHwnd_, nullptr, 0, 0, 0, 0,
                         SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
        }
        LogWindowState(L"捕获模式已应用（DWM 缩略图代理窗口，位于屏幕外）");
    } else {
        LogWarn(L"捕获模式应用暂缓：DWM 缩略图尚未就绪，将在下次重试");
    }
}

LRESULT CALLBACK WindowController::ProxyWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // DWM 缩略图由桌面窗口管理器自动实时合成到本窗口客户区。
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps = {0};
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void WindowController::CreateProxyWindow() {
    if (proxyHwnd_ && IsWindow(proxyHwnd_)) {
        return;
    }
    static const wchar_t* kCls = L"LilithWindowCaptureProxy";
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = ProxyWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        wc.lpszClassName = kCls;
        RegisterClassExW(&wc);
        registered = true;
    }

    // 代理窗口尺寸跟随游戏窗口，
    RECT rc = {0, 0, 0, 0};
    if (hwnd_ && IsWindow(hwnd_)) {
        GetWindowRect(hwnd_, &rc);
    }
    int w = (rc.right - rc.left) > 0 ? (rc.right - rc.left) : 320;
    int h = (rc.bottom - rc.top) > 0 ? (rc.bottom - rc.top) : 240;

    // 放到主显示器右侧之外。
    int offX = GetSystemMetrics(SM_CXSCREEN) + 1;
    int offY = 0;

    proxyHwnd_ = CreateWindowExW(
        WS_EX_NOACTIVATE,
        // 明确告知后台窗口
        kCls, L"Lilith Capture Proxy", WS_POPUP,
        offX, offY, w, h,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

    if (proxyHwnd_) {
        // 将 DWM 帧区域扩展至整个客户区
        MARGINS margins = { -1, -1, -1, -1 };
        HRESULT hr = DwmExtendFrameIntoClientArea(proxyHwnd_, &margins);
        if (FAILED(hr)) {
            LogWarn(L"DwmExtendFrameIntoClientArea 失败: 0x%08X（代理窗口底色将显示为纯黑）",
                    static_cast<unsigned>(hr));
        }
    }
}

bool WindowController::UpdateThumbnail() {
    if (!proxyHwnd_ || !hwnd_ || !IsWindow(proxyHwnd_) || !IsWindow(hwnd_)) {
        return false;
    }

    // 仅在缩略图句柄失效时重新注册；位置变化由 DWM 自动跟随源窗口。
    if (!thumbnail_) {
        HRESULT hr = DwmRegisterThumbnail(proxyHwnd_, hwnd_, &thumbnail_);
        if (FAILED(hr) || !thumbnail_) {
            LogWarn(L"DwmRegisterThumbnail 失败: 0x%08X", static_cast<unsigned>(hr));
            return false;
        }
    }

    RECT dest = {0, 0, 0, 0};
    GetClientRect(proxyHwnd_, &dest);
    RECT src = {0, 0, 0, 0};
    GetClientRect(hwnd_, &src);

    DWM_THUMBNAIL_PROPERTIES props = {0};
    props.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_RECTSOURCE | DWM_TNP_VISIBLE | DWM_TNP_OPACITY;
    props.fVisible = TRUE;
    props.opacity = 255;
    props.rcDestination = dest;
    props.rcSource = src;

    HRESULT hr = DwmUpdateThumbnailProperties(thumbnail_, &props);
    if (FAILED(hr)) {
        LogWarn(L"DwmUpdateThumbnailProperties 失败: 0x%08X", static_cast<unsigned>(hr));
        return false;
    }
    return true;
}

void WindowController::RestoreOriginalMode() {
    DestroyProxyWindow();
}

void WindowController::SyncProxyToGame() {
    if (!proxyHwnd_ || !hwnd_ || !IsWindow(proxyHwnd_) || !IsWindow(hwnd_)) {
        return;
    }

    RECT rc = {0, 0, 0, 0};
    GetWindowRect(hwnd_, &rc);

    // 矩形未变化则跳过，避免每帧无谓的 SetWindowPos / UpdateThumbnail。
    if (rc.left == lastGameRect_.left && rc.top == lastGameRect_.top &&
        rc.right == lastGameRect_.right && rc.bottom == lastGameRect_.bottom) {
        return;
    }
    lastGameRect_ = rc;

    int w = (rc.right - rc.left) > 0 ? (rc.right - rc.left) : 320;
    int h = (rc.bottom - rc.top) > 0 ? (rc.bottom - rc.top) : 240;

    // 跟随游戏窗口大小，
    int offX = GetSystemMetrics(SM_CXSCREEN) + 1;
    SetWindowPos(proxyHwnd_, nullptr, offX, 0, w, h,
                 SWP_NOACTIVATE);

    // 目标矩形已变化，刷新 DWM 缩略图使其匹配新尺寸。
    UpdateThumbnail();

    // 仅在详细日志下输出，避免启动阶段频繁变化时刷屏与多余系统调用。
    LogVerbose(L"代理窗口已跟随游戏窗口矩形更新");
}

void WindowController::DestroyProxyWindow() {
    if (thumbnail_) {
        DwmUnregisterThumbnail(thumbnail_);
        thumbnail_ = nullptr;
    }
    if (proxyHwnd_ && IsWindow(proxyHwnd_)) {
        DestroyWindow(proxyHwnd_);
    }
    proxyHwnd_ = nullptr;
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
    
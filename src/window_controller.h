#pragma once

#include <windows.h>
#include <dwmapi.h>

namespace lilithwindowcapture {

// 定位本进程的 Unity 主渲染窗口
HWND FindGameWindow();

// 创建DWM 缩略图镜像，
class WindowController {
public:
    WindowController();
    ~WindowController();

    // 确保已绑定有效窗口
    bool EnsureWindow();

    void SetCaptureMode(bool enabled);
    void Shutdown();

    // 将代理窗口的大小同步到当前游戏窗口矩形。
    void SyncProxyToGame();

    // 代理窗口是否已被创建并显示（用于 worker 线程判断就绪，避免过早创建灰屏）。
    bool IsProxyVisible() const {
        return proxyHwnd_ != nullptr && IsWindow(proxyHwnd_) != 0 &&
               (GetWindowLongPtrW(proxyHwnd_, GWL_STYLE) & WS_VISIBLE) != 0;
    }

private:
    void TakeSnapshot();
    void ApplyCaptureMode();
    void RestoreOriginalMode();
    void LogWindowState(const wchar_t* prefix);

    static LRESULT CALLBACK ProxyWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void CreateProxyWindow();
    void DestroyProxyWindow();
    bool UpdateThumbnail();

    HWND hwnd_ = nullptr;
    bool captureMode_ = false;

    // 代理窗口。
    HWND proxyHwnd_ = nullptr;
    // DWM 缩略图句柄。
    HTHUMBNAIL thumbnail_ = nullptr;

    // 最近一次同步到的游戏窗口矩形，用于判断是否需要重新定位代理窗口。
    RECT lastGameRect_ = {0, 0, 0, 0};

    // 原始样式快照
    bool snapshotTaken_ = false;
    LONG_PTR originalStyle_ = 0;
    LONG_PTR originalExStyle_ = 0;
    bool originalLayeredValid_ = false;
    COLORREF originalColorKey_ = 0;
    BYTE originalAlpha_ = 255;
    DWORD originalLayeredFlags_ = 0;
};

// 全局单例访问器：供 dllmain.cpp 中的导出函数查询就绪状态（见 LilithWindowCapture_IsReady）
class WindowControllerAccessor {
public:
    static WindowController* Instance() { return instance_; }
    static void SetInstance(WindowController* c) { instance_ = c; }
    static bool IsReady() { return instance_ != nullptr; }

private:
    static WindowController* instance_;
};

} // namespace lilithwindowcapture

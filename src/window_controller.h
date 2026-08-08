#pragma once

#include <windows.h>
#include <dwmapi.h>

namespace lilithwindowcapture {

// 定位本进程的 Unity 主渲染窗口
HWND FindGameWindow();

// 创建透明、可穿透的代理窗口（DWM 缩略图镜像桌宠画面），
// 使各类窗口采集工具能枚举到桌宠。采集模式默认永久开启。
class WindowController {
public:
    WindowController();
    ~WindowController();

    // 确保已绑定有效窗口；窗口重建时自动重新定位
    bool EnsureWindow();

    void SetCaptureMode(bool enabled);
    void Shutdown();

    // 将代理窗口的位置/大小同步到当前游戏窗口矩形（启动阶段矩形会变化，需轮询校正）。
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

    // 独立代理窗口：创建一个真实可见、有标题的窗口，
    // 通过 DwmRegisterThumbnail 把游戏桌宠窗口的画面实时镜像进本窗口，
    // 使各类窗口采集工具能枚举到该窗口并采到桌宠画面。
    // 游戏窗口自身样式完全不动（避免 Unity 渲染崩溃）。
    // 代理窗口放置在屏幕可见区域之外（屏幕右侧外），因此物理上不挡桌面点击，
    // 但 OBS 等基于 DWM 的窗口采集仍可采到其画面（DWM 合成不依赖窗口在屏幕内）。
    static LRESULT CALLBACK ProxyWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void CreateProxyWindow();
    void DestroyProxyWindow();
    bool UpdateThumbnail();

    HWND hwnd_ = nullptr;
    bool captureMode_ = false;

    // 代理窗口（独立可见窗口，各类窗口采集工具枚举目标）。
    HWND proxyHwnd_ = nullptr;
    // DWM 缩略图句柄（源=游戏窗口，目标=代理窗口）。
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

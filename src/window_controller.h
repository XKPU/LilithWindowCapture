#pragma once

#include <windows.h>
#include <shobjidl.h>

namespace lilithwindowcapture {

// 定位本进程的 Unity 主渲染窗口
HWND FindGameWindow();

// 在「桌宠原生模式」和「OBS 捕获模式」之间切换窗口样式
class WindowController {
public:
    WindowController();
    ~WindowController();

    // 确保已绑定有效窗口；窗口重建时自动重新定位
    bool EnsureWindow();

    void Toggle();
    void SetCaptureMode(bool enabled);
    bool IsCaptureMode() const { return captureMode_; }

    void Shutdown();

private:
    void TakeSnapshot();
    void ApplyCaptureMode();
    void RestoreOriginalMode();
    void LogWindowState(const wchar_t* prefix);

    // 强制在任务栏加入/移除本窗口按钮（仅 AddTab/DeleteTab）
    void AddTaskbarTab();
    void RemoveTaskbarTab();

    HWND hwnd_ = nullptr;
    bool captureMode_ = false;

    ITaskbarList* taskbarList_ = nullptr;

    // 原始样式快照
    bool snapshotTaken_ = false;
    LONG_PTR originalStyle_ = 0;
    LONG_PTR originalExStyle_ = 0;
    bool originalLayeredValid_ = false;
    COLORREF originalColorKey_ = 0;
    BYTE originalAlpha_ = 255;
    DWORD originalLayeredFlags_ = 0;

    bool comInitialized_ = false;
};

// 全局单例访问器：供 BepInEx UI 插件跨模块调用（见 dllmain.cpp 中的导出函数）
class WindowControllerAccessor {
public:
    static WindowController* Instance() { return instance_; }
    static void SetInstance(WindowController* c) { instance_ = c; }
    static bool IsReady() { return instance_ != nullptr; }

private:
    static WindowController* instance_;
};

} // namespace lilithwindowcapture

# LilithWindowCapture

让游戏《The NOexistenceN of Lilith》的桌面宠物原生 `TOOLWINDOW` 窗口，能在**窗口采集模式**下被 OBS 等任意窗口采集/录制工具枚举到。

通过 BepInEx 托盘菜单一键切换窗口可见性，无需手动改窗口样式。

## 原理

桌面宠物窗口在游戏里被创建为 `WS_EX_TOOLWINDOW` 样式，因此不会被窗口采集工具枚举。本 mod 由两部分组成：

- **C++ `winmm.dll` 代理**（`src/`）：转发全部 `winmm` 导出到系统原始的 `winmm_orig.dll`，并在后台 worker 线程里动态切换游戏 Unity 窗口（`UnityWndClass`）的 `WS_EX_TOOLWINDOW` / `WS_EX_APPWINDOW` 样式，实现窗口采集可见性的开/关。同时用 owned-window 技巧隐藏任务栏图标，避免托盘出现多余条目。
- **C# BepInEx 6 (IL2CPP) 插件**（`plugin/`）：注册托盘菜单开关，并通过托管日志回调把 C++ 侧日志转发到 BepInEx。

C# 侧通过 `[DllImport("winmm")]` 调用 C++ 导出的 `LilithWindowCapture_*` 函数完成跨模块通信。

## 构建

需要：
- .NET 6 SDK
- MSVC（Visual Studio 2019/2022，含 C++ 桌面开发 workload）
- BepInEx 6（IL2CPP）运行时

产物：
- `winmm.dll` —— C++ 代理 DLL
- `LilithWindowCapture.dll` —— C# 插件

## 使用

运行游戏后，在 BepInEx 托盘图标上打开菜单，点击开关即可在「普通模式」与「窗口采集模式」之间切换：

- **普通模式**：桌面宠物窗口为 `TOOLWINDOW`，不会出现在任务栏/采集源列表。
- **窗口采集模式**：桌面宠物窗口暴露为标准应用窗口，可被任意窗口采集工具单独捕获。

##

兼容其他模组？

对于所有未修改winmm.dll的模组都可以兼容，任意模组修改winmm.dll都会导致冲突，需要兼容请提issues

若目标修改了winhttp.dll，请使用目标模组的winhttp.dll。判断修改？对比该SHA256：8c6cdbc38836dee87e3368f5de1994d7c0ccebf29e4ce7aba3c0981f9375412c，大多数模组不会修改该DLL文件
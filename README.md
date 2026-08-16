# LilithWindowCapture

![构建](https://img.shields.io/badge/Windows-x64-blue?logo=data:image/svg%2bxml;base64,PHN2ZyB0PSIxNzg2ODcyNzIwMTU2IiBjbGFzcz0iaWNvbiIgdmlld0JveD0iMCAwIDEwMjQgMTAyNCIgdmVyc2lvbj0iMS4xIiB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHAtaWQ9IjM3NzMiIHdpZHRoPSIyMDAiIGhlaWdodD0iMjAwIj48cGF0aCBkPSJNNDE5Ljg0IDU0MC4xNnY0MDAuMzg0TDAgODgzLjJ2LTM0Mi41MjhoNDE5Ljg0eiBtMC00NTcuMjE2djQwNS41MDRIMFYxNDAuOGw0MTkuODQtNTcuODU2eiBtNjA0LjE2IDQ1Ny4yMTZWMTAyNEw0NjUuOTIgOTQ3LjJ2LTQwNi41MjhoNTU4LjA4ek0xMDI0IDB2NDg4LjQ0OEg0NjUuOTJWNzYuOEwxMDI0IDB6IiBmaWxsPSIjZmZmZmZmIiBwLWlkPSIzNzc0Ij48L3BhdGg+PC9zdmc+)
![版本](https://img.shields.io/github/v/release/XKPU/LilithWindowCapture?logo=github)
![许可证](https://img.shields.io/github/license/XKPU/LilithWindowCapture?logo=data:image/svg+xml;base64,PHN2ZyB0PSIxNzg2ODczMTA1MjgwIiBjbGFzcz0iaWNvbiIgdmlld0JveD0iMCAwIDEwMjQgMTAyNCIgdmVyc2lvbj0iMS4xIiB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHAtaWQ9IjY2MzEiIHdpZHRoPSIyMDAiIGhlaWdodD0iMjAwIj48cGF0aCBkPSJNNTEyIDE2QzIzOC4wNjYgMTYgMTYgMjM4LjA2NiAxNiA1MTJzMjIyLjA2NiA0OTYgNDk2IDQ5NiA0OTYtMjIyLjA2NiA0OTYtNDk2Uzc4NS45MzQgMTYgNTEyIDE2eiBtMCA4OTZjLTIyMS4wNjQgMC00MDAtMTc4LjkwMi00MDAtNDAwIDAtMjIxLjA2MiAxNzguOTAyLTQwMCA0MDAtNDAwIDIyMS4wNjQgMCA0MDAgMTc4LjkwMiA0MDAgNDAwIDAgMjIxLjA2NC0xNzguOTAyIDQwMC00MDAgNDAweiBtMjE0LjcwMi0yMDIuMTI4Yy0xOS4yMjggMTkuNDI0LTkxLjA2IDgyLjc5Mi0yMDguMTMgODIuNzkyLTE2NC44NiAwLTI4MC45NjgtMTIyLjg1LTI4MC45NjgtMjgzLjEzNCAwLTE1OC4zMDQgMTIwLjU1LTI3OC44MDIgMjc5LjUyNC0yNzguODAyIDExMS4wNjIgMCAxNzcuNDc2IDUzLjI0IDE5NS4xODYgNjkuNTU4YTIzLjkzIDIzLjkzIDAgMCAxIDMuODcyIDMwLjY0NGwtMzYuMzEgNTYuMjI2Yy03LjY4MiAxMS45LTIzLjkzMiAxNC41NjQtMzQuOTk4IDUuODQyLTE3LjE5LTEzLjU1Mi02My42MjgtNDUuMDc2LTEyMy40MTYtNDUuMDc2LTk2LjYwNiAwLTE1NS44MzIgNzAuNjYtMTU1LjgzMiAxNjAuMTY0IDAgODMuMTc4IDUzLjc3NiAxNjcuMzg0IDE1Ni41NTQgMTY3LjM4NCA2NS4zMTQgMCAxMTMuNjg2LTM4LjA3OCAxMzEuNDUyLTU0LjQ1IDEwLjU0LTkuNzE0IDI3LjE5Mi04LjA3OCAzNS42NCAzLjQ3NmwzOS43MyA1NC4zNGEyMy44OTQgMjMuODk0IDAgMCAxLTIuMzA0IDMxLjAzNnoiIGZpbGw9IiNmZmZmZmYiIHAtaWQ9IjY2MzIiPjwvcGF0aD48L3N2Zz4=)

游戏《The NOexistenceN of Lilith》的窗口不会出现在部分窗口采集工具的窗口列表中——这类工具通常过滤掉工具窗口，或使用了无法枚举该游戏窗口的枚举方法。

本项目在游戏进程内额外创建一个**代理窗口**，通过 DWM 缩略图实时镜像画面。该代理窗口是普通的可枚举窗口，因此可以被上述窗口采集工具直接选中，无需改动游戏窗口自身的样式。

## 原理

**C++ `winmm.dll` 代理**（`src/`）

- 在运行时把 `winmm` 的全部导出函数转发至 `C:\Windows\System32\winmm.dll`（真实系统 DLL），不改变原有调用行为，因此无需随附原版 `winmm.dll` 副本。
- 在后台 worker 线程中定位游戏的 Unity 主窗口（窗口类 `UnityWndClass`，取同进程内面积最大的可见窗口）。
- 创建代理窗口，并用 `DwmRegisterThumbnail` 将游戏窗口注册为缩略图源，由 DWM 在合成阶段持续叠加画面。
- 代理窗口**放置在屏幕可见区域之外**（主显示器右侧外）。
- 代理窗口使用 `WS_EX_NOACTIVATE`：不参与前台激活，不抢焦点。
- 通过 `DwmExtendFrameIntoClientArea` 将 DWM 帧区域扩展至整个客户区，使窗口底色获得 per-pixel alpha 透明。
- 不进入任务栏（未设置 `WS_EX_APPWINDOW`）。
- 代理窗口以隐藏状态创建，待缩略图首次注册成功后才显示，避免启动阶段出现未渲染的空白窗口。
- 显示后的 8 秒内轮询游戏窗口矩形并同步大小（位置始终保持在屏幕外），以适配启动阶段的窗口尺寸变化。
- 游戏（桌宠）窗口自身的样式完全不动，因此不会触发 Unity 重绘或 SwapChain 崩溃。
- 日志仅输出到调试器（`OutputDebugString`），不产生任何本地文件，无需任何外部依赖。

## 安装

### 最小部署

将 `winmm.dll` 放入游戏目录（含 `Lilith.exe` 的目录）即可：

```
<游戏目录>/
├── Lilith_Data/
├── Lilith.exe
├── UnityCrashHandler64.exe
├── baselib.dll
├── GameAssembly.dll
├── UnityPlayer.dll
└── winmm.dll                     ← 本项目的代理
```

只需这一个 `winmm.dll`。代理在启动时自动从 `C:\Windows\System32\winmm.dll` 加载真实系统实现并转发原始调用，无需随附任何原版 DLL 副本。启动游戏，代理窗口自动创建。

### 采集方式限制

代理窗口的画面由 DWM 在**合成阶段**叠加，并不写入窗口自身的绘图表面。因此：

- ✅ **Windows Graphics Capture**（OBS 的「Windows 10 (1903 及以上)」，以及多数现代采集工具的默认方式）——DWM 合成路径，正常采集。
- ❌ **BitBlt / PrintWindow**（OBS 的「Windows 7 兼容」等旧模式）——直接读取窗口 DC 的像素，其中不包含 DWM 合成期叠加的内容，采集结果为空白。

若采集到全黑或空白画面，请先确认采集方式是否为 Windows Graphics Capture。

### 运行环境

- Windows 10 1903 或更高版本（Windows Graphics Capture 的最低要求）。
- 需启用桌面窗口管理器（DWM）合成，即默认状态。

### 日志

日志**只输出到调试器**（`OutputDebugString`），不写入任何本地文件。遇到代理窗口未出现、画面全黑等问题时，用 **DebugView**（以管理员身份运行）或 Visual Studio 的「输出」窗口查看，过滤前缀 `[LilithWindowCapture]`。

## 构建

前置依赖：

- MSVC 编译器（支持 C++17，随 Visual Studio 2022 及以上的「使用 C++ 的桌面开发」工作负载提供；构建脚本按以下优先级定位 `vcvars64.bat`：`-Vcvars` 参数 > 环境变量 `VCVARS64` > 多个预设安装路径自动查找）

`version.txt` 是版本号的唯一来源，构建时会据此生成 `src/version.h`，该文件不应手工修改。

编译 `src/` 下的 `dllmain.cpp`、`log.cpp`、`window_controller.cpp`，链接 `user32.lib`、`shell32.lib`、`kernel32.lib`、`dwmapi.lib`、`gdi32.lib`、`dbghelp.lib`，输出 `winmm.dll`。其中 `src/winmm_exports.h` 提供全部转发 stub 的声明。

构建前若仓库根目录已存在 `winmm.dll`，脚本会先将其删除再构建新产物，避免陈旧文件被复用。

构建与清理：

```powershell
.\make.ps1                  # 编译，产物 winmm.dll 输出到仓库根目录
.\make.ps1 -Build           # 同上，显式指定构建
.\make.ps1 -Clean           # 清理中间文件（obj/ 与 make.log）
.\make.ps1 -Vcvars "Path"   # 手动指定 vcvars64.bat（最高优先级）
# 或设置环境变量后直接构建：
$env:VCVARS64 = "Path"
.\make.ps1
```

## 兼容性

- 本项目以 `winmm.dll` 作为加载入口。若其他 mod 同样替换 `winmm.dll` 则为冲突。
- 如遇问题，请通过 Issue 反馈。
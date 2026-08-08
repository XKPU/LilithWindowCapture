using System;
using System.Threading;
using BepInEx.Logging;
using Il2CppInterop.Runtime;
using Il2CppInterop.Runtime.InteropTypes;

namespace LilithWindowCapture
{
    // 借鉴 The-NOexistenceN-of-Lilith-Mod-main 的 TrayIntegration：
    // 不新建托盘，而是复用游戏自己的原生系统托盘（ShowSystemTray.instance.tray），
    // 在其右键菜单追加一个窗口采集模式开关，并在菜单文字里注明当前模式。
    // 注意：ISystemTray 无 RemoveItem/改文字 API，故菜单项文字在注册（及托盘重建）时
    // 按当前模式渲染；切换结果通过日志与托盘气泡反馈。IL2CPP 下插件无法挂载 MonoBehaviour，
    // 故用后台托管线程周期轮询托盘就绪后注册一次（并防原生菜单重建导致重复注册）。
    internal static class TrayIntegration
    {
        private static ManualLogSource _log;
        private static bool _initialized;
        private static bool _failureLogged;
        private static System.IntPtr _observedIdentity;
        private static long _readySinceMs;
        private static int _registeredCount = -1;
        private const long ReadyDelayMs = 2500;

        public static void Init(ManualLogSource log) => _log = log;

        public static bool IsRegistered => _initialized;

        private static string MenuItemLabel()
        {
            return "切换窗口采集模式";
        }

        private static void Toggle()
        {
            try
            {
                bool next = !WindowCaptureBridge.CaptureMode;
                WindowCaptureBridge.CaptureMode = next;
                // 持久化当前状态，供下次启动按 RestoreLastState 恢复。
                LilithWindowCapture.ConfigLastCaptureMode.Value = next;
                _log?.LogInfo($"[LilithWindowCapture] 切换窗口采集模式 -> {(next ? "开启" : "关闭")}（已写入配置）");
                try
                {
                    ShowSystemTray.instance?.tray?.ShowNotification(
                        "LilithWindowCapture", next ? "已切换至窗口采集模式" : "已关闭窗口采集模式", 3000);
                }
                catch { /* 通知失败不影响切换 */ }
            }
            catch (Exception e)
            {
                _log?.LogWarning($"[LilithWindowCapture] 托盘菜单动作执行失败: {e.GetType().Name}: {e.Message}");
            }
        }

        public static void TryRegister()
        {
            try
            {
                ShowSystemTray host = ShowSystemTray.instance;
                ISystemTray tray = host?.tray;
                if (host is null || !host.initialized || tray is null)
                {
                    return;
                }

                long now = Environment.TickCount64;
                System.IntPtr identity = ((Il2CppObjectBase)tray).Pointer;
                if (identity != _observedIdentity)
                {
                    // 原生托盘菜单被重建：重置，下一轮用最新模式重新注册
                    _observedIdentity = identity;
                    _readySinceMs = now;
                    _registeredCount = -1;
                    _initialized = false;
                }

                if (_registeredCount >= 0)
                {
                    return;
                }

                if (now - _readySinceMs < ReadyDelayMs)
                {
                    return;
                }

                tray.AddItem(MenuItemLabel(), (Il2CppSystem.Action)(Action)Toggle);
                _registeredCount = 1;
                _initialized = true;
                _log?.LogInfo("[LilithWindowCapture] 系统托盘菜单已注册窗口采集开关（当前：" +
                              (WindowCaptureBridge.CaptureMode ? "开启" : "关闭") + "）");
            }
            catch (Exception e)
            {
                if (!_failureLogged)
                {
                    _failureLogged = true;
                    _log?.LogWarning($"[LilithWindowCapture] 托盘菜单注入失败: {e.GetType().Name}: {e.Message}");
                }
            }
        }
    }
}

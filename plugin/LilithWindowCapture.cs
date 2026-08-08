using BepInEx;
using BepInEx.Configuration;
using BepInEx.Logging;
using BepInEx.Unity.IL2CPP;

namespace LilithWindowCapture
{
    // BepInEx 6 (IL2CPP) 插件入口。
    // 窗口采集模式的实际逻辑由 C++ 版 winmm.dll 代理承担，相关日志转发到 BepInEx 日志。
    // IL2CPP 下插件程序集内的 MonoBehaviour 无法作为组件 Add 到 GameObject（无对应 native 类），
    // 因此不在此处挂任何 MonoBehaviour，避免 Load() 阶段崩溃。
    [BepInPlugin("com.lilithwindowcapture.mod", "LilithWindowCapture", "1.0.0")]
    [BepInProcess("Lilith.exe")]
    public class LilithWindowCapture : BasePlugin
    {
        internal static ManualLogSource Logger;

        // 上次窗口采集模式（0=关闭,1=开启）。每次通过托盘切换后写入，供下次启动恢复。
        internal static ConfigEntry<bool> ConfigLastCaptureMode;

        // 是否在上次退出时记住窗口采集模式，并在下次启动时自动恢复。
        internal static ConfigEntry<bool> ConfigRestoreLastState;

        public override void Load()
        {
            Logger = Log;

            // 由 BepInEx 托管配置文件（BepInEx/config/com.lilithwindowcapture.mod.cfg）。
            // 这些条目会随注释自动生成到 cfg 文件中，用户可直接编辑。
            ConfigRestoreLastState = Config.Bind(
                "General", "RestoreLastState", false,
                "是否在启动时恢复上次的窗口采集模式。true=按上次状态自动开启/关闭；false=每次启动都从关闭开始。");
            ConfigLastCaptureMode = Config.Bind(
                "General", "LastCaptureMode", false,
                "上一次退出时的窗口采集模式（0=关闭,1=开启）。由插件自动维护，一般无需手动修改。");

            // 将 C++ 代理的日志回调转发到 BepInEx 日志（不再写单独的日志文件）。
            WindowCaptureBridge.RegisterLogCallback(Logger);

            Logger.LogInfo("[LilithWindowCapture] 插件已加载。窗口逻辑由 winmm.dll 代理提供。");

            if (!WindowCaptureBridge.IsReady)
            {
                Logger.LogWarning("[LilithWindowCapture] winmm.dll 代理未就绪，窗口采集开关可能无效（请确认游戏目录含已部署的 winmm.dll 代理）");
            }
            else
            {
                bool mode = WindowCaptureBridge.CaptureMode;
                Logger.LogInfo($"[LilithWindowCapture] winmm 代理就绪，当前窗口采集模式 = {(mode ? 1 : 0)}（0=关闭,1=开启）。通过系统托盘右键菜单切换。");
            }

            // 复用游戏原生系统托盘，追加窗口采集开关菜单项。
            // IL2CPP 下无法挂载 MonoBehaviour，改用后台托管线程周期轮询托盘就绪后注册一次。
            TrayIntegration.Init(Logger);
            var trayThread = new System.Threading.Thread(() =>
            {
                bool restoreApplied = false;
                while (true)
                {
                    // C++ 代理找到游戏窗口后，按需恢复上次状态（仅需执行一次）。
                    if (!restoreApplied && WindowCaptureBridge.IsReady)
                    {
                        if (ConfigRestoreLastState.Value)
                        {
                            WindowCaptureBridge.CaptureMode = ConfigLastCaptureMode.Value;
                            Logger.LogInfo($"[LilithWindowCapture] 已按配置恢复上次窗口采集模式 = {(ConfigLastCaptureMode.Value ? 1 : 0)}");
                        }
                        else
                        {
                            Logger.LogInfo("[LilithWindowCapture] 未启用恢复上次状态（RestoreLastState=false），保持关闭。");
                        }
                        restoreApplied = true;
                    }

                    try { TrayIntegration.TryRegister(); }
                    catch { /* 忽略瞬时错误，下个周期重试 */ }

                    System.Threading.Thread.Sleep(2000);
                }
            })
            {
                IsBackground = true,
                Name = "LilithWindowCaptureTray"
            };
            trayThread.Start();
            Logger.LogInfo("[LilithWindowCapture] 托盘菜单注入线程已启动（等待游戏系统托盘就绪后追加窗口采集开关）");
        }
    }
}

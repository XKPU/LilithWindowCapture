using BepInEx;
using BepInEx.Logging;
using BepInEx.Unity.IL2CPP;

namespace LilithWindowCapture
{
    // BepInEx 6 (IL2CPP) 插件入口。
    [BepInPlugin("com.lilithwindowcapture.mod", "LilithWindowCapture", Version.Value)]
    [BepInProcess("Lilith.exe")]
    public class LilithWindowCapture : BasePlugin
    {
        internal static ManualLogSource Logger;

        public override void Load()
        {
            Logger = Log;

            // 将 C++ 代理的日志回调转发到 BepInEx 日志。
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
            TrayIntegration.Init(Logger);
            var trayThread = new System.Threading.Thread(() =>
            {
                while (true)
                {
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

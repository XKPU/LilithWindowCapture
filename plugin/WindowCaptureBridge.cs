using System;
using System.Runtime.InteropServices;
using BepInEx.Logging;

namespace LilithWindowCapture
{
    // 与本机 winmm.dll 代理通信的桥接层。
    // winmm.dll 导出了 LilithWindowCapture_SetCaptureMode / GetCaptureMode / IsReady / SetLogCallback，
    // 这些函数由 C++ 侧通过窗口样式切换逻辑实现（见 src/dllmain.cpp）。
    internal static class WindowCaptureBridge
    {
        private const string DllName = "winmm";

        // 与 C++ 侧 log.h 的 LogCallback 签名一致：(level, message)
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void NativeLogCallback(
            [MarshalAs(UnmanagedType.LPWStr)] string level,
            [MarshalAs(UnmanagedType.LPWStr)] string message);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, EntryPoint = "LilithWindowCapture_SetCaptureMode")]
        public static extern void LilithWindowCapture_SetCaptureMode(int enable);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, EntryPoint = "LilithWindowCapture_GetCaptureMode")]
        public static extern int LilithWindowCapture_GetCaptureMode();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, EntryPoint = "LilithWindowCapture_IsReady")]
        public static extern int LilithWindowCapture_IsReady();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, EntryPoint = "LilithWindowCapture_SetLogCallback")]
        public static extern void LilithWindowCapture_SetLogCallback(NativeLogCallback callback);

        public static bool IsReady => LilithCapture_IsReady() != 0;

        public static bool CaptureMode
        {
            get => LilithWindowCapture_GetCaptureMode() != 0;
            set => LilithWindowCapture_SetCaptureMode(value ? 1 : 0);
        }

        // 注册一个稳定的托管回调，把 C++ 日志转发到 BepInEx 日志。
        // 持有静态引用防止 GC 回收委托。
        private static NativeLogCallback _logCallback;
        public static void RegisterLogCallback(ManualLogSource log)
        {
            _logCallback = (level, message) =>
            {
                switch (level)
                {
                    case "WARN": log.LogWarning(message); break;
                    case "DEBUG": log.LogDebug(message); break;
                    default: log.LogInfo(message); break;
                }
            };
            LilithWindowCapture_SetLogCallback(_logCallback);
        }
    }
}

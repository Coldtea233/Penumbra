using System.ComponentModel;
using System.Diagnostics;

namespace Nocturne;

internal enum WallpaperEngineState { NotDetected, Running, Unavailable }

internal static class WallpaperEngineStatus
{
    // Inspect only process names and session IDs; no injection, WMI, file or network access.
    internal static WallpaperEngineState Detect()
    {
        bool unavailable = false;
        try
        {
            using var current = Process.GetCurrentProcess();
            int session = current.SessionId;
            foreach (string name in new[] { "wallpaper32", "wallpaper64" })
            {
                var processes = Process.GetProcessesByName(name);
                try
                {
                    foreach (var process in processes)
                    {
                        try
                        {
                            if (process.SessionId == session && !process.HasExited)
                                return WallpaperEngineState.Running;
                        }
                        catch (InvalidOperationException) { } // Process exited during inspection.
                        catch (Win32Exception) { unavailable = true; }
                    }
                }
                finally { foreach (var process in processes) process.Dispose(); }
            }
        }
        catch (Exception e) when (e is Win32Exception or InvalidOperationException or NotSupportedException)
        {
            unavailable = true;
        }
        return unavailable ? WallpaperEngineState.Unavailable : WallpaperEngineState.NotDetected;
    }
}

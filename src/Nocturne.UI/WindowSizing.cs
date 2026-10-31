using System.ComponentModel;
using System.Runtime.InteropServices;

namespace Nocturne;

// Native tracking limits keep controls usable without fighting Windows resize,
// maximize, restore or Snap. Re-evaluate the DPI each time a resize starts.
internal sealed class WindowSizing : IDisposable
{
    private readonly IntPtr window;
    private readonly SubclassProc callback;
    private readonly int minimumWidth, minimumHeight;
    private bool disposed;
    private const nuint SubclassId = 0x504E4252;

    public WindowSizing(IntPtr window, int minimumWidth, int minimumHeight)
    {
        this.window = window;
        this.minimumWidth = minimumWidth;
        this.minimumHeight = minimumHeight;
        callback = HandleMessage;
        if (!SetWindowSubclass(window, callback, SubclassId, 0))
            throw new Win32Exception(Marshal.GetLastWin32Error(), "无法设置窗口尺寸范围。");
    }

    private IntPtr HandleMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam, nuint id, nuint data)
    {
        var result = DefSubclassProc(hwnd, message, wParam, lParam);
        if (message == 0x0024 && lParam != IntPtr.Zero) // WM_GETMINMAXINFO
        {
            var limits = Marshal.PtrToStructure<MinMaxInfo>(lParam);
            double scale = GetDpiForWindow(hwnd) / 96.0;
            limits.MinTrackSize.X = Math.Max(limits.MinTrackSize.X, (int)Math.Ceiling(minimumWidth * scale));
            limits.MinTrackSize.Y = Math.Max(limits.MinTrackSize.Y, (int)Math.Ceiling(minimumHeight * scale));
            Marshal.StructureToPtr(limits, lParam, false);
        }
        else if (message == 0x0082) // WM_NCDESTROY
        {
            Dispose();
        }
        return result;
    }

    public void Dispose()
    {
        if (disposed) return;
        RemoveWindowSubclass(window, callback, SubclassId);
        disposed = true;
    }

    [StructLayout(LayoutKind.Sequential)] private struct Point { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] private struct MinMaxInfo
    {
        public Point Reserved, MaxSize, MaxPosition, MinTrackSize, MaxTrackSize;
    }
    private delegate IntPtr SubclassProc(IntPtr window, uint message, IntPtr wParam, IntPtr lParam, nuint id, nuint data);
    [DllImport("comctl32.dll", SetLastError = true)] private static extern bool SetWindowSubclass(IntPtr window, SubclassProc callback, nuint id, nuint data);
    [DllImport("comctl32.dll")] private static extern bool RemoveWindowSubclass(IntPtr window, SubclassProc callback, nuint id);
    [DllImport("comctl32.dll")] private static extern IntPtr DefSubclassProc(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] private static extern uint GetDpiForWindow(IntPtr window);
}

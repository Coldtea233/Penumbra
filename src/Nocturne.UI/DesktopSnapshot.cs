using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

namespace Nocturne;

// One background snapshot per refresh. No screen scraping, activation or Win+D.
internal sealed record DesktopSnapshot(byte[] Pixels, int Width, int Height, byte[] Icons)
{
    public static Task<DesktopSnapshot> CaptureAsync() => Task.Run(async () =>
    {
        int sourceWidth = GetSystemMetrics(0), sourceHeight = GetSystemMetrics(1);
        if (sourceWidth <= 0 || sourceHeight <= 0)
            throw new InvalidOperationException("没有可用的主显示器。");
        var (host, icons) = FindDesktop(sourceWidth, sourceHeight);
        if (!GetWindowRect(host, out var bounds) || bounds.Width <= 0 || bounds.Height <= 0)
            throw new InvalidOperationException("无法读取桌面窗口尺寸。");
        // This color target is a wallpaper child and therefore appears in a
        // root capture, unlike the separate vignette/blur popups. Acquire raw
        // colors once so subsequent slider edits never apply saturation twice.
        // The native lease times out if capture or the UI process fails.
        var controller = FindWindow("Nocturne.Controller", null);
        bool rawColor = controller != IntPtr.Zero &&
            SendMessageTimeout(controller, 0x8007, 104, (IntPtr)Environment.ProcessId, 2, 100, out var acknowledged) != IntPtr.Zero && acknowledged == 1;
        WindowCapture.Frame frame;
        try
        {
            if (rawColor) await Task.Delay(34);
            frame = await WindowCapture.CaptureAsync(host);
        }
        finally
        {
            if (rawColor) SendMessageTimeout(controller, 0x8007, 105, (IntPtr)Environment.ProcessId, 2, 100, out _);
        }
        int width = 640, height = Math.Clamp((int)Math.Round(640.0 * sourceHeight / sourceWidth), 1, 2560);
        var pixels = ScalePrimary(frame, bounds, sourceWidth, sourceHeight, width, height);
        // New Explorer versions host wallpaper and icons in one tree. Older
        // layouts may place them in separate top-level WorkerW windows.
        bool compositeIcons = icons != IntPtr.Zero && GetAncestor(icons, 2) != host;
        var mask = CaptureIcons(icons, pixels, compositeIcons, sourceWidth, sourceHeight, width, height);
        return new DesktopSnapshot(pixels, width, height, mask);
    });

    private static (IntPtr Host, IntPtr Icons) FindDesktop(int width, int height)
    {
        var roots = new List<IntPtr>();
        EnumWindows((window, _) =>
        {
            if (IsWindowVisible(window) && ClassName(window) is "Progman" or "WorkerW") roots.Add(window);
            return true;
        }, IntPtr.Zero);
        IntPtr wallpaper = IntPtr.Zero, icons = IntPtr.Zero;
        foreach (var root in roots)
        {
            EnumChildWindows(root, (window, _) =>
            {
                string name = ClassName(window);
                if (name == "SysListView32" && ClassName(GetParent(window)) == "SHELLDLL_DefView") icons = window;
                if (name.StartsWith("WPE", StringComparison.Ordinal) && IsWindowVisible(window) &&
                    GetWindowRect(window, out var r) && r.Left <= 0 && r.Top <= 0 && r.Right >= width && r.Bottom >= height)
                    wallpaper = GetAncestor(window, 2);
                return true;
            }, IntPtr.Zero);
        }
        var host = wallpaper != IntPtr.Zero ? wallpaper : icons != IntPtr.Zero ? GetAncestor(icons, 2) : GetShellWindow();
        if (host == IntPtr.Zero) throw new InvalidOperationException("桌面尚未就绪，请稍后刷新壁纸。");
        return (host, icons);
    }

    private static unsafe byte[] ScalePrimary(WindowCapture.Frame frame, Rect bounds,
        int sourceWidth, int sourceHeight, int width, int height)
    {
        if (bounds.Left > 0 || bounds.Top > 0 || bounds.Right < sourceWidth || bounds.Bottom < sourceHeight)
            throw new InvalidOperationException("壁纸窗口尚未覆盖主屏幕，请稍后刷新。");
        int sx = (int)Math.Round(-bounds.Left * (double)frame.Width / bounds.Width);
        int sy = (int)Math.Round(-bounds.Top * (double)frame.Height / bounds.Height);
        int sw = Math.Min(frame.Width - sx, (int)Math.Round(sourceWidth * (double)frame.Width / bounds.Width));
        int sh = Math.Min(frame.Height - sy, (int)Math.Round(sourceHeight * (double)frame.Height / bounds.Height));
        var target = new BitmapInfo { Size = 40, Width = width, Height = -height, Planes = 1, Bits = 32 };
        var source = new BitmapInfo { Size = 40, Width = frame.Width, Height = -frame.Height, Planes = 1, Bits = 32 };
        IntPtr memory = CreateCompatibleDC(IntPtr.Zero), bitmap = IntPtr.Zero, previous = IntPtr.Zero;
        try
        {
            bitmap = CreateDIBSection(memory, ref target, 0, out var bits, IntPtr.Zero, 0);
            if (memory == IntPtr.Zero || bitmap == IntPtr.Zero)
                throw new Win32Exception(Marshal.GetLastWin32Error(), "无法创建预览位图。");
            previous = SelectObject(memory, bitmap);
            SetStretchBltMode(memory, 4);
            SetBrushOrgEx(memory, 0, 0, IntPtr.Zero);
            fixed (byte* data = frame.Pixels)
            {
                if (StretchDIBits(memory, 0, 0, width, height, sx, sy, sw, sh, (IntPtr)data, ref source, 0, 0x00CC0020) <= 0)
                    throw new InvalidOperationException("无法缩放壁纸预览。");
            }
            var pixels = new byte[width * height * 4];
            Marshal.Copy(bits, pixels, 0, pixels.Length);
            for (int i = 3; i < pixels.Length; i += 4) pixels[i] = 255;
            return pixels;
        }
        finally
        {
            if (previous != IntPtr.Zero) SelectObject(memory, previous);
            if (bitmap != IntPtr.Zero) DeleteObject(bitmap);
            if (memory != IntPtr.Zero) DeleteDC(memory);
        }
    }

    private static unsafe byte[] CaptureIcons(IntPtr list, byte[] pixels, bool composite,
        int sourceWidth, int sourceHeight, int width, int height)
    {
        var mask = new byte[width * height];
        if (list == IntPtr.Zero || !IsWindowVisible(list) || !GetWindowRect(list, out var bounds) ||
            bounds.Width <= 0 || bounds.Height <= 0) return mask;
        var info = new BitmapInfo { Size = 40, Width = bounds.Width, Height = -bounds.Height, Planes = 1, Bits = 32 };
        IntPtr memory = CreateCompatibleDC(IntPtr.Zero), bitmap = IntPtr.Zero, previous = IntPtr.Zero;
        try
        {
            bitmap = CreateDIBSection(memory, ref info, 0, out var bits, IntPtr.Zero, 0);
            if (memory == IntPtr.Zero || bitmap == IntPtr.Zero) return mask;
            previous = SelectObject(memory, bitmap);
            new Span<byte>((void*)bits, checked(bounds.Width * bounds.Height * 4)).Clear();
            if (!PrintWindow(list, memory, 3)) return mask;
            int count = 0;
            for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++)
            {
                int sx = (int)((x + .5) * sourceWidth / width) - bounds.Left;
                int sy = (int)((y + .5) * sourceHeight / height) - bounds.Top;
                if (sx < 0 || sy < 0 || sx >= bounds.Width || sy >= bounds.Height) continue;
                int color = Marshal.ReadInt32(bits, (sy * bounds.Width + sx) * 4);
                if ((color & 0xFFFFFF) == 0) continue;
                mask[y * width + x] = 1;
                count++;
            }
            // An opaque wallpaper must not be mistaken for an icon mask.
            if (count > mask.Length * .75) { Array.Clear(mask); return mask; }
            if (composite)
            {
                for (int y = 0; y < height; y++)
                for (int x = 0; x < width; x++)
                {
                    if (mask[y * width + x] == 0) continue;
                    int sx = (int)((x + .5) * sourceWidth / width) - bounds.Left;
                    int sy = (int)((y + .5) * sourceHeight / height) - bounds.Top;
                    int color = Marshal.ReadInt32(bits, (sy * bounds.Width + sx) * 4), i = (y * width + x) * 4;
                    pixels[i] = (byte)color; pixels[i + 1] = (byte)(color >> 8); pixels[i + 2] = (byte)(color >> 16);
                }
            }
            return mask;
        }
        finally
        {
            if (previous != IntPtr.Zero) SelectObject(memory, previous);
            if (bitmap != IntPtr.Zero) DeleteObject(bitmap);
            if (memory != IntPtr.Zero) DeleteDC(memory);
        }
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr FindWindow(string className, string? title);
    [DllImport("user32.dll")] private static extern IntPtr SendMessageTimeout(IntPtr window, uint message, nuint wp, IntPtr lp, uint flags, uint timeout, out nuint result);
    private static string ClassName(IntPtr window)
    {
        var name = new StringBuilder(128);
        GetClassName(window, name, name.Capacity);
        return name.ToString();
    }
    [StructLayout(LayoutKind.Sequential)] private struct Rect
    {
        public int Left, Top, Right, Bottom;
        public readonly int Width => Right - Left;
        public readonly int Height => Bottom - Top;
    }
    [StructLayout(LayoutKind.Sequential)] private struct BitmapInfo
    {
        public uint Size; public int Width, Height; public ushort Planes, Bits;
        public uint Compression, ImageSize; public int XPels, YPels; public uint Colors, Important, Color;
    }
    private delegate bool EnumCallback(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumCallback callback, IntPtr parameter);
    [DllImport("user32.dll")] private static extern bool EnumChildWindows(IntPtr parent, EnumCallback callback, IntPtr parameter);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] private static extern bool PrintWindow(IntPtr hwnd, IntPtr dc, uint flags);
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr hwnd, out Rect rect);
    [DllImport("user32.dll")] private static extern IntPtr GetShellWindow();
    [DllImport("user32.dll")] private static extern IntPtr GetParent(IntPtr hwnd);
    [DllImport("user32.dll")] private static extern IntPtr GetAncestor(IntPtr hwnd, uint flags);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetClassName(IntPtr hwnd, StringBuilder name, int size);
    [DllImport("user32.dll")] private static extern int GetSystemMetrics(int n);
    [DllImport("gdi32.dll")] private static extern IntPtr CreateCompatibleDC(IntPtr dc);
    [DllImport("gdi32.dll", SetLastError = true)] private static extern IntPtr CreateDIBSection(IntPtr dc, ref BitmapInfo info, uint usage, out IntPtr bits, IntPtr section, uint offset);
    [DllImport("gdi32.dll")] private static extern IntPtr SelectObject(IntPtr dc, IntPtr obj);
    [DllImport("gdi32.dll")] private static extern int SetStretchBltMode(IntPtr dc, int mode);
    [DllImport("gdi32.dll")] private static extern bool SetBrushOrgEx(IntPtr dc, int x, int y, IntPtr previous);
    [DllImport("gdi32.dll")] private static extern int StretchDIBits(IntPtr dc, int x, int y, int w, int h, int sx, int sy, int sw, int sh, IntPtr pixels, ref BitmapInfo info, uint usage, uint operation);
    [DllImport("gdi32.dll")] private static extern bool DeleteObject(IntPtr obj);
    [DllImport("gdi32.dll")] private static extern bool DeleteDC(IntPtr dc);
}

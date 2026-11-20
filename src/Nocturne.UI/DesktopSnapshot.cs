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
        // Saturation lives inside the wallpaper tree; temporarily bypass it.
        // The desktop blur/vignette popups are outside the captured tree and
        // do not need hiding (or cause a clear-frame flash) for this snapshot.
        // The native lease times out if capture or the UI process fails.
        var controller = FindWindow("Nocturne.Controller", null);
        bool rawWallpaper = controller != IntPtr.Zero &&
            SendMessageTimeout(controller, 0x8007, 104, (IntPtr)Environment.ProcessId, 2, 100, out var acknowledged) != IntPtr.Zero && acknowledged == 1;
        WindowCapture.Frame frame;
        try
        {
            if (rawWallpaper) await Task.Delay(34);
            frame = await WindowCapture.CaptureAsync(host);
        }
        finally
        {
            if (rawWallpaper) SendMessageTimeout(controller, 0x8007, 105, (IntPtr)Environment.ProcessId, 2, 100, out _);
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

    // 图标遮罩由后台（C++ 侧的 icon_mask.h）推导后放进命名共享内存，
    // 这里只按预览分辨率采样，不重复实现判定：C# 只负责把结果画出来。
    private static unsafe byte[] CaptureIcons(IntPtr list, byte[] pixels, bool composite,
        int sourceWidth, int sourceHeight, int width, int height)
    {
        var mask = new byte[width * height];
        if (list == IntPtr.Zero || !IsWindowVisible(list) || !GetWindowRect(list, out var bounds) ||
            bounds.Width <= 0 || bounds.Height <= 0) return mask;
        var shared = ReadSharedIconMask();
        bool captured = false;
        var info = new BitmapInfo { Size = 40, Width = bounds.Width, Height = -bounds.Height, Planes = 1, Bits = 32 };
        IntPtr memory = CreateCompatibleDC(IntPtr.Zero), bitmap = IntPtr.Zero, previous = IntPtr.Zero;
        byte* pixelsOfWindow = null;
        try
        {
            if (composite || shared is null)
            {
                bitmap = CreateDIBSection(memory, ref info, 0, out var bits, IntPtr.Zero, 0);
                if (memory == IntPtr.Zero || bitmap == IntPtr.Zero) return mask;
                previous = SelectObject(memory, bitmap);
                new Span<byte>((void*)bits, checked(bounds.Width * bounds.Height * 4)).Clear();
                pixelsOfWindow = (byte*)bits;
                captured = PrintWindow(list, memory, 3);
            }
            if (shared is { } sharedMask)
            {
                for (int y = 0; y < height; y++)
                for (int x = 0; x < width; x++)
                {
                    int sx = (int)((x + .5) * sourceWidth / width) - sharedMask.Left;
                    int sy = (int)((y + .5) * sourceHeight / height) - sharedMask.Top;
                    if (sx < 0 || sy < 0 || sx >= sharedMask.Width || sy >= sharedMask.Height) continue;
                    long index = (long)sy * sharedMask.Width + sx;
                    if ((sharedMask.Bits[index >> 3] & (1 << (int)(index & 7))) != 0) mask[y * width + x] = 1;
                }
            }
            else if (captured)
            {
                FillGlyphMask(mask, pixelsOfWindow, bounds, sourceWidth, sourceHeight, width, height);
            }
            if (composite && captured)
            {
                for (int y = 0; y < height; y++)
                for (int x = 0; x < width; x++)
                {
                    if (mask[y * width + x] == 0) continue;
                    int sx = (int)((x + .5) * sourceWidth / width) - bounds.Left;
                    int sy = (int)((y + .5) * sourceHeight / height) - bounds.Top;
                    if (sx < 0 || sy < 0 || sx >= bounds.Width || sy >= bounds.Height) continue;
                    int color = Marshal.ReadInt32((IntPtr)pixelsOfWindow, (sy * bounds.Width + sx) * 4), i = (y * width + x) * 4;
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

    private readonly record struct SharedMask(byte[] Bits, int Width, int Height, int Left, int Top);

    // 共享内存布局必须与渲染器的 IconMaskShare 一致（sizeof 40，越小越好核对）：
    // 0 magic / 4 version / 8 width / 12 height / 16 bytes / 20 left / 24 top / 32 generation，
    // 40 起是每像素 1 位的掩码。
    private static SharedMask? ReadSharedIconMask()
    {
        const int headerSize = 40;
        const uint magic = 0x314D494Eu;
        IntPtr mapping = OpenFileMapping(0x0004 /* FILE_MAP_READ */, false, @"Local\Nocturne.IconMask.v1");
        if (mapping == IntPtr.Zero) return null;
        IntPtr view = IntPtr.Zero;
        try
        {
            view = MapViewOfFile(mapping, 0x0004, 0, 0, UIntPtr.Zero);
            if (view == IntPtr.Zero) return null;
            if (VirtualQuery(view, out var region, (nuint)Marshal.SizeOf<MemoryBasicInformation>()) == 0) return null;
            long available = (long)region.RegionSize;
            if (available < headerSize) return null;
            // 代次先读、写完再校验：后台刷新时重取一次，避免半新半旧的遮罩。
            for (int attempt = 0; attempt < 2; attempt++)
            {
                long generation = Marshal.ReadInt64(view, 32);
                if ((uint)Marshal.ReadInt32(view, 0) != magic || (uint)Marshal.ReadInt32(view, 4) != 1) return null;
                int width = Marshal.ReadInt32(view, 8), height = Marshal.ReadInt32(view, 12);
                long payload = (uint)Marshal.ReadInt32(view, 16);
                if (width < 1 || height < 1 || payload != ((long)width * height + 7) / 8) return null;
                if (payload > available - headerSize) return null;
                int left = Marshal.ReadInt32(view, 20), top = Marshal.ReadInt32(view, 24);
                var bits = new byte[payload];
                Marshal.Copy(view + headerSize, bits, 0, bits.Length);
                Thread.MemoryBarrier();
                if ((uint)Marshal.ReadInt32(view, 0) == magic && Marshal.ReadInt64(view, 32) == generation)
                    return new SharedMask(bits, width, height, left, top);
            }
            return null;
        }
        finally
        {
            if (view != IntPtr.Zero) UnmapViewOfFile(view);
            CloseHandle(mapping);
        }
    }

    // 旧版 Explorer 的图标视图是透明的，捕获里非黑即图标；不透明壁纸不能当成全屏图标。
    private static unsafe void FillGlyphMask(byte[] mask, byte* bits, Rect bounds,
        int sourceWidth, int sourceHeight, int width, int height)
    {
        int painted = 0;
        for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
        {
            int sx = (int)((x + .5) * sourceWidth / width) - bounds.Left;
            int sy = (int)((y + .5) * sourceHeight / height) - bounds.Top;
            if (sx < 0 || sy < 0 || sx >= bounds.Width || sy >= bounds.Height) continue;
            if ((Marshal.ReadInt32((IntPtr)bits, (sy * bounds.Width + sx) * 4) & 0xFFFFFF) == 0) continue;
            mask[y * width + x] = 1;
            painted++;
        }
        if (painted > mask.Length * .75) Array.Clear(mask);
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr FindWindow(string className, string? title);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] private static extern IntPtr OpenFileMapping(uint access, bool inherit, string name);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern IntPtr MapViewOfFile(IntPtr mapping, uint access, uint offsetHigh, uint offsetLow, UIntPtr bytes);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool UnmapViewOfFile(IntPtr address);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool CloseHandle(IntPtr handle);
    [StructLayout(LayoutKind.Sequential)] private struct MemoryBasicInformation
    {
        public IntPtr BaseAddress, AllocationBase;
        public uint AllocationProtect, PartitionId;
        public nuint RegionSize;
        public uint State, Protect, Type;
    }
    [DllImport("kernel32.dll", SetLastError = true)] private static extern nuint VirtualQuery(IntPtr address, out MemoryBasicInformation information, nuint length);
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

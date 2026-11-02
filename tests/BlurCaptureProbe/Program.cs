using Nocturne;
using System.Runtime.InteropServices;

Native.SetProcessDpiAwarenessContext(new IntPtr(-4));
var foreground = Native.GetForegroundWindow();
foreach (string windowClass in args.Length > 0 ? args : ["Progman", "Nocturne.BlurSurface"])
{
    var window = Native.FindWindow(windowClass, null);
    if (window == IntPtr.Zero) window = Native.FindWindowEx(Native.FindWindow("Progman", null), IntPtr.Zero, windowClass, null);
    if (window == IntPtr.Zero) Native.EnumWindows((root, _) => {
        Native.EnumChildWindows(root, (child, _) => {
            var name = new System.Text.StringBuilder(128);
            Native.GetClassName(child, name, name.Capacity);
            if (name.ToString() == windowClass) window = child;
            return true;
        }, IntPtr.Zero);
        return true;
    }, IntPtr.Zero);
    if (window == IntPtr.Zero) return 77;
    bool visibleBefore = Native.IsWindowVisible(window);
    try
    {
        var frame = await WindowCapture.CaptureAsync(window);
        double sum = 0, squared = 0, alpha = 0;
        for (int x = 144; x < 400; x++)
        {
            int index = (144 * frame.Width + x) * 4;
            double red = frame.Pixels[index + 2];
            sum += red; squared += red * red; alpha += frame.Pixels[index + 3];
        }
        double chroma = 0; int colored = 0;
        for (int i = 0; i < frame.Pixels.Length; i += 4) {
            int c = Math.Max(frame.Pixels[i], Math.Max(frame.Pixels[i+1], frame.Pixels[i+2])) - Math.Min(frame.Pixels[i], Math.Min(frame.Pixels[i+1], frame.Pixels[i+2]));
            chroma += c; if (c > 20) colored++;
        }
        Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(new {
            WindowClass = windowClass, frame.Width, frame.Height,
            MeanRed = sum / 256, Contrast = Math.Sqrt(Math.Max(0, squared / 256 - Math.Pow(sum / 256, 2))),
            Alpha = alpha / 256, MeanChroma = chroma / (frame.Pixels.Length / 4), ColoredPixels = colored, VisibleBefore = visibleBefore, VisibleAfter = Native.IsWindowVisible(window), ForegroundPreserved = Native.GetForegroundWindow() == foreground
        }));
        if (alpha < 256 * 128) return 77;
    }
    catch (Exception ex) { Console.WriteLine(windowClass + ": " + ex.Message); return 77; }
}
return 0;
static class Native
{
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindow(string className, string? title);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string className, string? title);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr context);
    public delegate bool EnumProc(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr parameter);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc callback, IntPtr parameter);
    [DllImport("user32.dll",CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr window, System.Text.StringBuilder name, int count);
}

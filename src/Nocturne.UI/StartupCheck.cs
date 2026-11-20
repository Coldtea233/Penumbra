using Microsoft.UI.Dispatching;

namespace Nocturne;

// Opt-in regression check: construct/activate the real published window and
// verify its loaded controls. No Apply, wallpaper capture or settings writes.
internal static class StartupCheck
{
    private static string? report;
    private static bool check;
    private static DispatcherQueueTimer? timer;
    internal static bool Enabled => check;
    internal static bool Isolated { get; private set; }
    internal static bool UpdateCheckRequested { get; private set; }
    internal static bool LayoutTestRequested { get; private set; }
    internal static string? InstanceTestScope { get; private set; }
    internal static void Configure(string[] args)
    {
        UpdateCheckRequested = args.Contains("--update-test", StringComparer.Ordinal);
        LayoutTestRequested = args.Contains("--layout-test", StringComparer.Ordinal);
        string? instanceTest = args.FirstOrDefault(a => a.StartsWith("--instance-test=", StringComparison.Ordinal));
        if (instanceTest is not null)
        {
            if (!Guid.TryParse(instanceTest["--instance-test=".Length..], out var scope))
                throw new ArgumentException("Invalid isolated instance-test scope.");
            InstanceTestScope = scope.ToString("N");
        }
        Isolated = InstanceTestScope is not null || UpdateCheckRequested || LayoutTestRequested || args.Contains("--ui-test", StringComparer.Ordinal);
        const string prefix = "--startup-check=";
        string? option = args.FirstOrDefault(a => a.StartsWith(prefix, StringComparison.Ordinal));
        check = option is not null;
        Isolated |= check;
        if (option is null)
        {
            const string diagnosticPrefix = "--startup-log=";
            option = args.FirstOrDefault(a => a.StartsWith(diagnosticPrefix, StringComparison.Ordinal));
            if (option is null) return;
            report = Path.GetFullPath(option[diagnosticPrefix.Length..]);
            File.WriteAllText(report, "STARTING NORMAL\n");
            return;
        }
        report = Path.GetFullPath(option[prefix.Length..]);
        File.WriteAllText(report, "STARTING");
    }
    internal static DesktopSnapshot LayoutSnapshot()
    {
        // A deterministic image for isolated sizing/screenshot tests. Never
        // capture a user's desktop or apply test controls to their renderer.
        const int width = 640, height = 360;
        var pixels = new byte[width * height * 4];
        for (int y = 0; y < height; y++) for (int x = 0; x < width; x++)
        {
            int i = (y * width + x) * 4;
            bool grid = x % 80 < 2 || y % 60 < 2;
            pixels[i] = (byte)(grid ? 200 : 50 + x * 90 / width);
            pixels[i + 1] = (byte)(grid ? 200 : 60 + y * 80 / height);
            pixels[i + 2] = (byte)(grid ? 200 : 90 + x * 70 / width);
            pixels[i + 3] = 255;
        }
        return new DesktopSnapshot(pixels, width, height, new byte[width * height]);
    }
    internal static void Stage(string stage)
    {
        if (report is null) return;
        try { File.AppendAllText(report, stage + "\n"); } catch { }
    }
    internal static void Failed(Exception exception)
    {
        if (report is null) return;
        try { File.WriteAllText(report, "FAIL\n" + exception); } catch { }
    }
    internal static void Run(MainWindow window)
    {
        timer = window.DispatcherQueue.CreateTimer();
        timer.Interval = TimeSpan.FromMilliseconds(750);
        timer.IsRepeating = false;
        timer.Tick += (_, _) =>
        {
            timer.Stop();
            try
            {
                window.VerifyStartup();
                File.WriteAllText(report!, $"PASS\nPublished WinUI window loaded and visible; {window.DisplayVersion}; gray update link without underline and result states verified without network; startup label verified; blur/saturation controls synchronized; commands enabled; settings not written.");
            }
            catch (Exception ex) { Failed(ex); }
            finally { window.Close(); }
        };
        timer.Start();
    }
}

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
    internal static void Configure(string[] args)
    {
        UpdateCheckRequested = args.Contains("--update-test", StringComparer.Ordinal);
        Isolated = UpdateCheckRequested || args.Contains("--ui-test", StringComparer.Ordinal);
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
                File.WriteAllText(report!, "PASS\nPublished WinUI window loaded and visible; v1.8.2; gray update link without underline and result states verified without network; startup label verified; blur/saturation controls synchronized; commands enabled; settings not written.");
            }
            catch (Exception ex) { Failed(ex); }
            finally { window.Close(); }
        };
        timer.Start();
    }
}

using Microsoft.UI.Xaml;
using System.Runtime.InteropServices;

namespace Nocturne;
public partial class App : Application
{
    private Window? window;
    private Mutex? instance;
    public App()
    {
        UnhandledException += (_, e) => Log(e.Exception);
        InitializeComponent();
    }
    private static void Log(Exception e)
    {
        try { Directory.CreateDirectory(Settings.DirectoryPath); File.WriteAllText(Path.Combine(Settings.DirectoryPath, "ui-error.txt"), e.ToString()); } catch { }
    }
    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        instance = new Mutex(true, @"Local\VignFinder.Settings.v3", out bool first);
        if (!first)
        {
            var hwnd = FindWindow(null, "Penumbra 半影");
            if (hwnd != IntPtr.Zero) { ShowWindow(hwnd, 9); SetForegroundWindow(hwnd); }
            Exit();
            return;
        }
        try
        {
            window = new MainWindow();
            window.Activate();
        }
        catch (Exception e) { Log(e); throw; }
    }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr FindWindow(string? className, string title);
    [DllImport("user32.dll")] private static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] private static extern bool ShowWindow(IntPtr hwnd, int command);
}

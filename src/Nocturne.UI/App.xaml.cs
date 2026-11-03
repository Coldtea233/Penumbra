using Microsoft.UI.Xaml;
using System.Runtime.InteropServices;

namespace Nocturne;
public partial class App : Application
{
    // Keep this identity stable across future in-place updates.
    private const string SettingsIdentity = "Penumbra.Settings";
    private Window? window;
    private Mutex? instance;
    public App()
    {
        StartupCheck.Stage("App constructor");
        UnhandledException += (_, e) => { StartupCheck.Failed(e.Exception); Log(e.Exception); };
        InitializeComponent();
        StartupCheck.Stage("App XAML loaded");
    }
    private static void Log(Exception e)
    {
        try { Directory.CreateDirectory(Settings.DirectoryPath); File.WriteAllText(Path.Combine(Settings.DirectoryPath, "ui-error.txt"), e.ToString()); } catch { }
    }
    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        StartupCheck.Stage("OnLaunched");
        // Older builds used a shared mutex and the same title. An open older
        // settings window could silently intercept launching this upgrade.
        instance = new Mutex(true, StartupCheck.Isolated ? @"Local\Penumbra.StartupCheck." + Environment.ProcessId : @"Local\" + SettingsIdentity, out bool first);
        StartupCheck.Stage("Singleton first=" + first);
        if (!first)
        {
            IntPtr hwnd = IntPtr.Zero;
            for (int retry = 0; retry < 20 && hwnd == IntPtr.Zero; retry++)
            {
                EnumWindows((candidate, _) =>
                {
                    if (GetProp(candidate, SettingsIdentity) == IntPtr.Zero) return true;
                    hwnd = candidate;
                    return false;
                }, IntPtr.Zero);
                if (hwnd == IntPtr.Zero) Thread.Sleep(50);
            }
            if (hwnd != IntPtr.Zero) { ShowWindow(hwnd, 9); SetForegroundWindow(hwnd); }
            else MessageBox(IntPtr.Zero, "已有 Penumbra 设置进程，但暂时无法找到窗口。请关闭该设置进程后重试。", "Penumbra", 0x10);
            Exit();
            return;
        }
        try
        {
            var main = new MainWindow();
            window = main;
            SetProp(WinRT.Interop.WindowNative.GetWindowHandle(main), SettingsIdentity, (IntPtr)1);
            window.Activate();
            StartupCheck.Stage("Window activated");
            if (StartupCheck.Enabled) StartupCheck.Run(main);
        }
        catch (Exception e) { StartupCheck.Failed(e); Log(e); throw; }
    }
    private delegate bool EnumWindowCallback(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumWindowCallback callback, IntPtr parameter);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr GetProp(IntPtr window, string name);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern bool SetProp(IntPtr window, string name, IntPtr value);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int MessageBox(IntPtr window, string text, string caption, uint type);
    [DllImport("user32.dll")] private static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] private static extern bool ShowWindow(IntPtr hwnd, int command);
}

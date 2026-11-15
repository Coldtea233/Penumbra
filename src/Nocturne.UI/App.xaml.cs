using Microsoft.UI.Xaml;
using System.Runtime.InteropServices;

namespace Nocturne;
public partial class App : Application
{
    // Keep this identity stable across future in-place updates.
    private Window? window;
    private SettingsInstance? instance;
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
    protected override async void OnLaunched(LaunchActivatedEventArgs args)
    {
        StartupCheck.Stage("OnLaunched");
        try
        {
            string identity = StartupCheck.InstanceTestScope is string scope ? SettingsInstance.WindowProperty + ".Test." + scope
                : StartupCheck.Isolated ? "Penumbra.StartupCheck." + Environment.ProcessId : SettingsInstance.WindowProperty;
            instance = new SettingsInstance(identity, StartupCheck.Isolated, typeof(App).Assembly.GetName().Version!);
            if (!await instance.AcquireAsync()) { instance.Dispose(); Exit(); return; }
            StartupCheck.Stage("Settings instance acquired");
            var main = new MainWindow();
            window = main;
            instance.Attach(WinRT.Interop.WindowNative.GetWindowHandle(main), main.CloseForReplacement);
            main.Closed += (_, _) => instance.Dispose();
            window.Activate();
            StartupCheck.Stage("Window activated");
            if (StartupCheck.Enabled) StartupCheck.Run(main);
        }
        catch (Exception e)
        {
            StartupCheck.Failed(e); Log(e); instance?.Dispose();
            MessageBox(IntPtr.Zero, "无法打开 Penumbra 设置窗口：" + e.Message, "Penumbra", 0x10);
            Exit();
        }
    }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int MessageBox(IntPtr window, string text, string caption, uint type);
}

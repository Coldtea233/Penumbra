using Microsoft.UI;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Microsoft.Win32;
using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices.WindowsRuntime;
using System.Runtime.InteropServices;

namespace Nocturne;
public sealed partial class MainWindow : Window
{
    private bool ready;
    private bool busy;
    private bool previewRenderPending;
    private bool updatingBlurControls;
    private bool? wideLayout;
    private DesktopSnapshot? snapshot;
    private double previewAspect = 16.0 / 9.0;
    private DispatcherQueueTimer? previewTimer;
    private readonly DispatcherQueueTimer wallpaperStatusTimer;
    private readonly WindowSizing windowSizing;
    private bool checkingWallpaperStatus;
    private bool windowClosed;
    private WriteableBitmap? previewBitmap;
    private byte[]? previewPixels;
    private byte[]? blurredPreview;
    private double blurredPreviewRadius = -1;
    private const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private static string RendererPath => Path.Combine(AppContext.BaseDirectory, "Nocturne.Renderer.exe");
    public string DisplayVersion => typeof(App).Assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()!.InformationalVersion.Split('+')[0];
    public MainWindow()
    {
        InitializeComponent();
        SystemBackdrop = new MicaBackdrop();
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(TitleBar);
        var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
        AppWindow.SetIcon(Path.Combine(AppContext.BaseDirectory, "Assets", "AppIcon.ico"));
        windowSizing = new WindowSizing(hwnd, 480, 320);
        wallpaperStatusTimer = DispatcherQueue.GetForCurrentThread().CreateTimer();
        wallpaperStatusTimer.Interval = TimeSpan.FromSeconds(5);
        wallpaperStatusTimer.Tick += async (_, _) => await RefreshWallpaperStatusAsync();
        Activated += async (_, e) =>
        {
            if (ready && e.WindowActivationState != WindowActivationState.Deactivated)
                await RefreshWallpaperStatusAsync();
        };
        Closed += (_, _) =>
        {

            windowClosed = true;
            wallpaperStatusTimer.Stop();
            previewTimer?.Stop();
            windowSizing.Dispose();
        };
        previewTimer = DispatcherQueue.GetForCurrentThread().CreateTimer();
        previewTimer.Interval = TimeSpan.FromMilliseconds(35);
        previewTimer.IsRepeating = false;
        previewTimer.Tick += (_, _) =>
        {
            previewTimer.Stop();
            previewRenderPending = false;
            if (snapshot is not null && ready)
            {
                try { DrawPreview(ReadControls()); }
                catch (Exception e) { StatusText.Text = "预览更新失败：" + e.Message; }
            }
        };
        double scale = GetDpiForWindow(hwnd) / 96.0;
        var area = Microsoft.UI.Windowing.DisplayArea.GetFromWindowId(AppWindow.Id, Microsoft.UI.Windowing.DisplayAreaFallback.Primary).WorkArea;
        int width = Math.Min((int)(1000 * scale), Math.Max(600, area.Width - (int)(32 * scale)));
        int height = Math.Min((int)(920 * scale), Math.Max(500, area.Height - (int)(32 * scale)));
        AppWindow.MoveAndResize(new Windows.Graphics.RectInt32(area.X + (area.Width - width) / 2, area.Y + (area.Height - height) / 2, width, height));
        if (AppWindow.Presenter is Microsoft.UI.Windowing.OverlappedPresenter presenter)
        {
            presenter.IsMaximizable = true;
            presenter.IsResizable = true;
        }
        try
        {
            LoadControls(Settings.Load());
            using var key = Registry.CurrentUser.OpenSubKey(RunKey);
            StartupToggle.IsOn = key?.GetValue("Nocturne") is string;
        }
        catch (Exception e) { StatusText.Text = "读取配置失败：" + e.Message; }
        ready = true;
        if (!OperatingSystem.IsWindowsVersionAtLeast(10, 0, 22000))
        {
            BlurEnabledCheckBox.IsEnabled = false;
            ToolTipService.SetToolTip(BlurEnabledCheckBox, "桌面实时模糊需要 Windows 11；当前系统仍可正常使用暗角。");
        }
        UpdateBlurControls();
    }
    private void LoadControls(Settings s)
    {
        bool wasReady = ready;
        ready = false;
        OpacitySlider.Value = s.Opacity * 100; RadiusSlider.Value = s.Radius * 100;
        FeatherSlider.Value = s.Feather * 100; ShapeSlider.Value = s.Shape * 100;
        CenterXBox.Value = s.CenterX * 100; CenterYBox.Value = s.CenterY * 100;
        TintCombo.SelectedIndex = s.Tint; MonitorCombo.SelectedIndex = s.PrimaryOnly ? 1 : 0;
        DarkenIconsCheckBox.IsChecked = s.DarkenIcons;
        FocusOnlyCheckBox.IsChecked = s.FocusOnly;
        BlurEnabledCheckBox.IsChecked = s.BlurEnabled;
        BlurRadiusSlider.Value = s.BlurRadius;
        UpdateBlurControls();
        ready = wasReady;
        if (wasReady) SchedulePreviewRender();
    }
    private Settings ReadControls()
    {
        if (!double.IsFinite(CenterXBox.Value) || !double.IsFinite(CenterYBox.Value))
            throw new InvalidOperationException("中心位置必须是 0 到 100 的数字。");
        return new(OpacitySlider.Value / 100, RadiusSlider.Value / 100, FeatherSlider.Value / 100,
            ShapeSlider.Value / 100, Math.Clamp(CenterXBox.Value / 100, 0, 1), Math.Clamp(CenterYBox.Value / 100, 0, 1),
            Math.Clamp(TintCombo.SelectedIndex, 0, 2), MonitorCombo.SelectedIndex == 1,
            DarkenIconsCheckBox.IsChecked == true, FocusOnlyCheckBox.IsChecked == true,
            OperatingSystem.IsWindowsVersionAtLeast(10, 0, 22000) && BlurEnabledCheckBox.IsChecked == true,
            BlurRadiusSlider.Value);
    }
    private void UpdateBlurControls()
    {
        if (BlurRadiusSlider is null || BlurRadiusBox is null || updatingBlurControls) return;
        updatingBlurControls = true;
        try { BlurRadiusBox.Value = BlurRadiusSlider.Value; }
        finally { updatingBlurControls = false; }
    }
    private void BlurOption_Changed(object sender, RoutedEventArgs e)
    {
        UpdateBlurControls();

        SchedulePreviewRender();
    }
    private void BlurRadius_Changed(object sender, RangeBaseValueChangedEventArgs e)
    {
        if (updatingBlurControls) return;
        UpdateBlurControls();

        SchedulePreviewRender();
    }
    private void BlurRadiusBox_Changed(NumberBox sender, NumberBoxValueChangedEventArgs e)
    {
        if (updatingBlurControls || BlurRadiusSlider is null || !double.IsFinite(e.NewValue)) return;
        updatingBlurControls = true;
        try { BlurRadiusSlider.Value = Math.Clamp(Math.Round(e.NewValue), 0, 48); }
        finally { updatingBlurControls = false; }
        UpdateBlurControls();

        SchedulePreviewRender();
    }
    private void Parameter_Changed(object sender, RangeBaseValueChangedEventArgs e)
    {
        SchedulePreviewRender();
    }
    private void NumericParameter_Changed(NumberBox sender, NumberBoxValueChangedEventArgs e)
    {
        SchedulePreviewRender();
    }
    private void DiscreteParameter_Changed(object sender, SelectionChangedEventArgs e)
    {
        SchedulePreviewRender();
    }
    private void IconOption_Changed(object sender, RoutedEventArgs e)
    {
        SchedulePreviewRender();
    }
    private void SchedulePreviewRender()
    {
        if (!ready || previewTimer is null) return;
        if (previewRenderPending) return;
        previewRenderPending = true;
        previewTimer.Start();
    }
    private void Reset_Click(object sender, RoutedEventArgs e)
    {

        LoadControls(new Settings());
        StatusText.Text = "已恢复默认参数";
    }
    private void Root_SizeChanged(object sender, SizeChangedEventArgs e)
    {
        if (e.NewSize.Width <= 0) return;
        bool wide = e.NewSize.Width >= 920;
        if (wideLayout == wide) return;
        wideLayout = wide;
        PreviewColumn.MinWidth = wide ? 300 : 0;
        ParameterColumn.Width = new GridLength(wide ? 340 : 0);
        SettingsLayout.ColumnSpacing = wide ? 28 : 0;
        Grid.SetColumn(ParameterPanel, wide ? 1 : 0);
        Grid.SetRow(ParameterPanel, wide ? 0 : 1);
        Grid.SetRowSpan(ParameterPanel, wide ? 2 : 1);
        Grid.SetRow(AboutSection, wide ? 1 : 2);
        AboutSection.Margin = wide ? new Thickness(0, 24, 24, 0) : new Thickness(0, 24, 0, 0);
        ParameterPanel.Margin = wide ? new Thickness(8, 8, 0, 0) : new Thickness(0, 24, 0, 0);
    }
    private void Preview_SizeChanged(object sender, SizeChangedEventArgs e)
    {
        ResizePreview();
        // The existing ImageBrush scales automatically; resizing does not
        // need another capture or CPU vignette render.
    }
    private void ResizePreview()
    {
        if (PreviewHost.ActualWidth <= 0 || previewAspect <= 0) return;
        double height = PreviewHost.ActualWidth / previewAspect;
        if (Math.Abs(PreviewHost.Height - height) > 0.5) PreviewHost.Height = height;
    }
    private void DrawPreview(Settings s)
    {
        if (snapshot is null) return;
        int w = snapshot.Width, h = snapshot.Height;
        if (previewBitmap is null || previewPixels is null || previewPixels.Length != w * h * 4)
        {
            previewBitmap = new WriteableBitmap(w, h);
            previewPixels = new byte[w * h * 4];
            PreviewCanvas.Background = new ImageBrush { ImageSource = previewBitmap, Stretch = Stretch.Uniform };
        }
        var pixels = previewPixels;
        byte[] source = snapshot.Pixels;
        if (s.BlurEnabled)
        {
            if (blurredPreview is null || blurredPreviewRadius != s.BlurRadius)
            {
                // Approximation only: host-backdrop's baseline depends on
                // Windows, DPI and policy. Never claim screenshot equality.
                double sigma = BlurParameters.PreviewSigma(s.BlurRadius, w, GetSystemMetrics(0));
                blurredPreview = PreviewBlur.Apply(snapshot.Pixels, w, h, sigma);
                blurredPreviewRadius = s.BlurRadius;
            }
            source = blurredPreview;
        }
        int[] tint = s.Tint switch { 1 => [14, 22, 40], 2 => [27, 18, 16], _ => [0, 0, 0] };
        for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            double nx = (x + .5) / w, ny = (y + .5) / h;
            double dx = (nx - s.CenterX) * 2 / s.Shape, dy = (ny - s.CenterY) * 2;
            double t = Math.Clamp((Math.Sqrt(dx * dx + dy * dy) - s.Radius) / s.Feather, 0, 1);
            double a = snapshot.Icons[y * w + x] != 0 && !s.DarkenIcons ? 0 : s.Opacity * t * t * (3 - 2 * t);
            int i = (y * w + x) * 4;
            pixels[i] = (byte)(source[i] * (1 - a) + tint[2] * a);
            pixels[i + 1] = (byte)(source[i + 1] * (1 - a) + tint[1] * a);
            pixels[i + 2] = (byte)(source[i + 2] * (1 - a) + tint[0] * a); pixels[i + 3] = 255;
        }
        using (var stream = previewBitmap.PixelBuffer.AsStream())
        {
            stream.Position = 0;
            stream.Write(pixels);
        }
        previewBitmap.Invalidate();
    }
    private async void CapturePreview_Click(object sender, RoutedEventArgs e)
    {
        if (busy) return;
        busy = true;
        SetCommandsEnabled(false);
        try
        {
            await CapturePreviewCoreAsync();
        }
        catch (Exception ex) { StatusText.Text = "读取预览失败：" + ex.Message; }
        finally { busy = false; SetCommandsEnabled(true); }
    }
    private async void MainWindow_Loaded(object sender, RoutedEventArgs e)
    {
        wallpaperStatusTimer.Start();
        _ = RefreshWallpaperStatusAsync();
        if (busy || snapshot is not null) return;
        busy = true;
        SetCommandsEnabled(false);
        try { await CapturePreviewCoreAsync(); }
        catch (Exception ex) { StatusText.Text = "壁纸刷新失败：" + ex.Message; }
        finally { busy = false; SetCommandsEnabled(true); }
    }
    private async Task CapturePreviewCoreAsync()
    {
        _ = RefreshWallpaperStatusAsync();
        StatusText.Text = "正在刷新壁纸…";
        var captured = await DesktopSnapshot.CaptureAsync();
        snapshot = captured;
        blurredPreview = null;
        previewAspect = (double)captured.Width / captured.Height;
        ResizePreview();
        PreviewEmpty.Visibility = Visibility.Collapsed;
        DrawPreview(ReadControls());
        StatusText.Text = "壁纸预览已刷新";
    }
    private async Task RefreshWallpaperStatusAsync()
    {
        if (windowClosed || checkingWallpaperStatus || !AppWindow.IsVisible ||
            IsIconic(WinRT.Interop.WindowNative.GetWindowHandle(this))) return;
        checkingWallpaperStatus = true;
        try
        {
            var state = await Task.Run(WallpaperEngineStatus.Detect);
            if (windowClosed) return;
            bool running = state == WallpaperEngineState.Running;
            WallpaperEngineDot.Fill = new SolidColorBrush(running
                ? Windows.UI.Color.FromArgb(255, 99, 204, 134)
                : Windows.UI.Color.FromArgb(255, 139, 144, 152));
            string description = state switch
            {
                WallpaperEngineState.Running => "运行中（已检测到当前会话的 Wallpaper Engine 进程）",
                WallpaperEngineState.Unavailable => "暂时无法读取进程状态",
                _ => "未检测到 Wallpaper Engine 进程"
            };
            AutomationProperties.SetName(WallpaperEngineIndicator, "Wallpaper Engine 活动状态：" + description);
            AutomationProperties.SetName(WallpaperEngineLabel, "Wallpaper Engine 活动状态：" + description);
            ToolTipService.SetToolTip(WallpaperEngineIndicator, description);
        }
        finally { checkingWallpaperStatus = false; }
    }
    private void SetCommandsEnabled(bool enabled)
    {
        CaptureButton.IsEnabled = ApplyButton.IsEnabled = StopButton.IsEnabled = enabled;
    }
    private async void Apply_Click(object sender, RoutedEventArgs e)
    {
        if (busy) return;

        busy = true;
        SetCommandsEnabled(false);
        SettingsScrollViewer.IsEnabled = false;
        try
        {
            if (!File.Exists(RendererPath)) throw new FileNotFoundException("找不到原生渲染程序，请将完整发行包解压到同一文件夹。", RendererPath);
            var s = ReadControls();
            string? previewError = null;
            try { await CapturePreviewCoreAsync(); }
            catch (Exception ex) { previewError = ex.Message; }
            s = ReadControls();
            s.Save();
            using (var key = Registry.CurrentUser.CreateSubKey(RunKey))
            {
                if (StartupToggle.IsOn) key.SetValue("Nocturne", $"\"{RendererPath}\"");
                else key.DeleteValue("Nocturne", false);
            }
            StatusText.Text = "正在启动桌面效果…";
            await Task.Run(() =>
            {
                using var current = Process.GetCurrentProcess();
                var running = Process.GetProcessesByName("Nocturne.Renderer");
                try
                {
                    try { using var stop = EventWaitHandle.OpenExisting(@"Local\Nocturne.Stop.v1"); stop.Set(); }
                    catch (WaitHandleCannotBeOpenedException) { }
                    foreach (var old in running)
                        if (old.SessionId == current.SessionId && !old.WaitForExit(5000))
                            throw new InvalidOperationException("旧后台尚未退出，请从托盘退出暗角后重试。");
                }
                finally { foreach (var old in running) old.Dispose(); }
            });
            using var applied = new EventWaitHandle(false, EventResetMode.AutoReset, @"Local\Nocturne.Applied.v1");
            applied.Reset();
            using var process = Process.Start(new ProcessStartInfo(RendererPath) { UseShellExecute = false });
            StatusText.Text = "正在生成并挂接桌面效果…";
            bool completed = await Task.Run(() => applied.WaitOne(TimeSpan.FromSeconds(12)));
            DrawPreview(s);
            string status = Path.Combine(Settings.DirectoryPath, "status.txt");
            string detail = completed && File.Exists(status) ? File.ReadAllText(status).Trim() : "后台未响应，请检查进程或重试。";
            StatusText.Text = completed && File.Exists(status) ? detail : "后台未响应，请检查进程或重试。";
            if (previewError is not null) StatusText.Text += " · 预览未刷新";
        }
        catch (Exception ex) { StatusText.Text = "应用失败：" + ex.Message; }
        finally
        {
            SettingsScrollViewer.IsEnabled = true;
            busy = false;
            SetCommandsEnabled(true);
        }
    }
    private void Stop_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            using var stop = EventWaitHandle.OpenExisting(@"Local\Nocturne.Stop.v1");
            stop.Set(); StatusText.Text = "已请求恢复桌面";
        }
        catch (WaitHandleCannotBeOpenedException) { StatusText.Text = "后台未运行"; }
        catch (Exception ex) { StatusText.Text = "关闭失败：" + ex.Message; }
    }
    [DllImport("user32.dll")] private static extern uint GetDpiForWindow(IntPtr hwnd);
    [DllImport("user32.dll")] private static extern int GetSystemMetrics(int index);
    [DllImport("user32.dll")] private static extern bool IsIconic(IntPtr hwnd);
}

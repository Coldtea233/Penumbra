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
    private bool blurRadiusPending;
    private bool blurRadiusPreviewActive;
    private bool wallpaperRunning, updatingSaturationControls, saturationPending, saturationPreviewActive;
    private bool? wideLayout;
    private DesktopSnapshot? snapshot;
    private double previewAspect = 16.0 / 9.0;
    private DispatcherQueueTimer? previewTimer;
    private readonly DispatcherQueueTimer wallpaperStatusTimer;
    private readonly WindowSizing windowSizing;
    private bool checkingWallpaperStatus;
    private bool windowClosed;
    private bool closeApproved, closePending;
    private bool startupUpdateChecked, checkingUpdate;
    private readonly CancellationTokenSource updateCancellation = new();
    private WriteableBitmap? previewBitmap;
    private byte[]? previewPixels;
    private byte[]? colorPreview;
    private double colorPreviewSaturation = -1;
    private bool colorPreviewDarkenIcons;
    private const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private static string RendererPath => Path.Combine(AppContext.BaseDirectory, "Nocturne.Renderer.exe");
    public string DisplayVersion => UpdateChecker.DisplayVersion(typeof(App).Assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()!.InformationalVersion);
    public MainWindow()
    {
        StartupCheck.Stage("MainWindow InitializeComponent begin");
        InitializeComponent();
        UpdateInfoBar.Closed += (_, _) => UpdateInfoBar.Visibility = Visibility.Collapsed;
        AutomationProperties.SetHelpText(CheckUpdateLink, "通过 GitHub Releases 检查新版本；启动时自动检查一次。");
        StartupCheck.Stage("MainWindow XAML connected");
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
        AppWindow.Closing += async (_, e) =>
        {
            if (closeApproved || StartupCheck.Isolated) return;
            e.Cancel = true;
            if (closePending) return;
            if (busy) { StatusText.Text = "正在应用参数，请稍后关闭窗口"; return; }
            closePending = true;
            try
            {
                // Keep existing effects untouched. A new tray-only backend is
                // paused until Apply or an explicit tray Resume command.
                if (FindWindow("Nocturne.Controller", null) == IntPtr.Zero)
                {
                    using var readyEvent = new EventWaitHandle(false, EventResetMode.ManualReset, @"Local\Penumbra.TrayReady.v1");
                    readyEvent.Reset();
                    using var backend = Process.Start(new ProcessStartInfo(RendererPath, "--tray-only") { UseShellExecute = false, CreateNoWindow = true });
                    if (!await Task.Run(() => readyEvent.WaitOne(TimeSpan.FromSeconds(5))))
                        throw new InvalidOperationException("无法创建托盘图标，窗口已保留，请重试。");
                }
                closeApproved = true;
                Close();
            }
            catch (Exception ex) { StatusText.Text = "收起到托盘失败：" + ex.Message; }
            finally { closePending = false; }
        };
        Closed += (_, _) =>
        {
            CancelDesktopBlurRadiusPreview();
            CancelDesktopSaturationPreview();
            windowClosed = true;
            updateCancellation.Cancel();
            updateCancellation.Dispose();
            wallpaperStatusTimer.Stop();
            previewTimer?.Stop();
            windowSizing.Dispose();
            Application.Current.Exit();
        };
        previewTimer = DispatcherQueue.GetForCurrentThread().CreateTimer();
        previewTimer.Interval = TimeSpan.FromMilliseconds(35);
        previewTimer.IsRepeating = false;
        previewTimer.Tick += (_, _) =>
        {
            previewTimer.Stop();
            previewRenderPending = false;
            if (blurRadiusPending && ready && !busy)
            {
                blurRadiusPending = false;
                PreviewDesktopBlurRadius();
            }
            if (saturationPending && ready && !busy)
            {
                saturationPending = false;
                PreviewDesktopSaturation();
            }
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
        UpdateSaturationControls();
        StartupCheck.Stage("MainWindow constructor completed");
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
        BlurEnabledCheckBox.IsOn = s.BlurEnabled;
        BlurRadiusSlider.Value = s.BlurRadius;
        SaturationEnabledCheckBox.IsOn = s.SaturationEnabled;
        SaturationSlider.Value = s.Saturation;
        UpdateBlurControls();
        UpdateSaturationControls();
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
            OperatingSystem.IsWindowsVersionAtLeast(10, 0, 22000) && BlurEnabledCheckBox.IsOn,
            BlurRadiusSlider.Value,
            OperatingSystem.IsWindowsVersionAtLeast(10, 0, 22000) && SaturationEnabledCheckBox.IsOn,
            SaturationSlider.Value);
    }
    private void UpdateSaturationControls()
    {
        // XAML may raise ValueChanged before the later named elements exist.
        // Synchronize only a fully connected group, never a partially loaded tree.
        if (SaturationSlider is null || SaturationBox is null || SaturationEnabledCheckBox is null ||
            SaturationAvailabilityText is null || updatingSaturationControls) return;
        updatingSaturationControls = true;
        try
        {
            SaturationBox.Value = SaturationSlider.Value;
            bool available = wallpaperRunning && OperatingSystem.IsWindowsVersionAtLeast(10, 0, 22000);
            SaturationEnabledCheckBox.IsEnabled = SaturationSlider.IsEnabled = SaturationBox.IsEnabled = available;
            SaturationAvailabilityText.Text = !OperatingSystem.IsWindowsVersionAtLeast(10, 0, 22000)
                ? "此功能需要 Windows 11" : available ? "" : "运行 Wallpaper Engine 后可启用";
            SaturationAvailabilityText.Visibility = available ? Visibility.Collapsed : Visibility.Visible;
        }
        finally { updatingSaturationControls = false; }
    }
    private void SaturationOption_Changed(object sender, RoutedEventArgs e)
    {
        if (!ready) return;
        colorPreview = null;
        ScheduleDesktopSaturationPreview();
        SchedulePreviewRender();
    }
    private void Saturation_Changed(object sender, RangeBaseValueChangedEventArgs e)
    {
        if (!ready || updatingSaturationControls) return;
        UpdateSaturationControls();
        colorPreview = null;
        ScheduleDesktopSaturationPreview();
        SchedulePreviewRender();
    }
    private void SaturationBox_Changed(NumberBox sender, NumberBoxValueChangedEventArgs e)
    {
        if (!ready || updatingSaturationControls || SaturationSlider is null || !double.IsFinite(e.NewValue)) return;
        updatingSaturationControls = true;
        try { SaturationSlider.Value = Math.Clamp(Math.Round(e.NewValue), 0, 100); }
        finally { updatingSaturationControls = false; }
        UpdateSaturationControls();
        colorPreview = null;
        ScheduleDesktopSaturationPreview();
        SchedulePreviewRender();
    }
    private void ScheduleDesktopSaturationPreview()
    {
        if (!ready || busy) return;
        if (!wallpaperRunning || !SaturationEnabledCheckBox.IsOn)
        {
            CancelDesktopSaturationPreview();
            return;
        }
        saturationPending = true;
        SchedulePreviewRender();
    }
    private static nuint SendSaturationCommand(uint command)
    {
        var controller = FindWindow("Nocturne.Controller", null);
        if (controller == IntPtr.Zero) return 0;
        return SendMessageTimeout(controller, 0x8007, (nuint)command, (IntPtr)Environment.ProcessId,
            0x0002, 100, out var result) != IntPtr.Zero ? result : 0;
    }
    private void PreviewDesktopSaturation()
    {
        uint saturation = (uint)Math.Clamp(Math.Round(SaturationSlider.Value), 0, 100);
        if (SendSaturationCommand(saturation) == saturation + 1)
        {
            saturationPreviewActive = true;
            StatusText.Text = $"饱和度已预览：{saturation}% · 点击应用保存";
        }
        else StatusText.Text = "降饱和度尚未启用 · 勾选并点击应用后生效";
    }
    private void CancelDesktopSaturationPreview()
    {
        saturationPending = false;
        if (!saturationPreviewActive) return;
        SendSaturationCommand(101);
        saturationPreviewActive = false;
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
        ScheduleDesktopBlurRadiusPreview();
        SchedulePreviewRender();
    }
    private void BlurRadius_Changed(object sender, RangeBaseValueChangedEventArgs e)
    {
        if (updatingBlurControls) return;
        UpdateBlurControls();
        ScheduleDesktopBlurRadiusPreview();
        SchedulePreviewRender();
    }
    private void BlurRadiusBox_Changed(NumberBox sender, NumberBoxValueChangedEventArgs e)
    {
        if (updatingBlurControls || BlurRadiusSlider is null || !double.IsFinite(e.NewValue)) return;
        updatingBlurControls = true;
        try { BlurRadiusSlider.Value = Math.Clamp(Math.Round(e.NewValue), 0, 48); }
        finally { updatingBlurControls = false; }
        UpdateBlurControls();
        ScheduleDesktopBlurRadiusPreview();
        SchedulePreviewRender();
    }
    private void ScheduleDesktopBlurRadiusPreview()
    {
        if (!ready || busy) return;
        if (!BlurEnabledCheckBox.IsOn)
        {
            blurRadiusPending = false;
            CancelDesktopBlurRadiusPreview();
            return;
        }
        blurRadiusPending = true;
        SchedulePreviewRender();
    }
    private static nuint SendBlurRadiusCommand(uint command)
    {
        var controller = FindWindow("Nocturne.Controller", null);
        if (controller == IntPtr.Zero) return 0;
        return SendMessageTimeout(controller, 0x8006, (nuint)command, (IntPtr)Environment.ProcessId,
            0x0002, 100, out var result) != IntPtr.Zero ? result : 0;
    }
    private void PreviewDesktopBlurRadius()
    {
        uint radius = (uint)Math.Clamp(Math.Round(BlurRadiusSlider.Value), 0, 48);
        nuint result = SendBlurRadiusCommand(radius);
        if (result == radius + 1)
        {
            blurRadiusPreviewActive = true;
            StatusText.Text = $"桌面模糊半径已预览：{radius} px · 点击应用保存";
        }
        else StatusText.Text = "桌面模糊尚未启用 · 勾选模糊并点击应用后生效";
    }
    private void CancelDesktopBlurRadiusPreview()
    {
        blurRadiusPending = false;
        if (!blurRadiusPreviewActive) return;
        SendBlurRadiusCommand(49);
        blurRadiusPreviewActive = false;
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
        CancelDesktopBlurRadiusPreview();
        CancelDesktopSaturationPreview();
        colorPreview = null;
        LoadControls(new Settings());
        StatusText.Text = "已恢复默认参数";
    }
    private void Root_SizeChanged(object sender, SizeChangedEventArgs e)
    {
        if (e.NewSize.Width <= 0) return;
        bool wide = e.NewSize.Width >= 920;
        if (wideLayout == wide) return;
        ApplyResponsiveLayout(wide);
    }
    private void ApplyResponsiveLayout(bool wide)
    {
        wideLayout = wide;
        // Keep one set of controls/state while moving the experimental group.
        // Narrow order: preview, vignette, experiments, About.
        if (ExperimentalPanel.Parent is Panel previous) previous.Children.Remove(ExperimentalPanel);
        if (wide) PreviewPanel.Children.Add(ExperimentalPanel);
        else SettingsLayout.Children.Add(ExperimentalPanel);
        Grid.SetColumn(ExperimentalPanel, 0);
        Grid.SetRow(ExperimentalPanel, 2);
        ExperimentalPanel.Margin = new Thickness(0, wide ? 12 : 24, 0, 0);
        PreviewColumn.MinWidth = wide ? 300 : 0;
        ParameterColumn.Width = new GridLength(wide ? 340 : 0);
        SettingsLayout.ColumnSpacing = wide ? 28 : 0;
        Grid.SetColumn(ParameterPanel, wide ? 1 : 0);
        Grid.SetRow(ParameterPanel, wide ? 0 : 1);
        Grid.SetRowSpan(PreviewPanel, wide ? 2 : 1);
        Grid.SetColumn(AboutSection, wide ? 1 : 0);
        Grid.SetRow(AboutSection, wide ? 1 : 3);
        AboutSection.Margin = wide ? new Thickness(8, 24, 0, 0) : new Thickness(0, 24, 0, 0);
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
        if (s.SaturationEnabled && wallpaperRunning)
        {
            if (colorPreview is null || colorPreviewSaturation != s.Saturation || colorPreviewDarkenIcons != s.DarkenIcons)
            {
                colorPreview = PreviewColor.Apply(source, snapshot.Icons, s.Saturation, s.DarkenIcons);
                colorPreviewSaturation = s.Saturation;
                colorPreviewDarkenIcons = s.DarkenIcons;
            }
            source = colorPreview;
        }
        double sigma = s.BlurEnabled ? BlurParameters.PreviewSigma(s.BlurRadius, w, GetSystemMetrics(0)) : 0;
        pixels = PreviewEffects.Draw(source, snapshot.Icons, w, h, s, sigma, pixels);
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
        StartupCheck.Stage("MainWindow Loaded");
        if ((!StartupCheck.Isolated || StartupCheck.UpdateCheckRequested) && !startupUpdateChecked)
        {
            startupUpdateChecked = true;
            _ = CheckForUpdatesAsync(automatic: true);
        }
        if (StartupCheck.Isolated) return;
        wallpaperStatusTimer.Start();
        _ = RefreshWallpaperStatusAsync();
        if (busy || snapshot is not null) return;
        busy = true;
        SetCommandsEnabled(false);
        try { await CapturePreviewCoreAsync(); StartupCheck.Stage("Initial wallpaper capture completed"); }
        catch (Exception ex) { StartupCheck.Stage("Initial capture unavailable: " + ex.Message); StatusText.Text = "壁纸刷新失败：" + ex.Message; }
        finally { busy = false; SetCommandsEnabled(true); StartupCheck.Stage("Startup commands enabled"); }
    }
    internal void VerifyStartup()
    {
        if (!ready || !Root.IsLoaded || !AppWindow.IsVisible || Root.ActualWidth <= 0 || Root.ActualHeight <= 0)
            throw new InvalidOperationException("Settings window was not loaded and visible.");
        if (DisplayVersion != "1.8.1" || AboutVersionText.Text != DisplayVersion ||
            CheckUpdateLabel.Text != "检查更新" || !CheckUpdateLink.IsEnabled ||
            CheckUpdateLabel.TextDecorations != Windows.UI.Text.TextDecorations.Underline ||
            StartupToggle.Header?.ToString() != "登录时自启动" || BlurTitle.Text != "实验性功能" ||
            SaturationEnabledCheckBox.Header is not TextBlock saturationTitle || saturationTitle.Text != "窗口成为焦点时降低饱和度" ||
            BlurEnabledCheckBox.Parent is not StackPanel || SaturationEnabledCheckBox.Parent != SaturationSection ||
            !ApplyButton.IsEnabled || !CaptureButton.IsEnabled)
            throw new InvalidOperationException("Version, settings groups or command buttons failed initialization.");
        if (SaturationBox.Value != SaturationSlider.Value || BlurRadiusBox.Value != BlurRadiusSlider.Value)
            throw new InvalidOperationException("Settings number fields were not initialized from their sliders.");
        if (startupUpdateChecked || checkingUpdate || UpdateInfoBar.IsOpen ||
            AboutVersionText.Foreground is not SolidColorBrush versionBrush ||
            CheckUpdateLabel.Foreground is not SolidColorBrush updateBrush || !versionBrush.Color.Equals(updateBrush.Color))
            throw new InvalidOperationException("Isolated startup must not start update requests; version and update link must use the same gray.");
        object tooltip = ToolTipService.GetToolTip(CheckUpdateLink);
        string updateHelp = AutomationProperties.GetHelpText(CheckUpdateLink);
        try
        {
            PresentUpdateResult(new(UpdateState.Failed, "Offline fixture"), automatic: true);
            PresentUpdateResult(new(UpdateState.Current, "Current fixture"), automatic: true);
            if (UpdateInfoBar.IsOpen) throw new InvalidOperationException("Automatic current/error results must remain unobtrusive.");
            var release = new Uri("https://github.com/Coldtea233/Penumbra/releases/tag/v1.8.2");
            PresentUpdateResult(new(UpdateState.Available, "Available fixture", "1.8.2", release), automatic: true);
            if (!UpdateInfoBar.IsOpen || UpdateInfoBar.Title != "发现新版本 1.8.2" ||
                UpdateReleaseLink.NavigateUri != release || UpdateReleaseLink.Visibility != Visibility.Visible)
                throw new InvalidOperationException("Available updates must offer the correct release page.");
            PresentUpdateResult(new(UpdateState.Current, "Current fixture"), automatic: false);
            if (!UpdateInfoBar.IsOpen || UpdateInfoBar.Severity != InfoBarSeverity.Success ||
                UpdateReleaseLink.NavigateUri is not null || UpdateReleaseLink.Visibility != Visibility.Collapsed)
                throw new InvalidOperationException("Manual current result must not retain a stale update link.");
            PresentUpdateResult(new(UpdateState.Failed, "Offline fixture"), automatic: false);
            if (UpdateInfoBar.Severity != InfoBarSeverity.Warning || !CheckUpdateLink.IsEnabled)
                throw new InvalidOperationException("Failed checks must report an error and allow retry.");
        }
        finally
        {
            UpdateInfoBar.IsOpen = false;
            UpdateInfoBar.Visibility = Visibility.Collapsed;
            ToolTipService.SetToolTip(CheckUpdateLink, tooltip);
            AutomationProperties.SetHelpText(CheckUpdateLink, updateHelp);
        }
        bool originalWide = Root.ActualWidth >= 920;
        try
        {
            foreach (bool wide in new[] { false, true, false, true })
            {
                ApplyResponsiveLayout(wide);
                if (ExperimentalPanel.Parent != (wide ? (Panel)PreviewPanel : SettingsLayout) ||
                    Grid.GetRow(ParameterPanel) != (wide ? 0 : 1) ||
                    Grid.GetRow(AboutSection) != (wide ? 1 : 3) ||
                    Grid.GetColumn(AboutSection) != (wide ? 1 : 0) ||
                    (!wide && Grid.GetRow(ExperimentalPanel) != 2))
                    throw new InvalidOperationException("Responsive section order is incorrect.");
            }
        }
        finally { ApplyResponsiveLayout(originalWide); }
    }
    private async Task CapturePreviewCoreAsync()
    {
        _ = RefreshWallpaperStatusAsync();
        StatusText.Text = "正在刷新壁纸…";
        var captured = await DesktopSnapshot.CaptureAsync();
        snapshot = captured;
        colorPreview = null;
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
            if (wallpaperRunning != running)
            {
                wallpaperRunning = running;
                colorPreview = null;
                if (!running) CancelDesktopSaturationPreview();
                SchedulePreviewRender();
            }
            UpdateSaturationControls();
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
    private async void CheckUpdate_Click(object sender, RoutedEventArgs e)
        => await CheckForUpdatesAsync(automatic: false);

    private async Task CheckForUpdatesAsync(bool automatic)
    {
        if (windowClosed || checkingUpdate) return;
        checkingUpdate = true;
        CheckUpdateLink.IsEnabled = false;
        CheckUpdateLabel.Text = "检查中…";
        try
        {
            var result = await UpdateChecker.CheckAsync(DisplayVersion, updateCancellation.Token);
            if (windowClosed) return;
            PresentUpdateResult(result, automatic);
        }
        catch (OperationCanceledException) when (windowClosed) { }
        finally
        {
            checkingUpdate = false;
            if (!windowClosed)
            {
                CheckUpdateLabel.Text = "检查更新";
                CheckUpdateLink.IsEnabled = true;
            }
        }
    }
    private void PresentUpdateResult(UpdateResult result, bool automatic)
    {
        ToolTipService.SetToolTip(CheckUpdateLink, result.Message);
        AutomationProperties.SetHelpText(CheckUpdateLink, result.Message);
        if (automatic && result.State != UpdateState.Available) return;
        UpdateInfoBar.Title = result.State switch
        {
            UpdateState.Available => "发现新版本 " + result.Version,
            UpdateState.Current => "无需更新",
            _ => "检查更新失败"
        };
        UpdateInfoBar.Message = result.Message;
        UpdateInfoBar.Severity = result.State switch
        {
            UpdateState.Current => InfoBarSeverity.Success,
            UpdateState.Failed => InfoBarSeverity.Warning,
            _ => InfoBarSeverity.Informational
        };
        UpdateReleaseLink.NavigateUri = result.ReleasePage;
        UpdateReleaseLink.Visibility = result.ReleasePage is null ? Visibility.Collapsed : Visibility.Visible;
        UpdateInfoBar.Visibility = Visibility.Visible;
        UpdateInfoBar.IsOpen = true;
    }
    private async void Apply_Click(object sender, RoutedEventArgs e)
    {
        if (busy) return;
        CancelDesktopBlurRadiusPreview();
        CancelDesktopSaturationPreview();
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
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr FindWindow(string className, string? windowName);
    [DllImport("user32.dll")] private static extern IntPtr SendMessageTimeout(IntPtr hwnd, uint message, nuint wParam, IntPtr lParam, uint flags, uint timeout, out nuint result);
    [DllImport("user32.dll")] private static extern int GetSystemMetrics(int index);
    [DllImport("user32.dll")] private static extern bool IsIconic(IntPtr hwnd);
}

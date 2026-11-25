using Microsoft.UI;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
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
    private static readonly DesktopBlurRate[] BlurRates = [DesktopBlurRate.VSync, DesktopBlurRate.Hz120,
        DesktopBlurRate.Hz90, DesktopBlurRate.Hz60, DesktopBlurRate.Hz30, DesktopBlurRate.Hz15, DesktopBlurRate.Still];
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
    private bool animationTestOpen;
    private VignetteAnimationDurations animationDurations = VignetteAnimationDurations.Defaults;
    private bool startupUpdateChecked, checkingUpdate;
    private bool installingUpdate;
    private UpdateResult? availableUpdate;
    private readonly CancellationTokenSource updateCancellation = new();
    private WriteableBitmap? previewBitmap;
    private byte[]? previewPixels;
    private byte[]? colorPreview;
    private double colorPreviewSaturation = -1;
    private bool colorPreviewDarkenIcons;
    private static string RendererPath => Path.Combine(AppContext.BaseDirectory, "Nocturne.Renderer.exe");
    public string ApplicationName => AppDistribution.ApplicationName;
    public string DisplayVersion => "v" + UpdateChecker.DisplayVersion(typeof(App).Assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()!.InformationalVersion);
    public MainWindow()
    {
        StartupCheck.Stage("MainWindow InitializeComponent begin");
        InitializeComponent();
        if (StartupCheck.LayoutTestRequested) Root.LayoutUpdated += (_, _) => ReportLayoutTestState();
        UpdateInfoBar.Closed += (_, _) => UpdateInfoBar.Visibility = Visibility.Collapsed;
        Title = ApplicationName;
        AutomationProperties.SetHelpText(CheckUpdateLink, AppDistribution.UpdateHelp);
        ToolTipService.SetToolTip(CheckUpdateLink, AppDistribution.UpdateHelp);
        AutomationProperties.SetName(UpdateReleaseLink, AppDistribution.IsStore ? "在 Microsoft Store 查看更新" : "在 GitHub 查看新版本");
        AutomationProperties.SetName(TitleAppIcon, ApplicationName + " 图标");
        StartupToggle.IsEnabled = false;
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
    internal bool CloseForReplacement()
    {
        if (busy || closePending || windowClosed) return false;
        closeApproved = true;
        if (DispatcherQueue.TryEnqueue(() => Close())) return true;
        closeApproved = false;
        return false;
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
        AnimationSpeedSlider.Value = (int)s.VignetteSpeed;
        animationDurations = s.AnimationDurations;
        BlurEnabledCheckBox.IsOn = s.BlurEnabled;
        BlurRadiusSlider.Value = s.BlurRadius;
        int rateIndex = Array.IndexOf(BlurRates, s.BlurRenderRate);
        BlurRateCombo.SelectedIndex = rateIndex >= 0 ? rateIndex : Array.IndexOf(BlurRates, Settings.DefaultBlurRenderRate);
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
            SaturationSlider.Value,
            (VignetteAnimationSpeed)(int)AnimationSpeedSlider.Value)
        {
            AnimationDurations = animationDurations,
            BlurRenderRate = SelectedBlurRate
        };
    }
    private async void AnimationTest_Click(object sender, RoutedEventArgs e)
    {
        if (busy || animationTestOpen) return;
        animationTestOpen = true;
        try
        {
            var dialog = new VignetteTimingDialog(Root.XamlRoot, snapshot ?? StartupCheck.LayoutSnapshot(), ReadControls());
            if (await dialog.ShowAsync() == ContentDialogResult.Primary)
                animationDurations = dialog.Durations;
        }
        catch (Exception ex)
        {
            StartupCheck.Stage("Animation timing dialog failed: " + ex);
            StatusText.Text = "打开高级设置失败：" + ex.Message;
        }
        finally { animationTestOpen = false; }
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
    private DesktopBlurRate SelectedBlurRate
    {
        get
        {
            int index = BlurRateCombo.SelectedIndex;
            return index >= 0 && index < BlurRates.Length ? BlurRates[index] : Settings.DefaultBlurRenderRate;
        }
    }

    private void UpdateBlurRateWarning()
    {
        if (BlurRateWarning is null) return;
        BlurRateWarning.Visibility = BlurRateCombo.IsEnabled && (SelectedBlurRate is DesktopBlurRate.VSync or DesktopBlurRate.Hz120 or DesktopBlurRate.Hz90)
            ? Visibility.Visible : Visibility.Collapsed;
    }

    private void BlurRenderRate_Changed(object sender, SelectionChangedEventArgs e) => UpdateBlurRateWarning();

    private void UpdateBlurControls()
    {
        if (BlurRadiusSlider is null || BlurRadiusBox is null || updatingBlurControls) return;
        if (BlurRateCombo is not null)
        {
            BlurRateCombo.IsEnabled = BlurEnabledCheckBox.IsEnabled && BlurEnabledCheckBox.IsOn && BlurRadiusSlider.Value > 0;
            UpdateBlurRateWarning();
        }
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
        bool wide = SettingsLayoutMetrics.UseTwoColumns(e.NewSize.Width, wideLayout);
        if (wideLayout != wide) ApplyResponsiveLayout(wide);
        ResizePreview();
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
        PreviewColumn.Width = wide ? new GridLength(300) : new GridLength(1, GridUnitType.Star);
        ParameterColumn.MinWidth = wide ? SettingsLayoutMetrics.MinimumParameterWidth : 0;
        ParameterColumn.Width = wide ? new GridLength(1, GridUnitType.Star) : new GridLength(0);
        SettingsLayout.ColumnSpacing = wide ? SettingsLayoutMetrics.ColumnSpacing : 0;
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
        if (PreviewSlot is null || PreviewHost is null) return;
        double contentWidth = SettingsScrollViewer.ActualWidth - SettingsLayout.Margin.Left - SettingsLayout.Margin.Right;
        if (contentWidth <= 2) return;
        bool wide = wideLayout == true;
        var size = wide
            ? SettingsLayoutMetrics.TwoColumnPreviewSize(contentWidth,
                SettingsScrollViewer.ActualHeight, previewAspect)
            : SettingsLayoutMetrics.PreviewSize(contentWidth - 2,
                SettingsScrollViewer.ActualHeight, previewAspect, false);
        if (size.Width <= 0 || size.Height <= 0) return;
        double cardWidth = size.Width + 2;
        // The left column hugs the capped image. The right star column takes
        // all remaining space, without a centered page or an empty left slot.
        if (wide && (!PreviewColumn.Width.IsAbsolute || Math.Abs(PreviewColumn.Width.Value - cardWidth) > .5))
            PreviewColumn.Width = new GridLength(cardWidth);
        // Keep the card and image the same aspect: wide mode is height-capped,
        // while stacked mode fills the content width without side gaps.
        if (double.IsNaN(PreviewCard.Width) || Math.Abs(PreviewCard.Width - size.Width - 2) > .5)
            PreviewCard.Width = size.Width + 2;
        if (Math.Abs(PreviewHost.Height - size.Height) > .5) PreviewHost.Height = size.Height;
        bool compact = PreviewSlot.ActualWidth < SettingsLayoutMetrics.CompactActionsWidth;
        int statusRow = compact ? 1 : 0;
        if (Grid.GetRow(WallpaperEngineIndicator) != statusRow)
        {
            Grid.SetRow(WallpaperEngineIndicator, statusRow);
            Grid.SetColumn(WallpaperEngineIndicator, compact ? 0 : 1);
            Grid.SetColumnSpan(WallpaperEngineIndicator, compact ? 2 : 1);
            PreviewStatusRow.Height = compact ? GridLength.Auto : new GridLength(0);
            PreviewActions.RowSpacing = compact ? 10 : 0;
            WallpaperEngineIndicator.HorizontalAlignment = compact ? HorizontalAlignment.Stretch : HorizontalAlignment.Right;
            WallpaperEngineIndicator.Margin = new Thickness(0, 0, compact ? 0 : 24, 0);
        }
    }
    private void ReportLayoutTestState()
    {
        if (!Root.IsLoaded || PreviewHost.ActualWidth <= 0) return;
        using var stream = new MemoryStream();
        using (var writer = new System.Text.Json.Utf8JsonWriter(stream))
        {
            writer.WriteStartObject();
            writer.WriteNumber("Width", Root.ActualWidth);
            writer.WriteNumber("Height", Root.ActualHeight);
            writer.WriteBoolean("Wide", wideLayout == true);
            writer.WriteNumber("ContentWidth", SettingsLayout.ActualWidth);
            writer.WriteNumber("ViewportWidth", SettingsScrollViewer.ActualWidth);
            writer.WriteNumber("PreviewColumnWidth", PreviewColumn.ActualWidth);
            writer.WriteNumber("ParameterColumnWidth", ParameterColumn.ActualWidth);
            writer.WriteNumber("ColumnSpacing", SettingsLayout.ColumnSpacing);
            writer.WriteNumber("PreviewSlotWidth", PreviewSlot.ActualWidth);
            writer.WriteNumber("PreviewCardWidth", PreviewCard.ActualWidth);
            writer.WriteNumber("ViewportHeight", SettingsScrollViewer.ActualHeight);
            writer.WriteNumber("PreviewWidth", PreviewHost.ActualWidth);
            writer.WriteNumber("PreviewHeight", PreviewHost.ActualHeight);
            writer.WriteNumber("Aspect", previewAspect);
            writer.WriteBoolean("CompactActions", Grid.GetRow(WallpaperEngineIndicator) == 1);
            writer.WriteNumber("ParameterRow", Grid.GetRow(ParameterPanel));
            writer.WriteNumber("ExperimentalRow", Grid.GetRow(ExperimentalPanel));
            writer.WriteNumber("AboutRow", Grid.GetRow(AboutSection));
            writer.WriteBoolean("ExperimentalInPreview", ExperimentalPanel.Parent == PreviewPanel);
            writer.WriteEndObject();
        }
        string diagnostic = System.Text.Encoding.UTF8.GetString(stream.ToArray());
        if (AutomationProperties.GetHelpText(SettingsScrollViewer) != diagnostic)
            AutomationProperties.SetHelpText(SettingsScrollViewer, diagnostic);
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
        if (StartupCheck.LayoutTestRequested)
        {
            snapshot = StartupCheck.LayoutSnapshot();
            previewAspect = (double)snapshot.Width / snapshot.Height;
            PreviewEmpty.Visibility = Visibility.Collapsed;
            ResizePreview();
            DrawPreview(ReadControls());
        }
        if ((!StartupCheck.Isolated || StartupCheck.UpdateCheckRequested) && !startupUpdateChecked)
        {
            startupUpdateChecked = true;
            _ = CheckForUpdatesAsync(automatic: true);
        }
        if (StartupCheck.Isolated) return;
        await RefreshStartupAsync();
        if (windowClosed) return;
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
        if (DisplayVersion != "v" + typeof(App).Assembly.GetName().Version!.ToString(3) || AboutVersionText.Text != DisplayVersion ||
            Title != ApplicationName || AboutAppName.Text != "Penumbra" ||
            FocusOnlyCheckBox.Content?.ToString() != "仅在有窗口时淡入暗角" ||
            CheckUpdateLabel.Text != "检查更新" || !CheckUpdateLink.IsEnabled ||
            CheckUpdateLabel.TextDecorations != Windows.UI.Text.TextDecorations.None ||
            StartupToggle.Header?.ToString() != "登录时自启动" || BlurTitle.Text != "实验性功能" ||
            SaturationEnabledCheckBox.Header is not TextBlock saturationTitle || saturationTitle.Text != "窗口成为焦点时降低饱和度" ||
            BlurEnabledCheckBox.Parent is not StackPanel || SaturationEnabledCheckBox.Parent != SaturationSection ||
            !ApplyButton.IsEnabled || !CaptureButton.IsEnabled)
            throw new InvalidOperationException("Version, settings groups or command buttons failed initialization.");
        if (SaturationBox.Value != SaturationSlider.Value || BlurRadiusBox.Value != BlurRadiusSlider.Value)
            throw new InvalidOperationException("Settings number fields were not initialized from their sliders.");
        var settings = ReadControls();
        try
        {
            foreach (var speed in Enum.GetValues<VignetteAnimationSpeed>())
            {
                LoadControls(settings with { VignetteSpeed = speed });
                if (AnimationSpeedSlider.Value != (int)speed || ReadControls().VignetteSpeed != speed)
                    throw new InvalidOperationException("Vignette animation slider did not round-trip a preset.");
            }
            string[] expectedLabels = ["垂直同步", "120 Hz", "90 Hz", "60 Hz", "30 Hz", "15 Hz", "静止"];
            if (BlurRateCombo.Items.Count != expectedLabels.Length || AutomationProperties.GetName(BlurRateCombo) != "渲染帧率" ||
                BlurRateWarning.Text != "高FPS可能会影响性能" || BlurRateWarning.Parent != BlurRateCombo.Parent ||
                Grid.GetRow(BlurRateWarning) != Grid.GetRow(BlurRateCombo) || Grid.GetColumn(BlurRateWarning) >= Grid.GetColumn(BlurRateCombo))
                throw new InvalidOperationException("Blur render rate selector did not initialize.");
            for (int i = 0; i < expectedLabels.Length; i++)
            {
                var rate = BlurRates[i];
                LoadControls(settings with { BlurEnabled = true, BlurRadius = 12, BlurRenderRate = rate });
                if (BlurRateCombo.SelectedIndex != i || ReadControls().BlurRenderRate != rate ||
                    (BlurRateCombo.SelectedItem as ComboBoxItem)?.Content?.ToString() != expectedLabels[i] ||
                    BlurRateWarning.Visibility != (i is 0 or 1 or 2 ? Visibility.Visible : Visibility.Collapsed))
                    throw new InvalidOperationException("Blur render rate selector did not round-trip a preset.");
            }
        }
        finally { LoadControls(settings); }
        if (startupUpdateChecked || checkingUpdate || UpdateInfoBar.IsOpen ||
            AboutVersionText.Foreground is not SolidColorBrush versionBrush ||
            CheckUpdateLabel.Foreground is not SolidColorBrush updateBrush || !versionBrush.Color.Equals(updateBrush.Color))
            throw new InvalidOperationException("Isolated startup must not start update requests; version and update link must use the same gray.");
        object tooltip = ToolTipService.GetToolTip(CheckUpdateLink);
        string updateHelp = AutomationProperties.GetHelpText(CheckUpdateLink);
        void VerifyUpdateNoticeAppearance()
        {
            UpdateInfoBar.ApplyTemplate();
            if (!UpdateInfoBar.CornerRadius.Equals(new CornerRadius(12)) ||
                !UpdateInfoBar.CornerRadius.Equals(AboutCard.CornerRadius) ||
                !UpdateInfoBar.CornerRadius.Equals(PreviewCard.CornerRadius) ||
                VisualTreeHelper.GetChildrenCount(UpdateInfoBar) != 1 ||
                VisualTreeHelper.GetChild(UpdateInfoBar, 0) is not Border contentRoot ||
                !contentRoot.CornerRadius.Equals(UpdateInfoBar.CornerRadius))
                throw new InvalidOperationException("Update notice and cards must share the same rounded corners.");
            if (!new Windows.UI.ViewManagement.AccessibilitySettings().HighContrast &&
                (contentRoot.Background is not SolidColorBrush noticeBrush ||
                 !noticeBrush.Color.Equals(Windows.UI.Color.FromArgb(16, 255, 255, 255))))
                throw new InvalidOperationException("All update results must use the translucent white background, not severity fills.");
        }
        try
        {
            PresentUpdateResult(new(UpdateState.Failed, "Offline fixture"), automatic: true);
            PresentUpdateResult(new(UpdateState.Current, "Current fixture"), automatic: true);
            if (UpdateInfoBar.IsOpen) throw new InvalidOperationException("Automatic current/error results must remain unobtrusive.");
            var release = new Uri("https://github.com/Coldtea233/Penumbra/releases/tag/v1.9.1");
            PresentUpdateResult(new(UpdateState.Available, "Available fixture", "1.9.1", release), automatic: true);
            if (!UpdateInfoBar.IsOpen || UpdateInfoBar.Title != "发现新版本 1.9.1" ||
                UpdateActions.Visibility != Visibility.Visible || InstallUpdateButton.Visibility != Visibility.Collapsed ||
                UpdateReleaseLink.NavigateUri != release || UpdateReleaseLink.Visibility != Visibility.Visible)
                throw new InvalidOperationException("Available updates must offer the correct release page.");
            VerifyUpdateNoticeAppearance();
            var installer = new UpdateAsset(release, "fixture.exe", 1, new string('0', 64));
            PresentUpdateResult(new(UpdateState.Available, "Installer fixture", "1.9.1", release, installer), automatic: true);
            const double narrowNoticeWidth = 300;
            UpdateInfoBar.Width = narrowNoticeWidth;
            Root.UpdateLayout();
            var linkPosition = UpdateReleaseLink.TransformToVisual(UpdateInfoBar).TransformPoint(new Windows.Foundation.Point());
            var installPosition = InstallUpdateButton.TransformToVisual(UpdateInfoBar).TransformPoint(new Windows.Foundation.Point());
            double centerOffset = installPosition.Y + InstallUpdateButton.ActualHeight / 2 - linkPosition.Y - UpdateReleaseLink.ActualHeight / 2;
            double gap = installPosition.X - linkPosition.X - UpdateReleaseLink.ActualWidth;
            double right = installPosition.X + InstallUpdateButton.ActualWidth;
            if (UpdateInfoBar.Message.Length != 0 || InstallUpdateButton.Visibility != Visibility.Visible ||
                UpdateReleaseLink.ActualWidth <= 0 || InstallUpdateButton.ActualWidth <= 0 ||
                Math.Abs(centerOffset) > 1 || Math.Abs(gap - UpdateActions.Spacing) > 1 || right > narrowNoticeWidth)
                throw new InvalidOperationException($"Update actions did not fit on a compact row. Message length: {UpdateInfoBar.Message.Length}; " +
                    $"link width: {UpdateReleaseLink.ActualWidth}; install width: {InstallUpdateButton.ActualWidth}; " +
                    $"center offset: {centerOffset}; gap: {gap}; right: {right}.");
            UpdateInfoBar.ClearValue(FrameworkElement.WidthProperty);
            PresentUpdateResult(new(UpdateState.Current, "当前版本已为最新发行版", DisplayVersion[1..]), automatic: false);
            if (!UpdateInfoBar.IsOpen || UpdateInfoBar.Severity != InfoBarSeverity.Success ||
                UpdateInfoBar.Title != "当前版本已为最新发行版" || UpdateInfoBar.Message.Length != 0 ||
                UpdateActions.Visibility != Visibility.Collapsed || UpdateReleaseLink.NavigateUri is not null ||
                UpdateReleaseLink.Visibility != Visibility.Collapsed)
                throw new InvalidOperationException("Manual current result must not retain a stale update link.");
            VerifyUpdateNoticeAppearance();
            PresentUpdateResult(new(UpdateState.Failed, "Offline fixture"), automatic: false);
            if (UpdateInfoBar.Severity != InfoBarSeverity.Warning || UpdateInfoBar.Message != "Offline fixture" || !CheckUpdateLink.IsEnabled)
                throw new InvalidOperationException("Failed checks must report an error and allow retry.");
            VerifyUpdateNoticeAppearance();
        }
        finally
        {
            UpdateInfoBar.IsOpen = false;
            UpdateInfoBar.Visibility = Visibility.Collapsed;
            ToolTipService.SetToolTip(CheckUpdateLink, tooltip);
            AutomationProperties.SetHelpText(CheckUpdateLink, updateHelp);
        }
        bool originalWide = wideLayout ?? SettingsLayoutMetrics.UseTwoColumns(Root.ActualWidth, null);
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

    private void PresentStartupStatus(StartupStatus status)
    {
        StartupToggle.IsOn = status.Enabled;
        StartupToggle.IsEnabled = status.CanChange;
        ToolTipService.SetToolTip(StartupToggle, status.Message ?? "点击“应用到桌面”后保存。");
    }

    private async Task RefreshStartupAsync()
    {
        try
        {
            var status = await AppDistribution.ReadStartupAsync();
            if (!windowClosed) PresentStartupStatus(status);
        }
        catch (Exception ex)
        {
            if (windowClosed) return;
            StartupToggle.IsEnabled = false;
            ToolTipService.SetToolTip(StartupToggle, "无法读取自启动状态：" + ex.Message);
        }
    }

    private async Task CheckForUpdatesAsync(bool automatic)
    {
        if (windowClosed || checkingUpdate || installingUpdate) return;
        checkingUpdate = true;
        CheckUpdateLink.IsEnabled = false;
        CheckUpdateLabel.Text = "检查中…";
        try
        {
            var result = await AppDistribution.CheckForUpdatesAsync(
                WinRT.Interop.WindowNative.GetWindowHandle(this), DisplayVersion, updateCancellation.Token);
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
        bool canInstall = result.State == UpdateState.Available && (result.Asset is not null || result.StoreInstallAvailable);
        string message = canInstall ? "发现新版本" : result.Message;
        ToolTipService.SetToolTip(CheckUpdateLink, message);
        AutomationProperties.SetHelpText(CheckUpdateLink, message);
        if (automatic && result.State != UpdateState.Available) return;
        availableUpdate = result.State == UpdateState.Available ? result : null;
        InstallUpdateButton.Visibility = canInstall ? Visibility.Visible : Visibility.Collapsed;
        UpdateActions.Visibility = canInstall || result.ReleasePage is not null ? Visibility.Visible : Visibility.Collapsed;
        UpdateInfoBar.Title = result.State switch
        {
            UpdateState.Available => result.Version is null ? "发现新版本" : "发现新版本 " + result.Version,
            UpdateState.Current => result.Message,
            _ => "检查更新失败"
        };
        UpdateInfoBar.Message = canInstall || result.State == UpdateState.Current ? string.Empty : result.Message;
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

    private async void InstallUpdate_Click(object sender, RoutedEventArgs e)
    {
        if (installingUpdate || checkingUpdate || busy || closePending || availableUpdate is not { } update) return;
        installingUpdate = true;
        InstallUpdateButton.IsEnabled = CheckUpdateLink.IsEnabled = false;
        string? installer = null;
        bool launched = false;
        try
        {
            var dialog = new ContentDialog
            {
                XamlRoot = Root.XamlRoot,
                Title = "安装更新",
                Content = "安装前请保存参数。安装时设置窗口和桌面效果可能会暂时关闭。",
                PrimaryButtonText = "继续",
                CloseButtonText = "取消",
                DefaultButton = ContentDialogButton.Close
            };
            if (await dialog.ShowAsync() != ContentDialogResult.Primary || windowClosed) return;
            var token = updateCancellation.Token;
            if (update.StoreInstallAvailable)
            {
                UpdateInfoBar.Message = "正在请求 Microsoft Store 安装更新…";
                UpdateInfoBar.Message = await AppDistribution.InstallStoreUpdateAsync(WinRT.Interop.WindowNative.GetWindowHandle(this), token);
                return;
            }
            if (update.Asset is not { } asset) return;
            UpdateInfoBar.Message = "正在下载安装包…";
            installer = await UpdateDownload.DownloadAsync(asset, new Progress<int>(percent =>
            {
                if (!windowClosed && installingUpdate) UpdateInfoBar.Message = $"正在下载安装包… {percent}%";
            }), token);
            if (windowClosed) return;
            if (busy) throw new InvalidOperationException("请等待参数应用结束后重试。");
            try { using var stop = EventWaitHandle.OpenExisting(@"Local\Nocturne.Stop.v1"); stop.Set(); }
            catch (WaitHandleCannotBeOpenedException) { }
            for (int attempt = 0; attempt < 50 && FindWindow("Nocturne.Controller", null) != IntPtr.Zero; attempt++)
                await Task.Delay(100, token);
            if (FindWindow("Nocturne.Controller", null) != IntPtr.Zero)
                throw new InvalidOperationException("桌面效果仍在运行，请从托盘退出后重试。");
            token.ThrowIfCancellationRequested();
            using var process = Process.Start(new ProcessStartInfo(installer) { UseShellExecute = true });
            if (process is null) throw new InvalidOperationException("无法打开安装程序。");
            launched = true;
            closeApproved = true;
            Close();
        }
        catch (OperationCanceledException) when (windowClosed) { }
        catch (Exception ex)
        {
            if (!windowClosed) UpdateInfoBar.Message = "更新未完成。" + (ex is OperationCanceledException ? "下载超时，请重试。" : ex.Message);
        }
        finally
        {
            if (!launched && installer is not null) UpdateDownload.Cleanup(installer);
            installingUpdate = false;
            if (!windowClosed) InstallUpdateButton.IsEnabled = CheckUpdateLink.IsEnabled = true;
        }
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
            if (StartupToggle.IsEnabled)
                PresentStartupStatus(await AppDistribution.SetStartupAsync(StartupToggle.IsOn, RendererPath));
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

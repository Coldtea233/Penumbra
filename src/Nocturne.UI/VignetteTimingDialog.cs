using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Animation;
using Microsoft.UI.Xaml.Media.Imaging;
using System.Runtime.InteropServices.WindowsRuntime;
using Windows.Foundation;

namespace Nocturne;

internal sealed class VignetteTimingDialog
{
    private readonly ContentDialog dialog;
    private readonly NumberBox[] durationBoxes = new NumberBox[3];
    private readonly Image vignette;
    private Storyboard? playback;

    public VignetteTimingDialog(XamlRoot root, DesktopSnapshot snapshot, Settings settings)
    {
        dialog = new ContentDialog
        {
            XamlRoot = root,
            Title = "动画速率设置",
            PrimaryButtonText = "保存",
            CloseButtonText = "取消"
        };

        var content = new StackPanel { Spacing = 8 };
        var preview = new Grid { Height = 152 };
        preview.Children.Add(new Image { Source = CreateBitmap(snapshot, snapshot.Pixels), Stretch = Stretch.Uniform });
        // Render the two endpoints once. Compositing only their opacity is
        // equivalent to scaling the vignette, including protected icons.
        var previewSettings = settings.Opacity > 0 ? settings : settings with { Opacity = new Settings().Opacity };
        byte[] darkened = PreviewEffects.Draw(snapshot.Pixels, snapshot.Icons, snapshot.Width, snapshot.Height,
            previewSettings, 0, new byte[snapshot.Pixels.Length]);
        vignette = new Image { Source = CreateBitmap(snapshot, darkened), Stretch = Stretch.Uniform, Opacity = 0 };
        preview.Children.Add(vignette);
        content.Children.Add(new Border { CornerRadius = new CornerRadius(8), Child = preview });

        var hint = new TextBlock
        {
            Text = "时长单位为毫秒（0–5000）。0 表示立即切换。" +
                (settings.Opacity == 0 ? "当前透明度为 0，测试时使用默认暗角强度。" : ""),
            TextWrapping = TextWrapping.Wrap,
            FontSize = 12
        };
        content.Children.Add(hint);
        string[] names = ["快速", "均衡", "优雅"];
        for (int index = 0; index < names.Length; index++)
        {
            int presetIndex = index;
            var row = new Grid { ColumnSpacing = 8 };
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(88) });
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            row.Children.Add(new TextBlock { Text = names[index], VerticalAlignment = VerticalAlignment.Center });
            var box = new NumberBox { Minimum = 0, Maximum = VignetteAnimationDurations.Maximum };
            AutomationProperties.SetName(box, names[index] + "时长（毫秒）");
            Grid.SetColumn(box, 1);
            row.Children.Add(box);
            durationBoxes[index] = box;
            foreach (bool fadeIn in new[] { true, false })
            {
                var button = new Button { Content = fadeIn ? "淡入" : "淡出" };
                AutomationProperties.SetName(button, names[index] + button.Content);
                button.Click += (_, _) => Play(presetIndex, fadeIn);
                Grid.SetColumn(button, fadeIn ? 2 : 3);
                row.Children.Add(button);
            }
            content.Children.Add(row);
        }
        SetDurations(settings.AnimationDurations);

        var reset = new HyperlinkButton { Content = "恢复默认时长", Padding = new Thickness(0, 4, 0, 4) };
        reset.Click += (_, _) => SetDurations(VignetteAnimationDurations.Defaults);
        content.Children.Add(reset);
        content.Children.Add(new TextBlock
        {
            Text = "保存后，点主界面的「应用到桌面」使新时长生效。",
            TextWrapping = TextWrapping.Wrap,
            FontSize = 12
        });
        dialog.Content = new ScrollViewer { Content = content, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
        dialog.Closed += (_, _) => playback?.Stop();
    }

    public IAsyncOperation<ContentDialogResult> ShowAsync() => dialog.ShowAsync();

    public VignetteAnimationDurations Durations => new(ReadDuration(0), ReadDuration(1), ReadDuration(2));

    private int ReadDuration(int index)
    {
        double value = durationBoxes[index].Value;
        var defaults = VignetteAnimationDurations.Defaults;
        if (!double.IsFinite(value)) return index switch { 0 => defaults.Fast, 1 => defaults.Balanced, _ => defaults.Elegant };
        return (int)Math.Round(Math.Clamp(value, 0, VignetteAnimationDurations.Maximum));
    }

    private void SetDurations(VignetteAnimationDurations durations)
    {
        durationBoxes[0].Value = durations.Fast;
        durationBoxes[1].Value = durations.Balanced;
        durationBoxes[2].Value = durations.Elegant;
    }

    private void Play(int presetIndex, bool fadeIn)
    {
        playback?.Stop();
        playback = null;
        double from = fadeIn ? 0 : 1;
        double to = 1 - from;
        int milliseconds = ReadDuration(presetIndex);
        vignette.Opacity = to;
        if (milliseconds == 0) return;

        // Same ease-out curve used by the desktop renderer for all three presets.
        var duration = TimeSpan.FromMilliseconds(milliseconds);
        var animation = new DoubleAnimationUsingKeyFrames { Duration = new Duration(duration), FillBehavior = FillBehavior.Stop };
        animation.KeyFrames.Add(new DiscreteDoubleKeyFrame { KeyTime = KeyTime.FromTimeSpan(TimeSpan.Zero), Value = from });
        animation.KeyFrames.Add(new SplineDoubleKeyFrame
        {
            KeyTime = KeyTime.FromTimeSpan(duration), Value = to,
            KeySpline = new KeySpline { ControlPoint1 = new Point(.16, 1), ControlPoint2 = new Point(.30, 1) }
        });
        Storyboard.SetTarget(animation, vignette);
        Storyboard.SetTargetProperty(animation, nameof(UIElement.Opacity));
        playback = new Storyboard();
        playback.Children.Add(animation);
        playback.Begin();
    }

    private static WriteableBitmap CreateBitmap(DesktopSnapshot snapshot, byte[] pixels)
    {
        var bitmap = new WriteableBitmap(snapshot.Width, snapshot.Height);
        using var stream = bitmap.PixelBuffer.AsStream();
        stream.Write(pixels);
        bitmap.Invalidate();
        return bitmap;
    }
}

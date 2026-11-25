using Nocturne;

int failures = 0, checks = 0;
void Check(bool good, string name)
{
    checks++;
    if (!good) { failures++; Console.WriteLine("FAIL " + name); }
}
var defaults = new Settings();
var currentInstanceVersion = new Version(1, 8, 2, 0);
Check(!SettingsInstance.ShouldReplace(@"C:\A\Penumbra.exe", currentInstanceVersion,
    @"c:\a\penumbra.exe", currentInstanceVersion), "same build path reactivates the existing instance");
Check(SettingsInstance.ShouldReplace(@"C:\B\Penumbra.exe", currentInstanceVersion,
    @"C:\A\Penumbra.exe", currentInstanceVersion), "same-version hotfix in another directory replaces old settings");
Check(SettingsInstance.ShouldReplace(@"C:\A\Penumbra.exe", currentInstanceVersion,
    @"C:\A\Penumbra.exe", new Version(1, 8, 1, 0)), "newer version can replace in-place old settings");
Check(!SettingsInstance.ShouldReplace(@"C:\B\Penumbra.exe", currentInstanceVersion,
    @"C:\A\Penumbra.exe", new Version(1, 8, 3, 0)), "older launch cannot replace newer settings");
foreach (var instanceVersion in new[] { new Version(0, 0, 0, 0), new Version(1, 8, 2, 0), new Version(12, 34, 567, 89) })
    Check(SettingsInstance.UnpackVersion(SettingsInstance.PackVersion(instanceVersion)) == instanceVersion,
        "running version survives window-property encoding");
Check(SettingsInstance.UnpackVersion(0) is null, "legacy version property falls back to executable metadata");
Check(SettingsLayoutMetrics.UseTwoColumns(820, null) &&
    SettingsLayoutMetrics.UseTwoColumns(790, null), "near-square window keeps both columns");
Check(!SettingsLayoutMetrics.UseTwoColumns(750, true) &&
    !SettingsLayoutMetrics.UseTwoColumns(600, null), "columns stack only when genuinely narrow");
Check(SettingsLayoutMetrics.UseTwoColumns(770, true) &&
    !SettingsLayoutMetrics.UseTwoColumns(790, false) &&
    SettingsLayoutMetrics.UseTwoColumns(800, false), "resize breakpoint has a stable dead band");
Check(SettingsLayoutMetrics.UseTwoColumns(double.NaN, true) &&
    !SettingsLayoutMetrics.UseTwoColumns(0, null), "invalid sizing preserves an existing layout safely");
foreach (bool wide in new[] { true, false })
foreach (double width in new[] { 1, 300, 416, 640, 920, 1920, 3840 })
foreach (double viewport in new[] { 160, 320, 600, 920, 2160 })
foreach (double aspect in new[] { 16.0 / 9, 9.0 / 16, 1, 32.0 / 9 })
{
    var size = SettingsLayoutMetrics.PreviewSize(width, viewport, aspect, wide);
    Check(size.Width <= width + 1e-9 && size.Width > 0, "preview always fits available width");
    Check(wide
        ? size.Height <= 380 && size.Height <= Math.Max(96, viewport * .42) + 1e-9
        : size.Width == width && Math.Abs(size.Height - width / aspect) < 1e-9,
        "only wide mode caps height; stacked mode fills content width");
    Check(Math.Abs(size.Width / size.Height - aspect) < 1e-9, "sized preview preserves wallpaper aspect");
}
var hugePreview = SettingsLayoutMetrics.PreviewSize(3840, 2160, 16.0 / 9, true);
Check(hugePreview.Height == 380 && hugePreview.Width < 676, "maximized preview cannot fill the whole window");
var tallPreview = SettingsLayoutMetrics.PreviewSize(700, 1000, 16.0 / 9, false);
Check(tallPreview.Width == 700 && tallPreview.Height == 393.75,
    "stacked preview fills width beyond the former 260px height cap");
Check(SettingsLayoutMetrics.PreviewSize(700, 160, 16.0 / 9, false) == tallPreview,
    "stacked preview fills width independently of viewport height");
Check(SettingsLayoutMetrics.PreviewSize(0, 500, 1, true) == (0.0, 0.0) &&
    SettingsLayoutMetrics.PreviewSize(double.PositiveInfinity, 500, 1, true) == (0.0, 0.0),
    "unmeasured preview cannot produce invalid layout dimensions");
var fallbackPreview = SettingsLayoutMetrics.PreviewSize(600, double.NaN, double.NaN, true);
Check(double.IsFinite(fallbackPreview.Width) && double.IsFinite(fallbackPreview.Height), "invalid ratio and viewport use finite fallback");
foreach (double contentWidth in new[] { 696, 763, 923, 1240, 1376, 1920, 3840 })
foreach (double viewport in new[] { 211, 304, 600, 709, 2160 })
foreach (double aspect in new[] { 16.0 / 9, 9.0 / 16, 1, 32.0 / 9 })
{
    var fitted = SettingsLayoutMetrics.TwoColumnPreviewSize(contentWidth, viewport, aspect);
    double parameterWidth = contentWidth - fitted.Width - 2 - SettingsLayoutMetrics.ColumnSpacing;
    Check(parameterWidth >= SettingsLayoutMetrics.MinimumParameterWidth - 1e-9,
        "fitted preview always reserves readable parameter width");
    Check(Math.Abs(fitted.Width + 2 + SettingsLayoutMetrics.ColumnSpacing + parameterWidth - contentWidth) < 1e-9,
        "elastic parameters fill every pixel left by the preview");
    Check(fitted.Height <= 380 && fitted.Height <= Math.Max(96, viewport * .42) + 1e-9 &&
        Math.Abs(fitted.Width / fitted.Height - aspect) < 1e-9,
        "filling columns preserves capped preview and aspect");
    var repeated = SettingsLayoutMetrics.TwoColumnPreviewSize(contentWidth, viewport, aspect);
    Check(fitted == repeated, "column fitting is stable without previous column feedback");
}
var fittedNormal = SettingsLayoutMetrics.TwoColumnPreviewSize(923, 709, 16.0 / 9);
var fittedMaximized = SettingsLayoutMetrics.TwoColumnPreviewSize(1376, 709, 16.0 / 9);
Check(fittedNormal == fittedMaximized,
    "additional window width goes to parameters once the preview reaches its height cap");
Check(SettingsLayoutMetrics.TwoColumnPreviewSize(100, 600, 16.0 / 9) == (0.0, 0.0) &&
    SettingsLayoutMetrics.TwoColumnPreviewSize(double.NaN, 600, 16.0 / 9) == (0.0, 0.0),
    "incomplete column measurements are ignored safely");
Check(!defaults.SaturationEnabled && defaults.Saturation == 40, "saturation remains opt-in for old settings");
var colorSettings = defaults with { SaturationEnabled = true, Saturation = 25 };
Check(colorSettings.SaturationEnabled && colorSettings.Saturation == 25 && !colorSettings.BlurEnabled, "saturation independent from blur");
byte[] colorSource = [32, 64, 224, 255, 224, 80, 40, 127];
byte[] colorOriginal = (byte[])colorSource.Clone();
byte[] noIcons = [0, 0];
Check(PreviewColor.Apply(colorSource, noIcons, 100).SequenceEqual(colorSource), "100 percent retains original bytes");
var graySource = PreviewColor.Apply(colorSource, noIcons, 0);
Check(graySource[0] == graySource[1] && graySource[1] == graySource[2] && graySource[4] == graySource[5] && graySource[5] == graySource[6], "zero percent grayscale");
Check(graySource[3] == 255 && graySource[7] == 127, "color preview preserves alpha");
Check(PreviewColor.Apply(colorSource, [0, 1], 0).Skip(4).SequenceEqual(colorSource.Skip(4)), "icons keep original colors");
Check(PreviewColor.Apply(colorSource, [0, 1], 0, true).SequenceEqual(graySource), "darken-icons option includes icons in desaturation");
Check(PreviewColor.Apply(colorSource, [0, 1], 100, true).SequenceEqual(colorSource), "100 percent keeps icons unchanged even when included");
Check(!PreviewColor.Apply(colorSource, [0, 1], 40, true).Skip(4).SequenceEqual(colorSource.Skip(4)), "included icons use the adjustable saturation, not fixed grayscale");
Check(PreviewColor.Apply(colorSource, [0, 1], 0, false).Skip(4).SequenceEqual(colorSource.Skip(4)), "switching icon inclusion back off restores original icon colors");
Check(PreviewColor.Apply(colorSource, noIcons, double.NaN).SequenceEqual(colorSource), "nonfinite saturation safely retains original");
Check(PreviewColor.Apply(colorSource, noIcons, -5).SequenceEqual(graySource) && PreviewColor.Apply(colorSource, noIcons, 105).SequenceEqual(colorSource), "saturation range clamped");
int priorChroma = -1;
for (int saturation = 0; saturation <= 100; saturation++) {
    var next = PreviewColor.Apply(colorSource, noIcons, saturation);
    int chroma = next.Take(3).Max() - next.Take(3).Min();
    Check(chroma > priorChroma, $"preview saturation {saturation} produces increasing chroma");
    priorChroma = chroma;
}
Check(colorOriginal.SequenceEqual(colorSource), "saturation never mutates the source");
bool invalidColor = false;
try { PreviewColor.Apply(colorSource, [], 25); } catch (ArgumentException) { invalidColor = true; }
Check(invalidColor, "invalid icon mask rejected");
Check(!defaults.BlurEnabled && defaults.BlurRadius == 12 && defaults.BlurRenderRate == DesktopBlurRate.Hz30 &&
    Settings.Parse([]).BlurRenderRate == DesktopBlurRate.Hz30, "fresh settings default to 30 Hz");
foreach (var rate in Enum.GetValues<DesktopBlurRate>())
{
    var changed = defaults with { BlurEnabled = true, BlurRadius = 32, BlurRenderRate = rate };
    Check(Settings.Parse(changed.Serialize().Split('\n')) == changed, "blur rate round-trips independently of other settings");
}
Check(Settings.Parse(["blurMode=0"]).BlurRenderRate == DesktopBlurRate.VSync, "legacy live Gaussian maps to vertical sync");
Check(Settings.Parse(["blurMode=1"]).BlurRenderRate == DesktopBlurRate.Hz15, "legacy 8 FPS maps to the lowest fixed rate");
Check(Settings.Parse(["blurMode=2"]).BlurRenderRate == DesktopBlurRate.Still, "legacy freeze mode maps to still");
Check(Settings.Parse(["blurFreezeAfterFade=1"]).BlurRenderRate == DesktopBlurRate.Still, "original freeze flag maps to still");
Check(Settings.Parse(["blurMode=2", "blurRenderRate=90"]).BlurRenderRate == DesktopBlurRate.Hz90, "explicit rate takes precedence over the legacy mode");
Check(Settings.Parse(["blurRenderRate=121"]).BlurRenderRate == DesktopBlurRate.Hz30, "unsupported rates fall back to the default");
byte[] uniform = new byte[23 * 9 * 4];
for (int i = 0; i < uniform.Length; i += 4)
{ uniform[i] = 32; uniform[i + 1] = 64; uniform[i + 2] = 128; uniform[i + 3] = 255; }
var original = (byte[])uniform.Clone();
Check(PreviewBlur.Apply(uniform, 23, 9, 12).SequenceEqual(uniform), "uniform colors and clamped borders are preserved");
Check(original.SequenceEqual(uniform), "source snapshot is never mutated");
byte[] tiny = [8, 16, 32, 255];
Check(PreviewBlur.Apply(tiny, 1, 1, 30).SequenceEqual(tiny), "one-pixel preview with large blur");
byte[] stripes = new byte[160 * 4];
for (int x = 0; x < 160; x++)
{ byte color = (byte)(x % 80 < 40 ? 32 : 224); for (int c = 0; c < 3; c++) stripes[x * 4 + c] = color; stripes[x * 4 + 3] = 255; }
double Contrast(byte[] pixels)
{
    var values = Enumerable.Range(20, 120).Select(x => (double)pixels[x * 4]).ToArray();
    double mean = values.Average(); return values.Average(x => (x - mean) * (x - mean));
}
var slight = PreviewBlur.Apply(stripes, 160, 1, 4);
var strong = PreviewBlur.Apply(stripes, 160, 1, 16);
Check(Contrast(strong) < Contrast(slight) && Contrast(slight) < Contrast(stripes), "radius visibly increases preview blur");
Check(PreviewBlur.Apply(stripes, 160, 1, 0).SequenceEqual(stripes), "zero sigma is identity");
bool invalid = false;
try { PreviewBlur.Apply([], 2, 2, 12); } catch (ArgumentException) { invalid = true; }
Check(invalid, "invalid dimensions rejected");
byte[] previewStripes = new byte[640 * 4];
for (int x = 0; x < 640; x++)
{
    byte color = (byte)((x / 16) % 2 == 0 ? 16 : 240);
    for (int c = 0; c < 3; c++) previewStripes[x * 4 + c] = color;
    previewStripes[x * 4 + 3] = 255;
}
// Regression: the old UI pipeline used identical 13/13/15 box widths for
// every radius from 0 to 8 on a 2880px monitor with a 640px preview.
var fractional0 = PreviewBlur.Apply(previewStripes, 640, 1, 30 * 640.0 / 2880);
var fractional8 = PreviewBlur.Apply(previewStripes, 640, 1, Math.Sqrt(900 + 64) * 640.0 / 2880);
Check(!fractional0.SequenceEqual(fractional8), "fractional kernels resolve the former 0-8px preview dead zone");
var previousPreview = PreviewBlur.Apply(previewStripes, 640, 1, BlurParameters.PreviewSigma(0, 640, 2880));
Check(previousPreview.SequenceEqual(previewStripes), "production radius zero matches the unblurred preview byte-for-byte");
var faintPreview = PreviewBlur.Apply(previewStripes, 640, 1, BlurParameters.PreviewSigma(1, 640, 2880));
Check(Contrast(faintPreview) > Contrast(previewStripes) * .98, "production radius one starts with only a slight blur");
for (int radius = 1; radius <= 48; radius++)
{
    var nextPreview = PreviewBlur.Apply(previewStripes, 640, 1, BlurParameters.PreviewSigma(radius, 640, 2880));
    Check(!previousPreview.SequenceEqual(nextPreview), $"production preview changes from {radius - 1} to {radius}px");
    previousPreview = nextPreview;
}
Check(Math.Abs(BlurParameters.PreviewSigma(12, 640, 2880) - 12 * 640.0 / 2880) < 1e-12,
    "preview scales the actual Gaussian radius without a hidden floor");
Check(BlurParameters.PreviewSigma(-1, 640, 2880) == 0 &&
    BlurParameters.PreviewSigma(49, 640, 2880) == BlurParameters.PreviewSigma(48, 640, 2880) &&
    BlurParameters.PreviewSigma(double.NaN, 640, 2880) == BlurParameters.PreviewSigma(12, 640, 2880),
    "preview and desktop use the same clamping and fallback");
// A sharp icon-shaped punch-out AFTER the Gaussian caused broken edges.
byte[] desktopImage = new byte[64 * 48 * 4], desktopIcons = new byte[64 * 48];
for (int i = 3; i < desktopImage.Length; i += 4) desktopImage[i] = 255;
for (int y = 6; y < 18; y++) for (int x = 6; x < 18; x++)
{
    int i = y * 64 + x; desktopIcons[i] = 1;
    for (int c = 0; c < 3; c++) desktopImage[i * 4 + c] = 240;
}
var protectedSettings = defaults with { Opacity = 1, Radius = .05, Feather = .05, DarkenIcons = false, BlurEnabled = true, BlurRadius = 4 };
var protectedSharp = PreviewEffects.Draw(desktopImage, desktopIcons, 64, 48, protectedSettings, 0, new byte[desktopImage.Length]);
Check(protectedSharp[(10 * 64 + 10) * 4] == 240, "icon protection preserves original icon brightness before blur");
var protectedSoft = PreviewEffects.Draw(desktopImage, desktopIcons, 64, 48, protectedSettings, 4, new byte[desktopImage.Length]);
Check(protectedSoft.SequenceEqual(PreviewBlur.Apply(protectedSharp, 64, 48, 4)), "whole desktop and icon protection use the SAME Gaussian, in correct order");
Check(protectedSoft[(10 * 64 + 10) * 4] < 240 && protectedSoft[(10 * 64 + 4) * 4] > 0, "icons really blur and spread beyond original icon mask");
var darkenedSoft = PreviewEffects.Draw(desktopImage, desktopIcons, 64, 48, protectedSettings with { DarkenIcons = true }, 4, new byte[desktopImage.Length]);
Check(darkenedSoft[(10 * 64 + 10) * 4] < protectedSoft[(10 * 64 + 10) * 4], "darken option changes brightness, never blur coverage");
var noVignette = PreviewEffects.Draw(desktopImage, desktopIcons, 64, 48, protectedSettings with { Opacity = 0 }, 4, new byte[desktopImage.Length]);
Check(noVignette.SequenceEqual(PreviewBlur.Apply(desktopImage, 64, 48, 4)), "zero opacity still blurs entire desktop including icons");
Check(PreviewEffects.Draw(desktopImage, desktopIcons, 64, 48, protectedSettings, 0, new byte[desktopImage.Length]).SequenceEqual(protectedSharp), "zero radius is exactly the protected unblurred desktop");
for (int radius = 1; radius <= 48; radius++)
{
    var actual = PreviewEffects.Draw(desktopImage, desktopIcons, 64, 48, protectedSettings, radius, new byte[desktopImage.Length]);
    Check(actual.SequenceEqual(PreviewBlur.Apply(protectedSharp, 64, 48, radius)), $"protected icons and background share radius {radius}");
}
byte[] fullPreview = new byte[640 * 360 * 4];
new Random(42).NextBytes(fullPreview);
for (int i = 3; i < fullPreview.Length; i += 4) fullPreview[i] = 255;
PreviewBlur.Apply(fullPreview, 640, 360, BlurParameters.PreviewSigma(24, 640, 2880));
var previewClock = System.Diagnostics.Stopwatch.StartNew();
for (int i = 0; i < 5; i++) PreviewBlur.Apply(fullPreview, 640, 360, BlurParameters.PreviewSigma(i * 12, 640, 2880));
Console.WriteLine($"{{\"PreviewOnly640x360MeanMs\":{previewClock.Elapsed.TotalMilliseconds / 5:F3},\"DesktopDoesNotCallPreviewFilter\":true}}");
Console.WriteLine($"{{\"ManagedChecks\":{checks},\"Failures\":{failures},\"UserSettingsWritten\":false}}");
return failures == 0 ? 0 : 1;

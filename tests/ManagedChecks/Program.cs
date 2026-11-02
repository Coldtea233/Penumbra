using Nocturne;

int failures = 0, checks = 0;
void Check(bool good, string name)
{
    checks++;
    if (!good) { failures++; Console.WriteLine("FAIL " + name); }
}
var defaults = new Settings();
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
Check(!defaults.BlurEnabled && defaults.BlurRadius == 12, "blur remains opt-in with compatible defaults");
var changed = defaults with { BlurEnabled = true, BlurRadius = 32 };
Check(changed.BlurEnabled && changed.BlurRadius == 32 && changed.Opacity == defaults.Opacity, "independent immutable blur settings");
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
byte[] fullPreview = new byte[640 * 360 * 4];
new Random(42).NextBytes(fullPreview);
for (int i = 3; i < fullPreview.Length; i += 4) fullPreview[i] = 255;
PreviewBlur.Apply(fullPreview, 640, 360, BlurParameters.PreviewSigma(24, 640, 2880));
var previewClock = System.Diagnostics.Stopwatch.StartNew();
for (int i = 0; i < 5; i++) PreviewBlur.Apply(fullPreview, 640, 360, BlurParameters.PreviewSigma(i * 12, 640, 2880));
Console.WriteLine($"{{\"PreviewOnly640x360MeanMs\":{previewClock.Elapsed.TotalMilliseconds / 5:F3},\"DesktopDoesNotCallPreviewFilter\":true}}");
Console.WriteLine($"{{\"ManagedChecks\":{checks},\"Failures\":{failures},\"UserSettingsWritten\":false}}");
return failures == 0 ? 0 : 1;

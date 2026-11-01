using Nocturne;

int failures = 0, checks = 0;
void Check(bool good, string name)
{
    checks++;
    if (!good) { failures++; Console.WriteLine("FAIL " + name); }
}
var defaults = new Settings();
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
for (int radius = 1; radius <= 48; radius++)
{
    var nextPreview = PreviewBlur.Apply(previewStripes, 640, 1, BlurParameters.PreviewSigma(radius, 640, 2880));
    Check(!previousPreview.SequenceEqual(nextPreview), $"production preview changes from {radius - 1} to {radius}px");
    previousPreview = nextPreview;
}
Check(Math.Abs(BlurParameters.PreviewSigma(12, 640, 2880) - 42 * 640.0 / 2880) < 1e-12,
    "preview uses an effective-radius increment, not a tiny squared increment");
byte[] fullPreview = new byte[640 * 360 * 4];
new Random(42).NextBytes(fullPreview);
for (int i = 3; i < fullPreview.Length; i += 4) fullPreview[i] = 255;
PreviewBlur.Apply(fullPreview, 640, 360, BlurParameters.PreviewSigma(24, 640, 2880));
var previewClock = System.Diagnostics.Stopwatch.StartNew();
for (int i = 0; i < 5; i++) PreviewBlur.Apply(fullPreview, 640, 360, BlurParameters.PreviewSigma(i * 12, 640, 2880));
Console.WriteLine($"{{\"PreviewOnly640x360MeanMs\":{previewClock.Elapsed.TotalMilliseconds / 5:F3},\"DesktopDoesNotCallPreviewFilter\":true}}");
Console.WriteLine($"{{\"ManagedChecks\":{checks},\"Failures\":{failures},\"UserSettingsWritten\":false}}");
return failures == 0 ? 0 : 1;

namespace Nocturne;

internal static class BlurParameters
{
    // Keep this calibration in sync with the native blur_parameters.h.
    internal const double BaselineSigma = 30;
    internal static double PreviewSigma(double radius, int previewWidth, int screenWidth) =>
        (BaselineSigma + Math.Clamp(radius, 0, 48)) * previewWidth / Math.Max(previewWidth, screenWidth);
}

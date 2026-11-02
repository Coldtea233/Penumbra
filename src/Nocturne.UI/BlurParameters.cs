namespace Nocturne;

internal static class BlurParameters
{
    // The desktop source is unfiltered. Match native blur_parameters.h with
    // no hidden baseline: 0 must leave the original preview unchanged.
    internal static double PreviewSigma(double radius, int previewWidth, int screenWidth) =>
        Math.Clamp(double.IsFinite(radius) ? radius : 12, 0, 48)
            * previewWidth / Math.Max(previewWidth, screenWidth);
}

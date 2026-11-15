namespace Nocturne;

// Effective pixels, independent of monitor DPI. Only used when sizing settings;
// it does not change desktop sampling, bitmap resolution or renderer parameters.
internal static class SettingsLayoutMetrics
{
    internal const double MinimumParameterWidth = 340;
    internal const double ColumnSpacing = 24;
    internal const double CompactActionsWidth = 460;

    internal static bool UseTwoColumns(double windowWidth, bool? previous)
    {
        if (!double.IsFinite(windowWidth) || windowWidth <= 0) return previous ?? false;
        // Both columns still fit near a square window. A small dead band avoids
        // rearranging controls repeatedly while dragging around the breakpoint.
        return windowWidth >= (previous switch { true => 760, false => 800, null => 780 });
    }

    internal static (double Width, double Height) PreviewSize(double availableWidth,
        double viewportHeight, double aspect, bool twoColumns)
    {
        if (!double.IsFinite(availableWidth) || availableWidth <= 0) return (0, 0);
        if (!double.IsFinite(aspect) || aspect <= 0) aspect = 16.0 / 9.0;
        // Stacked mode fills the content width; only side-by-side mode caps height.
        if (!twoColumns) return (availableWidth, availableWidth / aspect);
        if (!double.IsFinite(viewportHeight) || viewportHeight <= 0) viewportHeight = 600;
        double maximumHeight = Math.Clamp(viewportHeight * .42, 96, 380);
        double height = Math.Min(availableWidth / aspect, maximumHeight);
        return (height * aspect, height);
    }

    internal static (double Width, double Height) TwoColumnPreviewSize(
        double contentWidth, double viewportHeight, double aspect)
    {
        // Size from the available viewport, not either column or their measured
        // parent: fixed column widths can keep old desired sizes while shrinking.
        double availableWidth = contentWidth - ColumnSpacing - MinimumParameterWidth - 2;
        return PreviewSize(availableWidth, viewportHeight, aspect, true);
    }
}

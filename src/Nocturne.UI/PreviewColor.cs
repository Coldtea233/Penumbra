namespace Nocturne;

// Static preview only. Desktop color is a retained native Composition effect
// on the selected desktop layer; this filter never runs in the native background.
internal static class PreviewColor
{
    internal static byte[] Apply(byte[] source, byte[] icons, double percentage, bool includeIcons = false)
    {
        if (source.Length % 4 != 0 || icons.Length != source.Length / 4)
            throw new ArgumentException("预览颜色蒙版尺寸不正确。");
        double saturation = Math.Clamp(double.IsFinite(percentage) ? percentage : 100, 0, 100) / 100;
        var output = (byte[])source.Clone();
        if (saturation == 1) return output;
        for (int pixel = 0; pixel < icons.Length; pixel++)
        {
            if (icons[pixel] != 0 && !includeIcons) continue;
            int i = pixel * 4;
            double gray = source[i + 2] * .213 + source[i + 1] * .715 + source[i] * .072;
            for (int c = 0; c < 3; c++)
                output[i + c] = (byte)Math.Clamp((int)Math.Round(gray + saturation * (source[i + c] - gray)), 0, 255);
        }
        return output;
    }
}

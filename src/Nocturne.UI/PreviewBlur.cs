namespace Nocturne;

// Three fractional box passes approximate a Gaussian, only for the small static
// UI preview. Fractional edge weights avoid whole-pixel kernel quantization:
// adjacent slider values must not collapse to the same blur kernel.
internal static class PreviewBlur
{
    internal static byte[] Apply(byte[] pixels, int width, int height, double sigma)
    {
        if (width <= 0 || height <= 0 || pixels.Length != checked(width * height * 4))
            throw new ArgumentException("预览尺寸不正确。");
        if (!double.IsFinite(sigma) || sigma < 0 || sigma > 256)
            throw new ArgumentOutOfRangeException(nameof(sigma));
        var a = (byte[])pixels.Clone();
        if (sigma == 0) return a;
        var b = new byte[a.Length];
        double variance = sigma * sigma / 3;
        int radius = Math.Max(0, (int)Math.Floor((Math.Sqrt(1 + 12 * variance) - 1) / 2));
        double squaredSum = radius * (radius + 1.0) * (2 * radius + 1) / 3;
        double edgeWeight = Math.Clamp((variance * (2 * radius + 1) - squaredSum)
            / (2 * ((radius + 1.0) * (radius + 1) - variance)), 0, 1);
        for (int pass = 0; pass < 3; pass++)
        {
            Box(a, b, width, height, radius, edgeWeight, false);
            Box(b, a, width, height, radius, edgeWeight, true);
        }
        return a;
    }
    private static void Box(byte[] source, byte[] target, int width, int height, int radius, double edgeWeight, bool vertical)
    {
        int lines = vertical ? width : height, length = vertical ? height : width;
        int step = vertical ? width * 4 : 4;
        double divisor = radius * 2 + 1 + edgeWeight * 2;
        for (int line = 0; line < lines; line++)
        {
            int start = vertical ? line * 4 : line * width * 4;
            for (int channel = 0; channel < 3; channel++)
            {
                int sum = 0;
                for (int k = -radius; k <= radius; k++)
                    sum += source[start + Math.Clamp(k, 0, length - 1) * step + channel];
                for (int p = 0; p < length; p++)
                {
                    double weighted = sum + edgeWeight *
                        (source[start + Math.Clamp(p - radius - 1, 0, length - 1) * step + channel]
                        + source[start + Math.Clamp(p + radius + 1, 0, length - 1) * step + channel]);
                    target[start + p * step + channel] = (byte)Math.Clamp((int)Math.Round(weighted / divisor), 0, 255);
                    sum += source[start + Math.Clamp(p + radius + 1, 0, length - 1) * step + channel]
                        - source[start + Math.Clamp(p - radius, 0, length - 1) * step + channel];
                }
            }
            for (int p = 0; p < length; p++) target[start + p * step + 3] = 255;
        }
    }
}

namespace Nocturne;

// Same ordering as the native desktop: protected vignette, then whole-desktop
// Gaussian. Never punch sharp icon holes over an already blurred image.
internal static class PreviewEffects
{
    internal static byte[] Draw(byte[] source, byte[] icons, int width, int height,
        Settings settings, double sigma, byte[] destination)
    {
        if (width < 1 || height < 1 || source.Length != checked(width * height * 4) ||
            destination.Length != source.Length || icons.Length != width * height)
            throw new ArgumentException("预览尺寸不正确。");
        (int red, int green, int blue) = settings.Tint switch
        {
            1 => (14, 22, 40), 2 => (27, 18, 16), _ => (0, 0, 0)
        };
        // 横向项与行无关：按列算一次，拖动滑杆时每像素只剩一次开方。
        var columns = new double[width];
        for (int x = 0; x < width; x++)
        {
            double dx = ((x + .5) / width - settings.CenterX) * 2 / settings.Shape;
            columns[x] = dx * dx;
        }
        for (int y = 0; y < height; y++)
        {
            double dy = ((y + .5) / height - settings.CenterY) * 2;
            double vertical = dy * dy;
            for (int x = 0; x < width; x++)
            {
                double t = Math.Clamp((Math.Sqrt(columns[x] + vertical) - settings.Radius) / settings.Feather, 0, 1);
                double a = icons[y * width + x] != 0 && !settings.DarkenIcons ? 0 : settings.Opacity * t * t * (3 - 2 * t);
                int i = (y * width + x) * 4;
                destination[i] = (byte)(source[i] * (1 - a) + blue * a);
                destination[i + 1] = (byte)(source[i + 1] * (1 - a) + green * a);
                destination[i + 2] = (byte)(source[i + 2] * (1 - a) + red * a);
                destination[i + 3] = 255;
            }
        }
        return sigma > 0 ? PreviewBlur.Apply(destination, width, height, sigma) : destination;
    }
}

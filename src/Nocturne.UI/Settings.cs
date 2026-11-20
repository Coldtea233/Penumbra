using System.Globalization;

namespace Nocturne;
internal enum VignetteAnimationSpeed { Fast = 0, Balanced = 1, Elegant = 2 }

internal readonly record struct VignetteAnimationDurations(int Fast, int Balanced, int Elegant)
{
    public const int Maximum = 5000;
    public static VignetteAnimationDurations Defaults => new(520, 800, 1200);
}

internal sealed record Settings(double Opacity = 0.68, double Radius = 0.48, double Feather = 0.65,
    double Shape = 1, double CenterX = 0.5, double CenterY = 0.5, int Tint = 0,
    bool PrimaryOnly = false, bool DarkenIcons = false, bool FocusOnly = false,
    bool BlurEnabled = false, double BlurRadius = 12,
    bool SaturationEnabled = false, double Saturation = 40,
    VignetteAnimationSpeed VignetteSpeed = VignetteAnimationSpeed.Balanced)
{
    public VignetteAnimationDurations AnimationDurations { get; init; } = VignetteAnimationDurations.Defaults;

    public static string DirectoryPath => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Nocturne");
    public static string FilePath => Path.Combine(DirectoryPath, "settings.ini");
    public static Settings Load()
    {
        var d = new Dictionary<string, string>();
        if (File.Exists(FilePath))
            foreach (var line in File.ReadAllLines(FilePath))
            {
                var p = line.Split('=', 2);
                if (p.Length == 2) d[p[0].Trim()] = p[1].Trim();
            }
        double Number(string k, double fallback, double min, double max) =>
            d.TryGetValue(k, out var s) && double.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture, out var n) && double.IsFinite(n)
                ? Math.Clamp(n, min, max) : fallback;
        var defaults = VignetteAnimationDurations.Defaults;
        int Duration(string key, int fallback) => (int)Number(key, fallback, 0, VignetteAnimationDurations.Maximum);
        return new(Number("opacity", .68, 0, 1), Number("radius", .48, .05, 1.5), Number("feather", .65, .05, 1.5),
            Number("shape", 1, .3, 2.5), Number("centerX", .5, 0, 1), Number("centerY", .5, 0, 1),
            (int)Number("tint", 0, 0, 2), Number("primaryOnly", 0, 0, 1) == 1,
            Number("darkenIcons", 0, 0, 1) == 1, Number("focusOnly", 0, 0, 1) == 1,
            Number("blurEnabled", 0, 0, 1) == 1, Number("blurRadius", 12, 0, 48),
            Number("saturationEnabled", 0, 0, 1) == 1, Number("saturation", 40, 0, 100),
            (VignetteAnimationSpeed)(int)Number("vignetteSpeed", (int)VignetteAnimationSpeed.Balanced, 0, 2))
        {
            AnimationDurations = new(Duration("vignetteFastMs", defaults.Fast),
                Duration("vignetteBalancedMs", defaults.Balanced), Duration("vignetteElegantMs", defaults.Elegant))
        };
    }
    public void Save()
    {
        Directory.CreateDirectory(DirectoryPath);
        string content = FormattableString.Invariant($"[vignette]\nopacity={Opacity:F6}\nradius={Radius:F6}\nfeather={Feather:F6}\nshape={Shape:F6}\ncenterX={CenterX:F6}\ncenterY={CenterY:F6}\ntint={Tint}\nprimaryOnly={(PrimaryOnly ? 1 : 0)}\ndarkenIcons={(DarkenIcons ? 1 : 0)}\nfocusOnly={(FocusOnly ? 1 : 0)}\nblurEnabled={(BlurEnabled ? 1 : 0)}\nblurRadius={BlurRadius:F6}\nsaturationEnabled={(SaturationEnabled ? 1 : 0)}\nsaturation={Saturation:F6}\nvignetteSpeed={(int)VignetteSpeed}\n");
        content += FormattableString.Invariant($"vignetteFastMs={AnimationDurations.Fast}\nvignetteBalancedMs={AnimationDurations.Balanced}\nvignetteElegantMs={AnimationDurations.Elegant}\n");
        File.WriteAllText(FilePath + ".tmp", content, new System.Text.UTF8Encoding(false));
        File.Move(FilePath + ".tmp", FilePath, true);
    }
}

# Penumbra branding

Assets supplied by the project author on 2026-11-01. Keep the original artwork;
do not replace it with a generated approximation.

- `AppIcon.png`: original `da-vinci-glow-fluent-v5.png`, used in the title bar and About.
- `AppIcon.ico`: corresponding multi-resolution `da-vinci-glow-fluent-v5.ico`, used by the launchers, window, native executable, and installer.
- `TrayWhite.ico`: original `da-vinci-tray-white.ico`, for a dark Windows taskbar.
- `TrayBlack.ico`: original `da-vinci-tray-black.ico`, for a light Windows taskbar.

Native resources embed both tray icons, so no loose tray assets or image runtime
are needed by the background renderer. `SystemUsesLightTheme` is read at startup
and on theme notifications, independently of the app's own appearance. Same-theme
notifications reuse the icon; changed icons are released after replacement.

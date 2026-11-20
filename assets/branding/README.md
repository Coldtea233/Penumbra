# Penumbra branding

Assets supplied by the project author. Keep the original artwork;
do not replace it with a generated approximation.

- `AppIcon.png`: author's `da-vinci-glow-v22-satin-gray-blue.png` (2026-11-21), used in the title bar, About, GitHub README, and Store assets.
- `AppIcon.ico`: author's matching `da-vinci-glow-v22-satin-gray-blue.ico`, with 16/20/24/32/40/48/64/128/256px frames for the launchers, window, native executable, and installer.
- `TrayWhite.ico`: original `tray-v12-tight/da-vinci-tray-v12-white.ico`, supplied on 2026-11-02, for a dark Windows taskbar.
- `TrayBlack.ico`: original `tray-v12-tight/da-vinci-tray-v12-black.ico`, supplied on 2026-11-02, for a light Windows taskbar.

Native resources embed both tray icons, so no loose tray assets or image runtime
are needed by the background renderer. `SystemUsesLightTheme` is read at startup
and on theme notifications, independently of the app's own appearance. Same-theme
notifications reuse the icon; changed icons are released after replacement.

# Penumbra branding

Assets supplied by the project author on 2026-11-01 through 2026-11-03. Keep the original artwork;
do not replace it with a generated approximation.

- `AppIcon.png`: author's latest `codex-clipboard-51211743-f4cd-478c-8801-8edd078e1977.png` (2026-11-03), resized proportionally to 256px, used in the title bar and About.
- `AppIcon.ico`: corresponding 16/20/24/32/40/48/64/128/256px icon, used by the launchers, window, native executable, and installer. Regenerate with `Update-AppIcon.ps1 -SourcePath <original.png>`.
- `TrayWhite.ico`: original `tray-v12-tight/da-vinci-tray-v12-white.ico`, supplied on 2026-11-02, for a dark Windows taskbar.
- `TrayBlack.ico`: original `tray-v12-tight/da-vinci-tray-v12-black.ico`, supplied on 2026-11-02, for a light Windows taskbar.

Native resources embed both tray icons, so no loose tray assets or image runtime
are needed by the background renderer. `SystemUsesLightTheme` is read at startup
and on theme notifications, independently of the app's own appearance. Same-theme
notifications reuse the icon; changed icons are released after replacement.

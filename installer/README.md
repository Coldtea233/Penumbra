# EXE installer

Public download: `Penumbra-v1.7.5-Setup-x64.exe`.

Build the application, then run `build-installer.ps1` and `build-exe-installer.ps1`. The EXE embeds the complete MSI payload and verifies its SHA-256 before installation; no runtime download is required.

Users can choose the installation directory. The fixed UpgradeCode supports replacing older versions while retaining user settings. Exit Penumbra before updating. Installation does not change the login startup preference.

The completion page defaults to launching Penumbra. Desktop and Start menu shortcut options and creation code have been removed. Windows Installer handles removal of shortcuts owned by previous packages during upgrade; user-created shortcuts are not scanned or deleted.

Arguments: `/quiet`, `/log <file>`, `/extract <new-file>`. Extraction refuses to overwrite an existing file. Quiet installation does not launch the app. The installer is unsigned; SHA-256 verification is not publisher authentication.

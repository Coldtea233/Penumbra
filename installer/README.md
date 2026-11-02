# EXE installer

Public downloads use `Penumbra-Beta-0.6-r1-Setup-x64.exe`, a statically linked x64
bootstrapper with the complete compressed Windows Installer payload embedded as
a resource. This is not a renamed MSI and does not download anything at runtime.

Build the application, run `build-installer.ps1` to make the internal MSI, then
run `build-exe-installer.ps1` as shown in the repository README. Publish only the
EXE, portable ZIP and checksum list; retain MSI/intermediate files locally.

The small EXE presents a confirmation dialog, rejects a running settings/backend
mutex without killing processes, verifies the embedded payload's SHA-256, and
calls the system Windows Installer API. It requests no elevation and preserves
the previous installer UpgradeCode, transactional rollback, start-menu shortcut,
and Windows Apps uninstall registration. User configuration is not in the
payload, and installation never enables startup or launches the application.

Arguments:

- `/quiet`: no installer UI; returns the underlying Windows Installer exit code.
- `/log <file>`: writes a Windows Installer log to a user-chosen location.
- `/extract <new-file>`: verifies and extracts the embedded MSI without installing;
  refuses to overwrite an existing file. Used for offline packaging checks.

The per-instance temporary directory has a random GUID. Cleanup removes only the
explicit extracted file and empty directory, never a recursive path. Windows
Installer maintains its own uninstall cache. The SHA-256 self-check detects
accidental payload corruption; it is not publisher authentication. The installer
is unsigned and may show a SmartScreen warning.

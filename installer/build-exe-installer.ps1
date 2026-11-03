param(
    [string]$Toolchain=$env:LLVM_MINGW_ROOT,
    [Parameter(Mandatory)][string]$PayloadPath,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [Parameter(Mandatory)][string]$IconPath
)
$ErrorActionPreference='Stop'
if(!$Toolchain){throw 'Pass -Toolchain with the llvm-mingw x64 directory.'}
$Toolchain=(Resolve-Path -LiteralPath $Toolchain).Path
$payload=(Resolve-Path -LiteralPath $PayloadPath).Path
$icon=(Resolve-Path -LiteralPath $IconPath).Path
$output=[IO.Path]::GetFullPath($OutputDirectory)
$installer=New-Object -ComObject WindowsInstaller.Installer
$database=$installer.OpenDatabase($payload,0)
try {
    $view=$database.OpenView('SELECT `Value` FROM `Property` WHERE `Property` = ''ProductVersion''')
    $view.Execute();$row=$view.Fetch();$productVersion=$row.StringData(1);$view.Close()
    $view=$database.OpenView('SELECT `Value` FROM `Property` WHERE `Property` = ''ProductCode''')
    $view.Execute();$row=$view.Fetch();$productCode=$row.StringData(1);$view.Close()
    $view=$database.OpenView('SELECT `Value` FROM `Property` WHERE `Property` = ''PENUMBRA_PATH_LIMIT''')
    $view.Execute();$row=$view.Fetch();$installPathLimit=[int]$row.StringData(1);$view.Close()
    $view=$database.OpenView('SELECT `Value` FROM `Property` WHERE `Property` = ''PENUMBRA_DISPLAY_VERSION''')
    $view.Execute();$row=$view.Fetch();$packageDisplayVersion=if($row){$row.StringData(1)}else{''};$view.Close()
} finally {[void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($database);[void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($installer)}
if($productVersion -notmatch '^\d+\.\d+\.\d+$'){throw "Unsupported version: $productVersion"}
if($productCode -notmatch '^\{[0-9A-Fa-f-]{36}\}$' -or $installPathLimit -lt 40 -or $installPathLimit -gt 220){throw 'Invalid installer identity/path limit.'}
$versionParts=$productVersion.Split('.')
$displayVersion='Beta '+$versionParts[0]+'.'+$versionParts[1]
if([int]$versionParts[2] -gt 0){$displayVersion+=' r'+$versionParts[2]}
if($packageDisplayVersion){$displayVersion=$packageDisplayVersion}
$stem='Penumbra-'+$displayVersion.Replace(' ','-')+'-Setup-x64'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$exePath=Join-Path $output ($stem+'.exe')
if(Test-Path -LiteralPath $exePath){throw "Refusing to overwrite existing installer: $exePath"}
$build=Join-Path $output ('build-exe-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $build | Out-Null
$hash=(Get-FileHash -LiteralPath $payload -Algorithm SHA256).Hash
# Generated compiler inputs; hand-maintained source lives alongside this script.
$config='inline constexpr const wchar_t* DisplayVersion=L"'+$displayVersion+'";'+"`r`n"+'inline constexpr const wchar_t* PayloadSha256=L"'+$hash+'";'+"`r`n"+'inline constexpr const wchar_t* ProductVersion=L"'+$productVersion+'";'+"`r`n"+'inline constexpr const wchar_t* ProductCode=L"'+$productCode+'";'+"`r`n"+'inline constexpr unsigned InstallPathLimit='+$installPathLimit+';'
[IO.File]::WriteAllText((Join-Path $build 'SetupConfig.h'),$config,[Text.UTF8Encoding]::new($false))
function ResourcePath([string]$path){$path.Replace('\','/')}
$resource=@"
#pragma code_page(65001)
#include <winver.h>
#include "$(ResourcePath (Join-Path $PSScriptRoot 'SetupDialog.rc'))"
1 24 "$(ResourcePath (Join-Path $PSScriptRoot 'Setup.manifest'))"
101 ICON "$(ResourcePath $icon)"
201 RCDATA "$(ResourcePath $payload)"
1 VERSIONINFO
FILEVERSION $($versionParts -join ','),0
PRODUCTVERSION $($versionParts -join ','),0
FILEOS VOS_NT_WINDOWS32
FILETYPE VFT_APP
BEGIN
 BLOCK "StringFileInfo"
 BEGIN
  BLOCK "040904B0"
  BEGIN
   VALUE "FileDescription", "Penumbra Setup"
   VALUE "FileVersion", "$productVersion.0"
   VALUE "ProductName", "Penumbra"
   VALUE "ProductVersion", "$displayVersion"
  END
 END
 BLOCK "VarFileInfo"
 BEGIN
  VALUE "Translation", 0x0409, 1200
 END
END
"@
$rc=Join-Path $build 'Setup.rc';$res=Join-Path $build 'Setup.res'
[IO.File]::WriteAllText($rc,$resource,[Text.UTF8Encoding]::new($false))
& (Join-Path $Toolchain 'bin/x86_64-w64-mingw32-windres.exe') --codepage=65001 $rc -O coff -o $res
if($LASTEXITCODE){throw 'Setup resource compilation failed.'}
& (Join-Path $Toolchain 'bin/x86_64-w64-mingw32-clang++.exe') (Join-Path $PSScriptRoot 'Setup.cpp') $res ('-I'+$build) -std=c++17 -Os -Wall -Wextra -static -s -municode -mwindows -luser32 -lcomctl32 -lmsi -lbcrypt -lole32 -lshell32 -luuid -ladvapi32 -o $exePath
if($LASTEXITCODE){throw 'EXE installer compilation failed.'}
[pscustomobject]@{Installer=$exePath;InstallerBytes=(Get-Item -LiteralPath $exePath).Length;EmbeddedPayload=$payload;PayloadSha256=$hash;DisplayVersion=$displayVersion;ProductVersion=$productVersion;PerUser=$true;RequiresAdmin=$false} | ConvertTo-Json -Compress

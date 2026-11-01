param([Parameter(Mandatory)][string]$InstallerPath,[Parameter(Mandatory)][string]$PayloadPath)
$ErrorActionPreference='Stop'
$exe=(Resolve-Path -LiteralPath $InstallerPath).Path
$msi=(Resolve-Path -LiteralPath $PayloadPath).Path
$temp=Join-Path ([IO.Path]::GetTempPath()) ('Penumbra-exe-check-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temp | Out-Null
$extracted=Join-Path $temp 'payload.msi'
$config=Join-Path $env:LOCALAPPDATA 'Nocturne/settings.ini'
$hash=if(Test-Path -LiteralPath $config){(Get-FileHash -LiteralPath $config).Hash}else{''}
$startup=(Get-ItemProperty 'HKCU:/Software/Microsoft/Windows/CurrentVersion/Run' -Name Nocturne -ErrorAction SilentlyContinue).Nocturne
$checks=0
Add-Type @'
using System;using System.Runtime.InteropServices;using System.Text;
public static class SetupTestNative {
 public delegate bool EnumProc(IntPtr h,IntPtr p);
 [DllImport("user32.dll")]static extern bool EnumWindows(EnumProc p,IntPtr l);
 [DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetClassName(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")]public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 public static IntPtr Dialog(uint pid){IntPtr found=IntPtr.Zero;EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);var c=new StringBuilder(64);GetClassName(h,c,64);if(id==pid&&c.ToString()=="#32770"){found=h;return false;}return true;},IntPtr.Zero);return found;}
}
'@
$dialogHandle=[IntPtr]::Zero
function Exit-Code([string[]]$arguments){
    $p=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$p.WaitForExit(20000)){throw 'Installer diagnostic did not finish.'}
    return $p.ExitCode
}
function Assert($condition,[string]$message){if(!$condition){throw $message};$script:checks++}
try {
    Assert ((Exit-Code @('/quiet','/extract',('"'+$extracted+'"'))) -eq 0) 'EXE extraction failed.'
    Assert ((Get-FileHash -LiteralPath $extracted).Hash -eq (Get-FileHash -LiteralPath $msi).Hash) 'Embedded payload differs from the verified MSI.'
    Assert ((Exit-Code @('/quiet','/extract',('"'+$extracted+'"'))) -in @(80,183)) 'Extraction must refuse to overwrite an existing file.'
    Assert ((Get-FileHash -LiteralPath $extracted).Hash -eq (Get-FileHash -LiteralPath $msi).Hash) 'Duplicate extraction changed an existing file.'
    Assert ((Exit-Code @('/quiet','/unsupported')) -eq 87) 'Unknown switch did not fail safely.'
    # Guarantee the busy path without closing a real UI/backend or installing.
    $mutex=[Threading.Mutex]::new($false,'Local\VignFinder.Settings.v3')
    try {Assert ((Exit-Code @('/quiet')) -eq 1618) 'An active settings mutex must block install before extraction.'}
    finally {$mutex.Dispose()}
    $p=Start-Process -FilePath $exe -PassThru -WindowStyle Hidden
    for($attempt=0;$attempt -lt 60;$attempt++){
        $dialogHandle=[SetupTestNative]::Dialog($p.Id)
        if($dialogHandle -ne [IntPtr]::Zero){break}
        Start-Sleep -Milliseconds 100
    }
    Assert ($dialogHandle -ne [IntPtr]::Zero) 'Install confirmation did not open.'
    Start-Sleep -Milliseconds 200
    [SetupTestNative]::PostMessage($dialogHandle,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
    Assert ($p.WaitForExit(5000) -and $p.ExitCode -eq 1602) 'Cancel must exit without installation.'
    $after=if(Test-Path -LiteralPath $config){(Get-FileHash -LiteralPath $config).Hash}else{''}
    Assert ($after -eq $hash) 'Installer checks changed user settings.'
    Assert ((Get-ItemProperty 'HKCU:/Software/Microsoft/Windows/CurrentVersion/Run' -Name Nocturne -ErrorAction SilentlyContinue).Nocturne -eq $startup) 'Installer checks changed startup registration.'
    [pscustomobject]@{Checks=$checks;Passed=$true;EmbeddedPayloadSha256=(Get-FileHash $extracted).Hash;SettingsUnchanged=$true;StartupUnchanged=$true;PerformedInstall=$false;PerformedUninstall=$false}|ConvertTo-Json -Compress
} finally {
    if($dialogHandle -ne [IntPtr]::Zero){[SetupTestNative]::PostMessage($dialogHandle,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null}
    if(Test-Path -LiteralPath $extracted){Remove-Item -LiteralPath $extracted}
    if(Test-Path -LiteralPath $temp){Remove-Item -LiteralPath $temp}
}

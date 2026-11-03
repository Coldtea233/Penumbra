param([Parameter(Mandatory)][string]$InstallerPath,[Parameter(Mandatory)][string]$PayloadPath,[string]$ScreenshotDirectory,[string]$ExpectedDisplayVersion='Beta 0.7')
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
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;using System.Runtime.InteropServices;using System.Text;
public static class SetupTestNative {
 public delegate bool EnumProc(IntPtr h,IntPtr p);
 [DllImport("user32.dll")]static extern bool EnumWindows(EnumProc p,IntPtr l);
 [DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetClassName(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern bool SetWindowText(IntPtr h,string s);
 [DllImport("user32.dll")]public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")]public static extern bool IsWindowEnabled(IntPtr h);
 [DllImport("user32.dll")]public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr h,int command);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [StructLayout(LayoutKind.Sequential)]public struct Rect{public int Left,Top,Right,Bottom;}
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")]public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
 public static IntPtr Dialog(uint pid){IntPtr found=IntPtr.Zero;EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);var c=new StringBuilder(64);GetClassName(h,c,64);if(id==pid&&c.ToString()=="#32770"){found=h;return false;}return true;},IntPtr.Zero);return found;}
 public static IntPtr OtherDialog(uint pid,IntPtr parent){IntPtr found=IntPtr.Zero;EnumWindows((h,p)=>{uint id;GetWindowThreadProcessId(h,out id);var c=new StringBuilder(64);GetClassName(h,c,64);if(h!=parent&&id==pid&&c.ToString()=="#32770"){found=h;return false;}return true;},IntPtr.Zero);return found;}
 public static string Text(IntPtr h){var s=new StringBuilder(2048);GetWindowText(h,s,s.Capacity);return s.ToString();}
}
'@
$dialogHandle=[IntPtr]::Zero
function Exit-Code([string[]]$arguments){
    $p=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$p.WaitForExit(15000)){throw 'Installer diagnostic did not finish.'}
    return $p.ExitCode
}
function Assert($condition,[string]$message){if(!$condition){throw $message};$script:checks++}
try {
    Assert ((Exit-Code @('/quiet','/extract',('"'+$extracted+'"'))) -eq 0) 'EXE extraction failed.'
    Assert ((Get-FileHash -LiteralPath $extracted).Hash -eq (Get-FileHash -LiteralPath $msi).Hash) 'Embedded payload differs from MSI.'
    Assert ((Exit-Code @('/quiet','/extract',('"'+$extracted+'"'))) -in @(80,183)) 'Extraction must not overwrite existing files.'
    Assert ((Get-FileHash -LiteralPath $extracted).Hash -eq (Get-FileHash -LiteralPath $msi).Hash) 'Duplicate extraction changed the existing MSI.'
    Assert ((Exit-Code @('/quiet','/unsupported')) -eq 87) 'Unknown switch did not fail safely.'
    foreach($invalid in @('relative','C:\','C:\Apps\..\Penumbra','C:\CON')){
        # Root argument deliberately has no quotes: trailing slash escaping is not involved.
        Assert ((Exit-Code @('/quiet','/dir',$invalid)) -eq 87) "Invalid directory accepted: $invalid"
    }
    foreach($identity in @('Penumbra.Settings','Penumbra.Settings.Beta0.6','VignFinder.Settings.v3','Nocturne.Renderer.v1')){
        $mutex=[Threading.Mutex]::new($false,('Local\'+$identity))
        try {Assert ((Exit-Code @('/quiet')) -eq 1618) "Active $identity must block installation."}
        finally {$mutex.Dispose()}
    }
    $p=Start-Process -FilePath $exe -PassThru -WindowStyle Hidden
    for($attempt=0;$attempt -lt 60;$attempt++){
        $dialogHandle=[SetupTestNative]::Dialog($p.Id)
        if($dialogHandle -ne [IntPtr]::Zero){break}
        Start-Sleep -Milliseconds 100
    }
    Assert ($dialogHandle -ne [IntPtr]::Zero) 'Directory selection dialog did not open.'
    # STARTUPINFO requests hidden launch. The first ShowWindow can consume
    # that inherited request; explicitly show again until the UI is ready.
    for($attempt=0;$attempt -lt 20 -and ![SetupTestNative]::IsWindowVisible($dialogHandle);$attempt++){
        [SetupTestNative]::ShowWindow($dialogHandle,1)|Out-Null
        Start-Sleep -Milliseconds 100
    }
    Assert ([SetupTestNative]::IsWindowVisible($dialogHandle)) 'Setup dialog must be visible.'
    Assert ([SetupTestNative]::Text([SetupTestNative]::GetDlgItem($dialogHandle,1001)) -eq ('Penumbra '+$ExpectedDisplayVersion)) 'Wrong installer version.'
    $pathLabel=[SetupTestNative]::Text([SetupTestNative]::GetDlgItem($dialogHandle,1003))
    Assert ($pathLabel -eq '安装位置') "Chinese resource strings are incorrectly encoded: $pathLabel"
    $edit=[SetupTestNative]::GetDlgItem($dialogHandle,1004)
    $browse=[SetupTestNative]::GetDlgItem($dialogHandle,1005)
    Assert ($edit -ne [IntPtr]::Zero -and $browse -ne [IntPtr]::Zero) 'Path editor or Browse button missing.'
    if([SetupTestNative]::IsWindowEnabled($edit)){
        $chosen='C:\测试目录\Penumbra Folder'
        [SetupTestNative]::SetWindowText($edit,$chosen)|Out-Null
        Assert ([SetupTestNative]::Text($edit) -eq $chosen) 'Unicode/space directory editing failed.'
        Assert ([SetupTestNative]::IsWindowEnabled($browse)) 'Browse should be enabled for new installations/upgrades.'
        [SetupTestNative]::PostMessage($dialogHandle,0x111,[IntPtr]1005,[IntPtr]::Zero)|Out-Null
        $folderDialog=[IntPtr]::Zero
        for($attempt=0;$attempt -lt 60;$attempt++){
            $folderDialog=[SetupTestNative]::OtherDialog($p.Id,$dialogHandle)
            if($folderDialog -ne [IntPtr]::Zero){break};Start-Sleep -Milliseconds 100
        }
        Assert ($folderDialog -ne [IntPtr]::Zero) 'Native folder picker did not open.'
        [SetupTestNative]::PostMessage($folderDialog,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
        for($attempt=0;$attempt -lt 40 -and ![SetupTestNative]::IsWindowEnabled($dialogHandle);$attempt++){Start-Sleep -Milliseconds 100}
        Assert ([SetupTestNative]::IsWindowEnabled($dialogHandle)) 'Cancelling folder selection must return to setup.'
        Assert ([SetupTestNative]::Text($edit) -eq $chosen) 'Cancelling folder selection changed the typed directory.'
    }
    if($ScreenshotDirectory){
        New-Item -ItemType Directory -Force -Path $ScreenshotDirectory|Out-Null
        $r=[SetupTestNative+Rect]::new();[SetupTestNative]::GetWindowRect($dialogHandle,[ref]$r)|Out-Null
        $bitmap=[Drawing.Bitmap]::new($r.Right-$r.Left,$r.Bottom-$r.Top)
        $graphics=[Drawing.Graphics]::FromImage($bitmap);$dc=$graphics.GetHdc()
        try{Assert ([SetupTestNative]::PrintWindow($dialogHandle,$dc,2)) 'Setup screenshot failed.'}
        finally{$graphics.ReleaseHdc($dc);$graphics.Dispose()}
        try{$bitmap.Save((Join-Path $ScreenshotDirectory 'setup-directory.png'),[Drawing.Imaging.ImageFormat]::Png)}finally{$bitmap.Dispose()}
    }
    [SetupTestNative]::PostMessage($dialogHandle,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
    Assert ($p.WaitForExit(5000) -and $p.ExitCode -eq 1602) 'Cancel must exit without installation.'
    $dialogHandle=[IntPtr]::Zero
    $after=if(Test-Path -LiteralPath $config){(Get-FileHash -LiteralPath $config).Hash}else{''}
    Assert ($after -eq $hash) 'Installer checks changed user settings.'
    Assert ((Get-ItemProperty 'HKCU:/Software/Microsoft/Windows/CurrentVersion/Run' -Name Nocturne -ErrorAction SilentlyContinue).Nocturne -eq $startup) 'Installer checks changed startup registration.'
    [pscustomobject]@{Checks=$checks;Passed=$true;EmbeddedPayloadSha256=(Get-FileHash $extracted).Hash;DirectoryDialog=$true;SettingsUnchanged=$true;StartupUnchanged=$true;PerformedInstall=$false;PerformedUninstall=$false}|ConvertTo-Json -Compress
} finally {
    if($dialogHandle -ne [IntPtr]::Zero){[SetupTestNative]::PostMessage($dialogHandle,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null}
    if(Test-Path -LiteralPath $extracted){Remove-Item -LiteralPath $extracted}
    if(Test-Path -LiteralPath $temp){Remove-Item -LiteralPath $temp}
}

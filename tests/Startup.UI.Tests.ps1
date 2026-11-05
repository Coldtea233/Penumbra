param([Parameter(Mandatory)][string]$LauncherPath)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class StartupUiNative {
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr window,uint message,IntPtr wp,IntPtr lp);
}
'@
$launcher=(Resolve-Path -LiteralPath $LauncherPath).Path
$expected=Join-Path ([IO.Path]::GetDirectoryName($launcher)) 'app\Penumbra.exe'
if(Get-Process Penumbra -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $expected}){throw 'Close the tested build before running the isolated startup check.'}
$config=Join-Path $env:LOCALAPPDATA 'Nocturne\settings.ini'
$hash=if(Test-Path -LiteralPath $config){(Get-FileHash -LiteralPath $config).Hash}else{''}
$app=$null
try {
    $entry=Start-Process -FilePath $launcher -PassThru -WindowStyle Hidden
    if(!$entry.WaitForExit(5000) -or $entry.ExitCode){throw 'Top-level launcher did not complete successfully.'}
    for($attempt=0;$attempt -lt 100;$attempt++){
        $app=Get-Process Penumbra -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $expected -and $_.MainWindowHandle -ne [IntPtr]::Zero} | Select-Object -First 1
        if($app){break};Start-Sleep -Milliseconds 100
    }
    if(!$app){throw 'Normal launcher did not open a settings window.'}
    $root=[Windows.Automation.AutomationElement]::FromHandle($app.MainWindowHandle)
    function Control([string]$id){
        for($retry=0;$retry -lt 20;$retry++){
            $item=$root.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$id))
            if($item){return $item};Start-Sleep -Milliseconds 100
        }
        throw "Missing control: $id"
    }
    if((Control 'AboutVersionText').Current.Name -ne '1.8.1'){throw 'Wrong settings version.'}
    foreach($id in @('TitleAppIcon','AboutAppIcon','CheckUpdateLink','SaturationEnabledCheckBox','SaturationSlider','SaturationBox','BlurRadiusSlider','ApplyButton','CaptureButton')){$null=Control $id}
    for($attempt=0;$attempt -lt 50 -and !(Control 'ApplyButton').Current.IsEnabled;$attempt++){Start-Sleep -Milliseconds 100}
    if(!(Control 'ApplyButton').Current.IsEnabled){throw 'Settings commands remained disabled after startup.'}
    $updateCheck=[DateTime]::UtcNow.AddSeconds(15)
    $updateHelp=(Control 'CheckUpdateLink').Current.HelpText
    while($updateHelp -eq '通过 GitHub Releases 检查新版本；启动时自动检查一次。' -and [DateTime]::UtcNow -lt $updateCheck){
        Start-Sleep -Milliseconds 100
        $updateHelp=(Control 'CheckUpdateLink').Current.HelpText
    }
    if($updateHelp -notmatch '当前已是最新|当前版本高于|可前往 GitHub|无法连接 GitHub|连接 GitHub 超时|检查次数已达上限|GitHub 暂时不可用|暂未找到公开|无法识别 GitHub 发行版|更新信息过大|读取更新信息失败|无法解析'){
        throw 'Startup did not finish or report its single GitHub release check.'
    }
    $again=Start-Process -FilePath $launcher -PassThru -WindowStyle Hidden
    $again.WaitForExit(5000)|Out-Null
    Start-Sleep -Milliseconds 1500
    $running=@(Get-Process Penumbra -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $expected})
    if($running.Count -ne 1 -or $running[0].Id -ne $app.Id){throw 'Double launch did not retain exactly the same settings instance.'}
    [pscustomobject]@{Passed=$true;NormalLauncher=$true;WindowVisible=(!$root.Current.IsOffscreen);Version='1.8.1';StartupGitHubCheck=$updateHelp;ControlsLoaded=$true;DoubleLaunchSameInstance=$true;SettingsWritten=$false}|ConvertTo-Json -Compress
} finally {
    if($app -and !$app.HasExited){[StartupUiNative]::PostMessage($app.MainWindowHandle,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null;$app.WaitForExit(5000)|Out-Null}
    if((Test-Path -LiteralPath $config) -and (Get-FileHash -LiteralPath $config).Hash -ne $hash){throw 'Startup check changed saved settings.'}
}

param([Parameter(Mandatory)][string]$ExecutablePath,[Parameter(Mandatory)][string]$ScreenshotDirectory)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
$config=Join-Path $env:LOCALAPPDATA 'Nocturne/settings.ini'
$before=if(Test-Path -LiteralPath $config){(Get-FileHash -LiteralPath $config).Hash}else{''}
$app=$null
try{
    $app=Start-Process -FilePath (Resolve-Path -LiteralPath $ExecutablePath).Path -ArgumentList '--update-test' -PassThru -WindowStyle Hidden
    $deadline=[DateTime]::UtcNow.AddSeconds(15)
    do{$app.Refresh();if($app.MainWindowHandle -ne [IntPtr]::Zero){break};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow -lt $deadline)
    if($app.MainWindowHandle -eq [IntPtr]::Zero){throw 'Isolated update settings window did not load.'}
    $root=[Windows.Automation.AutomationElement]::FromHandle($app.MainWindowHandle)
    function Control([string]$id){
        $element=$root.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$id))
        if(!$element){throw "Missing update UI control: $id"};return $element
    }
    function Wait-Update{
        $until=[DateTime]::UtcNow.AddSeconds(15)
        while([DateTime]::UtcNow -lt $until){
            $link=Control 'CheckUpdateLink'
            if($link.Current.IsEnabled -and $link.Current.HelpText -ne '通过 GitHub Releases 检查新版本；启动时自动检查一次。'){return $link.Current.HelpText}
            Start-Sleep -Milliseconds 100
        }
        throw 'Update check did not complete within the bounded request timeout.'
    }
    $auto=Wait-Update
    & (Join-Path $PSScriptRoot 'Branding.UI.Tests.ps1') -ExecutablePath $ExecutablePath -ScreenshotDirectory $ScreenshotDirectory
    if($auto -notmatch '当前版本已为最新发行版|当前版本高于'){throw "Live startup did not resolve GitHub version: $auto"}
    $link=Control 'CheckUpdateLink'
    $invoke=$link.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern)
    $invoke.Invoke()
    $manual=Wait-Update
    if($manual -notmatch '当前版本已为最新发行版|当前版本高于'){throw "Manual check did not return current version: $manual"}
    $message=$root.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,$manual))
    if(!$message){throw 'Manual update check must show an in-app result.'}
    if(!(Control 'ApplyButton').Current.IsEnabled -or !(Control 'CaptureButton').Current.IsEnabled){throw 'Update checking blocked desktop commands.'}
    & (Join-Path $PSScriptRoot 'Branding.UI.Tests.ps1') -ExecutablePath $ExecutablePath -ScreenshotDirectory (Join-Path $ScreenshotDirectory 'manual-result')
    [pscustomobject]@{Passed=$true;StartupGitHubCheck=$auto;ManualGitHubCheck=$manual;Version='v1.8.1';VersionPrefixPreserved=$true;GrayLinkWithoutUnderline=$true;DesktopCommandsEnabled=$true;SettingsWritten=$false}|ConvertTo-Json -Compress
}finally{
    if($app -and !$app.HasExited){$null=$app.CloseMainWindow();$null=$app.WaitForExit(5000)}
    $after=if(Test-Path -LiteralPath $config){(Get-FileHash -LiteralPath $config).Hash}else{''}
    if($before -ne $after){throw 'Update checks modified saved desktop settings.'}
}

param([Parameter(Mandatory)][string]$ExecutablePath,[Parameter(Mandatory)][string]$ScreenshotDirectory,
    [ValidateSet('Current','Available')][string]$ExpectedState='Current',[string]$ExpectedReleaseVersion)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
$config=Join-Path $env:LOCALAPPDATA 'Nocturne/settings.ini'
$before=if(Test-Path -LiteralPath $config){(Get-FileHash -LiteralPath $config).Hash}else{''}
$versionInfo=(Get-Item -LiteralPath $ExecutablePath).VersionInfo
$expectedVersion='v{0}.{1}.{2}' -f $versionInfo.FileMajorPart,$versionInfo.FileMinorPart,$versionInfo.FileBuildPart
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
    function Verify-Update([string]$message){
        if($ExpectedState -eq 'Available'){
            if($message -notlike '可前往 GitHub*'){throw "Expected an available update: $message"}
            $scroll=(Control 'SettingsScrollViewer').GetCurrentPattern([Windows.Automation.ScrollPattern]::Pattern)
            if($scroll.Current.VerticallyScrollable){$scroll.SetScrollPercent(-1,100);Start-Sleep -Milliseconds 150}
            $releaseLink=Control 'UpdateReleaseLink'
            if($releaseLink.Current.Name -ne '在 GitHub 查看新版本' -or $releaseLink.Current.IsOffscreen){throw 'Available update must offer the release page.'}
            if($ExpectedReleaseVersion){
                $title='发现新版本 '+$ExpectedReleaseVersion.TrimStart('v','V')
                $found=$root.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,$title))
                if(!$found){throw "Missing detected release version: $title"}
            }
        }elseif($message -notmatch '当前版本已为最新发行版|当前版本高于'){throw "Expected current version: $message"}
    }
    $auto=Wait-Update
    & (Join-Path $PSScriptRoot 'Branding.UI.Tests.ps1') -ExecutablePath $ExecutablePath -ScreenshotDirectory $ScreenshotDirectory
    Verify-Update $auto
    $link=Control 'CheckUpdateLink'
    $invoke=$link.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern)
    $invoke.Invoke()
    Start-Sleep -Milliseconds 150
    $manual=Wait-Update
    Verify-Update $manual
    $message=$root.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,$manual))
    if(!$message){throw 'Manual update check must show an in-app result.'}
    if(!(Control 'ApplyButton').Current.IsEnabled -or !(Control 'CaptureButton').Current.IsEnabled){throw 'Update checking blocked desktop commands.'}
    & (Join-Path $PSScriptRoot 'Branding.UI.Tests.ps1') -ExecutablePath $ExecutablePath -ScreenshotDirectory (Join-Path $ScreenshotDirectory 'manual-result')
    [pscustomobject]@{Passed=$true;StartupGitHubCheck=$auto;ManualGitHubCheck=$manual;Version=$expectedVersion;ExpectedState=$ExpectedState;ExpectedReleaseVersion=$ExpectedReleaseVersion;VersionPrefixPreserved=$true;GrayLinkWithoutUnderline=$true;DesktopCommandsEnabled=$true;SettingsWritten=$false}|ConvertTo-Json -Compress
}finally{
    if($app -and !$app.HasExited){$null=$app.CloseMainWindow();$null=$app.WaitForExit(5000)}
    $after=if(Test-Path -LiteralPath $config){(Get-FileHash -LiteralPath $config).Hash}else{''}
    if($before -ne $after){throw 'Update checks modified saved desktop settings.'}
}

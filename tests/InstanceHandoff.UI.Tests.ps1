param([Parameter(Mandatory)][string]$ExecutablePath,[Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class InstanceHandoffNative {
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr GetProp(IntPtr h,string n);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern bool SetProp(IntPtr h,string n,IntPtr v);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr RemoveProp(IntPtr h,string n);
}
'@
$executable=(Resolve-Path -LiteralPath $ExecutablePath).Path
$output=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $output|Out-Null
$scope=[Guid]::NewGuid().ToString('N')
$copyRoot=Join-Path $output ('second-build-'+$scope)
New-Item -ItemType Directory -Path $copyRoot|Out-Null
$appRoot=[IO.Path]::GetDirectoryName($executable)
Copy-Item -LiteralPath $appRoot -Destination (Join-Path $copyRoot 'app') -Recurse
$other=Join-Path $copyRoot 'app/Penumbra.exe'
$launcher=Join-Path ([IO.Path]::GetDirectoryName($appRoot)) 'Penumbra.exe'
Copy-Item -LiteralPath $launcher -Destination (Join-Path $copyRoot 'Penumbra.exe')
$processes=[Collections.Generic.List[Diagnostics.Process]]::new()
$results=[Collections.Generic.List[object]]::new()
$settings=Join-Path $env:LOCALAPPDATA 'Nocturne/settings.ini'
$before=if(Test-Path -LiteralPath $settings){(Get-FileHash -LiteralPath $settings).Hash}else{''}
$backends=@(Get-Process Nocturne.Renderer -ErrorAction SilentlyContinue|ForEach-Object{$_.Id}|Sort-Object)
function Check([bool]$ok,[string]$name){
 if(!$ok){throw $name}
 $results.Add([pscustomobject]@{Case=$name;Passed=$true})
}
function Start-App([string]$path){
 $report=Join-Path $output ('startup-'+[Guid]::NewGuid().ToString('N')+'.txt')
 $app=Start-Process -FilePath $path -ArgumentList @('--layout-test',"--instance-test=$scope",('"--startup-log='+$report+'"')) -PassThru -WindowStyle Hidden
 $processes.Add($app)
 return $app
}
function Wait-Window([Diagnostics.Process]$app){
 $until=[DateTime]::UtcNow.AddSeconds(15)
 do{
  $app.Refresh()
  if($app.HasExited){throw "Test settings exited before showing its window: $($app.Id)"}
  if($app.MainWindowHandle -ne [IntPtr]::Zero){
   $root=[Windows.Automation.AutomationElement]::FromHandle($app.MainWindowHandle)
   $button=$root.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,'ApplyButton'))
   if($button -and $button.Current.IsEnabled){return}
  }
  Start-Sleep -Milliseconds 100
 }while([DateTime]::UtcNow -lt $until)
 throw 'Replacement settings window did not become ready.'
}
try{
 $first=Start-App $executable
 Wait-Window $first
 Check ($first.MainWindowHandle -ne [IntPtr]::Zero) 'initial settings ready'
 $duplicate=Start-App $executable
 Check ($duplicate.WaitForExit(12000)) 'same-path duplicate exits'
 $first.Refresh()
 Check (!$first.HasExited) 'same-path launch retains original PID'

 $second=Start-App $other
 Wait-Window $second
 Check ($first.WaitForExit(5000)) 'different-directory build gracefully closes old settings'
 Check ([InstanceHandoffNative]::GetProp($second.MainWindowHandle,'Penumbra.Settings.ReplacementProtocol') -ne [IntPtr]::Zero) 'replacement protocol registered'

 $versionProperty='Penumbra.Settings.Version'
 $savedVersion=[InstanceHandoffNative]::GetProp($second.MainWindowHandle,$versionProperty)
 $runningVersion=(Get-Item -LiteralPath $executable).VersionInfo
 # PackVersion stores Major+1 so zero can represent an absent property.
 $higherVersion=([long]($runningVersion.FileMajorPart+2) -shl 48) -bor ([long]$runningVersion.FileMinorPart -shl 32) -bor ([long]$runningVersion.FileBuildPart -shl 16)
 Check ([InstanceHandoffNative]::SetProp($second.MainWindowHandle,$versionProperty,[IntPtr]$higherVersion)) 'advertise newer in-memory version'
 $older=Start-App $executable
 Check ($older.WaitForExit(12000)) 'older launch exits without downgrade'
 $second.Refresh()
 Check (!$second.HasExited) 'newer running version is preserved'
 $null=[InstanceHandoffNative]::SetProp($second.MainWindowHandle,$versionProperty,$savedVersion)

 $null=[InstanceHandoffNative]::RemoveProp($second.MainWindowHandle,'Penumbra.Settings.ReplacementProtocol')
 $null=[InstanceHandoffNative]::RemoveProp($second.MainWindowHandle,$versionProperty)
 $legacyReplacement=Start-App $executable
 Wait-Window $legacyReplacement
 Check ($second.WaitForExit(5000)) 'legacy close and executable-version fallback succeeds'

 $entry=Start-App (Join-Path $copyRoot 'Penumbra.exe')
 Check ($entry.WaitForExit(5000) -and $entry.ExitCode -eq 0) 'top-level launcher returns successfully'
 $until=[DateTime]::UtcNow.AddSeconds(15)
 $launched=$null
 do{
  $launched=Get-Process Penumbra -ErrorAction SilentlyContinue|Where-Object{$_.Path -eq $other -and $_.MainWindowHandle -ne [IntPtr]::Zero}|Select-Object -First 1
  if($launched){break}
  Start-Sleep -Milliseconds 100
 }while([DateTime]::UtcNow -lt $until)
 Check ([bool]$launched) 'top-level launcher shows selected build'
 $processes.Add($launched)
 Wait-Window $launched
 Check ($legacyReplacement.WaitForExit(5000)) 'launcher handoff closes previous settings'

 $duplicates=@(1..3|ForEach-Object{Start-App $other})
 foreach($item in $duplicates){Check ($item.WaitForExit(12000)) 'rapid same-build relaunch exits'}
 $launched.Refresh()
 Check (!$launched.HasExited) 'rapid relaunch keeps selected instance'
 $running=@(Get-Process Penumbra -ErrorAction SilentlyContinue|Where-Object{$_.Path -in @($executable,$other)})
 Check ($running.Count -eq 1 -and $running[0].Id -eq $launched.Id) 'exactly one tested settings instance remains'
 $results|ConvertTo-Json -Depth 3|Out-File -LiteralPath (Join-Path $output 'results.json') -Encoding utf8
 [pscustomobject]@{Passed=$true;Checks=$results.Count;SamePathReused=$true;CrossDirectoryHandoff=$true;LegacyFallback=$true;DowngradePrevented=$true;LauncherHandoff=$true;RapidRelaunch=$true;IsolatedScope=$scope;SettingsWritten=$false}|ConvertTo-Json -Compress
}finally{
 foreach($app in $processes){
  if(!$app.HasExited){$null=$app.CloseMainWindow();if(!$app.WaitForExit(5000)){throw "Test settings did not close: $($app.Id)"}}
 }
 $after=if(Test-Path -LiteralPath $settings){(Get-FileHash -LiteralPath $settings).Hash}else{''}
 if($before -ne $after){throw 'Instance tests changed saved settings.'}
 $remaining=@(Get-Process Nocturne.Renderer -ErrorAction SilentlyContinue|ForEach-Object{$_.Id}|Sort-Object)
 if(($backends -join ',') -ne ($remaining -join ',')){throw 'Instance tests started or stopped a desktop backend.'}
}

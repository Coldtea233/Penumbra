param([Parameter(Mandatory)][string]$RendererPath,[Parameter(Mandatory)][string]$OutputDirectory,[int]$Seconds=20,[switch]$OriginallyNoBackend,[switch]$SupplementalOnly)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
Add-Type -Path (Join-Path $PSScriptRoot 'PerformanceSampler.cs')
Add-Type @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class PerformanceUiNative {
 public delegate bool Callback(IntPtr h,IntPtr p);
 [DllImport("user32.dll")]static extern bool EnumWindows(Callback c,IntPtr p);
 [DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern IntPtr GetProp(IntPtr h,string n);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetClassName(IntPtr h,StringBuilder n,int count);
 public static IntPtr FindWindow(string c,string n){IntPtr result=IntPtr.Zero;EnumWindows((h,p)=>{var name=new StringBuilder(128);GetClassName(h,name,name.Capacity);if(name.ToString()==c){result=h;return false;}return true;},IntPtr.Zero);return result;}
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int w,int ht,uint flags);
 [StructLayout(LayoutKind.Sequential)]public struct Rect {public int Left,Top,Right,Bottom;}
 public static IntPtr Settings(uint pid){IntPtr result=IntPtr.Zero;EnumWindows((h,p)=>{uint owner;GetWindowThreadProcessId(h,out owner);if(owner==pid&&GetProp(h,"Penumbra.Settings")!=IntPtr.Zero){result=h;return false;}return true;},IntPtr.Zero);return result;}
}
'@
$output=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $output|Out-Null
$renderer=(Resolve-Path -LiteralPath $RendererPath).Path
$config=Join-Path $env:LOCALAPPDATA 'Nocturne/settings.ini'
$configHash=(Get-FileHash -LiteralPath $config).Hash
$startup=(Get-ItemProperty 'HKCU:/Software/Microsoft/Windows/CurrentVersion/Run' -Name Nocturne -ErrorAction SilentlyContinue).Nocturne
$status=Join-Path $env:LOCALAPPDATA 'Nocturne/status.txt'
$statusBytes=if(Test-Path -LiteralPath $status){[IO.File]::ReadAllBytes($status)}else{$null}
$originalBackend=Get-Process Nocturne.Renderer -ErrorAction SilentlyContinue|Select-Object -First 1
$originalBackendPath=if($originalBackend){$originalBackend.Path}else{''}
if($OriginallyNoBackend){$originalBackendPath=''}
$originalPaused=$statusBytes -and ([Text.Encoding]::UTF8.GetString($statusBytes) -like '*已暂停*')
$uis=@();$samples=[Collections.Generic.List[object]]::new();$helper=$null;$changedSession=$false
function Element($root,[string]$id) {
 $e=$null
 for($attempt=0;$attempt -lt 15;$attempt++){
  $e=$root.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$id))
  if($e){break};Start-Sleep -Milliseconds 100
 }
 if(!$e){throw "Cannot preserve settings control: $id"};return $e
}
function Read-Ui($app){
 $hwnd=[PerformanceUiNative]::Settings($app.Id);if($hwnd -eq [IntPtr]::Zero){throw 'Settings HWND unavailable; refusing to close an uninspectable UI.'}
 $root=[Windows.Automation.AutomationElement]::FromHandle($hwnd)
 $values=@{}
 foreach($id in @('OpacitySlider','RadiusSlider','FeatherSlider','ShapeSlider','BlurRadiusSlider','SaturationSlider')){
  $values[$id]=@{Kind='Range';Value=(Element $root $id).GetCurrentPattern([Windows.Automation.RangeValuePattern]::Pattern).Current.Value}
 }
 foreach($id in @('DarkenIconsCheckBox','FocusOnlyCheckBox','BlurEnabledCheckBox','SaturationEnabledCheckBox','StartupToggle')){
  $values[$id]=@{Kind='Toggle';Value=(Element $root $id).GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Current.ToggleState}
 }
 foreach($id in @('TintCombo','MonitorCombo')){
  $values[$id]=@{Kind='Selection';Value=(Element $root $id).GetCurrentPattern([Windows.Automation.SelectionPattern]::Pattern).Current.GetSelection()[0].Current.Name}
 }
 foreach($id in @('CenterXBox','CenterYBox')){
  $container=Element $root $id
  $edit=$container.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::IsValuePatternAvailableProperty,$true))
  if(!$edit){throw "Cannot preserve numeric input $id"}
  $values[$id]=@{Kind='Value';Value=$edit.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).Current.Value}
 }
 $rect=[PerformanceUiNative+Rect]::new();[void][PerformanceUiNative]::GetWindowRect($hwnd,[ref]$rect)
 return @{Process=$app;Path=$app.Path;Hwnd=$hwnd;Values=$values;Rect=$rect}
}
function Restore-Ui($saved){
 $app=Start-Process -FilePath $saved.Path -PassThru -WindowStyle Hidden
 $window=[IntPtr]::Zero
 for($i=0;$i -lt 80;$i++){Start-Sleep -Milliseconds 100;$window=[PerformanceUiNative]::Settings($app.Id);if($window -ne [IntPtr]::Zero){break}}
 if($window -eq [IntPtr]::Zero){throw 'Could not restore previous settings window.'}
 Start-Sleep -Seconds 2
 $root=[Windows.Automation.AutomationElement]::FromHandle($window)
 foreach($id in $saved.Values.Keys){
  $data=$saved.Values[$id];$control=Element $root $id
  switch($data.Kind){
   Range {$control.GetCurrentPattern([Windows.Automation.RangeValuePattern]::Pattern).SetValue($data.Value)}
   Toggle {$pattern=$control.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern);if($pattern.Current.ToggleState -ne $data.Value){$pattern.Toggle()}}
   Selection {
    $control.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern).Expand()
    $item=$control.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,$data.Value))
    if(!$item){throw "Could not restore selection $id"};$item.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
    $control.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern).Collapse()
   }
   Value {$edit=$control.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::IsValuePatternAvailableProperty,$true));$edit.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($data.Value)}
  }
 }
 $r=$saved.Rect;[void][PerformanceUiNative]::SetWindowPos($window,[IntPtr]::Zero,$r.Left,$r.Top,$r.Right-$r.Left,$r.Bottom-$r.Top,0x14)
 $restored=Read-Ui $app
 foreach($id in $saved.Values.Keys){
  $before=$saved.Values[$id].Value;$after=$restored.Values[$id].Value
  $matches=if($saved.Values[$id].Kind -eq 'Range'){[Math]::Abs([double]$before-[double]$after) -lt .00001}else{$before -eq $after}
  if(!$matches){Write-Warning "Unsaved control restore mismatch: $id expected=$before actual=$after"}
 }
}
function Stop-Backend {
 $controller=[PerformanceUiNative]::FindWindow('Nocturne.Controller',$null)
 if($controller -ne [IntPtr]::Zero){[void][PerformanceUiNative]::PostMessage($controller,0x10,[IntPtr]::Zero,[IntPtr]::Zero)}
 for($i=0;$i -lt 60;$i++){if(!(Get-Process Nocturne.Renderer -ErrorAction SilentlyContinue)){return};Start-Sleep -Milliseconds 100}
 throw 'Existing renderer did not stop safely; measurement cancelled.'
}
function Stop-Helper {
 if($script:helper -and !$script:helper.HasExited){
  $window=[PerformanceUiNative]::FindWindow('Penumbra.Performance.Controller',$null)
  if($window -ne [IntPtr]::Zero){[void][PerformanceUiNative]::PostMessage($window,0x10,[IntPtr]::Zero,[IntPtr]::Zero)}
  if(!$script:helper.WaitForExit(5000)){$script:helper.Kill();$script:helper.WaitForExit()}
 }
 $script:helper=$null
}
try {
 # Capture ALL editable controls before closing any user settings. No Apply,
 # no user INI writes and no silent loss of unsaved slider positions.
 foreach($app in @(Get-Process Penumbra -ErrorAction SilentlyContinue)){$uis+=Read-Ui $app}
 $uis|ForEach-Object{[pscustomobject]@{Path=$_.Path;Values=$_.Values;Rect=$_.Rect}}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $output 'original-ui.json') -Encoding utf8
 foreach($ui in $uis){[void][PerformanceUiNative]::PostMessage($ui.Hwnd,0x10,[IntPtr]::Zero,[IntPtr]::Zero);if(!$ui.Process.WaitForExit(7000)){throw 'Settings refused to close; benchmark cancelled.'};$changedSession=$true}
 Stop-Backend;$changedSession=$true
 $stages=@(@('Baseline',1,0),@('BlurOff',1,0),@('BlurOn2',1,2),@('BlurOn2',2,2),@('BlurOff',2,0),@('Baseline',2,0),@('BlurOn12',1,12),@('BlurOn48',1,48))
 if($SupplementalOnly){$stages=@(@('BlurOn12',1,12),@('BlurOn48',1,48))}
 foreach($stage in $stages){
  $mode=$stage[0];$round=[int]$stage[1];$radius=[int]$stage[2]
  if($mode -ne 'Baseline'){
   $argument=if($mode -eq 'BlurOff'){'off'}else{'on'}
   $helper=Start-Process -FilePath $renderer -ArgumentList @($argument,$radius,('"'+$output+'"')) -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $output "$mode-$round-renderer.json") -RedirectStandardError (Join-Path $output "$mode-$round-renderer.err")
  }
  Start-Sleep -Seconds 4
  if($helper -and $helper.HasExited){throw "Production-path renderer failed; inspect $mode-$round-renderer.json"}
  if(Get-Process Penumbra,Nocturne.Renderer -ErrorAction SilentlyContinue){throw 'Real Penumbra unexpectedly started; refusing contaminated baseline.'}
  Write-Output ("Measuring "+$mode+" round "+$round+" for "+$Seconds+" seconds")
  $result=[PenumbraPerformance]::Measure($mode,$round,$Seconds,$(if($helper){$helper.Id}else{0})).GetAwaiter().GetResult()
  foreach($sample in $result){$samples.Add($sample)}
  $samples | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output 'samples.json') -Encoding utf8
  $bad=@($result|Where-Object{ !$_.ForegroundStable -or ($mode -ne 'Baseline' -and (($_.BackendState -band 2) -eq 0 -or ($_.BackendState -band 24) -ne 0)) -or ($mode -like 'BlurOn*' -and ($_.BackendState -band 4) -eq 0) -or ($mode -eq 'BlurOff' -and ($_.BackendState -band 4) -ne 0)})
  Stop-Helper
  if($bad.Count){throw "Unstable desktop/focus/timer state in $mode ($($bad.Count) samples). Raw data retained; no false pass."}
 }
 $summary=@()
 foreach($mode in @('Baseline','BlurOff','BlurOn2','BlurOn12','BlurOn48')){
  $group=@($samples|Where-Object Mode -eq $mode)
  if(!$group.Count){continue}
  $row=[ordered]@{Mode=$mode;Samples=$group.Count;Seconds=($group.ElapsedSeconds|Measure-Object -Sum).Sum}
  foreach($metric in @('SystemCpuPercent','SystemGpuBusyPercent')){$v=@($group|ForEach-Object{$_.$metric}|Where-Object{$null -ne $_});$row[$metric]=if($v.Count){($v|Measure-Object -Average).Average}else{$null}}
  foreach($process in @('Penumbra','Dwm','WallpaperEngine')){
   $metrics=[ordered]@{}
   foreach($metric in @('CpuPercent','WorkingSetMiB','PrivateMiB','GpuBusyPercent','DedicatedGpuMiB','SharedGpuMiB')){
    $v=@($group|ForEach-Object{$_.$process.$metric}|Where-Object{$null -ne $_});$metrics[$metric]=if($v.Count){($v|Measure-Object -Average).Average}else{$null}
   }
   $row[$process]=$metrics
  }
  $summary+=[pscustomobject]$row
 }
 $summary|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $output 'summary.json') -Encoding utf8
  $summary|ConvertTo-Json -Depth 8
} catch {
 Write-Output ('BENCHMARK FAILURE: '+$_.Exception.ToString())
 throw
} finally {
 Stop-Helper
 if($changedSession){
  if($originalBackendPath){
   if($originalPaused){Start-Process -FilePath $originalBackendPath -ArgumentList '--tray-only' -WindowStyle Hidden|Out-Null}
   else{Start-Process -FilePath $originalBackendPath -WindowStyle Hidden|Out-Null}
  }
  foreach($ui in $uis){Restore-Ui $ui}
  if($statusBytes){[IO.File]::WriteAllBytes($status,$statusBytes)}
 }
 if((Get-FileHash -LiteralPath $config).Hash -ne $configHash){throw 'User settings hash changed during benchmark.'}
 if((Get-ItemProperty 'HKCU:/Software/Microsoft/Windows/CurrentVersion/Run' -Name Nocturne -ErrorAction SilentlyContinue).Nocturne -ne $startup){throw 'Startup registration changed during benchmark.'}
 Write-Output 'Original settings windows, unsaved controls, backend state and configuration restored.'
}

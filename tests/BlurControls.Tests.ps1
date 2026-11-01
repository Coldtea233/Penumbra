param([Parameter(Mandatory)][string]$ExecutablePath, [switch]$ReplaceRunningWindow, [string]$StateBackupPath, [switch]$SkipMouseDrag)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class BlurControlsNative {
 [StructLayout(LayoutKind.Sequential)] public struct Point { public int X,Y; }
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int w,int ht,uint f);
 [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")] public static extern bool GetCursorPos(out Point p);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f,uint x,uint y,uint d,UIntPtr extra);
}
'@
$expected = (Resolve-Path -LiteralPath $ExecutablePath).Path
$configPath = Join-Path $env:LOCALAPPDATA 'Nocturne\settings.ini'
$configHash = if (Test-Path -LiteralPath $configPath) { (Get-FileHash -LiteralPath $configPath).Hash } else { '' }
function Find-Control($root, [string]$id) {
    for ($attempt=0; $attempt -lt 10; $attempt++) {
        $control = $root.FindFirst([Windows.Automation.TreeScope]::Descendants,
            [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty, $id))
        if ($control) { return $control }
        Start-Sleep -Milliseconds 100
    }
    throw "Control not found: $id"
}
function Read-State($root) {
    $state = @{ Ranges=@{}; Toggles=@{}; Selections=@{} }
    foreach ($id in @('OpacitySlider','RadiusSlider','FeatherSlider','ShapeSlider','CenterXBox','CenterYBox','BlurRadiusSlider')) {
        if ($id -eq 'BlurRadiusSlider' -and !$root.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$id))) {
            # A pre-blur settings build has nothing to discard for this field.
            # Preserve the saved value/default when migrating that older UI.
            $radiusLine=Get-Content -LiteralPath $configPath -ErrorAction SilentlyContinue | Where-Object {$_ -match '^blurRadius='} | Select-Object -First 1
            $state.Ranges[$id]=if($radiusLine){[double]::Parse($radiusLine.Split('=',2)[1],[Globalization.CultureInfo]::InvariantCulture)}else{12.0}
            continue
        }
        $state.Ranges[$id] = (Find-Control $root $id).GetCurrentPattern([Windows.Automation.RangeValuePattern]::Pattern).Current.Value
    }
    foreach ($id in @('StartupToggle','DarkenIconsCheckBox','FocusOnlyCheckBox','BlurEnabledCheckBox')) {
        if ($id -eq 'BlurEnabledCheckBox' -and !$root.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$id))) {
            $enabledLine=Get-Content -LiteralPath $configPath -ErrorAction SilentlyContinue | Where-Object {$_ -match '^blurEnabled='} | Select-Object -First 1
            $state.Toggles[$id]=if($enabledLine -and $enabledLine.Split('=',2)[1] -eq '1'){[Windows.Automation.ToggleState]::On}else{[Windows.Automation.ToggleState]::Off}
            continue
        }
        $state.Toggles[$id] = (Find-Control $root $id).GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Current.ToggleState
    }
    foreach ($id in @('TintCombo','MonitorCombo')) {
        $state.Selections[$id] = (Find-Control $root $id).GetCurrentPattern([Windows.Automation.SelectionPattern]::Pattern).Current.GetSelection()[0].Current.Name
    }
    $state
}
function Restore-State($root, $state) {
    foreach ($id in $state.Toggles.Keys) {
        $p = (Find-Control $root $id).GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern)
        if ($p.Current.ToggleState -ne $state.Toggles[$id]) { $p.Toggle() }
    }
    foreach ($id in $state.Ranges.Keys) {
        (Find-Control $root $id).GetCurrentPattern([Windows.Automation.RangeValuePattern]::Pattern).SetValue($state.Ranges[$id])
    }
    foreach ($id in $state.Selections.Keys) {
        $combo = Find-Control $root $id
        $items = $combo.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)
        foreach ($item in $items) {
            $p = $null
            if ($item.Current.Name -eq $state.Selections[$id] -and $item.TryGetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern,[ref]$p)) { $p.Select(); break }
        }
    }
}
function Assert($condition, [string]$message) { if (!$condition) { throw $message } }
$existing = @(Get-Process Penumbra -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowTitle -in @('Penumbra','Penumbra 半影') })
if ($existing.Count -gt 1) { throw 'Multiple settings windows; refusing to replace.' }
$state = if ($StateBackupPath) { Import-Clixml -LiteralPath $StateBackupPath } else { $null }
$bounds = $null; $scrollPercent = -1
if ($existing.Count -and $existing[0].Path -ne $expected) {
    if (!$ReplaceRunningWindow) { throw 'Close the settings window or explicitly use -ReplaceRunningWindow to preserve and migrate its unsaved values.' }
    $old = $existing[0]
    $root = [Windows.Automation.AutomationElement]::FromHandle($old.MainWindowHandle)
    if (!$state) { $state = Read-State $root }
    $bounds = $root.Current.BoundingRectangle
    $scrollPercent = (Find-Control $root 'SettingsScrollViewer').GetCurrentPattern([Windows.Automation.ScrollPattern]::Pattern).Current.VerticalScrollPercent
    $backup = Join-Path ([IO.Path]::GetTempPath()) ('Penumbra-unsaved-controls-'+[Guid]::NewGuid().ToString('N')+'.clixml')
    $state | Export-Clixml -LiteralPath $backup
    Write-Output "Unsaved controls backup: $backup"
    [BlurControlsNative]::PostMessage($old.MainWindowHandle,0x10,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
    Assert ($old.WaitForExit(15000)) 'Existing settings window did not close; backend was not stopped.'
}
if (!$existing.Count -or $existing[0].Path -ne $expected) { Start-Process -FilePath $expected | Out-Null }
$app = $null
for ($i=0; $i -lt 100; $i++) {
    $app = Get-Process Penumbra -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $expected -and $_.MainWindowHandle -ne [IntPtr]::Zero } | Select-Object -First 1
    if ($app) { break }; Start-Sleep -Milliseconds 100
}
Assert ($null -ne $app) 'New settings window did not open.'
$root = [Windows.Automation.AutomationElement]::FromHandle($app.MainWindowHandle)
for ($i=0; $i -lt 100; $i++) {
    $root = [Windows.Automation.AutomationElement]::FromHandle($app.MainWindowHandle)
    $status = Find-Control $root 'StatusText'
    if ($status -and $status.Current.Name -notmatch '正在|Penumbra 半影') { break }; Start-Sleep -Milliseconds 100
}
if (!$state) { $state = Read-State $root }
$dpi = [BlurControlsNative]::SetThreadDpiAwarenessContext([IntPtr](-4))
$cursor = [BlurControlsNative+Point]::new()
[BlurControlsNative]::GetCursorPos([ref]$cursor) | Out-Null
$checks = 0
try {
    if ($bounds -and !$bounds.IsEmpty -and [double]::IsFinite($bounds.X) -and [double]::IsFinite($bounds.Y)) { [BlurControlsNative]::SetWindowPos($app.MainWindowHandle,[IntPtr]::Zero,[int]$bounds.X,[int]$bounds.Y,[int]$bounds.Width,[int]$bounds.Height,0x14) | Out-Null }
    Restore-State $root $state
    $slider = Find-Control $root 'BlurRadiusSlider'
    $range = $slider.GetCurrentPattern([Windows.Automation.RangeValuePattern]::Pattern)
    $box = (Find-Control $root 'BlurRadiusBox').GetCurrentPattern([Windows.Automation.RangeValuePattern]::Pattern)
    $toggle = (Find-Control $root 'BlurEnabledCheckBox').GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern)
    if ($toggle.Current.ToggleState -ne [Windows.Automation.ToggleState]::Off) { $toggle.Toggle() }
    Assert ($slider.Current.IsEnabled) 'Radius must remain adjustable with blur off.'; $checks++
    foreach ($value in @(0,1,12,32,48)) {
        $range.SetValue($value)
        Assert ($box.Current.Value -eq $value) "Slider to number synchronization failed: $value"; $checks++
    }
    foreach ($value in @(48,32,12,1,0)) {
        $box.SetValue($value)
        Assert ($range.Current.Value -eq $value) "Number to slider synchronization failed: $value"; $checks++
    }
    $toggle.Toggle()
    Assert ($range.Current.Value -eq 0) 'Enabling blur must preserve radius.'; $checks++
    $scroll = (Find-Control $root 'SettingsScrollViewer').GetCurrentPattern([Windows.Automation.ScrollPattern]::Pattern)
    if ($scroll.Current.VerticallyScrollable) { $scroll.SetScrollPercent(-1,100) }
    Start-Sleep -Milliseconds 200
    $dragValue=$null
    if (!$SkipMouseDrag) {
    # Exercise real mouse input, not only UI Automation SetValue.
    $range.SetValue(12); Start-Sleep -Milliseconds 100
    $rect = $slider.Current.BoundingRectangle
    $x = [int]($rect.Left + 20 + ($rect.Width-40)*0.25)
    $y = [int]($rect.Top + $rect.Height/2)
    [BlurControlsNative]::SetCursorPos($x,$y) | Out-Null
    [BlurControlsNative]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
    Start-Sleep -Milliseconds 60
    for ($i=1; $i -le 16; $i++) {
        # SetCursorPos alone does not promote drag movements to WinUI pointer
        # events on this system. Inject an actual mouse-move input packet.
        [BlurControlsNative]::mouse_event(1,12,0,0,[UIntPtr]::Zero)
        Start-Sleep -Milliseconds 25
    }
    [BlurControlsNative]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
    Start-Sleep -Milliseconds 100
    $dragValue = $range.Current.Value
    Assert ($dragValue -gt 20) "Mouse drag did not change radius sufficiently (12 -> $dragValue)."; $checks++
    Assert ($box.Current.Value -eq $dragValue) 'Mouse drag and numeric value disagree.'; $checks++
    }
    [pscustomobject]@{ Passed=$true; Checks=$checks; MouseDragSkipped=[bool]$SkipMouseDrag; MouseDragTo=$dragValue; Process=$app.Id; Path=$app.Path; PersonalSettingsWritten=$false } | ConvertTo-Json -Compress
}
finally {
    [BlurControlsNative]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
    Restore-State $root $state
    $scroll = (Find-Control $root 'SettingsScrollViewer').GetCurrentPattern([Windows.Automation.ScrollPattern]::Pattern)
    if ($scrollPercent -ge 0 -and $scroll.Current.VerticallyScrollable) { $scroll.SetScrollPercent(-1,$scrollPercent) }
    [BlurControlsNative]::SetCursorPos($cursor.X,$cursor.Y) | Out-Null
    [BlurControlsNative]::SetThreadDpiAwarenessContext($dpi) | Out-Null
    $afterHash = if (Test-Path -LiteralPath $configPath) { (Get-FileHash -LiteralPath $configPath).Hash } else { '' }
    Assert ($configHash -eq $afterHash) 'Personal settings changed unexpectedly.'
}

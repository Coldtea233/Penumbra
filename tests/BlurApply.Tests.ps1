param([Parameter(Mandatory)][string]$ExecutablePath,[switch]$CaptureProductionWindow,[switch]$MeasureDesktopFixture,[switch]$SkipPreviewPixels,[switch]$TemporarilyEnableBlur,[switch]$LiveRadius)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,System.Drawing
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class BlurApplyNative {
 [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c,IntPtr n);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr p,IntPtr a,string c,IntPtr n);
 [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h,uint c);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [StructLayout(LayoutKind.Sequential)]public struct Rect{public int Left,Top,Right,Bottom;}
 [DllImport("user32.dll")]static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")]static extern int GetWindowRgn(IntPtr h,IntPtr r);
 [DllImport("gdi32.dll")]static extern IntPtr CreateRectRgn(int l,int t,int r,int b);
 [DllImport("gdi32.dll")]static extern bool RectInRegion(IntPtr h,ref Rect r);
 [DllImport("gdi32.dll")]static extern bool DeleteObject(IntPtr h);
 public static bool TaskbarExcluded(IntPtr blur){
  Rect area;if(!GetWindowRect(blur,out area))return false;
  IntPtr region=CreateRectRgn(0,0,0,0);
  try{
   if(GetWindowRgn(blur,region)==0)return false;
   foreach(string cls in new[]{"Shell_TrayWnd","Shell_SecondaryTrayWnd"}){
    IntPtr bar=IntPtr.Zero;
    while((bar=FindWindowEx(IntPtr.Zero,bar,cls,IntPtr.Zero))!=IntPtr.Zero){
     Rect r;if(!GetWindowRect(bar,out r))return false;
     r.Left-=area.Left;r.Right-=area.Left;r.Top-=area.Top;r.Bottom-=area.Top;
     if(RectInRegion(region,ref r))return false;
    }
   }
   return true;
  }finally{DeleteObject(region);}
 }
 public static bool Above(IntPtr a,IntPtr b){for(IntPtr h=GetWindow(a,2);h!=IntPtr.Zero;h=GetWindow(h,2))if(h==b)return true;return false;}
 [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint f,uint t,out UIntPtr r);
}
'@
$expected=(Resolve-Path -LiteralPath $ExecutablePath).Path
$app=Get-Process Penumbra -ErrorAction SilentlyContinue|Where-Object{$_.Path -eq $expected -and $_.MainWindowHandle -ne [IntPtr]::Zero}|Select-Object -First 1
if(!$app){throw 'Open the expected settings build before running this opt-in apply test.'}
$root=[Windows.Automation.AutomationElement]::FromHandle($app.MainWindowHandle)
function Control([string]$id){
    for($attempt=0;$attempt -lt 10;$attempt++){
        $el=$root.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$id))
        if($el){return $el};Start-Sleep -Milliseconds 100
    }
    throw "Missing control: $id"
}
function Range([string]$id){(Control $id).GetCurrentPattern([Windows.Automation.RangeValuePattern]::Pattern)}
function Toggle([string]$id){(Control $id).GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern)}
function Ini(){
    $values=@{}
    foreach($line in Get-Content -LiteralPath $configPath){$parts=$line.Split('=',2);if($parts.Count -eq 2){$values[$parts[0]]=$parts[1]}}
    $values
}
function Number($values,[string]$key){[double]::Parse($values[$key],[Globalization.CultureInfo]::InvariantCulture)}
function Apply-Radius([double]$value){
    $blurExpected=(Toggle 'BlurEnabledCheckBox').Current.ToggleState -eq [Windows.Automation.ToggleState]::On
    (Range 'BlurRadiusSlider').SetValue($value)
    (Control 'ApplyButton').GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
    $done=$false
    for($i=0;$i -lt 200;$i++){
        Start-Sleep -Milliseconds 100
        if(!(Control 'ApplyButton').Current.IsEnabled){continue}
        $text=(Control 'StatusText').Current.Name
        if($text -match '模糊附加半径 (\d+) px' -and [int]$Matches[1] -eq $value){$done=$true;break}
        if(!$blurExpected -and $text -match '^暗角已应用'){$done=$true;break}
        if($text -match '失败|不可用|未响应'){throw $text}
    }
    if(!$done){throw "Backend did not acknowledge radius $value. Last status: $text"}
    $saved=Ini
    if((Number $saved 'blurRadius') -ne $value){throw "Saved radius does not match $value"}
    if((Number $saved 'blurEnabled') -ne [int]$blurExpected){throw 'Saved blur toggle mismatch.'}
    $backend=@(Get-Process 'Nocturne.Renderer' -ErrorAction SilentlyContinue)
    if($backend.Count -ne 1 -or $backend[0].Path -ne (Join-Path ([IO.Path]::GetDirectoryName($expected)) 'Nocturne.Renderer.exe')){throw 'Unexpected renderer version or duplicate backend.'}
    [pscustomobject]@{AppliedRadius=$value;SavedRadius=(Number $saved 'blurRadius');Renderer=$backend[0].Id;Acknowledged=$true}|ConvertTo-Json -Compress|Write-Host
    if($MeasureDesktopFixture -and $blurExpected){
        Start-Sleep -Milliseconds 1000
        $desktop=[BlurApplyNative]::GetWindow([BlurApplyNative]::FindWindow('Nocturne.BlurSurface',[IntPtr]::Zero),4)
        if($desktop -eq [IntPtr]::Zero){$desktop=[BlurApplyNative]::FindWindow('Progman',[IntPtr]::Zero)}
        $fixture=[BlurApplyNative]::FindWindowEx($desktop,[IntPtr]::Zero,'Penumbra.DesktopBlurFixture',[IntPtr]::Zero)
        if($fixture -eq [IntPtr]::Zero){$fixture=[BlurApplyNative]::FindWindow('Penumbra.DesktopBlurFixture',[IntPtr]::Zero)}
        if($fixture -eq [IntPtr]::Zero){throw 'Actual desktop fixture unavailable.'}
        $values=@()
        for($sample=0;$sample -lt 6;$sample++){
            $pixelResult=[UIntPtr]::Zero
            if([BlurApplyNative]::SendMessageTimeout($fixture,32848,[IntPtr]::Zero,[IntPtr]::Zero,2,1000,[ref]$pixelResult) -eq [IntPtr]::Zero){throw 'Actual desktop sample timed out.'}
            $values+=$pixelResult.ToUInt64()/1000.0
            Start-Sleep -Milliseconds 100
        }
        $blur=[BlurApplyNative]::FindWindow('Nocturne.BlurSurface',[IntPtr]::Zero)
        if(![BlurApplyNative]::IsWindowVisible($blur) -or ![BlurApplyNative]::Above($blur,$fixture)){throw 'Production blur is hidden or test fixture is not behind it; refusing a false pixel verdict.'}
        if(![BlurApplyNative]::TaskbarExcluded($blur)){throw 'Production blur region still intersects a taskbar.'}
        $script:desktopContrasts[[int]$value]=($values|Measure-Object -Average).Average
        [pscustomobject]@{ActualDesktopRadius=$value;ContrastMean=($values|Measure-Object -Average).Average;ContrastMin=($values|Measure-Object -Minimum).Minimum;ContrastMax=($values|Measure-Object -Maximum).Maximum;Blur=$blur.ToInt64();BlurVisible=[BlurApplyNative]::IsWindowVisible($blur);BlurAboveFixture=[BlurApplyNative]::Above($blur,$fixture);Foreground=[BlurApplyNative]::GetForegroundWindow().ToInt64()}|ConvertTo-Json -Compress|Write-Host
    }
    if($CaptureProductionWindow){
        Start-Sleep -Milliseconds 900
        & dotnet run --project (Join-Path $PSScriptRoot 'BlurCaptureProbe/BlurCaptureProbe.csproj') -c Release --no-build -- 'Nocturne.BlurSurface'
        if($LASTEXITCODE){throw 'Production window capture unavailable; no pixel verdict.'}
    }
}
function Preview-Pixels(){
    $bounds=$root.Current.BoundingRectangle
    $title=(Control 'PreviewTitle').Current.BoundingRectangle
    $button=(Control 'CaptureButton').Current.BoundingRectangle
    $x=[int]($title.Left-$bounds.Left+24);$y=[int]($title.Bottom-$bounds.Top+40)
    $w=[int][Math]::Min(440,$bounds.Width-$x-80)
    $h=[int][Math]::Min(220,$button.Top-$bounds.Top-$y-32)
    if($w -lt 100 -or $h -lt 100){throw 'Preview is not visible; refusing a false visual pass.'}
    $bitmap=[Drawing.Bitmap]::new([int]$bounds.Width,[int]$bounds.Height)
    $g=[Drawing.Graphics]::FromImage($bitmap);$dc=$g.GetHdc()
    try{if(![BlurApplyNative]::PrintWindow($app.MainWindowHandle,$dc,2)){throw 'UI screenshot failed'}}
    finally{$g.ReleaseHdc($dc);$g.Dispose()}
    $crop=$bitmap.Clone([Drawing.Rectangle]::new($x,$y,$w,$h),[Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $bitmap.Dispose();$stream=[IO.MemoryStream]::new()
    try{$crop.Save($stream,[Drawing.Imaging.ImageFormat]::Bmp);return [Security.Cryptography.SHA256]::HashData($stream.ToArray())}
    finally{$crop.Dispose();$stream.Dispose()}
}
$configPath=Join-Path $env:LOCALAPPDATA 'Nocturne/settings.ini'
$beforeHash=(Get-FileHash -LiteralPath $configPath).Hash
$before=Ini
# Never silently commit or discard unsaved controls during a regression test.
foreach($entry in @(@('OpacitySlider','opacity',100),@('RadiusSlider','radius',100),@('FeatherSlider','feather',100),@('ShapeSlider','shape',100),@('CenterXBox','centerX',100),@('CenterYBox','centerY',100),@('BlurRadiusSlider','blurRadius',1))){
    if([Math]::Abs((Range $entry[0]).Current.Value/$entry[2]-(Number $before $entry[1])) -gt 0.000001){throw "Unsaved $($entry[0]); refusing to apply test values."}
}
foreach($entry in @(@('DarkenIconsCheckBox','darkenIcons'),@('FocusOnlyCheckBox','focusOnly'),@('BlurEnabledCheckBox','blurEnabled'))){
    if([int](Toggle $entry[0]).Current.ToggleState -ne (Number $before $entry[1])){throw "Unsaved $($entry[0]); refusing to apply test values."}
}
foreach($entry in @(@('TintCombo','tint'),@('MonitorCombo','primaryOnly'))){
    $selection=(Control $entry[0]).GetCurrentPattern([Windows.Automation.SelectionPattern]::Pattern).Current.GetSelection()[0]
    $names=if($entry[0] -eq 'TintCombo'){@('碳黑','墨蓝','暖黑')}else{@('所有屏幕 · 每屏独立暗角','仅主屏幕')}
    if($selection.Current.Name -ne $names[[int](Number $before $entry[1])]){throw "Unsaved or unreadable $($entry[0]); refusing to apply test values."}
}
if((Number $before 'blurEnabled') -ne 1 -and !$TemporarilyEnableBlur){throw 'Pass -TemporarilyEnableBlur to temporarily enable and restore blur.'}
$startupBefore=(Get-ItemProperty -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name Nocturne -ErrorAction SilentlyContinue).Nocturne
if([bool]$startupBefore -ne ((Toggle 'StartupToggle').Current.ToggleState -eq [Windows.Automation.ToggleState]::On)){throw 'Unsaved startup option; refusing to apply test values.'}
$scroll=(Control 'SettingsScrollViewer').GetCurrentPattern([Windows.Automation.ScrollPattern]::Pattern)
$scrollBefore=$scroll.Current.VerticalScrollPercent
$dpi=[BlurApplyNative]::SetThreadDpiAwarenessContext([IntPtr](-4))
$applied=$false
$script:desktopContrasts=@{}
try{
    if((Toggle 'BlurEnabledCheckBox').Current.ToggleState -ne [Windows.Automation.ToggleState]::On){(Toggle 'BlurEnabledCheckBox').Toggle()}
    if($scroll.Current.VerticallyScrollable){$scroll.SetScrollPercent(-1,0)}
    if(!$SkipPreviewPixels){
        $previewHashes=@()
        foreach($value in @(0,4,12,24,48)){
            (Range 'BlurRadiusSlider').SetValue($value);Start-Sleep -Milliseconds 180
            $previewHashes+=[Convert]::ToHexString([byte[]](Preview-Pixels))
        }
        if(@($previewHashes|Select-Object -Unique).Count -ne 5){throw 'The rendered UI preview did not change across all five radii.'}
        Write-Host 'RenderedPreviewFiveDistinctRadii=PASS'
    }
    foreach($value in @(0,12,48)){$applied=$true;Apply-Radius $value}
    if($MeasureDesktopFixture -and !($script:desktopContrasts[12] -lt $script:desktopContrasts[0]*0.9 -and $script:desktopContrasts[48] -lt $script:desktopContrasts[12]*0.75)){throw 'Actual desktop pixels did not respond sufficiently to increasing blur radius.'}
    if($LiveRadius){
        $backend=Get-Process 'Nocturne.Renderer'|Select-Object -First 1
        $controller=[BlurApplyNative]::FindWindow('Nocturne.Controller',[IntPtr]::Zero)
        $liveHash=(Get-FileHash -LiteralPath $configPath).Hash
        $contrasts=@()
        foreach($value in @(0,4,12,24,48)){
            (Range 'BlurRadiusSlider').SetValue($value)
            $actual=[UIntPtr]::Zero
            for($attempt=0;$attempt -lt 30;$attempt++){
                Start-Sleep -Milliseconds 100
                [BlurApplyNative]::SendMessageTimeout($controller,32774,[IntPtr]50,[IntPtr]::Zero,2,1000,[ref]$actual)|Out-Null
                if($actual.ToUInt64() -eq $value+1){break}
            }
            if($actual.ToUInt64() -ne $value+1){throw "Live UI radius $value was not applied to the renderer."}
            if($MeasureDesktopFixture){
                Start-Sleep -Milliseconds 300
                $fixture=[BlurApplyNative]::FindWindow('Penumbra.DesktopBlurFixture',[IntPtr]::Zero)
                $pixel=[UIntPtr]::Zero
                if([BlurApplyNative]::SendMessageTimeout($fixture,32848,[IntPtr]::Zero,[IntPtr]::Zero,2,1000,[ref]$pixel) -eq [IntPtr]::Zero){throw 'Live desktop sampling failed.'}
                $contrasts+=$pixel.ToUInt64()/1000.0
                [pscustomobject]@{LiveRadius=$value;ActualDesktopContrast=$contrasts[-1];NoApplyClick=$true}|ConvertTo-Json -Compress|Write-Host
            }
            if($backend.HasExited -or (Get-Process 'Nocturne.Renderer').Id -ne $backend.Id){throw 'Live radius adjustment restarted the renderer.'}
        }
        if($MeasureDesktopFixture -and !($contrasts[4] -lt $contrasts[0]*.5 -and $contrasts[2] -lt $contrasts[0]*.9)){throw 'Live UI radius did not change actual desktop pixels.'}
        if((Get-FileHash -LiteralPath $configPath).Hash -ne $liveHash){throw 'Live radius preview wrote settings.'}
    }
    [pscustomobject]@{Passed=$true;FiveRenderedPreviewStates=(!$SkipPreviewPixels);UiSaveBackendRoundTrip=$true;ActualDesktopRadiusVerified=[bool]$MeasureDesktopFixture;LiveUiRadiusVerified=[bool]$LiveRadius;OriginalRadius=(Number $before 'blurRadius')}|ConvertTo-Json -Compress
}
finally{
    $originalEnabled=if((Number $before 'blurEnabled') -eq 1){[Windows.Automation.ToggleState]::On}else{[Windows.Automation.ToggleState]::Off}
    if((Toggle 'BlurEnabledCheckBox').Current.ToggleState -ne $originalEnabled){(Toggle 'BlurEnabledCheckBox').Toggle()}
    if($applied){Apply-Radius (Number $before 'blurRadius')}
    else{(Range 'BlurRadiusSlider').SetValue((Number $before 'blurRadius'))}
    if($scrollBefore -ge 0 -and $scroll.Current.VerticallyScrollable){$scroll.SetScrollPercent(-1,$scrollBefore)}
    [BlurApplyNative]::SetThreadDpiAwarenessContext($dpi)|Out-Null
    if((Get-FileHash -LiteralPath $configPath).Hash -ne $beforeHash){throw 'Settings hash changed after restoring the original radius.'}
    $startupAfter=(Get-ItemProperty -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name Nocturne -ErrorAction SilentlyContinue).Nocturne
    if($startupBefore -ne $startupAfter){throw 'Startup registration changed unexpectedly.'}
}

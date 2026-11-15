param([Parameter(Mandatory)][string]$ExecutablePath,[Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,System.Drawing
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ResponsiveLayoutNative {
 [StructLayout(LayoutKind.Sequential)]public struct Rect{public int Left,Top,Right,Bottom;}
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int w,int ht,uint flags);
 [DllImport("user32.dll")]public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
 [DllImport("user32.dll")]public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
}
'@
$executable=(Resolve-Path -LiteralPath $ExecutablePath).Path
$output=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $output | Out-Null
$config=Join-Path $env:LOCALAPPDATA 'Nocturne/settings.ini'
$before=if(Test-Path -LiteralPath $config){(Get-FileHash -LiteralPath $config).Hash}else{''}
$originalBackends=@(Get-Process Nocturne.Renderer -ErrorAction SilentlyContinue | ForEach-Object {$_.Id})
$originalUis=@(Get-Process Penumbra -ErrorAction SilentlyContinue | ForEach-Object {$_.Id})
$app=$null
$context=[ResponsiveLayoutNative]::SetThreadDpiAwarenessContext([IntPtr](-4))
$results=[Collections.Generic.List[object]]::new()
try {
 $app=Start-Process -FilePath $executable -ArgumentList '--layout-test' -PassThru -WindowStyle Hidden
 $until=[DateTime]::UtcNow.AddSeconds(15)
 do {
  $app.Refresh()
  if($app.HasExited){throw 'Layout test app exited before loading.'}
  if($app.MainWindowHandle -ne [IntPtr]::Zero){break}
  Start-Sleep -Milliseconds 100
 } while([DateTime]::UtcNow -lt $until)
 $hwnd=$app.MainWindowHandle
 if($hwnd -eq [IntPtr]::Zero){throw 'Layout test app window did not appear.'}
 $root=[Windows.Automation.AutomationElement]::FromHandle($hwnd)
 function Control([string]$id) {
  $element=$root.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$id))
  if(!$element){throw "Missing layout control: $id"}
  return $element
 }
 $scrollControl=Control 'SettingsScrollViewer'
 $scroll=$scrollControl.GetCurrentPattern([Windows.Automation.ScrollPattern]::Pattern)
 function Metrics {
  $until=[DateTime]::UtcNow.AddSeconds(5)
  $last='';$stable=0
  do{
   Start-Sleep -Milliseconds 100
   $json=$scrollControl.Current.HelpText
   if($json.StartsWith('{') -and $json -eq $last){$stable++}else{$stable=0}
   $last=$json
   if($stable -ge 2){return $json|ConvertFrom-Json}
  }while([DateTime]::UtcNow -lt $until)
  throw "Layout did not settle: $last"
 }
 function Screenshot([string]$name) {
  $rect=[ResponsiveLayoutNative+Rect]::new()
  $null=[ResponsiveLayoutNative]::GetWindowRect($hwnd,[ref]$rect)
  $bitmap=[Drawing.Bitmap]::new($rect.Right-$rect.Left,$rect.Bottom-$rect.Top)
  $graphics=[Drawing.Graphics]::FromImage($bitmap);$dc=$graphics.GetHdc()
  try {
   if(![ResponsiveLayoutNative]::PrintWindow($hwnd,$dc,2)){throw 'Could not capture isolated layout window.'}
  }finally{$graphics.ReleaseHdc($dc);$graphics.Dispose()}
  try{$bitmap.Save((Join-Path $output "$name.png"),[Drawing.Imaging.ImageFormat]::Png)}finally{$bitmap.Dispose()}
 }
 function Check-Layout([string]$name,[bool]$expectedWide,[bool]$saveScreenshot) {
  if($scroll.Current.VerticallyScrollable){$scroll.SetScrollPercent(-1,0)}
  $metrics=Metrics
  if($metrics.Wide -ne $expectedWide){throw "Unexpected column layout for $name at $($metrics.Width): $($metrics.Wide)"}
  if([Math]::Abs($metrics.ContentWidth-($metrics.Width-64)) -gt 1){throw "Content leaves unnecessary outer side space in ${name}: $($metrics|ConvertTo-Json -Compress)"}
  if($metrics.PreviewWidth -gt $metrics.ContentWidth+1){throw "Preview overflows its content in $name"}
  if($expectedWide){
   $cap=[Math]::Clamp($metrics.ViewportHeight*.42,96,380)
   if($metrics.PreviewHeight -gt $cap+.75){throw "Unbounded wide preview height in $name"}
  }else{
   if([Math]::Abs($metrics.PreviewCardWidth-$metrics.ContentWidth) -gt 1){throw "Stacked preview does not fill content width in $name"}
   if([Math]::Abs($metrics.PreviewSlotWidth-$metrics.PreviewCardWidth) -gt 1){throw "Unused space remains beside stacked preview in $name"}
  }
  if([Math]::Abs($metrics.PreviewWidth/$metrics.PreviewHeight-$metrics.Aspect) -gt .01){throw "Preview aspect changed in $name"}
  if($expectedWide){
   if($metrics.ParameterRow -ne 0 -or !$metrics.ExperimentalInPreview -or $metrics.AboutRow -ne 1){throw "Wide section order incorrect in $name"}
   if([Math]::Abs($metrics.PreviewSlotWidth-$metrics.PreviewCardWidth) -gt 1){throw "Unused space remains beside the preview in $name"}
   if([Math]::Abs($metrics.PreviewColumnWidth-$metrics.PreviewCardWidth) -gt 1){throw "Preview column does not hug the image in $name"}
   if($metrics.ParameterColumnWidth -lt 339.5){throw "Parameters became too narrow in $name"}
   if([Math]::Abs($metrics.PreviewColumnWidth+$metrics.ColumnSpacing+$metrics.ParameterColumnWidth-$metrics.ContentWidth) -gt 1){throw "Parameters do not fill remaining window width in $name"}
  }elseif($metrics.ParameterRow -ne 1 -or $metrics.ExperimentalInPreview -or $metrics.ExperimentalRow -ne 2 -or $metrics.AboutRow -ne 3){
   throw "Narrow order must be preview, parameters, experiments, About in $name"
  }
  if($scroll.Current.HorizontallyScrollable){throw "Unexpected horizontal scrolling in $name"}
  $capture=(Control 'CaptureButton').Current.BoundingRectangle
  $indicator=(Control 'WallpaperEngineLabel').Current.BoundingRectangle
  if($metrics.CompactActions){
   if($indicator.Top -lt $capture.Bottom){throw "Compact preview actions overlap in $name"}
  }elseif([Math]::Abs(($capture.Top+$capture.Bottom)-($indicator.Top+$indicator.Bottom)) -gt 4){
   throw "Preview status not aligned to refresh button in $name"
  }
  if(!(Control 'ApplyButton').Current.IsEnabled -or !(Control 'CaptureButton').Current.IsEnabled){throw "Commands disabled after resizing $name"}
  if($saveScreenshot){Screenshot $name}
  if(!$expectedWide){
   if($scroll.Current.VerticallyScrollable){$scroll.SetScrollPercent(-1,100);Start-Sleep -Milliseconds 200}
   if((Control 'AboutAppName').Current.IsOffscreen){throw "About cannot be reached at bottom in $name"}
   if($scroll.Current.VerticallyScrollable){$scroll.SetScrollPercent(-1,0)}
  }
  $results.Add([pscustomobject]@{Case=$name;Passed=$true;Metrics=$metrics})
 }
 $dpi=[ResponsiveLayoutNative]::GetDpiForWindow($hwnd)/96.0
 $cases=@(
  @('wide',1000,740,$true,$true),
  @('square',900,900,$true,$true),
  @('square-compact',840,840,$true,$true),
  @('before-stack',780,880,$true,$false),
  @('stack',740,880,$false,$true),
  @('dead-band',780,880,$false,$false),
  @('back-to-columns',840,840,$true,$false),
  @('portrait',600,880,$false,$true),
  @('minimum-width',480,650,$false,$true),
  @('short-wide',1280,420,$true,$true),
  @('large',1600,1000,$true,$true)
 )
 foreach($case in $cases){
  $null=[ResponsiveLayoutNative]::SetWindowPos($hwnd,[IntPtr]::Zero,16,16,[int]($case[1]*$dpi),[int]($case[2]*$dpi),0x14)
  Check-Layout $case[0] $case[3] $case[4]
 }
 $windowPattern=$root.GetCurrentPattern([Windows.Automation.WindowPattern]::Pattern)
 $windowPattern.SetWindowVisualState([Windows.Automation.WindowVisualState]::Maximized)
 Check-Layout 'maximized' $true $true
 $windowPattern.SetWindowVisualState([Windows.Automation.WindowVisualState]::Normal)
 $null=[ResponsiveLayoutNative]::SetWindowPos($hwnd,[IntPtr]::Zero,16,16,[int](1000*$dpi),[int](740*$dpi),0x14)
 Check-Layout 'restored' $true $true
 $results | ConvertTo-Json -Depth 6 | Out-File -LiteralPath (Join-Path $output 'results.json') -Encoding utf8
 [pscustomobject]@{Passed=$true;Cases=$results.Count;DpiScale=$dpi;MaximizeRestore=$true;SyntheticPreview=$true;DesktopCaptured=$false;SettingsWritten=$false}|ConvertTo-Json -Compress
} finally {
 if($app -and !$app.HasExited){$null=$app.CloseMainWindow();if(!$app.WaitForExit(5000)){throw 'Isolated layout window failed to close.'}}
 $null=[ResponsiveLayoutNative]::SetThreadDpiAwarenessContext($context)
 $after=if(Test-Path -LiteralPath $config){(Get-FileHash -LiteralPath $config).Hash}else{''}
 if($before -ne $after){throw 'Layout tests changed saved settings.'}
 $remaining=@(Get-Process Nocturne.Renderer -ErrorAction SilentlyContinue|ForEach-Object{$_.Id})
 if(($originalBackends -join ',') -ne ($remaining -join ',')){throw 'Layout tests started or stopped a renderer.'}
 foreach($existing in $originalUis){if(!(Get-Process -Id $existing -ErrorAction SilentlyContinue)){throw 'An existing settings process exited during layout tests.'}}
}

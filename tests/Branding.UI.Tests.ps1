param([Parameter(Mandatory)][string]$ExecutablePath,[Parameter(Mandatory)][string]$ScreenshotDirectory)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,System.Drawing
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class BrandingUiNative {
 [StructLayout(LayoutKind.Sequential)] public struct Rect{public int Left,Top,Right,Bottom;}
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int w,int ht,uint f);
 [DllImport("user32.dll")]public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint f);
 [DllImport("user32.dll")]public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")]public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint f,uint timeout,out UIntPtr result);
}
'@
$expected=(Resolve-Path -LiteralPath $ExecutablePath).Path
$app=Get-Process Penumbra | Where-Object {$_.Path -eq $expected -and $_.MainWindowTitle -eq 'Penumbra'} | Select-Object -First 1
if(!$app){throw 'Open the expected branded settings window first.'}
$root=[Windows.Automation.AutomationElement]::FromHandle($app.MainWindowHandle)
function Control([string]$id){
 $element=$root.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$id))
 if(!$element){throw "Missing branded control: $id"};return $element
}
function Screenshot([string]$name){
 $r=[BrandingUiNative+Rect]::new();[BrandingUiNative]::GetWindowRect($app.MainWindowHandle,[ref]$r)|Out-Null
 $bitmap=[Drawing.Bitmap]::new($r.Right-$r.Left,$r.Bottom-$r.Top)
 $graphics=[Drawing.Graphics]::FromImage($bitmap);$dc=$graphics.GetHdc()
 try{if(![BrandingUiNative]::PrintWindow($app.MainWindowHandle,$dc,2)){throw 'Settings screenshot failed.'}}
 finally{$graphics.ReleaseHdc($dc);$graphics.Dispose()}
 try{$bitmap.Save((Join-Path $ScreenshotDirectory $name),[Drawing.Imaging.ImageFormat]::Png)}finally{$bitmap.Dispose()}
}
$configPath=Join-Path $env:LOCALAPPDATA 'Nocturne/settings.ini'
$hash=(Get-FileHash -LiteralPath $configPath).Hash
$context=[BrandingUiNative]::SetThreadDpiAwarenessContext([IntPtr](-4))
$original=[BrandingUiNative+Rect]::new();[BrandingUiNative]::GetWindowRect($app.MainWindowHandle,[ref]$original)|Out-Null
$scroll=(Control 'SettingsScrollViewer').GetCurrentPattern([Windows.Automation.ScrollPattern]::Pattern)
$scrollBefore=$scroll.Current.VerticalScrollPercent
$dpi=[BrandingUiNative]::GetDpiForWindow($app.MainWindowHandle)/96.0
New-Item -ItemType Directory -Force -Path $ScreenshotDirectory | Out-Null
try{
 foreach($id in @('TitleAppIcon','AboutAppIcon','AboutAppName','AboutVersionText','ProjectLink','DeveloperName','AuthorLabel','BilibiliLink','GitHubLink')){$null=Control $id}
 if((Control 'AboutAppName').Current.Name -ne 'Penumbra'){throw 'Incorrect app name.'}
 if((Control 'AboutVersionText').Current.Name -ne 'Beta 0.4'){throw 'Incorrect displayed version.'}
 if((Control 'BlurTitle').Current.Name -ne '模糊参数'){throw 'Incorrect blur section title.'}
 if($root.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,'BlurSupportText'))){throw 'Removed support description is still visible.'}
 if((Control 'DeveloperName').Current.Name -ne '冷茶Reicha'){throw 'Developer identity changed.'}
 if((Control 'AuthorLabel').Current.Name -ne '开发者'){throw 'Developer section label was not applied.'}
 if((Control 'ProjectLink').Current.Name -ne 'Penumbra 项目主页'){throw 'Project homepage button is missing.'}
 $visibleUrl=$root.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,'项目地址：github.com/Coldtea233/Penumbra'))
 if($visibleUrl){throw 'The raw project URL must not appear in the layout.'}
 $icon=[UIntPtr]::Zero
 [BrandingUiNative]::SendMessageTimeout($app.MainWindowHandle,0x7F,[IntPtr]2,[IntPtr]::Zero,2,1000,[ref]$icon)|Out-Null
 if($icon -eq [UIntPtr]::Zero){throw 'Actual WinUI window icon was not set.'}
 foreach($mode in @('wide','narrow')){
  $width=if($mode -eq 'wide'){1000}else{600}
  [BrandingUiNative]::SetWindowPos($app.MainWindowHandle,[IntPtr]::Zero,$original.Left,$original.Top,[int]($width*$dpi),[int](740*$dpi),0x14)|Out-Null
  Start-Sleep -Milliseconds 400
  if($scroll.Current.VerticallyScrollable){$scroll.SetScrollPercent(-1,100)}
  Start-Sleep -Milliseconds 250
  $name=(Control 'DeveloperName').Current.BoundingRectangle
  $bilibili=(Control 'BilibiliLink').Current.BoundingRectangle
  $github=(Control 'GitHubLink').Current.BoundingRectangle
  if($bilibili.Left -le $name.Right -or [Math]::Abs($bilibili.Top-$github.Top) -gt 2 -or $bilibili.Bottom -lt $name.Top -or $bilibili.Top -gt $name.Bottom){throw 'Social buttons must align beside the developer identity without overlap.'}
  $product=(Control 'AboutAppName').Current.BoundingRectangle
  $project=(Control 'ProjectLink').Current.BoundingRectangle
  if($project.Left -le $product.Right -or $project.Bottom -lt $product.Top){throw 'Project button must align beside the product identity.'}
  if([Math]::Abs($product.Left-$name.Left) -gt 2){throw 'Product and developer text columns must align.'}
  if($mode -eq 'narrow' -and (Control 'AboutAppName').Current.BoundingRectangle.Top -le (Control 'BlurDescription').Current.BoundingRectangle.Top){throw 'Narrow layout must keep About below all parameters.'}
  Screenshot "branding-$mode.png"
 }
 [pscustomobject]@{Passed=$true;Title='Penumbra';ActualWindowIcon=$true;ProjectHomepageButton=$true;RawUrlHidden=$true;AlignedIdentityRows=$true;WideAndNarrowScreenshots=$true;NarrowAboutAfterParameters=$true;SettingsWritten=$false}|ConvertTo-Json -Compress
}
finally{
 [BrandingUiNative]::SetWindowPos($app.MainWindowHandle,[IntPtr]::Zero,$original.Left,$original.Top,$original.Right-$original.Left,$original.Bottom-$original.Top,0x14)|Out-Null
 Start-Sleep -Milliseconds 200
 if($scrollBefore -ge 0 -and $scroll.Current.VerticallyScrollable){$scroll.SetScrollPercent(-1,$scrollBefore)}
 [BrandingUiNative]::SetThreadDpiAwarenessContext($context)|Out-Null
 if((Get-FileHash -LiteralPath $configPath).Hash -ne $hash){throw 'Saved settings changed during UI verification.'}
}

param([int]$Seconds=15,[switch]$OpenFromTray)
$ErrorActionPreference='Stop'
# Opt-in real-desktop measurement. Requires DesktopBlurFixture behind the
# production renderer and an uncovered sample at screen x32..256, y176.
# Does not change settings, wallpaper, or another application's window state.
Add-Type @'
using System;using System.Diagnostics;using System.Runtime.InteropServices;using System.Threading;using System.Threading.Tasks;using System.Collections.Generic;
public static class BlurStabilityNative {
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr FindWindow(string c,IntPtr n);
 [DllImport("user32.dll")] static extern IntPtr GetWindow(IntPtr h,uint c);
 [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint f,uint t,out UIntPtr r);
 static bool Above(IntPtr a,IntPtr b){for(IntPtr h=GetWindow(a,2);h!=IntPtr.Zero;h=GetWindow(h,2))if(h==b)return true;return false;}
 public sealed class Result { public int Samples,HiddenSamples,InvalidStackSamples,ClearSpikes;public double Min=double.MaxValue,Max,Mean,Baseline,ElapsedMs;public bool TrayTriggered; }
 public static Task<Result> Begin(int milliseconds,bool tray){return Task.Run(()=>{
   var result=new Result();var fixture=FindWindow("Penumbra.DesktopBlurFixture",IntPtr.Zero);var blur=FindWindow("Nocturne.BlurSurface",IntPtr.Zero);var controller=FindWindow("Nocturne.Controller",IntPtr.Zero);
   if(fixture==IntPtr.Zero||blur==IntPtr.Zero)throw new InvalidOperationException("Production blur and desktop fixture must already be running.");
   var watch=Stopwatch.StartNew();double sum=0;
   while(watch.ElapsedMilliseconds<milliseconds){
     if(tray&&!result.TrayTriggered&&watch.ElapsedMilliseconds>=1000){if(!PostMessage(controller,0x8001,IntPtr.Zero,new IntPtr(0x400)))throw new InvalidOperationException("Tray action failed.");result.TrayTriggered=true;}
     if(!IsWindowVisible(blur))result.HiddenSamples++;
     if(!Above(blur,fixture))result.InvalidStackSamples++;
     UIntPtr output;if(SendMessageTimeout(fixture,32848,IntPtr.Zero,IntPtr.Zero,2,1000,out output)==IntPtr.Zero)throw new InvalidOperationException("Screen sampling timed out.");
     double value=output.ToUInt64()/1000.0;if(result.Samples==0)result.Baseline=value;
     result.Samples++;sum+=value;result.Min=Math.Min(result.Min,value);result.Max=Math.Max(result.Max,value);
     if(value>result.Baseline*1.35+2)result.ClearSpikes++;
     Thread.Sleep(16);
   }
   result.Mean=sum/result.Samples;result.ElapsedMs=watch.Elapsed.TotalMilliseconds;return result;
 });}
}
'@
$configPath=Join-Path $env:LOCALAPPDATA 'Nocturne/settings.ini'
$hash=(Get-FileHash -LiteralPath $configPath).Hash
$backend=Get-Process 'Nocturne.Renderer'
$cpu=$backend.TotalProcessorTime.TotalMilliseconds
$timer=[Diagnostics.Stopwatch]::StartNew()
$task=[BlurStabilityNative]::Begin($Seconds*1000,[bool]$OpenFromTray)
$result=$task.GetAwaiter().GetResult()
$backend.Refresh()
$cpuDelta=$backend.TotalProcessorTime.TotalMilliseconds-$cpu
[pscustomobject]@{Mode=$(if($OpenFromTray){'TrayOpen'}else{'StaticDesktop'});Samples=$result.Samples;MinContrast=$result.Min;MaxContrast=$result.Max;MeanContrast=$result.Mean;ClearSpikes=$result.ClearSpikes;HiddenSamples=$result.HiddenSamples;InvalidStackSamples=$result.InvalidStackSamples;TrayTriggered=$result.TrayTriggered;ElapsedMs=$timer.Elapsed.TotalMilliseconds;CpuMs=$cpuDelta;OneCoreCpuPercent=100*$cpuDelta/$timer.Elapsed.TotalMilliseconds;WorkingSetMiB=$backend.WorkingSet64/1MB;PrivateMiB=$backend.PrivateMemorySize64/1MB;DwmGpuCostExcluded=$true}|ConvertTo-Json -Compress
if((Get-FileHash -LiteralPath $configPath).Hash -ne $hash){throw 'Saved settings changed during stability measurement.'}
if($result.Baseline -gt 60){throw 'Initial test pattern is not sufficiently blurred; refusing an all-clear false pass.'}
if($result.HiddenSamples -or $result.InvalidStackSamples -or $result.ClearSpikes){throw 'Production desktop stability check failed; see measured counts.'}
if($OpenFromTray -and !$result.TrayTriggered){throw 'Tray action was not exercised.'}

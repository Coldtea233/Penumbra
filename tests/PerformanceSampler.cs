using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

public static class PenumbraPerformance
{
    [DllImport("pdh.dll", CharSet=CharSet.Unicode)] static extern uint PdhOpenQuery(string source,UIntPtr data,out IntPtr query);
    [DllImport("pdh.dll", CharSet=CharSet.Unicode)] static extern uint PdhAddEnglishCounter(IntPtr query,string path,UIntPtr data,out IntPtr counter);
    [DllImport("pdh.dll")] static extern uint PdhCollectQueryData(IntPtr query);
    [DllImport("pdh.dll", CharSet=CharSet.Unicode)] static extern uint PdhGetFormattedCounterArray(IntPtr counter,uint format,ref uint bytes,ref uint count,IntPtr data);
    [DllImport("pdh.dll")] static extern uint PdhCloseQuery(IntPtr query);
    [DllImport("kernel32.dll")] static extern bool GetSystemTimes(out long idle,out long kernel,out long user);
    delegate bool WindowCallback(IntPtr h,IntPtr p);
    [DllImport("user32.dll")] static extern bool EnumWindows(WindowCallback c,IntPtr p);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h,StringBuilder n,int count);
    static IntPtr FindController(int pid) {
        IntPtr result=IntPtr.Zero;EnumWindows((h,p)=>{uint owner;GetWindowThreadProcessId(h,out owner);if(owner==pid){var name=new StringBuilder(128);GetClassName(h,name,name.Capacity);if(name.ToString()=="Penumbra.Performance.Controller"){result=h;return false;}}return true;},IntPtr.Zero);return result;
    }
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window,out uint pid);
    [DllImport("user32.dll")] static extern IntPtr SendMessageTimeout(IntPtr h,uint message,IntPtr w,IntPtr l,uint flags,uint timeout,out UIntPtr result);
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [StructLayout(LayoutKind.Sequential)] struct CounterItem {public IntPtr Name;public uint Status;public double Value;}
    static Dictionary<string,double> Read(IntPtr counter)
    {
        var result=new Dictionary<string,double>();if(counter==IntPtr.Zero)return result;
        uint bytes=0,count=0;uint status=PdhGetFormattedCounterArray(counter,0x8200,ref bytes,ref count,IntPtr.Zero);
        if(status!=0x800007D2||bytes==0)return result;
        IntPtr data=Marshal.AllocHGlobal((int)bytes);
        try {
            status=PdhGetFormattedCounterArray(counter,0x8200,ref bytes,ref count,data);
            if(status!=0)return result;
            int size=Marshal.SizeOf(typeof(CounterItem));
            for(int i=0;i<count;i++) {
                var item=(CounterItem)Marshal.PtrToStructure(IntPtr.Add(data,i*size),typeof(CounterItem));
                if(item.Status<=1&&!double.IsNaN(item.Value)&&item.Value>=0)result[Marshal.PtrToStringUni(item.Name)]=item.Value;
            }
            return result;
        } finally {Marshal.FreeHGlobal(data);}
    }
    static readonly Regex Pid=new Regex(@"^pid_(\d+)_",RegexOptions.Compiled);
    static int InstancePid(string name){var match=Pid.Match(name);return match.Success?int.Parse(match.Groups[1].Value):-1;}
    static double? Busy(Dictionary<string,double> values,HashSet<int> pids)
    {
        if(values.Count==0)return null;
        // Sum processes on the SAME physical GPU engine, then take the busiest
        // engine. Never add 3D/copy/video or both adapters into a fake >100%.
        var engines=new Dictionary<string,double>();
        foreach(var item in values) {
            int pid=InstancePid(item.Key);if(pid<0||(pids!=null&&!pids.Contains(pid)))continue;
            string key=Pid.Replace(item.Key,"");
            double prior;engines.TryGetValue(key,out prior);engines[key]=prior+item.Value;
        }
        return engines.Count==0?0:Math.Min(100,engines.Values.Max());
    }
    static double? GpuBytes(Dictionary<string,double> values,HashSet<int> pids)
    {
        if(values.Count==0)return null;
        return values.Where(v=>pids.Contains(InstancePid(v.Key))).Sum(v=>v.Value)/1048576;
    }
    public sealed class ProcessSample {
        public double? CpuPercent,WorkingSetMiB,PrivateMiB,GpuBusyPercent,DedicatedGpuMiB,SharedGpuMiB;
    }
    public sealed class Sample {
        public string Mode;public int Round,Index,BackendState;public double ElapsedSeconds,SystemCpuPercent;
        public double? SystemGpuBusyPercent;public bool ForegroundStable;
        public ProcessSample Penumbra,Dwm,WallpaperEngine;
    }
    static ProcessSample ProcessValues(HashSet<int> pids,Dictionary<int,double> previous,double elapsed,
        Dictionary<string,double> gpu,Dictionary<string,double> dedicated,Dictionary<string,double> shared)
    {
        var result=new ProcessSample();double cpu=0,working=0,privateBytes=0;bool ok=true;
        foreach(int pid in pids) {
            try {using(var p=Process.GetProcessById(pid)) {
                double current=p.TotalProcessorTime.TotalMilliseconds,old;
                if(previous.TryGetValue(pid,out old))cpu+=Math.Max(0,current-old);
                previous[pid]=current;working+=p.WorkingSet64;privateBytes+=p.PrivateMemorySize64;
            }} catch {ok=false;}
        }
        if(ok){result.CpuPercent=cpu/elapsed/10/Environment.ProcessorCount;result.WorkingSetMiB=working/1048576;result.PrivateMiB=privateBytes/1048576;}
        result.GpuBusyPercent=Busy(gpu,pids);result.DedicatedGpuMiB=GpuBytes(dedicated,pids);result.SharedGpuMiB=GpuBytes(shared,pids);
        return result;
    }
    static HashSet<int> Ids(string name){return new HashSet<int>(Process.GetProcessesByName(name).Select(p=>{int id=p.Id;p.Dispose();return id;}));}
    public static Task<Sample[]> Measure(string mode,int round,int seconds,int backendPid)
    {
        return Task.Run(()=>{
            var all=new List<Sample>();IntPtr query=IntPtr.Zero,gpu=IntPtr.Zero,dedicated=IntPtr.Zero,shared=IntPtr.Zero;
            var dwm=Ids("dwm");var wallpaper=Ids("wallpaper64");wallpaper.UnionWith(Ids("wallpaper32"));
            var backend=new HashSet<int>();if(backendPid>0)backend.Add(backendPid);
            var prior=new Dictionary<int,double>();
            ProcessValues(backend,prior,1,new Dictionary<string,double>(),new Dictionary<string,double>(),new Dictionary<string,double>());
            ProcessValues(dwm,prior,1,new Dictionary<string,double>(),new Dictionary<string,double>(),new Dictionary<string,double>());
            ProcessValues(wallpaper,prior,1,new Dictionary<string,double>(),new Dictionary<string,double>(),new Dictionary<string,double>());
            long idleBefore,kernelBefore,userBefore;GetSystemTimes(out idleBefore,out kernelBefore,out userBefore);
            var foreground=GetForegroundWindow();var watch=Stopwatch.StartNew();double before=0;
            try {
                if(PdhOpenQuery(null,UIntPtr.Zero,out query)==0) {
                    PdhAddEnglishCounter(query,@"\GPU Engine(*)\Utilization Percentage",UIntPtr.Zero,out gpu);
                    PdhAddEnglishCounter(query,@"\GPU Process Memory(*)\Dedicated Usage",UIntPtr.Zero,out dedicated);
                    PdhAddEnglishCounter(query,@"\GPU Process Memory(*)\Shared Usage",UIntPtr.Zero,out shared);
                    PdhCollectQueryData(query);
                }
                for(int i=0;i<seconds;i++) {
                    Thread.Sleep(1000);double now=watch.Elapsed.TotalSeconds,elapsed=now-before;before=now;
                    if(query!=IntPtr.Zero)PdhCollectQueryData(query);
                    var g=Read(gpu);var d=Read(dedicated);var s=Read(shared);
                    long idle,kernel,user;GetSystemTimes(out idle,out kernel,out user);
                    long total=(kernel-kernelBefore)+(user-userBefore);
                    var sample=new Sample{Mode=mode,Round=round,Index=i,ElapsedSeconds=elapsed,
                        SystemCpuPercent=total>0?100.0*(total-(idle-idleBefore))/total:0,SystemGpuBusyPercent=Busy(g,null),ForegroundStable=GetForegroundWindow()==foreground,
                        Penumbra=ProcessValues(backend,prior,elapsed,g,d,s),Dwm=ProcessValues(dwm,prior,elapsed,g,d,s),WallpaperEngine=ProcessValues(wallpaper,prior,elapsed,g,d,s)};
                    idleBefore=idle;kernelBefore=kernel;userBefore=user;
                    if(backendPid>0) {
                        var window=FindController(backendPid);uint pid=0;UIntPtr state;
                        GetWindowThreadProcessId(window,out pid);
                        sample.BackendState=pid==backendPid&&SendMessageTimeout(window,0x8051,IntPtr.Zero,IntPtr.Zero,2,1000,out state)!=IntPtr.Zero?(int)state.ToUInt64():0;
                    }
                    all.Add(sample);
                }
            } finally {if(query!=IntPtr.Zero)PdhCloseQuery(query);}
            return all.ToArray();
        });
    }
}

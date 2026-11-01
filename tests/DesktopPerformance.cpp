// Opt-in actual-desktop benchmark. Stop the installed renderer first. Does not
// change settings, foreground windows, startup entries, or minimize user apps.
#define wWinMain PenumbraApplicationMain
#include "../src/Nocturne.Renderer/main.cpp"
#undef wWinMain
#include <cstdlib>

double CpuMs() {
    FILETIME created{},exited{},kernel{},user{};
    GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user);
    ULARGE_INTEGER k{},u{}; k.LowPart=kernel.dwLowDateTime; k.HighPart=kernel.dwHighDateTime;
    u.LowPart=user.dwLowDateTime; u.HighPart=user.dwHighDateTime;
    return (k.QuadPart+u.QuadPart)/10000.0;
}
void PumpFor(double duration) {
    double end=ClockMs()+duration;
    while(ClockMs()<end) if(!DispatchWork((DWORD)std::max(1.0,std::ceil(end-ClockMs())))) std::abort();
}
void Result(const char* stage,double elapsed,double cpu) {
    PROCESS_MEMORY_COUNTERS_EX memory{};
    GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&memory,sizeof(memory));
    DWORD handles=0; GetProcessHandleCount(GetCurrentProcess(),&handles);
    std::printf("{\"Stage\":\"%s\",\"ElapsedMs\":%.3f,\"CpuMs\":%.3f,\"SingleCoreCpuPercent\":%.4f,\"MachineCpuPercent\":%.4f,\"LogicalProcessors\":%lu,\"WorkingSetMiB\":%.3f,\"PrivateMiB\":%.3f,\"Gdi\":%lu,\"Handles\":%lu,\"AnimationTimer\":%s}\n",
        stage,elapsed,cpu,cpu*100/elapsed,cpu*100/elapsed/GetActiveProcessorCount(ALL_PROCESSOR_GROUPS),
        GetActiveProcessorCount(ALL_PROCESSOR_GROUPS),memory.WorkingSetSize/1048576.0,memory.PrivateUsage/1048576.0,
        GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),handles,animationTimerArmed?"true":"false");
    std::fflush(stdout);
}
int main() {
    instance=GetModuleHandleW(nullptr);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    InitializePaths(); Load(); params.focusOnly=true;
    if(!LocateDesktop()||(!params.darkenIcons&&!ReadIconMask(iconMask))) return 1;
    stopEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr); reloadEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    WNDCLASSW wc{}; wc.hInstance=instance; wc.lpfnWndProc=SurfaceProc; wc.lpszClassName=L"Nocturne.Surface"; RegisterClassW(&wc);
    wc.lpfnWndProc=ControllerProc; wc.lpszClassName=L"Nocturne.Controller"; RegisterClassW(&wc);
    controller=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"Penumbra benchmark",WS_POPUP,0,0,0,0,nullptr,nullptr,instance,nullptr);
    std::vector<RECT> monitors; EnumDisplayMonitors(nullptr,nullptr,CollectMonitor,reinterpret_cast<LPARAM>(&monitors));
    for(const RECT& r:monitors) { Surface s; s.rect=r; surfaces.push_back(std::move(s)); }
    double start=ClockMs(),cpu=CpuMs();
    for(auto& s:surfaces) if(!Render(s,0)) return 2;
    Result("PrepareOnce",ClockMs()-start,CpuMs()-cpu);
    start=ClockMs();cpu=CpuMs();PumpFor(10000);Result("HiddenIdle",ClockMs()-start,CpuMs()-cpu);
    const double settleMs=focus_animation::Transition::DurationMs+40;
    SetVisibility(true);PumpFor(settleMs);
    start=ClockMs();cpu=CpuMs();PumpFor(10000);Result("VisibleIdle",ClockMs()-start,CpuMs()-cpu);
    SetVisibility(false);PumpFor(settleMs);
    traceAnimation=true;traceFileOutput=false;frameTrace.clear();
    start=ClockMs();cpu=CpuMs();
    for(int i=0;i<12;i++) { SetVisibility(true);PumpFor(settleMs);SetVisibility(false);PumpFor(settleMs); }
    Result("24FullTransitions",ClockMs()-start,CpuMs()-cpu);
    std::vector<double> gaps,costs;
    for(size_t i=0;i<frameTrace.size();i++) {
        costs.push_back(frameTrace[i].submitMs);
        if(i&&frameTrace[i].target==frameTrace[i-1].target) gaps.push_back(frameTrace[i].tick-frameTrace[i-1].tick);
    }
    std::sort(gaps.begin(),gaps.end());std::sort(costs.begin(),costs.end());
    auto percentile=[](const std::vector<double>& v,double p){return v.empty()?0:v[(size_t)std::floor((v.size()-1)*p)];};
    double sum=0;for(double cost:costs) sum+=cost;
    std::printf("{\"Stage\":\"ActualDesktopSubmissions\",\"Width\":%d,\"Height\":%d,\"FrameIntervalTargetMs\":%.4f,\"Frames\":%llu,\"MeanSubmitMs\":%.4f,\"P95SubmitMs\":%.4f,\"MaxSubmitMs\":%.4f,\"MedianIntervalMs\":%.4f,\"P95IntervalMs\":%.4f,\"MaxIntervalMs\":%.4f,\"IntervalsOver25Ms\":%llu,\"NotPhysicalDisplayFps\":true}\n",
        GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN),frameIntervalMs,(unsigned long long)frameTrace.size(),sum/std::max<size_t>(1,costs.size()),
        percentile(costs,.95),percentile(costs,1),percentile(gaps,.5),percentile(gaps,.95),percentile(gaps,1),
        (unsigned long long)std::count_if(gaps.begin(),gaps.end(),[](double g){return g>25;}));
    traceAnimation=false;frameTrace.clear();Clear();DestroyWindow(controller);
    CloseHandle(stopEvent);CloseHandle(reloadEvent);
    return 0;
}

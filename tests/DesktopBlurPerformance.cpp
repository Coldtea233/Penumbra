// Opt-in real-desktop blur benchmark. Coexists with the existing renderer;
// does not write its settings/status, steal focus, or minimize applications.
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
    while(ClockMs()<end) if(!DispatchWork((DWORD)std::max(1.,std::ceil(end-ClockMs())))) std::abort();
}
void Result(const char* name,double elapsed,double cpu) {
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb=sizeof(memory);
    GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));
    DWORD handles=0; GetProcessHandleCount(GetCurrentProcess(),&handles);
    std::printf("{\"Stage\":\"%s\",\"ElapsedMs\":%.3f,\"CpuMs\":%.3f,\"OneCoreCpuPercent\":%.5f,\"MachineCpuPercent\":%.5f,\"WorkingSetMiB\":%.3f,\"PrivateMiB\":%.3f,\"Gdi\":%lu,\"Handles\":%lu,\"BlurTimer\":%s,\"VignetteTimer\":%s,\"DwmCostExcluded\":true}\n",
        name,elapsed,cpu,cpu*100/elapsed,cpu*100/elapsed/GetActiveProcessorCount(ALL_PROCESSOR_GROUPS),
        memory.WorkingSetSize/1048576.,memory.PrivateUsage/1048576.,GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),handles,
        blurTimerArmed?"true":"false",animationTimerArmed?"true":"false");
    std::fflush(stdout);
}
int main() {
    HWND originalForeground=GetForegroundWindow();
    instance=GetModuleHandleW(nullptr);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    params=Params{}; params.blurEnabled=true; params.opacity=0; params.primary=true;
    if(!LocateDesktop()) return 1;
    stopEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr); reloadEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    WNDCLASSW wc{}; wc.hInstance=instance; wc.lpfnWndProc=SurfaceProc; wc.lpszClassName=L"Nocturne.BlurSurface"; RegisterClassW(&wc);
    wc.lpfnWndProc=ControllerProc; wc.lpszClassName=L"Nocturne.Controller"; RegisterClassW(&wc);
    controller=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"Penumbra blur benchmark",WS_POPUP,0,0,0,0,nullptr,nullptr,instance,nullptr);
    std::vector<RECT> monitors; EnumDisplayMonitors(nullptr,nullptr,CollectMonitor,reinterpret_cast<LPARAM>(&monitors));
    blurEngine=std::make_unique<desktop_blur::Engine>();
    for(const auto& rect:monitors) {
        BlurSurface s; s.rect=rect;
        s.composition.hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,
            L"Nocturne.BlurSurface",L"Penumbra blur benchmark surface",WS_POPUP,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,nullptr);
        SetLayeredWindowAttributes(s.composition.hwnd,0,255,LWA_ALPHA);
        HRESULT hr=blurEngine->Attach(s.composition.hwnd,rect.right-rect.left,rect.bottom-rect.top,12,s.composition);
        if(FAILED(hr)) { std::printf("Attach failure %08lX %ls\n",static_cast<unsigned long>(hr),blurEngine->stage); s.composition.Reset(); Clear(); blurEngine.reset(); return 2; }
        blurSurfaces.push_back(std::move(s));
    }
    PumpFor(500);
    double start=ClockMs(),cpu=CpuMs(); PumpFor(8000); Result("HiddenIdle",ClockMs()-start,CpuMs()-cpu);
    const double settle=focus_animation::Transition::DurationMs+60;
    SetBlurVisibility(true); PumpFor(settle);
    bool visible=!blurSurfaces.empty()&&IsWindowVisible(blurSurfaces[0].composition.hwnd)&&!blurTimerArmed;
    POINT point{GetSystemMetrics(SM_CXSCREEN)-20,GetSystemMetrics(SM_CYSCREEN)-100};
    HWND hit=WindowFromPoint(point); bool clickThrough=true;
    for(const auto& s:blurSurfaces) if(hit==s.composition.hwnd) clickThrough=false;
    start=ClockMs(); cpu=CpuMs(); PumpFor(12000); Result("VisibleIdle",ClockMs()-start,CpuMs()-cpu);
    bool zero=SetLiveBlurRadius(0); PumpFor(250);
    start=ClockMs(); cpu=CpuMs(); PumpFor(5000); Result("ZeroRadiusIdle",ClockMs()-start,CpuMs()-cpu);
    zero=zero&&SetLiveBlurRadius(12);
    start=ClockMs();cpu=CpuMs();
    for(int i=0;i<3;i++) { SetBlurVisibility(false);PumpFor(settle);SetBlurVisibility(true);PumpFor(settle); }
    Result("SixTransitions",ClockMs()-start,CpuMs()-cpu);
    SetBlurVisibility(false); PumpFor(90);
    double before=blurTransition.Value(ClockMs()); SetBlurVisibility(true);
    bool continuous=std::abs(blurTransition.Value(ClockMs())-before)<.04;
    PumpFor(settle); SetBlurVisibility(false); PumpFor(settle);
    bool hidden=!blurTimerArmed&&!IsWindowVisible(blurSurfaces[0].composition.hwnd);
    Clear(); blurEngine.reset(); DestroyWindow(controller); CloseHandle(stopEvent); CloseHandle(reloadEvent);
    bool foreground=GetForegroundWindow()==originalForeground;
    std::printf("{\"VisibleAtRest\":%s,\"ClickThrough\":%s,\"ReversalContinuous\":%s,\"HiddenAtRest\":%s,\"ForegroundPreserved\":%s,\"ScreenWidth\":%d,\"ScreenHeight\":%d}\n",
        visible?"true":"false",clickThrough?"true":"false",continuous?"true":"false",hidden?"true":"false",foreground?"true":"false",GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN));
    return visible&&clickThrough&&continuous&&hidden&&foreground&&zero?0:3;
}

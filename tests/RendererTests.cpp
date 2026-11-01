// Exercise the production renderer off-screen, without its singleton, hooks,
// user settings, tray, login entry, or currently running desktop overlay.
#define wWinMain PenumbraApplicationMain
#include "../src/Nocturne.Renderer/main.cpp"
#undef wWinMain
#include <thread>
#include <atomic>
#include <cstdlib>

static int Check(bool condition,const char* message) {
    if(condition) return 0;
    std::fprintf(stderr,"FAIL: %s\n",message); return 1;
}
static void Pump(unsigned duration) {
    ULONGLONG end=GetTickCount64()+duration;
    do {
        if(!DispatchWork(2)) std::abort();
    } while(GetTickCount64()<end);
}
constexpr unsigned FullTransitionWaitMs=(unsigned)focus_animation::Transition::DurationMs+50;
static SIZE_T PrivateBytes() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),sizeof(counters));
    return counters.PrivateUsage;
}
int main() {
    if(SelfTest()) return Check(false,"math/self-test");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetPriorityClass(GetCurrentProcess(),NORMAL_PRIORITY_CLASS);
    stopEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    reloadEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    instance=GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.hInstance=instance; wc.lpfnWndProc=DefWindowProcW; wc.lpszClassName=L"Penumbra.TestDesktop";
    RegisterClassW(&wc);
    desktop=CreateWindowExW(0,wc.lpszClassName,L"isolated test desktop",WS_POPUP,-20000,-20000,512,288,nullptr,nullptr,instance,nullptr);
    wc.lpfnWndProc=SurfaceProc; wc.lpszClassName=L"Nocturne.Surface"; RegisterClassW(&wc);
    wc.lpfnWndProc=ControllerProc; wc.lpszClassName=L"Nocturne.Controller"; RegisterClassW(&wc);
    controller=CreateWindowExW(0,wc.lpszClassName,L"isolated test controller",WS_POPUP,0,0,0,0,nullptr,nullptr,instance,nullptr);
    if(Check(desktop&&controller,"test windows")) return 1;
    if(Check(ClassifyForeground(nullptr)==ForegroundKind::Transient,"null foreground is transient")||
       Check(ClassifyForeground(desktop)==ForegroundKind::Desktop,"desktop classification")||
       Check(ClassifyForeground(controller)==ForegroundKind::Transient,"controller classification")) return 1;
    HWND taskbar=FindWindowW(L"Shell_TrayWnd",nullptr);
    if(taskbar&&Check(ClassifyForeground(taskbar)==ForegroundKind::Transient,"taskbar holds state")) return 1;
    HWND shell=GetShellWindow();
    if(shell&&Check(ClassifyForeground(shell)==ForegroundKind::Desktop,"actual Explorer desktop classification")) return 1;
    wc.lpfnWndProc=DefWindowProcW; wc.lpszClassName=L"CabinetWClass"; RegisterClassW(&wc);
    HWND explorer=CreateWindowExW(0,wc.lpszClassName,L"test file manager",WS_POPUP,-21000,-21000,1,1,nullptr,nullptr,instance,nullptr);
    if(Check(ClassifyForeground(explorer)==ForegroundKind::Application,"file manager is an application")) return 1;
    if(Check(!IsApplicationWindow(explorer),"hidden application is not counted")) return 1;
    SetWindowPos(explorer,nullptr,16,16,1,1,SWP_NOACTIVATE|SWP_NOZORDER);
    ShowWindow(explorer,SW_SHOWNOACTIVATE);
    if(Check(IsApplicationWindow(explorer)&&HasApplicationWindow(),"visible non-foreground application is counted")) return 1;
    ShowWindow(explorer,SW_SHOWMINNOACTIVE);
    if(Check(!IsApplicationWindow(explorer),"minimized application is not counted")) return 1;
    ShowWindow(explorer,SW_HIDE);
    if(Check(!IsApplicationWindow(controller)&&!IsApplicationWindow(desktop),"desktop and utility windows are not applications")) return 1;
    params=Params{}; params.focusOnly=true;
    iconMask.rect={-20000,-20000,-19488,-19712}; iconMask.width=512; iconMask.height=288;
    iconMask.bits.assign((512*288+7)/8,0);
    size_t protectedPixel=10*512+10; iconMask.bits[protectedPixel/8]|=1u<<(protectedPixel%8);
    Surface surface; surface.rect=iconMask.rect; surfaces.push_back(std::move(surface));
    transition.Reset(false);
    BOOL windowsAnimations=TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&windowsAnimations,0);
    DWORD baseGdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    DWORD baseHandles=0; GetProcessHandleCount(GetCurrentProcess(),&baseHandles);
    SetVisibility(true);
    if(Check(animationTimerArmed&&surfaces[0].uploaded&&IsWindow(surfaces[0].hwnd),"short-lived timer and uploaded surface")) return 1;
    if(Check(RenderAnimation(surfaces[0],.7)&&surfaces[0].opacity==179,"constant alpha submission")) return 1;
    {
    Bitmap output; output.Create(512,288); FillVignette(output); ProtectIcons(output,iconMask.rect);
    if(Check(output.pixels[protectedPixel]==0,"icon protection after scaling")||
       Check((output.pixels[0]>>24)>0,"vignette alpha")||
       Check(output.pixels[144*512+256]==0,"clear vignette center")) return 1;
    for(int tint=0;tint<3;tint++) {
        params.tint=tint; FillVignette(output); ProtectIcons(output,iconMask.rect);
        auto& frame=output;
        for(int y=0;y<frame.height;y+=7) for(int x=0;x<frame.width;x+=7) {
            uint32_t pixel=frame.pixels[y*frame.width+x], a=pixel>>24;
            if(Check((pixel&255)<=a&&((pixel>>8)&255)<=a&&((pixel>>16)&255)<=a,"premultiplied color")) return 1;
        }
    }
    params.tint=0;
    }
    Pump(FullTransitionWaitMs);
    // Neither inspection nor production retains a CPU bitmap at rest.
    DWORD inspectionGdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    if(Check(!animationTimerArmed&&surfaces[0].uploaded&&IsWindow(surfaces[0].hwnd),"visible rest has no animation timer")) return 1;
    if(Check(inspectionGdi==baseGdi,"all CPU DC/bitmaps released")) return 1;
    SetVisibility(false); Pump(120);
    double before=transition.Value(ClockMs());
    SetVisibility(true);
    double after=transition.Value(ClockMs());
    if(Check(std::abs(before-after)<.025,"mid-flight reversal is continuous")) return 1;
    if(Check(transition.Velocity(ClockMs())>0,"reversal immediately moves towards new target")) return 1;
    Pump(FullTransitionWaitMs); SetVisibility(false); Pump(FullTransitionWaitMs);
    HWND cached=surfaces[0].hwnd;
    if(Check(!animationTimerArmed&&surfaces[0].uploaded&&!IsWindowVisible(cached),"hidden keeps only OS-owned cached surface")) return 1;
    if(Check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==inspectionGdi,"hidden has no production GDI buffers")) return 1;
    SetVisibility(true);
    if(Check(animationTimerArmed&&surfaces[0].hwnd==cached&&surfaces[0].opacity<5,"fully hidden fade reuses cache and starts transparent")) return 1;
    Pump(FullTransitionWaitMs); SetVisibility(false,false);
    if(Check(!animationTimerArmed&&!IsWindowVisible(surfaces[0].hwnd),"explicit immediate hide")) return 1;
    params.opacity=0;SetVisibility(true);
    if(Check(!animationTimerArmed&&!IsWindowVisible(surfaces[0].hwnd),"zero opacity never shows surface")) return 1;
    params.opacity=.68;
    // Check the actual production frame dispatcher and submitted alpha, not
    // only the ideal easing formula. Keep diagnostics in test memory only.
    for(bool showing:{true,false}) {
        traceAnimation=true;traceFileOutput=false;frameTrace.clear();
        SetVisibility(showing);
        Pump(100);
        if(Check(!frameTrace.empty(),"timed fade produces frames")) return 1;
        const auto first=frameTrace.front(),sample=frameTrace.back();
        double elapsed=sample.tick-first.tick;
        BYTE expectedAlpha=(BYTE)std::lround(255*focus_animation::Opacity(sample.value));
        if(Check(surfaces[0].opacity==expectedAlpha,"easing reaches native submitted alpha")) return 1;
        Pump(FullTransitionWaitMs);
        if(Check(!animationTimerArmed&&transition.Value(ClockMs())==(showing?1:0)&&transition.Velocity(ClockMs())==0,"timed fade settles and stops timer")) return 1;
        double start=frameTrace.front().tick;
        double previousProgress=0;
        double values[3]{};
        for(const auto& frame:frameTrace) {
            double progress=showing?frame.value:1-frame.value;
            if(Check(progress+1e-9>=previousProgress&&progress>=0&&progress<=1,"submitted easing is monotonic")) return 1;
            previousProgress=progress;
        }
        for(int step=0;step<3;step++) {
            const FrameTrace* closest=&frameTrace.front();double target=(step+1)*100.0;
            for(const auto& frame:frameTrace) if(std::abs(frame.tick-start-target)<std::abs(closest->tick-start-target)) closest=&frame;
            values[step]=showing?closest->value:1-closest->value;
        }
        if(Check(values[0]>.50&&values[0]<.75&&values[0]>2*(values[1]-values[0])&&values[1]-values[0]>1.5*(values[2]-values[1]),"real frames start fast and progressively decelerate")) return 1;
        std::printf("{\"Stage\":\"SubmittedEaseOut\",\"Showing\":%s,\"DurationMs\":%.0f,\"FirstSampleMs\":%.3f,\"FirstSampleAlpha\":%u,\"Progress100ms\":%.5f,\"Progress200ms\":%.5f,\"Progress300ms\":%.5f,\"Frames\":%llu,\"EndpointVelocity\":0,\"IdleTimer\":false}\n",
            showing?"true":"false",focus_animation::Transition::DurationMs,elapsed,expectedAlpha,values[0],values[1],values[2],(unsigned long long)frameTrace.size());
        traceAnimation=false;frameTrace.clear();
    }
    // Exercise the actual production wait/message dispatcher under a busy
    // message queue, not only the rendering function or an ideal test timer.
    traceAnimation=true; frameTrace.clear(); SetVisibility(true);
    std::atomic<bool> flood{true};
    std::thread producer([&] { while(flood) { for(int i=0;i<64;i++) PostMessageW(controller,WM_NULL,0,0); Sleep(1); } });
    Pump(160); flood=false; producer.join();
    size_t busyFrames=frameTrace.size();
    traceAnimation=false; frameTrace.clear();
    if(Check(busyFrames>=4,"waitable frame timer survives queued message traffic")) return 1;
    Pump(FullTransitionWaitMs); SetVisibility(false); Pump(FullTransitionWaitMs);
    // Warm up allocations, then require stable GDI resources and private memory.
    for(int i=0;i<2;i++) { SetVisibility(true); Pump(FullTransitionWaitMs); SetVisibility(false); Pump(FullTransitionWaitMs); }
    SIZE_T memoryBefore=PrivateBytes();
    for(int i=0;i<12;i++) {
        SetVisibility(true); Pump(45); SetVisibility(false); Pump(45); SetVisibility(true); Pump(45);
        SetVisibility(false); Pump(FullTransitionWaitMs);
        if(Check(!animationTimerArmed&&surfaces[0].hwnd==cached&&!IsWindowVisible(cached),"rapid-switch idle and cache reuse")) return 1;
    }
    SIZE_T memoryAfter=PrivateBytes();
    DWORD finalGdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    DWORD finalHandles=0; GetProcessHandleCount(GetCurrentProcess(),&finalHandles);
    if(Check(finalGdi==inspectionGdi,"no GDI leak after repeated reversals")||
       Check(memoryAfter<=memoryBefore+2*1024*1024,"no private memory growth")||
       Check(finalHandles<=baseHandles+1,"no waitable timer handle leak")) return 1;
    // Measure real display-sized temporary frames without touching the desktop.
    int screenWidth=GetSystemMetrics(SM_CXSCREEN),screenHeight=GetSystemMetrics(SM_CYSCREEN);
    DropSurface(surfaces[0]);
    surfaces[0].rect={-20000,-20000,-20000+screenWidth,-20000+screenHeight};
    LARGE_INTEGER frequency{},start{},end{};QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    if(Check(Render(surfaces[0],0),"prepare display-sized surface once")) return 1;
    QueryPerformanceCounter(&end);
    double prepareMs=1000.0*(end.QuadPart-start.QuadPart)/frequency.QuadPart;
    double totalMs=0,maxMs=0;
    for(int frame=0;frame<120;frame++) {
        QueryPerformanceCounter(&start);
        if(Check(RenderAnimation(surfaces[0],(frame+.5)/120.0),"display-sized cached alpha update")) return 1;
        QueryPerformanceCounter(&end);
        double ms=1000.0*(end.QuadPart-start.QuadPart)/frequency.QuadPart;
        totalMs+=ms;maxMs=std::max(maxMs,ms);
    }
    SIZE_T animationPrivate=PrivateBytes();
    DropSurface(surfaces[0]);
    if(Check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==inspectionGdi,"display-sized buffers released")) return 1;
    std::printf("{\"Stage\":\"FullSizeCachedAlpha\",\"Width\":%d,\"Height\":%d,\"Frames\":120,\"PrepareMs\":%.3f,\"MeanSubmitMs\":%.4f,\"MaxSubmitMs\":%.4f,\"AnimationPrivateBytes\":%llu,\"AfterReleasePrivateBytes\":%llu}\n",
        screenWidth,screenHeight,prepareMs,totalMs/120,maxMs,(unsigned long long)animationPrivate,(unsigned long long)PrivateBytes());
    std::printf("{\"Passed\":true,\"VisibleApplicationDetection\":true,\"Cycles\":12,\"GdiBefore\":%lu,\"GdiAfter\":%lu,\"HandlesBefore\":%lu,\"HandlesAfter\":%lu,\"BusyQueueFramesIn160ms\":%llu,\"PrivateBytesBefore\":%llu,\"PrivateBytesAfter\":%llu,\"IdleAnimationTimer\":false,\"ProtectedIcons\":true,\"Reversal\":true,\"WindowsAnimationsEnabled\":%s,\"ExplicitEffectStillAnimates\":true}\n",
        baseGdi,finalGdi,baseHandles,finalHandles,(unsigned long long)busyFrames,(unsigned long long)memoryBefore,(unsigned long long)memoryAfter,windowsAnimations?"true":"false");
    Clear(); DestroyWindow(explorer); DestroyWindow(controller); DestroyWindow(desktop);
    CloseHandle(stopEvent); CloseHandle(reloadEvent);
    return 0;
}

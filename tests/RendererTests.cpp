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
    if(Check(animationTimerArmed&&surfaces[0].frame&&IsWindow(surfaces[0].hwnd),"short-lived timer and buffers")) return 1;
    if(Check(RenderAnimation(surfaces[0],.7),"scaled frame submission")) return 1;
    auto& output=surfaces[0].frame->output;
    if(Check(output.pixels[protectedPixel]==0,"icon protection after scaling")||
       Check((output.pixels[0]>>24)>0,"GDI scaling preserves alpha")||
       Check(output.pixels[144*512+256]==0,"clear vignette center")) return 1;
    for(int tint=0;tint<3;tint++) {
        params.tint=tint; surfaces[0].frame.reset();
        if(Check(RenderAnimation(surfaces[0],.5),"tint frame")) return 1;
        auto& frame=surfaces[0].frame->output;
        for(int y=0;y<frame.height;y+=7) for(int x=0;x<frame.width;x+=7) {
            uint32_t pixel=frame.pixels[y*frame.width+x], a=pixel>>24;
            if(Check((pixel&255)<=a&&((pixel>>8)&255)<=a&&((pixel>>16)&255)<=a,"premultiplied color")) return 1;
        }
    }
    params.tint=0;
    Pump(430);
    if(Check(!animationTimerArmed&&!surfaces[0].frame&&IsWindow(surfaces[0].hwnd),"visible rest has no animation resources")) return 1;
    if(Check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==baseGdi,"visible GDI buffers released")) return 1;
    SetVisibility(false); Pump(120);
    double before=transition.Value(GetTickCount64());
    SetVisibility(true);
    double after=transition.Value(GetTickCount64());
    if(Check(std::abs(before-after)<.025,"mid-flight reversal is continuous")) return 1;
    Pump(430); SetVisibility(false); Pump(430);
    if(Check(!animationTimerArmed&&!surfaces[0].frame&&!surfaces[0].hwnd,"hidden destroys desktop surface and buffers")) return 1;
    if(Check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==baseGdi,"hidden GDI buffers released")) return 1;
    SetVisibility(true);
    if(Check(animationTimerArmed&&surfaces[0].frame,"explicit focus effect still animates with Windows animations off")) return 1;
    Pump(430); SetVisibility(false,false);
    if(Check(!animationTimerArmed&&!surfaces[0].hwnd,"explicit immediate cleanup")) return 1;
    params.opacity=0;SetVisibility(true);
    if(Check(!animationTimerArmed&&!surfaces[0].hwnd,"zero opacity does not allocate surfaces")) return 1;
    params.opacity=.68;
    // Exercise the actual production wait/message dispatcher under a busy
    // message queue, not only the rendering function or an ideal test timer.
    traceAnimation=true; frameTrace.clear(); SetVisibility(true);
    std::atomic<bool> flood{true};
    std::thread producer([&] { while(flood) { for(int i=0;i<64;i++) PostMessageW(controller,WM_NULL,0,0); Sleep(1); } });
    Pump(160); flood=false; producer.join();
    size_t busyFrames=frameTrace.size();
    traceAnimation=false; frameTrace.clear();
    if(Check(busyFrames>=4,"waitable frame timer survives queued message traffic")) return 1;
    Pump(430); SetVisibility(false); Pump(430);
    // Warm up allocations, then require stable GDI resources and private memory.
    for(int i=0;i<2;i++) { SetVisibility(true); Pump(390); SetVisibility(false); Pump(390); }
    SIZE_T memoryBefore=PrivateBytes();
    for(int i=0;i<12;i++) {
        SetVisibility(true); Pump(45); SetVisibility(false); Pump(45); SetVisibility(true); Pump(45);
        SetVisibility(false); Pump(430);
        if(Check(!animationTimerArmed&&!surfaces[0].hwnd&&!surfaces[0].frame,"rapid-switch cleanup")) return 1;
    }
    SIZE_T memoryAfter=PrivateBytes();
    DWORD finalGdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    DWORD finalHandles=0; GetProcessHandleCount(GetCurrentProcess(),&finalHandles);
    if(Check(finalGdi==baseGdi,"no GDI leak after repeated reversals")||
       Check(memoryAfter<=memoryBefore+2*1024*1024,"no private memory growth")||
       Check(finalHandles<=baseHandles+1,"no waitable timer handle leak")) return 1;
    // Measure real display-sized temporary frames without touching the desktop.
    int screenWidth=GetSystemMetrics(SM_CXSCREEN),screenHeight=GetSystemMetrics(SM_CYSCREEN);
    surfaces[0].rect={-20000,-20000,-20000+screenWidth,-20000+screenHeight};
    LARGE_INTEGER frequency{},start{},end{};QueryPerformanceFrequency(&frequency);
    double totalMs=0,maxMs=0;
    for(int frame=0;frame<24;frame++) {
        QueryPerformanceCounter(&start);
        if(Check(RenderAnimation(surfaces[0],(frame+.5)/24.0),"display-sized frame")) return 1;
        QueryPerformanceCounter(&end);
        double ms=1000.0*(end.QuadPart-start.QuadPart)/frequency.QuadPart;
        totalMs+=ms;maxMs=std::max(maxMs,ms);
    }
    SIZE_T animationPrivate=PrivateBytes();
    DropSurface(surfaces[0]);
    if(Check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==baseGdi,"display-sized buffers released")) return 1;
    std::printf("{\"Stage\":\"FullSizeFrames\",\"Width\":%d,\"Height\":%d,\"Frames\":24,\"MeanSubmitMs\":%.3f,\"MaxSubmitMs\":%.3f,\"AnimationPrivateBytes\":%llu,\"AfterReleasePrivateBytes\":%llu}\n",
        screenWidth,screenHeight,totalMs/24,maxMs,(unsigned long long)animationPrivate,(unsigned long long)PrivateBytes());
    std::printf("{\"Passed\":true,\"Cycles\":12,\"GdiBefore\":%lu,\"GdiAfter\":%lu,\"HandlesBefore\":%lu,\"HandlesAfter\":%lu,\"BusyQueueFramesIn160ms\":%llu,\"PrivateBytesBefore\":%llu,\"PrivateBytesAfter\":%llu,\"IdleAnimationTimer\":false,\"ProtectedIcons\":true,\"Reversal\":true,\"WindowsAnimationsEnabled\":%s,\"ExplicitEffectStillAnimates\":true}\n",
        baseGdi,finalGdi,baseHandles,finalHandles,(unsigned long long)busyFrames,(unsigned long long)memoryBefore,(unsigned long long)memoryAfter,windowsAnimations?"true":"false");
    Clear(); DestroyWindow(explorer); DestroyWindow(controller); DestroyWindow(desktop);
    CloseHandle(stopEvent); CloseHandle(reloadEvent);
    return 0;
}

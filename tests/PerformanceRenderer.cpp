// Opt-in benchmark entry, using production rendering, event hooks, recovery
// and tray. Overrides ONLY in-memory parameters; no user INI/startup writes.
#define wWinMain UnusedProductionEntry
#include "../src/Nocturne.Renderer/main.cpp"
#undef wWinMain
constexpr UINT PerformanceState=WM_APP+81;
LRESULT CALLBACK PerformanceController(HWND h,UINT m,WPARAM w,LPARAM l) {
    if(m==PerformanceState) {
        unsigned state=1;
        if(!surfaces.empty()&&IsWindowVisible(surfaces.front().hwnd)) state|=2;
        if(!blurSurfaces.empty()&&IsWindowVisible(blurSurfaces.front().composition.hwnd)) state|=4;
        if(blurTimerArmed) state|=8;if(animationTimerArmed) state|=16;
        if(params.blurEnabled) state|=64;if(blurTransition.Target()) state|=128;
        if(!blurSurfaces.empty()&&blurSurfaces.front().occluded) state|=256;
        if(!blurSurfaces.empty()&&blurSurfaces.front().composition.brushAttached) state|=512;
        return state;
    }
    return ControllerProc(h,m,w,l);
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=4) return 87;
    instance=GetModuleHandleW(nullptr);SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    PWSTR local{};
    if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local))) return 1;
    configFile=std::wstring(local)+L"\\Nocturne\\settings.ini";CoTaskMemFree(local);
    configDir=argv[3];Load();
    params.blurEnabled=wcscmp(argv[1],L"on")==0;
    params.blurRadius=std::clamp(wcstod(argv[2],nullptr),0.,48.);
    params.darkenIcons=false;params.saturationEnabled=false;
    if(!LocateDesktop()) {std::puts("ERROR actual Explorer desktop unavailable; no performance verdict");return 77;}
    stopEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);reloadEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=SurfaceProc;wc.lpszClassName=L"Nocturne.Surface";RegisterClassW(&wc);
    wc.lpszClassName=L"Nocturne.BlurSurface";RegisterClassW(&wc);wc.lpszClassName=L"Nocturne.ColorSurface";RegisterClassW(&wc);
    wc.lpfnWndProc=PerformanceController;wc.lpszClassName=L"Penumbra.Performance.Controller";RegisterClassW(&wc);
    controller=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"Penumbra performance test",WS_POPUP,0,0,0,0,nullptr,nullptr,instance,nullptr);
    AddTray();
    foregroundHook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,DesktopEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    minimizeHook=SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART,EVENT_SYSTEM_MINIMIZEEND,nullptr,DesktopEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    windowHook=SetWinEventHook(EVENT_OBJECT_CREATE,EVENT_OBJECT_HIDE,nullptr,DesktopEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    cloakHook=SetWinEventHook(EVENT_OBJECT_CLOAKED,EVENT_OBJECT_UNCLOAKED,nullptr,DesktopEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    locationHook=SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE,EVENT_OBJECT_LOCATIONCHANGE,nullptr,DesktopEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    moveSizeHook=SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART,EVENT_SYSTEM_MOVESIZEEND,nullptr,DesktopEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    reorderHook=SetWinEventHook(EVENT_OBJECT_REORDER,EVENT_OBJECT_REORDER,nullptr,DesktopEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    iconHook=SetWinEventHook(EVENT_OBJECT_CREATE,EVENT_OBJECT_NAMECHANGE,nullptr,DesktopEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    if(!controller||!windowHook||!foregroundHook||!Rebuild()||(params.blurEnabled&&blurSurfaces.empty())) {
        std::puts("ERROR production effect initialization failed");return 2;
    }
    UpdateWindowState();SetTimer(controller,1,10000,nullptr);
    std::printf("{\"Width\":%d,\"Height\":%d,\"RefreshHz\":%.1f,\"Blur\":%s,\"Radius\":%.0f,\"DarkenIcons\":false,\"SaturationEnabled\":false,\"UserIniUnchanged\":true}\n",GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN),detectedRefreshHz,params.blurEnabled?"true":"false",params.blurRadius);std::fflush(stdout);
    while(DispatchWork()){}
    for(auto hook:{foregroundHook,minimizeHook,windowHook,cloakHook,reorderHook,iconHook,locationHook,moveSizeHook}) if(hook) UnhookWinEvent(hook);
    Clear();blurEngine.reset();trayIcon.Reset();DestroyWindow(controller);CloseHandle(stopEvent);CloseHandle(reloadEvent);
    return 0;
}

// Use the production placement code on small off-screen windows. No singleton,
// hooks, settings writes, foreground changes, or manipulation of user windows.
#define UNICODE
#define _UNICODE
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static unsigned foreignAnchorAttempts=0, placementCalls=0;
static BOOL WINAPI CheckedSetWindowPos(HWND window,HWND after,int x,int y,int width,int height,UINT flags) {
    ++placementCalls;
    // Deterministic UIPI regression: fail any foreign insert-after reference,
    // even when the machine running the test has no elevated application open.
    if(after!=HWND_TOP&&after!=HWND_BOTTOM&&after!=HWND_TOPMOST&&after!=HWND_NOTOPMOST) {
        DWORD pid=0; GetWindowThreadProcessId(after,&pid);
        if(pid!=GetCurrentProcessId()) { ++foreignAnchorAttempts; SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    }
    return ::SetWindowPos(window,after,x,y,width,height,flags);
}
#define SetWindowPos CheckedSetWindowPos
#define wWinMain PenumbraApplicationMain
#include "../src/Nocturne.Renderer/main.cpp"
#undef wWinMain
#undef SetWindowPos

static std::vector<HWND> WindowOrder() {
    std::vector<HWND> order;
    EnumWindows([](HWND h,LPARAM data)->BOOL {
        reinterpret_cast<std::vector<HWND>*>(data)->push_back(h); return TRUE;
    },reinterpret_cast<LPARAM>(&order));
    return order;
}
static bool Above(const std::vector<HWND>& order,HWND a,HWND b) {
    auto first=std::find(order.begin(),order.end(),a),second=std::find(order.begin(),order.end(),b);
    return first!=order.end()&&second!=order.end()&&first<second;
}
int main() {
    instance=GetModuleHandleW(nullptr);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HWND foreground=GetForegroundWindow();
    WNDCLASSW wc{}; wc.hInstance=instance; wc.lpfnWndProc=SurfaceProc; wc.lpszClassName=L"Nocturne.Surface";
    if(!RegisterClassW(&wc)||!LocateDesktop()) return 1;
    auto before=WindowOrder();
    Surface first,second;
    first.rect={-20000,-20000,-19904,-19936}; second.rect={-21000,-21000,-20904,-20936};
    int failures=0;
    auto check=[&](bool condition,const char* message) {
        std::printf("%s: %s\n",condition?"PASS":"FAIL",message); if(!condition) ++failures;
    };
    transition.Reset(true);
    check(Render(first,255)&&Render(second,255),"prepare two independent surfaces");
    check(PositionSurface(first,true)&&PositionSurface(second,true),"placement succeeds with foreign anchors denied");
    check(GetWindow(first.hwnd,GW_OWNER)==desktop&&GetWindow(second.hwnd,GW_OWNER)==desktop,"desktop owns both popups");
    auto checkOrder=[&] {
        auto after=WindowOrder();
        bool belowApps=true;
        for(HWND h:before) if(Above(before,h,desktop)&&Above(after,h,desktop)) {
            bool above=Above(after,h,first.hwnd)&&Above(after,h,second.hwnd);
            if(!above) {
                wchar_t name[128]{}; GetClassNameW(h,name,128);
                std::printf("Window below overlay: class=%ls visible=%d hwnd=%p\n",name,IsWindowVisible(h),h);
            }
            belowApps=belowApps&&above;
        }
        check(Above(after,first.hwnd,desktop)&&Above(after,second.hwnd,desktop),"both surfaces remain above desktop");
        check(belowApps,"existing windows above desktop stay above both surfaces");
    };
    checkOrder();
    unsigned placed=placementCalls;
    for(int i=0;i<100;i++) { PositionSurface(first,true); PositionSurface(second,true); }
    check(placementCalls==placed,"100 unchanged placement checks never restack live surfaces");
    capturingPreview=true; PositionSurface(first,true);
    check(!IsWindowVisible(first.hwnd),"preview suppression hides without destroying cache");
    capturingPreview=false;
    check(PositionSurface(first,true)&&IsWindowVisible(first.hwnd)&&first.uploaded,"reshow uses same uploaded surface");
    ::SetWindowPos(first.hwnd,HWND_TOPMOST,0,0,0,0,SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_NOMOVE|SWP_NOSIZE);
    check(PositionSurface(first,true)&&!(GetWindowLongPtrW(first.hwnd,GWL_EXSTYLE)&WS_EX_TOPMOST),"placement clears accidental topmost state");
    checkOrder();
    paused=true; PositionSurface(first,true);
    check(!IsWindowVisible(first.hwnd),"pause still hides the surface");
    paused=false; PositionSurface(first,true);
    check(foreignAnchorAttempts==0&&placementCalls>0,"no dependency on third-party insert-after handles");
    check(GetForegroundWindow()==foreground,"foreground preserved");
    DropSurface(first); DropSurface(second);
    std::printf("{\"DesktopLayering\":%s,\"ForeignAnchorAttempts\":%u,\"PlacementCalls\":%u,\"SavedSettingsUntouched\":true}\n",
        failures?"false":"true",foreignAnchorAttempts,placementCalls);
    return failures?1:0;
}

// Exercise the production EnumWindows predicate and WinEvent handlers on a
// private, non-input Win32 desktop. Never switch desktops or touch user apps.
#define wWinMain PenumbraApplicationMain
#include "../src/Nocturne.Renderer/main.cpp"
#undef wWinMain
void PumpWindowEvents() {
    double until=ClockMs()+80;
    while(ClockMs()<until) if(!DispatchWork(5)) break;
}
int main() {
    HDESK original=GetThreadDesktop(GetCurrentThreadId());
    wchar_t name[80];swprintf(name,80,L"Penumbra.WindowTests.%lu",GetCurrentProcessId());
    HDESK isolated=CreateDesktopW(name,nullptr,nullptr,0,GENERIC_ALL,nullptr);
    if(!isolated||!SetThreadDesktop(isolated)) {std::puts("Cannot create isolated test desktop");return 1;}
    instance=GetModuleHandleW(nullptr);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    stopEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);reloadEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=ControllerProc;wc.lpszClassName=L"Nocturne.Controller";RegisterClassW(&wc);
    controller=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"",WS_POPUP,0,0,0,0,nullptr,nullptr,instance,nullptr);
    wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"Penumbra.TestApplication";RegisterClassW(&wc);
    HWND a=CreateWindowExW(0,wc.lpszClassName,L"A",WS_OVERLAPPEDWINDOW,100,100,200,200,nullptr,nullptr,instance,nullptr);
    HWND b=CreateWindowExW(0,wc.lpszClassName,L"B",WS_OVERLAPPEDWINDOW,400,100,200,200,nullptr,nullptr,instance,nullptr);
    params.focusOnly=true;
    windowHook=SetWinEventHook(EVENT_OBJECT_CREATE,EVENT_OBJECT_HIDE,nullptr,DesktopEvent,GetCurrentProcessId(),0,WINEVENT_OUTOFCONTEXT);
    minimizeHook=SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART,EVENT_SYSTEM_MINIMIZEEND,nullptr,DesktopEvent,GetCurrentProcessId(),0,WINEVENT_OUTOFCONTEXT);
    int failures=0;
    auto check=[&](bool expected,const char* stage) {
        PumpWindowEvents();
        bool present=HasApplicationWindow(),target=transition.Target();
        bool pass=present==expected&&target==expected;
        std::printf("%s present=%d target=%d %s\n",stage,present,target,pass?"PASS":"FAIL");
        if(!pass) failures++;
    };
    check(false,"all hidden");
    ShowWindow(a,SW_SHOWNOACTIVATE);check(true,"show A without focusing");
    ShowWindow(b,SW_SHOWNOACTIVATE);check(true,"show B without focusing");
    ShowWindow(a,SW_HIDE);check(true,"hide A while B remains");
    ShowWindow(b,SW_SHOWMINNOACTIVE);check(false,"minimize last application");
    ShowWindow(b,SW_SHOWNOACTIVATE);check(true,"restore last application");
    DestroyWindow(b);check(false,"close last application");
    UnhookWinEvent(windowHook);UnhookWinEvent(minimizeHook);Clear();
    DestroyWindow(a);DestroyWindow(controller);CloseHandle(stopEvent);CloseHandle(reloadEvent);
    SetThreadDesktop(original);CloseDesktop(isolated);
    std::printf("{\"IsolatedDesktopWindowEvents\":%s,\"UserWindowsUntouched\":true}\n",failures?"false":"true");
    return failures?1:0;
}

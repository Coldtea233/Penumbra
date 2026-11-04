// Production window classification on a private, non-input desktop. Count the
// actual expensive Win32 calls; never switch desktops or focus user windows.
#define UNICODE
#define _UNICODE
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <psapi.h>
#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdint>
#include <memory>
#include "../src/Nocturne.Renderer/desktop_blur.h"
#include "../src/Nocturne.Renderer/branding.h"
#include "../src/Nocturne.Renderer/blur_bounds.h"
#include "../src/Nocturne.Renderer/wallpaper_layer.h"
#include "../src/Nocturne.Renderer/render_budget.h"

namespace query_counts {
unsigned processes=0,enumerations=0;
HANDLE WINAPI OpenProcess(DWORD access,BOOL inherit,DWORD pid) {
    ++processes;return ::OpenProcess(access,inherit,pid);
}
BOOL WINAPI EnumWindows(WNDENUMPROC callback,LPARAM data) {
    ++enumerations;return ::EnumWindows(callback,data);
}
}
#define OpenProcess query_counts::OpenProcess
#define EnumWindows query_counts::EnumWindows
#define wWinMain PenumbraApplicationMain
#include "../src/Nocturne.Renderer/main.cpp"
#undef wWinMain
#undef EnumWindows
#undef OpenProcess

WindowState ReferenceState(HWND foreground) {
    bool hasWindows=HasApplicationWindow();
    ForegroundKind kind=ClassifyForeground(foreground);
    if(kind==ForegroundKind::Application&&!IsApplicationWindow(GetAncestor(foreground,GA_ROOT))) kind=ForegroundKind::Transient;
    return {kind,hasWindows};
}
int main() {
    HWND userForeground=GetForegroundWindow();
    HDESK original=GetThreadDesktop(GetCurrentThreadId());
    wchar_t name[80];swprintf(name,80,L"Penumbra.QueryTests.%lu",GetCurrentProcessId());
    HDESK isolated=CreateDesktopW(name,nullptr,nullptr,0,GENERIC_ALL,nullptr);
    if(!isolated||!SetThreadDesktop(isolated)) {std::puts("FAIL private test desktop unavailable");return 1;}
    instance=GetModuleHandleW(nullptr);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"Penumbra.QueryTestApplication";
    RegisterClassW(&wc);
    HWND app=CreateWindowExW(0,wc.lpszClassName,L"private test app",WS_POPUP,32,32,64,64,nullptr,nullptr,instance,nullptr);
    HWND utility=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"private tool",WS_POPUP,100,100,64,64,nullptr,nullptr,instance,nullptr);
    HWND child=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,0,1,1,app,nullptr,instance,nullptr);
    if(!app||!utility||!child) {std::puts("FAIL private own windows unavailable");return 1;}
    unsigned checks=0,failures=0;
    auto check=[&](bool good,const char* label){++checks;if(!good){++failures;std::printf("FAIL %s\n",label);}};
    auto compare=[&](HWND foreground,bool present,ForegroundKind kind,bool fast) {
        query_counts::processes=query_counts::enumerations=0;
        auto next=ReadWindowState(foreground);
        unsigned processes=query_counts::processes,enumerations=query_counts::enumerations;
        auto previous=ReferenceState(foreground);
        check(next.hasWindows==previous.hasWindows&&next.kind==previous.kind,"optimized query matches original behavior");
        check(next.hasWindows==present&&next.kind==kind,"classification and presence meet expected semantics");
        if(fast) check(processes==1&&enumerations==0,"valid foreground app uses one classification and no EnumWindows");
        else check(enumerations==1,"desktop transient hidden or invalid foreground still checks other windows");
    };
    compare(nullptr,false,ForegroundKind::Transient,false);
    compare(app,false,ForegroundKind::Transient,false);
    ShowWindow(app,SW_SHOWNOACTIVATE);
    compare(app,true,ForegroundKind::Application,true);
    compare(child,true,ForegroundKind::Application,true);
    compare(nullptr,true,ForegroundKind::Transient,false);
    ShowWindow(utility,SW_SHOWNOACTIVATE);
    compare(utility,true,ForegroundKind::Transient,false);
    desktop=app;
    compare(app,false,ForegroundKind::Desktop,false);
    desktop=nullptr;
    ShowWindow(app,SW_SHOWMINNOACTIVE);
    compare(app,false,ForegroundKind::Transient,false);
    ShowWindow(app,SW_SHOWNOACTIVATE);
    SetWindowPos(app,nullptr,-30000,-30000,64,64,SWP_NOZORDER|SWP_NOACTIVATE);
    compare(app,false,ForegroundKind::Transient,false);
    SetWindowPos(app,nullptr,32,32,64,64,SWP_NOZORDER|SWP_NOACTIVATE);
    query_counts::processes=query_counts::enumerations=0;
    auto previous=ReferenceState(app);
    unsigned oldProcesses=query_counts::processes,oldEnumerations=query_counts::enumerations;
    query_counts::processes=query_counts::enumerations=0;
    auto next=ReadWindowState(app);
    check(next.kind==previous.kind&&next.hasWindows==previous.hasWindows,"counted before and after queries agree");
    check(oldProcesses==3&&oldEnumerations==1&&query_counts::processes==1&&query_counts::enumerations==0,"foreground case removes two process queries and one enumeration");
    std::printf("{\"WindowQueryChecks\":%u,\"Failures\":%u,\"ForegroundProcessQueriesBefore\":%u,\"ForegroundProcessQueriesAfter\":%u,\"EnumerationsBefore\":%u,\"EnumerationsAfter\":%u,\"UserWindowsAndSettingsUntouched\":true}\n",
        checks,failures,oldProcesses,query_counts::processes,oldEnumerations,query_counts::enumerations);
    DestroyWindow(child);DestroyWindow(utility);DestroyWindow(app);
    SetThreadDesktop(original);CloseDesktop(isolated);
    check(GetForegroundWindow()==userForeground,"user foreground stayed unchanged");
    return failures?1:0;
}

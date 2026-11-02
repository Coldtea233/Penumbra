#pragma once
#include <windows.h>
#include <dwmapi.h>
#include <algorithm>

namespace blur_bounds {
inline bool IsTaskbar(HWND hwnd) {
    wchar_t name[64]{};
    GetClassNameW(hwnd,name,64);
    return wcscmp(name,L"Shell_TrayWnd")==0||wcscmp(name,L"Shell_SecondaryTrayWnd")==0;
}
// Clip the visible intersection, not the full thickness of an offscreen bar.
// Newly exposed wallpaper must remain filtered throughout auto-hide motion.
inline RECT ExcludeTaskbar(RECT safe,const RECT& monitor,const RECT& taskbar) {
    LONG width=taskbar.right-taskbar.left,height=taskbar.bottom-taskbar.top;
    if(width<=0||height<=0) return safe;
    RECT visible{};
    if(!IntersectRect(&visible,&monitor,&taskbar)) return safe;
    if(width>=height) {
        if(taskbar.top+taskbar.bottom<monitor.top+monitor.bottom)
            safe.top=std::max(safe.top,visible.bottom);
        else safe.bottom=std::min(safe.bottom,visible.top);
    } else {
        if(taskbar.left+taskbar.right<monitor.left+monitor.right)
            safe.left=std::max(safe.left,visible.right);
        else safe.right=std::min(safe.right,visible.left);
    }
    if(safe.right<safe.left) safe.right=safe.left;
    if(safe.bottom<safe.top) safe.bottom=safe.top;
    return safe;
}
inline RECT DesktopArea(const RECT& rect) {
    HMONITOR monitor=MonitorFromRect(&rect,MONITOR_DEFAULTTONEAREST);
    RECT safe=rect;
    // rcWork reserves layout space, not current visibility; starting from it
    // leaves a permanent clear strip when an auto-hide bar retracts.
    struct State { HMONITOR monitor; RECT rect,safe; } state{monitor,rect,safe};
    EnumWindows([](HWND window,LPARAM data)->BOOL {
        auto& state=*reinterpret_cast<State*>(data);
        if(!IsTaskbar(window)||!IsWindowVisible(window)||MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST)!=state.monitor) return TRUE;
        DWORD cloaked=0;
        if(SUCCEEDED(DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof(cloaked)))&&cloaked) return TRUE;
        RECT bar{};
        if(GetWindowRect(window,&bar)) state.safe=ExcludeTaskbar(state.safe,state.rect,bar);
        return TRUE;
    },reinterpret_cast<LPARAM>(&state));
    return state.safe;
}
}

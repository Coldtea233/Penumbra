#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <algorithm>

namespace wallpaper_layer {
inline bool IsHost(HWND window) {
    wchar_t name[64]{}; GetClassNameW(window,name,64);
    return wcscmp(name,L"Progman")==0||wcscmp(name,L"WorkerW")==0;
}
inline bool IsWallpaper(HWND window) {
    wchar_t name[64]{}; GetClassNameW(window,name,64);
    return wcsncmp(name,L"WPE",3)==0;
}
inline HANDLE OpenEngine(DWORD pid) {
    DWORD session=0,current=0;
    if(!ProcessIdToSessionId(pid,&session)||!ProcessIdToSessionId(GetCurrentProcessId(),&current)||session!=current) return nullptr;
    HANDLE process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!process) return nullptr;
    wchar_t path[32768]{}; DWORD count=32768;
    bool valid=QueryFullProcessImageNameW(process,0,path,&count)!=FALSE;
    const wchar_t* name=wcsrchr(path,L'\\'); name=name?name+1:path;
    valid=valid&&(_wcsicmp(name,L"wallpaper32.exe")==0||_wcsicmp(name,L"wallpaper64.exe")==0)&&WaitForSingleObject(process,0)==WAIT_TIMEOUT;
    if(!valid) { CloseHandle(process); return nullptr; }
    return process;
}
struct Layer { HWND wallpaper{},host{}; DWORD pid{}; RECT bounds{}; };
inline std::vector<Layer> Find() {
    std::vector<Layer> layers;
    EnumWindows([](HWND root,LPARAM data)->BOOL {
        if(!IsHost(root)||!IsWindowVisible(root)) return TRUE;
        EnumChildWindows(root,[](HWND window,LPARAM data)->BOOL {
            if(!IsWallpaper(window)||!IsWindowVisible(window)) return TRUE;
            HWND host=GetParent(window);
            // Never attach to an application/browser/unknown host or alter a
            // wallpaper window. Our disposable child belongs to Explorer.
            if(!IsHost(host)) return TRUE;
            // Require a separate wallpaper branch. Sampling an icon-hosting
            // HWND would also desaturate GDI icons on some Explorer versions.
            if(FindWindowExW(host,nullptr,L"SHELLDLL_DefView",nullptr)) return TRUE;
            RECT bounds{}; if(!GetWindowRect(window,&bounds)||bounds.right<=bounds.left||bounds.bottom<=bounds.top) return TRUE;
            DWORD pid=0; GetWindowThreadProcessId(window,&pid);
            HANDLE process=OpenEngine(pid); if(!process) return TRUE; CloseHandle(process);
            auto& result=*reinterpret_cast<std::vector<Layer>*>(data);
            if(std::none_of(result.begin(),result.end(),[&](const Layer& item){return item.host==host&&EqualRect(&item.bounds,&bounds);}))
                result.push_back({window,host,pid,bounds});
            return TRUE;
        },data);
        return TRUE;
    },reinterpret_cast<LPARAM>(&layers));
    return layers;
}
inline bool Place(HWND window,const Layer& layer,const RECT& screen,bool visible) {
    if(!visible) { if(IsWindowVisible(window)) ShowWindow(window,SW_HIDE); return true; }
    if(!IsWindow(window)||!IsWindow(layer.host)||!IsWindow(layer.wallpaper)||GetParent(window)!=layer.host) {
        if(IsWindow(window)) ShowWindow(window,SW_HIDE);
        return false;
    }
    // Explorer can reparent its icon view after a wallpaper/desktop restart.
    // Stop sampling this branch immediately if it is no longer wallpaper-only.
    if(FindWindowExW(layer.host,nullptr,L"SHELLDLL_DefView",nullptr)) {ShowWindow(window,SW_HIDE);return false;}
    POINT origin{screen.left,screen.top}; ScreenToClient(layer.host,&origin);
    HWND anchor=HWND_TOP;
    RECT current{}; GetWindowRect(window,&current);
    bool inOrder=true,reachesWallpaper=false;
    // Adjacent monitor filters may share a host. They need not each be the
    // topmost child; repeatedly swapping those siblings would invalidate the
    // live backdrop every recovery check.
    for(HWND above=GetWindow(window,GW_HWNDPREV);above;above=GetWindow(above,GW_HWNDPREV)) {
        DWORD pid=0;GetWindowThreadProcessId(above,&pid);
        if(pid!=GetCurrentProcessId()) { inOrder=false;break; }
    }
    for(HWND below=GetWindow(window,GW_HWNDNEXT);below;below=GetWindow(below,GW_HWNDNEXT)) {
        if(below==layer.wallpaper) { reachesWallpaper=true;break; }
    }
    if(IsWindowVisible(window)&&EqualRect(&current,&screen)&&inOrder&&reachesWallpaper) return true;
    return SetWindowPos(window,anchor,origin.x,origin.y,screen.right-screen.left,screen.bottom-screen.top,SWP_NOACTIVATE|SWP_SHOWWINDOW)!=FALSE;
}
}

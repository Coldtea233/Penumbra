#pragma once
#include <windows.h>
#include <shellapi.h>

namespace branding {
constexpr int AppIcon=101, TrayWhite=102, TrayBlack=103;

inline int TrayResource() {
    // Taskbar follows Windows mode, not the independently selectable app mode.
    DWORD light=0,bytes=sizeof(light);
    if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"SystemUsesLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&bytes)!=ERROR_SUCCESS) light=0;
    return light?TrayBlack:TrayWhite;
}

class TrayIcon {
    HICON icon{};
    HWND window{};
    UINT id{};
    int resource{},width{},height{};
public:
    TrayIcon()=default;
    TrayIcon(const TrayIcon&)=delete;
    TrayIcon& operator=(const TrayIcon&)=delete;
    ~TrayIcon() { Reset(); }
    int CurrentResource() const { return resource; }
    bool Update(HINSTANCE module,HWND owner,UINT identifier,UINT callback,bool add=false) {
        if(!module||!owner) return false;
        int next=TrayResource();
        HWND taskbar=FindWindowW(L"Shell_TrayWnd",nullptr);
        UINT dpi=taskbar?GetDpiForWindow(taskbar):GetDpiForSystem();
        if(!dpi) dpi=96;
        int cx=GetSystemMetricsForDpi(SM_CXSMICON,dpi),cy=GetSystemMetricsForDpi(SM_CYSMICON,dpi);
        if(!add&&icon&&resource==next&&width==cx&&height==cy) return true;
        HICON fresh=static_cast<HICON>(LoadImageW(module,MAKEINTRESOURCEW(next),IMAGE_ICON,cx,cy,LR_DEFAULTCOLOR));
        if(!fresh) return false; // Preserve the previous icon on a load failure.
        NOTIFYICONDATAW data{sizeof(data)}; data.hWnd=owner; data.uID=identifier;
        data.uFlags=NIF_ICON; data.hIcon=fresh;
        if(add) {
            data.uFlags|=NIF_MESSAGE|NIF_TIP; data.uCallbackMessage=callback;
            lstrcpyW(data.szTip,L"Penumbra");
        }
        if(!Shell_NotifyIconW(add?NIM_ADD:NIM_MODIFY,&data)) { DestroyIcon(fresh); return false; }
        if(icon) DestroyIcon(icon);
        icon=fresh; window=owner; id=identifier; resource=next; width=cx; height=cy;
        if(add) { data.uVersion=NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION,&data); }
        return true;
    }
    void Reset() {
        if(window) { NOTIFYICONDATAW data{sizeof(data)}; data.hWnd=window; data.uID=id; Shell_NotifyIconW(NIM_DELETE,&data); }
        if(icon) DestroyIcon(icon);
        icon=nullptr; window=nullptr; id=0; resource=width=height=0;
    }
};
}

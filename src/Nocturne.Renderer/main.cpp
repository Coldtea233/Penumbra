#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <psapi.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdint>

// All rendering is event-driven. No render loop, Direct3D device, or UI runtime.
constexpr UINT TrayMessage = WM_APP + 1, ReorderMessage = WM_APP + 2, IconMessage = WM_APP + 3, TrayId = 1;
constexpr UINT SettingsId = 101, ReloadId = 102, PauseId = 103, ExitId = 104;
struct Params { double opacity=.68, radius=.48, feather=.65, shape=1, cx=.5, cy=.5; int tint=0; bool primary=false, darkenIcons=false; };
struct Surface { HWND hwnd{}; RECT rect{}; };
HINSTANCE instance;
HWND controller{}, desktop{}, icons{};
HWND iconList{};
HANDLE stopEvent{}, reloadEvent{}, appliedEvent{};
UINT taskbarCreated;
UINT previewMessage;
HWINEVENTHOOK foregroundHook{}, minimizeHook{}, reorderHook{}, iconHook{};
std::vector<Surface> surfaces;
Params params;
bool paused=false, capturingPreview=false;
struct IconMask {
    RECT rect{};
    int width=0, height=0;
    std::vector<unsigned char> bits;
    bool At(int x,int y) const {
        x-=rect.left; y-=rect.top;
        if(x<0||y<0||x>=width||y>=height) return false;
        size_t i=(size_t)y*width+x;
        return (bits[i/8] & (1u<<(i%8)))!=0;
    }
};
IconMask iconMask;
std::wstring configDir, configFile;

double Alpha(const Params& p, double x, double y) {
    double dx=(x-p.cx)*2/p.shape, dy=(y-p.cy)*2;
    double t=std::clamp((std::sqrt(dx*dx+dy*dy)-p.radius)/p.feather,0.0,1.0);
    return p.opacity*t*t*(3-2*t);
}
void InitializePaths() {
    PWSTR path{};
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&path))) {
        configDir=std::wstring(path)+L"\\Nocturne"; CoTaskMemFree(path);
    } else { wchar_t temp[MAX_PATH]; GetTempPathW(MAX_PATH,temp); configDir=std::wstring(temp)+L"Nocturne"; }
    CreateDirectoryW(configDir.c_str(),nullptr); configFile=configDir+L"\\settings.ini";
}
void Status(const wchar_t* message) {
    HANDLE file=CreateFileW((configDir+L"\\status.txt").c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file!=INVALID_HANDLE_VALUE) {
        int length=WideCharToMultiByte(CP_UTF8,0,message,-1,nullptr,0,nullptr,nullptr);
        std::vector<char> bytes(length); WideCharToMultiByte(CP_UTF8,0,message,-1,bytes.data(),length,nullptr,nullptr);
        DWORD written; WriteFile(file,bytes.data(),length-1,&written,nullptr); CloseHandle(file);
    }
    if(appliedEvent) SetEvent(appliedEvent);
}
double ReadNumber(const wchar_t* key,double fallback,double low,double high) {
    wchar_t buffer[80],*end{}; GetPrivateProfileStringW(L"vignette",key,L"",buffer,80,configFile.c_str());
    double value=wcstod(buffer,&end);
    return end!=buffer && *end==0 && std::isfinite(value) ? std::clamp(value,low,high) : fallback;
}
void Load() {
    params.opacity=ReadNumber(L"opacity",.68,0,1); params.radius=ReadNumber(L"radius",.48,.05,1.5);
    params.feather=ReadNumber(L"feather",.65,.05,1.5); params.shape=ReadNumber(L"shape",1,.3,2.5);
    params.cx=ReadNumber(L"centerX",.5,0,1); params.cy=ReadNumber(L"centerY",.5,0,1);
    params.tint=(int)ReadNumber(L"tint",0,0,2); params.primary=ReadNumber(L"primaryOnly",0,0,1)==1;
    params.darkenIcons=ReadNumber(L"darkenIcons",0,0,1)==1;
}
BOOL CALLBACK FindDesktop(HWND hwnd,LPARAM) {
    HWND view=FindWindowExW(hwnd,nullptr,L"SHELLDLL_DefView",nullptr);
    if(view) { icons=view; desktop=hwnd; return FALSE; }
    return TRUE;
}
bool LocateDesktop() {
    desktop=nullptr; icons=nullptr; EnumWindows(FindDesktop,0);
    if(!desktop || !icons) return false;
    wchar_t name[80]{}; GetClassNameW(desktop,name,80);
    iconList=FindWindowExW(icons,nullptr,L"SysListView32",nullptr);
    return wcscmp(name,L"Progman")==0 || wcscmp(name,L"WorkerW")==0;
}
bool ReadIconMask(IconMask& mask) {
    if(!iconList || !IsWindowVisible(iconList)) return true;
    if(!GetWindowRect(iconList,&mask.rect)) return false;
    mask.width=mask.rect.right-mask.rect.left; mask.height=mask.rect.bottom-mask.rect.top;
    size_t count=(size_t)mask.width*mask.height;
    if(mask.width<1 || mask.height<1 || count>160000000) return false;
    BITMAPINFO bi{}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth=mask.width; bi.bmiHeader.biHeight=-mask.height;
    bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32;
    HDC screen=GetDC(nullptr), memory=CreateCompatibleDC(screen);
    void* bits{}; HBITMAP bitmap=CreateDIBSection(screen,&bi,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!memory||!bitmap||!bits) {
        if(bitmap) DeleteObject(bitmap); if(memory) DeleteDC(memory); ReleaseDC(nullptr,screen); return false;
    }
    HGDIOBJ previous=SelectObject(memory,bitmap);
    ZeroMemory(bits,count*4);
    // Explorer paints only the icon glyphs/labels into this black DIB. Excluding
    // those exact pixels preserves icons without bright rectangular cutouts.
    bool ok=PrintWindow(iconList,memory,3)!=FALSE;
    if(ok) {
        mask.bits.assign((count+7)/8,0);
        const auto* pixels=static_cast<const uint32_t*>(bits);
        size_t painted=0;
        for(size_t i=0;i<count;i++) if(pixels[i]&0x00FFFFFFu) {
            mask.bits[i/8]|=1u<<(i%8); painted++;
        }
        // Reject a wallpaper-backed capture rather than punching out the whole desktop.
        if(painted>count*3/4) { mask.bits.clear(); ok=false; }
    }
    SelectObject(memory,previous); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr,screen);
    return ok;
}
void Clear() { for(auto& s:surfaces) if(IsWindow(s.hwnd)) DestroyWindow(s.hwnd); surfaces.clear(); }
bool PositionSurface(Surface& s, bool show) {
    if(!show || capturingPreview) { ShowWindow(s.hwnd,SW_HIDE); return true; }
    // An unowned top-level window is composited above Explorer's wallpaper
    // visuals. Keep it immediately above the desktop, below all normal apps.
    // Passing desktop itself would put the surface *behind* the desktop.
    HWND anchor=GetWindow(desktop,GW_HWNDPREV);
    while(anchor) {
        wchar_t name[80]{}; GetClassNameW(anchor,name,80);
        if(wcscmp(name,L"Nocturne.Surface")!=0) break;
        anchor=GetWindow(anchor,GW_HWNDPREV);
    }
    if(GetWindowLongPtrW(s.hwnd,GWL_EXSTYLE)&WS_EX_TOPMOST)
        SetWindowPos(s.hwnd,HWND_NOTOPMOST,0,0,0,0,SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOSIZE);
    return SetWindowPos(s.hwnd,anchor?anchor:HWND_TOP,s.rect.left,s.rect.top,
        s.rect.right-s.rect.left,s.rect.bottom-s.rect.top,SWP_NOACTIVATE|SWP_SHOWWINDOW)!=FALSE;
}
BOOL CALLBACK CollectMonitor(HMONITOR monitor,HDC,LPRECT,LPARAM data) {
    MONITORINFO info{sizeof(info)};
    if(GetMonitorInfoW(monitor,&info) && (!params.primary || (info.dwFlags&MONITORINFOF_PRIMARY)))
        reinterpret_cast<std::vector<RECT>*>(data)->push_back(info.rcMonitor);
    return TRUE;
}
bool Render(Surface& s) {
    int width=s.rect.right-s.rect.left, height=s.rect.bottom-s.rect.top;
    if(width<1 || height<1 || width>32768 || height>32768 || (long long)width*height>160000000) return false;
    BITMAPINFO bi{}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth=width;
    bi.bmiHeader.biHeight=-height; bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
    HDC screen=GetDC(nullptr), memory=CreateCompatibleDC(screen); void* bits{};
    HBITMAP bitmap=CreateDIBSection(screen,&bi,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!memory || !bitmap || !bits) { if(bitmap) DeleteObject(bitmap); if(memory) DeleteDC(memory); ReleaseDC(nullptr,screen); return false; }
    HGDIOBJ old=SelectObject(memory,bitmap);
    unsigned char rgb[3]{};
    if(params.tint==1) { rgb[0]=14; rgb[1]=22; rgb[2]=40; }
    if(params.tint==2) { rgb[0]=27; rgb[1]=18; rgb[2]=16; }
    auto* pixel=static_cast<unsigned char*>(bits);
    // Premultiplied BGRA required by UpdateLayeredWindow.
    for(int y=0;y<height;y++) for(int x=0;x<width;x++) {
        double a=iconMask.At(s.rect.left+x,s.rect.top+y) && !params.darkenIcons?0:Alpha(params,(x+.5)/width,(y+.5)/height);
        *pixel++=(unsigned char)std::lround(rgb[2]*a); *pixel++=(unsigned char)std::lround(rgb[1]*a);
        *pixel++=(unsigned char)std::lround(rgb[0]*a); *pixel++=(unsigned char)std::lround(255*a);
    }
    POINT source{}, destination{s.rect.left,s.rect.top};
    SIZE size{width,height}; BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    bool ok=UpdateLayeredWindow(s.hwnd,screen,&destination,&size,memory,&source,0,&blend,ULW_ALPHA)!=FALSE;
    SelectObject(memory,old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr,screen);
    return ok;
}
bool Rebuild() {
    Clear(); if(paused) { Status(L"暗角已暂停 · 可从托盘恢复"); return true; }
    if(!LocateDesktop()) { Status(L"未找到桌面宿主 · 已安全隐藏，将自动重试"); return false; }
    IconMask fresh;
    if(!ReadIconMask(fresh)) { Status(L"无法读取桌面图标遮罩 · 未启用暗角"); return false; }
    iconMask=std::move(fresh);
    std::vector<RECT> monitors; EnumDisplayMonitors(nullptr,nullptr,CollectMonitor,reinterpret_cast<LPARAM>(&monitors));
    if(monitors.empty()) { Status(L"没有可用显示器"); return false; }
    for(const RECT& r:monitors) {
        Surface s; s.rect=r;
        s.hwnd=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,
            L"Nocturne.Surface",L"Nocturne vignette",WS_POPUP,0,0,1,1,nullptr,nullptr,instance,nullptr);
        if(!s.hwnd || !Render(s)) {
            if(s.hwnd) DestroyWindow(s.hwnd); Clear(); Status(L"桌面挂接或位图提交失败 · 未启用暗角"); return false;
        }
        if (!PositionSurface(s, true)) {
            DestroyWindow(s.hwnd); Clear(); Status(L"无法设置桌面暗角层级 · 未启用暗角"); return false;
        }
        surfaces.push_back(s);
    }
    Status(L"暗角已应用 · 位于桌面上方、普通应用下方"); return true;
}
void OpenSettings() {
    wchar_t exe[32768]; GetModuleFileNameW(nullptr,exe,32768);
    std::wstring path=exe; path=path.substr(0,path.find_last_of(L"\\/")+1)+L"Penumbra.exe";
    if(reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)
        MessageBoxW(nullptr,L"无法打开设置。请保持 Penumbra.exe 与原生程序位于同一文件夹。",L"Penumbra 半影",MB_OK|MB_ICONERROR);
}
void AddTray() {
    NOTIFYICONDATAW nid{sizeof(nid)}; nid.hWnd=controller; nid.uID=TrayId;
    nid.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP; nid.uCallbackMessage=TrayMessage;
    nid.hIcon=LoadIconW(nullptr,IDI_APPLICATION); lstrcpyW(nid.szTip,L"Penumbra · 半影"); Shell_NotifyIconW(NIM_ADD,&nid);
    nid.uVersion=NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION,&nid);
}
void Menu() {
    HMENU menu=CreatePopupMenu(); AppendMenuW(menu,MF_STRING,SettingsId,L"打开设置");
    AppendMenuW(menu,MF_STRING,ReloadId,L"重新挂接桌面"); AppendMenuW(menu,MF_STRING,PauseId,paused?L"恢复暗角":L"暂停暗角");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr); AppendMenuW(menu,MF_STRING,ExitId,L"退出暗角");
    POINT p; GetCursorPos(&p); SetForegroundWindow(controller);
    UINT command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,controller,nullptr); DestroyMenu(menu);
    if(command==SettingsId) OpenSettings();
    if(command==ReloadId) { Load(); Rebuild(); }
    if(command==PauseId) { paused=!paused; Rebuild(); }
    if(command==ExitId) PostQuitMessage(0);
    PostMessageW(controller,WM_NULL,0,0);
}
LRESULT CALLBACK SurfaceProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_NCHITTEST) return HTTRANSPARENT;
    if(msg==WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if(msg==WM_ERASEBKGND) return 1;
    return DefWindowProcW(hwnd,msg,wp,lp);
}
void CALLBACK DesktopEvent(HWINEVENTHOOK hook,DWORD event,HWND hwnd,LONG object,LONG,DWORD,DWORD) {
    if(hwnd==iconList || hwnd==icons) { PostMessageW(controller,IconMessage,0,0); return; }
    // The icon hook spans several object events. Ignore unrelated applications
    // here so animation/accessibility events cannot keep repositioning us.
    if(hook==iconHook) return;
    if(event==EVENT_OBJECT_REORDER && (object!=OBJID_WINDOW || (hwnd && hwnd!=desktop && hwnd!=GetDesktopWindow()))) return;
    PostMessageW(controller,ReorderMessage,0,0);
}
LRESULT CALLBACK ControllerProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(previewMessage && msg==previewMessage) {
        capturingPreview=wp!=0;
        if(capturingPreview) SetTimer(hwnd,4,3000,nullptr); else KillTimer(hwnd,4);
        for(auto& s:surfaces) PositionSurface(s,true);
        return 1;
    }
    if(taskbarCreated && msg==taskbarCreated) { AddTray(); SetTimer(hwnd,2,1500,nullptr); return 0; }
    switch(msg) {
    case ReorderMessage: SetTimer(hwnd,3,60,nullptr); return 0;
    case IconMessage: if(!paused) SetTimer(hwnd,5,350,nullptr); return 0;
    case TrayMessage:
        if(LOWORD(lp)==NIN_SELECT || LOWORD(lp)==WM_LBUTTONDBLCLK) OpenSettings();
        else if(LOWORD(lp)==WM_CONTEXTMENU || LOWORD(lp)==WM_RBUTTONUP) Menu();
        return 0;
    case WM_DISPLAYCHANGE: case WM_SETTINGCHANGE: SetTimer(hwnd,2,700,nullptr); return 0;
    case WM_TIMER:
        if(wp==4) {
            KillTimer(hwnd,4); capturingPreview=false;
            for(auto& s:surfaces) PositionSurface(s,true);
        }
        else if(wp==5) {
            KillTimer(hwnd,5);
            if(!paused) {
                IconMask fresh;
                if(ReadIconMask(fresh) && (fresh.bits!=iconMask.bits || !EqualRect(&fresh.rect,&iconMask.rect))) {
                    iconMask=std::move(fresh);
                    for(auto& s:surfaces) Render(s);
                }
            }
        }
        else if(wp==2) { KillTimer(hwnd,2); Rebuild(); }
        else if(wp==1 && !paused) {
            HWND previous=desktop;
            bool intact=LocateDesktop() && previous==desktop && !surfaces.empty();
            if(intact) for(auto& s:surfaces) if(!IsWindow(s.hwnd)) { intact=false; break; }
            if(!intact) Rebuild();
            else for(auto& s:surfaces) PositionSurface(s, true);
        }
        else if(wp==3) {
            KillTimer(hwnd,3);
            for(auto& s:surfaces) PositionSurface(s, true);
        }
        return 0;
    case WM_QUERYENDSESSION: return TRUE;
    case WM_ENDSESSION: if(wp) PostQuitMessage(0); return 0;
    case WM_CLOSE: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
int SelfTest() {
    Params p;
    if(Alpha(p,.5,.5)!=0 || std::abs(Alpha(p,0,0)-p.opacity)>1e-10) return 1;
    double previous=0;
    for(int i=0;i<=100;i++) { double a=Alpha(p,.5+i*.005,.5); if(!std::isfinite(a)||a<previous-1e-12||a>p.opacity) return 2; previous=a; }
    p.opacity=0; if(Alpha(p,0,0)!=0) return 3;
    p.opacity=1; p.feather=.05; p.shape=.3; if(Alpha(p,0,0)!=1) return 4;
    p.cx=.2; p.cy=.8; if(Alpha(p,.2,.8)!=0) return 5;
    return 0;
}
int WINAPI wWinMain(HINSTANCE app,HINSTANCE,PWSTR args,int) {
    if(wcsstr(args,L"--self-test")) return SelfTest();
    instance=app; SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HANDLE mutex=CreateMutexW(nullptr,TRUE,L"Local\\Nocturne.Renderer.v1");
    if(!mutex) return 2;
    if(GetLastError()==ERROR_ALREADY_EXISTS) {
        // Wait briefly for the first instance to publish its event handles.
        for(int i=0;i<30;i++) { HANDLE event=OpenEventW(EVENT_MODIFY_STATE,FALSE,L"Local\\Nocturne.Reload.v1");
            if(event) { SetEvent(event); CloseHandle(event); CloseHandle(mutex); return 0; } Sleep(50); }
        CloseHandle(mutex); return 3;
    }
    InitializePaths(); stopEvent=CreateEventW(nullptr,FALSE,FALSE,L"Local\\Nocturne.Stop.v1");
    reloadEvent=CreateEventW(nullptr,FALSE,FALSE,L"Local\\Nocturne.Reload.v1");
    appliedEvent=CreateEventW(nullptr,FALSE,FALSE,L"Local\\Nocturne.Applied.v1");
    if(!stopEvent||!reloadEvent||!appliedEvent) { CloseHandle(mutex); return 4; }
    WNDCLASSW wc{}; wc.hInstance=app; wc.lpfnWndProc=SurfaceProc; wc.lpszClassName=L"Nocturne.Surface"; RegisterClassW(&wc);
    wc.lpfnWndProc=ControllerProc; wc.lpszClassName=L"Nocturne.Controller"; RegisterClassW(&wc);
    controller=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"Nocturne Controller",WS_POPUP,0,0,0,0,nullptr,nullptr,app,nullptr);
    if(!controller) { Status(L"无法创建后台控制窗口"); return 5; }
    taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    previewMessage=RegisterWindowMessageW(L"Nocturne.CapturePreview.v1");
    AddTray(); Load(); Rebuild();
    foregroundHook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,DesktopEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    minimizeHook=SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART,EVENT_SYSTEM_MINIMIZEEND,nullptr,DesktopEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    reorderHook=SetWinEventHook(EVENT_OBJECT_REORDER,EVENT_OBJECT_REORDER,nullptr,DesktopEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    iconHook=SetWinEventHook(EVENT_OBJECT_CREATE,EVENT_OBJECT_NAMECHANGE,nullptr,DesktopEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    SetTimer(controller,1,10000,nullptr); SetPriorityClass(GetCurrentProcess(),IDLE_PRIORITY_CLASS);
    HANDLE events[]{stopEvent,reloadEvent}; bool running=true;
    while(running) {
        DWORD wait=MsgWaitForMultipleObjectsEx(2,events,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        if(wait==WAIT_OBJECT_0 || wait==WAIT_FAILED) break;
        if(wait==WAIT_OBJECT_0+1) { paused=false; Load(); Rebuild(); }
        if(wait==WAIT_OBJECT_0+2) { MSG msg; while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) { if(msg.message==WM_QUIT) { running=false; break; } TranslateMessage(&msg); DispatchMessageW(&msg); } }
    }
    if(foregroundHook) UnhookWinEvent(foregroundHook);
    if(minimizeHook) UnhookWinEvent(minimizeHook);
    if(reorderHook) UnhookWinEvent(reorderHook);
    if(iconHook) UnhookWinEvent(iconHook);
    Clear(); NOTIFYICONDATAW nid{sizeof(nid)}; nid.hWnd=controller; nid.uID=TrayId; Shell_NotifyIconW(NIM_DELETE,&nid);
    DestroyWindow(controller); CloseHandle(stopEvent); CloseHandle(reloadEvent); CloseHandle(appliedEvent); CloseHandle(mutex); return 0;
}

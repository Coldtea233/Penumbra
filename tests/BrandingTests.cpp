// Uses real embedded icon resources, but mocks theme reads and tray writes.
// Never changes Windows theme, creates a tray item, or touches user settings.
#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>

static DWORD light=0,themeError=ERROR_SUCCESS;
static UINT dpi=96;
static unsigned adds=0,modifies=0,deletes=0,versions=0,badKeys=0;
static bool failNotify=false;
static HICON submitted{};
static LSTATUS WINAPI ReadTheme(HKEY,LPCWSTR,LPCWSTR name,DWORD,LPDWORD,void* data,LPDWORD size) {
    if(wcscmp(name,L"SystemUsesLightTheme")) ++badKeys;
    if(themeError!=ERROR_SUCCESS) return themeError;
    if(*size<sizeof(light)) return ERROR_MORE_DATA;
    std::memcpy(data,&light,sizeof(light)); *size=sizeof(light); return ERROR_SUCCESS;
}
static BOOL WINAPI Notify(DWORD message,PNOTIFYICONDATAW data) {
    if(failNotify) return FALSE;
    if(message==NIM_ADD) { ++adds; submitted=data->hIcon; }
    if(message==NIM_MODIFY) { ++modifies; submitted=data->hIcon; }
    if(message==NIM_DELETE) ++deletes;
    if(message==NIM_SETVERSION) ++versions;
    return TRUE;
}
static UINT WINAPI WindowDpi(HWND) { return dpi; }
static UINT WINAPI SystemDpi() { return dpi; }
#define RegGetValueW ReadTheme
#define Shell_NotifyIconW Notify
#define GetDpiForWindow WindowDpi
#define GetDpiForSystem SystemDpi
#include "../src/Nocturne.Renderer/branding.h"
#undef RegGetValueW
#undef Shell_NotifyIconW
#undef GetDpiForWindow
#undef GetDpiForSystem

static bool Color(HICON icon,int size,bool white) {
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=size; info.bmiHeader.biHeight=-size;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
    void* bits{}; HDC dc=CreateCompatibleDC(nullptr);
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    auto previous=SelectObject(dc,bitmap); std::memset(bits,0,size*size*4);
    bool drawn=DrawIconEx(dc,0,0,icon,size,size,0,nullptr,DI_NORMAL)!=FALSE;
    GdiFlush(); unsigned count=0; double sum=0;
    auto pixels=static_cast<unsigned char*>(bits);
    for(int i=0;i<size*size;i++) if(pixels[i*4+3]>240) { sum+=pixels[i*4+2]; count++; }
    SelectObject(dc,previous); DeleteObject(bitmap); DeleteDC(dc);
    return drawn&&count>0&&(white?sum/count>245:sum/count<10);
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) return 2;
    HMODULE module=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if(!module) { std::printf("Could not read renderer icon resources: %lu\n",GetLastError()); return 2; }
    unsigned checks=0,failures=0;
    auto check=[&](bool good,const char* name) { checks++; if(!good) { failures++; std::printf("FAIL %s\n",name); } };
    for(int size:{16,20,24,32,40,48,64,256}) for(int resource:{branding::AppIcon,branding::TrayWhite,branding::TrayBlack}) {
        auto icon=static_cast<HICON>(LoadImageW(module,MAKEINTRESOURCEW(resource),IMAGE_ICON,size,size,LR_DEFAULTCOLOR));
        check(icon!=nullptr,"embedded icon is loadable at requested DPI size");
        if(resource!=branding::AppIcon) check(icon&&Color(icon,size,resource==branding::TrayWhite),"provided monochrome tray color is preserved");
        if(icon) DestroyIcon(icon);
    }
    branding::TrayIcon tray;
    HWND owner=reinterpret_cast<HWND>(static_cast<INT_PTR>(1));
    DWORD gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    check(tray.Update(module,owner,1,WM_APP+1,true)&&tray.CurrentResource()==branding::TrayWhite&&Color(submitted,16,true),"dark Windows taskbar uses white icon");
    unsigned before=adds+modifies;
    for(int i=0;i<100;i++) check(tray.Update(module,owner,1,WM_APP+1),"unchanged theme update succeeds");
    check(adds+modifies==before,"100 unchanged notifications do not reload or rewrite the tray");
    light=1;
    check(tray.Update(module,owner,1,WM_APP+1)&&tray.CurrentResource()==branding::TrayBlack&&Color(submitted,16,false),"light Windows taskbar switches to black icon");
    dpi=144;
    check(tray.Update(module,owner,1,WM_APP+1)&&Color(submitted,24,false),"DPI change reloads correctly sized black icon");
    themeError=ERROR_FILE_NOT_FOUND;
    check(tray.Update(module,owner,1,WM_APP+1)&&tray.CurrentResource()==branding::TrayWhite,"unavailable theme safely defaults to white");
    themeError=ERROR_SUCCESS; light=1; failNotify=true;
    check(!tray.Update(module,owner,1,WM_APP+1)&&tray.CurrentResource()==branding::TrayWhite,"failed tray update preserves previous icon");
    failNotify=false;
    check(tray.Update(module,owner,1,WM_APP+1,true)&&tray.CurrentResource()==branding::TrayBlack&&adds==2&&versions==2,"Explorer restart adds the current themed icon with version 4");
    tray.Reset(); tray.Reset();
    check(deletes==1,"shutdown removes tray item only once");
    check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==gdi,"themed replacements release all owned GDI icon resources");
    check(badKeys==0,"tray follows system theme rather than application theme");
    FreeLibrary(module);
    std::printf("{\"BrandingChecks\":%u,\"Failures\":%u,\"WindowsThemeUnchanged\":true,\"RealTrayUntouched\":true}\n",checks,failures);
    return failures?1:0;
}

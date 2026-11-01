// Opt-in actual-desktop fixture: no blur implementation here. The installed
// production renderer must blur this tiny surface in the desktop layer. No wallpaper
// file, settings, foreground app, or other application's window is modified.
#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <cstdio>
#include <cmath>
#include <algorithm>

constexpr UINT ReadContrast=WM_APP+80;
double Contrast(HWND window) {
    POINT point{16,160}; ClientToScreen(window,&point);
    HDC screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=224; info.bmiHeader.biHeight=-1;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
    void* pixels{}; HBITMAP bitmap=CreateDIBSection(memory,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    auto previous=SelectObject(memory,bitmap);
    BitBlt(memory,0,0,224,1,screen,point.x,point.y,SRCCOPY|CAPTUREBLT); GdiFlush();
    double sum=0,squared=0;
    for(int x=0;x<224;x++) { double r=static_cast<unsigned char*>(pixels)[4*x+2]; sum+=r; squared+=r*r; }
    SelectObject(memory,previous); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr,screen);
    return std::sqrt(std::max(0.,squared/224-(sum/224)*(sum/224)));
}
LRESULT CALLBACK Proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_NCHITTEST) return HTTRANSPARENT;
    if(msg==WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if(msg==WM_ERASEBKGND) return 1;
    if(msg==WM_PAINT) {
        PAINTSTRUCT paint{}; HDC dc=BeginPaint(hwnd,&paint);
        for(int x=0;x<256;x+=80) {
            int color=x%160?224:32; HBRUSH brush=CreateSolidBrush(RGB(color,color,color));
            RECT rect{x,0,x+80,384}; FillRect(dc,&rect,brush); DeleteObject(brush);
        }
        EndPaint(hwnd,&paint); return 0;
    }
    if(msg==ReadContrast) return static_cast<LRESULT>(std::lround(Contrast(hwnd)*1000));
    if(msg==WM_TIMER&&wp==2) {
        HWND blur=FindWindowW(L"Nocturne.BlurSurface",nullptr);
        bool below=false;
        if(blur) for(HWND next=GetWindow(blur,GW_HWNDNEXT);next;next=GetWindow(next,GW_HWNDNEXT)) if(next==hwnd) { below=true; break; }
        if(blur&&!below) {
            SetWindowPos(hwnd,blur,0,0,0,0,SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_NOMOVE|SWP_NOSIZE);
        }
        return 0;
    }
    if((msg==WM_TIMER&&wp==1)||msg==WM_CLOSE) { DestroyWindow(hwnd); return 0; }
    if(msg==WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HWND blur=FindWindowW(L"Nocturne.BlurSurface",nullptr);
    HWND desktop=blur?GetWindow(blur,GW_OWNER):nullptr;
    if(!desktop) desktop=FindWindowW(L"Progman",nullptr);
    if(!desktop) { std::puts("No production desktop blur owner found"); return 1; }
    WNDCLASSW wc{}; wc.hInstance=GetModuleHandleW(nullptr); wc.lpfnWndProc=Proc;
    wc.lpszClassName=L"Penumbra.DesktopBlurFixture"; RegisterClassW(&wc);
    HWND fixture=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT,wc.lpszClassName,L"Penumbra desktop-only blur fixture",WS_POPUP|WS_VISIBLE,16,16,256,384,desktop,nullptr,wc.hInstance,nullptr);
    if(!fixture) return 2;
    SetLayeredWindowAttributes(fixture,0,255,LWA_ALPHA);
    SetWindowPos(fixture,blur?blur:HWND_BOTTOM,16,16,256,384,SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_SHOWWINDOW);
    UpdateWindow(fixture); SetTimer(fixture,1,600000,nullptr); SetTimer(fixture,2,250,nullptr);
    std::printf("Fixture=%llu Desktop=%llu Message=%u\n",static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(fixture)),static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(desktop)),ReadContrast); std::fflush(stdout);
    MSG msg; while(GetMessageW(&msg,nullptr,0,0)>0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return 0;
}

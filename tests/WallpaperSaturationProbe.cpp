#define PENUMBRA_SATURATION_HELPERS_ONLY
#include "SaturationProbe.cpp"
#include "../src/Nocturne.Renderer/wallpaper_layer.h"
#include <vector>

std::vector<COLORREF> Patch() {
    HDC screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=256; info.bmiHeader.biHeight=-384;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* data{}; HBITMAP bitmap=CreateDIBSection(memory,&info,DIB_RGB_COLORS,&data,nullptr,0); auto previous=SelectObject(memory,bitmap);
    BitBlt(memory,0,0,256,384,screen,16,16,SRCCOPY|CAPTUREBLT);GdiFlush();
    std::vector<COLORREF> pixels;
    for(int y=8;y<364;y+=4) for(int x=8;x<234;x+=4) {auto p=static_cast<const unsigned char*>(data)+(y*256+x)*4; pixels.push_back(RGB(p[2],p[1],p[0]));}
    SelectObject(memory,previous);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(nullptr,screen);return pixels;
}

int main() {
    HWND foreground=GetForegroundWindow(); SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    auto layers=wallpaper_layer::Find();
    if(layers.empty()) { std::puts("SKIP Wallpaper Engine desktop is unavailable"); return 77; }
    WNDCLASSW wc{}; wc.hInstance=GetModuleHandleW(nullptr); wc.lpfnWndProc=Proc; wc.lpszClassName=L"Penumbra.WallpaperSaturationProbe"; RegisterClassW(&wc);
    desktop_blur::Engine engine; desktop_blur::Engine::Surface surface;
    auto layer=layers[0]; RECT area{16,16,272,400}; POINT origin{area.left,area.top}; ScreenToClient(layer.host,&origin);
    auto before=Patch();
    surface.hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,wc.lpszClassName,L"",WS_CHILD,origin.x,origin.y,256,384,layer.host,nullptr,wc.hInstance,nullptr);
    HRESULT hr=engine.Attach(surface.hwnd,256,384,0,surface,desktop_blur::Filter::Saturation);
    if(FAILED(hr)) { std::printf("Attach %08lX %ls\n",static_cast<unsigned long>(hr),engine.stage); surface.Reset(); return 1; }
    surface.visual->put_Opacity(1); wallpaper_layer::Place(surface.hwnd,layer,area,true); Pump(600);
    int sameColor=0,becameGray=0,colorBefore=0;auto after=Patch();
    for(size_t index=0;index<before.size();index++) {
        auto pixel=after[index],plain=before[index];
        if(Chroma(plain)>30) { colorBefore++; if(Chroma(pixel)<=2) becameGray++; if(pixel==plain) sameColor++; }
    }
    std::printf("WallpaperHost=%p Wallpaper=%p Effect=%p Visible=%d ColorSamples=%d Gray=%d ColorUnchanged=%d\n",layer.host,layer.wallpaper,surface.hwnd,IsWindowVisible(surface.hwnd),colorBefore,becameGray,sameColor);
    surface.Reset(); Pump(100);
    std::printf("ForegroundPreserved=%s\n",GetForegroundWindow()==foreground?"true":"false");
    return becameGray>50&&GetForegroundWindow()==foreground?0:1;
}

#define UNICODE
#define _UNICODE
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <dwmapi.h>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include "../src/Nocturne.Renderer/desktop_blur.h"

LRESULT CALLBACK Proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_NCHITTEST) return HTTRANSPARENT;
    if(msg==WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if(msg==WM_ERASEBKGND) return 1;
    return DefWindowProcW(hwnd,msg,wp,lp);
}
void Pump(DWORD milliseconds) {
    ULONGLONG end=GetTickCount64()+milliseconds;
    while(GetTickCount64()<end) {
        MsgWaitForMultipleObjectsEx(0,nullptr,10,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        MSG msg; while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
}
void PaintPattern(HWND hwnd,bool blue) {
    HDC dc=GetDC(hwnd);
    for(int x=0;x<640;x+=80) {
        int color=x%160?224:32;
        HBRUSH brush=CreateSolidBrush(blue?RGB(16,32,color):RGB(color,color,color));
        RECT rect{x,0,x+80,256}; FillRect(dc,&rect,brush); DeleteObject(brush);
    }
    ReleaseDC(hwnd,dc);
}
struct Sample { double contrast=0,red=0,blue=0; };
Sample ReadSample() {
    HDC screen=GetDC(nullptr); Sample sample; double sum=0,squared=0;
    HDC memory=CreateCompatibleDC(screen);
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=320;
    info.bmiHeader.biHeight=-1; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
    void* data{}; HBITMAP bitmap=CreateDIBSection(memory,&info,DIB_RGB_COLORS,&data,nullptr,0);
    HGDIOBJ previous=SelectObject(memory,bitmap);
    BitBlt(memory,0,0,320,1,screen,176,144,SRCCOPY|CAPTUREBLT); GdiFlush();
    auto* pixels=static_cast<const unsigned char*>(data);
    for(int x=0;x<320;x+=2) {
        double value=pixels[x*4+2]; sum+=value; squared+=value*value;
        sample.blue+=pixels[x*4];
    }
    sample.red=sum/160; sample.blue/=160;
    sample.contrast=std::sqrt(std::max(0.,squared/160-sample.red*sample.red));
    SelectObject(memory,previous); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr,screen); return sample;
}
int main() {
    HWND foreground=GetForegroundWindow();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW wc{}; wc.hInstance=GetModuleHandleW(nullptr); wc.lpfnWndProc=Proc; wc.lpszClassName=L"Penumbra.BlurProbe"; RegisterClassW(&wc);
    HWND backing=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TOPMOST,wc.lpszClassName,L"Blur probe background",WS_POPUP,16,16,640,256,nullptr,nullptr,wc.hInstance,nullptr);
    SetWindowPos(backing,HWND_TOPMOST,16,16,640,256,SWP_NOACTIVATE|SWP_SHOWWINDOW); PaintPattern(backing,false); Pump(180);
    std::printf("Backing=%p valid=%d visible=%d style=%08lx\n",backing,IsWindow(backing),IsWindowVisible(backing),static_cast<unsigned long>(GetWindowLongW(backing,GWL_STYLE)));
    auto plain=ReadSample();
    if(plain.contrast<60) {
        DestroyWindow(backing);
        std::printf("SKIP: test backing not readable in screen capture (contrast=%.2f); no visual verdict.\n",plain.contrast);
        return 77;
    }
    desktop_blur::Engine engine; Sample samples[5]; int index=0,failures=0;
    desktop_blur::Engine::Surface surface;
        surface.hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_LAYERED|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT|WS_EX_TOOLWINDOW|WS_EX_TOPMOST,wc.lpszClassName,L"Blur probe",WS_POPUP,16,16,640,256,backing,nullptr,wc.hInstance,nullptr);
        SetLayeredWindowAttributes(surface.hwnd,0,255,LWA_ALPHA);
        HRESULT hr=engine.Attach(surface.hwnd,640,256,0,surface);
        if(FAILED(hr)) { std::printf("Attach failed %08lx stage=%ls\n",static_cast<unsigned long>(hr),engine.stage); surface.Reset(); DestroyWindow(backing); return 1; }
        surface.visual->put_Opacity(1); SetWindowPos(surface.hwnd,HWND_TOPMOST,16,16,640,256,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    auto originalWindow=surface.hwnd; auto originalBrush=surface.effect.Get();
    for(float radius:{0.f,4.f,12.f,24.f,48.f}) {
        hr=engine.SetRadius(surface,radius);
        std::printf("radius=%.0f Attach=%08lx stage=%ls\n",radius,static_cast<unsigned long>(hr),engine.stage); std::fflush(stdout);
        if(FAILED(hr)) { surface.Reset(); failures++; break; }
        float actual=-1; desktop_blur::C::CompositionGetValueStatus result{};
        HRESULT read=surface.properties->TryGetScalar(desktop_blur::String(L"Blur.BlurAmount"),&actual,&result);
        if(FAILED(read)||result!=desktop_blur::C::CompositionGetValueStatus_Succeeded||std::abs(actual-blur_parameters::AdditionalSigma(radius))>.001||surface.hwnd!=originalWindow||surface.effect.Get()!=originalBrush) failures++;
        Pump(350);
        samples[index++]=ReadSample();
        std::printf("ConfiguredRadius=%.0f GaussianSigma=%.3f PixelContrast=%.3f\n",radius,blur_parameters::AdditionalSigma(radius),samples[index-1].contrast);
        if(radius==48) {
            PaintPattern(backing,true); Pump(350); auto changed=ReadSample();
            std::printf("Live content red %.2f -> %.2f, blue=%.2f\n",samples[4].red,changed.red,changed.blue);
            if(!(changed.red<40&&changed.blue>60)) failures++;
        }
    }
    surface.Reset(); Pump(100);
    DestroyWindow(backing);
    std::printf("Contrast plain=%.2f r0=%.2f r4=%.2f r12=%.2f r24=%.2f r48=%.2f\n",plain.contrast,samples[0].contrast,samples[1].contrast,samples[2].contrast,samples[3].contrast,samples[4].contrast);
    if(index!=5||samples[0].contrast>=plain.contrast*.9) failures++;
    for(int i=1;i<5;i++) if(!(samples[i].contrast<samples[i-1].contrast*.98&&samples[i].red>70)) failures++;
    if(GetForegroundWindow()!=foreground) failures++;
    std::printf("LiveAdjustableBlur=%s ForegroundPreserved=%s\n",failures?"FAIL":"PASS",GetForegroundWindow()==foreground?"true":"false");
    return failures?1:0;
}

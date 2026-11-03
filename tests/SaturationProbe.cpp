#define main BlurProbeUnusedMain
#include "BlurProbe.cpp"
#undef main

void PaintColor(HWND hwnd,bool blue=false) {
    SetWindowLongPtrW(hwnd,GWLP_USERDATA,blue?2:1);
    HDC dc=GetDC(hwnd);
    HBRUSH brush=CreateSolidBrush(blue?RGB(40,80,224):RGB(224,64,32));
    RECT rect{0,0,640,256}; FillRect(dc,&rect,brush); DeleteObject(brush); ReleaseDC(hwnd,dc);
}
LRESULT CALLBACK ColorProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto color=GetWindowLongPtrW(hwnd,GWLP_USERDATA);
    if(msg==WM_PAINT&&color) {
        PAINTSTRUCT paint{}; HDC dc=BeginPaint(hwnd,&paint); RECT area{};GetClientRect(hwnd,&area);
        HBRUSH brush=CreateSolidBrush(color==2?RGB(40,80,224):RGB(224,64,32));FillRect(dc,&area,brush);DeleteObject(brush);EndPaint(hwnd,&paint);return 0;
    }
    return Proc(hwnd,msg,wp,lp);
}
COLORREF ReadPixel(int x,int y) { HDC dc=GetDC(nullptr); COLORREF c=GetPixel(dc,x,y); ReleaseDC(nullptr,dc); return c; }
int Chroma(COLORREF c) { return std::max({GetRValue(c),GetGValue(c),GetBValue(c)})-std::min({GetRValue(c),GetGValue(c),GetBValue(c)}); }
#ifndef PENUMBRA_SATURATION_HELPERS_ONLY
int main() {
    HWND foreground=GetForegroundWindow();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW wc{}; wc.hInstance=GetModuleHandleW(nullptr); wc.lpfnWndProc=ColorProc; wc.lpszClassName=L"Penumbra.SaturationProbe"; RegisterClassW(&wc);
    HWND backing=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TOPMOST,wc.lpszClassName,L"Saturation probe",WS_POPUP|WS_CLIPCHILDREN,16,16,640,256,nullptr,nullptr,wc.hInstance,nullptr);
    SetWindowPos(backing,HWND_TOPMOST,16,16,640,256,SWP_NOACTIVATE|SWP_SHOWWINDOW); PaintColor(backing); Pump(200);
    COLORREF plain=ReadPixel(200,160);
    if(Chroma(plain)<150) { DestroyWindow(backing); std::puts("SKIP unreadable fixture"); return 77; }
    HWND host=CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"Wallpaper host branch",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,0,640,256,backing,nullptr,wc.hInstance,nullptr);
    PaintColor(host);
    HWND icon=CreateWindowExW(WS_EX_NOACTIVATE,wc.lpszClassName,L"Untouched foreground icon",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS,24,24,32,32,backing,nullptr,wc.hInstance,nullptr);
    SetWindowPos(icon,HWND_TOP,24,24,32,32,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    PaintColor(icon,true); Pump(100); COLORREF iconPlain=ReadPixel(48,48);
    RECT iconRect{};GetWindowRect(icon,&iconRect);
    std::printf("Icon=%p rect=%ld,%ld,%ld,%ld visible=%d\n",icon,iconRect.left,iconRect.top,iconRect.right,iconRect.bottom,IsWindowVisible(icon));
    std::printf("IconReference=%u,%u,%u\n",GetRValue(iconPlain),GetGValue(iconPlain),GetBValue(iconPlain));
    desktop_blur::Engine engine; desktop_blur::Engine::Surface surface;
    surface.hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,wc.lpszClassName,L"",WS_CHILD,0,0,640,256,host,nullptr,wc.hInstance,nullptr);
    HRESULT hr=engine.Attach(surface.hwnd,640,256,1,surface,desktop_blur::Filter::Saturation);
    if(FAILED(hr)) { std::printf("Attach %08lX %ls\n",static_cast<unsigned long>(hr),engine.stage); surface.Reset(); DestroyWindow(backing); return 1; }
    SetWindowPos(surface.hwnd,HWND_TOP,0,0,640,256,SWP_NOACTIVATE|SWP_SHOWWINDOW); surface.visual->put_Opacity(1);
    unsigned failures=0;
    auto hwnd=surface.hwnd; auto brush=surface.effect.Get(); int previous=300;
    for(float saturation:{1.f,.75f,.5f,.25f,0.f}) {
        engine.SetRadius(surface,saturation); Pump(180); COLORREF value=ReadPixel(200,160);
        std::printf("Saturation=%.2f RGB=%u,%u,%u Chroma=%d IconUnchanged=%s\n",saturation,GetRValue(value),GetGValue(value),GetBValue(value),Chroma(value),ReadPixel(48,48)==iconPlain?"true":"false");
        if(Chroma(value)>=previous||ReadPixel(48,48)!=iconPlain||surface.hwnd!=hwnd||surface.effect.Get()!=brush) failures++;
        if(saturation==1&&value!=plain) failures++;
        if(saturation==0&&Chroma(value)>1) failures++;
        previous=Chroma(value);
    }
    PaintColor(host,true); Pump(180); COLORREF changed=ReadPixel(200,160);
    if(Chroma(changed)>1||GetRValue(changed)<60) failures++;
    std::printf("LiveGrayscale=%u,%u,%u\n",GetRValue(changed),GetGValue(changed),GetBValue(changed));
    PaintColor(host); previous=-1;
    for(float opacity:{1.f,.75f,.5f,.25f,0.f}) {
        surface.visual->put_Opacity(opacity); Pump(100); int chroma=Chroma(ReadPixel(200,160));
        std::printf("FadeOpacity=%.2f Chroma=%d\n",opacity,chroma);
        if(chroma<=previous) failures++; previous=chroma;
    }
    engine.SetRadius(surface,1); surface.visual->put_Opacity(1); Pump(120);
    if(ReadPixel(200,160)!=plain) failures++;
    surface.Reset();
    // Checked darken-icons mode uses the same desktop popup as production,
    // above BOTH the wallpaper branch and the separate icon sibling.
    // Only disposable own windows are sampled; no Explorer settings change.
    surface.hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,
        wc.lpszClassName,L"Desktop color including icon",WS_POPUP,16,16,640,256,backing,nullptr,wc.hInstance,nullptr);
    if(!surface.hwnd||!SetLayeredWindowAttributes(surface.hwnd,0,255,LWA_ALPHA)) { DestroyWindow(backing);return 1; }
    hr=engine.Attach(surface.hwnd,640,256,1,surface,desktop_blur::Filter::Saturation);
    if(FAILED(hr)) { std::printf("DesktopColorAttach=%08lX %ls\n",static_cast<unsigned long>(hr),engine.stage);surface.Reset();DestroyWindow(backing);return 1; }
    SetWindowPos(surface.hwnd,HWND_TOPMOST,16,16,640,256,SWP_NOACTIVATE|SWP_SHOWWINDOW);surface.visual->put_Opacity(1);
    int previousIcon=300,previousWallpaper=300;
    for(float saturation:{1.f,.75f,.5f,.25f,0.f}) {
        engine.SetRadius(surface,saturation);Pump(150);
        auto wallpaperValue=ReadPixel(200,160),iconValue=ReadPixel(48,48);
        int wallpaperChroma=Chroma(wallpaperValue),iconChroma=Chroma(iconValue);
        std::printf("IncludingIconsSaturation=%.2f WallpaperChroma=%d IconRGB=%u,%u,%u IconChroma=%d\n",saturation,wallpaperChroma,GetRValue(iconValue),GetGValue(iconValue),GetBValue(iconValue),iconChroma);
        if(wallpaperChroma>=previousWallpaper||iconChroma>=previousIcon) failures++;
        if(saturation==1&&(wallpaperValue!=plain||iconValue!=iconPlain)) failures++;
        if(saturation==0&&(wallpaperChroma>1||iconChroma>1)) failures++;
        previousIcon=iconChroma;previousWallpaper=wallpaperChroma;
    }
    previousIcon=-1;
    for(float opacity:{1.f,.75f,.5f,.25f,0.f}) {
        surface.visual->put_Opacity(opacity);Pump(100);int chroma=Chroma(ReadPixel(48,48));
        std::printf("IncludingIconsFadeOpacity=%.2f IconChroma=%d\n",opacity,chroma);
        if(chroma<=previousIcon) failures++;previousIcon=chroma;
    }
    engine.SetRadius(surface,1);surface.visual->put_Opacity(1);Pump(100);
    if(ReadPixel(48,48)!=iconPlain) failures++;
    surface.Reset();Pump(100);
    if(ReadPixel(48,48)!=iconPlain) failures++;
    DestroyWindow(backing); Pump(100);
    if(GetForegroundWindow()!=foreground) failures++;
    std::printf("SaturationProbe=%s Failures=%u ForegroundPreserved=%s\n",failures?"FAIL":"PASS",failures,GetForegroundWindow()==foreground?"true":"false");
    return failures?1:0;
}
#endif

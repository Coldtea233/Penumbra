// Short, non-activating on-screen probe: verify that Windows changes the
// per-pixel layered surface's constant alpha without uploading its bitmap.
#define wWinMain PenumbraApplicationMain
#include "../src/Nocturne.Renderer/main.cpp"
#undef wWinMain
#include <dwmapi.h>
LRESULT CALLBACK WhiteBacking(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_PAINT) {
        PAINTSTRUCT paint{}; HDC dc=BeginPaint(hwnd,&paint);
        FillRect(dc,&paint.rcPaint,(HBRUSH)GetStockObject(WHITE_BRUSH)); EndPaint(hwnd,&paint); return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
void PumpCompositor() {
    ULONGLONG end=GetTickCount64()+100;
    while(GetTickCount64()<end) {
        MsgWaitForMultipleObjectsEx(0,nullptr,10,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        MSG msg; while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    DwmFlush();
}
int main() {
    HWND originalForeground=GetForegroundWindow();
    instance=GetModuleHandleW(nullptr);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW wc{}; wc.hInstance=instance; wc.lpfnWndProc=WhiteBacking;
    wc.lpszClassName=L"Penumbra.OpacityProbe"; RegisterClassW(&wc);
    HWND backing=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TOPMOST,wc.lpszClassName,L"",WS_POPUP,16,16,96,96,nullptr,nullptr,instance,nullptr);
    SetWindowPos(backing,HWND_TOPMOST,16,16,96,96,SWP_NOACTIVATE|SWP_SHOWWINDOW); UpdateWindow(backing); PumpCompositor();
    HDC bg=GetDC(backing); RECT area{0,0,96,96}; FillRect(bg,&area,(HBRUSH)GetStockObject(WHITE_BRUSH)); ReleaseDC(backing,bg);
    wc.lpszClassName=L"Nocturne.Surface"; wc.lpfnWndProc=SurfaceProc; RegisterClassW(&wc);
    Surface s; s.rect={16,16,112,112}; CreateSurface(s);
    Bitmap bitmap; bitmap.Create(96,96); std::fill(bitmap.pixels,bitmap.pixels+96*96,0xFF000000u);
    for(int y=16;y<32;y++) for(int x=16;x<32;x++) bitmap.pixels[y*96+x]=0;
    Submit(s,bitmap,255); SetWindowPos(s.hwnd,HWND_TOPMOST,16,16,96,96,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    HDC screen=GetDC(nullptr);
    int failed=0;
    // The first sample must see the white fixture through a transparent mask.
    // Remote/occluded capture sessions can return a stale desktop instead.
    for(DWORD flags:{(DWORD)ULW_ALPHA}) {
        for(BYTE alpha:{(BYTE)0,(BYTE)64,(BYTE)128,(BYTE)255}) {
            BLENDFUNCTION blend{AC_SRC_OVER,0,alpha,AC_SRC_ALPHA};
            BOOL ok=UpdateLayeredWindow(s.hwnd,nullptr,nullptr,nullptr,nullptr,nullptr,0,&blend,flags);
            DWORD error=GetLastError(); PumpCompositor();
            COLORREF pixel=GetPixel(screen,64,64);
            COLORREF hole=GetPixel(screen,40,40);
            if(alpha==0&&GetRValue(hole)<247) {
                ReleaseDC(nullptr,screen); DropSurface(s); DestroyWindow(backing);
                std::puts("SKIP: white test backing is not visible in screen capture; no visual verdict.");
                return 77;
            }
            std::printf("flags=%lu alpha=%u ok=%d error=%lu pixel=%lu\n",flags,alpha,ok,error,pixel);
            if(flags==ULW_ALPHA&&(!ok||std::abs((int)GetRValue(pixel)-(255-alpha))>8)) failed++;
            if(GetRValue(hole)<247) failed++;
        }
    }
    ReleaseDC(nullptr,screen); DropSurface(s); DestroyWindow(backing);
    if(GetForegroundWindow()!=originalForeground) failed++;
    std::printf("VisualAlphaAndFixedCutouts=%s ForegroundPreserved=%s\n",failed?"FAIL":"PASS",GetForegroundWindow()==originalForeground?"true":"false");
    return failed?1:0;
}

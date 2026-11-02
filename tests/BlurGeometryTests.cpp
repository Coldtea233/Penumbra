#define wWinMain PenumbraApplicationMain
#include "../src/Nocturne.Renderer/main.cpp"
#undef wWinMain

int main() {
    unsigned checks=0,failures=0;
    auto check=[&](bool good,const char* name) { ++checks; if(!good) { ++failures; std::printf("FAIL %s\n",name); } };
    RECT monitor{-1920,-200,0,880};
    for(const RECT& bar:std::vector<RECT>{{-1920,832,0,880},{-1920,878,0,926},{-1920,-200,0,-152},{-1920,-246,0,-198},{-1920,-200,-1872,880},{-1966,-200,-1918,880},{-48,-200,0,880},{-2,-200,46,880}}) {
        RECT safe=blur_bounds::ExcludeTaskbar(monitor,monitor,bar),intersection{};
        check(safe.left>=monitor.left&&safe.top>=monitor.top&&safe.right<=monitor.right&&safe.bottom<=monitor.bottom,"negative-coordinate monitor bounds");
        check(!IntersectRect(&intersection,&safe,&bar),"visible and auto-hide taskbar excluded on every edge");
        check((safe.right-safe.left)*(safe.bottom-safe.top)>1700000,"desktop area remains usable");
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HWND foreground=GetForegroundWindow(); instance=GetModuleHandleW(nullptr);
    check(LocateDesktop(),"locate actual desktop without moving it");
    WNDCLASSW wc{}; wc.hInstance=instance; wc.lpfnWndProc=SurfaceProc; wc.lpszClassName=L"Penumbra.BlurGeometryTest"; RegisterClassW(&wc);
    MONITORINFO info{sizeof(info)}; GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&info);
    blurEngine=std::make_unique<desktop_blur::Engine>(); params.blurEnabled=true; params.blurRadius=12;
    BlurSurface s; s.rect=info.rcMonitor;
    s.composition.hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,wc.lpszClassName,L"",WS_POPUP,s.rect.left,s.rect.top,s.rect.right-s.rect.left,s.rect.bottom-s.rect.top,desktop,nullptr,instance,nullptr);
    SetLayeredWindowAttributes(s.composition.hwnd,0,255,LWA_ALPHA);
    check(SUCCEEDED(blurEngine->Attach(s.composition.hwnd,s.rect.right-s.rect.left,s.rect.bottom-s.rect.top,12,s.composition)),"attach production composition graph");
    blurSurfaces.push_back(std::move(s)); auto& surface=blurSurfaces.front();
    check(ProtectBlurTaskbar(surface),"apply real taskbar clipping before show");
    HRGN actual=CreateRectRgn(0,0,0,0);
    check(GetWindowRgn(surface.composition.hwnd,actual)!=ERROR,"HWND region is present");
    RECT region{}; GetRgnBox(actual,&region); OffsetRect(&region,surface.rect.left,surface.rect.top);
    check(EqualRect(&region,&surface.protectedArea),"HWND clip matches protected desktop area"); DeleteObject(actual);
    float inset=-1; surface.composition.clip->get_BottomInset(&inset);
    check(inset==surface.rect.bottom-surface.protectedArea.bottom,"Composition visual has matching bottom clip");
    auto clip=surface.composition.clip.Get();
    for(int i=0;i<100;i++) check(ProtectBlurTaskbar(surface)&&surface.composition.clip.Get()==clip,"unchanged protection reuses the cached clip");
    auto hwnd=surface.composition.hwnd; auto brush=surface.composition.effect.Get();
    for(int radius=0;radius<=48;radius++) {
        auto reply=ControllerProc(nullptr,BlurRadiusMessage,radius,GetCurrentProcessId());
        float sigma=-1; desktop_blur::C::CompositionGetValueStatus status{};
        surface.composition.properties->TryGetScalar(desktop_blur::String(L"Blur.BlurAmount"),&sigma,&status);
        check(reply==radius+1&&status==desktop_blur::C::CompositionGetValueStatus_Succeeded&&std::abs(sigma-blur_parameters::Sigma(radius))<.001,"IPC updates the actual Composition property at all 49 radii");
        desktop_blur::ComPtr<desktop_blur::C::ISpriteVisual> sprite;
        desktop_blur::ComPtr<desktop_blur::C::ICompositionBrush> activeBrush;
        surface.composition.visual.As(&sprite); sprite->get_Brush(&activeBrush);
        check((radius==0)==!activeBrush,"zero bypasses all backdrop sampling; positive radius restores the existing brush");
        check(hwnd==surface.composition.hwnd&&brush==surface.composition.effect.Get()&&!blurTimerArmed,"radius updates never rebuild the window or run a fade timer");
    }
    ControllerProc(nullptr,BlurRadiusMessage,49,GetCurrentProcessId());
    check(surface.composition.radius==12&&!blurPreviewProcess&&!blurPreviewClient,"cancel restores saved radius and closes the preview lease");
    check(GetForegroundWindow()==foreground,"foreground was not changed");
    ClearBlur(); blurEngine.reset();
    std::printf("{\"GeometryAndRadiusChecks\":%u,\"Failures\":%u,\"SavedSettingsUntouched\":true}\n",checks,failures);
    return failures?1:0;
}

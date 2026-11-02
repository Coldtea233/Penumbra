// Production color state, IPC and animation tests on disposable own windows.
// No foreground changes, Wallpaper Engine configuration or user settings writes.
#define wWinMain PenumbraApplicationMain
#include "../src/Nocturne.Renderer/main.cpp"
#undef wWinMain

int main() {
    unsigned checks=0,failures=0;
    auto check=[&](bool good,const char* name){checks++;if(!good){failures++;std::printf("FAIL %s\n",name);}};
    check(!params.saturationEnabled&&params.saturation==40,"color effect is opt-in");
    check(!wallpaper_layer::OpenEngine(GetCurrentProcessId()),"unrelated process is not Wallpaper Engine");
    for(bool enabled:{false,true}) for(bool paused:{false,true}) for(bool present:{false,true})
    for(auto kind:{ForegroundKind::Desktop,ForegroundKind::Application,ForegroundKind::Transient}) for(bool previous:{false,true})
        check(WantsDesktopBlur(enabled,paused,present,kind,previous)==(enabled&&!paused&&present&&(kind==ForegroundKind::Application||(kind==ForegroundKind::Transient&&previous))),"color activation conjunction");
    wchar_t name[128],path[32768];swprintf(name,128,L"tests\\bin\\SaturationSettings-%lu.ini",GetCurrentProcessId());
    GetFullPathNameW(name,32768,path,nullptr);configFile=path;
    WritePrivateProfileStringW(L"vignette",L"blurEnabled",L"1",configFile.c_str());Load();
    check(!params.saturationEnabled&&params.saturation==40&&params.blurEnabled,"old INI retains old options and color defaults");
    WritePrivateProfileStringW(L"vignette",L"saturationEnabled",L"1",configFile.c_str());
    WritePrivateProfileStringW(L"vignette",L"saturation",L"25",configFile.c_str());Load();
    check(params.saturationEnabled&&params.saturation==25,"color settings load");
    for(const wchar_t* value:{L"NaN",L"inf",L"invalid"}) {
        WritePrivateProfileStringW(L"vignette",L"saturation",value,configFile.c_str());Load();check(params.saturation==40,"invalid saturation safely falls back");
    }
    DeleteFileW(configFile.c_str());configFile.clear();params=Params{};params.saturationEnabled=true;
    if(!desktop_blur::TransparencyAllowed()) { std::printf("SKIP Composition properties, accessibility policy; StateChecks=%u Failures=%u\n",checks,failures);return failures?1:77; }
    instance=GetModuleHandleW(nullptr);
    HWND host=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"Color test host",WS_POPUP,-10000,-10000,64,64,nullptr,nullptr,instance,nullptr);
    ShowWindow(host,SW_SHOWNOACTIVATE);
    HWND wallpaper=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Color test wallpaper",WS_CHILD|WS_VISIBLE,0,0,64,64,host,nullptr,instance,nullptr);
    ColorSurface color;color.layer={wallpaper,host,GetCurrentProcessId(),{-10000,-10000,-9936,-9936}};color.rect=color.layer.bounds;
    color.composition.hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,L"STATIC",L"Color test filter",WS_CHILD,0,0,64,64,host,nullptr,instance,nullptr);
    blurEngine=std::make_unique<desktop_blur::Engine>();
    HRESULT hr=blurEngine->Attach(color.composition.hwnd,64,64,.4f,color.composition,desktop_blur::Filter::Saturation);
    check(SUCCEEDED(hr),"native saturation graph attaches");
    if(SUCCEEDED(hr)) {
        colorSurfaces.push_back(std::move(color));wallpaperProcess=OpenProcess(SYNCHRONIZE,FALSE,GetCurrentProcessId());
        auto& surface=colorSurfaces.front().composition;HWND hwnd=surface.hwnd;auto brush=surface.effect.Get();
        for(unsigned value=0;value<=100;value++) {
            check(ControllerProc(nullptr,ColorValueMessage,value,GetCurrentProcessId())==value+1,"IPC acknowledges every saturation value");
            float actual=-1;desktop_blur::C::CompositionGetValueStatus status{};
            surface.properties->TryGetScalar(desktop_blur::String(L"Color.Saturation"),&actual,&status);
            check(std::abs(actual-value/100.f)<1e-6,"live Composition saturation matches IPC value");
            check(ControllerProc(nullptr,ColorValueMessage,102,0)==value+1,"query reports actual saturation");
            check(surface.hwnd==hwnd&&surface.effect.Get()==brush&&!colorTimerArmed,"slider preserves graph and does not start an animation");
        }
        desktop_blur::ComPtr<desktop_blur::C::ISpriteVisual> sprite;surface.visual.As(&sprite);
        desktop_blur::ComPtr<desktop_blur::C::ICompositionBrush> active;sprite->get_Brush(&active);
        check(!active,"100 percent bypasses the filter completely");
        ControllerProc(nullptr,ColorValueMessage,101,GetCurrentProcessId());
        check(std::abs(surface.radius-.4f)<1e-6&&!colorPreviewProcess,"cancelling preview restores saved saturation and releases lease");
        SetColorVisibility(true);check(colorTimerArmed&&colorTransition.Target(),"cold start animates from transparent");
        double now=ClockMs();colorTransition.Retarget(false,now+90);double before=colorTransition.Value(now+100);
        colorTransition.Retarget(true,now+100);check(std::abs(colorTransition.Value(now+100)-before)<1e-12,"rapid reversal is continuous");
        colorTransition.Reset(true);AdvanceColorAnimation();check(!colorTimerArmed&&!colorAnimationTick,"visible endpoint closes frame timer");
        colorTransition.Reset(false);AdvanceColorAnimation();check(!IsWindowVisible(hwnd)&&!colorTimerArmed,"hidden endpoint is invisible without a timer");
        params.saturationEnabled=false;check(RefreshColorLayers()&&colorSurfaces.empty()&&!wallpaperProcess,"disabled feature releases all color resources");
    } else color.composition.Reset();
    ClearColor();blurEngine.reset();DestroyWindow(host);
    check(colorSurfaces.empty()&&!colorPreviewProcess&&!colorAnimationTick,"idempotent cleanup");
    std::printf("{\"SaturationStateChecks\":%u,\"Failures\":%u,\"UserSettingsUntouched\":true}\n",checks,failures);
    return failures?1:0;
}

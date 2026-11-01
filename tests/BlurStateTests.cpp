// Exhaustively test the conjunction and independent vignette behavior without
// changing foreground focus, opening UI, or reading/writing user settings.
#define wWinMain PenumbraApplicationMain
#include "../src/Nocturne.Renderer/main.cpp"
#undef wWinMain

int main() {
    unsigned checks=0,failures=0;
    for(bool enabled:{false,true}) for(bool isPaused:{false,true})
    for(bool present:{false,true}) for(auto kind:{ForegroundKind::Desktop,ForegroundKind::Application,ForegroundKind::Transient}) for(bool previous:{false,true}) {
        bool expected=enabled&&!isPaused&&present&&(kind==ForegroundKind::Application||(kind==ForegroundKind::Transient&&previous));
        if(WantsDesktopBlur(enabled,isPaused,present,kind,previous)!=expected) failures++;
        checks++;
    }
    auto check=[&](bool good,const char* name) {
        checks++; if(!good) { failures++; std::printf("FAIL %s\n",name); }
    };
    params=Params{};
    check(WantsDesktopBlur(true,false,true,ForegroundKind::Transient,true),"tray transition preserves a visible blur");
    check(!WantsDesktopBlur(true,false,true,ForegroundKind::Transient,false),"tray transition does not spontaneously enable blur");
    check(!WantsDesktopBlur(true,false,true,ForegroundKind::Desktop,true),"desktop click still disables blur immediately");
    for(int radius=0;radius<=48;radius++) {
        double extra=blur_parameters::AdditionalSigma(radius);
        double effective=std::sqrt(blur_parameters::BaselineSigma*blur_parameters::BaselineSigma+extra*extra);
        check(std::abs(effective-(blur_parameters::BaselineSigma+radius))<1e-9,"linear effective-radius increment");
    }
    check(blur_parameters::AdditionalSigma(0)==0,"zero adds no extra Gaussian variance");
    check(blur_parameters::AdditionalSigma(12)>20,"low radii are no longer buried in the baseline variance");
    check(!params.blurEnabled&&params.blurRadius==12,"old settings keep blur opt-in");
    transition.Reset(true); blurTransition.Reset(false);
    check(transition.Target()&&!blurTransition.Target(),"vignette and blur states are independent");
    blurTransition.Retarget(true,1000);
    check(blurTransition.Value(1000)==0&&blurTransition.Value(1100)>.6,"cold fade begins at transparent and eases out");
    double old=blurTransition.Value(1120);
    blurTransition.Retarget(false,1120);
    check(std::abs(blurTransition.Value(1120)-old)<1e-12,"reverse from current opacity");
    check(transition.Target(),"blur reversal never resets vignette");
    double current=blurTransition.Value(1150);
    blurTransition.Retarget(true,1150);
    check(std::abs(blurTransition.Value(1150)-current)<1e-12,"rapid return is continuous");
    check(blurTransition.Value(2200)==1&&blurTransition.Velocity(2200)==0,"settled endpoint has zero speed");
    params.blurEnabled=true;
    blurPolicyAllowed=desktop_blur::TransparencyAllowed();
    HWND cached=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"",WS_POPUP,-20000,-20000,16,16,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(cached!=nullptr,"prepare non-visible cached surface for theme regression");
    BlurSurface existing; existing.composition.hwnd=cached; blurSurfaces.push_back(std::move(existing));
    ControllerProc(nullptr,WM_THEMECHANGED,0,0);
    check(IsWindow(cached)&&blurSurfaces.size()==1,"unrelated theme broadcast retains cached blur");
    ControllerProc(nullptr,WM_SETTINGCHANGE,0,reinterpret_cast<LPARAM>(L"ImmersiveColorSet"));
    check(IsWindow(cached)&&blurSurfaces.size()==1,"unchanged transparency policy never clears live blur");
    Clear();
    check(!blurTimerArmed&&!blurAnimationTick&&blurSurfaces.empty(),"clear releases all blur surfaces and animation timer");
    std::printf("{\"BlurStateChecks\":%u,\"Failures\":%u,\"UserSettingsUntouched\":true}\n",checks,failures);
    return failures?1:0;
}

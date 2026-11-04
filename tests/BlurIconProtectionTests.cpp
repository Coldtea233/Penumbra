// Production filters on disposable OWN windows, not Explorer. Verify full
// desktop sampling with either icon option and protected-vignette BEFORE blur.
#define UNICODE
#define _UNICODE
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static unsigned placementCalls=0,foreignAnchorAttempts=0;
static HWND ownShellBoundary{};
static BOOL WINAPI CheckedSetWindowPos(HWND window,HWND after,int x,int y,int w,int h,UINT flags) {
    ++placementCalls;
    if(after!=HWND_TOP&&after!=HWND_BOTTOM&&after!=HWND_TOPMOST&&after!=HWND_NOTOPMOST) {
        DWORD pid=0;GetWindowThreadProcessId(after,&pid);
        if(pid!=GetCurrentProcessId()) {++foreignAnchorAttempts;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    }
    // A normal STATIC popup does not have Explorer's shell-bottom behavior.
    // Emulate ONLY that boundary with an own offscreen sentinel. Relative
    // blur/vignette ordering is still performed and checked by real Win32.
    return ::SetWindowPos(window,after==HWND_BOTTOM&&ownShellBoundary?ownShellBoundary:after,x,y,w,h,flags);
}
#define SetWindowPos CheckedSetWindowPos
#define wWinMain PenumbraApplicationMain
#include "../src/Nocturne.Renderer/main.cpp"
#undef wWinMain
#undef SetWindowPos

int main() {
    unsigned checks=0,failures=0;
    auto check=[&](bool result,const char* text){++checks;if(!result){++failures;std::printf("FAIL %s\n",text);}};
    instance=GetModuleHandleW(nullptr);
    HWND foreground=GetForegroundWindow();
    std::vector<Surface> monitors(3);
    monitors[0].rect={-1920,-200,0,880};monitors[1].rect={0,0,1920,1080};monitors[2].rect={1920,0,5760,2160};
    for(bool darken:{false,true}) {
        params.darkenIcons=darken;
        auto plan=PlanBlurSurfaces(monitors);
        check(plan.size()==3,"both icon options blur every monitor without needing WE");
        for(size_t i=0;i<plan.size();i++) check(EqualRect(&plan[i].rect,&monitors[i].rect),"entire monitor includes icons, negative coordinates and 4K");
    }
    std::vector<wallpaper_layer::Layer> colorLayers{
        {nullptr,nullptr,1,{-1800,-100,-100,800}},
        {nullptr,nullptr,1,{0,0,960,1080}},
        {nullptr,nullptr,1,{960,0,1920,1080}}};
    params.darkenIcons=false;
    auto protectedColors=PlanColorSurfaces(monitors,colorLayers);
    check(protectedColors.size()==3,"unchecked option keeps each wallpaper-only intersection");
    for(const auto& color:protectedColors) check(!color.includeIcons,"unchecked option excludes icon branch");
    params.darkenIcons=true;
    auto includedColors=PlanColorSurfaces(monitors,colorLayers);
    check(includedColors.size()==2,"checked option creates one color filter per WE-backed monitor without double saturation");
    for(size_t i=0;i<includedColors.size();i++)
        check(includedColors[i].includeIcons&&EqualRect(&includedColors[i].rect,&monitors[i].rect),"checked option includes full desktop and negative coordinates");
    check(PlanColorSurfaces(monitors,{}).empty(),"color still waits for Wallpaper Engine");
    params=Params{};params.blurEnabled=true;params.blurRadius=12;
    if(!desktop_blur::TransparencyAllowed()) {std::printf("SKIP Composition: policy disabled; Checks=%u Failures=%u\n",checks,failures);return failures?1:77;}
    WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=SurfaceProc;wc.lpszClassName=L"Nocturne.BlurSurface";RegisterClassW(&wc);
    wc.lpszClassName=L"Nocturne.Surface";RegisterClassW(&wc);
    wc.lpszClassName=L"Nocturne.ColorSurface";RegisterClassW(&wc);
    desktop=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"Own test desktop",WS_POPUP,-10000,-10000,96,64,nullptr,nullptr,instance,nullptr);
    ShowWindow(desktop,SW_SHOWNOACTIVATE);
    ownShellBoundary=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"Own shell boundary",WS_POPUP,-10200,-10200,1,1,nullptr,nullptr,instance,nullptr);
    ::SetWindowPos(ownShellBoundary,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
    RECT bounds{};GetWindowRect(desktop,&bounds);
    iconMask.rect=bounds;iconMask.width=96;iconMask.height=64;iconMask.bits.assign(96*64/8,0);
    for(int y=4;y<20;y++) for(int x=4;x<20;x++) {size_t i=y*96+x;iconMask.bits[i/8]|=1u<<(i%8);}
    Bitmap protectedBitmap;check(protectedBitmap.Create(96,64),"prepare vignette and original icon mask");
    FillVignette(protectedBitmap);auto before=protectedBitmap.pixels[8*96+8];ProtectIcons(protectedBitmap,bounds);
    check(before!=0&&protectedBitmap.pixels[8*96+8]==0,"disabled darken option removes direct vignette contribution at icon pixels");
    check(protectedBitmap.pixels[1*96+1]!=0,"wallpaper still receives vignette");
    params.darkenIcons=true;FillVignette(protectedBitmap);ProtectIcons(protectedBitmap,bounds);
    check(protectedBitmap.pixels[8*96+8]==before,"enabled darken option dims icons normally");
    params.darkenIcons=false;
    Surface vignette;vignette.rect=bounds;surfaces.push_back(std::move(vignette));transition.Reset(true);
    check(Render(surfaces.front())&&PositionSurface(surfaces.front(),true),"production protected vignette uploads");
    check(RefreshBlurLayers()&&blurSurfaces.size()==1,"production full-desktop Gaussian attaches with icon protection and no WE");
    if(!blurSurfaces.empty()) {
        auto& current=blurSurfaces.front();auto window=current.composition.hwnd;auto brush=current.composition.effect.Get();
        check(GetWindow(window,GW_OWNER)==desktop&&!(GetWindowLongPtrW(window,GWL_STYLE)&WS_CHILD),"blur is above whole desktop, not a wallpaper-only child");
        blurTransition.Reset(true);AdvanceBlurAnimation();
        check(IsWindowVisible(window)&&WindowAbove(window,surfaces.front().hwnd)&&WindowAbove(surfaces.front().hwnd,desktop),"strict order: desktop/icons, protected vignette, then Gaussian");
        check(!blurTimerArmed&&!blurAnimationTick,"visible endpoint has no frame timer");
        check(current.composition.brushAttached,"visible blur samples its backdrop");
        float opacityBefore=0;current.composition.visual->get_Opacity(&opacityBefore);
        current.occluded=true;
        check(PositionBlurSurfaces()&&!IsWindowVisible(window)&&!current.composition.brushAttached,"occluded monitor hides and detaches the blur graph");
        for(int radius=0;radius<=48;radius++)
            check(SetLiveBlurRadius(static_cast<float>(radius))&&!current.composition.brushAttached,"editing radius cannot wake an occluded monitor");
        check(SetLiveBlurRadius(12),"restore configured radius while occluded");
        current.occluded=false;
        check(PositionBlurSurfaces()&&IsWindowVisible(window)&&current.composition.brushAttached,"uncovering restores sampling before the same HWND is shown");
        float opacityAfter=0;current.composition.visual->get_Opacity(&opacityAfter);
        check(current.composition.effect.Get()==brush&&current.composition.hwnd==window&&opacityAfter==opacityBefore&&!blurTimerArmed,"unculling preserves objects and settled animation");
        current.occluded=true;
        locationHook=moveSizeHook=reinterpret_cast<HWINEVENTHOOK>(1);
        check(UpdateRenderBudget(ForegroundKind::Transient)&&!current.occluded,"shell UI always restores a previously occluded monitor");
        current.occluded=true;locationHook=nullptr;
        check(UpdateRenderBudget(ForegroundKind::Application)&&!current.occluded,"missing location hook disables fullscreen culling safely");
        moveSizeHook=nullptr;
        check(PositionBlurSurfaces()&&current.composition.brushAttached,"fail-open policy restores the original graph");
        unsigned placed=placementCalls;
        for(int i=0;i<100;i++) check(PositionSurface(surfaces.front(),true)&&PositionBlurSurfaces(),"unchanged placement succeeds");
        check(placed==placementCalls,"100 unchanged pair checks never restack live backdrop");
        ::SetWindowPos(surfaces.front().hwnd,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER);
        check(WindowAbove(surfaces.front().hwnd,window),"simulate wrong ordering on own offscreen windows");
        check(PositionBlurSurfaces()&&WindowAbove(window,surfaces.front().hwnd),"recovery repairs inverted vignette/blur order");
        for(bool darken:{true,false}) {
            params.darkenIcons=darken;
            check(RefreshBlurLayers()&&blurSurfaces.front().composition.hwnd==window,"darken toggle never disables blur or switches to wallpaper-only sampling");
            check(WindowAbove(window,surfaces.front().hwnd),"both options keep blur above vignette");
        }
        for(int radius=0;radius<=48;radius++) {
            check(SetLiveBlurRadius(static_cast<float>(radius)),"all radii update the existing whole-desktop filter");
            float sigma=-1;desktop_blur::C::CompositionGetValueStatus status{};
            current.composition.properties->TryGetScalar(desktop_blur::String(L"Blur.BlurAmount"),&sigma,&status);
            check(status==desktop_blur::C::CompositionGetValueStatus_Succeeded&&sigma==radius,"actual Gaussian property tracks slider");
            desktop_blur::ComPtr<desktop_blur::C::ISpriteVisual> sprite;current.composition.visual.As(&sprite);
            desktop_blur::ComPtr<desktop_blur::C::ICompositionBrush> applied;sprite->get_Brush(&applied);
            check(radius?applied.Get()!=nullptr:applied.Get()==nullptr,"zero detaches brush; positive radius samples desktop including icons");
            check(window==current.composition.hwnd&&brush==current.composition.effect.Get()&&!blurTimerArmed,"radius edit reuses HWND/brush and no timer");
        }
        check(ControllerProc(nullptr,ColorValueMessage,104,GetCurrentProcessId())==0&&IsWindowVisible(window),"raw wallpaper refresh does not hide unrelated desktop blur");
        capturingPreview=true;PositionBlurSurfaces();check(!IsWindowVisible(window)&&!current.composition.brushAttached,"explicit suppression hides and detaches sampling");
        capturingPreview=false;PositionSurface(surfaces.front(),true);PositionBlurSurfaces();
        check(IsWindowVisible(window)&&WindowAbove(window,surfaces.front().hwnd),"restoring either layer restores correct order");
        blurTransition.Reset(false);AdvanceBlurAnimation();
        check(!IsWindowVisible(window)&&!blurTimerArmed,"hidden endpoint releases frame timer");
        SetBlurVisibility(true);check(blurTimerArmed&&blurTransition.Value(ClockMs())<.15,"cold fade starts transparent");
        double value=blurTransition.Value(ClockMs());SetBlurVisibility(false);
        check(std::abs(blurTransition.Value(ClockMs())-value)<.04,"rapid reversal remains continuous");
        blurTransition.Reset(true);AdvanceBlurAnimation();
        check(WindowAbove(window,surfaces.front().hwnd),"animation endpoint preserves ordering");
    }
    HWND wallpaper=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Own color wallpaper",WS_CHILD|WS_VISIBLE,0,0,96,64,desktop,nullptr,instance,nullptr);
    for(bool includeIcons:{false,true,false,true}) {
        ClearColor();params.saturationEnabled=true;params.darkenIcons=includeIcons;params.saturation=40;
        ColorSurface color;color.includeIcons=includeIcons;color.rect=bounds;color.layer={wallpaper,desktop,GetCurrentProcessId(),bounds};
        HRESULT hr=CreateColorSurface(color);
        check(SUCCEEDED(hr),"production color factory supports both icon modes");
        if(FAILED(hr)) {color.composition.Reset();continue;}
        auto colorWindow=color.composition.hwnd;auto brush=color.composition.effect.Get();
        check(includeIcons?GetWindow(colorWindow,GW_OWNER)==desktop&&!(GetWindowLongPtrW(colorWindow,GWL_STYLE)&WS_CHILD):GetParent(colorWindow)==desktop&&(GetWindowLongPtrW(colorWindow,GWL_STYLE)&WS_CHILD),"icon checkbox selects popup or wallpaper child sampling");
        colorSurfaces.push_back(std::move(color));wallpaperProcess=OpenProcess(SYNCHRONIZE,FALSE,GetCurrentProcessId());
        colorTransition.Reset(true);AdvanceColorAnimation();
        check(IsWindowVisible(colorWindow)&&!colorTimerArmed&&!colorAnimationTick,"color is retained at rest without frame timer");
        auto& retained=colorSurfaces.front();
        check(retained.composition.brushAttached,"visible color filter samples backdrop");
        retained.occluded=true;
        check(PositionColorSurfaces()&&!IsWindowVisible(colorWindow)&&!retained.composition.brushAttached,"fullscreen detaches color with either icon mode");
        check(SetLiveSaturation(25)&&!retained.composition.brushAttached,"saturation edits stay dormant while fully occluded");
        retained.occluded=false;
        check(PositionColorSurfaces()&&IsWindowVisible(colorWindow)&&retained.composition.brushAttached&&retained.composition.effect.Get()==brush,"color unculls without rebuilding or changing icon policy");
        check(SetLiveSaturation(40),"restore color parameter for remaining regressions");
        if(includeIcons) {
            check(WindowAbove(blurSurfaces.front().composition.hwnd,surfaces.front().hwnd)&&WindowAbove(surfaces.front().hwnd,colorWindow)&&WindowAbove(colorWindow,desktop),"strict color/icons -> vignette -> whole-desktop blur order");
            ::SetWindowPos(colorWindow,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER);
            check(PositionSurface(surfaces.front(),true)&&WindowAbove(surfaces.front().hwnd,colorWindow),"vignette recovery repairs inverted color order");
        }
        unsigned stable=placementCalls;
        for(int i=0;i<100;i++) check(PositionColorSurfaces()&&PositionBlurSurfaces()&&PositionSurface(surfaces.front(),true),"stable three-effect placement succeeds");
        check(stable==placementCalls,"100 unchanged three-effect checks never restack");
        for(unsigned value=0;value<=100;value++) {
            check(SetLiveSaturation(static_cast<float>(value)),"saturation updates for either icon mode");
            float actual=-1;desktop_blur::C::CompositionGetValueStatus status{};
            colorSurfaces.front().composition.properties->TryGetScalar(desktop_blur::String(L"Color.Saturation"),&actual,&status);
            check(status==desktop_blur::C::CompositionGetValueStatus_Succeeded&&std::abs(actual-value/100.f)<1e-6,"icons and wallpaper use exact adjustable saturation");
            check(colorWindow==colorSurfaces.front().composition.hwnd&&brush==colorSurfaces.front().composition.effect.Get()&&!colorTimerArmed,"slider reuses retained color graph without new timer");
        }
        check(!IsWindowVisible(colorWindow),"100 percent completely hides color filter including icons");
        SetLiveSaturation(40);
        capturingPreview=true;PositionColorSurfaces();check(!IsWindowVisible(colorWindow),"raw preview suppression applies to both color modes");
        capturingPreview=false;PositionColorSurfaces();check(IsWindowVisible(colorWindow),"color returns after preview capture");
        if(includeIcons) {
            for(auto& vignette:surfaces) DropSurface(vignette);
            check(PositionColorSurfaces()&&WindowAbove(blurSurfaces.front().composition.hwnd,colorWindow),"zero-vignette path retains color below blur");
            check(Render(surfaces.front())&&PositionSurface(surfaces.front(),true)&&WindowAbove(surfaces.front().hwnd,colorWindow),"cold vignette show restores color below vignette");
        }
        colorTransition.Reset(false);AdvanceColorAnimation();check(!IsWindowVisible(colorWindow)&&!colorTimerArmed,"color fade-out stops and hides including icons");
        SetColorVisibility(true);check(colorTimerArmed&&colorTransition.Value(ClockMs())<.15,"cold icon fade starts transparent");
        double value=colorTransition.Value(ClockMs());SetColorVisibility(false);
        check(std::abs(colorTransition.Value(ClockMs())-value)<.04,"icon color fade reverses continuously");
        colorTransition.Reset(true);AdvanceColorAnimation();check(!colorTimerArmed&&!colorAnimationTick,"icon color animation stops at endpoint");
    }
    Clear();blurEngine.reset();DestroyWindow(ownShellBoundary);ownShellBoundary=nullptr;DestroyWindow(desktop);desktop=nullptr;
    check(GetForegroundWindow()==foreground,"foreground unchanged");
    check(!foreignAnchorAttempts,"no third-party anchors, including elevated windows");
    check(!blurPreviewProcess&&!blurAnimationTick&&!wallpaperCaptureClient,"all resources and leases released");
    std::printf("{\"BlurIconProtectionChecks\":%u,\"Failures\":%u,\"UserSettingsUntouched\":true,\"ShellBoundarySimulated\":true,\"ActualExplorerVisualTest\":false}\n",checks,failures);
    return failures?1:0;
}

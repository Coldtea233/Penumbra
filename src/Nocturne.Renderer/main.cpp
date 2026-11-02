#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <psapi.h>
#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdint>
#include <memory>
#include "focus_animation.h"
#include "desktop_blur.h"
#include "blur_bounds.h"
#include "branding.h"
#include "wallpaper_layer.h"

// Event-driven at rest. A short-lived timer exists only during a transition.
constexpr UINT TrayMessage = WM_APP + 1, ReorderMessage = WM_APP + 2, IconMessage = WM_APP + 3, WindowStateMessage = WM_APP + 4, TrayId = 1;
constexpr UINT SettingsId = 101, ReloadId = 102, PauseId = 103, ExitId = 104;
constexpr UINT BlurBoundsMessage=WM_APP+5, BlurRadiusMessage=WM_APP+6;
constexpr UINT ColorValueMessage=WM_APP+7, ColorHostMessage=WM_APP+8;
struct Params { double opacity=.68, radius=.48, feather=.65, shape=1, cx=.5, cy=.5, blurRadius=12, saturation=40; int tint=0; bool primary=false, darkenIcons=false, focusOnly=false, blurEnabled=false, saturationEnabled=false; };
struct Bitmap {
    HDC dc{}; HBITMAP handle{}; HGDIOBJ previous{}; uint32_t* pixels{};
    int width=0, height=0;
    Bitmap()=default;
    Bitmap(const Bitmap&)=delete;
    Bitmap& operator=(const Bitmap&)=delete;
    ~Bitmap() { if(previous) SelectObject(dc,previous); if(handle) DeleteObject(handle); if(dc) DeleteDC(dc); }
    bool Create(int w,int h) {
        if(w<1||h<1||w>32768||h>32768||(int64_t)w*h>160000000) return false;
        width=w; height=h;
        BITMAPINFO bi{}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth=w; bi.bmiHeader.biHeight=-h;
        bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
        dc=CreateCompatibleDC(nullptr);
        void* bits{}; handle=CreateDIBSection(nullptr,&bi,DIB_RGB_COLORS,&bits,nullptr,0);
        pixels=static_cast<uint32_t*>(bits);
        if(!dc||!handle||!pixels) return false;
        previous=SelectObject(dc,handle);
        if(previous==HGDI_ERROR) previous=nullptr;
        return previous!=nullptr;
    }
};
// Windows retains the submitted layered bitmap. Reuse it for every focus fade;
// no full-screen DIB, GDI scaling or per-pixel icon work on the animation path.
struct Surface { HWND hwnd{}; RECT rect{}; bool uploaded=false; BYTE opacity=0; };
HINSTANCE instance;
HWND controller{}, desktop{}, icons{};
HWND iconList{};
HANDLE stopEvent{}, reloadEvent{}, appliedEvent{};
HANDLE animationTick{};
UINT taskbarCreated;
branding::TrayIcon trayIcon;
UINT previewMessage;
HWINEVENTHOOK foregroundHook{}, minimizeHook{}, reorderHook{}, iconHook{}, windowHook{}, cloakHook{};
std::vector<Surface> surfaces;
Params params;
bool paused=false, capturingPreview=false;
bool blurPolicyAllowed=true;
bool iconsDirty=false, animationTimerArmed=false;
bool windowStatePending=false, reorderPending=false, iconPending=false;
double frameIntervalMs=1000.0/60, detectedRefreshHz=0, nextFrameAt=0;
focus_animation::Transition transition;
struct BlurSurface { desktop_blur::Engine::Surface composition; RECT rect{}, protectedArea{}; bool protectionValid=false; };
std::unique_ptr<desktop_blur::Engine> blurEngine;
std::vector<BlurSurface> blurSurfaces;
focus_animation::Transition blurTransition;
HANDLE blurAnimationTick{};
double blurNextFrameAt=0;
bool blurTimerArmed=false;
bool blurBoundsPending=false;
HANDLE blurPreviewProcess{};
DWORD blurPreviewClient=0;
struct ColorSurface { desktop_blur::Engine::Surface composition; wallpaper_layer::Layer layer; RECT rect{},safe{}; bool clipped=false; };
std::vector<ColorSurface> colorSurfaces;
focus_animation::Transition colorTransition;
HANDLE colorAnimationTick{},colorPreviewProcess{},wallpaperProcess{};
DWORD colorPreviewClient=0,colorCaptureClient=0;
bool colorTimerArmed=false,colorHostPending=false;
double colorNextFrameAt=0;
HRESULT colorLastResult=S_OK;
// Opt-in diagnostics only. Keep samples in memory during the animation and
// write once at rest, so tracing cannot stall each frame with disk I/O.
bool traceAnimation=false, traceFileOutput=true;
struct FrameTrace { double tick, value, submitMs; bool target, visible; };
std::vector<FrameTrace> frameTrace;
double lastSubmitMs=0;
double ClockMs() {
    static const double ticksPerMs=[] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return f.QuadPart/1000.0; }();
    LARGE_INTEGER counter{}; QueryPerformanceCounter(&counter);
    return counter.QuadPart/ticksPerMs;
}
struct IconMask {
    RECT rect{};
    int width=0, height=0;
    std::vector<unsigned char> bits;
    bool At(int x,int y) const {
        x-=rect.left; y-=rect.top;
        if(x<0||y<0||x>=width||y>=height) return false;
        size_t i=(size_t)y*width+x;
        return (bits[i/8] & (1u<<(i%8)))!=0;
    }
};
IconMask iconMask;
std::wstring configDir, configFile;

void TraceFrame(double value) {
    if(!traceAnimation || frameTrace.size()>=4096) return;
    bool visible=!surfaces.empty()&&IsWindowVisible(surfaces.front().hwnd);
    frameTrace.push_back({ClockMs(),value,lastSubmitMs,transition.Target(),visible});
}
void FlushFrameTrace() {
    if(frameTrace.empty()||!traceFileOutput) return;
    FILE* file=_wfopen((configDir+L"\\animation-trace.csv").c_str(),L"a");
    if(file) {
        for(const auto& frame:frameTrace)
            std::fprintf(file,"%.3f,%d,%.6f,1.000000,%.6f,%d,%.4f\n",
                frame.tick,frame.target,frame.value,
                focus_animation::Opacity(frame.value),frame.visible,frame.submitMs);
        std::fclose(file);
    }
    frameTrace.clear();
}

double Alpha(const Params& p, double x, double y) {
    double dx=(x-p.cx)*2/p.shape, dy=(y-p.cy)*2;
    double t=std::clamp((std::sqrt(dx*dx+dy*dy)-p.radius)/p.feather,0.0,1.0);
    return p.opacity*t*t*(3-2*t);
}
void InitializePaths() {
    PWSTR path{};
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&path))) {
        configDir=std::wstring(path)+L"\\Nocturne"; CoTaskMemFree(path);
    } else { wchar_t temp[MAX_PATH]; GetTempPathW(MAX_PATH,temp); configDir=std::wstring(temp)+L"Nocturne"; }
    CreateDirectoryW(configDir.c_str(),nullptr); configFile=configDir+L"\\settings.ini";
}
void Status(const wchar_t* message) {
    HANDLE file=CreateFileW((configDir+L"\\status.txt").c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file!=INVALID_HANDLE_VALUE) {
        int length=WideCharToMultiByte(CP_UTF8,0,message,-1,nullptr,0,nullptr,nullptr);
        std::vector<char> bytes(length); WideCharToMultiByte(CP_UTF8,0,message,-1,bytes.data(),length,nullptr,nullptr);
        DWORD written; WriteFile(file,bytes.data(),length-1,&written,nullptr); CloseHandle(file);
    }
    if(appliedEvent) SetEvent(appliedEvent);
}
double ReadNumber(const wchar_t* key,double fallback,double low,double high) {
    wchar_t buffer[80],*end{}; GetPrivateProfileStringW(L"vignette",key,L"",buffer,80,configFile.c_str());
    double value=wcstod(buffer,&end);
    return end!=buffer && *end==0 && std::isfinite(value) ? std::clamp(value,low,high) : fallback;
}
void Load() {
    params.opacity=ReadNumber(L"opacity",.68,0,1); params.radius=ReadNumber(L"radius",.48,.05,1.5);
    params.feather=ReadNumber(L"feather",.65,.05,1.5); params.shape=ReadNumber(L"shape",1,.3,2.5);
    params.cx=ReadNumber(L"centerX",.5,0,1); params.cy=ReadNumber(L"centerY",.5,0,1);
    params.tint=(int)ReadNumber(L"tint",0,0,2); params.primary=ReadNumber(L"primaryOnly",0,0,1)==1;
    params.darkenIcons=ReadNumber(L"darkenIcons",0,0,1)==1;
    params.focusOnly=ReadNumber(L"focusOnly",0,0,1)==1;
    params.blurEnabled=ReadNumber(L"blurEnabled",0,0,1)==1;
    params.blurRadius=ReadNumber(L"blurRadius",12,0,48);
    params.saturationEnabled=ReadNumber(L"saturationEnabled",0,0,1)==1;
    params.saturation=ReadNumber(L"saturation",40,0,100);
    // This user-selected effect must not silently disappear when Windows'
    // unrelated client-area animations are disabled. Never change that setting.
}
enum class ForegroundKind { Desktop, Application, Transient };
ForegroundKind ClassifyForeground(HWND foreground) {
    if(!foreground || !IsWindow(foreground)) return ForegroundKind::Transient;
    HWND root=GetAncestor(foreground,GA_ROOT);
    if(root==desktop || foreground==icons || foreground==iconList || root==GetShellWindow() || root==GetDesktopWindow())
        return ForegroundKind::Desktop;
    wchar_t name[128]{}; GetClassNameW(root,name,128);
    if(wcscmp(name,L"Progman")==0||wcscmp(name,L"WorkerW")==0) return ForegroundKind::Desktop;
    for(const auto* transient:{L"Shell_TrayWnd",L"Shell_SecondaryTrayWnd",L"NotifyIconOverflowWindow",
        L"TopLevelWindowForOverflowXamlIsland",L"XamlExplorerHostIslandWindow",L"#32768",
        L"MultitaskingViewFrame",L"TaskListThumbnailWnd",L"Nocturne.Controller",L"Nocturne.Surface",L"Nocturne.BlurSurface"})
        if(wcscmp(name,transient)==0) return ForegroundKind::Transient;
    DWORD pid=0; GetWindowThreadProcessId(root,&pid);
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(process) {
        wchar_t path[32768]{}; DWORD length=32768;
        bool shell=false;
        if(QueryFullProcessImageNameW(process,0,path,&length)) {
            const wchar_t* file=wcsrchr(path,L'\\'); file=file?file+1:path;
            for(const auto* host:{L"StartMenuExperienceHost.exe",L"SearchHost.exe",L"SearchApp.exe",L"ShellExperienceHost.exe"})
                if(_wcsicmp(file,host)==0) { shell=true; break; }
        }
        CloseHandle(process);
        if(shell) return ForegroundKind::Transient;
    }
    // File Explorer windows are applications, not the desktop itself.
    return ForegroundKind::Application;
}
bool IsApplicationWindow(HWND hwnd) {
    if(!IsWindowVisible(hwnd)||IsIconic(hwnd)||GetAncestor(hwnd,GA_ROOT)!=hwnd) return false;
    LONG_PTR extended=GetWindowLongPtrW(hwnd,GWL_EXSTYLE);
    if(extended&(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE)) return false;
    DWORD cloaked=0;
    if(SUCCEEDED(DwmGetWindowAttribute(hwnd,DWMWA_CLOAKED,&cloaked,sizeof(cloaked)))&&cloaked) return false;
    RECT rect{},intersection{},screen{GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN),0,0};
    screen.right=screen.left+GetSystemMetrics(SM_CXVIRTUALSCREEN);
    screen.bottom=screen.top+GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if(!GetWindowRect(hwnd,&rect)||!IntersectRect(&intersection,&rect,&screen)) return false;
    return ClassifyForeground(hwnd)==ForegroundKind::Application;
}
BOOL CALLBACK FindApplicationWindow(HWND hwnd,LPARAM data) {
    if(!IsApplicationWindow(hwnd)) return TRUE;
    *reinterpret_cast<bool*>(data)=true;
    return FALSE;
}
bool HasApplicationWindow() {
    bool found=false;
    EnumWindows(FindApplicationWindow,reinterpret_cast<LPARAM>(&found));
    return found;
}
void TraceWindowState(const char* reason) {
    if(!traceAnimation) return;
    HWND foreground=GetForegroundWindow();
    wchar_t name[128]{}; GetClassNameW(foreground,name,128);
    FILE* file=_wfopen((configDir+L"\\animation-diagnostics.txt").c_str(),L"a");
    if(file) {
        std::fprintf(file,"%llu %s opacity=%.3f windowsOnly=%d applicationWindows=%d foreground=%p root=%p desktop=%p kind=%d class=%ls target=%d paused=%d\n",
            (unsigned long long)GetTickCount64(),reason,params.opacity,params.focusOnly,HasApplicationWindow(),foreground,
            GetAncestor(foreground,GA_ROOT),desktop,(int)ClassifyForeground(foreground),name,transition.Target(),paused);
        std::fclose(file);
    }
}
BOOL CALLBACK FindDesktop(HWND hwnd,LPARAM) {
    HWND view=FindWindowExW(hwnd,nullptr,L"SHELLDLL_DefView",nullptr);
    if(view) { icons=view; desktop=hwnd; return FALSE; }
    return TRUE;
}
bool LocateDesktop() {
    desktop=nullptr; icons=nullptr; EnumWindows(FindDesktop,0);
    if(!desktop || !icons) return false;
    wchar_t name[80]{}; GetClassNameW(desktop,name,80);
    iconList=FindWindowExW(icons,nullptr,L"SysListView32",nullptr);
    return wcscmp(name,L"Progman")==0 || wcscmp(name,L"WorkerW")==0;
}
bool ReadIconMask(IconMask& mask) {
    if(!iconList || !IsWindowVisible(iconList)) return true;
    if(!GetWindowRect(iconList,&mask.rect)) return false;
    mask.width=mask.rect.right-mask.rect.left; mask.height=mask.rect.bottom-mask.rect.top;
    size_t count=(size_t)mask.width*mask.height;
    if(mask.width<1 || mask.height<1 || count>160000000) return false;
    BITMAPINFO bi{}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth=mask.width; bi.bmiHeader.biHeight=-mask.height;
    bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32;
    HDC screen=GetDC(nullptr), memory=CreateCompatibleDC(screen);
    void* bits{}; HBITMAP bitmap=CreateDIBSection(screen,&bi,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!memory||!bitmap||!bits) {
        if(bitmap) DeleteObject(bitmap); if(memory) DeleteDC(memory); ReleaseDC(nullptr,screen); return false;
    }
    HGDIOBJ previous=SelectObject(memory,bitmap);
    ZeroMemory(bits,count*4);
    // Explorer paints only the icon glyphs/labels into this black DIB. Excluding
    // those exact pixels preserves icons without bright rectangular cutouts.
    bool ok=PrintWindow(iconList,memory,3)!=FALSE;
    if(ok) {
        mask.bits.assign((count+7)/8,0);
        const auto* pixels=static_cast<const uint32_t*>(bits);
        size_t painted=0;
        for(size_t i=0;i<count;i++) if(pixels[i]&0x00FFFFFFu) {
            mask.bits[i/8]|=1u<<(i%8); painted++;
        }
        // Reject a wallpaper-backed capture rather than punching out the whole desktop.
        if(painted>count*3/4) { mask.bits.clear(); ok=false; }
    }
    SelectObject(memory,previous); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr,screen);
    return ok;
}
void DropSurface(Surface& s) {
    if(IsWindow(s.hwnd)) DestroyWindow(s.hwnd);
    s.hwnd=nullptr; s.uploaded=false; s.opacity=0;
}
void StopAnimation() {
    if(animationTick) { CancelWaitableTimer(animationTick); CloseHandle(animationTick); animationTick=nullptr; }
    animationTimerArmed=false;
}
void StopBlurAnimation() {
    if(blurAnimationTick) { CancelWaitableTimer(blurAnimationTick); CloseHandle(blurAnimationTick); blurAnimationTick=nullptr; }
    blurTimerArmed=false;
}
void ClearBlur() {
    if(blurPreviewProcess) { CloseHandle(blurPreviewProcess); blurPreviewProcess=nullptr; }
    blurPreviewClient=0;
    StopBlurAnimation(); blurTransition.Reset(false);
    for(auto& s:blurSurfaces) s.composition.Reset();
    blurSurfaces.clear();
}
void StopColorAnimation() {
    if(colorAnimationTick) { CancelWaitableTimer(colorAnimationTick); CloseHandle(colorAnimationTick); colorAnimationTick=nullptr; }
    colorTimerArmed=false;
}
void ClearColor() {
    StopColorAnimation(); colorTransition.Reset(false);
    colorCaptureClient=0;if(controller) KillTimer(controller,7);
    if(colorPreviewProcess) { CloseHandle(colorPreviewProcess); colorPreviewProcess=nullptr; }
    if(wallpaperProcess) { CloseHandle(wallpaperProcess); wallpaperProcess=nullptr; }
    colorPreviewClient=0;
    for(auto& s:colorSurfaces) s.composition.Reset();
    colorSurfaces.clear();
}
void Clear() { StopAnimation(); ClearColor(); ClearBlur(); FlushFrameTrace(); for(auto& s:surfaces) DropSurface(s); surfaces.clear(); }
bool DesktopPlacementCurrent(HWND window,const RECT& rect) {
    if(!IsWindowVisible(window)||GetWindow(window,GW_OWNER)!=desktop||
       (GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST)) return false;
    RECT actual{};
    if(!GetWindowRect(window,&actual)||!EqualRect(&actual,&rect)) return false;
    // Do not restack a live backdrop just because a timer or foreground event
    // arrived. Changing its DWM sampling boundary can invalidate the backdrop
    // even when the requested coordinates and order did not actually change.
    for(HWND below=GetWindow(window,GW_HWNDNEXT);below;below=GetWindow(below,GW_HWNDNEXT)) {
        if(below==desktop) return true;
        if(IsApplicationWindow(below)||blur_bounds::IsTaskbar(below)) return false;
    }
    return false;
}
bool PlaceDesktopPopup(HWND window,const RECT& rect,bool visible) {
    if(!visible) { if(IsWindowVisible(window)) ShowWindow(window,SW_HIDE); return true; }
    if(!IsWindow(window)||!IsWindow(desktop)) return false;
    if(DesktopPlacementCurrent(window,rect)) return true;
    if(GetWindow(window,GW_OWNER)!=desktop) {
        SetLastError(ERROR_SUCCESS);
        if(!SetWindowLongPtrW(window,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(desktop))&&GetLastError()!=ERROR_SUCCESS) return false;
    }
    return SetWindowPos(window,HWND_BOTTOM,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,
        SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_SHOWWINDOW)!=FALSE;
}
bool PositionSurface(Surface& s, bool show) {
    if(!s.hwnd) return true;
    // A foreign insert-after HWND can belong to an elevated tool (e.g. PixPin),
    // making SetWindowPos fail with ERROR_ACCESS_DENIED. Own our popup by the
    // desktop instead: Windows keeps owned windows above their owner, so
    // HWND_BOTTOM puts us just above the desktop and below ordinary apps.
    // Only change OUR window; never reposition Explorer or a foreign app.
    // HWND_BOTTOM also clears an accidental topmost flag. Do not move or
    // activate the owner, and do not promote the overlay to HWND_TOP on failure.
    return PlaceDesktopPopup(s.hwnd,s.rect,show&&!paused&&!capturingPreview&&(transition.Target()||animationTimerArmed));
}
bool ProtectBlurTaskbar(BlurSurface& s) {
    RECT safe=blur_bounds::DesktopArea(s.rect);
    if(s.protectionValid&&EqualRect(&safe,&s.protectedArea)) return true;
    if(!blurEngine||FAILED(blurEngine->Clip(s.composition,s.rect,safe))) return false;
    HRGN region=CreateRectRgn(safe.left-s.rect.left,safe.top-s.rect.top,safe.right-s.rect.left,safe.bottom-s.rect.top);
    if(!region) return false;
    if(!SetWindowRgn(s.composition.hwnd,region,FALSE)) { DeleteObject(region); return false; }
    // SetWindowRgn owns the region on success. Both HWND and visual are clipped.
    s.protectedArea=safe; s.protectionValid=true;
    return true;
}
bool PositionBlurSurfaces() {
    for(auto& s:blurSurfaces) {
        if(!ProtectBlurTaskbar(s)) { ShowWindow(s.composition.hwnd,SW_HIDE); return false; }
        HWND window=s.composition.hwnd;
        // Reinsert LAST at the bottom: blur stays below our vignette and above
        // its desktop owner, never above ordinary apps. No foreign HWND anchor.
        if(!PlaceDesktopPopup(window,s.rect,!paused&&!capturingPreview&&(blurTransition.Target()||blurTimerArmed))) return false;
    }
    return true;
}
bool SetLiveBlurRadius(float radius) {
    if(!params.blurEnabled||!blurEngine||blurSurfaces.empty()) return false;
    float previous=blurSurfaces.front().composition.radius;
    for(auto& s:blurSurfaces) if(FAILED(blurEngine->SetRadius(s.composition,radius))) {
        for(auto& rollback:blurSurfaces) blurEngine->SetRadius(rollback.composition,previous);
        return false;
    }
    return true;
}
void EndBlurRadiusPreview() {
    SetLiveBlurRadius(static_cast<float>(params.blurRadius));
    if(blurPreviewProcess) { CloseHandle(blurPreviewProcess); blurPreviewProcess=nullptr; }
    blurPreviewClient=0;
}
void FailBlur() {
    ClearBlur();
    Status(L"桌面模糊不可用 · 已安全关闭模糊，暗角不受影响");
}
void RefreshBlurPolicy(HWND hwnd) {
    if(!params.blurEnabled&&!params.saturationEnabled) return;
    bool allowed=desktop_blur::TransparencyAllowed();
    if(allowed==blurPolicyAllowed) return;
    blurPolicyAllowed=allowed;
    if(!allowed) {
        ClearBlur(); ClearColor();
        Status(L"Windows 已关闭透明效果或开启高对比度 · 模糊与降饱和度已安全停用，暗角保留");
    }
    else SetTimer(hwnd,2,250,nullptr);
}
void AdvanceBlurAnimation() {
    double now=ClockMs();
    bool running=blurTransition.Running(now);
    double value=running?blurTransition.Value(now):(blurTransition.Target()?1:0);
    for(auto& s:blurSurfaces) if(FAILED(s.composition.visual->put_Opacity(static_cast<float>(value)))) { FailBlur(); return; }
    if(!running) {
        blurTransition.Reset(blurTransition.Target()); StopBlurAnimation();
        if(!PositionBlurSurfaces()) FailBlur();
        return;
    }
    // Only a short transition submits properties. No screenshots, CPU blur or
    // continuous property/timer updates after the endpoint has been submitted.
    do { blurNextFrameAt+=frameIntervalMs; } while(blurNextFrameAt<=now);
    LARGE_INTEGER due{};
    due.QuadPart=-(LONGLONG)std::max(1.0,10000*std::min(blurNextFrameAt-now,blurTransition.Remaining(now)));
    if(!SetWaitableTimer(blurAnimationTick,&due,0,nullptr,nullptr,FALSE)) FailBlur();
}
void SetBlurVisibility(bool visible) {
    visible=visible&&params.blurEnabled&&!paused;
    if(blurSurfaces.empty()||visible==blurTransition.Target()) return;
    double now=ClockMs();
    if(!blurTransition.Retarget(visible,now)) return;
    if(!blurTimerArmed) {
        blurAnimationTick=CreateWaitableTimerExW(nullptr,nullptr,0x00000002,TIMER_ALL_ACCESS);
        if(!blurAnimationTick) blurAnimationTick=CreateWaitableTimerW(nullptr,FALSE,nullptr);
        blurTimerArmed=blurAnimationTick!=nullptr;
        if(!blurTimerArmed) { FailBlur(); return; }
    }
    // Commit the current opacity before unhiding, including cold starts and
    // rapid reversals; a previously opaque surface can never flash on screen.
    for(auto& s:blurSurfaces) if(FAILED(s.composition.visual->put_Opacity(static_cast<float>(blurTransition.Value(now))))) { FailBlur(); return; }
    if(!PositionBlurSurfaces()) { FailBlur(); return; }
    blurNextFrameAt=now;
    AdvanceBlurAnimation();
}
bool WantsDesktopBlur(bool enabled,bool isPaused,bool hasWindows,ForegroundKind kind,bool previousTarget=false) {
    return enabled&&!isPaused&&hasWindows&&
        (kind==ForegroundKind::Application||(kind==ForegroundKind::Transient&&previousTarget));
}
bool PositionColorSurfaces() {
    for(auto& s:colorSurfaces) {
        RECT safe=blur_bounds::DesktopArea(s.rect);
        if(!s.clipped||!EqualRect(&s.safe,&safe)) {
            if(!blurEngine||FAILED(blurEngine->Clip(s.composition,s.rect,safe))) return false;
            HRGN region=CreateRectRgn(safe.left-s.rect.left,safe.top-s.rect.top,safe.right-s.rect.left,safe.bottom-s.rect.top);
            if(!region) return false;
            if(!SetWindowRgn(s.composition.hwnd,region,FALSE)) { DeleteObject(region); return false; }
            s.safe=safe; s.clipped=true;
        }
        if(!wallpaper_layer::Place(s.composition.hwnd,s.layer,s.rect,!paused&&!capturingPreview&&!colorCaptureClient&&s.composition.radius<1&&(colorTransition.Target()||colorTimerArmed))) return false;
    }
    return true;
}
void AdvanceColorAnimation() {
    double now=ClockMs(); bool running=colorTransition.Running(now);
    float value=static_cast<float>(running?colorTransition.Value(now):(colorTransition.Target()?1:0));
    for(auto& s:colorSurfaces) if(FAILED(s.composition.visual->put_Opacity(value))) { ClearColor(); return; }
    if(!running) {
        colorTransition.Reset(colorTransition.Target()); StopColorAnimation();
        if(!PositionColorSurfaces()) ClearColor();
        return;
    }
    do { colorNextFrameAt+=frameIntervalMs; } while(colorNextFrameAt<=now);
    LARGE_INTEGER due{}; due.QuadPart=-(LONGLONG)std::max(1.0,10000*std::min(colorNextFrameAt-now,colorTransition.Remaining(now)));
    if(!SetWaitableTimer(colorAnimationTick,&due,0,nullptr,nullptr,FALSE)) ClearColor();
}
void SetColorVisibility(bool visible) {
    visible=visible&&params.saturationEnabled&&!paused&&wallpaperProcess;
    if(colorSurfaces.empty()||visible==colorTransition.Target()) return;
    double now=ClockMs(); if(!colorTransition.Retarget(visible,now)) return;
    if(!colorTimerArmed) {
        colorAnimationTick=CreateWaitableTimerExW(nullptr,nullptr,0x00000002,TIMER_ALL_ACCESS);
        if(!colorAnimationTick) colorAnimationTick=CreateWaitableTimerW(nullptr,FALSE,nullptr);
        colorTimerArmed=colorAnimationTick!=nullptr;
        if(!colorTimerArmed) { ClearColor(); return; }
    }
    for(auto& s:colorSurfaces) if(FAILED(s.composition.visual->put_Opacity(static_cast<float>(colorTransition.Value(now))))) { ClearColor(); return; }
    if(!PositionColorSurfaces()) { ClearColor(); return; }
    colorNextFrameAt=now; AdvanceColorAnimation();
}
bool SetLiveSaturation(float percentage) {
    if(!params.saturationEnabled||!blurEngine||colorSurfaces.empty()) return false;
    float value=std::isfinite(percentage)?std::clamp(percentage,0.f,100.f)/100:1.f;
    float previous=colorSurfaces.front().composition.radius;
    for(auto& s:colorSurfaces) if(FAILED(blurEngine->SetRadius(s.composition,value))) {
        for(auto& rollback:colorSurfaces) blurEngine->SetRadius(rollback.composition,previous);
        return false;
    }
    return PositionColorSurfaces();
}
void EndSaturationPreview() {
    SetLiveSaturation(static_cast<float>(params.saturation));
    if(colorPreviewProcess) { CloseHandle(colorPreviewProcess); colorPreviewProcess=nullptr; }
    colorPreviewClient=0;
}
bool RefreshColorLayers() {
    colorLastResult=S_OK;
    if(!params.saturationEnabled||paused) { ClearColor(); return true; }
    auto layers=wallpaper_layer::Find();
    std::vector<ColorSurface> desired;
    for(const auto& layer:layers) for(const auto& monitor:surfaces) {
        RECT rect{};
        if(IntersectRect(&rect,&layer.bounds,&monitor.rect)) { ColorSurface s;s.layer=layer;s.rect=rect;desired.push_back(std::move(s)); }
    }
    bool same=!desired.empty()&&desired.size()==colorSurfaces.size()&&wallpaperProcess&&WaitForSingleObject(wallpaperProcess,0)==WAIT_TIMEOUT;
    if(same) for(size_t i=0;i<desired.size();i++) {
        auto& a=desired[i];auto& b=colorSurfaces[i];
        if(a.layer.wallpaper!=b.layer.wallpaper||a.layer.host!=b.layer.host||a.layer.pid!=b.layer.pid||!EqualRect(&a.rect,&b.rect)||!IsWindow(b.composition.hwnd)) { same=false;break; }
    }
    if(same) {
        if(PositionColorSurfaces()) return true;
        ClearColor(); colorLastResult=E_FAIL; return false;
    }
    ClearColor();
    if(desired.empty()) return true; // Waiting for a supported WE desktop.
    if(!desktop_blur::TransparencyAllowed()) { colorLastResult=HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);return false; }
    wallpaperProcess=wallpaper_layer::OpenEngine(desired.front().layer.pid);
    if(!wallpaperProcess) return true;
    if(!blurEngine) blurEngine=std::make_unique<desktop_blur::Engine>();
    for(auto& s:desired) {
        // A non-layered child target is required here. It stays INSIDE the
        // wallpaper-only Explorer branch, below icons, apps and the taskbar.
        POINT origin{s.rect.left,s.rect.top};ScreenToClient(s.layer.host,&origin);
        s.composition.hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,
            L"Nocturne.ColorSurface",L"Penumbra wallpaper color",WS_CHILD|WS_CLIPSIBLINGS,origin.x,origin.y,
            s.rect.right-s.rect.left,s.rect.bottom-s.rect.top,s.layer.host,nullptr,instance,nullptr);
        HRESULT hr=s.composition.hwnd?blurEngine->Attach(s.composition.hwnd,s.rect.right-s.rect.left,s.rect.bottom-s.rect.top,
            static_cast<float>(params.saturation/100),s.composition,desktop_blur::Filter::Saturation):HRESULT_FROM_WIN32(GetLastError());
        if(FAILED(hr)) { s.composition.Reset();ClearColor();colorLastResult=hr;return false; }
        colorSurfaces.push_back(std::move(s));
    }
    if(!PositionColorSurfaces()) { ClearColor();colorLastResult=E_FAIL;return false; }
    return true;
}
BOOL CALLBACK CollectMonitor(HMONITOR monitor,HDC,LPRECT,LPARAM data) {
    MONITORINFOEXW info{}; info.cbSize=sizeof(info);
    if(GetMonitorInfoW(monitor,&info) && (!params.primary || (info.dwFlags&MONITORINFOF_PRIMARY))) {
        reinterpret_cast<std::vector<RECT>*>(data)->push_back(info.rcMonitor);
        DEVMODEW mode{}; mode.dmSize=sizeof(mode);
        if(EnumDisplaySettingsW(info.szDevice,ENUM_CURRENT_SETTINGS,&mode)&&mode.dmDisplayFrequency>=24) {
            detectedRefreshHz=std::max(detectedRefreshHz,(double)std::min<DWORD>(240,mode.dmDisplayFrequency));
            frameIntervalMs=1000.0/detectedRefreshHz;
        }
    }
    return TRUE;
}
bool CreateSurface(Surface& s) {
    if(IsWindow(s.hwnd)) return true;
    s.hwnd=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,
        L"Nocturne.Surface",L"Nocturne vignette",WS_POPUP,0,0,1,1,nullptr,nullptr,instance,nullptr);
    return s.hwnd!=nullptr;
}
void FillVignette(Bitmap& bitmap) {
    unsigned char rgb[3]{};
    if(params.tint==1) { rgb[0]=14; rgb[1]=22; rgb[2]=40; }
    if(params.tint==2) { rgb[0]=27; rgb[1]=18; rgb[2]=16; }
    auto* pixel=reinterpret_cast<unsigned char*>(bitmap.pixels);
    // Premultiplied BGRA required by UpdateLayeredWindow.
    for(int y=0;y<bitmap.height;y++) for(int x=0;x<bitmap.width;x++) {
        double a=Alpha(params,(x+.5)/bitmap.width,(y+.5)/bitmap.height);
        *pixel++=(unsigned char)std::lround(rgb[2]*a); *pixel++=(unsigned char)std::lround(rgb[1]*a);
        *pixel++=(unsigned char)std::lround(rgb[0]*a); *pixel++=(unsigned char)std::lround(255*a);
    }
}
void ProtectIcons(Bitmap& bitmap,const RECT& rect) {
    if(params.darkenIcons || iconMask.width<1) return;
    // Scan the compact existing bit mask, not every screen pixel. No extra
    // full-size mask, per-frame allocations or shifted/scaled icon holes.
    for(size_t byte=0;byte<iconMask.bits.size();byte++) {
        unsigned value=iconMask.bits[byte];
        while(value) {
            unsigned bit=__builtin_ctz(value); value&=value-1;
            size_t index=byte*8+bit;
            int x=static_cast<int>(index%iconMask.width)+iconMask.rect.left-rect.left;
            int y=static_cast<int>(index/iconMask.width)+iconMask.rect.top-rect.top;
            if(x>=0&&y>=0&&x<bitmap.width&&y<bitmap.height) bitmap.pixels[(size_t)y*bitmap.width+x]=0;
        }
    }
}
bool Submit(Surface& s,Bitmap& bitmap,BYTE opacity) {
    POINT source{}, destination{s.rect.left,s.rect.top};
    SIZE size{bitmap.width,bitmap.height}; BLENDFUNCTION blend{AC_SRC_OVER,0,opacity,AC_SRC_ALPHA};
    bool ok=UpdateLayeredWindow(s.hwnd,nullptr,&destination,&size,bitmap.dc,&source,0,&blend,ULW_ALPHA)!=FALSE;
    if(ok) { s.uploaded=true; s.opacity=opacity; }
    return ok;
}
bool Render(Surface& s,BYTE opacity=255) {
    Bitmap bitmap;
    if(!CreateSurface(s)||!bitmap.Create(s.rect.right-s.rect.left,s.rect.bottom-s.rect.top)) return false;
    FillVignette(bitmap); ProtectIcons(bitmap,s.rect);
    return Submit(s,bitmap,opacity);
}
bool RenderAnimation(Surface& s,double visibility) {
    BYTE opacity=(BYTE)std::lround(255*focus_animation::Opacity(visibility));
    if(!s.uploaded||!IsWindow(s.hwnd)) return Render(s,opacity);
    if(s.opacity==opacity) return true;
    BLENDFUNCTION blend{AC_SRC_OVER,0,opacity,AC_SRC_ALPHA};
    // A null source DC preserves the existing bitmap; ULW_ALPHA updates its
    // compositor constant alpha only. Do NOT use SetLayeredWindowAttributes:
    // it prevents later UpdateLayeredWindow bitmap updates on this HWND.
    bool ok=UpdateLayeredWindow(s.hwnd,nullptr,nullptr,nullptr,nullptr,nullptr,0,&blend,ULW_ALPHA)!=FALSE;
    if(ok) s.opacity=opacity;
    return ok;
}
void FailTransition() {
    StopAnimation(); transition.Reset(false);
    for(auto& s:surfaces) DropSurface(s);
    Status(L"暗角动画提交失败 · 已安全隐藏，可从托盘重新挂接");
}
void AdvanceAnimation() {
    double now=ClockMs(), submittedAt=now;
    bool running=transition.Running(now), visible=transition.Target();
    if(!running) {
        transition.Reset(visible);
        for(auto& s:surfaces) {
            if(!visible) { if(s.hwnd) ShowWindow(s.hwnd,SW_HIDE); }
            else if(!RenderAnimation(s,1)||!PositionSurface(s,true)) { FailTransition(); return; }
        }
        StopAnimation();
        if(iconsDirty&&visible&&controller) SetTimer(controller,5,120,nullptr);
        lastSubmitMs=ClockMs()-submittedAt;
        TraceFrame(visible?1:0); FlushFrameTrace();
        return;
    }
    double value=transition.Value(now);
    for(auto& s:surfaces) {
        bool showing=IsWindowVisible(s.hwnd)!=FALSE;
        if(!RenderAnimation(s,value)||(!showing&&!PositionSurface(s,true))) { FailTransition(); return; }
    }
    lastSubmitMs=ClockMs()-submittedAt;
    TraceFrame(value);
    // One-shot high-resolution timer: fractional refresh periods do not drift
    // to 16/32ms steps, and there is no timer once a transition finishes.
    double after=ClockMs();
    do { nextFrameAt+=frameIntervalMs; } while(nextFrameAt<=after);
    LARGE_INTEGER due{};
    due.QuadPart=-(LONGLONG)std::max(1.0,10000*std::min(nextFrameAt-after,transition.Remaining(after)));
    if(!SetWaitableTimer(animationTick,&due,0,nullptr,nullptr,FALSE)) FailTransition();
}
void SetVisibility(bool visible,bool animate=true) {
    if(paused||params.opacity==0) visible=false;
    double now=ClockMs();
    if(!animate||params.opacity==0) {
        if(visible==transition.Target()&&!transition.Running(now)) return;
        transition.Reset(visible); AdvanceAnimation(); return;
    }
    if(visible==transition.Target()) return;
    // Finish cold resource preparation BEFORE starting the clock. Keep only
    // the OS-owned surface while hidden; CPU bitmap memory is already freed.
    if(visible) for(auto& s:surfaces) if(!s.uploaded&&!Render(s,0)) { FailTransition(); return; }
    now=ClockMs();
    if(!transition.Retarget(visible,now)) return;
    if(!animationTimerArmed) {
        // WM_TIMER is low priority and can be starved by Explorer/accessibility
        // traffic. A waitable timer is handled directly by the message loop.
        animationTick=CreateWaitableTimerExW(nullptr,nullptr,0x00000002,TIMER_ALL_ACCESS);
        if(!animationTick) animationTick=CreateWaitableTimerW(nullptr,FALSE,nullptr);
        animationTimerArmed=animationTick!=nullptr;
        if(!animationTimerArmed) { transition.Reset(visible); AdvanceAnimation(); return; }
    }
    nextFrameAt=now;
    AdvanceAnimation();
}
void UpdateWindowState() {
    TraceWindowState("windows");
    if(paused) return;
    if(!params.focusOnly&&!params.blurEnabled&&!params.saturationEnabled) return;
    bool hasWindows=HasApplicationWindow();
    if(params.focusOnly) SetVisibility(hasWindows);
    HWND foreground=GetForegroundWindow();
    ForegroundKind kind=ClassifyForeground(foreground);
    if(kind==ForegroundKind::Application&&!IsApplicationWindow(GetAncestor(foreground,GA_ROOT))) kind=ForegroundKind::Transient;
    SetBlurVisibility(WantsDesktopBlur(params.blurEnabled,paused,hasWindows,kind,blurTransition.Target()));
    SetColorVisibility(WantsDesktopBlur(params.saturationEnabled,paused,hasWindows,kind,colorTransition.Target()));
}
void QueueOnce(UINT message,bool& pending) {
    if(!pending) pending=PostMessageW(controller,message,0,0)!=FALSE;
}
bool Rebuild() {
    Clear(); transition.Reset(false);
    blurPolicyAllowed=desktop_blur::TransparencyAllowed();
    if(paused) { Status(L"桌面效果已暂停 · 可从托盘恢复"); return true; }
    if(!LocateDesktop()) { Status(L"未找到桌面宿主 · 已安全隐藏，将自动重试"); return false; }
    IconMask fresh;
    if(!params.darkenIcons&&!ReadIconMask(fresh)) { Status(L"无法读取桌面图标遮罩 · 未启用暗角"); return false; }
    iconMask=std::move(fresh); iconsDirty=false;
    frameIntervalMs=1000.0/60;
    detectedRefreshHz=0;
    std::vector<RECT> monitors; EnumDisplayMonitors(nullptr,nullptr,CollectMonitor,reinterpret_cast<LPARAM>(&monitors));
    if(monitors.empty()) { Status(L"没有可用显示器"); return false; }
    for(const RECT& r:monitors) {
        Surface s; s.rect=r;
        surfaces.push_back(std::move(s));
    }
    // Prewarm at apply/startup even when the desktop currently has focus. A
    // later fully-hidden -> app transition has exactly the same path as a reversal.
    if(params.opacity>0) for(auto& s:surfaces) if(!Render(s,0)) { FailTransition(); return false; }
    bool visible=params.opacity>0&&(!params.focusOnly||HasApplicationWindow());
    TraceWindowState("rebuild");
    SetVisibility(visible,params.focusOnly);
    if(!transition.Target()&&visible) return false;
    std::wstring message=params.focusOnly?L"暗角已应用 · 存在应用窗口时启用":L"暗角已应用";
    if(params.blurEnabled) {
        blurPolicyAllowed=desktop_blur::TransparencyAllowed();
        if(!blurEngine) blurEngine=std::make_unique<desktop_blur::Engine>();
        HRESULT hr=S_OK;
        for(const RECT& r:monitors) {
            BlurSurface s; s.rect=r;
            // Layered+transparent makes input pass through across process
            // boundaries, not just to windows on our own UI thread.
            s.composition.hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,
                L"Nocturne.BlurSurface",L"Penumbra desktop blur",WS_POPUP,r.left,r.top,r.right-r.left,r.bottom-r.top,desktop,nullptr,instance,nullptr);
            if(!s.composition.hwnd) hr=HRESULT_FROM_WIN32(GetLastError());
            else if(!SetLayeredWindowAttributes(s.composition.hwnd,0,255,LWA_ALPHA)) hr=HRESULT_FROM_WIN32(GetLastError());
            else hr=blurEngine->Attach(s.composition.hwnd,r.right-r.left,r.bottom-r.top,static_cast<float>(params.blurRadius),s.composition);
            if(FAILED(hr)) { s.composition.Reset(); ClearBlur(); break; }
            blurSurfaces.push_back(std::move(s));
            if(!ProtectBlurTaskbar(blurSurfaces.back())) { hr=E_FAIL; ClearBlur(); break; }
        }
        if(FAILED(hr)) {
            wchar_t error[100]; swprintf(error,100,L" · 模糊不可用（0x%08lX），暗角保留",static_cast<unsigned long>(hr));
            message+=error;
        } else {
            UpdateWindowState();
            wchar_t appliedRadius[96];
            swprintf(appliedRadius,96,L" · 模糊半径 %.0f px · 应用获焦时模糊，点击桌面恢复清晰",params.blurRadius);
            message+=appliedRadius;
        }
    }
    if(params.saturationEnabled) {
        if(!RefreshColorLayers()) message+=L" · 壁纸降饱和度不可用，其他效果保留";
        else if(colorSurfaces.empty()) message+=L" · 降饱和度待命（等待 Wallpaper Engine 壁纸）";
        else { wchar_t text[64];swprintf(text,64,L" · 壁纸保留饱和度 %.0f%%",params.saturation);message+=text; }
        UpdateWindowState();
    }
    Status(message.c_str()); return true;
}
void OpenSettings() {
    wchar_t exe[32768]; GetModuleFileNameW(nullptr,exe,32768);
    std::wstring path=exe; path=path.substr(0,path.find_last_of(L"\\/")+1)+L"Penumbra.exe";
    if(reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)
        MessageBoxW(nullptr,L"无法打开设置。请保持 Penumbra.exe 与原生程序位于同一文件夹。",L"Penumbra",MB_OK|MB_ICONERROR);
}
bool AddTray() {
    bool added=trayIcon.Update(instance,controller,TrayId,TrayMessage,true);
    if(added) {
        HANDLE ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,L"Local\\Penumbra.TrayReady.v1");
        if(ready) { SetEvent(ready);CloseHandle(ready); }
    }
    return added;
}
void Menu() {
    HMENU menu=CreatePopupMenu(); AppendMenuW(menu,MF_STRING,SettingsId,L"打开设置");
    AppendMenuW(menu,MF_STRING,ReloadId,L"重新挂接桌面"); AppendMenuW(menu,MF_STRING,PauseId,paused?L"恢复桌面效果":L"暂停桌面效果");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr); AppendMenuW(menu,MF_STRING,ExitId,L"退出并恢复桌面");
    POINT p; GetCursorPos(&p); SetForegroundWindow(controller);
    UINT command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,controller,nullptr); DestroyMenu(menu);
    if(command==SettingsId) OpenSettings();
    if(command==ReloadId) { Load(); Rebuild(); }
    if(command==PauseId) { paused=!paused; Rebuild(); }
    if(command==ExitId) PostQuitMessage(0);
    PostMessageW(controller,WM_NULL,0,0);
}
LRESULT CALLBACK SurfaceProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_NCHITTEST) return HTTRANSPARENT;
    if(msg==WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if(msg==WM_ERASEBKGND) return 1;
    return DefWindowProcW(hwnd,msg,wp,lp);
}
void CALLBACK DesktopEvent(HWINEVENTHOOK hook,DWORD event,HWND hwnd,LONG object,LONG,DWORD,DWORD) {
    if(params.saturationEnabled&&object==OBJID_WINDOW&&hwnd&&
       (wallpaper_layer::IsWallpaper(hwnd)||std::any_of(colorSurfaces.begin(),colorSurfaces.end(),[&](const ColorSurface& s){return s.layer.wallpaper==hwnd||s.layer.host==hwnd;})))
        QueueOnce(ColorHostMessage,colorHostPending);
    if(object==OBJID_WINDOW&&hwnd&&blur_bounds::IsTaskbar(hwnd))
        QueueOnce(BlurBoundsMessage,blurBoundsPending);
    if(event==EVENT_SYSTEM_FOREGROUND) {
        QueueOnce(WindowStateMessage,windowStatePending);
        QueueOnce(ReorderMessage,reorderPending);
        return;
    }
    if(hwnd==iconList || hwnd==icons) { QueueOnce(IconMessage,iconPending); return; }
    if(hook==windowHook||hook==cloakHook) {
        if(object==OBJID_WINDOW&&(!IsWindow(hwnd)||GetAncestor(hwnd,GA_ROOT)==hwnd))
            QueueOnce(WindowStateMessage,windowStatePending);
        return;
    }
    if(hook==minimizeHook) QueueOnce(WindowStateMessage,windowStatePending);
    // The icon hook spans several object events. Ignore unrelated applications
    // here so animation/accessibility events cannot keep repositioning us.
    if(hook==iconHook) return;
    if(event==EVENT_OBJECT_REORDER && (object!=OBJID_WINDOW || (hwnd && hwnd!=desktop && hwnd!=GetDesktopWindow()))) return;
    QueueOnce(ReorderMessage,reorderPending);
}
LRESULT CALLBACK ControllerProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(previewMessage && msg==previewMessage) {
        capturingPreview=wp!=0;
        if(capturingPreview) SetTimer(hwnd,4,3000,nullptr); else KillTimer(hwnd,4);
        for(auto& s:surfaces) PositionSurface(s,true);
        PositionBlurSurfaces();
        if(!PositionColorSurfaces()) ClearColor();
        return 1;
    }
    if(taskbarCreated && msg==taskbarCreated) { AddTray(); SetTimer(hwnd,2,1500,nullptr); return 0; }
    switch(msg) {
    case ColorHostMessage:
        colorHostPending=false;SetTimer(hwnd,6,100,nullptr);return 0;
    case ColorValueMessage: {
        DWORD client=static_cast<DWORD>(lp);
        if(wp==104) {
            if(!client||colorSurfaces.empty()) return 0;
            colorCaptureClient=client;SetTimer(hwnd,7,3000,nullptr);
            if(!PositionColorSurfaces()) { ClearColor();return 0; }
            return 1;
        }
        if(wp==105) {
            if(client==colorCaptureClient) {
                colorCaptureClient=0;KillTimer(hwnd,7);
                if(!PositionColorSurfaces()) ClearColor();
            }
            return 1;
        }
        if(wp==101) { if(client==colorPreviewClient) EndSaturationPreview();return 1; }
        if(wp==102) return colorSurfaces.empty()?0:static_cast<LRESULT>(std::lround(colorSurfaces.front().composition.radius*100)+1);
        if(wp==103) return (wallpaperProcess?8:0)|(colorTransition.Target()?1:0)|(colorTimerArmed?2:0)|
            (!colorSurfaces.empty()&&IsWindowVisible(colorSurfaces.front().composition.hwnd)?4:0);
        if(wp>100||!client||colorSurfaces.empty()) return 0;
        if(colorPreviewClient!=client) {
            HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,client);if(!process) return 0;
            EndSaturationPreview();colorPreviewProcess=process;colorPreviewClient=client;
        }
        return SetLiveSaturation(static_cast<float>(wp))?static_cast<LRESULT>(wp+1):0;
    }
    case BlurBoundsMessage:
        blurBoundsPending=false;
        if(!PositionBlurSurfaces()) FailBlur();
        if(!PositionColorSurfaces()) ClearColor();
        return 0;
    case BlurRadiusMessage: {
        DWORD client=static_cast<DWORD>(lp);
        if(wp==49) { if(client==blurPreviewClient) EndBlurRadiusPreview(); return 1; }
        if(wp==50) return blurSurfaces.empty()?0:static_cast<LRESULT>(std::lround(blurSurfaces.front().composition.radius)+1);
        if(wp>48||!client||blurSurfaces.empty()) return 0;
        if(blurPreviewClient!=client) {
            HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,client);
            if(!process) return 0;
            EndBlurRadiusPreview(); blurPreviewProcess=process; blurPreviewClient=client;
        }
        return SetLiveBlurRadius(static_cast<float>(wp))?static_cast<LRESULT>(wp+1):0;
    }
    case ReorderMessage: reorderPending=false; SetTimer(hwnd,3,60,nullptr); return 0;
    case WindowStateMessage: windowStatePending=false; UpdateWindowState(); return 0;
    case IconMessage:
        iconPending=false;
        if(params.darkenIcons) return 0;
        iconsDirty=true;
        if(!paused&&(transition.Target()||animationTimerArmed)) SetTimer(hwnd,5,350,nullptr);
        return 0;
    case TrayMessage:
        if(LOWORD(lp)==NIN_SELECT || LOWORD(lp)==WM_LBUTTONDBLCLK) OpenSettings();
        else if(LOWORD(lp)==WM_CONTEXTMENU || LOWORD(lp)==WM_RBUTTONUP) Menu();
        return 0;
    case WM_DISPLAYCHANGE: SetTimer(hwnd,2,700,nullptr); return 0;
    case WM_SETTINGCHANGE:
        trayIcon.Update(instance,controller,TrayId,TrayMessage);
        // Theme, animation preference and unrelated app broadcasts do not
        // change our mask. Rebuilding for all of them cancelled live fades.
        if(wp==SPI_SETWORKAREA||wp==SPI_SETICONMETRICS) SetTimer(hwnd,2,700,nullptr);
        if((params.blurEnabled||params.saturationEnabled)&&(wp==SPI_SETHIGHCONTRAST||(lp&&wcscmp(reinterpret_cast<const wchar_t*>(lp),L"ImmersiveColorSet")==0))) {
            RefreshBlurPolicy(hwnd);
        }
        return 0;
    case WM_DWMCOMPOSITIONCHANGED:
        // Keep a valid surface until recovery actually runs. Do not introduce
        // a guaranteed clear frame before a delayed rebuild.
        if(params.blurEnabled||params.saturationEnabled) SetTimer(hwnd,2,250,nullptr);
        return 0;
    case WM_THEMECHANGED:
        trayIcon.Update(instance,controller,TrayId,TrayMessage);
        RefreshBlurPolicy(hwnd);
        return 0;
    case WM_DPICHANGED:
        trayIcon.Update(instance,controller,TrayId,TrayMessage);
        return 0;
    case WM_TIMER:
        if(wp==4) {
            KillTimer(hwnd,4); capturingPreview=false;
            for(auto& s:surfaces) PositionSurface(s,true);
            PositionBlurSurfaces();
            if(!PositionColorSurfaces()) ClearColor();
        }
        else if(wp==5) {
            KillTimer(hwnd,5);
            if(animationTimerArmed) { SetTimer(hwnd,5,120,nullptr); return 0; }
            if(!paused&&(transition.Target()||animationTimerArmed)) {
                IconMask fresh;
                if(ReadIconMask(fresh)) {
                    iconsDirty=false;
                    if(fresh.bits!=iconMask.bits || !EqualRect(&fresh.rect,&iconMask.rect)) {
                        iconMask=std::move(fresh);
                        if(animationTimerArmed) AdvanceAnimation();
                        else for(auto& s:surfaces) if(!Render(s)) { FailTransition(); break; }
                    }
                }
            }
        }
        else if(wp==2) { KillTimer(hwnd,2); Rebuild(); }
        else if(wp==6) { KillTimer(hwnd,6);RefreshColorLayers();UpdateWindowState(); }
        else if(wp==7) { KillTimer(hwnd,7);colorCaptureClient=0;if(!PositionColorSurfaces()) ClearColor(); }
        else if(wp==1 && !paused) {
            HWND previous=desktop;
            bool intact=LocateDesktop() && previous==desktop && !surfaces.empty();
            // Hidden surfaces are cached too. Retry a failed/destroyed surface
            // even when FailTransition reset the visibility target to false.
            if(intact&&params.opacity>0) for(auto& s:surfaces) if(!IsWindow(s.hwnd)) { intact=false; break; }
            if(intact) for(auto& s:blurSurfaces) if(!IsWindow(s.composition.hwnd)) { intact=false; break; }
            if(!intact) Rebuild();
            else {
                UpdateWindowState();
                for(auto& s:surfaces) PositionSurface(s,true);
                PositionBlurSurfaces();
                if(params.saturationEnabled) { RefreshColorLayers();UpdateWindowState(); }
            }
        }
        else if(wp==3) {
            KillTimer(hwnd,3);
            for(auto& s:surfaces) PositionSurface(s, true);
            PositionBlurSurfaces();
            if(!PositionColorSurfaces()) ClearColor();
        }
        return 0;
    case WM_QUERYENDSESSION: return TRUE;
    case WM_ENDSESSION: if(wp) PostQuitMessage(0); return 0;
    case WM_CLOSE: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
int SelfTest() {
    Params p;
    if(Alpha(p,.5,.5)!=0 || std::abs(Alpha(p,0,0)-p.opacity)>1e-10) return 1;
    double previous=0;
    for(int i=0;i<=100;i++) { double a=Alpha(p,.5+i*.005,.5); if(!std::isfinite(a)||a<previous-1e-12||a>p.opacity) return 2; previous=a; }
    p.opacity=0; if(Alpha(p,0,0)!=0) return 3;
    p.opacity=1; p.feather=.05; p.shape=.3; if(Alpha(p,0,0)!=1) return 4;
    p.cx=.2; p.cy=.8; if(Alpha(p,.2,.8)!=0) return 5;
    using namespace focus_animation;
    if(Opacity(1)!=1||Opacity(0)!=0) return 6;
    if(Opacity(.75)!=.75||Opacity(.25)!=.25) return 7;
    Transition animation; animation.Reset(false); animation.Retarget(true,1000);
    double endpoint=1000+Transition::DurationMs;
    if(animation.Value(1000)!=0||!animation.Running(1100)||animation.Running(endpoint)||animation.Value(endpoint)!=1||animation.Velocity(endpoint)!=0) return 8;
    if(!(animation.Value(1050)>.36&&animation.Value(1050)<.39&&animation.Value(1100)>.60&&animation.Value(1100)<.64&&animation.Value(1360)>.97)) return 9;
    double partial=animation.Value(1120); animation.Retarget(false,1120);
    if(animation.Value(1120)!=partial||animation.Target()||animation.Value(2000)!=0) return 10;
    previous=0;
    for(int i=0;i<=100;i++) {
        double value=i/100.0, opacity=Opacity(value);
        if(opacity<previous||opacity<0||opacity>1) return 11;
        previous=opacity;
    }
    double lastValue=0,lastSpeed=EaseOut(0).slope;
    for(int i=1;i<=1000;i++) {
        auto sample=EaseOut(i/1000.0);
        if(sample.value<lastValue||sample.value>1||sample.slope<0||sample.slope>lastSpeed+1e-10) return 12;
        lastValue=sample.value;lastSpeed=sample.slope;
    }
    return 0;
}
bool DispatchWork(DWORD timeout=INFINITE) {
    HANDLE events[8]{stopEvent,reloadEvent};
    DWORD count=2,darkIndex=MAXDWORD,blurIndex=MAXDWORD,previewIndex=MAXDWORD,colorIndex=MAXDWORD,colorPreviewIndex=MAXDWORD,wallpaperIndex=MAXDWORD;
    if(animationTimerArmed) { darkIndex=count; events[count++]=animationTick; }
    if(blurTimerArmed) { blurIndex=count; events[count++]=blurAnimationTick; }
    if(blurPreviewProcess) { previewIndex=count; events[count++]=blurPreviewProcess; }
    if(colorTimerArmed) { colorIndex=count;events[count++]=colorAnimationTick; }
    if(colorPreviewProcess) { colorPreviewIndex=count;events[count++]=colorPreviewProcess; }
    if(wallpaperProcess) { wallpaperIndex=count;events[count++]=wallpaperProcess; }
    DWORD wait=MsgWaitForMultipleObjectsEx(count,events,timeout,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
    if(wait==WAIT_OBJECT_0||wait==WAIT_FAILED) return false;
    if(wait==WAIT_OBJECT_0+1) { paused=false; Load(); Rebuild(); }
    if(darkIndex!=MAXDWORD&&wait==WAIT_OBJECT_0+darkIndex) AdvanceAnimation();
    if(blurIndex!=MAXDWORD&&wait==WAIT_OBJECT_0+blurIndex) AdvanceBlurAnimation();
    if(previewIndex!=MAXDWORD&&wait==WAIT_OBJECT_0+previewIndex) EndBlurRadiusPreview();
    if(colorIndex!=MAXDWORD&&wait==WAIT_OBJECT_0+colorIndex) AdvanceColorAnimation();
    if(colorPreviewIndex!=MAXDWORD&&wait==WAIT_OBJECT_0+colorPreviewIndex) EndSaturationPreview();
    if(wallpaperIndex!=MAXDWORD&&wait==WAIT_OBJECT_0+wallpaperIndex) ClearColor();
    if(wait==WAIT_OBJECT_0+count) {
        MSG msg; unsigned messages=0;
        while(messages++<32&&PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {
            if(msg.message==WM_QUIT) return false;
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
    }
    return true;
}
int WINAPI wWinMain(HINSTANCE app,HINSTANCE,PWSTR args,int) {
    if(wcsstr(args,L"--self-test")) return SelfTest();
    const bool trayOnly=wcsstr(args,L"--tray-only")!=nullptr;
    instance=app; SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HANDLE mutex=CreateMutexW(nullptr,TRUE,L"Local\\Nocturne.Renderer.v1");
    if(!mutex) return 2;
    if(GetLastError()==ERROR_ALREADY_EXISTS) {
        if(trayOnly) {
            // Closing settings must never reload, unpause or replay effects.
            CloseHandle(mutex);return 0;
        }
        // Wait briefly for the first instance to publish its event handles.
        for(int i=0;i<30;i++) { HANDLE event=OpenEventW(EVENT_MODIFY_STATE,FALSE,L"Local\\Nocturne.Reload.v1");
            if(event) { SetEvent(event); CloseHandle(event); CloseHandle(mutex); return 0; } Sleep(50); }
        CloseHandle(mutex); return 3;
    }
    InitializePaths(); stopEvent=CreateEventW(nullptr,FALSE,FALSE,L"Local\\Nocturne.Stop.v1");
    reloadEvent=CreateEventW(nullptr,FALSE,FALSE,L"Local\\Nocturne.Reload.v1");
    appliedEvent=CreateEventW(nullptr,FALSE,FALSE,L"Local\\Nocturne.Applied.v1");
    if(!stopEvent||!reloadEvent||!appliedEvent) { CloseHandle(mutex); return 4; }
    WNDCLASSW wc{}; wc.hInstance=app; wc.hIcon=LoadIconW(app,MAKEINTRESOURCEW(branding::AppIcon)); wc.lpfnWndProc=SurfaceProc; wc.lpszClassName=L"Nocturne.Surface"; RegisterClassW(&wc);
    wc.lpszClassName=L"Nocturne.BlurSurface"; RegisterClassW(&wc);
    wc.lpszClassName=L"Nocturne.ColorSurface"; RegisterClassW(&wc);
    wc.lpfnWndProc=ControllerProc; wc.lpszClassName=L"Nocturne.Controller"; RegisterClassW(&wc);
    controller=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"Nocturne Controller",WS_POPUP,0,0,0,0,nullptr,nullptr,app,nullptr);
    if(!controller) { Status(L"无法创建后台控制窗口"); return 5; }
    taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    previewMessage=RegisterWindowMessageW(L"Nocturne.CapturePreview.v1");
    traceAnimation=wcsstr(args,L"--trace-animation")!=nullptr;
    if(traceAnimation) {
        FILE* file=_wfopen((configDir+L"\\animation-trace.csv").c_str(),L"w");
        if(file) { std::fputs("qpcMs,target,visibility,scale,opacity,windowVisible,submitMs\n",file); std::fclose(file); }
    }
    SetPriorityClass(GetCurrentProcess(),NORMAL_PRIORITY_CLASS);
    if(!AddTray()&&trayOnly) {
        DestroyWindow(controller);CloseHandle(stopEvent);CloseHandle(reloadEvent);CloseHandle(appliedEvent);CloseHandle(mutex);return 6;
    }
    paused=trayOnly;
    Load();
    foregroundHook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,DesktopEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    minimizeHook=SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART,EVENT_SYSTEM_MINIMIZEEND,nullptr,DesktopEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    windowHook=SetWinEventHook(EVENT_OBJECT_CREATE,EVENT_OBJECT_HIDE,nullptr,DesktopEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    cloakHook=SetWinEventHook(EVENT_OBJECT_CLOAKED,EVENT_OBJECT_UNCLOAKED,nullptr,DesktopEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    reorderHook=SetWinEventHook(EVENT_OBJECT_REORDER,EVENT_OBJECT_REORDER,nullptr,DesktopEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    iconHook=SetWinEventHook(EVENT_OBJECT_CREATE,EVENT_OBJECT_NAMECHANGE,nullptr,DesktopEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    if((!windowHook||!foregroundHook)&&(params.blurEnabled||params.saturationEnabled)) {
        params.blurEnabled=false;
        params.saturationEnabled=false;
        Rebuild();
        Status(L"窗口事件监听失败 · 已关闭自动模糊和降饱和度，暗角保留");
    } else if(!windowHook&&params.focusOnly) {
        Status(L"无法监听应用窗口 · 未启用暗角，请重新启动后台");
    } else { Rebuild(); UpdateWindowState(); }
    // Normal scheduling does not consume CPU while this thread is asleep. Idle
    // process priority starved focus notifications and animation under load.
    SetTimer(controller,1,10000,nullptr);
    while(DispatchWork()) { }
    if(foregroundHook) UnhookWinEvent(foregroundHook);
    if(minimizeHook) UnhookWinEvent(minimizeHook);
    if(windowHook) UnhookWinEvent(windowHook);
    if(cloakHook) UnhookWinEvent(cloakHook);
    if(reorderHook) UnhookWinEvent(reorderHook);
    if(iconHook) UnhookWinEvent(iconHook);
    Clear(); blurEngine.reset(); trayIcon.Reset();
    DestroyWindow(controller); CloseHandle(stopEvent); CloseHandle(reloadEvent); CloseHandle(appliedEvent); CloseHandle(mutex); return 0;
}

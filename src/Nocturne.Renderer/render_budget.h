#pragma once
#include <windows.h>
#include <dwmapi.h>
#include <vector>

// Conservative, event-driven occlusion hints. Unknown/transparent cases keep
// rendering; saving work must never expose a missing desktop effect at an edge.
namespace render_budget {
struct WindowSnapshot {
    RECT bounds{}; // DWM's visible frame, not GetWindowRect's invisible borders.
    bool application=false,visible=false,iconic=false,root=false,cloaked=true;
    bool frameKnown=false,regionCovers=false,backdrop=false,backdropKnown=false;
    LONG_PTR style=0,extended=0;
};
inline bool Nonempty(const RECT& r) { return r.left<r.right&&r.top<r.bottom; }
inline bool Contains(const RECT& outer,const RECT& inner) {
    return Nonempty(outer)&&Nonempty(inner)&&outer.left<=inner.left&&outer.top<=inner.top&&
        outer.right>=inner.right&&outer.bottom>=inner.bottom;
}
inline bool CoversMonitor(const WindowSnapshot& w,const RECT& monitor) {
    // Standard framed windows can have rounded corners, invisible resize
    // borders, glass and reserved taskbar space. Only confidently opaque,
    // frameless fullscreen windows are eligible; false negatives are harmless.
    return w.application&&w.visible&&!w.iconic&&w.root&&!w.cloaked&&w.frameKnown&&
        w.regionCovers&&!w.backdrop&&!(w.style&(WS_CAPTION|WS_THICKFRAME))&&
        !(w.extended&(WS_EX_LAYERED|WS_EX_NOREDIRECTIONBITMAP|WS_EX_TRANSPARENT|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE))&&
        Contains(w.bounds,monitor);
}
inline bool RegionCovers(HWND window,const RECT& monitor) {
    HRGN region=CreateRectRgn(0,0,0,0);
    if(!region) return false;
    int kind=GetWindowRgn(window,region);
    // ERROR means there is no custom window region. This is the documented
    // result for ordinary rectangular windows, not an assumed region bounds.
    if(kind==ERROR) { DeleteObject(region);return true; }
    RECT windowRect{};
    if(kind==NULLREGION||!GetWindowRect(window,&windowRect)) { DeleteObject(region);return false; }
    HRGN missing=CreateRectRgn(monitor.left-windowRect.left,monitor.top-windowRect.top,
        monitor.right-windowRect.left,monitor.bottom-windowRect.top);
    bool covers=missing&&CombineRgn(missing,missing,region,RGN_DIFF)==NULLREGION;
    if(missing) DeleteObject(missing);
    DeleteObject(region);
    return covers;
}
inline WindowSnapshot ReadWindowFrame(HWND window,bool application) {
    WindowSnapshot w;
    w.application=application;
    if(!application||!IsWindow(window)) return w;
    w.visible=IsWindowVisible(window)!=FALSE;
    w.iconic=IsIconic(window)!=FALSE;
    w.root=GetAncestor(window,GA_ROOT)==window;
    w.style=GetWindowLongPtrW(window,GWL_STYLE);
    w.extended=GetWindowLongPtrW(window,GWL_EXSTYLE);
    if(!w.visible||w.iconic||!w.root||
        (w.style&(WS_CAPTION|WS_THICKFRAME))||
        (w.extended&(WS_EX_LAYERED|WS_EX_NOREDIRECTIONBITMAP|WS_EX_TRANSPARENT|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE))) return w;
    DWORD cloaked=1;
    if(FAILED(DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof(cloaked)))) return w;
    w.cloaked=cloaked!=0;
    if(w.cloaked) return w;
    w.frameKnown=SUCCEEDED(DwmGetWindowAttribute(window,DWMWA_EXTENDED_FRAME_BOUNDS,&w.bounds,sizeof(w.bounds)));
    return w;
}
inline void ReadWindowBackdrop(HWND window,WindowSnapshot& w) {
    // DWMWA_SYSTEMBACKDROP_TYPE (Windows 11). Unsupported older systems leave
    // the default zero (auto); explicit Mica/Acrylic are never opaque hints.
    DWORD backdrop=0;
    w.backdropKnown=SUCCEEDED(DwmGetWindowAttribute(window,static_cast<DWMWINDOWATTRIBUTE>(38),&backdrop,sizeof(backdrop)));
    w.backdrop=w.backdropKnown&&backdrop>1;
}
inline WindowSnapshot ReadWindow(HWND window,const RECT& monitor,bool application) {
    auto w=ReadWindowFrame(window,application);
    if(!w.frameKnown||!Contains(w.bounds,monitor)) return w;
    ReadWindowBackdrop(window,w);
    w.regionCovers=RegionCovers(window,monitor);
    return w;
}
using ApplicationPredicate=bool(*)(HWND);
inline bool WindowCoversMonitor(HWND window,const RECT& monitor,ApplicationPredicate application) {
    // Reject ordinary framed/partial windows BEFORE querying their process or
    // classifying shell hosts. Desktop notifications can be frequent while an
    // app is being resized; only a genuine fullscreen candidate needs the
    // more expensive application predicate.
    return window&&application&&CoversMonitor(ReadWindow(window,monitor,true),monitor)&&application(window);
}
inline std::vector<bool> IsMonitorsOccluded(const std::vector<RECT>& monitors,ApplicationPredicate application,HWND desktopBoundary=nullptr) {
    std::vector<bool> found(monitors.size(),false);
    if(!application) return found;
    struct State {
        const std::vector<RECT>& monitors;
        std::vector<bool>& found;
        ApplicationPredicate application;
        HWND boundary,foreground;
        size_t remaining=0;
        void Visit(HWND window) {
            if(!window) return;
            // Frame/style/cloak data are shared by every monitor. Only custom
            // window-region coverage depends on an individual monitor.
            auto w=ReadWindowFrame(window,true);
            if(!w.frameKnown) return;
            bool candidate=false;
            for(size_t i=0;i<monitors.size();++i)
                if(!found[i]&&Contains(w.bounds,monitors[i])) { candidate=true;break; }
            if(!candidate) return;
            ReadWindowBackdrop(window,w);
            if(w.backdrop) return;
            std::vector<size_t> covered;
            covered.reserve(remaining);
            for(size_t i=0;i<monitors.size();++i) {
                if(found[i]||!Contains(w.bounds,monitors[i])) continue;
                w.regionCovers=RegionCovers(window,monitors[i]);
                if(CoversMonitor(w,monitors[i])) covered.push_back(i);
            }
            // Retain cheap geometry/opacity rejection before looking up a
            // process; classify each candidate at most once for the batch.
            if(covered.empty()||!application(window)) return;
            // Window queries are not atomic. A move/minimize/destruction or
            // opacity change while checking regions/processes must not turn
            // an old spanning frame into fresh proof for another monitor.
            auto current=ReadWindowFrame(window,true);
            if(!current.frameKnown) return;
            ReadWindowBackdrop(window,current);
            if(current.visible!=w.visible||current.iconic!=w.iconic||
               current.root!=w.root||current.cloaked!=w.cloaked||
               current.frameKnown!=w.frameKnown||current.backdrop!=w.backdrop||
               current.backdropKnown!=w.backdropKnown||
               current.style!=w.style||current.extended!=w.extended||
               current.bounds.left!=w.bounds.left||current.bounds.top!=w.bounds.top||
               current.bounds.right!=w.bounds.right||current.bounds.bottom!=w.bounds.bottom) return;
            for(size_t i:covered) {
                found[i]=true;
                --remaining;
            }
        }
    } state{monitors,found,application,desktopBoundary,nullptr};
    for(const auto& monitor:monitors) if(Nonempty(monitor)) ++state.remaining;
    if(!state.remaining) return found;
    state.foreground=GetAncestor(GetForegroundWindow(),GA_ROOT);
    state.Visit(state.foreground);
    if(!state.remaining) return found;
    EnumWindows([](HWND window,LPARAM data)->BOOL {
        auto& s=*reinterpret_cast<State*>(data);
        // Never treat an app placed below Explorer's desktop as occluding it.
        if(s.boundary&&window==s.boundary) return FALSE;
        if(window!=s.foreground) s.Visit(window);
        return s.remaining?TRUE:FALSE;
    },reinterpret_cast<LPARAM>(&state));
    // Failed/interrupted enumeration leaves unproven monitors live. Proven
    // opaque coverage remains valid even if a later window cannot be queried.
    return found;
}
inline bool IsMonitorOccluded(const RECT& monitor,ApplicationPredicate application,HWND desktopBoundary=nullptr) {
    return IsMonitorsOccluded({monitor},application,desktopBoundary).front();
}
inline bool ShouldSample(bool effectActive,bool visible,bool occluded) {
    return effectActive&&visible&&!occluded;
}
}

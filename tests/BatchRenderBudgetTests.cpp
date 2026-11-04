// Intercept every queried Win32 API: no real user window is read or modified.
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <dwmapi.h>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <vector>

namespace fake {
struct Counts { unsigned frame=0,cloaked=0,backdrop=0,region=0,predicate=0,valid=0; };
struct Window {
    HWND handle=nullptr,root=nullptr;
    RECT bounds{};
    LONG_PTR style=0,extended=0;
    bool valid=true,visible=true,iconic=false,cloaked=false,application=true;
    bool failCloaked=false,failFrame=false,failBackdrop=false,failRect=false;
    int mutation=0;
    bool mutateOnRegion=false,mutated=false;
    DWORD backdrop=0;
    int regionKind=ERROR;
    RECT region{},hole{};
    Counts reads;
};
struct Region { RECT bounds{};RECT hole{}; };
std::deque<Window> windows;
std::vector<HWND> order;
HWND foreground=nullptr;
unsigned enumerations=0,foregroundReads=0,visits=0;
int failEnumerationAfter=-1;
bool failAllocation=false;
Window* Find(HWND window) {
    for(auto& w:windows) if(w.handle==window) return &w;
    return nullptr;
}
Window& Add(RECT bounds) {
    windows.emplace_back();
    auto& w=windows.back();
    w.handle=reinterpret_cast<HWND>(static_cast<uintptr_t>(windows.size()));
    w.root=w.handle;w.bounds=bounds;order.push_back(w.handle);
    return w;
}
void ResetCounts() {
    enumerations=foregroundReads=visits=0;
    for(auto& w:windows) w.reads={};
}
void Reset() {
    windows.clear();order.clear();foreground=nullptr;
    failEnumerationAfter=-1;failAllocation=false;ResetCounts();
}
void Mutate(Window& w) {
    if(w.mutated||!w.mutation) return;
    w.mutated=true;
    if(w.mutation==1)w.bounds.right-=64;
    if(w.mutation==2)w.iconic=true;
    if(w.mutation==3)w.valid=false;
    if(w.mutation==4)w.extended|=WS_EX_LAYERED;
    if(w.mutation==5)w.cloaked=true;
    if(w.mutation==6)w.backdrop=2;
    if(w.mutation==7)w.visible=false;
    if(w.mutation==8)w.style=WS_CAPTION;
    if(w.mutation==9)w.root=nullptr;
    if(w.mutation==10)w.failCloaked=true;
    if(w.mutation==11)w.failFrame=true;
    if(w.mutation==12)w.failBackdrop=true;
}
bool Application(HWND window) {
    auto* w=Find(window);if(!w) return false;
    ++w->reads.predicate;
    if(!w->mutateOnRegion) Mutate(*w);
    return w->application;
}
BOOL WINAPI IsWindow(HWND window) {auto* w=Find(window);if(!w)return FALSE;++w->reads.valid;return w->valid;}
BOOL WINAPI IsWindowVisible(HWND window) {auto* w=Find(window);return w&&w->visible;}
BOOL WINAPI IsIconic(HWND window) {auto* w=Find(window);return w&&w->iconic;}
HWND WINAPI GetAncestor(HWND window,UINT) {auto* w=Find(window);return w?w->root:nullptr;}
LONG_PTR WINAPI GetWindowLongPtrW(HWND window,int index) {auto* w=Find(window);return w?(index==GWL_STYLE?w->style:w->extended):0;}
HWND WINAPI GetForegroundWindow() {++foregroundReads;return foreground;}
BOOL WINAPI EnumWindows(WNDENUMPROC callback,LPARAM data) {
    ++enumerations;
    for(auto window:order) {
        if(failEnumerationAfter>=0&&visits==static_cast<unsigned>(failEnumerationAfter)) return FALSE;
        ++visits;if(!callback(window,data)) return FALSE;
    }
    return TRUE;
}
HRESULT WINAPI DwmGetWindowAttribute(HWND window,DWORD attribute,PVOID value,DWORD) {
    auto* w=Find(window);if(!w)return E_FAIL;
    if(attribute==DWMWA_CLOAKED) {++w->reads.cloaked;if(w->failCloaked)return E_FAIL;*static_cast<DWORD*>(value)=w->cloaked;}
    else if(attribute==DWMWA_EXTENDED_FRAME_BOUNDS) {++w->reads.frame;if(w->failFrame)return E_FAIL;*static_cast<RECT*>(value)=w->bounds;}
    else if(attribute==38) {++w->reads.backdrop;if(w->failBackdrop)return E_FAIL;*static_cast<DWORD*>(value)=w->backdrop;}
    else return E_FAIL;
    return S_OK;
}
HRGN WINAPI CreateRectRgn(int left,int top,int right,int bottom) {
    return failAllocation?nullptr:reinterpret_cast<HRGN>(new Region{{left,top,right,bottom},{}});
}
BOOL WINAPI DeleteObject(HGDIOBJ region) {delete reinterpret_cast<Region*>(region);return TRUE;}
int WINAPI GetWindowRgn(HWND window,HRGN region) {
    auto* w=Find(window);if(!w)return ERROR;++w->reads.region;
    auto& r=*reinterpret_cast<Region*>(region);r.bounds=w->region;r.hole=w->hole;
    if(w->mutateOnRegion) Mutate(*w);
    return w->regionKind;
}
BOOL WINAPI GetWindowRect(HWND window,LPRECT rect) {
    auto* w=Find(window);if(!w||w->failRect)return FALSE;*rect=w->bounds;return TRUE;
}
int WINAPI CombineRgn(HRGN,HRGN missing,HRGN covered,int) {
    const auto& a=*reinterpret_cast<Region*>(missing);
    const auto& b=*reinterpret_cast<Region*>(covered);
    bool contains=b.bounds.left<=a.bounds.left&&b.bounds.top<=a.bounds.top&&
        b.bounds.right>=a.bounds.right&&b.bounds.bottom>=a.bounds.bottom;
    bool hole=b.hole.left<b.hole.right&&b.hole.top<b.hole.bottom&&
        b.hole.left<a.bounds.right&&b.hole.right>a.bounds.left&&
        b.hole.top<a.bounds.bottom&&b.hole.bottom>a.bounds.top;
    return contains&&!hole?NULLREGION:SIMPLEREGION;
}
}

#define IsWindow fake::IsWindow
#define IsWindowVisible fake::IsWindowVisible
#define IsIconic fake::IsIconic
#define GetAncestor fake::GetAncestor
#define GetWindowLongPtrW fake::GetWindowLongPtrW
#define GetForegroundWindow fake::GetForegroundWindow
#define EnumWindows fake::EnumWindows
#define DwmGetWindowAttribute fake::DwmGetWindowAttribute
#define CreateRectRgn fake::CreateRectRgn
#define DeleteObject fake::DeleteObject
#define GetWindowRgn fake::GetWindowRgn
#define GetWindowRect fake::GetWindowRect
#define CombineRgn fake::CombineRgn
#include "../src/Nocturne.Renderer/render_budget.h"

int main() {
    unsigned checks=0,failures=0;
    auto check=[&](bool good,const char* name){++checks;if(!good){++failures;std::printf("FAIL %s\n",name);}};
    const RECT left{-1920,0,0,1080},right{0,0,1920,1080},span{-1920,0,1920,1080},empty{0,0,0,100};
    const std::vector<RECT> monitors{left,right,empty};
    auto query=[&](HWND boundary=nullptr){return render_budget::IsMonitorsOccluded(monitors,fake::Application,boundary);};
    fake::Reset();
    check(render_budget::IsMonitorsOccluded({},fake::Application).empty(),"empty batch retains result shape");
    check(render_budget::IsMonitorsOccluded({empty},fake::Application)==std::vector<bool>{false},"invalid monitor is fail-open");
    check(render_budget::IsMonitorsOccluded(monitors,nullptr)==std::vector<bool>({false,false,false}),"null classifier is fail-open");
    check(!fake::foregroundReads&&!fake::enumerations,"empty invalid and null queries never touch Win32");

    auto& foreground=fake::Add(span);fake::foreground=foreground.handle;
    check(query()==std::vector<bool>({true,true,false}),"foreground spanning two monitors covers both");
    check(!fake::enumerations&&fake::foregroundReads==1,"foreground fast path avoids enumeration");
    check(foreground.reads.frame==2&&foreground.reads.cloaked==2&&foreground.reads.backdrop==2,"spanning frame and DWM properties are shared and revalidated once");
    check(foreground.reads.predicate==1&&foreground.reads.region==2,"spanning app is classified once with per-monitor region proofs");
    fake::ResetCounts();
    check(render_budget::IsMonitorOccluded(left,fake::Application)&&render_budget::IsMonitorOccluded(right,fake::Application),"single-monitor compatibility matches batch results");
    check(foreground.reads.frame==4&&foreground.reads.predicate==2,"separate compatibility calls duplicate validation avoided by batch");

    fake::Reset();
    auto& partial=fake::Add({-100,0,100,200});fake::foreground=partial.handle;
    auto& leftApp=fake::Add(left);auto& rightApp=fake::Add(right);
    check(query()==std::vector<bool>({true,true,false}),"separate fullscreen apps cover their own monitors");
    check(fake::enumerations==1&&fake::foregroundReads==1,"all monitors share a single enumeration");
    check(partial.reads.frame==1&&!partial.reads.predicate,"foreground is not reread when enumeration encounters it");
    check(leftApp.reads.frame==2&&rightApp.reads.frame==2&&leftApp.reads.predicate==1&&rightApp.reads.predicate==1,"each fullscreen window is classified once and revalidated once");

    fake::Reset();
    auto& above=fake::Add(left);auto& boundary=fake::Add(span);auto& below=fake::Add(right);
    check(query(boundary.handle)==std::vector<bool>({true,false,false}),"desktop boundary excludes windows below desktop");
    check(above.reads.predicate==1&&!boundary.reads.valid&&!below.reads.valid,"boundary stops enumeration before reading lower windows");

    fake::Reset();
    auto& shell=fake::Add(span);shell.application=false;fake::foreground=shell.handle;
    check(query()==std::vector<bool>({false,false,false}),"covering shell host never occludes");
    check(shell.reads.frame==1&&shell.reads.predicate==1,"rejected foreground is classified only once across monitors and enumeration");

    fake::Reset();
    auto& shaped=fake::Add(span);shaped.regionKind=COMPLEXREGION;
    shaped.region={0,0,3840,1080};shaped.hole={10,10,20,20};
    check(query()==std::vector<bool>({false,true,false}),"custom region hole keeps only affected monitor rendering");
    check(shaped.reads.predicate==1&&shaped.reads.region==2,"region failure on one monitor does not prevent checking another");

    for(bool duringRegion:{false,true}) for(int mutation=1;mutation<=12;++mutation) {
        fake::Reset();auto& w=fake::Add(span);fake::foreground=w.handle;
        w.mutation=mutation;w.mutateOnRegion=duringRegion;
        check(query()==std::vector<bool>({false,false,false}),"mid-query geometry visibility opacity or query-status change never commits stale coverage");
        check(w.mutated&&w.reads.predicate==1,"changing candidate is classified at most once without retrying foreground");
    }
    fake::Reset();auto& unsupported=fake::Add(span);unsupported.failBackdrop=true;
    check(query()==std::vector<bool>({true,true,false}),"consistently unsupported backdrop attribute preserves older Windows policy");

    for(int excluded=0;excluded<10;++excluded) {
        fake::Reset();auto& w=fake::Add(span);
        if(excluded==0)w.style=WS_CAPTION;
        if(excluded==1)w.style=WS_THICKFRAME;
        if(excluded==2)w.extended=WS_EX_LAYERED;
        if(excluded==3)w.extended=WS_EX_NOREDIRECTIONBITMAP;
        if(excluded==4)w.extended=WS_EX_TRANSPARENT;
        if(excluded==5)w.extended=WS_EX_TOOLWINDOW;
        if(excluded==6)w.extended=WS_EX_NOACTIVATE;
        if(excluded==7)w.backdrop=2;
        if(excluded==8)w.cloaked=true;
        if(excluded==9)w.iconic=true;
        check(query()==std::vector<bool>({false,false,false}),"excluded styles and states remain fail-open");
        check(!w.reads.predicate,"excluded window never requires expensive process classification");
    }

    for(int failure=0;failure<5;++failure) {
        fake::Reset();auto& w=fake::Add(span);
        if(failure==0)w.failCloaked=true;
        if(failure==1)w.failFrame=true;
        if(failure==2)fake::failAllocation=true;
        if(failure==3)w.regionKind=NULLREGION;
        if(failure==4){w.regionKind=SIMPLEREGION;w.failRect=true;}
        check(query()==std::vector<bool>({false,false,false}),"failed frame region or allocation never creates false coverage");
    }
    fake::Reset();fake::Add(left);fake::Add(right);fake::failEnumerationAfter=0;
    check(query()==std::vector<bool>({false,false,false}),"enumeration failure leaves unproven monitors live");
    fake::ResetCounts();fake::failEnumerationAfter=1;
    check(query()==std::vector<bool>({true,false,false}),"partial enumeration retains only proven monitor coverage");
    std::printf("{\"BatchRenderBudgetChecks\":%u,\"Failures\":%u,\"TwoMonitorFrameReads\":2,\"TwoMonitorClassifications\":1,\"UserWindowsAndSettingsUntouched\":true}\n",checks,failures);
    return failures?1:0;
}

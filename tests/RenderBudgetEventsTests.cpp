// Exercise production event/budget integration without touching user windows.
// Only an invisible OWN controller/child exists. Timer and occlusion queries
// are intercepted; no real timers, Composition surfaces or desktop scans run.
#define UNICODE
#define _UNICODE
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include "../src/Nocturne.Renderer/render_budget.h"

namespace {
unsigned timerStarts=0,timerStops=0,monitorQueries=0,batchQueries=0;
UINT_PTR lastTimer=0,lastKilledTimer=0;
UINT lastPeriod=0;
HWND timerWindow=nullptr;
bool monitorCovered[2]{true,true};
UINT_PTR WINAPI TestSetTimer(HWND window,UINT_PTR id,UINT period,TIMERPROC) {
    ++timerStarts;timerWindow=window;lastTimer=id;lastPeriod=period;return id;
}
BOOL WINAPI TestKillTimer(HWND,UINT_PTR id) { ++timerStops;lastKilledTimer=id;return TRUE; }
}
namespace render_budget {
inline std::vector<bool> TestMonitorOcclusion(const std::vector<RECT>& monitors,ApplicationPredicate,HWND) {
    if(!monitors.empty()) ++batchQueries;
    std::vector<bool> result;
    for(const auto& monitor:monitors) {
        ++monitorQueries;result.push_back(monitorCovered[monitor.left<0?1:0]);
    }
    return result;
}
}
#define IsMonitorsOccluded TestMonitorOcclusion
#define SetTimer TestSetTimer
#define KillTimer TestKillTimer
#define wWinMain PenumbraApplicationMain
#include "../src/Nocturne.Renderer/main.cpp"
#undef wWinMain
#undef KillTimer
#undef SetTimer
#undef IsMonitorsOccluded

namespace {
unsigned checks=0,failures=0;
void Check(bool good,const char* name) {
    ++checks;if(!good){++failures;std::printf("FAIL %s\n",name);}
}
unsigned DrainState() {
    unsigned messages=0;MSG msg{};
    while(PeekMessageW(&msg,controller,WindowStateMessage,WindowStateMessage,PM_REMOVE))++messages;
    windowStatePending=false;return messages;
}
void Hooks() {
    foregroundHook=reinterpret_cast<HWINEVENTHOOK>(1);
    minimizeHook=reinterpret_cast<HWINEVENTHOOK>(2);
    windowHook=reinterpret_cast<HWINEVENTHOOK>(3);
    cloakHook=reinterpret_cast<HWINEVENTHOOK>(4);
    locationHook=reinterpret_cast<HWINEVENTHOOK>(5);
    moveSizeHook=reinterpret_cast<HWINEVENTHOOK>(6);
    iconHook=reinterpret_cast<HWINEVENTHOOK>(7);
}
void Event(HWINEVENTHOOK hook,DWORD event,HWND window,LONG object=OBJID_WINDOW) {
    DesktopEvent(hook,event,window,object,CHILDID_SELF,0,0);
}
bool AllOccluded(bool expected) {
    for(const auto& surface:blurSurfaces)if(surface.occluded!=expected)return false;
    for(const auto& surface:colorSurfaces)if(surface.occluded!=expected)return false;
    return true;
}
void MarkOccluded() {
    for(auto& surface:blurSurfaces)surface.occluded=true;
    for(auto& surface:colorSurfaces)surface.occluded=true;
}
void Reconsider() { monitorQueries=0;UpdateRenderBudget(ForegroundKind::Application); }
}

int main() {
    HWND foreground=GetForegroundWindow();
    instance=GetModuleHandleW(nullptr);
    controller=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"Penumbra hidden budget event test",
        WS_POPUP,-20000,-20000,8,8,nullptr,nullptr,instance,nullptr);
    if(!controller){std::puts("FAIL hidden own controller unavailable");return 1;}
    HWND child=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,0,0,1,1,controller,nullptr,instance,nullptr);
    if(!child){DestroyWindow(controller);return 1;}
    Hooks();params=Params{};paused=false;
    const RECT monitors[2]{{0,0,1920,1080},{-1920,-200,0,880}};
    for(const auto& rect:monitors) {
        Surface monitor;monitor.rect=rect;surfaces.push_back(std::move(monitor));
        BlurSurface blur;blur.rect=rect;blurSurfaces.push_back(std::move(blur));
        ColorSurface color;color.rect={rect.left+10,rect.top+10,rect.right-10,rect.bottom-10};
        colorSurfaces.push_back(std::move(color));
    }
    Reconsider();
    Check(AllOccluded(true)&&monitorQueries==2&&batchQueries==1,"one batch serves both monitors, blur and partial-wallpaper color filters");
    Check(!UpdateRenderBudget(ForegroundKind::Application),"unchanged fullscreen budget produces no graph update");
    monitorCovered[0]=false;Reconsider();
    Check(!blurSurfaces[0].occluded&&!colorSurfaces[0].occluded&&blurSurfaces[1].occluded&&colorSurfaces[1].occluded,
        "only the exposed monitor wakes; adjacent negative-coordinate monitor remains occluded");
    monitorCovered[0]=true;
    HWINEVENTHOOK* required[]{&foregroundHook,&minimizeHook,&windowHook,&cloakHook,&locationHook,&moveSizeHook};
    for(auto hook:required) {
        Hooks();*hook=nullptr;MarkOccluded();Reconsider();
        Check(AllOccluded(false)&&monitorQueries==0,"each missing required hook disables culling without scanning");
    }
    Hooks();
    for(auto kind:{ForegroundKind::Transient,ForegroundKind::Desktop}) {
        MarkOccluded();monitorQueries=0;bool changed=UpdateRenderBudget(kind);
        Check(changed&&AllOccluded(false)&&monitorQueries==0,"shell transitions and desktop focus immediately fail open");
    }
    MarkOccluded();DrainState();
    Event(moveSizeHook,EVENT_SYSTEM_MOVESIZESTART,controller);
    Check(occlusionMovingWindow==controller&&windowStatePending&&DrainState()==1,"move start queues a coalesced update and records only the moving window");
    Reconsider();
    Check(AllOccluded(false)&&monitorQueries==0,"move gesture wakes filters before any desktop is exposed");
    Event(moveSizeHook,EVENT_SYSTEM_MOVESIZEEND,controller);
    Check(!occlusionMovingWindow&&DrainState()==1,"move end clears gesture state and schedules re-evaluation");
    Reconsider();
    Check(AllOccluded(true)&&monitorQueries==2,"fullscreen culling resumes after moving ends");
    for(DWORD event:{DWORD(EVENT_SYSTEM_MINIMIZESTART),DWORD(EVENT_SYSTEM_MINIMIZEEND)}) {
        const unsigned starts=timerStarts,stops=timerStops;
        Event(minimizeHook,event,controller);
        Check(occlusionSettling&&!occlusionMovingWindow&&timerStarts==starts+1&&lastTimer==8&&lastPeriod==450&&timerWindow==controller&&DrainState()==1,
            "either minimize or restore event starts exactly one 450ms grace timer");
        Reconsider();
        Check(AllOccluded(false)&&monitorQueries==0,"minimize/restore grace immediately restores sampling");
        // In production this is the single timer callback. No matching END is
        // emitted here: minimizing may remain minimized for hours.
        ControllerProc(controller,WM_TIMER,8,0);
        Check(!occlusionSettling&&timerStops==stops+1&&lastKilledTimer==8,"grace expires and kills itself without a paired minimize event");
        Reconsider();
        Check(AllOccluded(true)&&monitorQueries==2,"culling can resume when grace expires with no END event");
    }
    Event(moveSizeHook,EVENT_SYSTEM_MOVESIZESTART,controller);
    Event(minimizeHook,EVENT_SYSTEM_MINIMIZESTART,controller);
    ControllerProc(controller,WM_TIMER,8,0);DrainState();Reconsider();
    Check(!occlusionSettling&&occlusionMovingWindow==controller&&AllOccluded(false)&&monitorQueries==0,
        "minimize grace expiry does not erase an overlapping move gesture");
    Event(moveSizeHook,EVENT_SYSTEM_MOVESIZEEND,controller);DrainState();Reconsider();
    Check(AllOccluded(true),"ending the outstanding move allows culling again");
    occlusionMovingWindow=reinterpret_cast<HWND>(static_cast<UINT_PTR>(0x7ffff001));Reconsider();
    Check(!occlusionMovingWindow&&AllOccluded(true),"destroyed moving window cannot permanently disable culling");
    DrainState();Event(locationHook,EVENT_OBJECT_LOCATIONCHANGE,controller);
    Event(locationHook,EVENT_OBJECT_LOCATIONCHANGE,controller);
    Check(windowStatePending&&DrainState()==1,"repeated root location events coalesce into one update");
    Event(locationHook,EVENT_OBJECT_LOCATIONCHANGE,child);
    Check(!windowStatePending&&DrainState()==0,"child location chatter does not trigger desktop scanning");
    Event(locationHook,EVENT_OBJECT_LOCATIONCHANGE,controller,OBJID_CLIENT);
    Check(!windowStatePending&&DrainState()==0,"non-window accessibility location chatter is ignored");
    params.blurEnabled=true;
    Event(iconHook,EVENT_OBJECT_STATECHANGE,controller);
    Check(windowStatePending&&DrainState()==1,"top-level opacity/style/state changes trigger a budget recheck");
    Event(iconHook,EVENT_OBJECT_STATECHANGE,child);
    Check(!windowStatePending&&DrainState()==0,"child state changes cannot wake desktop scans");
    params.blurEnabled=false;
    Event(iconHook,EVENT_OBJECT_STATECHANGE,controller);
    Check(!windowStatePending&&DrainState()==0,"disabled effects do not scan on unrelated root state changes");
    Check(!IsWindowVisible(controller)&&!IsWindowVisible(child),"all own test windows stayed hidden");
    Check(GetForegroundWindow()==foreground,"foreground was never changed");
    Clear();DestroyWindow(child);DestroyWindow(controller);controller=nullptr;
    std::printf("{\"RenderBudgetEventChecks\":%u,\"Failures\":%u,\"UserWindowsAndSettingsUntouched\":true,\"DesktopQueriesIntercepted\":true,\"RealTimersCreated\":false}\n",checks,failures);
    return failures?1:0;
}

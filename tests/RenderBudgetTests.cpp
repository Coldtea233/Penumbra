// Pure policy checks. No real application/desktop windows are created or moved.
#define _WIN32_WINNT 0x0A00
#include "../src/Nocturne.Renderer/render_budget.h"
#include <cstdio>
#include <vector>

int main() {
    unsigned checks=0,failures=0;
    auto check=[&](bool good,const char* name){++checks;if(!good){++failures;std::printf("FAIL %s\n",name);}};
    using render_budget::WindowSnapshot;
    auto opaque=[](RECT bounds){WindowSnapshot w;w.bounds=bounds;w.application=w.visible=w.root=w.frameKnown=w.regionCovers=true;w.cloaked=false;return w;};
    const std::vector<RECT> monitors{{0,0,1920,1080},{-1920,-200,0,880},{1920,0,5760,2160},{0,-2160,3840,0}};
    for(const auto& monitor:monitors) {
        auto w=opaque(monitor);
        check(render_budget::CoversMonitor(w,monitor),"exact fullscreen opaque app is eligible");
        w.bounds={monitor.left-16,monitor.top-16,monitor.right+16,monitor.bottom+16};
        check(render_budget::CoversMonitor(w,monitor),"oversized fullscreen safely contains monitor");
        for(int edge=0;edge<4;++edge) for(int exposed:{1,2,48,100}) {
            w=opaque(monitor);
            if(edge==0) w.bounds.left+=exposed;
            if(edge==1) w.bounds.top+=exposed;
            if(edge==2) w.bounds.right-=exposed;
            if(edge==3) w.bounds.bottom-=exposed;
            check(!render_budget::CoversMonitor(w,monitor),"any visible edge or reserved taskbar keeps rendering");
        }
        for(auto style:{LONG_PTR(WS_CAPTION),LONG_PTR(WS_THICKFRAME),LONG_PTR(WS_OVERLAPPEDWINDOW)}) {
            w=opaque(monitor);w.style=style;
            check(!render_budget::CoversMonitor(w,monitor),"framed and potentially rounded windows stay live");
        }
        for(auto extended:{LONG_PTR(WS_EX_LAYERED),LONG_PTR(WS_EX_NOREDIRECTIONBITMAP),LONG_PTR(WS_EX_TRANSPARENT),LONG_PTR(WS_EX_TOOLWINDOW),LONG_PTR(WS_EX_NOACTIVATE)}) {
            w=opaque(monitor);w.extended=extended;
            check(!render_budget::CoversMonitor(w,monitor),"unknown transparency or tool windows never cull");
        }
        for(int missing=0;missing<9;++missing) {
            w=opaque(monitor);
            if(missing==0)w.application=false;
            if(missing==1)w.visible=false;
            if(missing==2)w.iconic=true;
            if(missing==3)w.root=false;
            if(missing==4)w.cloaked=true;
            if(missing==5)w.frameKnown=false;
            if(missing==6)w.regionCovers=false;
            if(missing==7)w.backdrop=true;
            if(missing==8)w.bounds.right=w.bounds.left;
            check(!render_budget::CoversMonitor(w,monitor),"missing visibility or opacity evidence keeps rendering");
        }
        for(const auto& other:monitors) {
            w=opaque(other);
            const bool expected=render_budget::Contains(other,monitor);
            check(render_budget::CoversMonitor(w,monitor)==expected,"occlusion is isolated per monitor including negative coordinates");
        }
    }
    RECT left{-1920,0,0,1080},right{0,0,1920,1080},span{-1920,0,1920,1080};
    auto w=opaque(span);
    check(render_budget::CoversMonitor(w,left)&&render_budget::CoversMonitor(w,right),"spanning fullscreen can cull both completely covered monitors");
    w=opaque(left);
    check(render_budget::CoversMonitor(w,left)&&!render_budget::CoversMonitor(w,right),"single fullscreen leaves adjacent monitor live");
    RECT invalid{0,0,0,1080};
    check(!render_budget::CoversMonitor(w,invalid),"empty monitor is never considered occluded");
    check(!render_budget::IsMonitorOccluded(span,nullptr),"missing application predicate is fail-open");
    for(bool active:{false,true})for(bool visible:{false,true})for(bool occluded:{false,true})
        check(render_budget::ShouldSample(active,visible,occluded)==(active&&visible&&!occluded),"only visible active non-occluded filters sample");
    std::printf("{\"RenderBudgetChecks\":%u,\"Failures\":%u,\"UserWindowsAndSettingsUntouched\":true}\n",checks,failures);
    return failures?1:0;
}

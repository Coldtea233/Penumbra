#include "../src/Nocturne.Renderer/blur_bounds.h"
#include <cstdio>
#include <vector>

int main() {
    unsigned checks=0,failures=0;
    auto check=[&](bool good,const char* text) { ++checks;if(!good){++failures;std::printf("FAIL %s\n",text);} };
    // No changes to Explorer, auto-hide preferences, foreground or settings.
    for(const RECT& m:std::vector<RECT>{{0,0,1920,1080},{-1920,-200,0,880},{1920,0,5760,2160},{0,-2160,3840,0}}) {
        for(int edge=0;edge<4;++edge) for(int exposed=0;exposed<=48;++exposed) {
            RECT bar{},expected=m;
            if(edge==0) {bar={m.left,m.top+exposed-48,m.right,m.top+exposed};expected.top+=exposed;}
            if(edge==1) {bar={m.left,m.bottom-exposed,m.right,m.bottom-exposed+48};expected.bottom-=exposed;}
            if(edge==2) {bar={m.left+exposed-48,m.top,m.left+exposed,m.bottom};expected.left+=exposed;}
            if(edge==3) {bar={m.right-exposed,m.top,m.right-exposed+48,m.bottom};expected.right-=exposed;}
            RECT actual=blur_bounds::ExcludeTaskbar(m,m,bar),overlap{};
            check(EqualRect(&actual,&expected),"every reveal/hide step clips only the visible thickness");
            check(!IntersectRect(&overlap,&actual,&bar),"visible taskbar never belongs to the filtered area");
            if(exposed==0) check(EqualRect(&actual,&m),"fully hidden bar restores the full monitor");
            if(exposed==2) {
                LONG excluded=edge<2?(m.bottom-m.top)-(actual.bottom-actual.top):(m.right-m.left)-(actual.right-actual.left);
                check(excluded==2,"two-pixel activation edge does not reserve 48 pixels");
            }
            RECT limited=m;limited.top+=60;limited.left+=60;
            RECT combined=blur_bounds::ExcludeTaskbar(limited,m,bar);
            check(combined.top>=limited.top&&combined.left>=limited.left&&combined.right<=limited.right&&combined.bottom<=limited.bottom,"multiple reservations never expand an existing clip");
        }
        RECT offscreen{m.right+10,m.top,m.right+58,m.bottom};
        RECT unchanged=blur_bounds::ExcludeTaskbar(m,m,offscreen);
        check(EqualRect(&m,&unchanged),"neighbor-monitor taskbar does not remove wallpaper");
        RECT invalid{m.left,m.top,m.left,m.top};unchanged=blur_bounds::ExcludeTaskbar(m,m,invalid);
        check(EqualRect(&m,&unchanged),"empty taskbar does not change the clip");
    }
    std::printf("{\"TaskbarBoundsChecks\":%u,\"Failures\":%u,\"ExplorerAndSettingsUntouched\":true}\n",checks,failures);
    return failures?1:0;
}

#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vignette_pixels {
// Temporary O(width) scratch only. Preserve the original double expressions,
// smoothstep, rounding and premultiplied BGRA; no reduced-resolution mask.
template<class Parameters>
void Fill(uint32_t* pixels,int width,int height,const Parameters& p) {
    if(!pixels||width<1||height<1) return;
    std::vector<double> columns(width);
    for(int x=0;x<width;++x) {
        double dx=((x+.5)/width-p.cx)*2/p.shape;
        columns[x]=dx*dx;
    }
    const unsigned char red=p.tint==1?14:p.tint==2?27:0;
    const unsigned char green=p.tint==1?22:p.tint==2?18:0;
    const unsigned char blue=p.tint==1?40:p.tint==2?16:0;
    for(int y=0;y<height;++y) {
        double dy=((y+.5)/height-p.cy)*2;
        double vertical=dy*dy;
        for(int x=0;x<width;++x) {
            double t=std::clamp((std::sqrt(columns[x]+vertical)-p.radius)/p.feather,0.0,1.0);
            double a=p.opacity*t*t*(3-2*t);
            uint32_t alpha=static_cast<unsigned char>(std::lround(255*a));
            if(p.tint==0) *pixels++=alpha<<24;
            else {
                uint32_t b=static_cast<unsigned char>(std::lround(blue*a));
                uint32_t g=static_cast<unsigned char>(std::lround(green*a));
                uint32_t r=static_cast<unsigned char>(std::lround(red*a));
                *pixels++=b|(g<<8)|(r<<16)|(alpha<<24);
            }
        }
    }
}
}

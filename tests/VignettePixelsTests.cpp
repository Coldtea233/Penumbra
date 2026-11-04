// Byte-for-byte comparison against the original full-resolution renderer.
// No HWND, desktop capture, settings or running background process involved.
#include "../src/Nocturne.Renderer/vignette_pixels.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>

struct Parameters { double opacity=.68,radius=.48,feather=.65,shape=1,cx=.5,cy=.5;int tint=0; };
void Reference(uint32_t* pixels,int width,int height,const Parameters& p) {
    unsigned char rgb[3]{};
    if(p.tint==1) {rgb[0]=14;rgb[1]=22;rgb[2]=40;}
    if(p.tint==2) {rgb[0]=27;rgb[1]=18;rgb[2]=16;}
    auto* pixel=reinterpret_cast<unsigned char*>(pixels);
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        double nx=(x+.5)/width,ny=(y+.5)/height;
        double dx=(nx-p.cx)*2/p.shape,dy=(ny-p.cy)*2;
        double t=std::clamp((std::sqrt(dx*dx+dy*dy)-p.radius)/p.feather,0.0,1.0);
        double a=p.opacity*t*t*(3-2*t);
        *pixel++=(unsigned char)std::lround(rgb[2]*a);*pixel++=(unsigned char)std::lround(rgb[1]*a);
        *pixel++=(unsigned char)std::lround(rgb[0]*a);*pixel++=(unsigned char)std::lround(255*a);
    }
}
uint64_t Hash(const std::vector<uint32_t>& pixels) {
    uint64_t value=1469598103934665603ULL;
    for(auto pixel:pixels) {value^=pixel;value*=1099511628211ULL;}
    return value;
}
void Benchmark(int width,int height,int tint) {
    Parameters p;p.tint=tint;
    std::vector<uint32_t> before(size_t(width)*height),after(before.size());
    std::vector<double> referenceTimes,optimizedTimes;
    auto measure=[&](bool optimized) {
        auto start=std::chrono::steady_clock::now();
        if(optimized) vignette_pixels::Fill(after.data(),width,height,p);
        else Reference(before.data(),width,height,p);
        return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    };
    measure(false);measure(true);
    for(int i=0;i<9;++i) {
        // Alternate order so the optimized loop is not always the warm run.
        if(i%2) {optimizedTimes.push_back(measure(true));referenceTimes.push_back(measure(false));}
        else {referenceTimes.push_back(measure(false));optimizedTimes.push_back(measure(true));}
    }
    std::sort(referenceTimes.begin(),referenceTimes.end());std::sort(optimizedTimes.begin(),optimizedTimes.end());
    std::printf("{\"Width\":%d,\"Height\":%d,\"Tint\":%d,\"ReferenceMedianMs\":%.3f,\"OptimizedMedianMs\":%.3f,\"SavedPercent\":%.2f,\"TemporaryScratchBytes\":%zu,\"Identical\":%s,\"Hash\":\"%016llX\"}\n",
        width,height,tint,referenceTimes[4],optimizedTimes[4],100*(1-optimizedTimes[4]/referenceTimes[4]),
        size_t(width)*sizeof(double),before==after?"true":"false",(unsigned long long)Hash(after));
}
int main(int argc,char** argv) {
    unsigned checks=0,failures=0;uint64_t compared=0;
    auto compare=[&](int width,int height,const Parameters& p) {
        std::vector<uint32_t> before(size_t(width)*height),after(before.size());
        Reference(before.data(),width,height,p);vignette_pixels::Fill(after.data(),width,height,p);
        ++checks;compared+=before.size();
        if(before!=after) {++failures;std::printf("FAIL %dx%d tint=%d radius=%.9f feather=%.9f shape=%.9f center=%.9f,%.9f opacity=%.9f\n",width,height,p.tint,p.radius,p.feather,p.shape,p.cx,p.cy,p.opacity);}
    };
    for(int tint=0;tint<3;++tint) {
        Parameters p;p.tint=tint;
        for(auto size:std::vector<std::pair<int,int>>{{1,1},{1,97},{97,1},{2,2},{41,23},{512,288},{2880,1620},{3840,2160}})
            compare(size.first,size.second,p);
        for(double value:{0.,.01,.5,.68,1.}) {auto q=p;q.opacity=value;compare(103,71,q);}
        for(double value:{.05,.48,1.,1.5}) {auto q=p;q.radius=value;compare(103,71,q);q=p;q.feather=value;compare(103,71,q);}
        for(double value:{.3,1.,2.5}) {auto q=p;q.shape=value;compare(103,71,q);}
        for(double cx:{0.,.5,1.})for(double cy:{0.,.5,1.}) {auto q=p;q.cx=cx;q.cy=cy;compare(103,71,q);}
    }
    std::mt19937 random(175);std::uniform_real_distribution<double> unit(0,1);
    for(int i=0;i<300;++i) {
        Parameters p;p.opacity=unit(random);p.radius=.05+1.45*unit(random);p.feather=.05+1.45*unit(random);
        p.shape=.3+2.2*unit(random);p.cx=unit(random);p.cy=unit(random);p.tint=i%3;
        compare(1+random()%251,1+random()%149,p);
    }
    uint32_t sentinel=0x12345678;
    vignette_pixels::Fill(&sentinel,0,3,Parameters{});vignette_pixels::Fill(nullptr,10,10,Parameters{});
    ++checks;if(sentinel!=0x12345678)++failures;
    std::printf("{\"VignettePixelChecks\":%u,\"PixelsCompared\":%llu,\"Failures\":%u}\n",checks,(unsigned long long)compared,failures);
    if(!failures&&argc>1&&!std::strcmp(argv[1],"--benchmark")) for(int tint=0;tint<3;++tint) {
        Benchmark(2880,1620,tint);Benchmark(3840,2160,tint);
    }
    return failures?1:0;
}

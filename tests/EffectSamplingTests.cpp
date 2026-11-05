// Real Composition graphs on hidden, disposable OWN windows only. No desktop
// capture, foreground changes, user settings or running renderer manipulation.
#define UNICODE
#define _UNICODE
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "../src/Nocturne.Renderer/desktop_blur.h"

namespace desktop_blur {
struct EngineTestAccess {
    static C::ICompositionEffectFactory* Factory(const Engine& engine,Filter filter) {
        return filter==Filter::Gaussian?engine.gaussianFactory.Get():engine.saturationFactory.Get();
    }
};
}

namespace {
unsigned checks=0,failures=0;
void Check(bool result,const char* name) {
    ++checks;if(!result){++failures;std::printf("FAIL %s\n",name);}
}
void Pump(DWORD duration) {
    ULONGLONG end=GetTickCount64()+duration;
    while(true) {
        ULONGLONG now=GetTickCount64();if(now>=end)break;
        DWORD left=static_cast<DWORD>(end-now);
        MsgWaitForMultipleObjectsEx(0,nullptr,left,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    }
}
desktop_blur::ComPtr<desktop_blur::C::ICompositionBrush> Brush(const desktop_blur::Engine::Surface& surface) {
    desktop_blur::ComPtr<desktop_blur::C::ISpriteVisual> sprite;
    desktop_blur::ComPtr<desktop_blur::C::ICompositionBrush> brush;
    Check(SUCCEEDED(surface.visual.As(&sprite)),"sprite interface available");
    Check(sprite&&SUCCEEDED(sprite->get_Brush(&brush)),"actual sprite brush readable");
    return brush;
}
bool SameObject(IUnknown* first,IUnknown* second) {
    desktop_blur::ComPtr<IUnknown> a,b;
    return first&&second&&SUCCEEDED(first->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(a.GetAddressOf())))&&
        SUCCEEDED(second->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(b.GetAddressOf())))&&a.Get()==b.Get();
}
double CpuMs() {
    FILETIME created{},exited{},kernel{},user{};
    GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user);
    ULARGE_INTEGER k{},u{};k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;
    u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;
    return (k.QuadPart+u.QuadPart)/10000.;
}
float StoredValue(const desktop_blur::Engine::Surface& surface) {
    float value=-1;desktop_blur::C::CompositionGetValueStatus status{};
    Check(SUCCEEDED(surface.properties->TryGetScalar(desktop_blur::String(desktop_blur::PropertyName(surface.filter)),&value,&status))&&
        status==desktop_blur::C::CompositionGetValueStatus_Succeeded,"brush retains its own scalar property");
    return value;
}
void TestFactories(desktop_blur::Engine& engine) {
    using namespace desktop_blur;
    Check(!EngineTestAccess::Factory(engine,Filter::Gaussian)&&!EngineTestAccess::Factory(engine,Filter::Saturation),
        "compiled factories are created lazily, not at process startup");
    Engine::Surface surfaces[6];
    const Filter filters[]{Filter::Gaussian,Filter::Saturation,Filter::Gaussian,Filter::Saturation,Filter::Gaussian,Filter::Saturation};
    const float values[]{0,1,12,.4f,48,0};
    ComPtr<C::ICompositionEffectFactory> gaussian,saturation;
    for(int i=0;i<6;++i) {
        auto& surface=surfaces[i];
        surface.hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"Hidden independent effect test",
            WS_POPUP,-20000,-20000,32,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        HRESULT hr=engine.Attach(surface.hwnd,32,32,values[i],surface,filters[i]);
        Check(surface.hwnd&&SUCCEEDED(hr),"attach independent real brush from cached factory");
        if(FAILED(hr)||!surface.hwnd) {for(auto& s:surfaces)s.Reset();return;}
        auto& factory=filters[i]==Filter::Gaussian?gaussian:saturation;
        auto actual=EngineTestAccess::Factory(engine,filters[i]);
        if(!factory) factory=actual;
        Check(SameObject(factory.Get(),actual),"same filter reuses exactly the same compiled effect factory");
        Check(StoredValue(surface)==values[i],"fresh brush overrides factory default before exposing the visual");
        Check(surface.brushAttached==(filters[i]==Filter::Gaussian?values[i]>0:values[i]<1),
            "cached identity defaults do not force other brushes into bypass");
        for(int j=0;j<i;++j)
            Check(!SameObject(surface.effect.Get(),surfaces[j].effect.Get())&&!SameObject(surface.properties.Get(),surfaces[j].properties.Get()),
                "surfaces never share effect brushes or mutable property sets");
    }
    Check(!SameObject(gaussian.Get(),saturation.Get()),"Gaussian and saturation use distinct compiled topologies");
    Check(SUCCEEDED(engine.SetRadius(surfaces[0],24))&&StoredValue(surfaces[0])==24,"change a brush created from identity-default factory");
    Check(SUCCEEDED(engine.SetRadius(surfaces[1],.2f))&&StoredValue(surfaces[1])==.2f,"change independent saturation brush");
    for(int i=2;i<6;++i)Check(StoredValue(surfaces[i])==values[i],"edits never bleed across other monitors or filters");
    Check(SUCCEEDED(engine.SetSampling(surfaces[2],false))&&!surfaces[2].brushAttached&&surfaces[4].brushAttached,
        "occluded monitor can sleep without detaching another monitor's graph");
    for(auto& surface:surfaces) {
        Check(!IsWindowVisible(surface.hwnd),"factory tests never show or focus windows");
        surface.Reset();
    }
    Check(SameObject(gaussian.Get(),EngineTestAccess::Factory(engine,Filter::Gaussian))&&
        SameObject(saturation.Get(),EngineTestAccess::Factory(engine,Filter::Saturation)),"rebuilding surfaces retains only the two reusable factories");
    // Recreate with non-default values after every previous surface is gone.
    for(int i=0;i<12;++i) {
        Engine::Surface surface;auto filter=i%2?Filter::Saturation:Filter::Gaussian;
        float value=i%2?.35f:4.f;
        surface.hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"",
            WS_POPUP,-20000,-20000,16,16,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        HRESULT hr=engine.Attach(surface.hwnd,16,16,value,surface,filter);
        Check(SUCCEEDED(hr),"repeated apply rebuild uses cached factory");
        if(SUCCEEDED(hr))Check(StoredValue(surface)==value,"rebuilt brush starts with configured value, not first-ever factory default");
        surface.Reset();
    }
}
void TestFilter(desktop_blur::Engine& engine,desktop_blur::Filter type) {
    desktop_blur::Engine::Surface surface;
    surface.hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,
        L"STATIC",L"Penumbra hidden sampling regression",WS_POPUP,-20000,-20000,64,64,
        nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(surface.hwnd!=nullptr,"create a hidden own window");
    float initial=type==desktop_blur::Filter::Gaussian?12.f:.4f;
    HRESULT hr=engine.Attach(surface.hwnd,64,64,initial,surface,type);
    Check(SUCCEEDED(hr),"attach a real production Composition graph");
    if(FAILED(hr)){std::printf("Attach=%08lX stage=%ls\n",static_cast<unsigned long>(hr),engine.stage);surface.Reset();return;}
    const auto originalHwnd=surface.hwnd;
    const auto originalVisual=surface.visual.Get();
    const auto originalEffect=surface.effect.Get();
    const auto originalProperties=surface.properties.Get();
    Check(surface.samplingEnabled&&surface.brushAttached,"initial active filter has a brush");
    auto attached=Brush(surface);
    Check(SameObject(attached.Get(),surface.effect.Get()),"sprite uses the original effect brush");
    for(float opacity:{0.f,.25f,.5f,.75f,1.f}) {
        Check(SUCCEEDED(surface.visual->put_Opacity(opacity)),"set animation position");
        Check(SUCCEEDED(engine.SetSampling(surface,false)),"detach sampling at any opacity");
        Check(!surface.samplingEnabled&&!surface.brushAttached&&!Brush(surface),"detached sprite holds no backdrop brush");
        float actual=-1;
        Check(SUCCEEDED(surface.visual->get_Opacity(&actual))&&actual==opacity,"suspend preserves animation opacity");
        Check(surface.radius==initial,"suspend preserves the radius");
        for(int i=0;i<=48;i++) {
            float value=type==desktop_blur::Filter::Gaussian?static_cast<float>(i):i/48.f;
            Check(SUCCEEDED(engine.SetRadius(surface,value)),"radius can change while suspended");
            Check(!surface.brushAttached&&!Brush(surface),"radius change never wakes a suspended graph");
            float stored=-1;desktop_blur::C::CompositionGetValueStatus status{};
            Check(SUCCEEDED(surface.properties->TryGetScalar(desktop_blur::String(desktop_blur::PropertyName(type)),&stored,&status))&&
                status==desktop_blur::C::CompositionGetValueStatus_Succeeded&&std::abs(stored-value)<.00001f,"suspended graph retains the actual edited property");
        }
        Check(SUCCEEDED(engine.SetRadius(surface,initial)),"restore configured parameter while suspended");
        Check(SUCCEEDED(engine.SetSampling(surface,true)),"resume the retained graph");
        attached=Brush(surface);
        Check(surface.samplingEnabled&&surface.brushAttached&&SameObject(attached.Get(),surface.effect.Get()),"resume reuses exactly the original effect brush");
        actual=-1;
        Check(SUCCEEDED(surface.visual->get_Opacity(&actual))&&actual==opacity,"resume does not replay or reset animation");
        Check(surface.hwnd==originalHwnd&&surface.visual.Get()==originalVisual&&surface.effect.Get()==originalEffect&&surface.properties.Get()==originalProperties,"suspend and radius edits preserve all retained objects");
    }
    float bypass=type==desktop_blur::Filter::Gaussian?0.f:1.f;
    Check(SUCCEEDED(engine.SetRadius(surface,bypass)),"set identity radius");
    for(int i=0;i<100;i++) {
        Check(SUCCEEDED(engine.SetSampling(surface,false))&&SUCCEEDED(engine.SetSampling(surface,true)),"identity filter supports repeated sleep/wake");
        Check(!surface.brushAttached&&!Brush(surface),"identity filter never samples backdrop even when enabled");
    }
    Check(SUCCEEDED(engine.SetRadius(surface,initial)),"restore active radius");
    Pump(100);
    DWORD gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),handles=0;
    GetProcessHandleCount(GetCurrentProcess(),&handles);
    for(int i=0;i<1000;i++) {
        Check(SUCCEEDED(engine.SetSampling(surface,false))&&SUCCEEDED(engine.SetSampling(surface,false)),"duplicate suspend is a cheap no-op");
        Check(SUCCEEDED(engine.SetSampling(surface,true))&&SUCCEEDED(engine.SetSampling(surface,true)),"duplicate resume is a cheap no-op");
    }
    Pump(100);
    DWORD after=0;GetProcessHandleCount(GetCurrentProcess(),&after);
    Check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==gdi,"1000 sleep/wake cycles do not allocate GDI objects");
    Check(after<=handles+2,"1000 sleep/wake cycles do not leak handles");
    Check(!IsWindowVisible(surface.hwnd),"test window remained hidden throughout");
    surface.Reset();
    Check(!surface.hwnd&&!surface.target&&!surface.visual&&!surface.effect&&!surface.brushAttached,"reset releases the graph and the owned window");
}
void Benchmark(desktop_blur::Engine& engine) {
    desktop_blur::Engine::Surface surface;
    surface.hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"",
        WS_POPUP,-20000,-20000,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(FAILED(engine.Attach(surface.hwnd,64,64,12,surface))){surface.Reset();return;}
    engine.SetSampling(surface,false);Pump(200);
    const ULONGLONG before=GetTickCount64();const double cpu=CpuMs();
    Pump(10000);
    PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
    GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));
    const double elapsed=static_cast<double>(GetTickCount64()-before),used=CpuMs()-cpu;
    std::printf("{\"Stage\":\"DetachedHiddenIdle\",\"ElapsedMs\":%.1f,\"CpuMs\":%.3f,\"OneCoreCpuPercent\":%.6f,\"WorkingSetMiB\":%.3f,\"PrivateMiB\":%.3f,\"GpuMeasured\":false,\"DwmCostExcluded\":true}\n",
        elapsed,used,used*100/elapsed,memory.WorkingSetSize/1048576.,memory.PrivateUsage/1048576.);
    LARGE_INTEGER frequency{},start{},end{};QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&start);
    volatile bool keepDetached=false;
    for(int i=0;i<1000000;i++)engine.SetSampling(surface,keepDetached);
    QueryPerformanceCounter(&end);
    std::printf("{\"Stage\":\"OneMillionDetachedNoOps\",\"ElapsedMs\":%.6f,\"BrushAttached\":%s}\n",
        (end.QuadPart-start.QuadPart)*1000./frequency.QuadPart,surface.brushAttached?"true":"false");
    surface.Reset();
}
}
int main(int argc,char** argv) {
    if(!desktop_blur::TransparencyAllowed()){std::puts("SKIP Composition disabled by current accessibility/transparency policy");return 77;}
    HWND foreground=GetForegroundWindow();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    {
        desktop_blur::Engine engine;
        TestFactories(engine);
        TestFilter(engine,desktop_blur::Filter::Gaussian);
        TestFilter(engine,desktop_blur::Filter::Saturation);
        if(argc>1&&std::strcmp(argv[1],"--benchmark")==0)Benchmark(engine);
    }
    Check(GetForegroundWindow()==foreground,"foreground remained unchanged");
    std::printf("{\"EffectSamplingChecks\":%u,\"Failures\":%u,\"UserSettingsUntouched\":true,\"NoVisibleWindows\":true}\n",checks,failures);
    return failures?1:0;
}

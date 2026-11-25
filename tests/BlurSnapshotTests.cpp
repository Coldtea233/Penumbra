#define wWinMain RendererMain
#include "../src/Nocturne.Renderer/main.cpp"
#undef wWinMain

unsigned __stdcall RunFixtureCapture(void* context) {
    std::unique_ptr<std::shared_ptr<blur_snapshot::Snapshot>> request(
        static_cast<std::shared_ptr<blur_snapshot::Snapshot>*>(context));
    if(SUCCEEDED(RoInitialize(RO_INIT_MULTITHREADED))) {
        blur_snapshot::RunCapture(**request,false);
        request.reset();
        RoUninitialize();
    }
    return 0;
}

int BenchmarkFilter() {
    using desktop_blur::ComPtr;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> gpu;
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<ID2D1Factory1> factory;
    ComPtr<ID2D1Device> drawingDevice;
    ComPtr<ID2D1DeviceContext> draw;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&gpu))||FAILED(device.As(&dxgi))||
       FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED,factory.GetAddressOf()))||
       FAILED(factory->CreateDevice(dxgi.Get(),&drawingDevice))||
       FAILED(drawingDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,&draw))) return 77;
    constexpr UINT width=2880,height=1620;
    const RECT area{0,0,width,height};
    std::vector<uint32_t> pattern(size_t(width)*height);
    for(UINT y=0;y<height;++y) for(UINT x=0;x<width;++x)
        pattern[size_t(y)*width+x]=((x/17+y/23)%2)?0xff506db0:0xffd2ba79;
    const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);
    ComPtr<ID2D1Bitmap1> input;
    if(FAILED(draw->CreateBitmap({width,height},pattern.data(),width*4,&props,&input))) return 10;

    auto compare=[&](float radius,UINT divisor) -> double {
        blur_snapshot::MonitorFrame frame{area,radius};
        for(int i=0;i<5;++i) {
            const HRESULT hr=blur_snapshot::BlurReduced(draw.Get(),input.Get(),area,frame,divisor);
            if(FAILED(hr)) { std::printf("Filter warm-up failed (0x%08lx)\n",hr); return -1; }
        }
        ComPtr<ID3D11Query> disjoint,start,end;
        const D3D11_QUERY_DESC timing{D3D11_QUERY_TIMESTAMP_DISJOINT,0},stamp{D3D11_QUERY_TIMESTAMP,0};
        if(FAILED(device->CreateQuery(&timing,&disjoint))||FAILED(device->CreateQuery(&stamp,&start))||
           FAILED(device->CreateQuery(&stamp,&end))) return -1;
        constexpr int count=35;
        gpu->Begin(disjoint.Get());
        gpu->End(start.Get());
        for(int i=0;i<count;++i)
            if(FAILED(blur_snapshot::BlurReduced(draw.Get(),input.Get(),area,frame,divisor))) return -1;
        gpu->End(end.Get());
        gpu->End(disjoint.Get());
        gpu->Flush();
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT info{};
        const auto deadline=GetTickCount64()+4000;
        HRESULT result;
        while((result=gpu->GetData(disjoint.Get(),&info,sizeof(info),D3D11_ASYNC_GETDATA_DONOTFLUSH))==S_FALSE) {
            if(GetTickCount64()>=deadline) return -1;
            Sleep(4);
        }
        UINT64 begin=0,finish=0;
        auto readTimestamp=[&](ID3D11Query* query,UINT64& value) {
            HRESULT hr;
            while((hr=gpu->GetData(query,&value,sizeof(value),0))==S_FALSE&&GetTickCount64()<deadline) Sleep(2);
            return hr;
        };
        const auto startResult=readTimestamp(start.Get(),begin);
        const auto endResult=readTimestamp(end.Get(),finish);
        if(result!=S_OK||info.Disjoint||!info.Frequency||startResult!=S_OK||endResult!=S_OK||finish<=begin) {
            std::printf("Timing unavailable: query=0x%08lx disjoint=%d frequency=%llu start=0x%08lx end=0x%08lx ticks=%llu/%llu\n",
                result,info.Disjoint,info.Frequency,startResult,endResult,begin,finish);
            return -1;
        }
        return 1000.0*(finish-begin)/info.Frequency/count;
    };
    for(float radius:{12.f,24.f,48.f}) {
        const UINT divisor=blur_snapshot::DownsampleFactor(radius);
        constexpr size_t rounds=3;
        double native[rounds]{},reduced[rounds]{};
        for(size_t i=0;i<rounds;++i) {
            if(i%2==0) { native[i]=compare(radius,1); reduced[i]=compare(radius,divisor); }
            else { reduced[i]=compare(radius,divisor); native[i]=compare(radius,1); }
            if(native[i]<0||reduced[i]<0) return 11;
        }
        std::sort(std::begin(native),std::end(native));
        std::sort(std::begin(reduced),std::end(reduced));
        std::printf("GPU FILTER radius=%.0f, %ux%u, full=%.3f ms/frame, 1/%u=%.3f ms/frame\n",
            radius,width,height,native[1],divisor,reduced[1]);
    }
    return 0;
}

// A moving source driven by wall time, not by frame consumption. Both windows
// stay at the bottom and never activate or change the user's desktop settings.
int ContinuousFixture(int hz,bool color) {
    using namespace desktop_blur;
    if(FAILED(RoInitialize(RO_INIT_SINGLETHREADED))) return 77;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const int width=1920,height=1080;
    auto window=[&](DWORD extra) {
        return CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|extra,L"STATIC",L"",
            WS_POPUP,0,0,width,height,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    };
    HWND input=window(0);
    if(!input||!SetWindowPos(input,HWND_BOTTOM,0,0,width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW)) return 2;
    HDC dc=GetDC(input);
    HBRUSH bg=CreateSolidBrush(RGB(61,64,70)),fg=CreateSolidBrush(RGB(83,86,94));
    RECT bounds{0,0,width,height};
    FillRect(dc,&bounds,bg);
    GdiFlush();
    auto captured=std::make_shared<blur_snapshot::Snapshot>();
    auto rate=blur_snapshot::ResolveRenderRate(hz,2);
    Engine engine;
    Engine::Surface blur,sat;
    ComPtr<C::ICompositionGraphicsDevice> graphics;
    ComPtr<C::ICompositionDrawingSurface> drawing;
    HANDLE worker=nullptr;
    bool attached=false;
    if(hz>=0) {
        captured->host=input;
        GetWindowRect(input,&bounds);
        captured->monitors.push_back({bounds,12.f});
        if(hz>0&&!captured->EnableStreaming(rate)) return 3;
        blur.hwnd=window(WS_EX_NOREDIRECTIONBITMAP);
        if(FAILED(engine.Attach(blur.hwnd,width,height,12,blur))||FAILED(engine.FinishOpacityAnimation(blur,1))) return 4;
        if(color) {
            sat.hwnd=window(WS_EX_NOREDIRECTIONBITMAP);
            if(FAILED(engine.Attach(sat.hwnd,width,height,.5f,sat,Filter::Saturation))||
               FAILED(engine.FinishOpacityAnimation(sat,1))) return 4;
        }
        auto* request=new std::shared_ptr<blur_snapshot::Snapshot>(captured);
        worker=reinterpret_cast<HANDLE>(_beginthreadex(nullptr,0,RunFixtureCapture,request,0,nullptr));
        if(!worker) return 5;
    }
    HANDLE tick=CreateWaitableTimerExW(nullptr,nullptr,2,TIMER_ALL_ACCESS);
    double start=ClockMs(),next=start;
    int painted=0,filtered=0,steady=0,previous=0;
    while(ClockMs()-start<9000) {
        double now=ClockMs();
        if(now>=next) {
            int slot=static_cast<int>((now-start)*120/1000);
            next=start+(slot+1)*1000./120;
            int x=(slot*5)%(width-96);
            RECT old{previous,80,previous+96,height-80},rect{x,80,x+96,height-80};
            FillRect(dc,&old,bg); FillRect(dc,&rect,fg); GdiFlush();
            previous=x; ++painted;
        }
        if(worker&&WaitForSingleObject(hz>0?captured->ready:worker,0)==WAIT_OBJECT_0) {
            auto& frame=captured->monitors.front();
            if(!frame.ready||!frame.bitmap) return 6;
            if(!attached) {
                ComPtr<C::ICompositionBrush> brush;
                if(FAILED(engine.SnapshotDevice(captured->drawingDevice.Get(),graphics))||
                    FAILED(engine.SnapshotBrush(graphics.Get(),frame.bitmap.Get(),brush,&drawing))||
                    (color&&FAILED(engine.SetColorInput(sat,brush)))||
                    FAILED(engine.SetFrozenBrush(blur,std::move(brush)))) return 6;
                // From bottom up: input, blur, optional saturation.
                SetWindowPos(blur.hwnd,HWND_BOTTOM,0,0,width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW);
                SetWindowPos(input,blur.hwnd,0,0,0,0,SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOSIZE);
                if(color) {
                    SetWindowPos(sat.hwnd,HWND_BOTTOM,0,0,width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW);
                    SetWindowPos(blur.hwnd,sat.hwnd,0,0,0,0,SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOSIZE);
                    SetWindowPos(input,blur.hwnd,0,0,0,0,SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOSIZE);
                }
                attached=true;
            } else if(FAILED(engine.UpdateSnapshot(drawing.Get(),frame.bitmap.Get()))) return 7;
            filtered++;
            if(now-start>=1000) steady++;
            if(hz>0) SetEvent(captured->consumed);
            else { CloseHandle(worker); worker=nullptr; }
        }
        MSG msg;
        while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        LARGE_INTEGER due{};
        due.QuadPart=-static_cast<LONGLONG>(std::max(1.,(next-ClockMs())*10000.));
        if(tick&&SetWaitableTimer(tick,&due,0,nullptr,nullptr,FALSE)) WaitForSingleObject(tick,12);
        else Sleep(1);
    }
    if(worker) { captured->Cancel(); WaitForSingleObject(worker,3000); CloseHandle(worker); }
    if(tick) CloseHandle(tick);
    sat.Reset(); blur.Reset();
    ReleaseDC(input,dc); DeleteObject(fg); DeleteObject(bg); DestroyWindow(input);
    std::printf("1920x1080 rate=%d color=%d inputPaints=%d filtered=%d steadyFramesPerSecond=%.2f\n",
        hz,color,painted,filtered,steady/8.);
    return hz<0||(!hz&&filtered==1)||(hz>0&&steady>0&&steady<=hz*8+3)?0:8;
}

int main(int argc,char** argv) {
    if(argc>2&&std::strcmp(argv[1],"--continuous")==0) return ContinuousFixture(atoi(argv[2]),argc>3&&atoi(argv[3]));
    if(argc>1&&std::strcmp(argv[1],"--gpu-filter")==0) return BenchmarkFilter();
    using blur_snapshot::RenderRate;
    using blur_snapshot::CaptureInterval;
    static_assert(CaptureInterval(RenderRate::VSync)==0&&CaptureInterval(RenderRate::Still)==0);
    static_assert(CaptureInterval(RenderRate::Hz15)==666666&&CaptureInterval(RenderRate::Hz30)==333333&&
        CaptureInterval(RenderRate::Hz60)==166666&&CaptureInterval(RenderRate::Hz90)==111111&&
        CaptureInterval(RenderRate::Hz120)==83333);
    static_assert(blur_snapshot::ResolveRenderRate(-2,-1)==RenderRate::Hz30&&
        blur_snapshot::ResolveRenderRate(-2,0)==RenderRate::VSync&&blur_snapshot::ResolveRenderRate(-2,1)==RenderRate::Hz15&&
        blur_snapshot::ResolveRenderRate(-2,2)==RenderRate::Still&&blur_snapshot::ResolveRenderRate(90,2)==RenderRate::Hz90);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const bool stillFixture=argc>1&&std::strcmp(argv[1],"--still-fixture")==0;
    const bool streaming=argc>1&&!stillFixture;
    const bool useFixture=stillFixture||(streaming&&std::strcmp(argv[1],"--stream-fixture")==0);
    if(!useFixture&&!LocateDesktop()) { std::puts("SKIP: no Explorer desktop host"); return 77; }
    const auto rate=argc>2?blur_snapshot::ResolveRenderRate(atoi(argv[2]),0):RenderRate::Hz15;
    struct TestWindow {
        HWND hwnd=nullptr;
        ~TestWindow() { if(hwnd) DestroyWindow(hwnd); }
    } fixture;
    auto paint=[&](COLORREF color) {
        const HDC dc=GetDC(fixture.hwnd);
        RECT rect{};
        GetClientRect(fixture.hwnd,&rect);
        const HBRUSH brush=CreateSolidBrush(color);
        FillRect(dc,&rect,brush);
        DeleteObject(brush);
        ReleaseDC(fixture.hwnd,dc);
        GdiFlush();
    };
    blur_snapshot::Snapshot snapshot;
    snapshot.host=desktop;
    RECT primary{0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)};
    if(useFixture) {
        WNDCLASSW sourceClass{};
        sourceClass.lpfnWndProc=DefWindowProcW;
        sourceClass.hInstance=GetModuleHandleW(nullptr);
        sourceClass.lpszClassName=L"Penumbra.BlurStreamFixture";
        if(!RegisterClassW(&sourceClass)) return 9;
        fixture.hwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,sourceClass.lpszClassName,L"",WS_POPUP,
            32,32,320,192,nullptr,nullptr,sourceClass.hInstance,nullptr);
        if(!fixture.hwnd||!GetWindowRect(fixture.hwnd,&primary)) return 9;
        ShowWindow(fixture.hwnd,SW_SHOWNOACTIVATE);
        paint(RGB(200,64,48));
        snapshot.host=fixture.hwnd;
    }
    snapshot.monitors.push_back({primary,12.f,{}});
    RECT hostBounds{};
    GetWindowRect(snapshot.host,&hostBounds);
    std::printf("Host: %ld,%ld to %ld,%ld. Primary: %ldx%ld. Shared wallpaper host: %d.\n",
        hostBounds.left,hostBounds.top,hostBounds.right,hostBounds.bottom,primary.right-primary.left,primary.bottom-primary.top,
        blur_snapshot::WallpaperSharesHost(snapshot.host));
    if(FAILED(RoInitialize(RO_INIT_SINGLETHREADED))) return 1;
    auto capture=std::make_shared<blur_snapshot::Snapshot>();
    capture->host=snapshot.host;
    capture->monitors=std::move(snapshot.monitors);
    if(streaming&&!capture->EnableStreaming(rate)) return 8;
    auto* request=new std::shared_ptr<blur_snapshot::Snapshot>(capture);
    const auto thread=reinterpret_cast<HANDLE>(_beginthreadex(nullptr,0,useFixture?RunFixtureCapture:RunBlurSnapshot,request,0,nullptr));
    if(!thread) { delete request; return 1; }
    struct Cleanup {
        HANDLE thread;
        blur_snapshot::Snapshot& capture;
        ~Cleanup() { capture.Cancel(); WaitForSingleObject(thread,2000); CloseHandle(thread); }
    } cleanup{thread,*capture};
    HANDLE captureEvents[]{thread,capture->ready};
    const DWORD wait=WaitForMultipleObjects(streaming?2:1,captureEvents,FALSE,5000);
    if(wait==WAIT_TIMEOUT||wait==WAIT_FAILED) return 2;
    if(streaming&&wait==WAIT_OBJECT_0) {
        std::printf("Stream ended before first frame: %s (0x%08lx)\n",capture->stage,capture->captureResult);
        return 8;
    }
    auto& bitmap=capture->monitors.front().bitmap;
    if(!bitmap) { std::printf("SKIP: no eligible desktop frame (%s, 0x%08lx)\n",capture->stage,capture->captureResult); return 77; }
    blurEngine=std::make_unique<desktop_blur::Engine>();
    auto& engine=*blurEngine;
    const HRESULT init=engine.Initialize();
    if(FAILED(init)) { std::printf("Composition startup: 0x%08lx\n",init); return 3; }
    desktop_blur::ComPtr<desktop_blur::C::ICompositionGraphicsDevice> graphics;
    const HRESULT device=engine.SnapshotDevice(capture->drawingDevice.Get(),graphics);
    if(FAILED(device)) { std::printf("Composition graphics device: 0x%08lx\n",device); return 4; }
    desktop_blur::ComPtr<desktop_blur::C::ICompositionBrush> brush;
    desktop_blur::ComPtr<desktop_blur::C::ICompositionDrawingSurface> drawing;
    const HRESULT surface=engine.SnapshotBrush(graphics.Get(),bitmap.Get(),brush,&drawing);
    if(FAILED(surface)||!brush) { std::printf("Snapshot brush: 0x%08lx\n",surface); return 5; }
    wchar_t className[]=L"Penumbra.BlurSnapshotTest";
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc=DefWindowProcW;
    windowClass.hInstance=GetModuleHandleW(nullptr);
    windowClass.lpszClassName=className;
    if(!RegisterClassW(&windowClass)) return 6;
    desktop_blur::Engine::Surface effect;
    effect.hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,className,L"",WS_POPUP,
        0,0,primary.right,primary.bottom,nullptr,nullptr,windowClass.hInstance,nullptr);
    auto verify=[&](bool result,const char* name) {
        std::printf("%s: %s\n",result?"PASS":"FAIL",name);
        return result;
    };
    bool ok=verify(effect.hwnd&&SUCCEEDED(engine.Attach(effect.hwnd,primary.right,primary.bottom,12.f,effect))&&effect.brushAttached,
        "live compositor blur initially attached");
    ok=verify(SUCCEEDED(engine.SetFrozenBrush(effect,std::move(brush)))&&effect.frozenAttached&&!effect.brushAttached,
        "retained desktop frame replaces the live backdrop graph")&&ok;
    if(streaming) {
        const auto lastFrame=GetTickCount64();
        if(useFixture) paint(RGB(48,64,200));
        SetEvent(capture->consumed);
        const auto next=WaitForMultipleObjects(2,captureEvents,FALSE,3000);
        if(next==WAIT_OBJECT_0+1) {
            const auto elapsed=GetTickCount64()-lastFrame;
            std::printf("Stream %d Hz: update latency %llu ms, texture %ux%u\n",static_cast<int>(rate),elapsed,
                bitmap->GetPixelSize().width,bitmap->GetPixelSize().height);
            ok=verify(SUCCEEDED(engine.UpdateSnapshot(drawing.Get(),bitmap.Get()))&&effect.frozenAttached,
                "rate-limited frame replaces texture contents without reattaching the live graph")&&ok;
            SetEvent(capture->consumed);
        } else if(next==WAIT_TIMEOUT) {
            std::puts("No second desktop frame arrived during the read-only test");
            ok=ok&&!useFixture;
        } else {
            std::printf("Streaming stopped: %s (0x%08lx)\n",capture->stage,capture->captureResult);
            ok=false;
        }
        if(useFixture&&ok) {
            constexpr int updates=15;
            const double started=ClockMs();
            int count=0;
            for(;count<updates;++count) {
                paint(count%2?RGB(48,64,200):RGB(200,64,48));
                if(WaitForMultipleObjects(2,captureEvents,FALSE,1000)!=WAIT_OBJECT_0+1) break;
                if(FAILED(engine.UpdateSnapshot(drawing.Get(),bitmap.Get()))) break;
                SetEvent(capture->consumed);
            }
            const double elapsedMs=ClockMs()-started;
            std::printf("Stream %d Hz: %d changing frames in %.1f ms\n",static_cast<int>(rate),count,elapsedMs);
            const double minimumMs=1000.0*(updates-2)/static_cast<int>(rate);
            ok=verify(count==updates&&elapsedMs>=minimumMs,"changing frames respect the selected rate limit")&&ok;
        }
        capture->Cancel();
        ok=verify(WaitForSingleObject(thread,2000)==WAIT_OBJECT_0,"cancel releases the capture session promptly")&&ok;
    }
    ok=verify(SUCCEEDED(engine.SetSampling(effect,false))&&!effect.frozenAttached&&!effect.brushAttached,
        "hiding disconnects both sources")&&ok;
    ok=verify(SUCCEEDED(engine.SetSampling(effect,true))&&effect.frozenAttached&&!effect.brushAttached,
        "showing the existing snapshot does not resume backdrop sampling")&&ok;
    ok=verify(SUCCEEDED(engine.SetFrozenBrush(effect,{}))&&!effect.frozen&&!effect.frozenAttached&&effect.brushAttached,
        "thaw releases the snapshot and restores the live brush")&&ok;
    ok=verify(SUCCEEDED(engine.SetRadius(effect,0))&&!effect.brushAttached,"zero radius disconnects the live brush")&&ok;
    effect.Reset();
    if(stillFixture) {
        // Keep status reporting away from the user's installed configuration.
        wchar_t testExecutable[MAX_PATH]{};
        if(!GetModuleFileNameW(nullptr,testExecutable,MAX_PATH)) return 6;
        configDir=testExecutable;
        appliedEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!appliedEvent) return 6;
        desktop=fixture.hwnd;
        params.blurEnabled=true;
        params.blurRenderRate=RenderRate::Still;
        blurTransition.Reset(true);
        blurPreviewClient=GetCurrentProcessId();
        blurPreviewSettling=true;
        ok=verify(!CanFreezeBlur(),"dragging temporarily suppresses capture")&&ok;
        blurPreviewSettling=false;
        ok=verify(CanFreezeBlur(),"holding the settled preview does not keep the live graph on")&&ok;
        const RECT missing{primary.left,primary.top,primary.right-32,primary.bottom-32};
        for(const auto& rect:{primary,missing}) {
            auto& s=blurSurfaces.emplace_back();
            s.rect=rect;
            s.protectionValid=true;
            s.protectedArea=blur_bounds::DesktopArea(rect);
            s.freezeAttempted=true;
            s.composition.hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,
                className,L"",WS_POPUP,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,
                desktop,nullptr,windowClass.hInstance,nullptr);
            if(!s.composition.hwnd||FAILED(blurEngine->Attach(s.composition.hwnd,rect.right-rect.left,
                rect.bottom-rect.top,12.f,s.composition))) return 6;
        }
        capture->monitors.push_back({missing,12.f,{}});
        blurSnapshot=capture;
        if(!DuplicateHandle(GetCurrentProcess(),thread,GetCurrentProcess(),&blurSnapshotThread,
            0,FALSE,DUPLICATE_SAME_ACCESS)) return 6;
        FinishBlurSnapshot();
        auto checkStill=[&] {
            return blurSurfaces.size()==2&&!blurSnapshotThread&&!blurSnapshot&&
                blurSurfaces[0].composition.frozenAttached&&!blurSurfaces[0].composition.brushAttached&&
                blurSurfaces[1].stillUnavailable&&!blurSurfaces[1].composition.brushAttached&&
                !IsWindowVisible(blurSurfaces[1].composition.hwnd);
        };
        ok=verify(checkStill(),"partial capture keeps only the successful static texture and stops live sampling")&&ok;
        ok=verify(WaitForSingleObject(appliedEvent,0)==WAIT_TIMEOUT,
            "asynchronous snapshot failure does not acknowledge an Apply operation")&&ok;
        capturingPreview=true;
        const bool hidden=PositionBlurSurfaces();
        capturingPreview=false;
        ok=verify(hidden&&PositionBlurSurfaces()&&checkStill(),
            "preview recovery keeps failed surfaces detached and resumes static textures")&&ok;
        DegradeBlurStream();
        ok=verify(streamFallback&&checkStill()&&!blurStreamGraphics,
            "capture failure preserves existing texture without reattaching live sampling")&&ok;
        ClearBlur();
        CloseHandle(appliedEvent);
        appliedEvent=nullptr;
    }
    blurEngine.reset();
    return ok?0:7;
}

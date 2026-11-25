#pragma once
#include "desktop_blur.h"
#include <d3d11.h>
#include <d3d11_4.h>
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.capture.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <vector>

// Newer than the MinGW capture headers. Query at runtime on older Windows.
struct CaptureCadence : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_MinUpdateInterval(ABI::Windows::Foundation::TimeSpan*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_MinUpdateInterval(ABI::Windows::Foundation::TimeSpan) = 0;
};
__CRT_UUID_DECL(CaptureCadence,0x67c0ea62,0x1f85,0x5061,0x92,0x5a,0x23,0x9b,0xe0,0xac,0x09,0xcb)

namespace blur_snapshot {
using desktop_blur::ComPtr;
namespace Capture=ABI::Windows::Graphics::Capture;
namespace Graphics=ABI::Windows::Graphics;
namespace DirectX=Graphics::DirectX;
namespace DXInterop=Windows::Graphics::DirectX::Direct3D11;
constexpr int64_t MaxPixels=20'000'000;
constexpr DWORD FrameTimeoutMs=350;
enum class RenderRate { Still=-1,VSync=0,Hz15=15,Hz30=30,Hz60=60,Hz90=90,Hz120=120 };
constexpr auto DefaultRenderRate=RenderRate::Hz30;
constexpr RenderRate ResolveRenderRate(double rate,double legacyMode) {
    if(rate==-1||rate==0||rate==15||rate==30||rate==60||rate==90||rate==120)
        return static_cast<RenderRate>(static_cast<int>(rate));
    if(legacyMode==0) return RenderRate::VSync;
    if(legacyMode==1) return RenderRate::Hz15;
    if(legacyMode==2) return RenderRate::Still;
    return DefaultRenderRate;
}
constexpr bool IsFixedRate(RenderRate rate) { return static_cast<int>(rate)>0; }
constexpr INT64 CaptureInterval(RenderRate rate) {
    return IsFixedRate(rate)?10000000LL/static_cast<int>(rate):0;
}

struct MonitorFrame {
    RECT screen{};
    float radius=0;
    ComPtr<ID2D1Bitmap1> bitmap;
    ComPtr<ID2D1Bitmap1> reduced;
    ComPtr<ID2D1Effect> effect;
    bool ready=false,validated=false;
};
struct Snapshot {
    HWND host{};
    ComPtr<ID2D1Device> drawingDevice;
    std::vector<MonitorFrame> monitors;
    std::atomic<bool> cancelled{false};
    bool streaming=false;
    INT64 captureInterval=0;
    HANDLE ready{},consumed{},stop{};
    const char* stage="source window";
    HRESULT captureResult=S_OK;
    bool EnableStreaming(RenderRate rate) {
        captureInterval=CaptureInterval(rate);
        if(!captureInterval) return false;
        ready=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        consumed=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        streaming=ready&&consumed&&stop;
        return streaming;
    }
    void Cancel() {
        cancelled.store(true);
        if(stop) SetEvent(stop);
    }
    ~Snapshot() {
        if(ready) CloseHandle(ready);
        if(consumed) CloseHandle(consumed);
        if(stop) CloseHandle(stop);
    }
};

using FrameArrivedHandler=desktop_blur::F::ITypedEventHandler<Capture::Direct3D11CaptureFramePool*,IInspectable*>;
class FrameSignal final : public FrameArrivedHandler {
    std::atomic<ULONG> refs{1};
public:
    const HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    ~FrameSignal() { if(event) CloseHandle(event); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** result) override {
        if(!result) return E_POINTER;
        *result=nullptr;
        if(iid!=__uuidof(IUnknown)&&iid!=__uuidof(IAgileObject)&&iid!=__uuidof(FrameArrivedHandler)) return E_NOINTERFACE;
        *result=static_cast<FrameArrivedHandler*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto count=--refs;if(!count) delete this;return count; }
    HRESULT STDMETHODCALLTYPE Invoke(Capture::IDirect3D11CaptureFramePool*,IInspectable*) override {
        SetEvent(event);
        return S_OK;
    }
};

inline bool ValidBounds(const RECT& rect) {
    const auto width=static_cast<int64_t>(rect.right)-rect.left;
    const auto height=static_cast<int64_t>(rect.bottom)-rect.top;
    return width>0&&height>0&&width<=16384&&height<=16384&&width*height<=MaxPixels;
}
inline bool Contains(const RECT& outer,const RECT& inner) {
    return outer.left<=inner.left&&outer.top<=inner.top&&outer.right>=inner.right&&outer.bottom>=inner.bottom;
}
inline bool ReadCaptureBounds(HWND host,Graphics::SizeInt32 size,RECT& bounds) {
    const auto matches=[&] {
        return ValidBounds(bounds)&&bounds.right-bounds.left==size.Width&&bounds.bottom-bounds.top==size.Height;
    };
    if(SUCCEEDED(DwmGetWindowAttribute(host,DWMWA_EXTENDED_FRAME_BOUNDS,&bounds,sizeof(bounds)))&&matches()) return true;
    return GetWindowRect(host,&bounds)&&matches();
}

// Some Explorer versions put a live wallpaper into an independent top-level
// WorkerW. A snapshot from the icon host alone would miss that wallpaper.
inline bool WallpaperSharesHost(HWND host) {
    struct Search { HWND host; bool separate; } state{host,false};
    EnumWindows([](HWND root,LPARAM data)->BOOL {
        auto& state=*reinterpret_cast<Search*>(data);
        if(root==state.host||!IsWindowVisible(root)) return TRUE;
        wchar_t name[32]{};
        GetClassNameW(root,name,32);
        if(wcscmp(name,L"Progman")!=0&&wcscmp(name,L"WorkerW")!=0) return TRUE;
        EnumChildWindows(root,[](HWND child,LPARAM data)->BOOL {
            wchar_t name[32]{};
            GetClassNameW(child,name,32);
            if(IsWindowVisible(child)&&wcsncmp(name,L"WPE",3)==0) {
                reinterpret_cast<Search*>(data)->separate=true;
                return FALSE;
            }
            return TRUE;
        },data);
        return !state.separate;
    },reinterpret_cast<LPARAM>(&state));
    return !state.separate;
}

template<typename Interface> struct CaptureHandle {
    ComPtr<Interface> value;
    ~CaptureHandle() {
        Close();
    }
    void Close() {
        ComPtr<desktop_blur::F::IClosable> close;
        if(value&&SUCCEEDED(value.As(&close))) close->Close();
        value.Reset();
    }
};

// Sample a few pixels with one tiny staging copy. A transparent/icon-only
// capture or an all-black capture leaves the existing live effect in place.
inline bool UsableFrame(ID3D11Device* device,ID3D11Texture2D* texture,const RECT& area) {
    constexpr UINT samples=9;
    D3D11_TEXTURE2D_DESC desc{samples,1,1,1,DXGI_FORMAT_B8G8R8A8_UNORM,{1,0},
        D3D11_USAGE_STAGING,0,D3D11_CPU_ACCESS_READ,0};
    ComPtr<ID3D11Texture2D> staging;
    if(FAILED(device->CreateTexture2D(&desc,nullptr,&staging))) return false;
    ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(&context);
    for(UINT i=0;i<samples;++i) {
        const UINT x=area.left+static_cast<UINT>(area.right-area.left)*(1+2*(i%3))/6;
        const UINT y=area.top+static_cast<UINT>(area.bottom-area.top)*(1+2*(i/3))/6;
        const D3D11_BOX box{x,y,0,x+1,y+1,1};
        context->CopySubresourceRegion(staging.Get(),0,i,0,0,texture,0,&box);
    }
    D3D11_MAPPED_SUBRESOURCE map{};
    if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map))) return false;
    bool opaque=true,nonblack=false;
    auto* pixels=static_cast<const uint32_t*>(map.pData);
    for(UINT i=0;i<samples;++i) {
        opaque&=(pixels[i]>>24)==255;
        nonblack|=(pixels[i]&0x00ffffff)!=0;
    }
    context->Unmap(staging.Get(),0);
    return opaque&&nonblack;
}

inline HRESULT BlurMonitor(ID2D1DeviceContext* context,ID2D1Bitmap1* source,const RECT& area,MonitorFrame& frame) {
    const auto width=static_cast<UINT>(area.right-area.left),height=static_cast<UINT>(area.bottom-area.top);
    // A heavily blurred image has little high-frequency detail. Retain a
    // half-resolution texture at larger radii to cut steady VRAM by 75%.
    const auto outputWidth=frame.radius>=8?(width+1)/2:width;
    const auto outputHeight=frame.radius>=8?(height+1)/2:height;
    const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);
    ComPtr<ID2D1Bitmap1> crop,output;
    HRESULT hr;
    if(FAILED(hr=context->CreateBitmap({width,height},nullptr,0,&props,&crop))||
       FAILED(hr=context->CreateBitmap({outputWidth,outputHeight},nullptr,0,&props,&output))) return hr;
    const auto from=D2D1::RectU(area.left,area.top,area.right,area.bottom);
    if(FAILED(hr=crop->CopyFromBitmap(nullptr,source,&from))) return hr;
    ComPtr<ID2D1Effect> blur;
    if(FAILED(hr=context->CreateEffect(CLSID_D2D1GaussianBlur,&blur))||
       FAILED(hr=blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION,frame.radius))||
       FAILED(hr=blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE,D2D1_BORDER_MODE_HARD))) return hr;
    blur->SetInput(0,crop.Get());
    context->SetTarget(output.Get());
    context->SetTransform(D2D1::Matrix3x2F::Scale(static_cast<float>(outputWidth)/width,static_cast<float>(outputHeight)/height));
    context->BeginDraw();
    context->DrawImage(blur.Get(),nullptr,nullptr,D2D1_INTERPOLATION_MODE_LINEAR,D2D1_COMPOSITE_MODE_SOURCE_COPY);
    hr=context->EndDraw();
    context->SetTarget(nullptr);
    context->SetTransform(D2D1::Matrix3x2F::Identity());
    if(SUCCEEDED(hr)) frame.bitmap=std::move(output);
    return hr;
}

inline UINT DownsampleFactor(float radius) {
    if(radius>=32) return 8;
    if(radius>=8) return 4;
    if(radius>=4) return 2;
    return 1;
}
inline HRESULT BlurReduced(ID2D1DeviceContext* context,ID2D1Bitmap1* source,const RECT& area,MonitorFrame& frame,UINT divisor) {
    // Keep subtle blurs at native resolution. At stronger radii the downsample
    // is masked by the Gaussian and saves half to three quarters per axis.
    const UINT width=static_cast<UINT>(area.right-area.left),height=static_cast<UINT>(area.bottom-area.top);
    const auto w=(width+divisor-1)/divisor,h=(height+divisor-1)/divisor;
    const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);
    HRESULT hr;
    if(!frame.effect) {
        if(FAILED(hr=context->CreateBitmap({w,h},nullptr,0,&props,&frame.reduced))||
           FAILED(hr=context->CreateBitmap({w,h},nullptr,0,&props,&frame.bitmap))||
           FAILED(hr=context->CreateEffect(CLSID_D2D1GaussianBlur,&frame.effect))) return hr;
        if(FAILED(hr=frame.effect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION,frame.radius/divisor))||
           FAILED(hr=frame.effect->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE,D2D1_BORDER_MODE_HARD))||
           FAILED(hr=frame.effect->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION,0u))) return hr; // Speed
    }
    const auto from=D2D1::RectF(area.left,area.top,area.right,area.bottom),to=D2D1::RectF(0,0,w,h);
    context->SetTarget(frame.reduced.Get());
    context->BeginDraw();
    context->DrawBitmap(source,&to,1.f,divisor<=2?D2D1_INTERPOLATION_MODE_LINEAR:
        D2D1_INTERPOLATION_MODE_MULTI_SAMPLE_LINEAR,&from,nullptr);
    hr=context->EndDraw();
    context->SetTarget(nullptr);
    if(FAILED(hr)) return hr;
    frame.effect->SetInput(0,frame.reduced.Get(),TRUE);
    context->SetTarget(frame.bitmap.Get());
    context->BeginDraw();
    context->DrawImage(frame.effect.Get(),nullptr,nullptr,D2D1_INTERPOLATION_MODE_LINEAR,D2D1_COMPOSITE_MODE_SOURCE_COPY);
    hr=context->EndDraw();
    context->SetTarget(nullptr);
    return hr;
}

inline bool ProcessFrame(Snapshot& snapshot,Capture::IDirect3D11CaptureFrame* frame,Graphics::SizeInt32 size,
    const RECT& bounds,ID3D11Device* device,IDXGIDevice* dxgi,ComPtr<ID2D1DeviceContext>& context) {
    Graphics::SizeInt32 actual{};
    RECT current{};
    snapshot.stage="frame bounds";
    if(FAILED(frame->get_ContentSize(&actual))||actual.Width!=size.Width||actual.Height!=size.Height||
       !ReadCaptureBounds(snapshot.host,actual,current)||!EqualRect(&bounds,&current)) return false;

    ComPtr<DirectX::Direct3D11::IDirect3DSurface> captureSurface;
    ComPtr<DXInterop::IDirect3DDxgiInterfaceAccess> access;
    ComPtr<ID3D11Texture2D> input;
    snapshot.stage="frame texture";
    if(FAILED(frame->get_Surface(&captureSurface))||FAILED(captureSurface.As(&access))||
       FAILED(access->GetInterface(IID_PPV_ARGS(&input)))) return false;
    D3D11_TEXTURE2D_DESC inputDesc{};
    input->GetDesc(&inputDesc);
    if(inputDesc.Width<static_cast<UINT>(size.Width)||inputDesc.Height<static_cast<UINT>(size.Height)||
       inputDesc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM) return false;

    ComPtr<IDXGISurface> inputSurface;
    ComPtr<ID2D1Bitmap1> source;
    const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);
    snapshot.stage="Direct2D capture surface";
    if(!context) {
        ComPtr<ID2D1Factory1> d2d;
        if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED,d2d.GetAddressOf()))||
           FAILED(d2d->CreateDevice(dxgi,&snapshot.drawingDevice))||
           FAILED(snapshot.drawingDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,&context))) return false;
    }
    if(FAILED(input.As(&inputSurface))||FAILED(context->CreateBitmapFromDxgiSurface(inputSurface.Get(),&props,&source))) return false;

    bool usable=false;
    for(auto& monitor:snapshot.monitors) {
        if(snapshot.cancelled.load()) return false;
        if(!ValidBounds(monitor.screen)||!Contains(bounds,monitor.screen)) continue;
        RECT area=monitor.screen;
        OffsetRect(&area,-bounds.left,-bounds.top);
        snapshot.stage="frame opacity and color";
        if(!monitor.validated) monitor.validated=UsableFrame(device,input.Get(),area);
        if(monitor.validated) {
            snapshot.stage="Gaussian effect";
            monitor.ready=SUCCEEDED(BlurReduced(context.Get(),source.Get(),area,monitor,DownsampleFactor(monitor.radius)));
            usable|=monitor.ready;
        }
    }
    return usable;
}

inline void RunCapture(Snapshot& snapshot,bool checkWallpaperHost=true) {
    if(snapshot.cancelled.load()||!IsWindowVisible(snapshot.host)||
       (checkWallpaperHost&&!WallpaperSharesHost(snapshot.host))) return;
    ComPtr<IGraphicsCaptureItemInterop> interop;
    ComPtr<Capture::IGraphicsCaptureItem> item;
    snapshot.stage="capture item";
    if(FAILED(snapshot.captureResult=desktop_blur::Activate(L"Windows.Graphics.Capture.GraphicsCaptureItem",IID_PPV_ARGS(&interop)))||
       FAILED(snapshot.captureResult=interop->CreateForWindow(snapshot.host,IID_PPV_ARGS(&item)))) return;
    Graphics::SizeInt32 size{};
    RECT bounds{};
    snapshot.stage="capture bounds";
    if(FAILED(item->get_Size(&size))||!ReadCaptureBounds(snapshot.host,size,bounds)) return;
    if(std::none_of(snapshot.monitors.begin(),snapshot.monitors.end(),[&](const auto& monitor) {
        return ValidBounds(monitor.screen)&&Contains(bounds,monitor.screen);
    })) return;

    ComPtr<ID3D11Device> device;
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<IInspectable> projected;
    ComPtr<DirectX::Direct3D11::IDirect3DDevice> captureDevice;
    // No software fallback for a GPU-saving feature.
    snapshot.stage="capture device";
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr,0,D3D11_SDK_VERSION,&device,nullptr,nullptr))||FAILED(device.As(&dxgi))||
       FAILED(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(),&projected))||FAILED(projected.As(&captureDevice))) return;
    ComPtr<ID3D11Multithread> multithread;
    if(FAILED(device.As(&multithread))) return;
    multithread->SetMultithreadProtected(TRUE);

    ComPtr<Capture::IDirect3D11CaptureFramePoolStatics2> factory;
    CaptureHandle<Capture::IDirect3D11CaptureFramePool> pool;
    CaptureHandle<Capture::IGraphicsCaptureSession> session;
    snapshot.stage="capture session";
    if(FAILED(desktop_blur::Activate(L"Windows.Graphics.Capture.Direct3D11CaptureFramePool",IID_PPV_ARGS(&factory)))||
       FAILED(factory->CreateFreeThreaded(captureDevice.Get(),DirectX::DirectXPixelFormat_B8G8R8A8UIntNormalized,1,size,&pool.value))||
       FAILED(pool.value->CreateCaptureSession(item.Get(),&session.value))) return;
    ComPtr<Capture::IGraphicsCaptureSession2> cursor;
    if(FAILED(session.value.As(&cursor))||FAILED(cursor->put_IsCursorCaptureEnabled(false))) return;
    ComPtr<Capture::IGraphicsCaptureSession3> border;
    if(SUCCEEDED(session.value.As(&border))) border->put_IsBorderRequired(false);
    ComPtr<FrameSignal> signal;
    EventRegistrationToken frameToken{};
    if(snapshot.streaming) {
        snapshot.stage="capture rate limit";
        ComPtr<CaptureCadence> cadence;
        if(FAILED(snapshot.captureResult=session.value.As(&cadence))||
           FAILED(snapshot.captureResult=cadence->put_MinUpdateInterval({snapshot.captureInterval}))) return;
        signal.Attach(new(std::nothrow) FrameSignal());
        if(!signal||!signal->event||FAILED(pool.value->add_FrameArrived(signal.Get(),&frameToken))) return;
    }
    snapshot.stage="start capture";
    if(snapshot.cancelled.load()||FAILED(session.value->StartCapture())) return;
    const ULONGLONG deadline=GetTickCount64()+FrameTimeoutMs;
    ComPtr<ID2D1DeviceContext> context;
    bool delivered=false;
    while(!snapshot.cancelled.load()) {
        if(snapshot.streaming) {
            // Capture already paces updates; a second timer here could skip
            // a refresh or wake to process the same content unnecessarily.
            HANDLE events[]{snapshot.stop,signal->event};
            if(WaitForMultipleObjects(2,events,FALSE,delivered?INFINITE:FrameTimeoutMs)!=WAIT_OBJECT_0+1) return;
        }
        CaptureHandle<Capture::IDirect3D11CaptureFrame> frame;
        snapshot.stage="wait for frame";
        do {
            if(snapshot.cancelled.load()||FAILED(pool.value->TryGetNextFrame(&frame.value))) return;
            if(frame.value||snapshot.streaming) break;
            Sleep(8);
        } while(GetTickCount64()<deadline);
        if(!frame.value) {
            if(!delivered&&GetTickCount64()>=deadline) return;
            continue;
        }
        const bool usable=ProcessFrame(snapshot,frame.value.Get(),size,bounds,device.Get(),dxgi.Get(),context);
        frame.Close();
        if(!usable||!snapshot.streaming) return;

        // A single in-flight frame bounds memory and avoids a backlog if
        // the desktop message loop is busy. Cancellation wakes either wait.
        delivered=true;
        SetEvent(snapshot.ready);
        HANDLE events[]{snapshot.stop,snapshot.consumed};
        if(WaitForMultipleObjects(2,events,FALSE,INFINITE)!=WAIT_OBJECT_0+1) return;
    }
}
}

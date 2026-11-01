#pragma once
// Native Windows Composition: no Win2D, managed helper, frame capture or CPU blur.
// The small ABI additions below match Microsoft's Windows SDK WinRT headers.
#include <roapi.h>
#include <winstring.h>
// WIDL aliases boolean and BYTE to the same C++ type; its generated reference
// specializations collide. This unused specialization is not needed here.
#ifdef __MINGW32__
#define ____FIReference_1_boolean_INTERFACE_DEFINED__
#endif
#include <dispatcherqueue.h>
#include <windows.ui.composition.h>
#include <windows.graphics.effects.h>
#include <wrl/client.h>
#include <atomic>
#include "blur_parameters.h"

namespace desktop_blur {
namespace C = ABI::Windows::UI::Composition;
namespace E = ABI::Windows::Graphics::Effects;
namespace F = ABI::Windows::Foundation;
using Microsoft::WRL::ComPtr;
struct Compositor3 : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateHostBackdropBrush(IInspectable**) = 0;
};
struct DesktopInterop : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CreateDesktopWindowTarget(HWND,BOOL,IInspectable**) = 0;
};
struct SourceFactory : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Create(HSTRING,IInspectable**) = 0;
};
struct EffectInterop : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetEffectId(GUID*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetNamedPropertyMapping(LPCWSTR,UINT*,UINT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyCount(UINT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProperty(UINT,F::IPropertyValue**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSource(UINT,E::IGraphicsEffectSource**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSourceCount(UINT*) = 0;
};
}
__CRT_UUID_DECL(desktop_blur::Compositor3,0xc9dd8ef0,0x6eb1,0x4e3c,0xa6,0x58,0x67,0x5d,0x9c,0x64,0xd4,0xab)
__CRT_UUID_DECL(desktop_blur::DesktopInterop,0x29e691fa,0x4567,0x4dca,0xb3,0x19,0xd0,0xf2,0x07,0xeb,0x68,0x07)
__CRT_UUID_DECL(desktop_blur::SourceFactory,0xb3d9f276,0xaba3,0x4724,0xac,0xf3,0xd0,0x39,0x74,0x64,0xdb,0x1c)
__CRT_UUID_DECL(desktop_blur::EffectInterop,0x2fc57384,0xa068,0x44d7,0xa3,0x31,0x30,0x98,0x2f,0xcf,0x71,0x77)
namespace desktop_blur {
inline bool TransparencyAllowed() {
    HIGHCONTRASTW contrast{sizeof(contrast)};
    if(SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0)&&(contrast.dwFlags&HCF_HIGHCONTRASTON)) return false;
    DWORD transparency=1,size=sizeof(transparency);
    return RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"EnableTransparency",RRF_RT_REG_DWORD,nullptr,&transparency,&size)!=ERROR_SUCCESS||transparency!=0;
}
class String {
    HSTRING value{};
public:
    explicit String(const wchar_t* text) { WindowsCreateString(text,(UINT32)wcslen(text),&value); }
    ~String() { WindowsDeleteString(value); }
    operator HSTRING() const { return value; }
};
inline HRESULT Activate(const wchar_t* name,REFIID iid,void** result) {
    return RoGetActivationFactory(String(name),iid,result);
}
class Gaussian final : public E::IGraphicsEffect, public E::IGraphicsEffectSource, public EffectInterop {
    std::atomic<ULONG> refs{1};
    ComPtr<E::IGraphicsEffectSource> source;
    ComPtr<F::IPropertyValueStatics> values;
    float deviation;
public:
    Gaussian(E::IGraphicsEffectSource* input,F::IPropertyValueStatics* factory,float radius)
        : source(input),values(factory),deviation(radius) { }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(!out) return E_POINTER;
        *out=nullptr;
        if(iid==__uuidof(IUnknown)||iid==__uuidof(IInspectable)||iid==__uuidof(E::IGraphicsEffect)) *out=static_cast<E::IGraphicsEffect*>(this);
        else if(iid==__uuidof(E::IGraphicsEffectSource)) *out=static_cast<E::IGraphicsEffectSource*>(this);
        else if(iid==__uuidof(EffectInterop)) *out=static_cast<EffectInterop*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG count=--refs; if(!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG* count,IID** ids) override { *count=0; *ids=nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* name) override { *name=nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* trust) override { *trust=BaseTrust; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Name(HSTRING* name) override { return WindowsCreateString(L"Blur",4,name); }
    HRESULT STDMETHODCALLTYPE put_Name(HSTRING) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetEffectId(GUID* id) override {
        static constexpr GUID gaussian{0x1feb6d69,0x2fe6,0x4ac9,{0x8c,0x58,0x1d,0x7f,0x93,0xe7,0xa6,0xa5}};
        *id=gaussian; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetNamedPropertyMapping(LPCWSTR name,UINT* index,UINT* mapping) override {
        if(wcscmp(name,L"BlurAmount")) return E_INVALIDARG;
        *index=0; *mapping=1; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyCount(UINT* count) override { *count=3; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetProperty(UINT index,F::IPropertyValue** out) override {
        ComPtr<IInspectable> value;
        HRESULT hr=index==0?values->CreateSingle(deviation,&value):
            index<3?values->CreateUInt32(index==1?0:1,&value):E_INVALIDARG;
        return FAILED(hr)?hr:value.CopyTo(out);
    }
    HRESULT STDMETHODCALLTYPE GetSource(UINT index,E::IGraphicsEffectSource** out) override {
        if(index!=0) return E_INVALIDARG;
        return source.CopyTo(out);
    }
    HRESULT STDMETHODCALLTYPE GetSourceCount(UINT* count) override { *count=1; return S_OK; }
};

class Engine {
    bool initialized=false;
    HRESULT initializationResult=E_PENDING;
    ComPtr<ABI::Windows::System::IDispatcherQueueController> queue;
    ComPtr<C::ICompositor> compositor;
    ComPtr<DesktopInterop> interop;
    ComPtr<C::ICompositionBrush> backdrop;
    ComPtr<F::IPropertyValueStatics> values;
    ComPtr<SourceFactory> sourceFactory;
public:
    const wchar_t* stage=L"initialization";
    Engine()=default;
    Engine(const Engine&)=delete;
    Engine& operator=(const Engine&)=delete;
    ~Engine() {
        sourceFactory.Reset(); values.Reset(); backdrop.Reset(); interop.Reset();
        if(compositor) {
            ComPtr<F::IClosable> closable;
            if(SUCCEEDED(compositor.As(&closable))) closable->Close();
            compositor.Reset();
        }
        if(queue) {
            ComPtr<F::IAsyncAction> shutdown;
            queue->ShutdownQueueAsync(&shutdown);
            queue.Reset();
        }
        if(initialized) RoUninitialize();
    }
    HRESULT Initialize() {
        if(initializationResult!=E_PENDING) return initializationResult;
        initializationResult=InitializeCore();
        return initializationResult;
    }
private:
    HRESULT InitializeCore() {
        HRESULT hr=RoInitialize(RO_INIT_SINGLETHREADED);
        if(FAILED(hr)) return hr;
        initialized=true;
        DispatcherQueueOptions options{sizeof(options),DQTYPE_THREAD_CURRENT,DQTAT_COM_STA};
        stage=L"dispatcher";
        hr=CreateDispatcherQueueController(options,&queue);
        if(FAILED(hr)) return hr;
        ComPtr<IInspectable> object;
        stage=L"compositor";
        hr=RoActivateInstance(String(L"Windows.UI.Composition.Compositor"),&object);
        if(FAILED(hr)||FAILED(hr=object.As(&compositor))||FAILED(hr=compositor.As(&interop))) return hr;
        ComPtr<Compositor3> host;
        stage=L"host backdrop interface";
        if(FAILED(hr=compositor.As(&host))) return hr;
        object.Reset();
        stage=L"host backdrop";
        if(FAILED(hr=host->CreateHostBackdropBrush(&object))||FAILED(hr=object.As(&backdrop))) return hr;
        stage=L"property values";
        if(FAILED(hr=Activate(L"Windows.Foundation.PropertyValue",__uuidof(F::IPropertyValueStatics),reinterpret_cast<void**>(values.GetAddressOf())))) return hr;
        stage=L"source factory";
        return Activate(L"Windows.UI.Composition.CompositionEffectSourceParameter",__uuidof(SourceFactory),reinterpret_cast<void**>(sourceFactory.GetAddressOf()));
    }
public:
    struct Surface {
        HWND hwnd{};
        ComPtr<C::ICompositionTarget> target;
        ComPtr<C::IVisual> visual;
        ComPtr<C::ICompositionEffectBrush> effect;
        void Reset() {
            if(target) target->put_Root(nullptr);
            effect.Reset(); visual.Reset(); target.Reset();
            if(IsWindow(hwnd)) DestroyWindow(hwnd);
            hwnd=nullptr;
        }
    };
    HRESULT Attach(HWND hwnd,int width,int height,float radius,Surface& surface) {
        // Respect accessibility/transparency preferences. A host backdrop can
        // otherwise become an opaque fallback instead of a useful desktop blur.
        if(!TransparencyAllowed()) return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        BOOL yes=TRUE;
        stage=L"enable host backdrop";
        HRESULT hr=DwmSetWindowAttribute(hwnd,static_cast<DWMWINDOWATTRIBUTE>(17),&yes,sizeof(yes)); // DWMWA_USE_HOSTBACKDROPBRUSH (Windows 11)
        if(FAILED(hr)) return hr;
        if(FAILED(hr=Initialize())) return hr;
        ComPtr<IInspectable> object;
        stage=L"desktop target";
        if(FAILED(hr=interop->CreateDesktopWindowTarget(hwnd,FALSE,&object))||FAILED(hr=object.As(&surface.target))) return hr;
        ComPtr<C::ISpriteVisual> sprite;
        stage=L"sprite visual";
        if(FAILED(hr=compositor->CreateSpriteVisual(&sprite))||FAILED(hr=sprite.As(&surface.visual))) return hr;
        if(FAILED(hr=surface.visual->put_Size({static_cast<float>(width),static_cast<float>(height)}))||FAILED(hr=surface.visual->put_Opacity(0))) return hr;
        object.Reset();
        stage=L"source parameter";
        if(FAILED(hr=sourceFactory->Create(String(L"Backdrop"),&object))) return hr;
        ComPtr<E::IGraphicsEffectSource> input;
        if(FAILED(hr=object.As(&input))) return hr;
        ComPtr<E::IGraphicsEffect> effect;
        effect.Attach(new Gaussian(input.Get(),values.Get(),static_cast<float>(blur_parameters::AdditionalSigma(radius))));
        ComPtr<C::ICompositionEffectFactory> factory;
        stage=L"effect factory";
        if(FAILED(hr=compositor->CreateEffectFactory(effect.Get(),&factory))||FAILED(hr=factory->CreateBrush(&surface.effect))) return hr;
        stage=L"effect source";
        if(FAILED(hr=surface.effect->SetSourceParameter(String(L"Backdrop"),backdrop.Get()))) return hr;
        ComPtr<C::ICompositionBrush> brush;
        if(FAILED(hr=surface.effect.As(&brush))||FAILED(hr=sprite->put_Brush(brush.Get()))) return hr;
        stage=L"target root";
        return surface.target->put_Root(surface.visual.Get());
    }

};
}

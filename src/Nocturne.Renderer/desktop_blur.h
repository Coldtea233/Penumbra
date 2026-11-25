#pragma once
// Native Windows Composition for the live effect and retained snapshot brushes.
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
#include <windows.ui.composition.interop.h>
#include <d2d1_1.h>
#include <windows.graphics.effects.h>
#include <wrl/client.h>
#include <atomic>
#include <cmath>
#include <utility>
#include "blur_parameters.h"

namespace desktop_blur {
namespace C = ABI::Windows::UI::Composition;
namespace E = ABI::Windows::Graphics::Effects;
namespace F = ABI::Windows::Foundation;
using Microsoft::WRL::ComPtr;
struct Compositor2 : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateAmbientLight(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateAnimationGroup(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateBackdropBrush(IInspectable**) = 0;
};
// ABI of IKeyFrameAnimation through InsertExpressionKeyFrame. The MinGW
// header exposes IScalarKeyFrameAnimation but omits this base-class interface.
struct KeyFrameAnimation : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_DelayTime(F::TimeSpan*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_DelayTime(F::TimeSpan) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Duration(F::TimeSpan*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Duration(F::TimeSpan) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IterationBehavior(INT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IterationBehavior(INT32) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IterationCount(INT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IterationCount(INT32) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_KeyFrameCount(INT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_StopBehavior(INT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_StopBehavior(INT32) = 0;
    virtual HRESULT STDMETHODCALLTYPE InsertExpressionKeyFrame(FLOAT,HSTRING) = 0;
};
struct DesktopInterop : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CreateDesktopWindowTarget(HWND,BOOL,IInspectable**) = 0;
};
// The MinGW interop header omits the drawing-surface ABI.
struct DrawingSurfaceInterop : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE BeginDraw(const RECT*,REFIID,void**,POINT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE EndDraw() = 0;
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
__CRT_UUID_DECL(desktop_blur::Compositor2,0x735081dc,0x5e24,0x45da,0xa3,0x8f,0xe3,0x2c,0xc3,0x49,0xa9,0xa0)
__CRT_UUID_DECL(desktop_blur::KeyFrameAnimation,0x126e7f22,0x3ae9,0x4540,0x9a,0x8a,0xde,0xae,0x8a,0x4a,0x4a,0x84)
__CRT_UUID_DECL(desktop_blur::DesktopInterop,0x29e691fa,0x4567,0x4dca,0xb3,0x19,0xd0,0xf2,0x07,0xeb,0x68,0x07)
__CRT_UUID_DECL(desktop_blur::DrawingSurfaceInterop,0xfd04e6e3,0xfe0c,0x4c3c,0xab,0x19,0xa0,0x76,0x01,0xa5,0x76,0xee)
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
enum class Filter { Gaussian, Saturation };
inline const wchar_t* PropertyName(Filter filter) { return filter==Filter::Gaussian?L"Blur.BlurAmount":L"Color.Saturation"; }
class Gaussian final : public E::IGraphicsEffect, public E::IGraphicsEffectSource, public EffectInterop {
    std::atomic<ULONG> refs{1};
    ComPtr<E::IGraphicsEffectSource> source;
    ComPtr<F::IPropertyValueStatics> values;
    float deviation;
    Filter filter;
public:
    Gaussian(E::IGraphicsEffectSource* input,F::IPropertyValueStatics* factory,float radius,Filter type=Filter::Gaussian)
        : source(input),values(factory),deviation(radius),filter(type) { }
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
    HRESULT STDMETHODCALLTYPE get_Name(HSTRING* name) override { return WindowsCreateString(filter==Filter::Gaussian?L"Blur":L"Color",filter==Filter::Gaussian?4:5,name); }
    HRESULT STDMETHODCALLTYPE put_Name(HSTRING) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetEffectId(GUID* id) override {
        static constexpr GUID gaussian{0x1feb6d69,0x2fe6,0x4ac9,{0x8c,0x58,0x1d,0x7f,0x93,0xe7,0xa6,0xa5}};
        static constexpr GUID saturation{0x5cb2d9cf,0x327d,0x459f,{0xa0,0xce,0x40,0xc0,0xb2,0x08,0x6b,0xf7}};
        *id=filter==Filter::Gaussian?gaussian:saturation; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetNamedPropertyMapping(LPCWSTR name,UINT* index,UINT* mapping) override {
        if(wcscmp(name,filter==Filter::Gaussian?L"BlurAmount":L"Saturation")) return E_INVALIDARG;
        *index=0; *mapping=1; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyCount(UINT* count) override { *count=filter==Filter::Gaussian?3:1; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetProperty(UINT index,F::IPropertyValue** out) override {
        ComPtr<IInspectable> value;
        HRESULT hr=index==0?values->CreateSingle(deviation,&value):
            index<3&&filter==Filter::Gaussian?values->CreateUInt32(index==1?0:1,&value):E_INVALIDARG;
        return FAILED(hr)?hr:value.CopyTo(out);
    }
    HRESULT STDMETHODCALLTYPE GetSource(UINT index,E::IGraphicsEffectSource** out) override {
        if(index!=0) return E_INVALIDARG;
        return source.CopyTo(out);
    }
    HRESULT STDMETHODCALLTYPE GetSourceCount(UINT* count) override { *count=1; return S_OK; }
};

// Expose the Gaussian property to Composition; subsequent radius edits update
// the existing brush rather than destroying the HWND/backdrop and replaying a fade.
using PropertyIterator = F::Collections::IIterator<HSTRING>;
using PropertyIterable = F::Collections::IIterable<HSTRING>;
template<class Interface> class Inspectable : public Interface {
    std::atomic<ULONG> refs{1};
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** value) override {
        if(!value) return E_POINTER;
        *value=nullptr;
        if(iid!=__uuidof(IUnknown)&&iid!=__uuidof(IInspectable)&&iid!=__uuidof(Interface)) return E_NOINTERFACE;
        *value=static_cast<Interface*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG count=--refs; if(!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG* count,IID** ids) override { if(!count||!ids) return E_POINTER; *count=0; *ids=nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* name) override { if(!name) return E_POINTER; *name=nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* trust) override { if(!trust) return E_POINTER; *trust=BaseTrust; return S_OK; }
protected:
    virtual ~Inspectable()=default;
};
class PropertyNamesIterator final : public Inspectable<PropertyIterator> {
    bool current=true;
    Filter filter;
public:
    explicit PropertyNamesIterator(Filter type):filter(type) { }
    HRESULT STDMETHODCALLTYPE get_Current(HSTRING* value) override {
        if(!value) return E_POINTER;
        *value=nullptr;
        const wchar_t* name=PropertyName(filter);
        return current?WindowsCreateString(name,static_cast<UINT32>(wcslen(name)),value):E_BOUNDS;
    }
    HRESULT STDMETHODCALLTYPE get_HasCurrent(boolean* value) override { if(!value) return E_POINTER; *value=current; return S_OK; }
    HRESULT STDMETHODCALLTYPE MoveNext(boolean* value) override { if(!value) return E_POINTER; current=false; *value=false; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetMany(UINT32 size,HSTRING* items,UINT32* count) override {
        if(!count||(size&&!items)) return E_POINTER;
        *count=0;
        if(current&&size) { HRESULT hr=get_Current(items); if(FAILED(hr)) return hr; current=false; *count=1; }
        return S_OK;
    }
};
class PropertyNames final : public Inspectable<PropertyIterable> {
    Filter filter;
public:
    explicit PropertyNames(Filter type):filter(type) { }
    HRESULT STDMETHODCALLTYPE First(PropertyIterator** value) override {
        if(!value) return E_POINTER;
        *value=new PropertyNamesIterator(filter); return S_OK;
    }
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
    // Only the compiled topology is shared; each monitor still has an
    // independent brush, source, property set and animation state.
    ComPtr<C::ICompositionEffectFactory> gaussianFactory,saturationFactory;
    friend struct EngineTestAccess;
public:
    const wchar_t* stage=L"initialization";
    Engine()=default;
    Engine(const Engine&)=delete;
    Engine& operator=(const Engine&)=delete;
    ~Engine() {
        gaussianFactory.Reset(); saturationFactory.Reset();
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
        ComPtr<Compositor2> raw;
        stage=L"backdrop interface";
        if(FAILED(hr=compositor.As(&raw))) return hr;
        object.Reset();
        stage=L"unfiltered backdrop";
        if(FAILED(hr=raw->CreateBackdropBrush(&object))||FAILED(hr=object.As(&backdrop))) return hr;
        stage=L"property values";
        if(FAILED(hr=Activate(L"Windows.Foundation.PropertyValue",__uuidof(F::IPropertyValueStatics),reinterpret_cast<void**>(values.GetAddressOf())))) return hr;
        stage=L"source factory";
        return Activate(L"Windows.UI.Composition.CompositionEffectSourceParameter",__uuidof(SourceFactory),reinterpret_cast<void**>(sourceFactory.GetAddressOf()));
    }
    HRESULT CreateEffectBrush(Filter filter,float radius,C::ICompositionEffectBrush** brush) {
        auto& cached=filter==Filter::Gaussian?gaussianFactory:saturationFactory;
        if(!cached) {
            ComPtr<IInspectable> object;
            stage=L"source parameter";
            HRESULT hr=sourceFactory->Create(String(L"Backdrop"),&object);
            if(FAILED(hr)) return hr;
            ComPtr<E::IGraphicsEffectSource> input;
            if(FAILED(hr=object.As(&input))) return hr;
            ComPtr<E::IGraphicsEffect> effect;
            float initial=filter==Filter::Gaussian?static_cast<float>(blur_parameters::Sigma(radius)):
                std::isfinite(radius)?std::clamp(radius,0.f,1.f):1.f;
            effect.Attach(new Gaussian(input.Get(),values.Get(),initial,filter));
            ComPtr<PropertyIterable> names;names.Attach(new PropertyNames(filter));
            ComPtr<C::ICompositionEffectFactory> factory;
            stage=L"effect factory";
            hr=compositor->CreateEffectFactoryWithProperties(effect.Get(),names.Get(),&factory);
            if(FAILED(hr)) return hr;
            cached=std::move(factory);
        }
        stage=L"effect brush";
        return cached->CreateBrush(brush);
    }
public:
    struct Surface {
        HWND hwnd{};
        ComPtr<C::ICompositionTarget> target;
        ComPtr<C::IVisual> visual;
        ComPtr<C::ICompositionEffectBrush> effect;
        ComPtr<C::ICompositionPropertySet> properties;
        ComPtr<C::IInsetClip> clip;
        ComPtr<C::ICompositionBrush> frozen;
        ComPtr<C::ICompositionBrush> colorInput;
        float radius=-1;
        Filter filter=Filter::Gaussian;
        bool samplingEnabled=true,brushAttached=false,frozenAttached=false;
        void Reset() {
            if(target) target->put_Root(nullptr);
            properties.Reset(); clip.Reset(); effect.Reset(); frozen.Reset(); colorInput.Reset(); visual.Reset(); target.Reset(); radius=-1;
            samplingEnabled=true;brushAttached=false;frozenAttached=false;
            if(IsWindow(hwnd)) DestroyWindow(hwnd);
            hwnd=nullptr;
        }
    };
    // Use the same cubic Bézier as focus_animation::EaseOut. StartingValue
    // preserves the compositor's current opacity when a fade is reversed.
    HRESULT CreateOpacityAnimation(double milliseconds,float target,ComPtr<C::ICompositionAnimation>& animation) {
        ComPtr<C::IScalarKeyFrameAnimation> scalar;
        ComPtr<KeyFrameAnimation> timing;
        ComPtr<C::ICubicBezierEasingFunction> bezier;
        ComPtr<C::ICompositionEasingFunction> easing;
        HRESULT hr;
        if(FAILED(hr=compositor->CreateScalarKeyFrameAnimation(&scalar))) return hr;
        if(FAILED(hr=scalar.As(&timing))) return hr;
        if(FAILED(hr=compositor->CreateCubicBezierEasingFunction({.16f,1},{.30f,1},&bezier))) return hr;
        if(FAILED(hr=bezier.As(&easing))) return hr;
        if(FAILED(hr=timing->put_Duration({static_cast<INT64>(std::ceil(milliseconds*10000))}))) return hr;
        if(FAILED(hr=timing->InsertExpressionKeyFrame(0,String(L"this.StartingValue")))) return hr;
        if(FAILED(hr=scalar->InsertKeyFrameWithEasingFunction(1,target,easing.Get()))) return hr;
        return scalar.As(&animation);
    }
    HRESULT StartOpacityAnimation(Surface& surface,C::ICompositionAnimation* animation) {
        ComPtr<C::ICompositionObject> visual;
        HRESULT hr=surface.visual.As(&visual);
        return FAILED(hr)?hr:visual->StartAnimation(String(L"Opacity"),animation);
    }
    HRESULT FinishOpacityAnimation(Surface& surface,float opacity) {
        ComPtr<C::ICompositionObject> visual;
        HRESULT hr=surface.visual.As(&visual);
        if(FAILED(hr)) return hr;
        if(FAILED(hr=visual->StopAnimation(String(L"Opacity")))) return hr;
        return surface.visual->put_Opacity(opacity);
    }
    HRESULT Attach(HWND hwnd,int width,int height,float radius,Surface& surface,Filter filter=Filter::Gaussian) {
        // Respect accessibility/transparency preferences.
        if(!TransparencyAllowed()) return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        // HostBackdrop has an unavoidable system blur. The ordinary backdrop
        // reads the unfiltered pixels behind this transparent visual instead.
        HRESULT hr;
        if(FAILED(hr=Initialize())) return hr;
        surface.filter=filter;
        ComPtr<IInspectable> object;
        stage=L"desktop target";
        if(FAILED(hr=interop->CreateDesktopWindowTarget(hwnd,FALSE,&object))||FAILED(hr=object.As(&surface.target))) return hr;
        ComPtr<C::ISpriteVisual> sprite;
        stage=L"sprite visual";
        if(FAILED(hr=compositor->CreateSpriteVisual(&sprite))||FAILED(hr=sprite.As(&surface.visual))) return hr;
        if(FAILED(hr=surface.visual->put_Size({static_cast<float>(width),static_cast<float>(height)}))||FAILED(hr=surface.visual->put_Opacity(0))) return hr;
        if(FAILED(hr=CreateEffectBrush(filter,radius,&surface.effect))) return hr;
        ComPtr<C::ICompositionObject> effectObject;
        if(FAILED(hr=surface.effect.As(&effectObject))||FAILED(hr=effectObject->get_Properties(&surface.properties))) return hr;
        stage=L"effect source";
        if(FAILED(hr=surface.effect->SetSourceParameter(String(L"Backdrop"),backdrop.Get()))) return hr;
        if(FAILED(hr=SetRadius(surface,radius))) return hr;
        stage=L"target root";
        return surface.target->put_Root(surface.visual.Get());
    }
    HRESULT SetRadius(Surface& surface,float radius) {
        radius=surface.filter==Filter::Gaussian?static_cast<float>(blur_parameters::Sigma(radius)):
            std::isfinite(radius)?std::clamp(radius,0.f,1.f):1.f;
        if(surface.radius==radius) return S_OK;
        if(!surface.properties) return E_UNEXPECTED;
        stage=surface.filter==Filter::Gaussian?L"Gaussian radius property":L"Saturation property";
        HRESULT hr=surface.properties->InsertScalar(String(PropertyName(surface.filter)),radius);
        if(FAILED(hr)) return hr;
        float previous=surface.radius;surface.radius=radius;
        hr=UpdateBrush(surface);
        if(FAILED(hr)) surface.radius=previous;
        return hr;
    }
    HRESULT SetSampling(Surface& surface,bool enabled) {
        if(surface.samplingEnabled==enabled) return S_OK;
        bool previous=surface.samplingEnabled;surface.samplingEnabled=enabled;
        HRESULT hr=UpdateBrush(surface);
        if(FAILED(hr)) surface.samplingEnabled=previous;
        return hr;
    }
    HRESULT SetColorInput(Surface& surface,ComPtr<C::ICompositionBrush> texture) {
        if(surface.colorInput.Get()==texture.Get()) return S_OK;
        const HRESULT hr=surface.effect->SetSourceParameter(String(L"Backdrop"),texture?texture.Get():backdrop.Get());
        if(SUCCEEDED(hr)) surface.colorInput=std::move(texture);
        return hr;
    }
    HRESULT SnapshotDevice(ID2D1Device* device,ComPtr<C::ICompositionGraphicsDevice>& graphics) {
        ComPtr<C::ICompositorInterop> graphicsInterop;
        HRESULT hr=compositor.As(&graphicsInterop);
        return FAILED(hr)?hr:graphicsInterop->CreateGraphicsDevice(device,&graphics);
    }
    HRESULT UpdateSnapshot(C::ICompositionDrawingSurface* surface,ID2D1Bitmap1* bitmap) {
        ComPtr<DrawingSurfaceInterop> drawing;
        HRESULT hr=surface->QueryInterface(IID_PPV_ARGS(&drawing));
        if(FAILED(hr)) return hr;
        const auto size=bitmap->GetSize();
        ComPtr<ID2D1DeviceContext> context;
        POINT offset{};
        if(FAILED(hr=drawing->BeginDraw(nullptr,IID_PPV_ARGS(&context),&offset))) return hr;
        context->SetDpi(96,96);
        const auto dest=D2D1::RectF(offset.x,offset.y,offset.x+size.width,offset.y+size.height);
        context->DrawBitmap(bitmap,&dest,1,D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR,nullptr,nullptr);
        return drawing->EndDraw();
    }
    HRESULT SnapshotBrush(C::ICompositionGraphicsDevice* graphics,ID2D1Bitmap1* bitmap,ComPtr<C::ICompositionBrush>& brush,
        ComPtr<C::ICompositionDrawingSurface>* retained=nullptr) {
        const auto size=bitmap->GetSize();
        namespace DX=ABI::Windows::Graphics::DirectX;
        ComPtr<C::ICompositionDrawingSurface> surface;
        HRESULT hr;
        if(FAILED(hr=graphics->CreateDrawingSurface({size.width,size.height},DX::DirectXPixelFormat_B8G8R8A8UIntNormalized,
            DX::DirectXAlphaMode_Premultiplied,&surface))||FAILED(hr=UpdateSnapshot(surface.Get(),bitmap))) return hr;
        ComPtr<C::ICompositionSurface> content;
        ComPtr<C::ICompositionSurfaceBrush> texture;
        if(FAILED(hr=surface.As(&content))||FAILED(hr=compositor->CreateSurfaceBrushWithSurface(content.Get(),&texture))||
           FAILED(hr=texture->put_Stretch(C::CompositionStretch_Fill))) return hr;
        hr=texture.As(&brush);
        if(SUCCEEDED(hr)&&retained) *retained=std::move(surface);
        return hr;
    }
    HRESULT SetFrozenBrush(Surface& surface,ComPtr<C::ICompositionBrush> brush) {
        if(!surface.frozen&&!brush) return S_OK;
        auto old=std::move(surface.frozen);
        surface.frozen=std::move(brush);
        const HRESULT hr=UpdateBrush(surface);
        if(FAILED(hr)) surface.frozen=std::move(old);
        return hr;
    }
private:
    HRESULT UpdateBrush(Surface& surface) {
        bool active=surface.samplingEnabled&&(surface.filter==Filter::Gaussian?surface.radius>0:surface.radius<1);
        const bool frozen=active&&surface.frozen;
        const bool live=active&&!frozen;
        if(live==surface.brushAttached&&frozen==surface.frozenAttached&&!frozen) return S_OK;
        ComPtr<C::ISpriteVisual> sprite;
        HRESULT hr=surface.visual.As(&sprite);
        if(FAILED(hr)) return hr;
        ComPtr<C::ICompositionBrush> brush;
        if(frozen) brush=surface.frozen;
        if(live&&FAILED(hr=surface.effect.As(&brush))) return hr;
        // Detach the render graph, not just its opacity or HWND visibility.
        // Cached objects/radius survive so restoring the live desktop costs no
        // screenshot, GPU readback, texture allocation, or animation reset.
        hr=sprite->put_Brush(brush.Get());
        if(SUCCEEDED(hr)) { surface.brushAttached=live; surface.frozenAttached=frozen; }
        return hr;
    }
public:
    HRESULT Clip(Surface& surface,const RECT& monitor,const RECT& safe) {
        HRESULT hr;
        if(!surface.clip) {
            if(FAILED(hr=compositor->CreateInsetClip(&surface.clip))) return hr;
            ComPtr<C::ICompositionClip> clip;
            if(FAILED(hr=surface.clip.As(&clip))||FAILED(hr=surface.visual->put_Clip(clip.Get()))) return hr;
        }
        if(FAILED(hr=surface.clip->put_LeftInset(static_cast<float>(safe.left-monitor.left)))||
           FAILED(hr=surface.clip->put_TopInset(static_cast<float>(safe.top-monitor.top)))||
           FAILED(hr=surface.clip->put_RightInset(static_cast<float>(monitor.right-safe.right)))) return hr;
        return surface.clip->put_BottomInset(static_cast<float>(monitor.bottom-safe.bottom));
    }
};
}

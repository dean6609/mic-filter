#include <initguid.h>
#include "rate_processor.h"
#include "apo_sdk.h"
#include <cstring>
#include <new>
#include "apo_trace.h"
#include <functiondiscoverykeys_devpkey.h>

namespace {
volatile LONG objects=0;
volatile LONG locks=0;
constexpr GUID floatGuid={3,0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
bool format(IAudioMediaType* media,UNCOMPRESSEDAUDIOFORMAT& out) {
    return media && SUCCEEDED(media->GetUncompressedAudioFormat(&out)) &&
        out.guidFormatType==floatGuid && out.dwBytesPerSampleContainer==4 &&
        out.dwValidBitsPerSample==32 && out.dwSamplesPerFrame>=1 && out.dwSamplesPerFrame<=8 &&
        out.fFramesPerSecond>=8000.0f && out.fFramesPerSecond<=192000.0f;
}
class Apo final:public IAudioProcessingObject,public IAudioProcessingObjectRT,
                public IAudioProcessingObjectConfiguration,public IAudioSystemEffects {
    class InnerUnknown final:public IUnknown {
        Apo& owner_;
    public:
        explicit InnerUnknown(Apo& owner):owner_(owner){}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override{return owner_.nonDelegatingQueryInterface(iid,out);}
        ULONG STDMETHODCALLTYPE AddRef() override{return owner_.nonDelegatingAddRef();}
        ULONG STDMETHODCALLTYPE Release() override{return owner_.nonDelegatingRelease();}
    } inner_{*this};
    IUnknown* outer_=nullptr;
    volatile LONG references_=1;
    bool initialized_=false,locked_=false,supported_=false;
    UINT32 channels_=0,maxFrames_=0;
    float rate_=0;
    micfilter::StateMapping mapping_;
    micfilter::StateMapping endpointTelemetry_;
    micfilter::OptionsMapping options_;
    micfilter::RateProcessor processor_;
    micfilter::Trace trace_;
    HRESULT report(const wchar_t* step,HRESULT result){
        std::wostringstream log;log<<step<<L" HRESULT=0x"<<std::hex<<static_cast<unsigned long>(result);trace_.write(log.str());
        return result;
    }
public:
    explicit Apo(IUnknown* outer=nullptr):outer_(outer){InterlockedIncrement(&objects);trace_.write(L"APO constructed aggregated="+std::to_wstring(outer!=nullptr));}
    ~Apo(){InterlockedDecrement(&objects);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        return outer_?outer_->QueryInterface(iid,out):nonDelegatingQueryInterface(iid,out);
    }
    HRESULT nonDelegatingQueryInterface(REFIID iid,void** out) {
        wchar_t iidText[40]{};StringFromGUID2(iid,iidText,40);trace_.write(L"APO interface "+std::wstring(iidText));
        if(!out) return E_POINTER;
        *out=nullptr;
        if(iid==__uuidof(IUnknown)){*out=static_cast<IUnknown*>(&inner_);nonDelegatingAddRef();return S_OK;}
        if(iid==__uuidof(IAudioProcessingObject)) *out=static_cast<IAudioProcessingObject*>(this);
        else if(iid==__uuidof(IAudioProcessingObjectRT)) *out=static_cast<IAudioProcessingObjectRT*>(this);
        else if(iid==__uuidof(IAudioProcessingObjectConfiguration)) *out=static_cast<IAudioProcessingObjectConfiguration*>(this);
        else if(iid==__uuidof(IAudioSystemEffects)) *out=static_cast<IAudioSystemEffects*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return outer_?outer_->AddRef():nonDelegatingAddRef();}
    ULONG STDMETHODCALLTYPE Release() override {return outer_?outer_->Release():nonDelegatingRelease();}
    ULONG nonDelegatingAddRef(){return InterlockedIncrement(&references_);}
    ULONG nonDelegatingRelease(){const auto n=InterlockedDecrement(&references_);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Initialize(UINT32 bytes,BYTE* data) override {
        if(initialized_)return APOERR_ALREADY_INITIALIZED;
        if(bytes && !data)return E_POINTER;
        initialized_=true;
        const bool mapped=mapping_.open(); // All file operations happen before real-time processing.
        options_.open(); // Optional: without it the Natural voice preset applies.
        // Endpoint telemetry prevents another input's activity from confirming this microphone.
        // Unknown initialization layouts keep global controls but cannot confirm an endpoint.
        if(data&&(bytes==sizeof(APOInitSystemEffects)||bytes==sizeof(APOInitSystemEffects2))){
            APOInitSystemEffects context{};std::memcpy(&context,data,sizeof(context));
            if(context.APOInit.cbSize==bytes&&context.APOInit.clsid==micfilter::kClsid&&context.pAPOEndpointProperties){
                PROPVARIANT value;PropVariantInit(&value);
                if(SUCCEEDED(context.pAPOEndpointProperties->GetValue(PKEY_AudioEndpoint_GUID,&value))&&value.vt==VT_LPWSTR&&value.pwszVal)
                    endpointTelemetry_.openEndpoint(value.pwszVal);
                PropVariantClear(&value);
            }
        }
        const auto mappingError=mapped?0:GetLastError();
        trace_.write(L"Initialize bytes="+std::to_wstring(bytes)+L" controls="+std::to_wstring(mapped)+L" mappingError="+std::to_wstring(mappingError));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Reset() override {processor_.reset();return S_OK;}
    HRESULT STDMETHODCALLTYPE GetLatency(HNSTIME* time) override {
        if(!time)return E_POINTER;
        // 10 ms buffering plus RNNoise's 20 ms (overlap/add and delayed_X). Bypass has zero delay.
        *time=(supported_ && micfilter::snapshot(mapping_.get()).enabled) ? processor_.latency() : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetInputChannelCount(UINT32* count) override {if(!count)return E_POINTER;*count=channels_?channels_:2;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetRegistrationProperties(APO_REG_PROPERTIES** result) override {
        trace_.write(L"GetRegistrationProperties");
        if(!result)return E_POINTER;
        *result=nullptr;
        constexpr UINT32 interfaceCount=1;
        const size_t size=sizeof(APO_REG_PROPERTIES)+(interfaceCount-1)*sizeof(IID);
        auto* p=static_cast<APO_REG_PROPERTIES*>(CoTaskMemAlloc(size));
        if(!p)return E_OUTOFMEMORY;
        std::memset(p,0,size);
        p->clsid=micfilter::kClsid;p->Flags=static_cast<APO_FLAG>(APO_FLAG_DEFAULT|APO_FLAG_INPLACE);
        wcscpy_s(p->szFriendlyName,L"MicFilter - RNNoise v1.21");
        wcscpy_s(p->szCopyrightInfo,L"GPL-3.0; RNNoise: Xiph.Org BSD-3-Clause");
        p->u32MajorVersion=0;p->u32MinorVersion=5;
        p->u32MinInputConnections=p->u32MaxInputConnections=1;
        p->u32MinOutputConnections=p->u32MaxOutputConnections=1;
        p->u32MaxInstances=UINT32_MAX;p->u32NumAPOInterfaces=interfaceCount;
        p->iidAPOInterfaceList[0]=__uuidof(IAudioProcessingObject);
        *result=p;return S_OK;
    }
    HRESULT checkFormat(IAudioMediaType* opposite,IAudioMediaType* requested,IAudioMediaType** suggested) {
        if(suggested)*suggested=nullptr;
        UNCOMPRESSEDAUDIOFORMAT req{},opp{};
        if(requested&&SUCCEEDED(requested->GetUncompressedAudioFormat(&req))){std::wostringstream log;log<<L"Format rate="<<req.fFramesPerSecond<<L" channels="<<req.dwSamplesPerFrame<<L" bytes="<<req.dwBytesPerSampleContainer<<L" bits="<<req.dwValidBitsPerSample<<L" subtype="<<req.guidFormatType.Data1;trace_.write(log.str());}
        if(!format(requested,req))return report(L"Format requested",APOERR_FORMAT_NOT_SUPPORTED);
        if(opposite && (!format(opposite,opp) || req.dwSamplesPerFrame!=opp.dwSamplesPerFrame || req.fFramesPerSecond!=opp.fFramesPerSecond))return report(L"Format opposite",APOERR_FORMAT_NOT_SUPPORTED);
        if(suggested){*suggested=requested;requested->AddRef();}
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE IsInputFormatSupported(IAudioMediaType* opposite,IAudioMediaType* requested,IAudioMediaType** suggested) override{return checkFormat(opposite,requested,suggested);}
    HRESULT STDMETHODCALLTYPE IsOutputFormatSupported(IAudioMediaType* opposite,IAudioMediaType* requested,IAudioMediaType** suggested) override{return checkFormat(opposite,requested,suggested);}
    HRESULT STDMETHODCALLTYPE LockForProcess(UINT32 inputs,APO_CONNECTION_DESCRIPTOR** in,UINT32 outputs,APO_CONNECTION_DESCRIPTOR** out) override {
        trace_.write(L"LockForProcess inputs="+std::to_wstring(inputs)+L" outputs="+std::to_wstring(outputs));
        if(!initialized_)return APOERR_NOT_INITIALIZED;
        if(locked_)return APOERR_APO_LOCKED;
        if(inputs!=1 || outputs!=1)return APOERR_NUM_CONNECTIONS_INVALID;
        if(!in||!out||!in[0]||!out[0])return E_POINTER;
        UNCOMPRESSEDAUDIOFORMAT a{},b{};
        if(!format(in[0]->pFormat,a)||!format(out[0]->pFormat,b)||a.dwSamplesPerFrame!=b.dwSamplesPerFrame||a.fFramesPerSecond!=b.fFramesPerSecond)return report(L"Lock format",APOERR_INVALID_CONNECTION_FORMAT);
        if(!in[0]->u32MaxFrameCount || out[0]->u32MaxFrameCount<in[0]->u32MaxFrameCount)return report(L"Lock buffer capacity",APOERR_INVALID_OUTPUT_MAXFRAMECOUNT);
        channels_=a.dwSamplesPerFrame;rate_=a.fFramesPerSecond;maxFrames_=in[0]->u32MaxFrameCount;
        supported_=processor_.initialize(channels_,static_cast<unsigned>(rate_));
        if(!supported_)return E_OUTOFMEMORY;
        for(auto* state:{mapping_.get(),endpointTelemetry_.get()})if(state) {
            InterlockedExchange(&state->channels,channels_);InterlockedExchange(&state->sampleRate,static_cast<LONG>(rate_));
            InterlockedExchange(&state->formatSupported,supported_?1:0);
        }
        locked_=true;return report(L"LockForProcess",S_OK);
    }
    HRESULT STDMETHODCALLTYPE UnlockForProcess() override {if(!locked_)return APOERR_ALREADY_UNLOCKED;locked_=false;return S_OK;}
    void STDMETHODCALLTYPE APOProcess(UINT32 inputs,APO_CONNECTION_PROPERTY** in,UINT32 outputs,APO_CONNECTION_PROPERTY** out) override {
        if(outputs!=1||!out||!out[0])return;
        // Input and output connection metadata may alias for an in-place graph.
        const auto inputFlags=(inputs==1&&in&&in[0])?in[0]->u32BufferFlags:BUFFER_INVALID;
        const auto frames=(inputs==1&&in&&in[0])?in[0]->u32ValidFrameCount:0;
        const auto inputBuffer=(inputs==1&&in&&in[0])?in[0]->pBuffer:0;
        out[0]->u32BufferFlags=BUFFER_INVALID;out[0]->u32ValidFrameCount=0;
        if(!locked_||inputFlags==BUFFER_INVALID)return;
        if(frames>maxFrames_||!out[0]->pBuffer)return;
        const bool silent=inputFlags==BUFFER_SILENT;
        if(!silent&&!inputBuffer)return;
        auto* dst=reinterpret_cast<float*>(out[0]->pBuffer);
        const auto* src=reinterpret_cast<const float*>(inputBuffer);
        auto* state=mapping_.get();const auto settings=micfilter::snapshot(state,options_.get());
        if(supported_)processor_.process(src,dst,frames,silent,settings);
        else if(silent)std::memset(dst,0,static_cast<size_t>(frames)*channels_*4);
        else if(src!=dst)std::memcpy(dst,src,static_cast<size_t>(frames)*channels_*4);
        out[0]->u32ValidFrameCount=frames;
        out[0]->u32BufferFlags=(silent&&(!supported_||!settings.enabled))?BUFFER_SILENT:BUFFER_VALID;
        for(auto* telemetry:{state,endpointTelemetry_.get()})if(telemetry){
            InterlockedExchange(&telemetry->producerPid,static_cast<LONG>(GetCurrentProcessId()));InterlockedIncrement64(&telemetry->callbacks);InterlockedExchange64(&telemetry->lastTick,GetTickCount64());
            if(supported_&&settings.enabled&&processor_.healthy())InterlockedAdd64(&telemetry->processedFrames,frames);
            if(!processor_.healthy())InterlockedExchange(&telemetry->formatSupported,0);
        }
    }
    UINT32 STDMETHODCALLTYPE CalcInputFrames(UINT32 frames) override{return frames;}
    UINT32 STDMETHODCALLTYPE CalcOutputFrames(UINT32 frames) override{return frames;}
};
class Factory final:public IClassFactory {
    volatile LONG references_=1;
public:
    Factory(){InterlockedIncrement(&objects);}~Factory(){InterlockedDecrement(&objects);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** p) override {if(!p)return E_POINTER;*p=nullptr;if(iid!=__uuidof(IUnknown)&&iid!=__uuidof(IClassFactory))return E_NOINTERFACE;*p=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&references_);}
    ULONG STDMETHODCALLTYPE Release() override {auto n=InterlockedDecrement(&references_);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer,REFIID iid,void** p) override {micfilter::Trace trace;wchar_t text[40]{};StringFromGUID2(iid,text,40);trace.write(L"Factory outer="+std::to_wstring(outer!=nullptr)+L" iid="+text);if(!p)return E_POINTER;*p=nullptr;if(outer&&iid!=__uuidof(IUnknown))return E_NOINTERFACE;auto* apo=new(std::nothrow)Apo(outer);if(!apo)return E_OUTOFMEMORY;auto hr=apo->nonDelegatingQueryInterface(iid,p);apo->nonDelegatingRelease();return hr;}
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {if(lock)InterlockedIncrement(&locks);else InterlockedDecrement(&locks);return S_OK;}
};
}
extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid,REFIID iid,void** out) {
    micfilter::Trace trace;trace.write(L"DllGetClassObject");
    if(!out)return E_POINTER;*out=nullptr;if(clsid!=micfilter::kClsid)return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory=new(std::nothrow)Factory;if(!factory)return E_OUTOFMEMORY;
    const auto hr=factory->QueryInterface(iid,out);factory->Release();return hr;
}
extern "C" HRESULT __stdcall DllCanUnloadNow(){return !objects&&!locks?S_OK:S_FALSE;}
BOOL WINAPI DllMain(HINSTANCE,DWORD,LPVOID){return TRUE;}

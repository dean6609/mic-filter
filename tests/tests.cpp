#include <initguid.h>
#include "shared.h"
#include "dsp_tests.h"
#include "dsp.h"
#include "rate_processor.h"
#include "apo_sdk.h"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

class PrivateState {
    std::wstring original_,directory_;
public:
    PrivateState(){
        wchar_t previous[32768]{},temp[32768]{},guidText[40]{};GUID id{};
        const auto length=GetEnvironmentVariableW(L"ProgramData",previous,32768);require(length&&length<32768,"read original ProgramData");original_=previous;
        require(GetTempPathW(32768,temp)>0&&SUCCEEDED(CoCreateGuid(&id))&&StringFromGUID2(id,guidText,40)>0,"private state path");
        directory_=std::wstring(temp)+L"MicFilter-test-"+guidText;
        require(CreateDirectoryW(directory_.c_str(),nullptr)!=0,"private test directory");
        require(CreateDirectoryW((directory_+L"\\MicFilter").c_str(),nullptr)!=0,"private settings directory");
        require(SetEnvironmentVariableW(L"ProgramData",directory_.c_str())!=0,"isolate test process environment");
        require(micfilter::dataDirectory()==directory_+L"\\MicFilter","tests did not use private state path");
    }
    void enable(){
        micfilter::SharedState state{};state.magic=micfilter::kMagic;state.version=micfilter::kStateVersion;state.enabled=1;state.graceBlocks=20;state.wetPermille=1000;
        const auto file=CreateFileW((directory_+L"\\MicFilter\\state.bin").c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        require(file!=INVALID_HANDLE_VALUE,"create isolated controls");DWORD bytes=0;const auto written=WriteFile(file,&state,sizeof(state),&bytes,nullptr);CloseHandle(file);require(written&&bytes==sizeof(state),"write isolated controls");
    }
    void endpoint(const std::wstring& guid){
        CreateDirectoryW((directory_+L"\\MicFilter\\endpoints").c_str(),nullptr);
        micfilter::SharedState state{};state.magic=micfilter::kMagic;state.version=micfilter::kStateVersion;
        HANDLE f=CreateFileW((directory_+L"\\MicFilter\\endpoints\\"+guid+L".bin").c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);
        require(f!=INVALID_HANDLE_VALUE,"create private endpoint telemetry");DWORD bytes=0;require(WriteFile(f,&state,64,&bytes,nullptr)&&bytes==64,"write private endpoint telemetry");CloseHandle(f);
    }
    ~PrivateState(){
        SetEnvironmentVariableW(L"ProgramData",original_.c_str());
        DeleteFileW((directory_+L"\\MicFilter\\state.bin").c_str());
        WIN32_FIND_DATAW item{};auto files=FindFirstFileW((directory_+L"\\MicFilter\\endpoints\\*.bin").c_str(),&item);
        if(files!=INVALID_HANDLE_VALUE){do{DeleteFileW((directory_+L"\\MicFilter\\endpoints\\"+item.cFileName).c_str());}while(FindNextFileW(files,&item));FindClose(files);}
        RemoveDirectoryW((directory_+L"\\MicFilter\\endpoints").c_str());
        RemoveDirectoryW((directory_+L"\\MicFilter").c_str());RemoveDirectoryW(directory_.c_str());
    }
};
class Media final:public IAudioMediaType {
    ULONG refs_=1;
public:
    WAVEFORMATEX wave{};
    Media(unsigned channels,unsigned rate){wave.wFormatTag=WAVE_FORMAT_IEEE_FLOAT;wave.nChannels=channels;wave.nSamplesPerSec=rate;wave.wBitsPerSample=32;wave.nBlockAlign=channels*4;wave.nAvgBytesPerSec=rate*channels*4;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** p)override{if(!p)return E_POINTER;*p=nullptr;if(iid!=__uuidof(IUnknown)&&iid!=__uuidof(IAudioMediaType))return E_NOINTERFACE;*p=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}ULONG STDMETHODCALLTYPE Release()override{return --refs_;}
    HRESULT STDMETHODCALLTYPE IsCompressedFormat(BOOL* p)override{if(!p)return E_POINTER;*p=FALSE;return S_OK;}
    HRESULT STDMETHODCALLTYPE IsEqual(IAudioMediaType*,DWORD*)override{return E_NOTIMPL;}
    const WAVEFORMATEX* STDMETHODCALLTYPE GetAudioFormat()override{return &wave;}
    HRESULT STDMETHODCALLTYPE GetUncompressedAudioFormat(UNCOMPRESSEDAUDIOFORMAT* p)override{if(!p)return E_POINTER;*p={GUID{3,0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}},wave.nChannels,4,32,static_cast<float>(wave.nSamplesPerSec),0};return S_OK;}
};
class TestOuter final:public IUnknown {
public:
    ULONG references=1;
    IUnknown* inner=nullptr;
    ~TestOuter(){if(inner)inner->Release();}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** result) override{
        if(!result)return E_POINTER;*result=nullptr;
        if(iid==__uuidof(IUnknown)){*result=this;AddRef();return S_OK;}
        return inner?inner->QueryInterface(iid,result):E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return ++references;}
    ULONG STDMETHODCALLTYPE Release() override{return --references;}
};
class EndpointStore final:public IPropertyStore {
    std::wstring guid_;
public:
    explicit EndpointStore(std::wstring guid):guid_(std::move(guid)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void** out)override{if(out)*out=nullptr;return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return 1;} ULONG STDMETHODCALLTYPE Release()override{return 1;}
    HRESULT STDMETHODCALLTYPE GetCount(DWORD*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetAt(DWORD,PROPERTYKEY*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetValue(REFPROPERTYKEY key,PROPVARIANT* value)override{
        if(!value)return E_POINTER;PropVariantInit(value);
        if(key.fmtid!=PKEY_AudioEndpoint_GUID.fmtid||key.pid!=PKEY_AudioEndpoint_GUID.pid)return E_INVALIDARG;
        value->vt=VT_LPWSTR;value->pwszVal=static_cast<LPWSTR>(CoTaskMemAlloc((guid_.size()+1)*sizeof(wchar_t)));
        if(!value->pwszVal)return E_OUTOFMEMORY;std::memcpy(value->pwszVal,guid_.c_str(),(guid_.size()+1)*sizeof(wchar_t));return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetValue(REFPROPERTYKEY,REFPROPVARIANT)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Commit()override{return E_NOTIMPL;}
};
void apoTests(IPropertyStore* endpoint=nullptr){
    auto library=LoadLibraryW(L"MicFilterAPO.dll");require(library!=nullptr,"DLL failed to load");
    using FactoryFn=HRESULT(__stdcall*)(REFCLSID,REFIID,void**);using UnloadFn=HRESULT(__stdcall*)();
    auto factoryFn=reinterpret_cast<FactoryFn>(GetProcAddress(library,"DllGetClassObject"));auto unload=reinterpret_cast<UnloadFn>(GetProcAddress(library,"DllCanUnloadNow"));
    require(factoryFn&&unload,"COM exports missing");require(unload()==S_OK,"new DLL busy");IClassFactory* factory=nullptr;
    require(SUCCEEDED(factoryFn(micfilter::kClsid,__uuidof(IClassFactory),reinterpret_cast<void**>(&factory))),"class factory failed");
    {
        TestOuter outer;void* invalid=reinterpret_cast<void*>(1);
        require(factory->CreateInstance(&outer,__uuidof(IAudioProcessingObject),&invalid)==E_NOINTERFACE&&!invalid,"aggregation must initially request inner IUnknown");
        require(factory->CreateInstance(&outer,__uuidof(IUnknown),reinterpret_cast<void**>(&outer.inner))==S_OK,"audio engine COM aggregation rejected");
        IAudioProcessingObject* aggregated=nullptr;
        require(outer.QueryInterface(__uuidof(IAudioProcessingObject),reinterpret_cast<void**>(&aggregated))==S_OK,"aggregated APO interface");
        IUnknown* identity=nullptr;require(aggregated->QueryInterface(__uuidof(IUnknown),reinterpret_cast<void**>(&identity))==S_OK&&identity==static_cast<IUnknown*>(&outer),"aggregated controlling identity");
        identity->Release();aggregated->Release();require(outer.references==1,"aggregated outer references leaked");
        IUnknown* innerIdentity=nullptr;require(outer.inner->QueryInterface(__uuidof(IUnknown),reinterpret_cast<void**>(&innerIdentity))==S_OK&&innerIdentity==outer.inner,"nondelegating inner identity");innerIdentity->Release();
    }
    IAudioProcessingObject* apo=nullptr;require(SUCCEEDED(factory->CreateInstance(nullptr,__uuidof(IAudioProcessingObject),reinterpret_cast<void**>(&apo))),"APO creation failed");factory->Release();
    APOInitSystemEffects2 context{};context.APOInit.cbSize=sizeof(context);context.APOInit.clsid=micfilter::kClsid;context.pAPOEndpointProperties=endpoint;
    require(apo->Initialize(endpoint?sizeof(context):0,endpoint?reinterpret_cast<BYTE*>(&context):nullptr)==S_OK,"APO initialize");require(apo->Initialize(0,nullptr)==APOERR_ALREADY_INITIALIZED,"double initialize accepted");
    APO_REG_PROPERTIES* properties=nullptr;require(apo->GetRegistrationProperties(&properties)==S_OK,"registration properties");require(properties->clsid==micfilter::kClsid&&properties->u32NumAPOInterfaces==1&&properties->iidAPOInterfaceList[0]==__uuidof(IAudioProcessingObject),"registration incorrect");
    require(properties->u32MajorVersion==0&&properties->u32MinorVersion==7,"APO version differs from installer registration");CoTaskMemFree(properties);
    IAudioProcessingObjectConfiguration* config=nullptr;IAudioProcessingObjectRT* rt=nullptr;IAudioSystemEffects* effects=nullptr;
    require(SUCCEEDED(apo->QueryInterface(__uuidof(IAudioProcessingObjectConfiguration),reinterpret_cast<void**>(&config))),"configuration interface");
    require(SUCCEEDED(apo->QueryInterface(__uuidof(IAudioProcessingObjectRT),reinterpret_cast<void**>(&rt))),"realtime interface");
    require(SUCCEEDED(apo->QueryInterface(__uuidof(IAudioSystemEffects),reinterpret_cast<void**>(&effects))),"system effect interface");effects->Release();
    IAudioSystemEffects2* modeEffects=nullptr;
    micfilter::StateMapping effectsMapping;effectsMapping.open();
    require(SUCCEEDED(apo->QueryInterface(__uuidof(IAudioSystemEffects2),reinterpret_cast<void**>(&modeEffects))),"mode-aware system effect interface");
    GUID* effectIds=nullptr;UINT effectCount=99;
    require(modeEffects->GetEffectsList(nullptr,&effectCount,nullptr)==E_POINTER,"effects list accepted null output");
    HANDLE changed=CreateEventW(nullptr,FALSE,FALSE,nullptr);require(changed!=nullptr,"effects event");
    require(modeEffects->GetEffectsList(&effectIds,&effectCount,changed)==S_OK,"mode-aware effects discovery");
    require(effectCount==(effectsMapping.get()?1U:0U),"effects discovery differs from enabled controls");
    if(effectCount)require(effectIds[0]==kNoiseSuppressionEffect,"incorrect noise suppression effect identity");CoTaskMemFree(effectIds);
    if(effectsMapping.get()){
        InterlockedExchange(&effectsMapping.get()->enabled,0);
        require(WaitForSingleObject(changed,2000)==WAIT_OBJECT_0,"effects change did not notify Windows");
        require(modeEffects->GetEffectsList(&effectIds,&effectCount,nullptr)==S_OK&&effectCount==0&&effectIds==nullptr,"disabled effect reported active");
        InterlockedExchange(&effectsMapping.get()->enabled,1);
        require(WaitForSingleObject(changed,500)==WAIT_TIMEOUT,"removed notification handle still signalled");
    }else require(modeEffects->GetEffectsList(&effectIds,&effectCount,nullptr)==S_OK,"clear effects notification");
    CloseHandle(changed);modeEffects->Release();
    Media media(2,48000);IAudioMediaType* suggested=nullptr;require(apo->IsInputFormatSupported(nullptr,&media,&suggested)==S_OK&&suggested==&media,"format negotiation");suggested->Release();
    auto input=signal(512,2);std::vector<float> output(input.size(),0);
    APO_CONNECTION_DESCRIPTOR inDesc{APO_CONNECTION_BUFFER_TYPE_EXTERNAL,reinterpret_cast<UINT_PTR>(input.data()),512,&media,APO_CONNECTION_DESCRIPTOR_SIGNATURE};
    APO_CONNECTION_DESCRIPTOR outDesc{APO_CONNECTION_BUFFER_TYPE_EXTERNAL,reinterpret_cast<UINT_PTR>(output.data()),512,&media,APO_CONNECTION_DESCRIPTOR_SIGNATURE};
    auto* ip=&inDesc;auto* op=&outDesc;require(config->LockForProcess(1,&ip,1,&op)==S_OK,"lock failed");require(config->LockForProcess(1,&ip,1,&op)==APOERR_APO_LOCKED,"double lock accepted");
    APO_CONNECTION_PROPERTY in{reinterpret_cast<UINT_PTR>(input.data()),512,BUFFER_VALID,APO_CONNECTION_PROPERTY_SIGNATURE};APO_CONNECTION_PROPERTY out{reinterpret_cast<UINT_PTR>(output.data()),0,BUFFER_INVALID,APO_CONNECTION_PROPERTY_SIGNATURE};auto* i=&in;auto* o=&out;
    rt->APOProcess(1,&i,1,&o);require(out.u32BufferFlags==BUFFER_VALID&&out.u32ValidFrameCount==512,"APO output metadata");
    // On an uninstalled workstation state.bin is absent, which must mean fail-open/bypass.
    micfilter::StateMapping privateMapping;
    if(!privateMapping.open())require(output==input,"missing settings does not bypass");
    else require(output==run(input,2,{512},{true,0,20,1}),"APO output differs from model with private controls");
    auto alias=input;APO_CONNECTION_PROPERTY aliasProperty{reinterpret_cast<UINT_PTR>(alias.data()),512,BUFFER_VALID,APO_CONNECTION_PROPERTY_SIGNATURE};auto* aliasPtr=&aliasProperty;rt->APOProcess(1,&aliasPtr,1,&aliasPtr);require(aliasProperty.u32BufferFlags==BUFFER_VALID&&aliasProperty.u32ValidFrameCount==512,"aliased connection properties failed");
    in.u32ValidFrameCount=513;rt->APOProcess(1,&i,1,&o);require(out.u32BufferFlags==BUFFER_INVALID&&out.u32ValidFrameCount==0,"oversized buffer accepted");
    require(config->UnlockForProcess()==S_OK,"unlock failed");Media incompatible(2,44100);inDesc.pFormat=&incompatible;outDesc.pFormat=&incompatible;
    require(config->LockForProcess(1,&ip,1,&op)==S_OK,"44.1 kHz resampling lock failed");in.u32ValidFrameCount=512;rt->APOProcess(1,&i,1,&o);
    if(!privateMapping.get())require(output==input,"missing controls at 44.1 kHz do not bypass");
    else require(output==runRate(input,2,44100,{512},{true,0,20,1}),"44.1 kHz APO differs from streaming model");
    config->UnlockForProcess();
    rt->Release();config->Release();apo->Release();require(unload()==S_OK,"COM references leaked");FreeLibrary(library);
    std::cout<<"PASS APO: DLL/COM factory, all interfaces, negotiation, locking, metadata, bounds, 44.1 kHz processing and safe missing-control bypass, reference counts\n";
}
int main(){try{CoInitializeEx(nullptr,COINIT_MULTITHREADED);{PrivateState state;dspTests();rateTests();apoTests();state.enable();apoTests();micfilter::StateMapping telemetry;require(telemetry.open(),"open private telemetry");require(micfilter::read(telemetry.get()->callbacks)==3&&micfilter::read(telemetry.get()->producerPid)==static_cast<LONG>(GetCurrentProcessId()),"private APO telemetry");
    const std::wstring first=L"{00000000-0000-0000-0000-000000000001}",second=L"{00000000-0000-0000-0000-000000000002}";
    state.endpoint(first);state.endpoint(second);EndpointStore firstStore(first),secondStore(second);micfilter::StateMapping a,b;
    require(a.openEndpoint(first)&&b.openEndpoint(second),"map per-input telemetry");apoTests(&firstStore);
    require(micfilter::read(a.get()->callbacks)==3&&micfilter::read(b.get()->callbacks)==0,"one input falsely confirms another");apoTests(&secondStore);
    require(micfilter::read(a.get()->callbacks)==3&&micfilter::read(b.get()->callbacks)==3,"per-input counters are not independent");
    require(micfilter::read(a.get()->processedFrames)==1536&&micfilter::read(b.get()->processedFrames)==1536,"per-input processing not confirmed");
    std::cout<<"PASS isolation: private controls and independent per-input APO telemetry; installed microphones unchanged\n";
}CoUninitialize();return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}

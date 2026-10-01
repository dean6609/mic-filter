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

void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
class PrivateState {
    std::wstring original_,directory_;
public:
    PrivateState(){
        wchar_t previous[32768]{},temp[32768]{},guidText[40]{};GUID id{};
        const auto length=GetEnvironmentVariableW(L"ProgramData",previous,32768);require(length&&length<32768,"read original ProgramData");original_=previous;
        require(GetTempPathW(32768,temp)>0&&SUCCEEDED(CoCreateGuid(&id))&&StringFromGUID2(id,guidText,40)>0,"private state path");
        directory_=std::wstring(temp)+L"WavoFilter-test-"+guidText;
        require(CreateDirectoryW(directory_.c_str(),nullptr)!=0,"private test directory");
        require(CreateDirectoryW((directory_+L"\\WavoFilter").c_str(),nullptr)!=0,"private settings directory");
        require(SetEnvironmentVariableW(L"ProgramData",directory_.c_str())!=0,"isolate test process environment");
        require(wavo::dataDirectory()==directory_+L"\\WavoFilter","tests did not use private state path");
    }
    void enable(){
        wavo::SharedState state{};state.magic=wavo::kMagic;state.version=wavo::kStateVersion;state.enabled=1;state.graceBlocks=20;state.wetPermille=1000;
        const auto file=CreateFileW((directory_+L"\\WavoFilter\\state.bin").c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        require(file!=INVALID_HANDLE_VALUE,"create isolated controls");DWORD bytes=0;const auto written=WriteFile(file,&state,sizeof(state),&bytes,nullptr);CloseHandle(file);require(written&&bytes==sizeof(state),"write isolated controls");
    }
    ~PrivateState(){
        SetEnvironmentVariableW(L"ProgramData",original_.c_str());
        DeleteFileW((directory_+L"\\WavoFilter\\state.bin").c_str());
        RemoveDirectoryW((directory_+L"\\WavoFilter").c_str());RemoveDirectoryW(directory_.c_str());
    }
};
std::vector<float> signal(unsigned frames,unsigned channels){std::vector<float> samples(frames*channels);uint32_t rng=12345;for(unsigned f=0;f<frames;++f)for(unsigned c=0;c<channels;++c){rng=rng*1664525u+1013904223u;const float noise=((rng>>8)/16777216.0f-0.5f)*0.03f;samples[f*channels+c]=0.15f*std::sin(6.2831853f*(130.0f+c*40)*f/48000)+noise;}return samples;}
std::vector<float> run(const std::vector<float>& input,unsigned channels,const std::vector<unsigned>& sizes,wavo::Settings settings){wavo::Processor processor;require(processor.initialize(channels),"processor init");std::vector<float> out(input.size());unsigned done=0,index=0,frames=input.size()/channels;while(done<frames){const unsigned n=std::min(sizes[index++%sizes.size()],frames-done);processor.process(input.data()+done*channels,out.data()+done*channels,n,false,settings);done+=n;}return out;}
void dspTests(){
    auto input=signal(48000,2);wavo::Settings active{true,0,20,1};
    auto one=run(input,2,{480},active),irregular=run(input,2,{1,200,512,37,960},active);
    require(one==irregular,"streaming changes with callback size");
    require(run(input,2,{200,512},{false,0.85f,20,1})==input,"bypass is not bit-exact");
    require(run(input,2,{200,512},{true,0.85f,20,0})==input,"100% dry bypass is not bit-exact");
    for(float x:one)require(std::isfinite(x),"nonfinite output");
    const auto mixed=run(input,2,{200,512},{true,0,20,0.85f});
    for(unsigned f=0;f<48000;++f)for(unsigned c=0;c<2;++c){const float original=f>=960?input[(f-960)*2+c]:0;require(std::abs(mixed[f*2+c]-(one[f*2+c]*0.85f+original*0.15f))<1e-7f,"dry/wet timing is misaligned");}
    std::vector<float> reference(input.size(),0);std::array<DenoiseState*,2> rn{rnnoise_create(nullptr),rnnoise_create(nullptr)};
    for(auto* p:rn)require(p!=nullptr,"reference model init");
    std::array<float,480> in{},out{};
    for(unsigned offset=0;offset<48000;offset+=480)for(unsigned c=0;c<2;++c){
        for(unsigned f=0;f<480;++f)in[f]=input[(offset+f)*2+c]*32767;
        rnnoise_process_frame(rn[c],out.data(),in.data());
        if(offset+480<48000)for(unsigned f=0;f<480;++f)reference[(offset+480+f)*2+c]=out[f]/32767;
    }
    for(auto* p:rn)rnnoise_destroy(p);
    require(one==reference,"processor differs from direct v1.21 RNNoise");
    wavo::Processor inPlace;require(inPlace.initialize(2),"inplace init");auto same=input;inPlace.process(same.data(),same.data(),48000,false,active);require(same==one,"in-place processing corrupts input");
    wavo::Processor silence;require(silence.initialize(1),"silent init");std::vector<float> zeros(512,1);silence.process(nullptr,zeros.data(),512,true,{false,0,20,1});for(float x:zeros)require(x==0,"silent bypass failed");
    std::cout<<"PASS DSP: bit-exact bypass, direct model equivalence, callback sizes 1/37/200/480/512/960, stereo, in-place, silence, aligned dry/wet mix\n";
    auto performanceInput=signal(48000*10,2);auto begin=std::chrono::steady_clock::now();run(performanceInput,2,{480},active);double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    std::cout<<"BENCHMARK: 10 seconds stereo processed in "<<seconds<<" seconds; real-time factor "<<seconds/10<<" (CPU synthetic workload, not microphone latency)\n";
}
std::vector<float> runRate(const std::vector<float>& input,unsigned channels,unsigned rate,const std::vector<unsigned>& sizes,wavo::Settings settings){
    wavo::RateProcessor processor;require(processor.initialize(channels,rate),"rate processor init");
    std::vector<float> out(input.size());unsigned done=0,index=0,frames=input.size()/channels;
    while(done<frames){const auto n=std::min(sizes[index++%sizes.size()],frames-done);processor.process(input.data()+done*channels,out.data()+done*channels,n,false,settings);require(processor.healthy(),"resampler drift/overflow/failure");done+=n;}
    return out;
}
void rateTests(){
    const auto input=signal(48000,2);require(runRate(input,2,48000,{37,512},{true,0,20,1})==run(input,2,{480},{true,0,20,1}),"48 kHz path changed");
    for(unsigned rate:{8000u,16000u,22050u,44100u,48000u,96000u,192000u}){
        auto samples=signal(rate*2,2);
        for(unsigned f=0;f<rate*2;++f)for(unsigned c=0;c<2;++c)samples[f*2+c]=0.2f*std::sin(6.28318530718*(130+c*40)*f/rate);
        require(runRate(samples,2,rate,{1,37,511},{false,0,20,1})==samples,"resampled bypass changed samples");
        const auto one=runRate(samples,2,rate,{480},{true,0,20,0.01f});
        const auto varied=runRate(samples,2,rate,{1,37,200,512,960},{true,0,20,0.01f});
        for(size_t i=0;i<one.size();++i)require(std::isfinite(one[i])&&std::abs(one[i]-varied[i])<2e-6f,"resampler output depends on callback sizes");
        // A nearly dry filtered path still travels through both resamplers and delays.
        // Verify that a low voice fundamental keeps its frequency and useful amplitude.
        double re=0,im=0;
        for(unsigned f=rate;f<rate*2;++f){const auto angle=6.28318530718*130*f/rate;re+=one[f*2]*std::cos(angle);im+=one[f*2]*std::sin(angle);}
        require(2*std::sqrt(re*re+im*im)/rate>0.15,"voice fundamental damaged by resampling");
        wavo::RateProcessor inplace;require(inplace.initialize(2,rate),"inplace rate init");auto alias=samples;inplace.process(alias.data(),alias.data(),rate*2,false,{true,0,20,0.01f});
        require(inplace.healthy(),"inplace resampler failed");for(size_t i=0;i<one.size();++i)require(std::abs(alias[i]-one[i])<2e-6f,"resampled inplace corruption");
    }
    for(unsigned channels:{1u,4u,8u}){auto samples=signal(4800,channels);const auto out=runRate(samples,channels,44100,{1,512},{true,0,20,1});for(float x:out)require(std::isfinite(x),"multichannel resampling failed");}
    wavo::RateProcessor toggle;require(toggle.initialize(1,44100),"toggle init");auto samples=signal(4410,1);std::vector<float> out(samples.size());
    toggle.process(samples.data(),out.data(),4410,false,{true,0,20,1});toggle.process(samples.data(),out.data(),4410,false,{false,0,20,1});require(out==samples,"toggle did not restore original audio");
    toggle.process(samples.data(),out.data(),4410,false,{true,0,20,1});require(out==runRate(samples,1,44100,{4410},{true,0,20,1}),"toggle did not reset resamplers");
    std::cout<<"PASS rates: 8/16/22.05/44.1/48/96/192 kHz, 1/2/4/8 channels, no drift, callback invariance, voice fundamental, bypass, in-place, toggle\n";
}
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
void apoTests(){
    auto library=LoadLibraryW(L"WavoFilterAPO.dll");require(library!=nullptr,"DLL failed to load");
    using FactoryFn=HRESULT(__stdcall*)(REFCLSID,REFIID,void**);using UnloadFn=HRESULT(__stdcall*)();
    auto factoryFn=reinterpret_cast<FactoryFn>(GetProcAddress(library,"DllGetClassObject"));auto unload=reinterpret_cast<UnloadFn>(GetProcAddress(library,"DllCanUnloadNow"));
    require(factoryFn&&unload,"COM exports missing");require(unload()==S_OK,"new DLL busy");IClassFactory* factory=nullptr;
    require(SUCCEEDED(factoryFn(wavo::kClsid,__uuidof(IClassFactory),reinterpret_cast<void**>(&factory))),"class factory failed");
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
    require(apo->Initialize(0,nullptr)==S_OK,"APO initialize");require(apo->Initialize(0,nullptr)==APOERR_ALREADY_INITIALIZED,"double initialize accepted");
    APO_REG_PROPERTIES* properties=nullptr;require(apo->GetRegistrationProperties(&properties)==S_OK,"registration properties");require(properties->clsid==wavo::kClsid&&properties->u32NumAPOInterfaces==1&&properties->iidAPOInterfaceList[0]==__uuidof(IAudioProcessingObject),"registration incorrect");CoTaskMemFree(properties);
    IAudioProcessingObjectConfiguration* config=nullptr;IAudioProcessingObjectRT* rt=nullptr;IAudioSystemEffects* effects=nullptr;
    require(SUCCEEDED(apo->QueryInterface(__uuidof(IAudioProcessingObjectConfiguration),reinterpret_cast<void**>(&config))),"configuration interface");
    require(SUCCEEDED(apo->QueryInterface(__uuidof(IAudioProcessingObjectRT),reinterpret_cast<void**>(&rt))),"realtime interface");
    require(SUCCEEDED(apo->QueryInterface(__uuidof(IAudioSystemEffects),reinterpret_cast<void**>(&effects))),"system effect interface");effects->Release();
    Media media(2,48000);IAudioMediaType* suggested=nullptr;require(apo->IsInputFormatSupported(nullptr,&media,&suggested)==S_OK&&suggested==&media,"format negotiation");suggested->Release();
    auto input=signal(512,2);std::vector<float> output(input.size(),0);
    APO_CONNECTION_DESCRIPTOR inDesc{APO_CONNECTION_BUFFER_TYPE_EXTERNAL,reinterpret_cast<UINT_PTR>(input.data()),512,&media,APO_CONNECTION_DESCRIPTOR_SIGNATURE};
    APO_CONNECTION_DESCRIPTOR outDesc{APO_CONNECTION_BUFFER_TYPE_EXTERNAL,reinterpret_cast<UINT_PTR>(output.data()),512,&media,APO_CONNECTION_DESCRIPTOR_SIGNATURE};
    auto* ip=&inDesc;auto* op=&outDesc;require(config->LockForProcess(1,&ip,1,&op)==S_OK,"lock failed");require(config->LockForProcess(1,&ip,1,&op)==APOERR_APO_LOCKED,"double lock accepted");
    APO_CONNECTION_PROPERTY in{reinterpret_cast<UINT_PTR>(input.data()),512,BUFFER_VALID,APO_CONNECTION_PROPERTY_SIGNATURE};APO_CONNECTION_PROPERTY out{reinterpret_cast<UINT_PTR>(output.data()),0,BUFFER_INVALID,APO_CONNECTION_PROPERTY_SIGNATURE};auto* i=&in;auto* o=&out;
    rt->APOProcess(1,&i,1,&o);require(out.u32BufferFlags==BUFFER_VALID&&out.u32ValidFrameCount==512,"APO output metadata");
    // On an uninstalled workstation state.bin is absent, which must mean fail-open/bypass.
    wavo::StateMapping privateMapping;
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
int main(){try{CoInitializeEx(nullptr,COINIT_MULTITHREADED);{PrivateState state;dspTests();rateTests();apoTests();state.enable();apoTests();wavo::StateMapping telemetry;require(telemetry.open(),"open private telemetry");require(wavo::read(telemetry.get()->callbacks)==3&&wavo::read(telemetry.get()->producerPid)==static_cast<LONG>(GetCurrentProcessId()),"private APO telemetry");std::cout<<"PASS isolation: missing controls and active controls tested without changing installed microphone state\n";}CoUninitialize();return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}

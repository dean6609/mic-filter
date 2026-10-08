#include <initguid.h>
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
std::vector<float> signal(unsigned frames,unsigned channels){std::vector<float> samples(frames*channels);uint32_t rng=12345;for(unsigned f=0;f<frames;++f)for(unsigned c=0;c<channels;++c){rng=rng*1664525u+1013904223u;const float noise=((rng>>8)/16777216.0f-0.5f)*0.03f;samples[f*channels+c]=0.15f*std::sin(6.2831853f*(130.0f+c*40)*f/48000)+noise;}return samples;}
std::vector<float> run(const std::vector<float>& input,unsigned channels,const std::vector<unsigned>& sizes,micfilter::Settings settings){micfilter::Processor processor;require(processor.initialize(channels),"processor init");std::vector<float> out(input.size());unsigned done=0,index=0,frames=input.size()/channels;while(done<frames){const unsigned n=std::min(sizes[index++%sizes.size()],frames-done);processor.process(input.data()+done*channels,out.data()+done*channels,n,false,settings);done+=n;}return out;}
// Find the delay of the model's own output with a broadband, non-periodic voiced sweep.
// A dry path at any other delay would comb-filter the mix.
void alignmentTest(){
    const unsigned frames=96000;std::vector<float> sweep(frames);double phase=0;
    for(unsigned n=0;n<frames;++n){const double t=n/48000.0;phase+=6.28318530718*(90+60*t+25*std::sin(6.28318530718*5.5*t))/48000;double s=0;for(int h=1;h<25;++h)s+=std::sin(h*phase+h*h)/h;sweep[n]=static_cast<float>(0.08*s);}
    const auto wet=run(sweep,1,{480},{true,0,20,1});
    unsigned best=0;double bestCorrelation=-1;
    for(unsigned lag=0;lag<2400;++lag){double c=0,a=0,b=0;for(unsigned n=24000;n<72000;++n){c+=sweep[n]*wet[n+lag];a+=sweep[n]*sweep[n];b+=wet[n+lag]*wet[n+lag];}c/=std::sqrt(a*b+1e-30);if(c>bestCorrelation){bestCorrelation=c;best=lag;}}
    require(best>=1438&&best<=1442&&bestCorrelation>0.7,"filtered output delay is not the 1440-sample delay used for the original signal");
}
// After voice stops the gate must fade, never jump to zero at a block boundary.
void gateTest(){
    std::vector<float> tone(48000,0);
    for(unsigned n=0;n<24000;++n)tone[n]=0.2f*std::sin(6.2831853f*150*n/48000);
    const auto out=run(tone,1,{480},{true,0.99f,0,1});
    float worst=0;for(unsigned n=26000;n<47999;++n)worst=std::max(worst,std::abs(out[n+1]-out[n]));
    require(worst<0.02f,"gate closes with a click");
}
// Direct RNNoise v1.21 output, aligned the way Processor delivers it (one extra 10 ms block),
// plus the floor gain per output sample, re-derived here from the model's voice probability.
struct Reference {std::vector<float> samples,floor;};
Reference rnnoiseReference(const std::vector<float>& input,unsigned channels){
    const unsigned frames=input.size()/channels;Reference r{std::vector<float>(input.size(),0),std::vector<float>(frames,0)};
    std::array<std::array<float,480>,2> in{},out{};std::array<DenoiseState*,2> rn{};
    for(unsigned c=0;c<channels;++c){rn[c]=rnnoise_create(nullptr);require(rn[c]!=nullptr,"reference model init");}
    unsigned hold=0;float level=0;
    for(unsigned offset=0;offset+480<=frames;offset+=480){
        float probability=0;
        for(unsigned c=0;c<channels;++c){
            for(unsigned f=0;f<480;++f)in[c][f]=input[(offset+f)*channels+c]*32767;
            probability=std::max(probability,rnnoise_process_frame(rn[c],out[c].data(),in[c].data()));
            if(offset+960<=frames)for(unsigned f=0;f<480;++f)r.samples[(offset+480+f)*channels+c]=out[c][f]/32767;
        }
        if(probability>=micfilter::kFloorVoice)hold=micfilter::kFloorHoldBlocks;else if(hold>0)--hold;
        for(unsigned f=0;f<480;++f){
            level=hold>0?std::min(1.0f,level+micfilter::kFloorOpenStep):std::max(0.0f,level-micfilter::kFloorCloseStep);
            if(offset+960<=frames)r.floor[offset+480+f]=level*micfilter::kFloorGain;
        }
    }
    for(unsigned c=0;c<channels;++c)rnnoise_destroy(rn[c]);
    return r;
}
// Around speech the original signal is kept at -20 dB; away from speech the full RNNoise
// reduction returns through a 400 ms fade, never a jump.
void floorTest(){
    const unsigned frames=48000*3,voiced=48000,delay=1440;const float f=micfilter::kFloorGain;
    std::vector<float> input(frames);uint32_t rng=7;float lp=0;double phase=0;
    for(unsigned n=0;n<frames;++n){
        rng=rng*1664525u+1013904223u;const float white=(rng>>8)/16777216.0f-0.5f;lp=0.97f*lp+0.03f*white;
        const double t=n/48000.0;phase+=6.28318530718*(120+15*std::sin(6.28318530718*5*t))/48000;double voice=0;for(int h=1;h<30;++h)voice+=std::sin(h*phase)/h;
        input[n]=0.02f*white+0.3f*lp+(n<voiced&&std::fmod(t,0.5)<0.35?0.12f*static_cast<float>(voice):0.0f);
    }
    const auto out=run(input,1,{480},{true,0,20,1});const auto reference=rnnoiseReference(input,1).samples;
    auto original=[&](unsigned n){return n>=delay?input[n-delay]:0.0f;};
    unsigned withFloor=0;for(unsigned n=9600;n<voiced;++n)if(std::abs(out[n]-(reference[n]*(1-f)+original(n)*f))<1e-6f)++withFloor;
    require(withFloor>(voiced-9600)*9/10,"the -20 dB floor is missing around speech");
    for(unsigned n=voiced+48000;n<frames;++n)require(std::abs(out[n]-reference[n])<1e-6f,"the floor still leaks noise long after speech");
    // Recover the floor gain g from out=reference*(1-g)+original*g; a click would be a jump in g.
    float previous=-1;unsigned previousAt=0;
    for(unsigned n=voiced;n<voiced+48000;++n){const float d=original(n)-reference[n];if(std::abs(d)<0.01f)continue;const float g=(out[n]-reference[n])/d;
        if(previous>=0)require(std::abs(g-previous)<1e-4f+(n-previousAt)*1e-5f,"the floor fades out with a click");previous=g;previousAt=n;}
    double in=0,left=0;for(unsigned n=voiced+48000;n<frames;++n){in+=input[n]*input[n];left+=out[n]*out[n];}
    require(10*std::log10(in/(left+1e-30))>35,"noise reduction away from speech is below 35 dB");
}
// Steady-state gain of a sine through one voice preset, in dB.
double toneGainDb(unsigned preset,double frequency,float amplitude){
    micfilter::VoiceChain chain;std::array<std::array<float,480>,8> block{};double in=0,out=0;
    for(unsigned b=0;b<200;++b){
        for(unsigned i=0;i<480;++i)block[0][i]=amplitude*static_cast<float>(std::sin(6.283185307179586*frequency*(b*480+i)/48000));
        const auto original=block[0];chain.process(block,1,preset);
        if(b>=100)for(unsigned i=0;i<480;++i){in+=original[i]*original[i];out+=block[0][i]*block[0][i];}
    }
    return 10*std::log10(out/in);
}
// Natural is untouched; Clear and Broadcast shape the spectrum; Broadcast evens out loudness;
// nothing exceeds full scale and preset changes crossfade without clicks.
void voiceTest(){
    micfilter::VoiceChain natural;std::array<std::array<float,480>,8> block{};uint32_t rng=3;
    for(auto& channel:block)for(float& x:channel){rng=rng*1664525u+1013904223u;x=(rng>>8)/16777216.0f-0.5f;}
    auto copy=block;natural.process(block,8,0);require(block==copy,"Natural voice preset changes samples");
    require(toneGainDb(1,30,0.05f)<-10,"Clear does not remove rumble");
    const double clear1k=toneGainDb(1,1000,0.05f);
    require(std::abs(clear1k)<1.5,"Clear changes the voice midrange");
    require(toneGainDb(1,3000,0.05f)-clear1k>0.8,"Clear adds no presence");
    require(toneGainDb(1,10000,0.05f)<1.5,"Clear adds excessive brightness");
    require(toneGainDb(1,100,0.05f)>-2.0,"Clear removes too much voice body");
    const double quiet=toneGainDb(2,1000,0.01f),loud=toneGainDb(2,1000,0.3f);
    require(quiet>2&&quiet<4,"Broadcast makeup gain is off");
    require((20*std::log10(0.3)+loud)-(20*std::log10(0.01)+quiet)<26,"Broadcast does not compress");
    require(toneGainDb(3,70,0.02f)>0,"Deep loses a low voice fundamental");
    require(toneGainDb(3,20,0.02f)<-9,"Deep does not remove subsonic rumble");
    require(toneGainDb(3,250,0.02f)<toneGainDb(3,100,0.02f)-1,"Deep does not reduce low-mid mud");
    require(toneGainDb(3,2200,0.02f)>toneGainDb(3,1000,0.02f)+0.4,"Deep loses consonant clarity");
    require(toneGainDb(3,10000,0.02f)<toneGainDb(3,2200,0.02f),"Deep adds excessive brightness");
    require(toneGainDb(3,1000,0.3f)<toneGainDb(3,1000,0.01f)-2,"Deep does not control loud speech");
    micfilter::VoiceChain hot;float peak=0;
    for(unsigned preset=1;preset<4;++preset)for(unsigned b=0;b<100;++b){for(unsigned c=0;c<8;++c)for(unsigned i=0;i<480;++i)block[c][i]=0.99f*static_cast<float>(std::sin(6.283185307179586*4000*(b*480+i)/48000));hot.process(block,8,preset);for(unsigned c=0;c<8;++c)for(unsigned i=0;i<480;++i){require(std::isfinite(block[c][i]),"voice preset produces non-finite output");peak=std::max(peak,std::abs(block[c][i]));require(block[c][i]==block[0][i],"linked processing changes identical channels");}}
    require(peak<=1.0f,"Broadcast exceeds full scale");
    micfilter::VoiceChain switching;float previous=0,worst=0;
    for(unsigned b=0;b<200;++b){
        for(unsigned i=0;i<480;++i)block[0][i]=0.1f*static_cast<float>(std::sin(6.283185307179586*1000*(b*480+i)/48000));
        switching.process(block,1,(b/25)%4);
        for(unsigned i=0;i<480;++i){if(b||i)worst=std::max(worst,std::abs(block[0][i]-previous));previous=block[0][i];}
    }
    require(worst<0.06f,"changing the voice preset clicks");
}
void dspTests(){
    auto input=signal(48000,2);micfilter::Settings active{true,0,20,1};
    auto one=run(input,2,{480},active),irregular=run(input,2,{1,200,512,37,960},active);
    require(one==irregular,"streaming changes with callback size");
    require(run(input,2,{200,512},{false,0.85f,20,1})==input,"bypass is not bit-exact");
    require(run(input,2,{200,512},{true,0.85f,20,0})==input,"100% dry bypass is not bit-exact");
    for(float x:one)require(std::isfinite(x),"nonfinite output");
    // The original signal must use the model's real delay: 10 ms buffering plus 20 ms inside RNNoise.
    const auto expected=rnnoiseReference(input,2);const auto& reference=expected.samples;constexpr unsigned delay=1440;
    auto original=[&](unsigned frame,unsigned c){return frame>=delay?input[(frame-delay)*2+c]:0.0f;};
    for(unsigned n=0;n<48000;++n)for(unsigned c=0;c<2;++c){const float g=expected.floor[n];require(std::abs(one[n*2+c]-(reference[n*2+c]*(1-g)+original(n,c)*g))<1e-6f,"processor differs from direct v1.21 RNNoise plus voice-held floor");}
    const auto mixed=run(input,2,{200,512},{true,0,20,0.85f});
    for(unsigned n=0;n<48000;++n)for(unsigned c=0;c<2;++c){const float g=expected.floor[n],want=(reference[n*2+c]*0.85f+original(n,c)*0.15f)*(1-g)+original(n,c)*g;require(std::abs(mixed[n*2+c]-want)<1e-6f,"dry/wet mix differs from expected");}
    alignmentTest();
    gateTest();
    floorTest();
    voiceTest();
    {micfilter::Settings off{false,0.85f,20,1,2};require(run(input,2,{200,512},off)==input,"bypass with a voice preset is not bit-exact");}
    micfilter::Processor inPlace;require(inPlace.initialize(2),"inplace init");auto same=input;inPlace.process(same.data(),same.data(),48000,false,active);require(same==one,"in-place processing corrupts input");
    micfilter::Processor silence;require(silence.initialize(1),"silent init");std::vector<float> zeros(512,1);silence.process(nullptr,zeros.data(),512,true,{false,0,20,1});for(float x:zeros)require(x==0,"silent bypass failed");
    std::cout<<"PASS DSP: bit-exact bypass, direct model equivalence, callback sizes 1/37/200/480/512/960, stereo, in-place, silence, dry/wet aligned to measured 30 ms model delay, faded gate, -20 dB floor around speech, >35 dB noise reduction away from speech, voice presets (EQ, compression, limiter, click-free changes)\n";
    auto performanceInput=signal(48000*10,2);auto begin=std::chrono::steady_clock::now();run(performanceInput,2,{480},active);double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    std::cout<<"BENCHMARK: 10 seconds stereo processed in "<<seconds<<" seconds; real-time factor "<<seconds/10<<" (CPU synthetic workload, not microphone latency)\n";
}
std::vector<float> runRate(const std::vector<float>& input,unsigned channels,unsigned rate,const std::vector<unsigned>& sizes,micfilter::Settings settings){
    micfilter::RateProcessor processor;require(processor.initialize(channels,rate),"rate processor init");
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
        micfilter::RateProcessor inplace;require(inplace.initialize(2,rate),"inplace rate init");auto alias=samples;inplace.process(alias.data(),alias.data(),rate*2,false,{true,0,20,0.01f});
        require(inplace.healthy(),"inplace resampler failed");for(size_t i=0;i<one.size();++i)require(std::abs(alias[i]-one[i])<2e-6f,"resampled inplace corruption");
    }
    for(unsigned channels:{1u,4u,8u}){auto samples=signal(4800,channels);const auto out=runRate(samples,channels,44100,{1,512},{true,0,20,1});for(float x:out)require(std::isfinite(x),"multichannel resampling failed");}
    micfilter::RateProcessor toggle;require(toggle.initialize(1,44100),"toggle init");auto samples=signal(4410,1);std::vector<float> out(samples.size());
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
    require(properties->u32MajorVersion==0&&properties->u32MinorVersion==6,"APO version differs from installer registration");CoTaskMemFree(properties);
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

#pragma once
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <sstream>
#include <cmath>

// Open and release a shared capture stream without starting it or recording audio.
inline HRESULT checkAudio(const std::wstring& deviceId,std::wstring& report,bool probe=false,bool* compatible=nullptr){
    if(compatible)*compatible=false;
    struct Resources {
        IMMDeviceEnumerator* enumerator=nullptr;
        IMMDevice* device=nullptr;
        IAudioClient* client=nullptr;
        WAVEFORMATEX* format=nullptr;
        IAudioCaptureClient* capture=nullptr;
        ~Resources(){if(capture)capture->Release();if(format)CoTaskMemFree(format);if(client)client->Release();if(device)device->Release();if(enumerator)enumerator->Release();}
    } resource;
    const wchar_t* step=L"Locate microphone";
    HRESULT result=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(&resource.enumerator));
    if(SUCCEEDED(result))result=resource.enumerator->GetDevice(deviceId.c_str(),&resource.device);
    if(SUCCEEDED(result)){step=L"Activate audio client";result=resource.device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(&resource.client));}
    if(SUCCEEDED(result)){step=L"Query engine format";result=resource.client->GetMixFormat(&resource.format);}
    if(SUCCEEDED(result)&&compatible){bool floating=resource.format->wFormatTag==WAVE_FORMAT_IEEE_FLOAT;
        if(resource.format->wFormatTag==WAVE_FORMAT_EXTENSIBLE&&resource.format->cbSize>=22)floating=reinterpret_cast<WAVEFORMATEXTENSIBLE*>(resource.format)->SubFormat.Data1==3;
        *compatible=floating&&resource.format->wBitsPerSample==32&&resource.format->nChannels>=1&&resource.format->nChannels<=8&&resource.format->nSamplesPerSec>=8000&&resource.format->nSamplesPerSec<=192000;}
    if(SUCCEEDED(result)){step=L"Open shared capture";result=resource.client->Initialize(AUDCLNT_SHAREMODE_SHARED,0,1000000,0,resource.format,nullptr);}
    UINT64 framesRead=0,packets=0,silentPackets=0;float peak=0;
    if(probe&&SUCCEEDED(result)){
        step=L"Get capture service";result=resource.client->GetService(__uuidof(IAudioCaptureClient),reinterpret_cast<void**>(&resource.capture));
        if(SUCCEEDED(result)){step=L"Start capture";result=resource.client->Start();}
        if(SUCCEEDED(result)){
            step=L"Read capture counters";
            const auto until=GetTickCount64()+2500;
            while(SUCCEEDED(result)&&GetTickCount64()<until){
                UINT32 available=0;result=resource.capture->GetNextPacketSize(&available);
                for(unsigned batch=0;SUCCEEDED(result)&&available&&batch<100;++batch){
                    BYTE* data=nullptr;UINT32 count=0;DWORD flags=0;result=resource.capture->GetBuffer(&data,&count,&flags,nullptr,nullptr);if(FAILED(result))break;
                    ++packets;framesRead+=count;if(flags&AUDCLNT_BUFFERFLAGS_SILENT)++silentPackets;
                    bool floating=resource.format->wFormatTag==WAVE_FORMAT_IEEE_FLOAT;
                    if(resource.format->wFormatTag==WAVE_FORMAT_EXTENSIBLE&&resource.format->cbSize>=22)floating=reinterpret_cast<WAVEFORMATEXTENSIBLE*>(resource.format)->SubFormat.Data1==3;
                    if(data&&!(flags&AUDCLNT_BUFFERFLAGS_SILENT)&&floating&&resource.format->wBitsPerSample==32){const auto* samples=reinterpret_cast<const float*>(data);for(UINT64 sample=0;sample<static_cast<UINT64>(count)*resource.format->nChannels;++sample)peak=std::max(peak,std::abs(samples[sample]));}
                    result=resource.capture->ReleaseBuffer(count);if(SUCCEEDED(result))result=resource.capture->GetNextPacketSize(&available);
                }
                if(SUCCEEDED(result))Sleep(10);
            }
            resource.client->Stop();
        }
    }
    std::wostringstream output;
    output<<(SUCCEEDED(result)?L"Shared capture: opened successfully.\n":L"Shared capture: could not open.\n")<<L"Step: "<<step<<L"\nHRESULT: 0x"<<std::hex<<static_cast<unsigned long>(result)<<std::dec<<L"\n";
    if(resource.format)output<<L"Windows engine format: "<<resource.format->nSamplesPerSec<<L" Hz / "<<resource.format->nChannels<<L" channels / "<<resource.format->wBitsPerSample<<L" bits\n";
    if(probe)output<<L"Frames="<<framesRead<<L" Packets="<<packets<<L" SilentPackets="<<silentPackets<<L" Peak="<<peak<<L"\nBrief probe: counters and peak level only; audio is not saved.\n";
    else output<<L"Checks capture without starting or recording it; does not confirm effect loading or measure the signal.\n";
    report=output.str();return result;
}

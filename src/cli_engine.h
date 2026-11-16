#pragma once
#include "audio_backend.h"
#include "rate_processor.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace micfilter::cli {
// Microphone -> noise reduction -> virtual microphone, mono at 48 kHz.
// Two devices with their own clocks are joined by a small lock-free ring. All buffers exist
// before audio starts; the callbacks never allocate, lock or touch files.
class Engine {
    static constexpr ma_uint32 kRate=48000,kPeriod=480;
    static constexpr ma_uint32 kRing=19200;     // 400 ms of room
    static constexpr ma_uint32 kPrime=960;      // start playing with 20 ms queued
    static constexpr ma_uint32 kHighWater=4800; // above 100 ms, drop back to the target
    ma_device capture_{},playback_{};
    ma_pcm_rb ring_{};
    bool captureOpen_=false,playbackOpen_=false,ringOpen_=false;
    bool primed_=false; // playback thread only
    std::atomic<bool> stopping_{false};
    RateProcessor processor_;
    std::array<float,1024> scratch_{};

    static void raise(std::atomic<float>& peak,float value) noexcept {if(value>peak.load(std::memory_order_relaxed))peak.store(value,std::memory_order_relaxed);}
    static void onCapture(ma_device* device,void*,const void* input,ma_uint32 frames){
        auto& self=*static_cast<Engine*>(device->pUserData);const auto* source=static_cast<const float*>(input);
        if(!source)return;
        const Settings settings{self.enabled.load(std::memory_order_relaxed),self.silencePermille.load(std::memory_order_relaxed)/1000.0f,20,
            self.wetPermille.load(std::memory_order_relaxed)/1000.0f,self.voice.load(std::memory_order_relaxed)};
        while(frames){
            const ma_uint32 count=frames<self.scratch_.size()?frames:static_cast<ma_uint32>(self.scratch_.size());
            self.processor_.process(source,self.scratch_.data(),count,false,settings);
            float in=0,out=0;
            for(ma_uint32 i=0;i<count;++i){in=std::fmax(in,std::fabs(source[i]));out=std::fmax(out,std::fabs(self.scratch_[i]));}
            raise(self.inputPeak,in);raise(self.outputPeak,out);
            const float* pending=self.scratch_.data();ma_uint32 left=count;
            while(left){
                ma_uint32 chunk=left;void* target=nullptr;
                // A full ring means playback stalled; dropping is better than blocking capture.
                if(ma_pcm_rb_acquire_write(&self.ring_,&chunk,&target)!=MA_SUCCESS||!chunk)break;
                std::memcpy(target,pending,chunk*sizeof(float));ma_pcm_rb_commit_write(&self.ring_,chunk);
                pending+=chunk;left-=chunk;
            }
            self.capturedFrames.fetch_add(count,std::memory_order_relaxed);
            source+=count;frames-=count;
        }
    }
    // The output buffer arrives silent, so anything not written stays silent.
    static void onPlayback(ma_device* device,void* output,const void*,ma_uint32 frames){
        auto& self=*static_cast<Engine*>(device->pUserData);auto* target=static_cast<float*>(output);
        ma_uint32 available=ma_pcm_rb_available_read(&self.ring_);
        if(!self.primed_){if(available<kPrime+frames)return;self.primed_=true;}
        // The microphone clock ran ahead of the output: skip to the target instead of growing delay.
        // Measured beyond this callback's own request, so a large request can never underflow the skip.
        if(available>kHighWater+frames){ma_pcm_rb_seek_read(&self.ring_,available-kPrime-frames);available=kPrime+frames;}
        if(available<frames)self.primed_=false; // ran dry: play what is left, then queue up again
        ma_uint32 left=available<frames?available:frames;
        while(left){
            ma_uint32 chunk=left;void* source=nullptr;
            if(ma_pcm_rb_acquire_read(&self.ring_,&chunk,&source)!=MA_SUCCESS||!chunk)break;
            std::memcpy(target,source,chunk*sizeof(float));ma_pcm_rb_commit_read(&self.ring_,chunk);
            target+=chunk;left-=chunk;
        }
    }
    static void onNotification(const ma_device_notification* notification){
        auto& self=*static_cast<Engine*>(notification->pDevice->pUserData);
        if(notification->type==ma_device_notification_type_stopped&&!self.stopping_.load())self.lost.store(true);
    }
    ma_device_config configuration(ma_device_type type,const ma_device_id& id){
        auto config=ma_device_config_init(type);
        if(type==ma_device_type_capture){config.capture.pDeviceID=&id;config.capture.format=ma_format_f32;config.capture.channels=1;}
        else{config.playback.pDeviceID=&id;config.playback.format=ma_format_f32;config.playback.channels=1;}
        config.sampleRate=kRate;config.periodSizeInFrames=kPeriod;config.pUserData=this;
        config.dataCallback=type==ma_device_type_capture?onCapture:onPlayback;
        config.notificationCallback=onNotification;
        config.pulse.pStreamNameCapture="MicFilter microphone input";
        config.pulse.pStreamNamePlayback="MicFilter filtered voice";
        return config;
    }
public:
    // Controls, written by the terminal and read once per callback.
    std::atomic<bool> enabled{true};
    std::atomic<unsigned> voice{0},silencePermille{0},wetPermille{1000};
    // Meters: the callback raises them, the terminal takes and clears them.
    std::atomic<float> inputPeak{0},outputPeak{0};
    std::atomic<std::uint64_t> capturedFrames{0};
    std::atomic<bool> lost{false};

    Engine()=default;
    ~Engine(){stop();}
    Engine(const Engine&)=delete;
    Engine& operator=(const Engine&)=delete;
    bool start(ma_context& context,const ma_device_id& microphone,const ma_device_id& output,std::string& error){
        stop();stopping_=false;lost=false;primed_=false;capturedFrames=0;inputPeak=0;outputPeak=0;
        if(!processor_.initialize(1,kRate)){error="The noise reduction engine could not start.";return false;}
        if(ma_pcm_rb_init(ma_format_f32,1,kRing,nullptr,nullptr,&ring_)!=MA_SUCCESS){error="Not enough memory for the audio buffer.";return false;}
        ringOpen_=true;
        auto playback=configuration(ma_device_type_playback,output);
        if(ma_device_init(&context,&playback,&playback_)!=MA_SUCCESS){error="The MicFilter virtual microphone could not be opened.";stop();return false;}
        playbackOpen_=true;
        auto capture=configuration(ma_device_type_capture,microphone);
        if(ma_device_init(&context,&capture,&capture_)!=MA_SUCCESS){error="This microphone could not be opened. Check that it is connected and that no app is using it exclusively.";stop();return false;}
        captureOpen_=true;
        if(ma_device_start(&playback_)!=MA_SUCCESS||ma_device_start(&capture_)!=MA_SUCCESS){error="Audio could not start on this microphone.";stop();return false;}
        return true;
    }
    void stop(){
        stopping_=true;
        if(captureOpen_){ma_device_uninit(&capture_);captureOpen_=false;}
        if(playbackOpen_){ma_device_uninit(&playback_);playbackOpen_=false;}
        if(ringOpen_){ma_pcm_rb_uninit(&ring_);ringOpen_=false;}
    }
};
}

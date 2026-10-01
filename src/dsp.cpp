#include "dsp.h"
#include <cmath>
#include <cstring>

namespace micfilter {
Processor::~Processor() {
    for(auto* s:states_) if(s) { VirtualUnlock(s,rnnoise_get_size()); rnnoise_destroy(s); }
    VirtualUnlock(this,sizeof(*this));
}
bool Processor::initialize(unsigned channels) {
    if(channels<1 || channels>8) return false;
    for(auto*& s:states_) { if(s) { VirtualUnlock(s,rnnoise_get_size()); rnnoise_destroy(s); } s=nullptr; }
    channels_=channels;
    for(unsigned c=0;c<channels_;++c) {
        states_[c]=rnnoise_create(nullptr);
        if(!states_[c]) return false;
        VirtualLock(states_[c],rnnoise_get_size());
    }
    VirtualLock(this,sizeof(*this));
    // Warm the network and DSP tables on this non-real-time initialization path.
    std::array<float,480> zero{},warm{};
    for(unsigned c=0;c<channels_;++c) rnnoise_process_frame(states_[c],warm.data(),zero.data());
    reset();
    return true;
}
void Processor::reset() noexcept {
    position_=0; graceLeft_=0; floorHoldLeft_=0; gate_=1.0f; floor_=0.0f; voice_.reset(); previouslyEnabled_=false;
    input_={}; dryPrevious_={}; dryOlder_={}; output_={};
    for(unsigned c=0;c<channels_;++c) if(states_[c]) rnnoise_init(states_[c],nullptr);
}
void Processor::block(const Settings& settings) noexcept {
    float probability=0;
    for(unsigned c=0;c<channels_;++c)
        probability=std::max(probability,rnnoise_process_frame(states_[c],output_[c].data(),input_[c].data()));
    bool voice=probability>=settings.threshold;
    if(voice) graceLeft_=std::max(settings.grace,20u); // v1.21 has this same 200 ms minimum.
    else if(graceLeft_>0) { voice=true; --graceLeft_; }
    const float target=voice ? 1.0f : 0.0f;
    const float step=voice ? kGateOpenStep : kGateCloseStep;
    // The floor only blends in the original signal; the voice itself always comes from RNNoise.
    if(probability>=kFloorVoice) floorHoldLeft_=kFloorHoldBlocks;
    else if(floorHoldLeft_>0) --floorHoldLeft_;
    const float floorTarget=floorHoldLeft_>0 ? 1.0f : 0.0f;
    const float floorStep=floorHoldLeft_>0 ? kFloorOpenStep : kFloorCloseStep;
    for(unsigned i=0;i<480;++i) {
        // One gate envelope for all channels keeps the stereo image stable.
        gate_=gate_<target ? std::min(target,gate_+step) : std::max(target,gate_-step);
        floor_=floor_<floorTarget ? std::min(floorTarget,floor_+floorStep) : std::max(floorTarget,floor_-floorStep);
        const float floorGain=kFloorGain*floor_;
        for(unsigned c=0;c<channels_;++c) {
            const float dry=dryOlder_[c][i];
            const float mixed=output_[c][i]/32767.0f*gate_*settings.wet + dry*(1.0f-settings.wet);
            output_[c][i]=mixed*(1.0f-floorGain) + dry*floorGain;
        }
    }
    voice_.process(output_,channels_,settings.voice);
    dryOlder_=dryPrevious_;
    for(unsigned c=0;c<channels_;++c)
        for(unsigned i=0;i<480;++i) dryPrevious_[c][i]=input_[c][i]/32767.0f;
}
void Processor::process(const float* input,float* output,unsigned frames,bool silent,const Settings& settings) noexcept {
    if(!channels_ || !output) return;
    if(!settings.enabled || settings.wet==0.0f) {
        if(silent || !input) std::memset(output,0,static_cast<size_t>(frames)*channels_*sizeof(float));
        else if(input!=output) std::memcpy(output,input,static_cast<size_t>(frames)*channels_*sizeof(float));
        previouslyEnabled_=false;
        return;
    }
    if(!previouslyEnabled_) { reset(); previouslyEnabled_=true; }
    for(unsigned f=0;f<frames;++f) {
        // Read every channel before overwriting it; supports exact in-place audio buffers.
        for(unsigned c=0;c<channels_;++c) {
            float sample=(silent || !input) ? 0.0f : input[f*channels_+c];
            if(!std::isfinite(sample)) sample=0;
            input_[c][position_]=std::clamp(sample,-1.0f,1.0f)*32767.0f;
            output[f*channels_+c]=output_[c][position_];
        }
        if(++position_==480) { block(settings); position_=0; }
    }
}
}

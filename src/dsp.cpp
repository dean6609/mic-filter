#include "dsp.h"
#include <cmath>
#include <cstring>

namespace wavo {
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
    position_=0; graceLeft_=0; previouslyEnabled_=false;
    input_={}; dryPrevious_={}; output_={};
    for(unsigned c=0;c<channels_;++c) if(states_[c]) rnnoise_init(states_[c],nullptr);
}
void Processor::block(const Settings& settings) noexcept {
    float probability=0;
    for(unsigned c=0;c<channels_;++c)
        probability=std::max(probability,rnnoise_process_frame(states_[c],output_[c].data(),input_[c].data()));
    bool voice=probability>=settings.threshold;
    if(voice) graceLeft_=std::max(settings.grace,20u); // v1.21 has this same 200 ms minimum.
    else if(graceLeft_>0) { voice=true; --graceLeft_; }
    for(unsigned c=0;c<channels_;++c) {
        for(unsigned i=0;i<480;++i) {
            const float wet=voice ? output_[c][i]/32767.0f : 0.0f;
            // RNNoise's overlap/add has one frame of delay. Match dry to that delay.
            output_[c][i]=wet*settings.wet + dryPrevious_[c][i]*(1.0f-settings.wet);
            dryPrevious_[c][i]=input_[c][i]/32767.0f;
        }
    }
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

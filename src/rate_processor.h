#pragma once
#include "dsp.h"
#include <speex_resampler.h>

namespace wavo {
// Resampler tables and all buffers are created before entering the audio callback.
// Keep the proven 48 kHz path unchanged; bypass always preserves the original samples.
class RateProcessor {
    Processor core_;
    SpeexResamplerState* to48_=nullptr;
    SpeexResamplerState* from48_=nullptr;
    unsigned channels_=0,rate_=0,head_=0,count_=0,padding_=0;
    bool enabled_=false,healthy_=true;
    std::array<float,256*8> input_{};
    std::array<float,1552*8> converted_{};
    std::array<float,1552*8> filtered_{};
    std::array<float,272*8> returned_{};
    std::array<float,2048*8> queue_{};
public:
    ~RateProcessor();
    bool initialize(unsigned channels,unsigned rate);
    void reset() noexcept;
    bool healthy() const noexcept {return healthy_;}
    LONG64 latency() const noexcept;
    void process(const float* input,float* output,unsigned frames,bool silent,const Settings& settings) noexcept;
};
}

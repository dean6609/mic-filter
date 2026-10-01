#pragma once
#include "shared.h"
#include <array>
#include <rnnoise.h>

namespace wavo {
// The model and rnnoise_process_frame are unchanged from Werman's v1.21.
// This streaming adapter accepts arbitrary Windows callback sizes using fixed buffers.
// Default gate values match v1.21 (85%, 200 ms, zero retroactive grace, 100% wet).
class Processor {
    unsigned channels_=0;
    unsigned position_=0;
    unsigned graceLeft_=0;
    bool previouslyEnabled_=false;
    std::array<DenoiseState*,8> states_{};
    std::array<std::array<float,480>,8> input_{};
    std::array<std::array<float,480>,8> dryPrevious_{};
    std::array<std::array<float,480>,8> output_{};
    void block(const Settings& settings) noexcept;
public:
    Processor()=default;
    ~Processor();
    Processor(const Processor&)=delete;
    Processor& operator=(const Processor&)=delete;
    bool initialize(unsigned channels);
    void reset() noexcept;
    void process(const float* input,float* output,unsigned frames,bool silent,const Settings& settings) noexcept;
};
}

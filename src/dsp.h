#pragma once
#include "shared.h"
#include "voice.h"
#include <array>
#include <rnnoise.h>

namespace micfilter {
// The model and rnnoise_process_frame are unchanged from Werman's v1.21.
// This streaming adapter accepts arbitrary Windows callback sizes using fixed buffers.
// Default gate values match v1.21 (85%, 200 ms, zero retroactive grace, 100% wet).
// Unlike v1.21, the gate fades instead of switching at block edges, and around speech the
// original signal is kept at -20 dB so word endings never fall into digital silence.
// Away from speech that floor fades out, leaving RNNoise's full noise reduction.
inline constexpr float kFloorGain=0.1f;          // -20 dB original signal around speech
inline constexpr float kFloorVoice=0.6f;         // RNNoise voice probability that holds the floor
inline constexpr unsigned kFloorHoldBlocks=30;   // 300 ms after the last voiced block
inline constexpr float kFloorOpenStep=1.0f/480;    // 10 ms fade in at 48 kHz
inline constexpr float kFloorCloseStep=1.0f/19200; // 400 ms fade out at 48 kHz
inline constexpr float kGateOpenStep=1.0f/240;   // 5 ms fade in at 48 kHz
inline constexpr float kGateCloseStep=1.0f/2400; // 50 ms fade out at 48 kHz
class Processor {
    unsigned channels_=0;
    unsigned position_=0;
    unsigned graceLeft_=0;
    unsigned floorHoldLeft_=0;
    float gate_=1.0f;
    float floor_=0.0f;
    bool previouslyEnabled_=false;
    std::array<DenoiseState*,8> states_{};
    std::array<std::array<float,480>,8> input_{};
    // RNNoise v1.21 outputs the frame from two calls earlier (overlap/add plus delayed_X).
    std::array<std::array<float,480>,8> dryPrevious_{};
    std::array<std::array<float,480>,8> dryOlder_{};
    std::array<std::array<float,480>,8> output_{};
    VoiceChain voice_;
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

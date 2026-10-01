#pragma once
#include <array>

namespace micfilter {
// Optional voice polish applied after noise suppression, at 48 kHz.
// 0 Natural: samples pass through untouched.
// 1 Clear: rumble filter, less low-mid boxiness, more presence and air.
// 2 Broadcast: stronger presence and air, slightly fuller lows than Clear, plus a gentle
//   linked compressor (3:1 above -24 dBFS, +6 dB makeup).
// Both polished presets end in a soft limiter. Changing presets crossfades over 10 ms.
class VoiceChain {
public:
    static constexpr unsigned kStages=5;
    struct Biquad {float b0=1,b1=0,b2=0,a1=0,a2=0;};
    struct Preset {std::array<Biquad,kStages> eq{};bool compress=false;};
    VoiceChain();
    void reset() noexcept;
    // Processes one 480-sample block for each channel in place.
    void process(std::array<std::array<float,480>,8>& block,unsigned channels,unsigned preset) noexcept;
private:
    std::array<Preset,3> presets_{};
    std::array<std::array<std::array<float,2>,kStages>,8> history_{};
    unsigned current_=0;
    float blend_=1.0f;
    float envelope_=0.0f;
    float attack_=0.0f,release_=0.0f;
    float polish(unsigned channel,float x) noexcept;
};
}

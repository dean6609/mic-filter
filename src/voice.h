#pragma once
#include <array>

namespace micfilter {
// Optional voice polish applied after noise suppression, at 48 kHz.
// 0 Natural: samples pass through untouched.
// 1 Clear: gentle rumble/boxiness removal and restrained presence.
// 2 Broadcast: warm, even speech with moderate linked compression.
// 3 Deep: preserves low fundamentals, reduces mud and keeps consonants audible.
// Polished presets end in a soft limiter. Changes fade out and in over 10 ms each.
class VoiceChain {
public:
    static constexpr unsigned kStages=5;
    struct Biquad {float b0=1,b1=0,b2=0,a1=0,a2=0;};
    struct Preset {
        std::array<Biquad,kStages> eq{};
        bool compress=false;
        float threshold=-20,ratio=2,makeup=3,attack=0,release=0;
    };
    VoiceChain();
    void reset() noexcept;
    // Processes one 480-sample block for each channel in place.
    void process(std::array<std::array<float,480>,8>& block,unsigned channels,unsigned preset) noexcept;
private:
    std::array<Preset,4> presets_{};
    std::array<std::array<std::array<float,2>,kStages>,8> history_{};
    unsigned current_=0;
    float blend_=1.0f;
    float envelope_=0.0f;
    float polish(unsigned channel,float x) noexcept;
};
}

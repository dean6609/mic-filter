#include "voice.h"
#include <algorithm>
#include <cmath>

namespace micfilter {
namespace {
constexpr double kRate=48000;
constexpr double kPi=3.14159265358979323846;
constexpr float kBlendStep=1.0f/480;      // 10 ms crossfade when the preset changes
constexpr float kKnee=0.9f;               // soft limiter starts at -0.9 dBFS
// Coefficients from the RBJ Audio EQ Cookbook, normalized by a0.
VoiceChain::Biquad normalized(double b0,double b1,double b2,double a0,double a1,double a2){
    return {static_cast<float>(b0/a0),static_cast<float>(b1/a0),static_cast<float>(b2/a0),static_cast<float>(a1/a0),static_cast<float>(a2/a0)};
}
VoiceChain::Biquad highPass(double f,double q){
    const double w=2*kPi*f/kRate,c=std::cos(w),alpha=std::sin(w)/(2*q);
    return normalized((1+c)/2,-(1+c),(1+c)/2,1+alpha,-2*c,1-alpha);
}
VoiceChain::Biquad peak(double f,double q,double db){
    const double a=std::pow(10,db/40),w=2*kPi*f/kRate,c=std::cos(w),alpha=std::sin(w)/(2*q);
    return normalized(1+alpha*a,-2*c,1-alpha*a,1+alpha/a,-2*c,1-alpha/a);
}
VoiceChain::Biquad shelf(double f,double db,bool high){
    const double a=std::pow(10,db/40),w=2*kPi*f/kRate,c=std::cos(w),s=std::sin(w);
    const double beta=2*std::sqrt(a)*s/2*std::sqrt(2.0); // shelf slope S=1
    const double sign=high?1:-1;
    return normalized(a*((a+1)+sign*(a-1)*c+beta),-sign*2*a*((a-1)+sign*(a+1)*c),a*((a+1)+sign*(a-1)*c-beta),
                      (a+1)-sign*(a-1)*c+beta,sign*2*((a-1)-sign*(a+1)*c),(a+1)-sign*(a-1)*c-beta);
}
float softLimit(float x){
    const float a=std::fabs(x);
    if(a<=kKnee)return x;
    return std::copysign(kKnee+(1-kKnee)*std::tanh((a-kKnee)/(1-kKnee)),x);
}
}

VoiceChain::VoiceChain(){
    presets_[1].eq={highPass(65,0.707),peak(280,0.9,-1.5),peak(3000,0.8,1.8),shelf(9500,0.5,true),Biquad{}};
    presets_[2].eq={highPass(60,0.707),shelf(140,0.8,false),peak(320,0.9,-1.8),peak(2800,0.8,1.5),shelf(9500,0.5,true)};
    presets_[2].compress=true;
    presets_[3].eq={highPass(40,0.707),shelf(130,1.2,false),peak(250,0.9,-2.0),peak(2200,0.8,1.2),shelf(6500,-1.0,true)};
    presets_[3].compress=true;
    presets_[3].threshold=-18;presets_[3].ratio=1.6f;presets_[3].makeup=1.5f;
    for(unsigned i=2;i<4;++i){
        presets_[i].attack=std::exp(-1.0f/(0.012f*48000));
        presets_[i].release=std::exp(-1.0f/(0.200f*48000));
    }
}
void VoiceChain::reset() noexcept {history_={};envelope_=0;}
float VoiceChain::polish(unsigned channel,float x) noexcept {
    for(unsigned s=0;s<kStages;++s){
        const auto& q=presets_[current_].eq[s];auto& z=history_[channel][s];
        const float y=q.b0*x+z[0];
        z[0]=q.b1*x-q.a1*y+z[1];
        z[1]=q.b2*x-q.a2*y;
        x=y;
    }
    return x;
}
void VoiceChain::process(std::array<std::array<float,480>,8>& block,unsigned channels,unsigned preset) noexcept {
    preset=std::min(preset,3u);
    for(unsigned i=0;i<480;++i){
        if(preset!=current_){
            blend_=std::max(0.0f,blend_-kBlendStep);
            if(blend_==0){current_=preset;reset();}
        }else blend_=std::min(1.0f,blend_+kBlendStep);
        if(current_==0)continue; // Natural: untouched
        std::array<float,8> polished{};float level=0;
        for(unsigned c=0;c<channels;++c){polished[c]=polish(c,block[c][i]);level=std::max(level,std::fabs(polished[c]));}
        float gain=1;
        if(presets_[current_].compress){
            const auto& p=presets_[current_];
            // One envelope for all channels keeps the stereo image stable.
            envelope_=level>envelope_?p.attack*envelope_+(1-p.attack)*level:p.release*envelope_+(1-p.release)*level;
            const float over=20*std::log10(envelope_+1e-9f)-p.threshold;
            // 6 dB soft knee avoids an abrupt change in gain around normal speech levels.
            const float reduction=over<=-3?0:over>=3?over*(1-1/p.ratio):(over+3)*(over+3)/12*(1-1/p.ratio);
            gain=std::pow(10.0f,(p.makeup-reduction)/20);
        }
        for(unsigned c=0;c<channels;++c){
            const float x=block[c][i];
            block[c][i]=x+blend_*(softLimit(polished[c]*gain)-x);
        }
    }
}
}

#include "rate_processor.h"
#include <cmath>
#include <cstring>

namespace micfilter {
RateProcessor::~RateProcessor(){if(to48_)speex_resampler_destroy(to48_);if(from48_)speex_resampler_destroy(from48_);}
bool RateProcessor::initialize(unsigned channels,unsigned rate){
    if(channels<1||channels>8||rate<8000||rate>192000)return false;
    if(to48_)speex_resampler_destroy(to48_);if(from48_)speex_resampler_destroy(from48_);
    to48_=from48_=nullptr;channels_=channels;rate_=rate;
    if(!core_.initialize(channels))return false;
    if(rate!=48000){
        int error=0;to48_=speex_resampler_init(channels,rate,48000,5,&error);
        if(!to48_||error!=RESAMPLER_ERR_SUCCESS)return false;
        from48_=speex_resampler_init(channels,48000,rate,5,&error);
        if(!from48_||error!=RESAMPLER_ERR_SUCCESS)return false;
    }
    padding_=(rate+47999)/48000+2;reset();return true;
}
void RateProcessor::reset() noexcept {
    core_.reset();if(to48_)speex_resampler_reset_mem(to48_);if(from48_)speex_resampler_reset_mem(from48_);
    head_=0;count_=padding_;queue_={};enabled_=false;healthy_=true;
}
LONG64 RateProcessor::latency() const noexcept {
    double seconds=0.020;
    if(to48_&&from48_)seconds+=static_cast<double>(speex_resampler_get_input_latency(to48_)+speex_resampler_get_output_latency(from48_)+padding_)/rate_;
    return static_cast<LONG64>(std::ceil(seconds*10000000.0));
}
void RateProcessor::process(const float* src,float* dst,unsigned frames,bool silent,const Settings& settings) noexcept {
    if(!channels_||!dst)return;
    if(!settings.enabled||settings.wet==0||!healthy_){
        if(silent||!src)std::memset(dst,0,static_cast<size_t>(frames)*channels_*sizeof(float));
        else if(src!=dst)std::memcpy(dst,src,static_cast<size_t>(frames)*channels_*sizeof(float));
        enabled_=false;return;
    }
    if(rate_==48000){core_.process(src,dst,frames,silent,settings);return;}
    if(!enabled_){reset();enabled_=true;}
    unsigned offset=0;
    while(offset<frames){
        const unsigned n=std::min(256u,frames-offset);
        for(unsigned i=0;i<n*channels_;++i){const float x=(silent||!src)?0.0f:src[offset*channels_+i];input_[i]=std::isfinite(x)?std::clamp(x,-1.0f,1.0f):0.0f;}
        spx_uint32_t consumed=n,converted=1552;
        const auto first=speex_resampler_process_interleaved_float(to48_,input_.data(),&consumed,converted_.data(),&converted);
        core_.process(converted_.data(),filtered_.data(),converted,false,settings);
        spx_uint32_t used=converted,returned=272;
        const auto second=speex_resampler_process_interleaved_float(from48_,filtered_.data(),&used,returned_.data(),&returned);
        if(first!=RESAMPLER_ERR_SUCCESS||second!=RESAMPLER_ERR_SUCCESS||consumed!=n||used!=converted||count_+returned>2048||count_+returned<n){
            // An unexpected resampler failure preserves audio rather than dropping the stream.
            healthy_=false;
            std::memcpy(dst+offset*channels_,input_.data(),static_cast<size_t>(n)*channels_*sizeof(float));
            offset+=n;if(offset<frames)process(src?src+offset*channels_:nullptr,dst+offset*channels_,frames-offset,silent,settings);return;
        }
        for(unsigned f=0;f<returned;++f){const unsigned tail=(head_+count_+f)%2048;std::memcpy(queue_.data()+tail*channels_,returned_.data()+f*channels_,channels_*sizeof(float));}
        count_+=returned;
        for(unsigned f=0;f<n;++f){std::memcpy(dst+(offset+f)*channels_,queue_.data()+head_*channels_,channels_*sizeof(float));head_=(head_+1)%2048;--count_;}
        offset+=n;
    }
}
}

#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace rdj {
inline float safeParam(float v,float lo,float hi,float fallback) { return std::isfinite(v)?std::clamp(v,lo,hi):fallback; }
struct PadRegion {
    double begin=0,end=0;
    bool valid() const { return end-begin>=2; }
    double first(bool reverse) const { return reverse?end-1:begin; }
    double wrap(double p) const { const double len=end-begin; return begin+((p-begin)-std::floor((p-begin)/len)*len); }
};
inline PadRegion padRegion(int64_t frames,float start,float end) {
    if(frames<2) return {};
    const double a=std::floor(safeParam(start,0,1,0)*frames);
    const double b=std::ceil(safeParam(end,0,1,1)*frames);
    const double begin=std::min(a,double(frames-2));
    return {begin,std::clamp(b,begin+2.,double(frames))};
}
class CueMetronome {
public:
    void prepare(double rate) { fs_=rate; reset(); }
    void reset() { env_=phase_=0; }
    void add(float* L,float* R,int n,double b0,double b1,bool on,float level) {
        if(!on || !std::isfinite(b0) || !std::isfinite(b1) || b1<=b0 || b1-b0>1) {reset();return;}
        const double delta=(b1-b0)/n;
        const float gain=safeParam(level,0,.5f,.15f);
        const float decay=float(std::exp(-1./(.006*fs_)));
        for(int i=0;i<n;++i) {
            const double prev=b0+delta*i,beat=prev+delta;
            if(std::floor(beat)!=std::floor(prev)) { env_=1;phase_=0;hz_=(int64_t(std::floor(beat))%4==0)?1500:1000; }
            const float click=float(std::sin(phase_)*env_*gain);
            L[i]+=click;R[i]+=click;
            phase_+=6.283185307179586*hz_/fs_;
            if(phase_>6.283185307179586)phase_-=6.283185307179586;
            env_*=decay;
            if(env_<.00001)env_=0;
        }
    }
private:
    double fs_=48000,phase_=0,hz_=1000,env_=0;
};
} // namespace rdj

#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include "pad_tools.h"
namespace rdj {
using RhythmPattern=std::array<std::array<uint8_t,32>,8>;
inline RhythmPattern generateRhythm(uint32_t seed,float density,int steps) {
    RhythmPattern result{};steps=steps==32?32:16;
    const float d=safeParam(density,0,1,.5f);if(d<=0)return result;
    uint32_t rng=seed?seed:1;
    auto random=[&](){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return float(rng>>8)/16777216.f;};
    for(int t=0;t<8;++t)for(int s=0;s<steps;++s) {
        float chance=d*.12f;
        if(t==0)chance=s%4==0?std::min(1.f,d*2.5f):d*.08f;
        if(t==1 || t==2)chance=s%8==4?d*.9f:d*.04f;
        if(t==3)chance=s%2==0?std::min(1.f,d*1.8f):d*.5f;
        if(t==4)chance=s%4==2?d*.7f:d*.02f;
        if(t>=5)chance=d*(s%16>=12?.45f:.05f);
        if(random()<chance)result[t][s]=(s%4==0)?2:1;
    }
    return result;
}
inline RhythmPattern rotateRhythm(const RhythmPattern& source,int steps,int shift) {
    steps=steps==32?32:16;RhythmPattern result=source;
    shift=((shift%steps)+steps)%steps;
    for(int t=0;t<8;++t)for(int s=0;s<steps;++s)result[t][(s+shift)%steps]=source[t][s];
    return result;
}
} // namespace rdj

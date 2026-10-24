#pragma once
#include "track.h"
#include <algorithm>
#include <cmath>
#include <vector>
namespace rdj {
inline std::vector<TrackPtr> sliceSample(const Track& source, unsigned count) {
    std::vector<TrackPtr> result;
    if (!source.sampleRate || count<2 || count>16 || source.frames<count*16 || source.frames>source.pcm.size()/2) return result;
    for (unsigned k=0;k<count;++k) {
        const uint64_t begin=source.frames*k/count, end=source.frames*(k+1)/count;
        auto t=std::make_shared<Track>();
        t->sampleRate=source.sampleRate; t->frames=end-begin;
        t->pcm.assign(source.pcm.begin()+begin*2,source.pcm.begin()+end*2);
        const uint64_t edge=std::max<uint64_t>(1,std::min<uint64_t>(source.sampleRate*3/1000,t->frames/4));
        for (uint64_t i=0;i<t->frames;++i) {
            const float g=std::min(1.f,float(std::min(i,t->frames-1-i))/edge);
            t->pcm[i*2]*=g; t->pcm[i*2+1]*=g;
            t->maxPeak=std::max(t->maxPeak,std::max(std::fabs(t->pcm[i*2]),std::fabs(t->pcm[i*2+1])));
        }
        t->meta.durationSec=t->lengthSec(); result.push_back(t);
    }
    return result;
}
} // namespace rdj

#pragma once
#include "track.h"
#include "pad_tools.h"
#include <array>
#include <cmath>
namespace rdj {
struct DrumControl {
    float attackMs=.5f,holdMs=0,decayMs=600,pitchSemis=0,pitchDepth=0,pitchDecayMs=50;
    float cutoff=18000,drive=0,pan=0;
    int choke=0;
};
class SampleDrums {
public:
    void prepare(double fs) {fs_=fs;for(auto& s:slots_){s.on=false;s.age=0;s.lpL=s.lpR=0;}}
    bool has(int v)const{return v>=0&&v<8&&slots_[v].t;}
    bool any()const{for(const auto& s:slots_)if(s.on)return true;return false;}
    void set(int v,const Track* t) {
        if(v<0||v>=8)return;
        slots_[v]={};
        if(t&&t->sampleRate>0&&t->frames>=2&&t->frames<=t->pcm.size()/2)slots_[v].t=t;
    }
    void trigger(int v,float velocity,DrumControl c) {
        if(!has(v))return;
        c.attackMs=safeParam(c.attackMs,.2f,200,.5f);c.holdMs=safeParam(c.holdMs,0,500,0);
        c.decayMs=safeParam(c.decayMs,5,5000,600);c.pitchSemis=safeParam(c.pitchSemis,-12,12,0);
        c.pitchDepth=safeParam(c.pitchDepth,-24,24,0);c.pitchDecayMs=safeParam(c.pitchDecayMs,2,1000,50);
        c.cutoff=safeParam(c.cutoff,100,20000,18000);c.drive=safeParam(c.drive,0,1,0);c.pan=safeParam(c.pan,-1,1,0);c.choke=std::clamp(c.choke,0,4);
        if(c.choke)for(int j=0;j<8;++j)if(j!=v&&slots_[j].on&&slots_[j].c.choke==c.choke)slots_[j].choking=true;
        auto& s=slots_[v];s.on=true;s.pos=0;s.age=0;s.vel=safeParam(velocity,0,1,1);s.c=c;s.lpL=s.lpR=0;s.choking=false;s.chokeGain=1;
    }
    void render(float* L,float* R,int n,const float* level,const bool* mute) {
        for(int v=0;v<8;++v) {
            auto& s=slots_[v];if(!s.on||!s.t)continue;
            const auto& c=s.c;
            const double attack=c.attackMs*.001*fs_,hold=c.holdMs*.001*fs_,decay=c.decayMs*.001*fs_;
            const float lp=float(1-std::exp(-6.283185307179586*std::min(double(c.cutoff),fs_*.45)/fs_));
            const double pitchFall=std::exp(-1./(c.pitchDecayMs*.001*fs_));
            double pitchEnv=std::pow(pitchFall,double(s.age));
            const float g=mute[v]?0:safeParam(level[v],0,1,.8f)*safeParam(level[v],0,1,.8f)*s.vel;
            const float gl=c.pan>0?1-c.pan:1,gr=c.pan<0?1+c.pan:1;
            const float drive=1+c.drive*12,normal=std::tanh(drive);
            for(int i=0;i<n;++i) {
                if(s.pos>=s.t->frames-1||s.age>attack+hold+decay*1.4){s.on=false;break;}
                float env=s.age<attack?float(s.age/attack):s.age<attack+hold?1.f:float(std::exp(-6.907755278982137*(s.age-attack-hold)/decay));
                if(s.choking){s.chokeGain=std::max(0.f,s.chokeGain-float(1./(.005*fs_)));env*=s.chokeGain;if(s.chokeGain<=0){s.on=false;break;}}
                const uint64_t at=uint64_t(s.pos);const float fr=float(s.pos-at);
                float l=s.t->pcm[at*2]+(s.t->pcm[at*2+2]-s.t->pcm[at*2])*fr;
                float r=s.t->pcm[at*2+1]+(s.t->pcm[at*2+3]-s.t->pcm[at*2+1])*fr;
                l=std::isfinite(l)?std::clamp(l,-8.f,8.f):0;r=std::isfinite(r)?std::clamp(r,-8.f,8.f):0;
                if(c.drive>.0001f){l=std::tanh(l*drive)/normal;r=std::tanh(r*drive)/normal;}
                s.lpL+=lp*(l-s.lpL);s.lpR+=lp*(r-s.lpR);
                // Natural sample end also fades, including when pitched upwards.
                const float edge=float(std::clamp((double(s.t->frames)-1-s.pos)/(.002*s.t->sampleRate),0.,1.));
                L[i]+=s.lpL*g*env*edge*gl;R[i]+=s.lpR*g*env*edge*gr;
                s.pos+=double(s.t->sampleRate)/fs_*std::pow(2.,(c.pitchSemis+c.pitchDepth*pitchEnv)/12.);
                pitchEnv*=pitchFall;++s.age;
            }
        }
    }
private:
    struct Slot {const Track* t=nullptr;DrumControl c{};bool on=false,choking=false;double pos=0;uint64_t age=0;float vel=1,lpL=0,lpR=0,chokeGain=1;};
    std::array<Slot,8> slots_{};double fs_=48000;
};
} // namespace rdj

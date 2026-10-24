#include "audio/drum_banks.h"
#include "audio/engine.h"
#include <set>
#include <bit>
#include <iostream>
#include <stdexcept>
#include <atomic>
#include <cstdlib>
#include <new>
#include <limits>
using namespace rdj;
static std::atomic<bool> watch{false};static std::atomic<int> allocs{0};
void* operator new(std::size_t n){if(watch)++allocs;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}void* operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
int checks=0;void check(bool ok,const char* msg){++checks;if(!ok)throw std::runtime_error(msg);}
Track tone(float hz=220,int channel=-1){Track t;t.sampleRate=48000;t.frames=48000;t.pcm.resize(t.frames*2);for(uint64_t i=0;i<t.frames;++i){const float value=.4f*std::sin(i*6.283185307179586*hz/48000);t.pcm[i*2]=channel==1?0:value;t.pcm[i*2+1]=channel==0?0:value;}return t;}
float level[8]={1,1,1,1,1,1,1,1};bool mute[8]={};
double energy(SampleDrums& s,int frames,int channel=-1){double sum=0;while(frames){float L[256]={},R[256]={};int n=std::min(frames,256);watch=true;s.render(L,R,n,level,mute);watch=false;for(int i=0;i<n;++i){check(std::isfinite(L[i])&&std::isfinite(R[i]),"finite sampled drum");if(channel!=1)sum+=L[i]*L[i];if(channel!=0)sum+=R[i]*R[i];}frames-=n;}return sum;}
int main(){try{
    std::set<uint64_t> hashes;
    for(int k=0;k<24;++k)for(int v=0;v<8;++v){auto t=makeFactoryDrum(k,v);check(bool(t),"factory sound available");uint64_t hash=1469598103934665603ull;double power=0;
        for(float x:t->pcm){check(std::isfinite(x)&&std::fabs(x)<=.801f,"factory sound finite and normalized");power+=x*x;hash^=std::bit_cast<uint32_t>(x);hash*=1099511628211ull;}
        check(power>.01,"factory sound audible");hashes.insert(hash);
    }
    check(hashes.size()==192,"all 192 factory sounds differ");check(!makeFactoryDrum(-1,0)&&!makeFactoryDrum(24,0)&&!makeFactoryDrum(0,8)&&!makeFactoryDrum(0,0,0),"invalid factory indices rejected");
    auto t=tone();DrumControl c;c.decayMs=1000;
    {SampleDrums fast,slow;fast.prepare(48000);slow.prepare(48000);fast.set(0,&t);slow.set(0,&t);auto soft=c;soft.attackMs=100;fast.trigger(0,1,c);slow.trigger(0,1,soft);check(energy(slow,1000)<energy(fast,1000)*.2,"attack changes transient");}
    {SampleDrums shortD,longD;shortD.prepare(48000);longD.prepare(48000);shortD.set(0,&t);longD.set(0,&t);auto quick=c;quick.decayMs=10;shortD.trigger(0,1,quick);longD.trigger(0,1,c);energy(shortD,4800);energy(longD,4800);check(!shortD.any()&&longD.any(),"decay controls tail");}
    {SampleDrums held;held.prepare(48000);held.set(0,&t);auto h=c;h.holdMs=100;h.decayMs=5;held.trigger(0,1,h);energy(held,2400);check(held.any(),"hold keeps envelope open");}
    {SampleDrums low,bright;low.prepare(48000);bright.prepare(48000);auto hi=tone(6000);low.set(0,&hi);bright.set(0,&hi);auto dark=c;dark.cutoff=100;low.trigger(0,1,dark);bright.trigger(0,1,c);check(energy(low,2000)<energy(bright,2000)*.02,"filter changes spectrum");}
    {SampleDrums pan;pan.prepare(48000);pan.set(0,&t);auto p=c;p.pan=-1;pan.trigger(0,1,p);float L[256]={},R[256]={};pan.render(L,R,256,level,mute);double left=0;for(int i=0;i<256;++i){check(R[i]==0,"pan isolates right");left+=std::fabs(L[i]);}check(left>1,"pan preserves left");}
    {SampleDrums flat,bend;flat.prepare(48000);bend.prepare(48000);flat.set(0,&t);bend.set(0,&t);auto p=c;p.pitchDepth=12;p.pitchDecayMs=100;flat.trigger(0,1,c);bend.trigger(0,1,p);float L[256]={},R[256]={},A[256]={},B[256]={};flat.render(L,R,256,level,mute);bend.render(A,B,256,level,mute);double diff=0;for(int i=0;i<256;++i)diff+=std::fabs(L[i]-A[i]);check(diff>1,"pitch envelope changes sound");}
    {SampleDrums normal,driven;normal.prepare(48000);driven.prepare(48000);normal.set(0,&t);driven.set(0,&t);auto p=c;p.drive=1;normal.trigger(0,1,c);driven.trigger(0,1,p);check(std::fabs(energy(normal,2000)-energy(driven,2000))>1,"saturation changes sound");}
    {auto left=tone(220,0),right=tone(330,1);SampleDrums s;s.prepare(48000);s.set(0,&left);s.set(1,&right);auto group=c;group.choke=1;s.trigger(0,1,group);energy(s,1000);s.trigger(1,1,group);energy(s,512);float L[256]={},R[256]={};s.render(L,R,256,level,mute);double rightPower=0;for(int i=0;i<256;++i){check(L[i]==0,"choke silences old drum");rightPower+=std::fabs(R[i]);}check(rightPower>1,"new drum continues");}
    {SampleDrums s;s.prepare(48000);Track invalid;invalid.frames=100;invalid.sampleRate=48000;s.set(0,&invalid);check(!s.has(0),"invalid PCM rejected");s.set(0,&t);auto bad=c;bad.attackMs=std::numeric_limits<float>::quiet_NaN();bad.cutoff=std::numeric_limits<float>::infinity();s.trigger(0,1,bad);energy(s,1000);s.set(0,nullptr);check(!s.any()&&!s.has(0),"sample replacement clears old playback");}
    {auto left=tone(220,0),right=tone(330,1);Instruments x;x.prepare(48000);x.setDrumSample(0,&left,10);x.setDrumSample(1,&right,11);x.params.dSolo[0]=true;x.triggerDrum(0,1);x.triggerDrum(1,1);RackCtx ctx;float L[256],R[256];watch=true;x.render(L,R,256,ctx);watch=false;double power=0;for(int i=0;i<256;++i){check(R[i]==0,"solo isolates sampled voice");power+=std::fabs(L[i]);}check(power>0,"solo voice audible");check(x.status.drumSampleGen[0]==10&&x.status.drumSampleGen[1]==11,"sample load acknowledged");}
    {auto e=std::make_unique<Engine>();Command load{Cmd::DrumSample};load.i=0;load.track=&t;load.gen=99;e->send(load);Command hit{Cmd::DrumHit};hit.i=0;hit.d=1;e->send(hit);float out[512];watch=true;e->process(out,256,2);watch=false;double sum=0;for(float value:out)sum+=std::fabs(value);check(sum>0,"manual drum hit reaches master");check(e->rack.instr.status.drumSampleGen[0]==99,"engine acknowledges sample command");}
    check(allocs==0,"sampled drum audio paths allocate no memory");std::cout<<"PASS "<<checks<<" checks: 24 kits / 192 distinct sounds, envelopes, color, choke, solo, commands and no audio allocations\n";
}catch(const std::exception& e){watch=false;std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<"\n";return 1;}}

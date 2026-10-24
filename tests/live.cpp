#include "audio/engine.h"
#include "audio/slice.h"
#include "audio/performance_tools.h"
#include <iostream>
#include <stdexcept>
#include <limits>
#include <atomic>
#include <cstdlib>
#include <new>
using namespace rdj;
static std::atomic<bool> watch{false};
static std::atomic<int> allocs{0};
void* operator new(std::size_t n) { if(watch) ++allocs; if(auto* p=std::malloc(n?n:1))return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
int checks=0;
void check(bool v,const char* text) {++checks;if(!v)throw std::runtime_error(text);}
int main() {try {
    BarLaunch q; q.arm(3.5); check(!q.tick(3.9),"not before bar"); check(q.tick(4.01),"next bar"); check(!q.tick(4.1),"one shot");
    q.arm(4); check(!q.tick(4),"exact boundary waits next"); check(!q.tick(7),"seek rearms"); check(q.tick(8),"rearmed launch");
    q.arm(20); check(!q.tick(2),"backwards rearm"); check(!q.tick(3.9),"wait after seek"); check(q.tick(4),"backwards new bar");
    q.arm(0);q.cancel(); check(!q.tick(4),"cancel launch");
    check(transitionProgress(32,0,8)==1 && transitionProgress(-1,0,8)==0 && transitionProgress(16,0,8)==.5f,"transition progress");
    Track t; t.sampleRate=48000;t.frames=48003;t.pcm.resize(t.frames*2, .5f);
    const auto slices=sliceSample(t,8);check(slices.size()==8,"slice count");
    uint64_t total=0; for(const auto& s:slices) {total+=s->frames;check(s->pcm.front()==0 && s->pcm.back()==0,"slice edge fades");check(s->maxPeak==.5f,"slice unchanged middle");}
    check(total==t.frames,"no lost frames");check(t.pcm.front()==.5f,"source immutable");check(sliceSample(t,0).empty(),"invalid count");
    Track shortT;shortT.sampleRate=48000;shortT.frames=32;shortT.pcm.resize(64);check(sliceSample(shortT,8).empty(),"short input rejected");
    for(double fs:{44100.,48000.,96000.}) {
        CreativeFx fx;fx.prepare(fs);
        for(int kind=0;kind<3;++kind) {
            fx.reset();float energy=0;
            for(int b=0;b<450;++b) {
                float L[256],R[256];
                for(int i=0;i<256;++i){L[i]=.2f*std::sin(float((b*256+i)*2*kPi*440/fs));R[i]=0;}
                watch=true;fx.process(L,R,256,kind,.7f);watch=false;
                for(int i=0;i<256;++i){check(std::isfinite(L[i]) && std::fabs(L[i])<4,"bounded FX");check(R[i]==0,"no stereo bleed");energy+=L[i]*L[i];}
            }
            check(energy>1,"FX audible");
            float L[256]={},R[256]={};fx.process(L,R,256,kind,std::numeric_limits<float>::quiet_NaN());
            for(float v:L)check(std::isfinite(v),"NaN amount sanitized");
        }
    }
    check(allocs==0,"creative effects do not allocate");
    // Exercise the real deck routing for each newly exposed effect.
    for (FxType type : {FxType::Granular,FxType::Resonador,FxType::Multibanda}) {
        Track song;song.sampleRate=48000;song.frames=96000;song.pcm.resize(song.frames*2);
        for(uint64_t i=0;i<song.frames;++i) song.pcm[i*2]=song.pcm[i*2+1]=.15f*std::sin(i*2*kPi*330/48000);
        DeckEngine dry,wet; DeckParams dp,wp; DeckStatus ds,ws;
        dry.prepare(48000);wet.prepare(48000);
        Command load{Cmd::Load};load.track=&song;
        dry.handle(load,dp,ds);wet.handle(load,wp,ws);
        dry.handle(Command{Cmd::Play},dp,ds);wet.handle(Command{Cmd::Play},wp,ws);
        wp.fxType=(int)type;wp.fxOn=true;wp.fxAmount=.8f;
        double change=0,energy=0;
        for(int block=0;block<300;++block) {
            float dl[256],dr[256],wl[256],wr[256];
            watch=true;dry.render(dl,dr,256,dp,ds);wet.render(wl,wr,256,wp,ws);watch=false;
            for(int i=0;i<256;++i) {check(std::isfinite(wl[i]) && std::fabs(wl[i])<4,"new deck FX bounded");change+=std::fabs(wl[i]-dl[i]);energy+=wl[i]*wl[i];}
        }
        check(change>1 && energy>1,"new effect reaches real deck audio");
    }
    // Real engine test: applying EQ/FX to stem 0 cannot change stem 1.
    auto baseline=std::make_unique<Engine>(), processed=std::make_unique<Engine>();
    Track left,right;left.sampleRate=right.sampleRate=48000;left.frames=right.frames=48000*3;
    left.pcm.resize(left.frames*2);right.pcm.resize(right.frames*2);
    for(uint64_t i=0;i<left.frames;++i) {left.pcm[i*2]=.1f*std::sin(i*2*kPi*220/48000);right.pcm[i*2+1]=.1f*std::sin(i*2*kPi*880/48000);}
    StemSet set;set.count=2;set.tracks[0]=&left;set.tracks[1]=&right;
    for(auto* e:{baseline.get(),processed.get()}) {Command c{Cmd::StemLoad};c.stems=&set;e->send(c);e->send(Command{Cmd::StemPlay});}
    processed->stem.fx[0].type=(int)StFx::Multibanda;processed->stem.fx[0].on=true;processed->stem.fx[0].mix=1;processed->stem.fx[0].amount=.9f;
    processed->stem.low[0]=.2f;
    double difference=0;
    for(int b=0;b<300;++b) {
        float a[512],c[512];
        watch=true;baseline->process(a,256,2);processed->process(c,256,2);watch=false;
        for(int i=0;i<256;++i){check(std::fabs(a[i*2+1]-c[i*2+1])<1e-7,"other stem remains unchanged");difference+=std::fabs(a[i*2]-c[i*2]);check(std::isfinite(c[i*2]),"stem output finite");}
    }
    check(difference>1,"stem FX actually changes audio");check(allocs==0,"audio callback does not allocate");
    processed->stem.mute[0]=true;
    for(int b=0;b<10;++b){float c[512];processed->process(c,256,2);if(b==9)for(int i=0;i<256;++i)check(std::fabs(c[i*2])<1e-7,"mute suppresses processed stem");}
    processed->stem.mute[0]=false;processed->stem.fx[0].on=false;
    processed->stem.solo[0]=true;
    for(int b=0;b<10;++b){float c[512];processed->process(c,256,2);if(b==9)for(int i=0;i<256;++i)check(std::fabs(c[i*2+1])<1e-7,"solo suppresses other stem");}
    std::cout<<"PASS "<<checks<<" checks: DSP, no allocations, slicing, scheduler, stem isolation, mute/solo\n";
}catch(const std::exception& e){watch=false;std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<"\n";return 1;}}

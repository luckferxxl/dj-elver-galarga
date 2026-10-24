#include "audio/engine.h"
#include "audio/pattern_tools.h"
#include <iostream>
#include <stdexcept>
#include <atomic>
#include <cstdlib>
#include <new>
#include <limits>
using namespace rdj;
static std::atomic<bool> watch{false};static std::atomic<int> allocs{0};
void* operator new(std::size_t n){if(watch)++allocs;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
int checks=0;void check(bool v,const char* msg){++checks;if(!v)throw std::runtime_error(msg);}
Track sample(int frames=48000){Track t;t.sampleRate=48000;t.frames=frames;t.pcm.resize(frames*2,.2f);return t;}
void render(Instruments& x,int frames){RackCtx ctx;while(frames){float L[256],R[256];int n=std::min(frames,256);watch=true;x.render(L,R,n,ctx);watch=false;for(int i=0;i<n;++i)check(std::isfinite(L[i])&&std::fabs(L[i])<=2,"finite sampler output");frames-=n;}}
int main(){try{
    auto region=padRegion(1000,.2f,.8f);check(region.begin==200&&region.end==800,"fractional trim bounds");
    check(padRegion(1,0,1).valid()==false,"short region rejected");
    check(padRegion(100,1,0).valid(),"invalid trim gets bounded region");
    check(region.wrap(region.end+5)<region.end&&region.wrap(region.begin-5)>=region.begin,"wrap both directions");
    auto pattern=generateRhythm(42,.55f,32);check(pattern==generateRhythm(42,.55f,32),"seed reproducible");check(pattern!=generateRhythm(43,.55f,32),"different seeds differ");
    check(rotateRhythm(rotateRhythm(pattern,32,1),32,-1)==pattern,"pattern rotation reversible");
    for(auto row:generateRhythm(1,0,16))for(auto cell:row)check(cell==0,"zero density empty");
    for(auto row:pattern)for(auto cell:row)check(cell<=2,"valid pattern velocity");
    Track t=sample();
    {Instruments x;x.prepare(48000);x.padSet(0,&t,1);x.params.padStart[0]=.25f;x.params.padEnd[0]=.75f;x.params.padReverse[0]=true;x.padPress(0);render(x,256);
     check(std::fabs(x.status.padPos[0].load()*t.frames-(36000-1-256))<1,"reverse starts at trim end");}
    {Instruments x;x.prepare(48000);x.padSet(0,&t,1);x.params.padStart[0]=.25f;x.params.padEnd[0]=.75f;x.params.padPitch[0]=12;x.padPress(0);render(x,256);
     check(std::fabs(x.status.padPos[0].load()*t.frames-(12000+512))<1,"octave doubles playback speed");}
    {Instruments x;x.prepare(48000);x.padSet(0,&t,1);x.params.padStart[0]=.25f;x.params.padEnd[0]=.3f;x.params.padReverse[0]=true;x.params.padMode[0]=PM_Loop;x.params.padQuant=false;x.padPress(0);render(x,24000);
     check(x.status.padPlaying[0],"reverse trimmed loop continues");check(x.status.padPos[0]>=.25f&&x.status.padPos[0]<=.301f,"loop stays inside region");}
    {Instruments x;x.prepare(48000);x.padSet(0,&t,1);x.padSet(1,&t,2);x.params.padChoke[0]=x.params.padChoke[1]=1;x.padPress(0);render(x,1000);x.padPress(1);render(x,1000);
     check(!x.status.padPlaying[0]&&x.status.padPlaying[1],"choke fades other pad");}
    {Instruments x;x.prepare(48000);x.padSet(0,&t,1);x.padSet(1,&t,2);x.padPress(0);x.padPress(1);render(x,1000);check(x.status.padPlaying[0]&&x.status.padPlaying[1],"ungrouped pads overlap");}
    {Instruments x;x.prepare(48000);x.padSet(0,&t,1);x.params.padMode[0]=PM_Mantener;x.params.padRelease[0]=200;x.padPress(0);render(x,1000);x.padRelease(0);render(x,4800);check(x.status.padPlaying[0],"long release sustained");render(x,6000);check(!x.status.padPlaying[0],"release eventually stops");}
    {Instruments x;x.prepare(48000);Track bad;bad.frames=100;bad.sampleRate=48000;x.padSet(0,&bad,1);x.padPress(0);render(x,256);check(!x.status.padPlaying[0],"invalid PCM rejected");}
    {Instruments x;x.prepare(48000);x.padSet(0,&t,1);x.params.padStart[0]=std::numeric_limits<float>::quiet_NaN();x.params.padPitch[0]=std::numeric_limits<float>::infinity();x.padPress(0);render(x,1000);check(x.status.padPlaying[0],"nonfinite controls sanitized");}
    {Instruments fast,slow;fast.prepare(48000);slow.prepare(48000);fast.padSet(0,&t,1);slow.padSet(0,&t,1);slow.params.padAttack[0]=100;fast.padPress(0);slow.padPress(0);render(fast,1000);render(slow,1000);check(slow.status.peak.load()<fast.status.peak.load()*.5f,"long attack reduces initial peak");}
    {Instruments straight,human;straight.prepare(48000);human.prepare(48000);
     for(auto* x:{&straight,&human}){x->params.sync=3;x->params.ownBpm=120;x->params.quantStart=false;x->params.drumPlay=true;for(int s=0;s<16;++s)x->params.drum[0][0][s]=1;}
     human.params.humanizeVelocity=.5f;RackCtx c;double difference=0;
     for(int b=0;b<200;++b){float L[256],R[256],H[256],J[256];straight.render(L,R,256,c);human.render(H,J,256,c);for(int i=0;i<256;++i)difference+=std::fabs(L[i]-H[i]);}
     check(difference>1,"humanization changes actual drum dynamics");}
    for(float probability:{0.f,.5f,1.f}) {
        Instruments x;x.prepare(48000);x.params.sync=3;x.params.ownBpm=120;x.params.quantStart=false;x.params.drumPlay=true;x.params.dProbability[0]=probability;
        for(int s=0;s<16;++s)x.params.drum[0][0][s]=1;
        render(x,96000);const auto hits=x.status.drumHits[0].load();
        check(probability==0?hits==0:probability==1?hits>=15:hits>0&&hits<15,"drum probability affects triggers");
    }
    {DeckEngine deck;DeckParams p;DeckStatus st;deck.prepare(48000);Command load{Cmd::Load};load.track=&t;deck.handle(load,p,st);p.bpm=120;p.quantize=false;
     Command recall{Cmd::RecallLoop};recall.d=4800;recall.end=9600;deck.handle(recall,p,st);float L[256],R[256];deck.render(L,R,256,p,st);
     check(st.loopActive&&st.playing&&st.loopIn==4800&&st.loopOut==9600,"saved loop restores exact region");
     Command move{Cmd::BeatJump};move.d=-100;deck.handle(move,p,st);deck.render(L,R,256,p,st);check(st.loopIn==0&&st.loopOut==4800,"loop move clamps beginning");
     move.d=100;deck.handle(move,p,st);deck.render(L,R,256,p,st);check(st.loopOut==48000&&st.loopIn==43200,"loop move clamps end without shrinking");
     recall.d=20;recall.end=10;deck.handle(recall,p,st);deck.render(L,R,256,p,st);check(st.loopIn==43200,"invalid recall rejected");}
    {auto e=std::make_unique<Engine>();Track silence=sample(96000);std::fill(silence.pcm.begin(),silence.pcm.end(),0);Command load{Cmd::Load};load.track=&silence;e->send(load);e->deck[0].bpm=120;e->send(Command{Cmd::Play});
     e->mixer.cueClick=true;e->mixer.hpRouting=(int)HpRouting::Canales34;e->mixer.hpVol=1;e->mixer.hpMix=0;e->mixer.recording=true;
     double cueEnergy=0;
     for(int b=0;b<200;++b){float out[1024];watch=true;e->process(out,256,4);watch=false;for(int i=0;i<256;++i){check(out[i*4]==0&&out[i*4+1]==0,"cue metronome absent from master");cueEnergy+=std::fabs(out[i*4+2]);}}
     check(cueEnergy>1,"metronome audible in headphone channels");
     float rec[1024];while(auto n=e->recRing.read(rec,1024))for(size_t i=0;i<n;++i)check(rec[i]==0,"metronome absent from recorder");
     std::vector<float> captured;check(e->retrospective.capture(5,captured),"capture available");for(float x:captured)check(x==0,"metronome absent from retrospective");
     e->mixer.cueClick=false;double after=0;for(int b=0;b<20;++b){float out[1024];e->process(out,256,4);if(b>0)for(int i=0;i<256;++i)after+=std::fabs(out[i*4+2]);}check(after==0,"metronome off silences cue after limiter lookahead");}
    check(allocs==0,"tested audio paths allocate no memory");std::cout<<"PASS "<<checks<<" checks: pad editing, loops, probability, pattern tools, headphone-only metronome\n";
}catch(const std::exception& e){watch=false;std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<"\n";return 1;}}

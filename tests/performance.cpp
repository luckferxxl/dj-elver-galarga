// Portable regression tests for audio changes in 0.6. No audio hardware needed.
#include "audio/engine.h"
#include "core/filename.h"
#include <iostream>
#include <stdexcept>
#include <chrono>
using namespace rdj;
int checks=0;
void check(bool ok,const char* msg) { ++checks; if (!ok) throw std::runtime_error(msg); }
struct Fixture {
    Track t; DeckEngine deck; DeckParams p; DeckStatus s; float l[256],r[256];
    Fixture() {
        t.sampleRate=48000; t.frames=48000*12; t.pcm.resize(t.frames*2);
        for (size_t i=0;i<t.frames;++i) t.pcm[2*i]=t.pcm[2*i+1]=0.25f*std::sin(i*2*kPi*440/48000);
        deck.prepare(48000); Command c{Cmd::Load}; c.track=&t; deck.handle(c,p,s); p.bpm=120; p.quantize=false;
    }
    void cmd(Cmd c,double v=0) { Command q{c};q.d=v;deck.handle(q,p,s); }
    void render(int frames) { while(frames) {int n=std::min(256,frames);deck.render(l,r,n,p,s);for(int i=0;i<n;++i) if(!std::isfinite(l[i])||!std::isfinite(r[i])) throw std::runtime_error("non-finite audio");frames-=n;} }
};
int main() { try {
    { Fixture f; f.cmd(Cmd::Play); f.render(48000); check(std::abs(f.deck.position()-48000)<2,"normal playback");
      f.cmd(Cmd::RollStart,0.25); f.render(48000); check(f.s.rolling && f.deck.position()<54001,"roll wraps");
      f.cmd(Cmd::RollEnd); f.render(256); check(std::abs(f.deck.position()-96256)<3,"roll resumes timeline"); }
    { Fixture f; f.cmd(Cmd::Seek,48000); f.cmd(Cmd::RollStart,0.5); f.render(24000); f.cmd(Cmd::RollEnd); f.render(256);
      check(!f.s.playing && std::abs(f.deck.position()-48000)<3,"roll paused restores position/state"); }
    { Fixture f; f.cmd(Cmd::Seek,48000); f.cmd(Cmd::Play); f.cmd(Cmd::AutoLoop,2); f.render(12000); auto before=f.deck.position();
      f.cmd(Cmd::RollStart,0.125); f.render(96000); f.cmd(Cmd::RollEnd); f.render(256);
      check(f.s.loopActive && std::abs(f.deck.position()-(before+256))<3,"roll restores preexisting loop"); }
    { Fixture f; f.cmd(Cmd::Seek,96000); f.cmd(Cmd::Play); f.p.slip=true; f.p.reverse=true; f.render(24000);
      check(f.deck.position()<73000,"reverse moves backwards"); f.p.reverse=false; f.render(256);
      check(std::abs(f.deck.position()-120256)<3,"slip reverse resumes timeline"); }
    { Fixture f; f.cmd(Cmd::Seek,96000); f.cmd(Cmd::Play); f.p.slip=true; f.p.scratching=true; f.p.scratchVelocity=-1; f.render(24000);
      f.p.scratching=false; f.render(256); check(std::abs(f.deck.position()-120256)<3,"slip scratch resumes timeline"); }
    { Fixture f; f.cmd(Cmd::Play); f.render(48000); f.p.slip=true; f.cmd(Cmd::AutoLoop,0.5); f.render(48000); f.cmd(Cmd::LoopExit); f.render(256);
      check(std::abs(f.deck.position()-96256)<3,"slip loop resumes timeline"); }
    for (auto c:{Cmd::Brake,Cmd::Backspin}) { Fixture f; f.cmd(Cmd::Seek,192000); f.cmd(Cmd::Play); f.cmd(c,0.5); f.render(26000); check(!f.s.playing,"one shot stops"); }
    for (auto c:{Cmd::Brake,Cmd::Backspin}) { Fixture f; f.cmd(Cmd::Seek,192000); f.cmd(Cmd::Play); f.p.slip=true; f.cmd(c,0.5); f.render(26000); check(f.s.playing,"slip one shot resumes playing"); check(std::abs(f.deck.position()-218000)<10,"slip one shot timeline"); }
    { Fixture f; f.cmd(Cmd::Seek,96000); f.cmd(Cmd::Play); f.p.keyLock=true; f.render(48000); f.cmd(Cmd::RollStart,0.25); f.render(48000); f.cmd(Cmd::RollEnd); f.render(512); check(std::abs(f.deck.position()-192512)<4,"keylock roll timeline"); }
    { Fixture f; f.cmd(Cmd::Seek,575980); f.cmd(Cmd::RollStart,1); f.render(256); check(!f.s.rolling,"reject roll too close to end"); }
    { Fixture f; f.cmd(Cmd::Play);f.p.slip=true;f.p.reverse=true;f.render(100);Command q{Cmd::Unload};f.deck.handle(q,f.p,f.s);f.render(256);check(!f.s.rolling && !f.s.slipActive && !f.s.hasTrack && !f.p.reverse,"unload clears transient states"); }
    { Fixture f; f.cmd(Cmd::Play);f.render(48000);f.cmd(Cmd::RollStart,0.25);f.render(24000);f.cmd(Cmd::Pause);f.cmd(Cmd::RollEnd);f.render(256);check(!f.s.playing && !f.s.rolling,"pause wins over roll release"); }
    { Fixture f; f.cmd(Cmd::Seek,96000);f.p.keyLock=true;f.cmd(Cmd::Play);f.render(10000);f.p.reverse=true;f.render(10000);f.p.reverse=false;f.render(10000);check(f.s.playing,"keylock reverse transitions"); }
    auto start=std::chrono::steady_clock::now();
    for (int type=1;type<(int)FxType::Riser;++type) {   // los de Riser en adelante los procesa StUnit (pruebas del rack)
        FxSlot fx; fx.prepare(48000); double difference=0,energy=0;
        for (int block=0;block<400;++block) {float l[256],r[256],dry[256];for(int i=0;i<256;++i) dry[i]=l[i]=r[i]=0.3f*std::sin((block*256+i)*2*kPi*330/48000);
          fx.process(l,r,256,(FxType)type,block<300,0.8f,0.5f,132);
          for(int i=0;i<256;++i) {check(std::isfinite(l[i])&&std::abs(l[i])<8,"bounded effect output");difference+=std::abs(l[i]-dry[i]);energy+=std::abs(l[i]);}}
        check(difference>1 && energy>1,"effect changes audio");
    }
    check(safeFileComponent(L"AC/DC: Tema?")==L"AC_DC_ Tema_","safe folder punctuation");
    check(safeFileComponent(L"CON")==L"_CON","Windows device name");
    check(safeFileComponent(L"Tema... ")==L"Tema","trailing dot");
    check(!safeFileComponent(L"...").empty(),"empty sanitized name");
    std::cout<<"PASS "<<checks<<" checks. FX benchmark: "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" s for 19.2 s of audio.\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<"\n";return 1;} }

#pragma once
#include "instruments.h"
#include <array>
namespace rdj {
struct FactoryDrumKit {const char* name;float tune,decay,drive,cut;int bits;};
inline constexpr std::array<FactoryDrumKit,24> kFactoryDrumKits={{
 {"Club seco",.50f,.22f,.05f,17500,16},{"Analógico redondo",.38f,.55f,.08f,14000,16},
 {"Sub profundo",.10f,.85f,.04f,11000,16},{"Techno duro",.42f,.40f,.65f,16000,16},
 {"Industrial",.62f,.48f,.90f,12000,9},{"Warehouse",.30f,.68f,.45f,14500,14},
 {"House cálido",.46f,.42f,.18f,12000,16},{"Minimal corto",.58f,.08f,.02f,18000,16},
 {"Electro brillante",.73f,.32f,.25f,19500,14},{"Acid percusivo",.64f,.28f,.42f,16000,12},
 {"Breaks crujientes",.54f,.35f,.48f,15000,10},{"Lo-fi cinta",.35f,.60f,.30f,6500,11},
 {"Digital roto",.80f,.26f,.65f,9000,6},{"Vintage oscuro",.25f,.52f,.12f,5000,13},
 {"Garage ligero",.66f,.18f,.10f,17000,16},{"Tribal grave",.18f,.72f,.22f,10500,16},
 {"Dub largo",.22f,.95f,.16f,7500,16},{"Disco compacto",.57f,.25f,.15f,15500,15},
 {"Rave agresivo",.71f,.61f,.80f,18500,10},{"Percusión metálica",.94f,.44f,.28f,19000,16},
 {"Hip-hop pesado",.16f,.62f,.40f,8500,12},{"Drum and bass",.61f,.15f,.38f,20000,14},
 {"Ambient suave",.32f,.98f,.02f,4500,16},{"Micro percusión",.87f,.03f,.12f,16500,8}
}};
inline TrackPtr makeFactoryDrum(int kit,int voice,uint32_t rate=48000) {
    if(rate<8000||rate>192000)return {};
    if(kit<0||kit>=int(kFactoryDrumKits.size())||voice<0||voice>=8)return {};
    const auto& recipe=kFactoryDrumKits[kit];
    auto track=std::make_shared<Track>();track->sampleRate=rate;
    const double lengths[8]={1.1,.7,.7,.25,1.3,1.1,.2,3.0};
    track->frames=uint64_t(rate*lengths[voice]);track->pcm.resize(track->frames*2);
    DrumVoices synth;synth.prepare(rate);synth.trigger(voice,1,recipe.tune,recipe.decay);
    float level[8]={1,1,1,1,1,1,1,1};bool mute[8]={};float lpL=0,lpR=0,peak=0;
    const float coef=float(1-std::exp(-6.283185307179586*std::min(double(recipe.cut),rate*.45)/rate));
    const float drive=1+recipe.drive*10,norm=std::tanh(drive),steps=std::pow(2.f,float(recipe.bits-1));
    for(uint64_t pos=0;pos<track->frames;pos+=256) {
        const int n=int(std::min<uint64_t>(256,track->frames-pos));float L[256]={},R[256]={};synth.render(L,R,n,level,mute);
        for(int i=0;i<n;++i) {
            lpL+=coef*(L[i]-lpL);lpR+=coef*(R[i]-lpR);
            float l=std::tanh(lpL*drive)/norm,r=std::tanh(lpR*drive)/norm;
            if(recipe.bits<16){l=std::round(l*steps)/steps;r=std::round(r*steps)/steps;}
            const float fade=float(std::clamp(double(track->frames-1-pos-i)/(.003*rate),0.,1.));
            track->pcm[(pos+i)*2]=l*fade;track->pcm[(pos+i)*2+1]=r*fade;peak=std::max(peak,std::max(std::fabs(l*fade),std::fabs(r*fade)));
        }
    }
    if(peak>0)for(auto& x:track->pcm)x*=.8f/peak;
    track->maxPeak=peak>0?.8f:0;track->title=std::string(recipe.name)+" · "+drumVoiceName(voice);track->meta.durationSec=track->lengthSec();
    return track;
}
} // namespace rdj

#include "app.h"
#include "../core/util.h"
#include "../audio/pattern_tools.h"
#include <json.hpp>
#include <algorithm>
#include <cmath>

namespace rdj {
using json=nlohmann::json;
std::string App::workbenchToJson() const {
    json loops=json::array();
    for(const auto& [path,slots]:savedLoops_) {
        json arr=json::array();for(const auto& s:slots)arr.push_back({s.begin,s.end});
        loops.push_back({{"ruta",toUtf8(path)},{"slots",arr}});
    }
    return loops.dump();
}
void App::workbenchFromJson(const std::string& text) {
    auto j=json::parse(text,nullptr,false);if(!j.is_array())return;
    for(const auto& x:j) {
        if(!x.is_object() || !x.contains("ruta") || !x["ruta"].is_string() || !x.contains("slots") || !x["slots"].is_array())continue;
        const auto path=x["ruta"].get<std::string>();if(path.empty())continue;
        std::array<SavedLoop,8> slots{};
        for(size_t i=0;i<8 && i<x["slots"].size();++i) {
            const auto& a=x["slots"][i];
            if(!a.is_array() || a.size()!=2 || !a[0].is_number() || !a[1].is_number())continue;
            const double start=a[0].get<double>(),end=a[1].get<double>();
            if(std::isfinite(start) && std::isfinite(end) && start>=0 && end>start && end<86400)slots[i]={start,end};
        }
        savedLoops_[fromUtf8(path)]=slots;
    }
}
void App::saveDeckLoop(int d,int slot) {
    if(d<0 || d>=kNumDecks || slot<0 || slot>=8 || !deck_[d].track || !deck_[d].pending.empty())return;
    const auto& st=engine_.deckStatus[d];
    const double a=st.loopIn.load(),b=st.loopOut.load();
    if(!st.loopActive.load() || !std::isfinite(a) || !std::isfinite(b) || a<0 || b<=a+64 || b>deck_[d].track->frames) {
        notify("Activa un loop válido antes de guardarlo.",true);return;
    }
    const double sr=deck_[d].track->sampleRate;
    savedLoops_[deck_[d].path][slot]={a/sr,b/sr};
    if(saveRack())notify("Loop guardado en la memoria " + std::to_string(slot+1));
}
void App::recallDeckLoop(int d,int slot) {
    if(d<0 || d>=kNumDecks || slot<0 || slot>=8 || !deck_[d].track || !deck_[d].pending.empty())return;
    const auto it=savedLoops_.find(deck_[d].path);if(it==savedLoops_.end())return;
    const auto& s=it->second[slot];const double sr=deck_[d].track->sampleRate;
    if(s.begin<0 || s.end*sr>deck_[d].track->frames) {notify("El loop no cabe en el archivo actual.",true);return;}
    Command c{Cmd::RecallLoop,uint8_t(d)};c.d=s.begin*sr;c.end=s.end*sr;
    if(!engine_.send(c))notify("Cola de audio ocupada; vuelve a lanzar el loop.",true);
}
void App::drawWorkbench() {
    if(!workbenchOpen_)return;
    ImGui::SetNextWindowSize(ImVec2(690,650),ImGuiCond_FirstUseEver);
    if(!ImGui::Begin("TALLER · Preparación y actuación",&workbenchOpen_)){ImGui::End();return;}
    if(ImGui::Button("CAJA PRO: kits, samples y envolventes"))drumStudioOpen_=true;
    if(ImGui::BeginTabBar("worktabs")) {
        for(int d=0;d<kNumDecks;++d)if(ImGui::BeginTabItem(d==0?"Loops A":"Loops B")) {
            const auto& dk=deck_[d];
            ImGui::TextWrapped("%s",dk.track?dk.title.c_str():"Carga una canción en este plato.");
            if(dk.track) {
                auto& slots=savedLoops_[dk.path];
                ImGui::TextWrapped("Ocho memorias por archivo. Lanzar recupera la región exacta y reproduce. Guardar sustituye esa memoria. El archivo de audio no cambia.");
                if(ImGui::BeginTable("loopmem",3,ImGuiTableFlags_SizingStretchProp)) {
                    for(int i=0;i<8;++i) {
                        ImGui::PushID(i);ImGui::TableNextRow();ImGui::TableNextColumn();
                        if(slots[i].begin>=0)ImGui::Text("%d · %.2f–%.2f s",i+1,slots[i].begin,slots[i].end);
                        else ImGui::Text("%d · Vacía",i+1);
                        ImGui::TableNextColumn();ImGui::BeginDisabled(slots[i].begin<0 || !dk.pending.empty());
                        if(ImGui::Button("Lanzar"))recallDeckLoop(d,i);
                        ImGui::EndDisabled();
                        if(ImGui::BeginPopupContextItem("loopmidi")) {
                            if(ImGui::MenuItem("Aprender botón MIDI")){midi_.startLearn(MA_SavedLoop1+i,d);notify("Pulsa el botón MIDI para lanzar esta memoria…");}
                            ImGui::EndPopup();
                        }
                        ImGui::TableNextColumn();
                        if(ImGui::Button("Guardar"))saveDeckLoop(d,i);
                        ImGui::SameLine();if(ImGui::Button("Vaciar")){slots[i]={};saveRack();}
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                ImGui::Separator();ImGui::TextUnformatted("Mover el loop activo (pulsos)");
                ImGui::BeginDisabled(!engine_.deckStatus[d].loopActive.load());
                for(int beats:{-16,-4,-1,1,4,16}) {
                    if(beats!=-16)ImGui::SameLine();const auto label=(beats>0?"+":"")+std::to_string(beats);
                    if(ImGui::Button(label.c_str())){Command c{Cmd::BeatJump,uint8_t(d)};c.d=beats;engine_.send(c);}
                }
                ImGui::EndDisabled();
                if(ImGui::Button("Salir del loop"))engine_.send(Command{Cmd::LoopExit,uint8_t(d)});
            }
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Sampler")) {
            int shownPad=editPad_+1;if(ImGui::SliderInt("Pad",&shownPad,1,kPads))editPad_=shownPad-1;
            auto& p=engine_.rack.instr.params;const int k=editPad_;const auto& sample=pads_[k];
            ImGui::Text("PAD %d · %s",k+1,sample.track?sample.name.c_str():"Vacío");
            if(sample.track && sample.track->frames>0) {
                float wave[256];for(int i=0;i<256;++i){const auto at=std::min<uint64_t>(sample.track->frames-1,sample.track->frames*i/256);wave[i]=sample.track->pcm[at*2];}
                ImGui::PlotLines("Vista simplificada",wave,256,0,nullptr,-1,1,ImVec2(0,80));
            }
            auto slider=[](const char* label,std::atomic<float>& a,float lo,float hi,const char* fmt){float v=a.load();if(ImGui::SliderFloat(label,&v,lo,hi,fmt))a=v;};
            slider("Afinación",p.padPitch[k],-24,24,"%+.1f semitonos");
            bool reverse=p.padReverse[k].load();if(ImGui::Checkbox("Reversa",&reverse))p.padReverse[k]=reverse;
            float start=p.padStart[k].load(),end=p.padEnd[k].load();
            if(ImGui::SliderFloat("Inicio",&start,0,.999f,"%.3f"))p.padStart[k]=std::min(start,end-.001f);
            if(ImGui::SliderFloat("Final",&end,.001f,1,"%.3f"))p.padEnd[k]=std::max(end,start+.001f);
            slider("Ataque",p.padAttack[k],.2f,250,"%.1f ms");slider("Salida",p.padRelease[k],1,1000,"%.1f ms");
            int group=p.padChoke[k].load();if(ImGui::SliderInt("Grupo de corte",&group,0,4,"%d (0 = independiente)"))p.padChoke[k]=group;
            ImGui::TextWrapped("Pads del mismo grupo se cortan entre sí, respetando su salida. Afinar también cambia velocidad y duración; no aplica key lock. El recorte es no destructivo.");
            if(ImGui::Button("Disparar"))padCmd(k,1);ImGui::SameLine();if(ImGui::Button("Soltar"))padCmd(k,0);ImGui::SameLine();if(ImGui::Button("Parar todos"))padCmd(0,2);
            if(ImGui::Button("Restablecer edición del pad")){p.padPitch[k]=0;p.padReverse[k]=false;p.padStart[k]=0;p.padEnd[k]=1;p.padAttack[k]=2;p.padRelease[k]=10;p.padChoke[k]=0;}
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Práctica")) {
            auto& m=engine_.mixer;
            bool on=m.cueClick.load();if(ImGui::Checkbox("Metrónomo solo en preescucha",&on))m.cueClick=on;
            int d=m.cueClickDeck.load();if(ImGui::RadioButton("Plato A",d==0))m.cueClickDeck=0;ImGui::SameLine();if(ImGui::RadioButton("Plato B",d==1))m.cueClickDeck=1;
            float v=m.cueClickVol.load();if(ImGui::SliderFloat("Nivel del clic",&v,0,.5f,"%.2f"))m.cueClickVol=v;
            ImGui::TextWrapped("Requiere un plato sonando con BPM/rejilla y una salida de auriculares configurada. Lleva la mezcla de auriculares hacia CUE. No entra en el master ni en la grabación.");
            for(int k=0;k<kNumDecks;++k) {
                const int other=engine_.deck[k].syncTo.load();
                const double bpm=engine_.effectiveBpm(other>=0&&other<kNumDecks?other:k),err=engine_.deckStatus[k].syncError.load();
                const double unit=engine_.deck[k].syncBar.load()||(other>=0&&other<kNumDecks&&engine_.deck[other].syncBar.load())?4.:1.;
                if(engine_.deck[k].syncTo.load()>=0 && bpm>0)ImGui::Text("SYNC %c: error %.1f ms · %s",'A'+k,err*unit*60000/bpm,engine_.deckStatus[k].syncLocked.load()?"alineado":"corrigiendo");
                else ImGui::Text("Plato %c: %.2f BPM · activa SYNC para ver su diagnóstico",'A'+k,bpm);
            }
            ImGui::TextWrapped("El diagnóstico muestra el error publicado por SYNC; no puntúa la habilidad del DJ ni mide desfase acústico de altavoces.");
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Ritmos")) {
            auto& p=engine_.rack.instr.params;
            float human=p.humanizeVelocity.load();if(ImGui::SliderFloat("Variación de intensidad",&human,0,.5f,"%.2f"))p.humanizeVelocity=human;
            for(int t=0;t<kDrumTracks;++t){float v=p.dProbability[t].load();if(ImGui::SliderFloat(drumVoiceName(t),&v,0,1,"Probabilidad %.2f"))p.dProbability[t]=v;}
            ImGui::Separator();
            ImGui::InputInt("Semilla del patrón",&patternSeed_);ImGui::SliderFloat("Densidad",&patternDensity_,0,1,"%.2f");
            auto readPattern=[&](int at){RhythmPattern data{};for(int t=0;t<8;++t)for(int s=0;s<32;++s)data[t][s]=p.drum[at][t][s].load();return data;};
            auto writePattern=[&](int at,const RhythmPattern& data){for(int t=0;t<8;++t)for(int s=0;s<32;++s)p.drum[at][t][s]=data[t][s];};
            const int at=std::clamp(p.drumPattern.load(),0,kDrumPatterns-1),steps=p.drumSteps[at].load()==32?32:16;
            auto backup=[&](int target){patternUndo_=readPattern(target);patternUndoIndex_=target;patternUndoSteps_=p.drumSteps[target].load();patternCanUndo_=true;};
            ImGui::Text("Editando patrón %d · %d pasos",at+1,steps);
            if(ImGui::Button("Generar y sustituir")){backup(at);writePattern(at,generateRhythm(uint32_t(patternSeed_),patternDensity_,steps));}
            ImGui::SameLine();if(ImGui::Button("Rotar +1")){backup(at);writePattern(at,rotateRhythm(patternUndo_,steps,1));}
            ImGui::SameLine();if(ImGui::Button("Rotar -1")){backup(at);writePattern(at,rotateRhythm(patternUndo_,steps,-1));}
            if(ImGui::Button("Copiar al patrón siguiente")){const auto data=readPattern(at);const int target=(at+1)%kDrumPatterns;backup(target);writePattern(target,data);p.drumSteps[target]=steps;}
            ImGui::SameLine();ImGui::BeginDisabled(!patternCanUndo_);
            if(ImGui::Button("Deshacer última edición")){writePattern(patternUndoIndex_,patternUndo_);p.drumSteps[patternUndoIndex_]=patternUndoSteps_;patternCanUndo_=false;}
            ImGui::EndDisabled();
            ImGui::TextWrapped("Misma semilla y densidad producen el mismo patrón. La probabilidad decide qué golpes se tocan; la variación cambia su intensidad, no el tempo. Deshacer cubre las ediciones de este taller.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}
} // namespace rdj

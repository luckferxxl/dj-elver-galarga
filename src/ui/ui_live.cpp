// Banco de escenas y transiciones de macro. Cuantización de interfaz, no sample-accurate.
#include "app.h"
#include "../core/util.h"
#include <json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rdj {
using json = nlohmann::json;

std::string App::scenesToJson() const {
    json arr=json::array();
    for (const auto& s : scenes_) arr.push_back({{"nombre",s.name},{"rack",s.rack},{"instrumentos",s.instruments},
        {"ritmos",s.drums},{"bajo",s.bass}});
    return arr.dump();
}
void App::scenesFromJson(const std::string& text) {
    auto j=json::parse(text,nullptr,false);
    if (!j.is_array()) return;
    for (size_t i=0;i<scenes_.size() && i<j.size();++i) {
        const auto& x=j[i];
        if (!x.is_object() || !x.contains("rack") || !x["rack"].is_string() ||
            !x.contains("instrumentos") || !x["instrumentos"].is_string()) continue;
        auto rack=json::parse(x["rack"].get<std::string>(),nullptr,false);
        auto instr=json::parse(x["instrumentos"].get<std::string>(),nullptr,false);
        if (!rack.is_object() || !instr.is_object()) continue;
        auto& s=scenes_[i];
        s.name=x.contains("nombre") && x["nombre"].is_string() ? x["nombre"].get<std::string>().substr(0,60) : "Escena";
        s.rack=rack.dump(); s.instruments=instr.dump();
        s.drums=x.contains("ritmos") && x["ritmos"].is_boolean() && x["ritmos"].get<bool>();
        s.bass=x.contains("bajo") && x["bajo"].is_boolean() && x["bajo"].get<bool>();
    }
}
void App::sceneSave(int i) {
    if (i<0 || i>=int(scenes_.size())) return;
    auto& s=scenes_[i];
    s.name=sceneName_[0] ? sceneName_ : "Escena " + std::to_string(i+1);
    s.rack=rackToJson();
    auto instr=json::parse(instrToJson());
    // No recarga archivos al lanzar: los pads mantienen sus muestras actuales.
    instr["sampler"].erase("pads");
    s.instruments=instr.dump();
    s.drums=engine_.rack.instr.params.drumPlay.load();
    s.bass=engine_.rack.instr.params.bassPlay.load();
    if(saveRack()) notify("Guardada: " + s.name);
}
void App::sceneApply(int i) {
    if (i<0 || i>=int(scenes_.size()) || scenes_[i].rack.empty()) return;
    transitionRunning_=false; transitionLaunch_.cancel();
    motion_.state=Motion::Parado; motion_.xyOwned=false;
    engine_.rack.params.xyTouch=false;
    try {
        if (!rackFromJson(scenes_[i].rack) || !instrFromJson(scenes_[i].instruments)) return;
    } catch (const json::exception&) { notify("La escena contiene parámetros inválidos.",true); return; }
    engine_.rack.instr.params.drumPlay=scenes_[i].drums;
    engine_.rack.instr.params.bassPlay=scenes_[i].bass;
    rackPresetName_.clear();
    notify("Escena: " + scenes_[i].name);
}
void App::sceneLaunch(int i) {
    if (i<0 || i>=int(scenes_.size()) || scenes_[i].rack.empty()) return;
    sceneLaunch_.cancel(); pendingScene_=-1;
    double beat=0;
    if (!sceneQuant_) { sceneApply(i); return; }
    int d=engine_.deck[1].fader.load()>engine_.deck[0].fader.load() ? 1 : 0;
    if (!motionBeat(d,beat)) d=1-d;
    if (!motionBeat(d,beat)) { notify("Para lanzar en compás debe sonar un plato con BPM. Desactiva la cuantización para lanzar ahora.",true); return; }
    sceneDeck_=d; pendingScene_=i; sceneLaunch_.arm(beat);
    notify("Escena preparada para el siguiente compás: " + scenes_[i].name);
}
void App::liveTick() {
    double beat=0;
    if (sceneLaunch_.armed()) {
        if (!motionBeat(sceneDeck_,beat)) { sceneLaunch_.cancel(); pendingScene_=-1; notify("Escena cancelada: el plato se ha parado."); }
        else if (sceneLaunch_.tick(beat)) { const int i=pendingScene_; pendingScene_=-1; sceneApply(i); }
    }
    if (transitionLaunch_.armed()) {
        if (!motionBeat(transitionDeck_,beat)) { transitionLaunch_.cancel(); notify("Transición cancelada: el plato se ha parado."); }
        else if (transitionLaunch_.tick(beat)) {
            transitionStart_=std::floor(beat/4.)*4.; transitionLastBeat_=beat;
            transitionFrom_=engine_.rack.params.macro.load(); transitionLast_=transitionFrom_;
            transitionRunning_=true;
        }
    }
    if (!transitionRunning_) return;
    if (!motionBeat(transitionDeck_,beat) || beat<transitionLastBeat_-.25 || beat-transitionLastBeat_>2.) {
        transitionRunning_=false; notify("Transición detenida por pausa o salto."); return;
    }
    transitionLastBeat_=beat;
    auto& macro=engine_.rack.params.macro;
    if (std::fabs(macro.load()-transitionLast_)>.005f) {
        transitionRunning_=false; notify("Transición detenida: el control manual tiene prioridad."); return;
    }
    const float progress=transitionProgress(beat,transitionStart_,transitionBars_);
    const float curve=progress*progress*(3-2*progress);
    transitionLast_=transitionFrom_+(transitionTarget_-transitionFrom_)*curve;
    macro=transitionLast_;
    if (progress>=1) { transitionRunning_=false; notify("Transición de macro terminada."); }
}
void App::drawLiveWindow() {
    if (!liveOpen_) return;
    ImGui::SetNextWindowSize(ImVec2(650,580),ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("LIVE · Escenas y transiciones",&liveOpen_)) { ImGui::End(); return; }
    if(ImGui::Button("TALLER: loops, sampler, práctica y ritmos"))workbenchOpen_=true;
    ImGui::TextWrapped("Guarda efectos, patrones, sonido del bajo y estado de reproducción de instrumentos. Las muestras de pads, pistas cargadas y audio del looper no cambian.");
    ImGui::Checkbox("Lanzar escenas en el siguiente compás",&sceneQuant_);
    ImGui::InputText("Nombre al guardar",sceneName_,sizeof sceneName_);
    if (ImGui::BeginTable("scenes",3,ImGuiTableFlags_SizingStretchProp)) {
        for (int i=0;i<int(scenes_.size());++i) {
            ImGui::PushID(i); ImGui::TableNextRow(); ImGui::TableNextColumn();
            ImGui::Text("%d · %s%s",i+1,scenes_[i].name.empty()?"Vacía":scenes_[i].name.c_str(),pendingScene_==i?" · PREPARADA":"");
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(scenes_[i].rack.empty());
            if (ImGui::Button("Lanzar")) sceneLaunch(i);
            ImGui::EndDisabled();
            if (ImGui::BeginPopupContextItem("sceneMidi")) {
                if (ImGui::MenuItem("Aprender botón MIDI")) { midi_.startLearn(MA_Scene1+i,0); rackLearning_=MA_Scene1+i; }
                ImGui::EndPopup();
            }
            ImGui::TableNextColumn();
            if (ImGui::Button("Guardar")) {
                if (scenes_[i].rack.empty()) sceneSave(i); else ImGui::OpenPopup("replace");
            }
            if (ImGui::BeginPopup("replace")) {
                ImGui::TextUnformatted("¿Sustituir esta escena?");
                if (ImGui::Button("Sustituir")) { sceneSave(i); ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Borrar")) ImGui::OpenPopup("delete");
            if (ImGui::BeginPopup("delete")) {
                ImGui::TextUnformatted("¿Borrar esta escena?");
                if (ImGui::Button("Borrar escena")) {
                    if (pendingScene_==i) { pendingScene_=-1; sceneLaunch_.cancel(); }
                    scenes_[i]={}; saveRack(); ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (sceneLaunch_.armed() && ImGui::Button("Cancelar escena preparada")) { sceneLaunch_.cancel(); pendingScene_=-1; }
    ImGui::Separator();
    ImGui::TextUnformatted("TRANSICIÓN DE MACRO");
    ImGui::TextWrapped("Mueve la macro del rack hasta el destino en 8, 16 o 32 compases. Primero asigna la macro a intensidad o mezcla de las ranuras. No mueve los faders de los platos.");
    const bool busy=transitionRunning_ || transitionLaunch_.armed();
    ImGui::BeginDisabled(busy);
    for (int bars : {8,16,32}) { if(bars!=8) ImGui::SameLine(); const auto text=std::to_string(bars)+" compases"; ImGui::RadioButton(text.c_str(),&transitionBars_,bars); }
    ImGui::SliderFloat("Destino macro",&transitionTarget_,0.f,1.f,"%.2f");
    if (ImGui::Button("Preparar transición")) {
        double bt=0; int d=0;
        if (!motionBeat(d,bt)) d=1;
        if (!motionBeat(d,bt)) notify("Debe sonar un plato con BPM.",true);
        else {
            motion_.state=Motion::Parado;
            if (motion_.xyOwned && !rackMouseXy_ && !rackMidiTouchHeld_) engine_.rack.params.xyTouch=false;
            motion_.xyOwned=false;
            transitionDeck_=d; transitionLaunch_.arm(bt);
        }
    }
    ImGui::EndDisabled();
    if (busy) {
        ImGui::SameLine(); if (ImGui::Button("Detener transición")) { transitionRunning_=false; transitionLaunch_.cancel(); }
        ImGui::TextUnformatted(transitionLaunch_.armed()?"Esperando el compás…":"Transición en marcha. Mover la macro a mano la detiene.");
    }
    ImGui::Separator();
    ImGui::TextWrapped("Cuantización al compás evaluada por la interfaz; no tiene precisión de muestra. Los saltos rearman escenas pendientes. QUITAR FX cancela las automatizaciones pendientes.");
    ImGui::End();
}
} // namespace rdj

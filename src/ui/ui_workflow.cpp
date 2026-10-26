#include "app.h"
#include "ui_common.h"
#include "covers.h"
#include "winutil.h"
#include "../core/util.h"
#include <cstdio>
#include <filesystem>

namespace rdj {
using namespace ui;
using namespace uic;

std::wstring App::recordingFolder() const {
    return settings_.recFolder.empty() ? recordingsDir() : fromUtf8(settings_.recFolder);
}
void App::recordingTick() {
    if (!recorder_.active() || !recordingTracklist_) return;
    for (int d = 0; d < 2; ++d) {
        const bool playing = engine_.deckStatus[d].playing.load();
        const auto path = toUtf8(deck_[d].path);
        if (playing && (!recLastPlaying_[d] || recLastPath_[d] != path)) {
            recLog_.push_back(formatTime(recorder_.seconds(), true) + " | Deck " + (d ? "B" : "A") + " | " + deck_[d].artist + " - " + deck_[d].title);
        }
        recLastPlaying_[d] = playing; recLastPath_[d] = path;
    }
}
void App::drawRecordingDialog() {
    ImGui::SetNextWindowSize(ImVec2(590*S(), 0), ImGuiCond_Appearing);
    if (!ImGui::Begin("Grabación de la sesión", &showRecording_, ImGuiWindowFlags_AlwaysAutoResize)) { ImGui::End(); return; }
    const bool active = recorder_.active();
    ImGui::TextWrapped("Destino: %s", toUtf8(recordingFolder()).c_str());
    ImGui::BeginDisabled(active);
    if (ImGui::Button("Elegir carpeta…")) { auto folder = win::pickFolder(hwnd_); if (!folder.empty()) { settings_.recFolder = toUtf8(folder); settings_.save(); } }
    ImGui::SameLine();
    if (ImGui::Button("Carpeta de Música")) { settings_.recFolder.clear(); settings_.save(); }
    char name[256]; std::snprintf(name, sizeof name, "%s", settings_.recName.c_str());
    if (ImGui::InputText("Nombre de sesión", name, sizeof name)) settings_.recName = name;
    const char* formats[] = {"WAV 16 bits", "WAV 32 bits flotantes"};
    ImGui::Combo("Formato", &settings_.recFormat, formats, 2);
    ImGui::Checkbox("Guardar listado de temas y tiempos (.txt)", &settings_.recTracklist);
    ImGui::TextWrapped("Se añade la fecha y hora al nombre. El listado registra reproducciones de los decks, incluso si su fader está cerrado.");
    ImGui::EndDisabled();
    std::error_code ec; const auto space = std::filesystem::space(recordingFolder(), ec);
    if (!ec) ImGui::Text("Espacio disponible: %.1f GB", space.available / 1e9);
    if (active) {
        ImGui::Separator();
        ImGui::Text("REC %s · aproximadamente %.1f MB", formatTime(recorder_.seconds()).c_str(), recorder_.framesWritten()*2.0*(recordingFormat_ == 0 ? 2 : 4)/1e6);
        ImGui::TextWrapped("Archivo: %s", toUtf8(recorder_.path()).c_str());
        if (engine_.status.recDropped.load()) ImGui::TextColored(toVec4(col::Red), "Aviso: el disco no está siguiendo el ritmo de grabación.");
        if (!recorder_.error().empty()) ImGui::TextColored(toVec4(col::Red), "%s", recorder_.error().c_str());
    }
    if (ImGui::Button(active ? "Detener y guardar" : "Iniciar grabación")) { settings_.save(); if (active) stopRecording(); else startRecording(); }
    ImGui::SameLine(); if (ImGui::Button("Abrir carpeta")) win::openFolder(recordingFolder());
    ImGui::End();
}

void App::drawPerformance(int d) {
    auto& p = engine_.deck[d]; const float s = S();
    const float w = ImGui::GetContentRegionAvail().x;
    const float bw = std::max(42*s,(w-4*5*s)/5);
    bool v = p.vinyl.load();
    if (NeonButton("VINILO",col::deck(d),v,ImVec2(bw,24*s))) p.vinyl = !v;
    ImGui::SameLine(0,5*s); bool slip = p.slip.load();
    if (NeonButton("SLIP",col::deck(d),slip,ImVec2(bw,24*s))) p.slip = !slip;
    ImGui::SetItemTooltip("Scratch, reverse y loops temporales: recupera el avance original al terminar.");
    ImGui::SameLine(0,5*s); bool rev = p.reverse.load();
    if (NeonButton("REV",col::deck(d),rev,ImVec2(bw,24*s))) p.reverse = !rev;
    ImGui::SameLine(0,5*s);
    static float duration[2] = {1,1};
    if (NeonButton("SPIN",col::Amber,false,ImVec2(bw,24*s))) send(d,Cmd::Backspin,duration[d]);
    ImGui::SameLine(0,5*s);
    if (NeonButton("FRENO",col::Amber,false,ImVec2(bw,24*s))) send(d,Cmd::Brake,duration[d]);
    const bool small = ImGui::GetContentRegionAvail().y < 32*s;
    if (small) {
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("motion-settings");
        if (!ImGui::BeginPopup("motion-settings")) return;
    }
    NeonButton("REV mantener",col::deck(d),reverseHeld_[d],ImVec2(w*0.33f,24*s));
    if (ImGui::IsItemActivated()) { reverseHeld_[d]=true; p.reverse=true; }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(w*0.35f);
    ImGui::SliderFloat("Duración",&duration[d],0.15f,4.f,"%.2f s");
    if (small) ImGui::EndPopup();
}

void App::drawLibraryTools() {
    ImGui::SetNextWindowSize(ImVec2(650*S(),550*S()),ImGuiCond_Appearing);
    if (!ImGui::Begin("Organización y sugerencias",&showLibraryTools_)) { ImGui::End(); return; }
    ImGui::SeparatorText("Mantenimiento y Relocalización Inteligente del PC");
    ImGui::TextWrapped("Si has movido carpetas de música o borrado archivos antiguos, pulsa aquí para buscar automáticamente todos los audios de tu PC, relocalizar las canciones movidas conservando tus Hot Cues, BPM, Tonalidad y Playlists, y limpiar los archivos borrados.");
    if (ImGui::Button("🔄 Buscar toda la música del PC y relocalizar movidos")) {
        const int n = library_.relocateAndPurgeMissing(true);
        library_.save();
        library_.scanPcMusic(true);
        ++srcGen_;
        notify("Relocalizando biblioteca y escaneando música del PC (" + std::to_string(n) + " ajustadas al instante)…");
    }
    ImGui::SameLine();
    if (ImGui::Button("🧹 Quitar no encontrados")) {
        const int n = library_.relocateAndPurgeMissing(true);
        library_.save();
        ++srcGen_;
        notify(n > 0 ? ("Limpiadas " + std::to_string(n) + " pistas no encontradas.") : "0 pistas no encontradas.");
    }
    ImGui::SeparatorText("Sugerencias de mezcla");
    bool changed=false;
    const char* decks[] = {"Automático", "Deck A", "Deck B"};
    changed |= ImGui::Combo("Referencia",&settings_.suggestDeck,decks,3);
    changed |= ImGui::SliderFloat("Margen BPM",&settings_.suggestBpmPercent,1,25,"±%.0f %%");
    changed |= ImGui::Checkbox("Solo tonalidades compatibles conocidas",&settings_.suggestKey);
    ImGui::TextWrapped("Ordenadas por cercanía de BPM y tonalidad. Los temas ya reproducidos hoy reciben menos prioridad. También se compara a medio/doble tempo.");
    if (ImGui::Button("Ver sugerencias")) selectSource(BrowseSrc{Src::Sugerencias});
    ImGui::SeparatorText("Playlists inteligentes");
    static char name[128]="Techno 130–138",tag[128]="Techno";
    static float mn=130,mx=138; static bool unplayed=false; static int minRating=0;
    ImGui::InputText("Nombre",name,sizeof name); ImGui::InputText("Etiqueta, género o texto",tag,sizeof tag);
    ImGui::DragFloatRange2("BPM",&mn,&mx,1,0,250,"%.0f","%.0f");
    ImGui::Checkbox("Solo sin reproducir",&unplayed);
    {
        const char* rl[] = {"Cualquier valoración", "1 estrella o más", "2 o más", "3 o más", "4 o más", "5 estrellas"};
        ImGui::Combo("Valoración", &minRating, rl, 6);
    }
    if (ImGui::Button("Crear lista inteligente") && name[0]) {
        settings_.smartLists.push_back({name,tag,std::min(mn,mx),std::max(mn,mx),unplayed,minRating});
        changed=true;
    }
    for (int i=0;i<(int)settings_.smartLists.size();++i) {
        ImGui::PushID(i);
        const auto& q=settings_.smartLists[i];
        if (ImGui::Button("Abrir")) selectSource(BrowseSrc{Src::Smart,L"",i});
        ImGui::SameLine(); ImGui::Text("%s · %.0f–%.0f BPM · %s%s",q.name.c_str(),q.minBpm,q.maxBpm,q.tag.c_str(),q.minRating?(" · "+std::to_string(q.minRating)+"+ estrellas").c_str():"");
        ImGui::SameLine();
        if (ImGui::SmallButton("Eliminar")) {
            settings_.smartLists.erase(settings_.smartLists.begin()+i);
            if (src_.kind==Src::Smart) selectSource(BrowseSrc{Src::Coleccion});
            changed=true; ImGui::PopID(); break;
        }
        ImGui::PopID();
    }
    if (!libSelectedPath_.empty()) {
        ImGui::SeparatorText("Etiquetas de la pista seleccionada");
        const auto key=toUtf8(libSelectedPath_);
        char tags[512]; std::snprintf(tags,sizeof tags,"%s",settings_.trackTags[key].c_str());
        if (ImGui::InputText("Etiquetas (separadas por comas)",tags,sizeof tags)) { settings_.trackTags[key]=tags; changed=true; }
        ImGui::TextWrapped("También puedes usar el menú derecho de las canciones para asignar etiquetas y colores a varias a la vez.");
    }
    if (changed) sortDirty_=true; // autosave in logic(); avoid disk I/O on every keystroke
    ImGui::End();
}
} // namespace rdj

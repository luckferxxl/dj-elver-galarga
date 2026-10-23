// Automix, recuperación de sesión, programador de eventos y paneles de
// información / estadísticas.
#include "app.h"
#include "widgets.h"
#include "winutil.h"
#include "../core/util.h"
#include <json.hpp>
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace rdj {

using namespace ui;
using json = nlohmann::json;

namespace {
float S() { return ImGui::GetFontSize() / 16.f; }
std::wstring sessionFile() { return appDataDir() + L"\\sesion.json"; }
std::wstring scheduleFile() { return appDataDir() + L"\\programador.json"; }
const char* kSchedActions[] = {"Iniciar grabación", "Detener grabación", "Iniciar Automix", "Detener Automix",
                               "Fundido final del master (15 s) y pausa", "Reproducir plato A", "Reproducir plato B"};
constexpr int kNumSchedActions = 7;
std::string today() {
    std::time_t t = std::time(nullptr);
    std::tm lt{};
    localtime_s(&lt, &t);
    char b[16];
    std::strftime(b, sizeof b, "%Y-%m-%d", &lt);
    return b;
}
} // namespace

// =================================================================== Automix

void App::setAutomix(bool on) {
    if (on == automixOn_) return;
    automixOn_ = on;
    automixPhase_ = 0;
    if (on) {
        const int list = ensurePlaylist("Automix");
        size_t n = 0;
        { std::lock_guard<std::recursive_mutex> lk(library_.mutex()); n = library_.playlists()[list].paths.size(); }
        if (n == 0) { automixOn_ = false; notify("La lista Automix está vacía: arrastra pistas a la pestaña Automix.", true); return; }
        if (automixIndex_ >= (int)n) automixIndex_ = 0;
        notify("Automix activado.");
    } else notify("Automix detenido.");
}

void App::automixTick() {
    if (!automixOn_) return;
    std::vector<std::wstring> paths;
    {
        const int list = ensurePlaylist("Automix");
        std::lock_guard<std::recursive_mutex> lk(library_.mutex());
        paths = library_.playlists()[list].paths;
    }
    if (paths.empty()) { setAutomix(false); return; }
    const bool p0 = engine_.deckStatus[0].playing.load(), p1 = engine_.deckStatus[1].playing.load();

    // Sin nada sonando: arrancar la primera pista en el plato A.
    if (!p0 && !p1 && automixPhase_ == 0) {
        if (automixIndex_ >= (int)paths.size()) {
            if (!settings_.automixRepeat) { setAutomix(false); notify("Automix: fin de la lista."); return; }
            automixIndex_ = 0;
        }
        const std::wstring& first = paths[automixIndex_];
        if (deck_[0].path != first) {
            if (!loader_.busy(0)) loadToDeck(0, first, true);
            return;
        }
        if (!deck_[0].track) return;
        engine_.mixer.crossfader = 0.f;
        send(0, Cmd::Seek, std::max(0.0, deck_[0].meta.cueSec) * deck_[0].track->sampleRate);
        togglePlay(0);
        ++automixIndex_;
        return;
    }

    if (automixPhase_ == 0) {
        const int P = (p0 && (!p1 || engine_.mixer.crossfader.load() <= 0.5f)) ? 0 : 1;
        const int O = 1 - P;
        if (automixIndex_ >= (int)paths.size()) {
            if (!settings_.automixRepeat) return;  // la última pista termina sola
            automixIndex_ = 0;
        }
        const std::wstring& next = paths[automixIndex_];
        // Precarga en el otro plato (si no está sonando).
        if (deck_[O].path != next && !engine_.deckStatus[O].playing.load() && !loader_.busy(O)) { loadToDeck(O, next, true); return; }
        if (!deck_[O].track || deck_[O].path != next || !deck_[P].track) return;
        // ¿Empieza la transición? Duración en pulsos del plato que suena.
        const double bpm = engine_.effectiveBpm(P);
        double fade = bpm > 0 ? settings_.automixFadeBeats * 60.0 / bpm : 10.0;
        fade = std::clamp(fade, 3.0, 40.0);
        const double remaining = deckLenSec(P) - deckPosSec(P);
        if (remaining > fade) return;
        // Entra la siguiente: desde su primer tiempo de compás, sincronizada.
        double startSec = deck_[O].meta.cueSec > 0 ? deck_[O].meta.cueSec : (deck_[O].meta.bpm > 0 ? deck_[O].meta.gridAnchorSec : 0.0);
        send(O, Cmd::Seek, startSec * deck_[O].track->sampleRate);
        if (settings_.automixSync && deck_[O].meta.bpm > 0 && deck_[P].meta.bpm > 0) {
            if (deck_[P].sync) setSync(P, false);  // el que suena manda: el que entra le sigue
            setSync(O, true);
        }
        togglePlay(O);
        automixPhase_ = 1;
        automixFrom_ = P; automixTo_ = O;
        automixStart_ = now_; automixDur_ = std::min(fade, std::max(1.0, remaining));
        automixXfFrom_ = engine_.mixer.crossfader.load();
        return;
    }
    // Transición: mover el crossfader hacia el plato que entra.
    const double t = std::clamp((now_ - automixStart_) / automixDur_, 0.0, 1.0);
    const float target = automixTo_ == 0 ? 0.f : 1.f;
    engine_.mixer.crossfader = automixXfFrom_ + (target - automixXfFrom_) * (float)(t * t * (3 - 2 * t));
    if (t >= 1.0) {
        send(automixFrom_, Cmd::Pause);
        deck_[automixTo_].sync = false;  // el plato que queda manda; conserva su tempo
        automixPhase_ = 0;
        ++automixIndex_;
    }
}

// =================================================================== sesión

void App::saveSession(bool clean) {
    json j;
    j["clean"] = clean;
    j["automixIndex"] = automixIndex_;
    json decks = json::array();
    for (int d = 0; d < kNumDecks; ++d) {
        const DeckParams& p = engine_.deck[d];
        json jd;
        // Si una pista recuperada aún se está cargando, se conserva lo que había.
        const bool pendingRestore = deck_[d].restorePos >= 0 && !deck_[d].restorePath.empty();
        jd["path"] = toUtf8(pendingRestore ? deck_[d].restorePath : deck_[d].path);
        jd["pos"] = pendingRestore ? deck_[d].restorePos : (deck_[d].track ? deckPosSec(d) : 0.0);
        jd["pitch"] = deck_[d].pitch;
        jd["keyLock"] = p.keyLock.load();
        jd["keyShift"] = p.keyShift.load();
        jd["fader"] = p.fader.load(); jd["gain"] = p.gainDb.load();
        jd["eq"] = {p.eqLow.load(), p.eqMid.load(), p.eqHigh.load()};
        jd["filter"] = p.filter.load();
        jd["pfl"] = p.pfl.load();
        jd["fx"] = p.fxType.load(); jd["fxAmount"] = p.fxAmount.load();
        decks.push_back(jd);
    }
    j["decks"] = decks;
    j["crossfader"] = engine_.mixer.crossfader.load();
    const std::wstring tmp = sessionFile() + L".tmp";
    {
        std::ofstream f(std::filesystem::path(tmp), std::ios::binary | std::ios::trunc);
        if (!f) return;
        f << j.dump(1);
    }
    MoveFileExW(tmp.c_str(), sessionFile().c_str(), MOVEFILE_REPLACE_EXISTING);
}

void App::restoreSession() {
    std::ifstream f(std::filesystem::path(sessionFile()), std::ios::binary);
    if (!f) return;
    try {
        json j; f >> j;
        const bool clean = j.value("clean", true);
        automixIndex_ = j.value("automixIndex", 0);
        engine_.mixer.crossfader = j.value("crossfader", 0.5f);
        auto& decks = j["decks"];
        for (int d = 0; d < kNumDecks && d < (int)decks.size(); ++d) {
            const json& jd = decks[d];
            DeckParams& p = engine_.deck[d];
            deck_[d].pitch = jd.value("pitch", 0.f);
            p.pitch = deck_[d].pitch;
            p.keyLock = jd.value("keyLock", false);
            p.keyShift = jd.value("keyShift", 0.f);
            p.fader = jd.value("fader", 0.8f); p.gainDb = jd.value("gain", 0.f);
            if (jd.contains("eq") && jd["eq"].size() == 3) { p.eqLow = jd["eq"][0].get<float>(); p.eqMid = jd["eq"][1].get<float>(); p.eqHigh = jd["eq"][2].get<float>(); }
            p.filter = jd.value("filter", 0.f);
            p.pfl = jd.value("pfl", false);
            p.fxType = jd.value("fx", 0); p.fxAmount = jd.value("fxAmount", 0.5f);
            const std::wstring path = fromUtf8(jd.value("path", ""));
            if (!path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
                deck_[d].restorePath = path;
                deck_[d].restorePos = jd.value("pos", 0.0);
                loadToDeck(d, path, true);
            }
        }
        sessionRecovered_ = true;
        if (!clean) notify("DJ Elver Galarga no se cerró bien la última vez: se ha recuperado la sesión (platos en pausa).", true);
    } catch (...) {
    }
}

void App::dumpState(const std::wstring& file) {
    std::ofstream f(std::filesystem::path(file), std::ios::binary | std::ios::trunc);
    for (int d = 0; d < kNumDecks; ++d) {
        f << "plato " << d << " | " << toUtf8(deck_[d].path) << " | pos " << deckPosSec(d) << " | pitch " << deck_[d].pitch
          << " | keylock " << engine_.deck[d].keyLock.load() << " | fader " << engine_.deck[d].fader.load() << "\r\n";
    }
    f << "crossfader " << engine_.mixer.crossfader.load() << "\r\n";
}

// =================================================================== programador

void App::loadSchedule() {
    std::ifstream f(std::filesystem::path(scheduleFile()), std::ios::binary);
    if (!f) return;
    try {
        json j; f >> j;
        schedule_.clear();
        for (auto& e : j.value("eventos", json::array())) {
            SchedEvent ev;
            ev.hour = e.value("hora", 22); ev.minute = e.value("minuto", 0);
            ev.action = std::clamp(e.value("accion", 0), 0, kNumSchedActions - 1);
            ev.enabled = e.value("activo", true); ev.lastFired = e.value("ultimo", "");
            schedule_.push_back(ev);
        }
    } catch (...) {
    }
}

void App::saveSchedule() {
    json arr = json::array();
    for (auto& e : schedule_) arr.push_back({{"hora", e.hour}, {"minuto", e.minute}, {"accion", e.action}, {"activo", e.enabled}, {"ultimo", e.lastFired}});
    json j; j["eventos"] = arr;
    std::ofstream f(std::filesystem::path(scheduleFile()), std::ios::binary | std::ios::trunc);
    f << j.dump(1);
}

void App::schedulerTick() {
    // Fundido final en curso
    if (fadeOutStart_ >= 0) {
        const double t = (now_ - fadeOutStart_) / 15.0;
        if (t >= 1.0) {
            send(0, Cmd::Pause); send(1, Cmd::Pause);
            engine_.mixer.masterVol = fadeOutFrom_;
            fadeOutStart_ = -1;
            notify("Programador: fundido final terminado.");
        } else engine_.mixer.masterVol = fadeOutFrom_ * (float)(1.0 - t);
    }
    static double lastCheck = 0;
    if (now_ - lastCheck < 1.0) return;
    lastCheck = now_;
    std::time_t tt = std::time(nullptr);
    std::tm lt{};
    localtime_s(&lt, &tt);
    const std::string day = today();
    bool changed = false;
    for (auto& e : schedule_) {
        if (!e.enabled || e.lastFired == day || e.hour != lt.tm_hour || e.minute != lt.tm_min) continue;
        e.lastFired = day;
        changed = true;
        notify(std::string("Programador: ") + kSchedActions[e.action]);
        switch (e.action) {
        case 0: if (!recorder_.active()) startRecording(); break;
        case 1: stopRecording(); break;
        case 2: setAutomix(true); break;
        case 3: setAutomix(false); break;
        case 4: if (fadeOutStart_ < 0) { fadeOutStart_ = now_; fadeOutFrom_ = engine_.mixer.masterVol.load(); setAutomix(false); } break;
        case 5: case 6: {
            const int d = e.action - 5;
            if (!deck_[d].track || engine_.deckStatus[d].playing.load()) break;
            // Si el plato está al final (p. ej. tras el Automix), suena desde su CUE y no solo el último instante.
            if (deckLenSec(d) - deckPosSec(d) < 1.0) send(d, Cmd::Seek, std::max(0.0, deck_[d].meta.cueSec) * deck_[d].track->sampleRate);
            togglePlay(d);
            break;
        }
        }
    }
    if (changed) saveSchedule();
}

void App::drawSchedulerDialog() {
    const float s = S();
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620 * s, 420 * s), ImGuiCond_Appearing);
    if (!ImGui::Begin("Programador de eventos", &showScheduler_, ImGuiWindowFlags_NoCollapse)) { ImGui::End(); return; }
    ImGui::TextWrapped("Acciones automáticas a una hora del día (una vez al día). La aplicación tiene que estar abierta.");
    ImGui::Spacing();
    int remove = -1;
    bool changed = false;
    if (ImGui::BeginTable("eventos", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Activo", ImGuiTableColumnFlags_WidthFixed, 60 * s);
        ImGui::TableSetupColumn("Hora", ImGuiTableColumnFlags_WidthFixed, 150 * s);
        ImGui::TableSetupColumn("Acción");
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 40 * s);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < schedule_.size(); ++i) {
            SchedEvent& e = schedule_[i];
            ImGui::PushID((int)i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            changed |= ImGui::Checkbox("##on", &e.enabled);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(60 * s);
            changed |= ImGui::InputInt("##h", &e.hour, 0);
            ImGui::SameLine(); ImGui::TextUnformatted(":"); ImGui::SameLine();
            ImGui::SetNextItemWidth(60 * s);
            changed |= ImGui::InputInt("##m", &e.minute, 0);
            e.hour = std::clamp(e.hour, 0, 23); e.minute = std::clamp(e.minute, 0, 59);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            changed |= ImGui::Combo("##a", &e.action, kSchedActions, kNumSchedActions);
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("×")) remove = (int)i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (remove >= 0) { schedule_.erase(schedule_.begin() + remove); changed = true; }
    if (NeonButton("+ Añadir evento", col::Violet, false, ImVec2(160 * s, 28 * s))) {
        std::time_t tt = std::time(nullptr);
        std::tm lt{};
        localtime_s(&lt, &tt);
        SchedEvent ev;
        ev.hour = lt.tm_hour; ev.minute = (lt.tm_min + 5) % 60;
        if (lt.tm_min + 5 >= 60) ev.hour = (ev.hour + 1) % 24;
        schedule_.push_back(ev);
        changed = true;
    }
    if (changed) saveSchedule();
    ImGui::End();
}

// =================================================================== paneles inferiores

void App::drawAutomixPanel() {
    const float s = S();
    const int list = ensurePlaylist("Automix");
    if (IconTextButton("##am", automixOn_ ? Ic::Stop : Ic::Play, automixOn_ ? "DETENER AUTOMIX" : "INICIAR AUTOMIX", col::Green, automixOn_, ImVec2(ImGui::GetContentRegionAvail().x, 30 * s)))
        setAutomix(!automixOn_);
    ImGui::SetNextItemWidth(120 * s);
    const int beats[] = {4, 8, 16, 32, 64};
    const char* bn[] = {"4 pulsos", "8 pulsos", "16 pulsos", "32 pulsos", "64 pulsos"};
    int bi = 2;
    for (int i = 0; i < 5; ++i) if (settings_.automixFadeBeats == beats[i]) bi = i;
    if (ImGui::Combo("fundido##amf", &bi, bn, 5)) settings_.automixFadeBeats = beats[bi];
    ImGui::SameLine();
    ImGui::Checkbox("sincronizar##ams", &settings_.automixSync);
    ImGui::SameLine();
    ImGui::Checkbox("repetir##amr", &settings_.automixRepeat);
    if (automixOn_ && automixPhase_ == 1) ImGui::TextColored(toVec4(col::Green), "Transición %s → %s…", automixFrom_ ? "B" : "A", automixTo_ ? "B" : "A");
    std::lock_guard<std::recursive_mutex> lk(library_.mutex());
    auto& pl = library_.playlists()[list];
    if (ImGui::BeginTable("amtabla", 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp, ImVec2(0, -1))) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 26 * s);
        ImGui::TableSetupColumn("Pista");
        ImGui::TableSetupColumn("BPM", ImGuiTableColumnFlags_WidthFixed, 40 * s);
        int removeAt = -1, moveFrom = -1, moveTo = -1;
        for (int i = 0; i < (int)pl.paths.size(); ++i) {
            const LibTrack* lt = library_.find(pl.paths[i]);
            ImGui::TableNextRow();
            if (i == automixIndex_) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, col::withAlpha(col::Green, 50));
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            ImGui::TextDisabled(i < automixIndex_ ? "✓" : "%d", i + 1);
            ImGui::TableNextColumn();
            const std::string name = lt ? (lt->artist.empty() ? lt->title : lt->artist + " — " + lt->title) : toUtf8(fileNameOf(pl.paths[i]));
            ImGui::Selectable(name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
            if (ImGui::BeginDragDropSource()) { ImGui::SetDragDropPayload("RDJ_AMPOS", &i, sizeof(int)); ImGui::TextUnformatted(name.c_str()); ImGui::EndDragDropSource(); }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("RDJ_AMPOS")) { moveFrom = *(const int*)p->Data; moveTo = i; }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Siguiente en sonar")) automixIndex_ = i;
                if (ImGui::MenuItem("Quitar")) removeAt = i;
                ImGui::EndPopup();
            }
            ImGui::TableNextColumn();
            if (lt && lt->meta.bpm > 0) ImGui::Text("%.0f", lt->meta.bpm);
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (moveFrom >= 0 && moveTo >= 0 && moveFrom != moveTo) {
            auto item = pl.paths[moveFrom];
            pl.paths.erase(pl.paths.begin() + moveFrom);
            pl.paths.insert(pl.paths.begin() + moveTo, item);
            library_.markDirty();
        }
        if (removeAt >= 0) {
            pl.paths.erase(pl.paths.begin() + removeAt);
            if (removeAt < automixIndex_) --automixIndex_;
            library_.markDirty();
        }
    }
    if (pl.paths.empty()) CenterText(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), col::TextDim, "Arrastra aquí las pistas para el Automix");
}

void App::drawStatsPanel() {
    const float s = S();
    const LibraryStats st = library_.stats();
    char b[128];
    std::snprintf(b, sizeof b, "%zu pistas · %.1f horas de música · %zu reproducidas", st.tracks, st.totalSec / 3600.0, st.played);
    ImGui::TextUnformatted(b);
    ImGui::TextDisabled("Analizadas: %zu con BPM, %zu con tonalidad · %zu no encontradas", st.analyzed, st.withKey, st.missing);
    // Histograma de BPM
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::TextDisabled("BPM");
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x, h = 60 * s;
    int mx = 1;
    for (int v : st.bpmHist) mx = std::max(mx, v);
    const float bw = w / 13.f;
    for (int i = 0; i < 13; ++i) {
        const float bh = h * st.bpmHist[i] / (float)mx;
        dl->AddRectFilled(ImVec2(p.x + i * bw + 2, p.y + h - bh), ImVec2(p.x + (i + 1) * bw - 2, p.y + h), col::withAlpha(col::DeckA, 200), 2.f);
        if (i % 2 == 0) { std::snprintf(b, sizeof b, "%d", 60 + i * 10); dl->AddText(ImVec2(p.x + i * bw + 2, p.y + h + 1), col::TextDim, b); }
    }
    ImGui::Dummy(ImVec2(w, h + 18 * s));
    if (st.withKey > 0) {
        // Tonalidades en orden Camelot (1A..12A, 1B..12B)
        ImGui::TextDisabled("Tonalidades (Camelot, estimadas)");
        const ImVec2 q = ImGui::GetCursorScreenPos();
        int kmx = 1;
        for (int v : st.keyHist) kmx = std::max(kmx, v);
        const float kw = w / 24.f, kh = 40 * s;
        int slot = 0;
        for (int minor = 1; minor >= 0; --minor)
            for (int n = 1; n <= 12; ++n, ++slot)
                for (int k = 0; k < 24; ++k)
                    if ((k >= 12) == (minor == 1) && std::atoi(keyCamelot(k)) == n) {
                        const float bh = kh * st.keyHist[k] / (float)kmx;
                        dl->AddRectFilled(ImVec2(q.x + slot * kw + 1, q.y + kh - bh), ImVec2(q.x + (slot + 1) * kw - 1, q.y + kh), col::withAlpha(minor ? col::Violet : col::DeckB, 200), 2.f);
                    }
        dl->AddText(ImVec2(q.x, q.y + kh + 1), col::TextDim, "1A");
        dl->AddText(ImVec2(q.x + 12 * kw, q.y + kh + 1), col::TextDim, "1B");
        ImGui::Dummy(ImVec2(w, kh + 18 * s));
    }
    if (!st.topPlayed.empty()) {
        ImGui::TextDisabled("Más reproducidas");
        std::lock_guard<std::recursive_mutex> lk(library_.mutex());
        for (auto& [path, n] : st.topPlayed) {
            const LibTrack* t = library_.find(path);
            ImGui::Text("%3d×  %s", n, t ? (t->artist + " — " + t->title).c_str() : toUtf8(fileNameOf(path)).c_str());
        }
    }
}

} // namespace rdj

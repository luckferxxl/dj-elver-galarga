// Instrumentos del rack (0.10): caja de ritmos, sintetizador de bajos y sampler de 16 pads.
// El audio está en src/audio/instruments.*; aquí, su interfaz, sus estilos y patrones de
// partida, las muestras de los pads y su estado guardado (dentro de rack.json).
#include "app.h"
#include "ui_common.h"
#include "widgets.h"
#include "winutil.h"
#include "../core/util.h"
#include <json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <future>
#include <filesystem>

namespace rdj {

using namespace ui;
using json = nlohmann::json;
using uic::ellipsize;

namespace {

float S() { return ImGui::GetFontSize() / 16.f; }

const char* kNoteEs[12] = {"Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si"};
const char* kPatName[8] = {"A", "B", "C", "D", "E", "F", "G", "H"};

// Estilos de la caja de ritmos: una línea por sonido ('x' golpe, 'X' acento, '.' nada).
struct DrumStyle { const char* name; int steps; float swing; const char* rows[kDrumTracks]; };
const DrumStyle kDrumStyles[] = {
    {"House", 16, 0.54f, {"X...x...X...x...", "................", "....x.......x...", "x.x.x.x.x.x.x.x.", "..x...x...x...x.", "................", "................", "................"}},
    {"Techno", 16, 0.5f, {"X...x...X...x...", "................", "....x.......x...", "x.xxx.xxx.xxx.xx", "..x...x...x...x.", "................", "...x.......x..x.", "................"}},
    {"Tech house", 16, 0.56f, {"X...x...X...x...", "................", "....x.......x...", "..x...x...x...x.", "..x...x...x...x.", "..........x.....", "x..x..x.........", "................"}},
    {"Breakbeat", 16, 0.52f, {"X......x..x.....", "....X.......X..x", "................", "x.x.x.x.x.x.x.x.", "..............x.", "................", "................", "................"}},
    {"Hip-hop", 16, 0.62f, {"X.......x.x.....", "....X.......X...", "................", "x.x.x.x.x.x.x.x.", "................", "................", "..........x.....", "................"}},
    {"Trap", 32, 0.5f, {"X.....x.........x.......x.......", "........X...............X.......", "........x...............x.......",
                        "x.x.x.x.x.x.x.xxx.x.x.x.xxxxx.x.", "................................", "...........................x.x..", "................................",
                        "X..............................."}},
    {"Drum&bass", 16, 0.5f, {"X.........x.....", "....X.......X...", "................", "x.x.x.x.x.x.x.x.", "......x.........", "................", "................", "................"}},
    {"Electro", 16, 0.5f, {"X.....x...x.....", "................", "....X.......X...", "x.x.x.x.x.x.x.x.", "................", "..........x..x..", "................", "................"}},
    {"Reguetón", 16, 0.5f, {"X...x...X...x...", "...x..x....x..x.", "................", "x.x.x.x.x.x.x.x.", "................", "................", "...x..x....x..x.", "................"}},
};

// Patrones de bajo: nota (semitonos sobre la tónica) o -1 = silencio; 'A' acento, 'S' slide.
struct BassPreset { const char* name; int notes[16]; const char* flags; };
const BassPreset kBassPresets[] = {
    {"Acid", {0, 0, 12, 0, 3, 0, 0, 7, 0, 10, 0, 12, 0, 5, 0, 7}, "..S.A.S..A..S..A"},
    {"Rodante", {-1, 0, 0, 0, -1, 0, 0, 7, -1, 0, 0, 0, -1, 0, 12, 0}, ".A...A...A...A.."},
    {"Offbeat house", {-1, -1, 0, -1, -1, -1, 0, -1, -1, -1, 0, -1, -1, -1, 12, -1}, "..A...A...A...A."},
    {"Electro", {0, -1, 0, 12, -1, 0, -1, 3, 0, -1, 7, -1, 5, -1, 3, -1}, "A..S.A..A.S.A..."},
    {"Minimal", {0, -1, -1, 0, -1, -1, 0, -1, -1, 0, -1, -1, 7, -1, 5, -1}, "A..........S...."},
};

bool Seg(const char* id, int* v, const char* const* items, int count, ImVec2 size, ImU32 accent, ImFont* font, float fs) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float s = S();
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), col::Bg, 5 * s);
    const float w = size.x / count;
    bool changed = false;
    ImGui::PushID(id);
    for (int i = 0; i < count; ++i) {
        const ImVec2 a(p.x + i * w, p.y), b(p.x + (i + 1) * w, p.y + size.y);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##s", ImVec2(w, size.y)) && *v != i) { *v = i; changed = true; }
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        if (*v == i) dl->AddRectFilled(ImVec2(a.x + 2 * s, a.y + 2 * s), ImVec2(b.x - 2 * s, b.y - 2 * s), col::mix(col::Panel2, accent, 0.35f), 4 * s);
        CenterText(dl, a, b, *v == i ? col::Text : hov ? col::mix(col::TextDim, col::Text, 0.5f) : col::TextDim, items[i], font, fs);
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + size.y));
    ImGui::Dummy(ImVec2(size.x, 0));
    ImGui::SetCursorScreenPos(ImVec2(p.x + size.x, p.y));
    return changed;
}

void Led(ImDrawList* dl, ImVec2 c, float r, ImU32 color, bool on) {
    if (on) {
        if (col::Glow > 0.2f) dl->AddCircleFilled(c, r * 2.f, col::withAlpha(color, (int)(70 * col::Glow)));
        dl->AddCircleFilled(c, r, color);
    } else {
        dl->AddCircleFilled(c, r, col::mix(col::Bg, color, 0.18f));
    }
}

} // namespace

// =================================================================== muestras de los pads

void App::setPad(int k, TrackPtr t, const std::string& name, const std::wstring& path, int builtin) {
    if (k < 0 || k >= kPads) return;
    PadSlot& ps = pads_[k];
    const uint32_t gen = ++padGenCounter_;
    if (ps.track) padRetire_.push_back({k, gen, ps.track});   // se libera cuando el audio confirma la nueva
    ps.track = std::move(t);
    ps.name = name; ps.path = path; ps.builtin = builtin;
    Command c{Cmd::RackPad, 0};
    c.i = k; c.d = 3; c.track = ps.track.get(); c.gen = gen;
    engine_.send(c);
}

void App::loadPadFile(int k, const std::wstring& path) {
    if (k < 0 || k >= kPads || path.empty()) return;
    pads_[k].loading = true;
    padLoads_.push_back({k, path, std::async(std::launch::async, [path] {
                             std::string err;
                             TrackPtr t = decodeFile(path, err);
                             return t;
                         })});
}

void App::padsTick() {
    // Muestras ya decodificadas
    for (size_t i = 0; i < padLoads_.size();) {
        auto& L = padLoads_[i];
        if (L.f.wait_for(std::chrono::seconds(0)) != std::future_status::ready) { ++i; continue; }
        TrackPtr t = L.f.get();
        pads_[L.pad].loading = false;
        if (!t) notify("No se pudo abrir la muestra del pad " + std::to_string(L.pad + 1) + ".", true);
        else if (t->lengthSec() > 120.0) notify("La muestra del pad " + std::to_string(L.pad + 1) + " dura más de 2 minutos: elige una más corta.", true);
        else setPad(L.pad, t, toUtf8(std::filesystem::path(L.path).stem().wstring()), L.path, -1);
        padLoads_.erase(padLoads_.begin() + i);
    }
    // Muestras viejas que el audio ya no usa
    for (size_t i = 0; i < padRetire_.size();) {
        const PadRetire& r = padRetire_[i];
        if (engine_.rack.instr.status.padGen[r.pad].load(std::memory_order_acquire) >= r.gen) padRetire_.erase(padRetire_.begin() + i);
        else ++i;
    }
}

void App::padCmd(int pad, int action) {
    Command c{Cmd::RackPad, 0};
    c.i = pad; c.d = action;
    engine_.send(c);
}

void App::initPads() {
    // Sonidos incluidos en los 8 primeros pads (los que no tengan otra cosa guardada).
    for (int k = 0; k < kFxSamples && k < kPads; ++k)
        if (!pads_[k].track && pads_[k].path.empty() && pads_[k].builtin < 0 && !pads_[k].cleared) setPad(k, makeFxSample(k, 48000), fxSampleName(k), L"", k);
}

// =================================================================== estado (rack.json)

std::string App::instrToJson() const {
    const InstrParams& p = engine_.rack.instr.params;
    json j;
    j["reloj"] = {{"sync", p.sync.load()}, {"bpm", p.ownBpm.load()}, {"entrarEnCompas", p.quantStart.load()}, {"volumen", p.instrVol.load()}, {"auriculares", p.instrCue.load()}};
    json pats = json::array();
    for (int pt = 0; pt < kDrumPatterns; ++pt) {
        json rows = json::object();
        const int steps = p.drumSteps[pt].load() == 32 ? 32 : 16;
        for (int t = 0; t < kDrumTracks; ++t) {
            std::string r;
            for (int k = 0; k < steps; ++k) { const int v = p.drum[pt][t][k].load(); r += v == 2 ? 'X' : v == 1 ? 'x' : '.'; }
            rows[drumVoiceId(t)] = r;
        }
        pats.push_back({{"pasos", steps}, {"filas", rows}});
    }
    json voices = json::object();
    for (int t = 0; t < kDrumTracks; ++t)
        voices[drumVoiceId(t)] = {{"nivel", p.dLevel[t].load()}, {"afinacion", p.dTune[t].load()}, {"caida", p.dDecay[t].load()}, {"silencio", p.dMute[t].load()}};
    j["ritmos"] = {{"patron", p.drumPattern.load()}, {"swing", p.swing.load()}, {"volumen", p.drumVol.load()}, {"patrones", pats}, {"sonidos", voices}};
    json bpats = json::array();
    for (int pt = 0; pt < kBassPatterns; ++pt) {
        json steps = json::array();
        for (int k = 0; k < kBassSteps; ++k) steps.push_back((int)p.bass[pt][k].load());
        bpats.push_back(steps);
    }
    j["bajo"] = {{"patron", p.bassPattern.load()}, {"tonica", p.bassRoot.load()}, {"octava", p.bassOct.load()}, {"onda", p.bassWave.load()},
                 {"corte", p.cutoff.load()}, {"resonancia", p.reso.load()}, {"envolvente", p.envMod.load()}, {"caida", p.decay.load()},
                 {"acento", p.accent.load()}, {"drive", p.drive.load()}, {"volumen", p.bassVol.load()}, {"patrones", bpats}};
    json pads = json::array();
    for (int k = 0; k < kPads; ++k) {
        const PadSlot& ps = pads_[k];
        json pj = {{"modo", p.padMode[k].load()}, {"volumen", p.padVol[k].load()}};
        if (ps.builtin >= 0) pj["incluido"] = ps.builtin;
        else if (!ps.path.empty()) { pj["ruta"] = toUtf8(ps.path); pj["nombre"] = ps.name; }
        else pj["vacio"] = true;
        pads.push_back(pj);
    }
    j["sampler"] = {{"volumen", p.samplerVol.load()}, {"loopsEnElPulso", p.padQuant.load()}, {"pads", pads}};
    return j.dump();
}

bool App::instrFromJson(const std::string& text) {
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return false;
    InstrParams& p = engine_.rack.instr.params;
    auto f01 = [](const json& o, const char* k, float def) { const float v = o.value(k, def); return std::isfinite(v) ? std::clamp(v, 0.f, 1.f) : def; };
    if (j.contains("reloj") && j["reloj"].is_object()) {
        const json& r = j["reloj"];
        p.sync = std::clamp(r.value("sync", 0), 0, 3);
        p.ownBpm = std::clamp(r.value("bpm", 124.f), 40.f, 220.f);
        p.quantStart = r.value("entrarEnCompas", true);
        p.instrVol = f01(r, "volumen", 0.8f);
        p.instrCue = r.value("auriculares", false);
    }
    if (j.contains("ritmos") && j["ritmos"].is_object()) {
        const json& d = j["ritmos"];
        p.drumPattern = std::clamp(d.value("patron", 0), 0, kDrumPatterns - 1);
        p.swing = std::clamp(d.value("swing", 0.5f), 0.5f, 0.75f);
        p.drumVol = f01(d, "volumen", 0.8f);
        if (d.contains("patrones") && d["patrones"].is_array())
            for (int pt = 0; pt < kDrumPatterns && pt < (int)d["patrones"].size(); ++pt) {
                const json& pj = d["patrones"][pt];
                if (!pj.is_object()) continue;
                p.drumSteps[pt] = pj.value("pasos", 16) == 32 ? 32 : 16;
                for (int t = 0; t < kDrumTracks; ++t) {
                    const std::string r = pj.contains("filas") && pj["filas"].is_object() ? pj["filas"].value(drumVoiceId(t), std::string()) : std::string();
                    for (int k = 0; k < kMaxSteps; ++k) p.drum[pt][t][k] = k < (int)r.size() ? (r[k] == 'X' ? 2 : r[k] == 'x' ? 1 : 0) : 0;
                }
            }
        if (d.contains("sonidos") && d["sonidos"].is_object())
            for (int t = 0; t < kDrumTracks; ++t) {
                if (!d["sonidos"].contains(drumVoiceId(t))) continue;
                const json& v = d["sonidos"][drumVoiceId(t)];
                p.dLevel[t] = f01(v, "nivel", 0.8f); p.dTune[t] = f01(v, "afinacion", 0.5f); p.dDecay[t] = f01(v, "caida", 0.5f); p.dMute[t] = v.value("silencio", false);
            }
    }
    if (j.contains("bajo") && j["bajo"].is_object()) {
        const json& b = j["bajo"];
        p.bassPattern = std::clamp(b.value("patron", 0), 0, kBassPatterns - 1);
        p.bassRoot = std::clamp(b.value("tonica", 9), 0, 11); p.bassOct = std::clamp(b.value("octava", 0), -1, 1); p.bassWave = b.value("onda", 0) ? 1 : 0;
        p.cutoff = f01(b, "corte", 0.3f); p.reso = f01(b, "resonancia", 0.6f); p.envMod = f01(b, "envolvente", 0.55f); p.decay = f01(b, "caida", 0.35f);
        p.accent = f01(b, "acento", 0.6f); p.drive = f01(b, "drive", 0.25f); p.bassVol = f01(b, "volumen", 0.7f);
        if (b.contains("patrones") && b["patrones"].is_array())
            for (int pt = 0; pt < kBassPatterns && pt < (int)b["patrones"].size(); ++pt)
                if (b["patrones"][pt].is_array())
                    for (int k = 0; k < kBassSteps && k < (int)b["patrones"][pt].size(); ++k)
                        p.bass[pt][k] = (int16_t)(b["patrones"][pt][k].is_number_integer() ? b["patrones"][pt][k].get<int>() & 0x7FFF : 0);
    }
    if (j.contains("sampler") && j["sampler"].is_object()) {
        const json& sm = j["sampler"];
        p.samplerVol = f01(sm, "volumen", 0.8f);
        p.padQuant = sm.value("loopsEnElPulso", true);
        if (sm.contains("pads") && sm["pads"].is_array())
            for (int k = 0; k < kPads && k < (int)sm["pads"].size(); ++k) {
                const json& pj = sm["pads"][k];
                if (!pj.is_object()) continue;
                p.padMode[k] = std::clamp(pj.value("modo", 0), 0, 2);
                p.padVol[k] = f01(pj, "volumen", 0.8f);
                const int inc = pj.value("incluido", -1);
                const std::string ruta = pj.value("ruta", std::string());
                PadSlot& ps = pads_[k];
                if (inc >= 0 && inc < kFxSamples) { if (ps.builtin != inc) setPad(k, makeFxSample(inc, 48000), fxSampleName(inc), L"", inc); }
                else if (!ruta.empty()) { if (ps.path != fromUtf8(ruta)) { ps.name = pj.value("nombre", std::string("Pad")); ps.path = fromUtf8(ruta); ps.builtin = -1; loadPadFile(k, ps.path); } }
                else if (pj.value("vacio", false) && (ps.track || ps.builtin >= 0)) { setPad(k, nullptr, "", L"", -1); ps.cleared = true; }
            }
    }
    return true;
}

void App::instrDemo() {
    InstrParams& p = engine_.rack.instr.params;
    const DrumStyle& ds = kDrumStyles[1];
    for (int t = 0; t < kDrumTracks; ++t)
        for (int k = 0; k < kMaxSteps; ++k) p.drum[0][t][k] = k < 16 && ds.rows[t][k] == 'X' ? 2 : k < 16 && ds.rows[t][k] == 'x' ? 1 : 0;
    const BassPreset& bp = kBassPresets[0];
    for (int k = 0; k < kBassSteps; ++k)
        p.bass[0][k] = (int16_t)(bp.notes[k] < 0 ? 0 : bp.notes[k] | kBassGate | (bp.flags[k] == 'A' ? kBassAccent : 0) | (bp.flags[k] == 'S' ? kBassSlide : 0));
    p.drumPattern = 0; p.bassPattern = 0;
    p.drumPlay = true; p.bassPlay = true;
}

// =================================================================== reloj (común a los tres)

void App::instrClockBar(ImVec2 a, float w, float h) {
    const float s = S();
    InstrParams& p = engine_.rack.instr.params;
    const InstrStatus& st = engine_.rack.instr.status;
    const int cd = st.clockDeck.load();
    char t[48];
    std::snprintf(t, sizeof t, "%s %.1f##ck", cd == 0 ? "A" : cd == 1 ? "B" : "RELOJ", st.bpm.load());
    ImGui::SetCursorScreenPos(a);
    if (NeonButton(t, cd == 0 ? col::DeckA : cd == 1 ? col::DeckB : col::Violet, false, ImVec2(w, h), true, fontHead, 12 * s)) ImGui::OpenPopup("relojinstr");
    ImGui::SetItemTooltip("Tempo de los instrumentos (%s). Clic: con qué plato van, tempo propio y volumen del bus",
                          cd == 0 ? "sigue al plato A" : cd == 1 ? "sigue al plato B" : "tempo propio");
    if (ImGui::BeginPopup("relojinstr")) {
        ImGui::PushFont(fontHead, 13.f);
        ImGui::TextDisabled("RELOJ DE LOS INSTRUMENTOS");
        ImGui::PopFont();
        int sync = p.sync.load();
        const char* modes[4] = {"El plato que manda (el que más suena)", "Plato A", "Plato B", "Tempo propio"};
        for (int i = 0; i < 4; ++i) if (ImGui::RadioButton(modes[i], sync == i)) p.sync = i;
        float bpm = p.ownBpm.load();
        ImGui::SetNextItemWidth(160 * s);
        if (ImGui::DragFloat("Tempo propio", &bpm, 0.1f, 40.f, 220.f, "%.1f BPM")) p.ownBpm = bpm;
        ImGui::TextDisabled("Siguiendo a un plato, los golpes van con su pulso (y su SYNC, su pitch y sus empujones).\nSi el plato se para, siguen solos a su tempo.");
        bool q = p.quantStart.load();
        if (ImGui::Checkbox("Arrancar en el siguiente compás", &q)) p.quantStart = q;
        ImGui::Separator();
        float v = p.instrVol.load();
        ImGui::SetNextItemWidth(160 * s);
        if (ImGui::SliderFloat("Volumen del bus", &v, 0.f, 1.f, "%.2f")) p.instrVol = v;
        bool cue = p.instrCue.load();
        if (ImGui::Checkbox("También por los auriculares", &cue)) p.instrCue = cue;
        ImGui::EndPopup();
    }
}

// =================================================================== caja de ritmos

void App::drawRackDrums(ImVec2 a, ImVec2 b) {
    const float s = S();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    InstrParams& p = engine_.rack.instr.params;
    const InstrStatus& st = engine_.rack.instr.status;
    const ImU32 acc = col::Amber;
    const float rowH = 22 * s;
    const bool blink = std::fmod(now_, 0.5) < 0.25;
    // Fila de arriba
    float x = a.x;
    ImGui::SetCursorScreenPos(ImVec2(x, a.y));
    const bool run = st.drumRunning.load(), armed = st.drumArmed.load();
    if (IconTextButton("##drplay", p.drumPlay.load() ? Ic::Stop : Ic::Play, p.drumPlay.load() ? "PARAR" : "TOCAR", col::Green, run || (armed && blink),
                       ImVec2(78 * s, rowH), true, fontHead, 12.5f * s))
        p.drumPlay = !p.drumPlay.load();
    ImGui::SetItemTooltip(armed ? "Espera al siguiente compás para entrar" : "Tocar o parar la caja de ritmos (entra en el compás)");
    rackMidiMenu("mididrplay", {MA_RackDrumPlay});
    x += 84 * s;
    ImGui::SetCursorScreenPos(ImVec2(x, a.y));
    int pat = p.drumPattern.load();
    if (Seg("drpat", &pat, kPatName, 8, ImVec2(152 * s, rowH), acc, fontHead, 12.5f * s)) p.drumPattern = pat;
    x += 158 * s;
    ImGui::SetCursorScreenPos(ImVec2(x, a.y));
    int stepsIdx = p.drumSteps[pat].load() == 32 ? 1 : 0;
    const char* stTxt[2] = {"16", "32"};
    if (Seg("drsteps", &stepsIdx, stTxt, 2, ImVec2(56 * s, rowH), acc, fontHead, 12.5f * s)) p.drumSteps[pat] = stepsIdx ? 32 : 16;
    x += 62 * s;
    ImGui::SetCursorScreenPos(ImVec2(x, a.y - 1 * s));
    float sw = (p.swing.load() - 0.5f) / 0.25f * 100.f;
    ImGui::SetNextItemWidth(90 * s);
    if (ImGui::SliderFloat("##swing", &sw, 0.f, 100.f, "SWING %.0f%%")) p.swing = 0.5f + sw / 100.f * 0.25f;
    ImGui::SetItemTooltip("Retrasa las semicorcheas pares (0 = recto, 67 %% = tresillo, 100 %% = máximo)");
    x += 96 * s;
    ImGui::SetCursorScreenPos(ImVec2(x, a.y));
    ImGui::SetNextItemWidth(108 * s);
    if (ImGui::BeginCombo("##drstyle", "Estilos…", ImGuiComboFlags_HeightLarge)) {
        for (const DrumStyle& ds : kDrumStyles)
            if (ImGui::Selectable(ds.name)) {
                p.drumSteps[pat] = ds.steps;
                for (int t = 0; t < kDrumTracks; ++t)
                    for (int k = 0; k < kMaxSteps; ++k) {
                        const char c = k < (int)std::strlen(ds.rows[t]) ? ds.rows[t][k] : '.';
                        p.drum[pat][t][k] = c == 'X' ? 2 : c == 'x' ? 1 : 0;
                    }
                p.swing = ds.swing;
            }
        ImGui::Separator();
        if (ImGui::Selectable("Vaciar este patrón")) for (int t = 0; t < kDrumTracks; ++t) for (int k = 0; k < kMaxSteps; ++k) p.drum[pat][t][k] = 0;
        if (ImGui::Selectable("Copiar al patrón siguiente")) {
            const int nx = (pat + 1) % kDrumPatterns;
            p.drumSteps[nx] = p.drumSteps[pat].load();
            for (int t = 0; t < kDrumTracks; ++t) for (int k = 0; k < kMaxSteps; ++k) p.drum[nx][t][k] = p.drum[pat][t][k].load();
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Cargar un estilo en el patrón %s (se puede editar después)", kPatName[pat]);
    x += 114 * s;
    if (b.x - 98 * s - x > 70 * s) {
        float v = p.drumVol.load();
        ImGui::SetCursorScreenPos(ImVec2(x, a.y - 1 * s));
        ImGui::SetNextItemWidth(std::min(90 * s, b.x - 98 * s - x));
        if (ImGui::SliderFloat("##drvol", &v, 0.f, 1.f, "VOL %.2f")) p.drumVol = v;
    }
    instrClockBar(ImVec2(b.x - 92 * s, a.y), 92 * s, rowH);
    // Rejilla de pasos
    const int steps = p.drumSteps[pat].load() == 32 ? 32 : 16;
    const float gy = a.y + rowH + 6 * s;
    const float nameW = 112 * s;
    const float rh = std::max(11.f * s, std::min(20 * s, (b.y - gy) / kDrumTracks));
    const float cw = (b.x - a.x - nameW) / steps;
    const bool playingThis = run && st.drumPatternNow.load() == pat;
    const int cur = playingThis ? st.drumStep.load() : -1;
    if (cur >= 0) dl->AddRectFilled(ImVec2(a.x + nameW + cur * cw, gy), ImVec2(a.x + nameW + (cur + 1) * cw, gy + rh * kDrumTracks), col::withAlpha(col::Text, 26));
    for (int t = 0; t < kDrumTracks; ++t) {
        ImGui::PushID(t);
        const float ry = gy + t * rh;
        // Nombre: clic para silenciar; clic derecho para nivel, afinación y caída.
        ImGui::SetCursorScreenPos(ImVec2(a.x, ry));
        const bool muted = p.dMute[t].load();
        static uint32_t lastHits[kDrumTracks] = {};
        static double flashAt[kDrumTracks] = {};
        const uint32_t hits = st.drumHits[t].load();
        if (hits != lastHits[t]) { lastHits[t] = hits; flashAt[t] = now_; }
        const float fl = (float)std::max(0.0, 1.0 - (now_ - flashAt[t]) / 0.15);
        ImGui::InvisibleButton("##name", ImVec2(nameW - 6 * s, rh - 1 * s));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) p.dMute[t] = !muted;
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("voz");
        ImGui::SetItemTooltip("Clic: silenciar o no · clic derecho: nivel, afinación y caída");
        Led(dl, ImVec2(a.x + 5 * s, ry + rh * 0.5f), 3 * s, acc, fl > 0.05f && !muted);
        dl->AddText(ImGui::GetFont(), std::min(12.f * s, rh * 0.85f), ImVec2(a.x + 13 * s, ry + (rh - std::min(12.f * s, rh * 0.85f)) * 0.5f),
                    muted ? col::withAlpha(col::TextDim, 120) : col::Text, drumVoiceName(t));
        if (ImGui::BeginPopup("voz")) {
            ImGui::PushFont(fontHead, 13.f);
            ImGui::TextUnformatted(drumVoiceName(t));
            ImGui::PopFont();
            float lv = p.dLevel[t].load(), tu = p.dTune[t].load(), dc = p.dDecay[t].load();
            ImGui::SetNextItemWidth(180 * s);
            if (ImGui::SliderFloat("nivel", &lv, 0.f, 1.f, "%.2f")) p.dLevel[t] = lv;
            ImGui::SetNextItemWidth(180 * s);
            if (ImGui::SliderFloat("afinación", &tu, 0.f, 1.f, "%.2f")) p.dTune[t] = tu;
            ImGui::SetNextItemWidth(180 * s);
            if (ImGui::SliderFloat("caída", &dc, 0.f, 1.f, "%.2f")) p.dDecay[t] = dc;
            if (ImGui::Button("Valores normales")) { p.dLevel[t] = 0.8f; p.dTune[t] = 0.5f; p.dDecay[t] = 0.5f; }
            ImGui::EndPopup();
        }
        for (int k = 0; k < steps; ++k) {
            const ImVec2 ca(a.x + nameW + k * cw + 1 * s, ry + 1 * s), cb(a.x + nameW + (k + 1) * cw - 1 * s, ry + rh - 1 * s);
            const int v = p.drum[pat][t][k].load();
            const bool beatCol = (k / 4) % 2 == 0;
            ImU32 fill = beatCol ? col::Panel2 : col::mix(col::Panel2, col::Bg, 0.5f);
            if (v == 1) fill = col::mix(col::Panel2, acc, 0.65f);
            if (v == 2) fill = acc;
            if (muted && v) fill = col::mix(fill, col::Bg, 0.6f);
            dl->AddRectFilled(ca, cb, fill, 2 * s);
            ImGui::SetCursorScreenPos(ca);
            ImGui::PushID(k);
            ImGui::InvisibleButton("##c", ImVec2(std::max(1.f, cb.x - ca.x), std::max(1.f, cb.y - ca.y)));
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) p.drum[pat][t][k] = ImGui::GetIO().KeyShift ? (v == 2 ? 1 : 2) : (v ? 0 : 1);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) p.drum[pat][t][k] = v == 2 ? 1 : 2;
            if (ImGui::IsItemHovered()) { dl->AddRect(ca, cb, col::Text, 2 * s); ImGui::SetTooltip("Clic: poner o quitar · clic derecho (o Mayús): acento"); }
            ImGui::PopID();
        }
        ImGui::PopID();
    }
}

// =================================================================== bajo

void App::drawRackBass(ImVec2 a, ImVec2 b) {
    const float s = S();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    InstrParams& p = engine_.rack.instr.params;
    const InstrStatus& st = engine_.rack.instr.status;
    const ImU32 acc = col::Green;
    const float rowH = 22 * s;
    const bool blink = std::fmod(now_, 0.5) < 0.25;
    float x = a.x;
    ImGui::SetCursorScreenPos(ImVec2(x, a.y));
    const bool run = st.bassRunning.load(), armed = st.bassArmed.load();
    if (IconTextButton("##bsplay", p.bassPlay.load() ? Ic::Stop : Ic::Play, p.bassPlay.load() ? "PARAR" : "TOCAR", col::Green, run || (armed && blink),
                       ImVec2(78 * s, rowH), true, fontHead, 12.5f * s))
        p.bassPlay = !p.bassPlay.load();
    rackMidiMenu("midibsplay", {MA_RackBassPlay});
    x += 84 * s;
    ImGui::SetCursorScreenPos(ImVec2(x, a.y));
    int pat = p.bassPattern.load();
    if (Seg("bspat", &pat, kPatName, 8, ImVec2(152 * s, rowH), acc, fontHead, 12.5f * s)) p.bassPattern = pat;
    x += 158 * s;
    ImGui::SetCursorScreenPos(ImVec2(x, a.y));
    int wave = p.bassWave.load();
    const char* waves[2] = {"SIERRA", "CUADR."};
    if (Seg("bswave", &wave, waves, 2, ImVec2(100 * s, rowH), acc, fontHead, 11.5f * s)) p.bassWave = wave;
    x += 106 * s;
    ImGui::SetCursorScreenPos(ImVec2(x, a.y));
    ImGui::SetNextItemWidth(62 * s);
    int root = p.bassRoot.load();
    if (ImGui::BeginCombo("##root", kNoteEs[root])) {
        for (int i = 0; i < 12; ++i) if (ImGui::Selectable(kNoteEs[i], i == root)) p.bassRoot = i;
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Tónica del bajo");
    ImGui::SameLine(0, 4 * s);
    {
        // Tónica del plato que suena (su tonalidad, con el pitch y Tono aplicados).
        int d = engine_.rack.instr.status.clockDeck.load();
        if (d < 0) d = engine_.deckStatus[0].playing.load() ? 0 : engine_.deckStatus[1].playing.load() ? 1 : 0;
        const int key = deck_[d].track ? soundingKey(d) : -1;
        if (NeonButton("TONO##tp", acc, false, ImVec2(52 * s, rowH), key >= 0, fontHead, 11.5f * s)) p.bassRoot = key % 12;
        ImGui::SetItemTooltip(key >= 0 ? "Poner la tónica del plato %s (%s)" : "El plato no tiene tonalidad", d ? "B" : "A", key >= 0 ? keyText(key).c_str() : "");
    }
    ImGui::SameLine(0, 4 * s);
    int oct = p.bassOct.load() + 1;
    const char* octs[3] = {"-1", "0", "+1"};
    if (Seg("bsoct", &oct, octs, 3, ImVec2(66 * s, rowH), acc, fontHead, 12 * s)) p.bassOct = oct - 1;
    x = ImGui::GetCursorScreenPos().x + 6 * s;
    ImGui::SetCursorScreenPos(ImVec2(x, a.y));
    if (b.x - 98 * s - x > 92 * s) {
        ImGui::SetNextItemWidth(92 * s);
        if (ImGui::BeginCombo("##bspre", "Patrones…", ImGuiComboFlags_HeightLarge)) {
            for (const BassPreset& bp : kBassPresets)
                if (ImGui::Selectable(bp.name))
                    for (int k = 0; k < kBassSteps; ++k) {
                        int v = 0;
                        if (bp.notes[k] >= 0) v = bp.notes[k] | kBassGate | (bp.flags[k] == 'A' ? kBassAccent : 0) | (bp.flags[k] == 'S' ? kBassSlide : 0);
                        p.bass[pat][k] = (int16_t)v;
                    }
            ImGui::Separator();
            if (ImGui::Selectable("Vaciar este patrón")) for (int k = 0; k < kBassSteps; ++k) p.bass[pat][k] = 0;
            ImGui::EndCombo();
        }
    }
    instrClockBar(ImVec2(b.x - 92 * s, a.y), 92 * s, rowH);
    // Ruletas
    const float ky = a.y + rowH + 6 * s;
    const float kr = std::clamp((b.y - ky - 70 * s) * 0.5f, 11 * s, 17 * s);
    struct K { const char* l; std::atomic<float>* v; float def; const char* tip; };
    const K knobs[7] = {{"CORTE", &p.cutoff, 0.3f, "Frecuencia del filtro"}, {"RESON.", &p.reso, 0.6f, "Resonancia"},
                        {"ENV", &p.envMod, 0.55f, "Cuánto abre el filtro cada nota"}, {"CAÍDA", &p.decay, 0.35f, "Lo que tarda en cerrarse el filtro"},
                        {"ACENTO", &p.accent, 0.6f, "Fuerza de los pasos con acento"}, {"DRIVE", &p.drive, 0.25f, "Saturación"},
                        {"VOL", &p.bassVol, 0.7f, "Volumen del bajo"}};
    const float kw = std::min(64 * s, (b.x - a.x) / 7.f);
    for (int i = 0; i < 7; ++i) {
        ImGui::SetCursorScreenPos(ImVec2(a.x + i * kw + (kw - kr * 2) * 0.5f, ky));
        float v = knobs[i].v->load();
        char tip[64];
        std::snprintf(tip, sizeof tip, "%s: %.0f %%", knobs[i].tip, v * 100);
        ImGui::PushID(i);
        if (Knob("##k", knobs[i].l, &v, 0.f, 1.f, knobs[i].def, false, kr, acc, tip)) *knobs[i].v = v;
        ImGui::PopID();
    }
    // Pasos: nota (clic: suena o no; rueda o arrastrar: nota), acento y slide.
    const float gx = a.x + 7 * kw + 8 * s;
    const float gw = b.x - gx;
    const float cw = gw / kBassSteps;
    const float r1 = std::min(30 * s, (b.y - ky) * 0.5f), r2 = std::min(16 * s, (b.y - ky - r1) * 0.5f);
    const int cur = run && st.bassPatternNow.load() == pat ? st.bassStep.load() : -1;
    if (cur >= 0) dl->AddRectFilled(ImVec2(gx + cur * cw, ky), ImVec2(gx + (cur + 1) * cw, ky + r1 + 2 * r2), col::withAlpha(col::Text, 26));
    for (int k = 0; k < kBassSteps; ++k) {
        ImGui::PushID(k + 100);
        const int v = p.bass[pat][k].load();
        const bool gate = v & kBassGate;
        const int note = bassNote(v);
        // Nota
        const ImVec2 na(gx + k * cw + 1 * s, ky), nb(gx + (k + 1) * cw - 1 * s, ky + r1 - 2 * s);
        dl->AddRectFilled(na, nb, gate ? col::mix(col::Panel2, acc, 0.55f) : ((k / 4) % 2 ? col::mix(col::Panel2, col::Bg, 0.5f) : col::Panel2), 3 * s);
        if (gate) {
            // Altura de la nota como barrita, y su nombre.
            const float h = (nb.y - na.y - 4 * s) * (note / 24.f);
            dl->AddRectFilled(ImVec2(na.x + 2 * s, nb.y - 2 * s - h - 2 * s), ImVec2(nb.x - 2 * s, nb.y - 2 * s - h), col::Text, 1 * s);
            char nn[12];
            std::snprintf(nn, sizeof nn, "%s%s", kNoteEs[(p.bassRoot.load() + note) % 12], note >= 12 ? "'" : "");
            if (cw > 22 * s) CenterText(dl, na, ImVec2(nb.x, na.y + 12 * s), col::onColor(col::mix(col::Panel2, acc, 0.55f)), nn, ImGui::GetFont(), std::min(10.5f * s, cw * 0.32f));
        }
        ImGui::SetCursorScreenPos(na);
        ImGui::InvisibleButton("##n", ImVec2(std::max(1.f, nb.x - na.x), std::max(1.f, nb.y - na.y)));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) p.bass[pat][k] = (int16_t)(v ^ kBassGate);
        if (ImGui::IsItemHovered()) {
            const float wh = ImGui::GetIO().MouseWheel;
            if (wh != 0) p.bass[pat][k] = (int16_t)((v & ~0x3F) | std::clamp(note + (wh > 0 ? 1 : -1), 0, 24) | kBassGate);
            ImGui::SetTooltip("Paso %d: %s%s\nClic: suena o no · rueda: subir o bajar la nota · clic derecho: elegir nota", k + 1,
                              gate ? kNoteEs[(p.bassRoot.load() + note) % 12] : "silencio", gate && note >= 12 ? (note >= 24 ? " (+2 oct.)" : " (+1 oct.)") : "");
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("nota");
        if (ImGui::BeginPopup("nota")) {
            for (int o = 0; o < 3; ++o) {
                for (int i = 0; i < 12; ++i) {
                    const int nn = o * 12 + i;
                    if (nn > 24) break;
                    char l[16];
                    std::snprintf(l, sizeof l, "%s%s##%d", kNoteEs[(p.bassRoot.load() + i) % 12], o == 1 ? "'" : o == 2 ? "''" : "", nn);
                    if (i) ImGui::SameLine();
                    if (ImGui::SmallButton(l)) { p.bass[pat][k] = (int16_t)((v & ~0x3F) | nn | kBassGate); ImGui::CloseCurrentPopup(); }
                }
            }
            ImGui::EndPopup();
        }
        // Acento y slide
        const char* lbl[2] = {"A", "S"};
        const int flag[2] = {kBassAccent, kBassSlide};
        for (int r = 0; r < 2; ++r) {
            const ImVec2 fa(gx + k * cw + 1 * s, ky + r1 + r * r2), fb(gx + (k + 1) * cw - 1 * s, ky + r1 + (r + 1) * r2 - 2 * s);
            const bool on = v & flag[r];
            dl->AddRectFilled(fa, fb, on ? (r ? col::Violet : col::Amber) : col::mix(col::Panel2, col::Bg, 0.3f), 2 * s);
            CenterText(dl, fa, fb, on ? col::onColor(r ? col::Violet : col::Amber) : col::withAlpha(col::TextDim, 120), lbl[r], fontHead, std::min(11.f * s, r2 * 0.8f));
            ImGui::SetCursorScreenPos(fa);
            ImGui::PushID(r);
            ImGui::InvisibleButton("##f", ImVec2(std::max(1.f, fb.x - fa.x), std::max(1.f, fb.y - fa.y)));
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) p.bass[pat][k] = (int16_t)(v ^ flag[r]);
            ImGui::SetItemTooltip(r ? "Slide: liga esta nota con la siguiente (deslizando)" : "Acento: más fuerte y más brillante");
            ImGui::PopID();
        }
        ImGui::PopID();
    }
}

// =================================================================== sampler

void App::drawRackSampler(ImVec2 a, ImVec2 b) {
    const float s = S();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    InstrParams& p = engine_.rack.instr.params;
    const InstrStatus& st = engine_.rack.instr.status;
    const bool blink = std::fmod(now_, 0.5) < 0.25;
    const float sideW = std::clamp((b.x - a.x) * 0.32f, 170 * s, 240 * s);
    const float gw = (b.x - a.x) - sideW - 10 * s, gh = b.y - a.y;
    const float pw = gw / 8.f, ph = gh / 2.f;   // 2 filas de 8 pads
    const ImU32 modeCol[3] = {col::DeckA, col::Amber, col::Green};
    const char* modeTxt[3] = {"DISPARO", "MANTENER", "LOOP"};
    for (int k = 0; k < kPads; ++k) {
        ImGui::PushID(k);
        const ImVec2 pa(a.x + (k % 8) * pw + 2 * s, a.y + (k / 8) * ph + 2 * s), pb(pa.x + pw - 4 * s, pa.y + ph - 4 * s);
        const PadSlot& ps = pads_[k];
        const int mode = p.padMode[k].load();
        const bool playing = st.padPlaying[k].load(), waiting = st.padWaiting[k].load();
        const bool has = ps.track != nullptr;
        const ImU32 c = modeCol[std::clamp(mode, 0, 2)];
        ImU32 fill = has ? col::mix(col::Panel2, c, 0.18f) : col::mix(col::Panel2, col::Bg, 0.4f);
        if (playing) fill = col::mix(col::Panel2, c, 0.75f);
        if (waiting && blink) fill = col::mix(col::Panel2, c, 0.45f);
        dl->AddRectFilled(pa, pb, fill, 6 * s);
        dl->AddRect(pa, pb, k == padSel_ ? col::Text : col::withAlpha(c, has ? 140 : 50), 6 * s, 0, k == padSel_ ? 1.6f * s : 1.f);
        if (playing) {
            const float pos = std::clamp(st.padPos[k].load(), 0.f, 1.f);
            dl->AddRectFilled(ImVec2(pa.x + 3 * s, pb.y - 5 * s), ImVec2(pa.x + 3 * s + (pb.x - pa.x - 6 * s) * pos, pb.y - 3 * s), col::onColor(fill), 1 * s);
        }
        char num[8];
        std::snprintf(num, sizeof num, "%d", k + 1);
        dl->AddText(fontHead, 11 * s, ImVec2(pa.x + 4 * s, pa.y + 2 * s), playing ? col::onColor(fill) : col::TextDim, num);
        const std::string nm = ps.loading ? std::string("cargando…") : has ? ellipsize(ps.name, pb.x - pa.x - 8 * s, ImGui::GetFont(), 11.5f * s) : std::string("vacío");
        dl->AddText(ImGui::GetFont(), 11.5f * s, ImVec2(pa.x + 4 * s, pa.y + (pb.y - pa.y) * 0.45f), playing ? col::onColor(fill) : has ? col::Text : col::withAlpha(col::TextDim, 130),
                    nm.c_str());
        ImGui::SetCursorScreenPos(pa);
        ImGui::InvisibleButton("##pad", ImVec2(pb.x - pa.x, pb.y - pa.y));
        if (ImGui::IsItemActivated()) { padSel_ = k; if (has) { padCmd(k, 1); padHeld_[k] = true; } }
        if (padHeld_[k] && !ImGui::IsItemActive()) { padCmd(k, 0); padHeld_[k] = false; }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) { padSel_ = k; ImGui::OpenPopup("padmenu"); }
        ImGui::SetItemTooltip("%s · %s\nClic: tocar · clic derecho: cargar, modo, volumen · también puedes soltar aquí un archivo", has ? ps.name.c_str() : "Vacío",
                              modeTxt[std::clamp(mode, 0, 2)]);
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("RDJ_PISTA")) {
                const auto paths = uic::PayloadPaths(pl);
                if (!paths.empty()) loadPadFile(k, paths[0]);
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::BeginPopup("padmenu")) {
            ImGui::PushFont(fontHead, 13.f);
            ImGui::Text("PAD %d", k + 1);
            ImGui::PopFont();
            if (ImGui::MenuItem("Cargar un archivo de audio…")) {
                auto f = win::pickAudioFiles(hwnd_, false);
                if (!f.empty()) loadPadFile(k, f[0]);
            }
            if (ImGui::BeginMenu("Sonidos incluidos")) {
                for (int fx = 0; fx < kFxSamples; ++fx) if (ImGui::MenuItem(fxSampleName(fx))) setPad(k, makeFxSample(fx, 48000), fxSampleName(fx), L"", fx);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            for (int m = 0; m < 3; ++m) if (ImGui::MenuItem(m == 0 ? "Disparo (suena entero)" : m == 1 ? "Mantener (suena mientras se pulsa)" : "Loop (un toque arranca, otro para)", nullptr, mode == m)) p.padMode[k] = m;
            float v = p.padVol[k].load();
            ImGui::SetNextItemWidth(160 * s);
            if (ImGui::SliderFloat("volumen", &v, 0.f, 1.f, "%.2f")) p.padVol[k] = v;
            ImGui::Separator();
            if (ImGui::MenuItem("Asignar a un control MIDI (Aprender)")) {
                wizStep_ = -1;
                midi_.startLearn(MA_RackPad1 + k, 0);
                rackLearning_ = MA_RackPad1 + k;
                notify("Pulsa el botón o pad del mando para el pad " + std::to_string(k + 1) + "…");
            }
            if (has && ImGui::MenuItem("Vaciar el pad")) { setPad(k, nullptr, "", L"", -1); pads_[k].cleared = true; }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    // Lateral: pad elegido y controles generales
    const float sx = b.x - sideW;
    float y = a.y;
    {
        const PadSlot& ps = pads_[padSel_];
        char t[64];
        std::snprintf(t, sizeof t, "PAD %d", padSel_ + 1);
        dl->AddText(fontHead, 13 * s, ImVec2(sx, y), col::TextDim, t);
        const std::string nm = ellipsize(ps.track ? ps.name : "vacío", sideW - 60 * s, ImGui::GetFont(), 13 * s);
        dl->AddText(ImGui::GetFont(), 13 * s, ImVec2(sx + 52 * s, y), col::Text, nm.c_str());
        y += 20 * s;
        ImGui::SetCursorScreenPos(ImVec2(sx, y));
        int mode = p.padMode[padSel_].load();
        const char* mt[3] = {"DISPARO", "MANTENER", "LOOP"};
        if (Seg("padmode", &mode, mt, 3, ImVec2(sideW, 22 * s), modeCol[std::clamp(mode, 0, 2)], fontHead, 11 * s)) p.padMode[padSel_] = mode;
        y += 28 * s;
        ImGui::SetCursorScreenPos(ImVec2(sx, y));
        float v = p.padVol[padSel_].load();
        ImGui::SetNextItemWidth(sideW * 0.5f - 4 * s);
        if (ImGui::SliderFloat("##pv", &v, 0.f, 1.f, "PAD %.2f")) p.padVol[padSel_] = v;
        ImGui::SameLine(0, 6 * s);
        float sv = p.samplerVol.load();
        ImGui::SetNextItemWidth(sideW * 0.5f - 4 * s);
        if (ImGui::SliderFloat("##sv", &sv, 0.f, 1.f, "TODOS %.2f")) p.samplerVol = sv;
        y += 28 * s;
    }
    if (y + 22 * s < b.y) {
        ImGui::SetCursorScreenPos(ImVec2(sx, y));
        bool q = p.padQuant.load();
        if (NeonButton("LOOPS EN EL PULSO##pq", col::Green, q, ImVec2(sideW * 0.6f - 3 * s, 22 * s), true, fontHead, 11 * s)) p.padQuant = !q;
        ImGui::SetItemTooltip("Los pads en modo LOOP entran en el siguiente pulso del reloj");
        ImGui::SameLine(0, 6 * s);
        if (NeonButton("PARAR##pstop", col::Red, false, ImVec2(sideW * 0.4f - 3 * s, 22 * s), true, fontHead, 11 * s)) padCmd(0, 2);
        ImGui::SetItemTooltip("Parar todos los pads");
    }
    if (y + 52 * s < b.y) instrClockBar(ImVec2(sx, y + 30 * s), sideW, 22 * s);
}

} // namespace rdj

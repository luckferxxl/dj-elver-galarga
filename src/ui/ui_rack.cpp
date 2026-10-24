// Rack de hardware virtual (0.9): interfaz bajo el mezclador, presets y MIDI.
// El audio está en src/audio/rack.*; aquí solo se escriben sus parámetros (atómicos),
// se mandan órdenes por la cola del motor y se lee su estado.
#include "app.h"
#include "ui_common.h"
#include "widgets.h"
#include "../core/util.h"
#include <json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#endif

namespace rdj {

using namespace ui;
using json = nlohmann::json;
using uic::BeginPanel;
using uic::ellipsize;

namespace {

float S() { return ImGui::GetFontSize() / 16.f; }

const char* kModNames[7] = {"EFECTOS", "PAD XY", "AISLADOR", "LOOP", "RITMOS", "SAMPLER", "BAJO"};

// Control segmentado (fuente, modo, compases…). Devuelve true si cambia.
bool Segmented(const char* id, int* v, const char* const* items, int count, ImVec2 size, ImU32 accent, ImFont* font, float fs, const char* const* tips = nullptr) {
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
        if (ImGui::InvisibleButton("##seg", ImVec2(w, size.y)) && *v != i) { *v = i; changed = true; }
        if (tips && tips[i]) ImGui::SetItemTooltip("%s", tips[i]);
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        const bool on = *v == i;
        if (on) dl->AddRectFilled(ImVec2(a.x + 2 * s, a.y + 2 * s), ImVec2(b.x - 2 * s, b.y - 2 * s), col::mix(col::Panel2, accent, 0.35f), 4 * s);
        CenterText(dl, a, b, on ? col::Text : hov ? col::mix(col::TextDim, col::Text, 0.5f) : col::TextDim, items[i], font, fs);
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
        dl->AddCircle(c, r, col::withAlpha(color, 90), 0, 1.f);
    }
}

// Potenciómetro que además marca el valor efectivo (con macro o pad XY) si difiere.
bool RackKnob(const char* id, const char* label, float* v, float vdef, bool bipolar, float r, ImU32 accent, const char* tip, float eff) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float ls = ImGui::GetFontSize() * 0.72f;
    const float w = std::max(r * 2.f, label && *label ? ImGui::GetFont()->CalcTextSizeA(ls, FLT_MAX, 0, label).x : 0.f);
    const bool ch = Knob(id, label, v, 0.f, 1.f, vdef, bipolar, r, accent, tip);
    if (std::fabs(eff - *v) > 0.01f) {
        const float a0 = 0.75f * IM_PI, a1 = 2.25f * IM_PI;
        const float at = a0 + (a1 - a0) * std::clamp(eff, 0.f, 1.f);
        const ImVec2 c(p.x + w * 0.5f, p.y + r);
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(c.x + std::cos(at) * (r + 1.5f), c.y + std::sin(at) * (r + 1.5f)), std::max(2.5f, r * 0.17f), col::Amber);
    }
    return ch;
}

float lvlDb(float g) { return g <= 1e-5f ? -120.f : 20.f * std::log10(g); }

std::string isoText(float knob, bool kill) {
    if (kill || knob <= 0.002f) return "Corte total";
    char t[32];
    std::snprintf(t, sizeof t, "%+.1f dB", lvlDb(eqKnobToGain(knob)));
    return t;
}

const char* srcId(int s) { return s == 0 ? "A" : s == 1 ? "B" : s == 3 ? "instrumentos" : "mezcla"; }
int srcFromId(const std::string& s) { return s == "A" ? 0 : s == "B" ? 1 : s == "instrumentos" ? 3 : 2; }

std::string midiText(const MidiBinding& b) {
    char t[64];
    std::snprintf(t, sizeof t, "%s %d, canal %d%s", b.type == 0xB ? "CC" : "nota", b.data1, b.channel + 1, b.shift ? " (SHIFT)" : "");
    return t;
}

} // namespace

// =================================================================== estado y presets

std::string App::rackToJson() const {
    const RackParams& p = engine_.rack.params;
    json st, slots = json::array();
    for (int k = 0; k < kStSlots; ++k) {
        const StSlotParams& sp = p.slot[k];
        slots.push_back({{"tipo", stFxId(sp.type.load())}, {"on", sp.on.load()}, {"intensidad", sp.amount.load()}, {"mezcla", sp.mix.load()},
                         {"division", stDivName(sp.div.load())}, {"macroInt", sp.macroAmt.load()}, {"macroMez", sp.macroMix.load()}});
    }
    json order = json::array();
    const uint32_t o = p.stOrder.load();
    for (int i = 0; i < kStSlots; ++i) order.push_back((int)((o >> (i * 4)) & 0xF));
    st = {{"encendida", p.stOn.load()}, {"fuente", srcId(p.stSrc.load())}, {"modo", p.stMode.load() ? "envio" : "insercion"},
          {"retorno", p.stReturn.load()}, {"orden", order}, {"macro", p.macro.load()}, {"ranuras", slots}};
    json xy = {{"x", xyTargetId(p.xyTargetX.load())}, {"y", xyTargetId(p.xyTargetY.load())}, {"tocarEnciende", p.xyAutoOn.load()}};
    json iso = {{"encendido", p.isoOn.load()}, {"fuente", srcId(p.isoSrc.load())}, {"graves", p.isoGain[0].load()}, {"medios", p.isoGain[1].load()},
                {"agudos", p.isoGain[2].load()}, {"corte", {p.isoKill[0].load(), p.isoKill[1].load(), p.isoKill[2].load()}},
                {"cruceBajo", p.isoLowHz.load()}, {"cruceAlto", p.isoHighHz.load()}};
    json lp = {{"fuente", srcId(p.lpSrc.load())}, {"compases", p.lpBars.load()}, {"volumen", p.lpVol.load()}, {"cuantizar", p.lpQuant.load()},
               {"seguirBpm", p.lpFollow.load()}, {"auriculares", p.lpCue.load()}, {"reproducirAlTerminar", p.lpAutoPlay.load()}};
    json stems=json::array();
    for (int k=0;k<kStemSlots;++k) {
        const auto& f=engine_.stem.fx[k];
        stems.push_back({{"low",engine_.stem.low[k].load()},{"mid",engine_.stem.mid[k].load()},{"high",engine_.stem.high[k].load()},
            {"type",stFxId(f.type.load())},{"on",f.on.load()},{"amount",f.amount.load()},{"mix",f.mix.load()},{"div",f.div.load()}});
    }
    return json{{"estacion", st}, {"xy", xy}, {"aislador", iso}, {"loop", lp}, {"stemFx",stems},{"stemBpm",engine_.stem.bpm.load()}}.dump();
}

// Aplica un estado o preset. Lo que no viene en el JSON se queda como está.
bool App::rackFromJson(const std::string& text) {
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return false;
    RackParams& p = engine_.rack.params;
    auto f01 = [](const json& o, const char* k, float def) { const float v = o.value(k, def); return std::isfinite(v) ? std::clamp(v, 0.f, 1.f) : def; };
    if (j.contains("estacion") && j["estacion"].is_object()) {
        const json& st = j["estacion"];
        p.stOn = st.value("encendida", true);
        p.stSrc = srcFromId(st.value("fuente", std::string("mezcla")));
        p.stMode = st.value("modo", std::string("insercion")) == "envio" ? 1 : 0;
        p.stReturn = f01(st, "retorno", 0.8f);
        p.macro = f01(st, "macro", 0.f);
        if (st.contains("orden") && st["orden"].is_array() && st["orden"].size() == kStSlots) {
            uint32_t o = 0; bool seen[kStSlots] = {}, ok = true;
            for (int i = 0; i < kStSlots; ++i) {
                const int k = st["orden"][i].is_number_integer() ? st["orden"][i].get<int>() : -1;
                if (k < 0 || k >= kStSlots || seen[k]) { ok = false; break; }
                seen[k] = true; o |= (uint32_t)k << (i * 4);
            }
            p.stOrder = ok ? o : 0x3210u;
        }
        if (st.contains("ranuras") && st["ranuras"].is_array())
            for (int k = 0; k < kStSlots && k < (int)st["ranuras"].size(); ++k) {
                const json& r = st["ranuras"][k];
                if (!r.is_object()) continue;
                StSlotParams& sp = p.slot[k];
                sp.type = stFxFromId(r.value("tipo", std::string("ninguno")));
                sp.on = r.value("on", false);
                sp.amount = f01(r, "intensidad", 0.5f);
                sp.mix = f01(r, "mezcla", 0.5f);
                int div = 6;
                const std::string dn = r.value("division", std::string("1"));
                for (int i = 0; i < kStDivs; ++i) if (dn == stDivName(i)) div = i;
                sp.div = div;
                sp.macroAmt = std::clamp(r.value("macroInt", 0.f), -1.f, 1.f);
                sp.macroMix = std::clamp(r.value("macroMez", 0.f), -1.f, 1.f);
            }
    }
    if (j.contains("xy") && j["xy"].is_object()) {
        const json& x = j["xy"];
        p.xyTargetX = xyTargetFromId(x.value("x", std::string("nada")));
        p.xyTargetY = xyTargetFromId(x.value("y", std::string("nada")));
        p.xyAutoOn = x.value("tocarEnciende", true);
        p.xyHold = false;
    }
    if (j.contains("aislador") && j["aislador"].is_object()) {
        const json& i = j["aislador"];
        p.isoOn = i.value("encendido", false);
        p.isoSrc = srcFromId(i.value("fuente", std::string("mezcla")));
        p.isoGain[0] = f01(i, "graves", 0.5f); p.isoGain[1] = f01(i, "medios", 0.5f); p.isoGain[2] = f01(i, "agudos", 0.5f);
        for (int b = 0; b < 3; ++b) p.isoKill[b] = i.contains("corte") && i["corte"].is_array() && i["corte"].size() == 3 && i["corte"][b].is_boolean() ? i["corte"][b].get<bool>() : false;
        p.isoLowHz = std::clamp(i.value("cruceBajo", 200.f), kIsoLowMin, kIsoLowMax);
        p.isoHighHz = std::clamp(i.value("cruceAlto", 2500.f), kIsoHighMin, kIsoHighMax);
    }
    if (j.contains("loop") && j["loop"].is_object()) {
        const json& l = j["loop"];
        p.lpSrc = srcFromId(l.value("fuente", std::string("mezcla")));
        int bars = l.value("compases", 2);
        p.lpBars = bars == 1 || bars == 2 || bars == 4 || bars == 8 ? bars : 2;
        p.lpVol = f01(l, "volumen", 0.8f);
        p.lpQuant = l.value("cuantizar", true); p.lpFollow = l.value("seguirBpm", true);
        p.lpCue = l.value("auriculares", false); p.lpAutoPlay = l.value("reproducirAlTerminar", false);
    }
    if (j.contains("stemBpm") && j["stemBpm"].is_number()) engine_.stem.bpm=std::clamp(j["stemBpm"].get<float>(),40.f,220.f);
    if (j.contains("stemFx") && j["stemFx"].is_array()) {
        for (int k=0;k<kStemSlots && k<int(j["stemFx"].size());++k) {
            const auto& x=j["stemFx"][k]; if(!x.is_object()) continue;
            auto& f=engine_.stem.fx[k];
            engine_.stem.low[k]=f01(x,"low",.5f); engine_.stem.mid[k]=f01(x,"mid",.5f); engine_.stem.high[k]=f01(x,"high",.5f);
            f.type=stFxFromId(x.value("type",std::string("ninguno"))); f.on=x.value("on",false);
            f.amount=f01(x,"amount",.5f); f.mix=f01(x,"mix",.5f); f.div=std::clamp(x.value("div",6),0,kStDivs-1);
        }
    }
    return true;
}

namespace {
// Familias del desplegable de efectos (el orden de StFx no se puede tocar: los presets guardan el id).
struct StFxGroup { const char* title; int types[10]; };
const std::vector<StFxGroup>& stFxGroups() {
    using F = StFx;
    static const std::vector<StFxGroup> g = {
        {nullptr, {(int)F::Ninguno, -1}},
        {"Filtro y color", {(int)F::Filtro, (int)F::Saturacion, (int)F::Distorsion, (int)F::Bitcrusher, (int)F::Vinilo, (int)F::Telefono, -1}},
        {"Eco y espacio", {(int)F::Eco, (int)F::Delay, (int)F::EcoCinta, (int)F::Spiral, (int)F::Reverb, (int)F::ReverbLarga, (int)F::Shimmer, -1}},
        {"Modulación", {(int)F::Flanger, (int)F::Phaser, (int)F::Chorus, (int)F::Tremolo, (int)F::AutoPan, -1}},
        {"Ritmo y cortes", {(int)F::Gate, (int)F::Sidechain, (int)F::Repetidor, (int)F::Slicer, (int)F::Freeze, -1}},
        {"Texturas y carácter", {(int)F::Granular, (int)F::Resonador, (int)F::Multibanda, -1}},
        {"Subidas", {(int)F::Riser, -1}},
    };
    return g;
}
// Valores de partida con sentido para cada efecto al elegirlo.
void applyStFxDefaults(StSlotParams& sp, int t) {
    switch ((StFx)t) {
    case StFx::Filtro: sp.amount = 0.5f; sp.mix = 1.f; break;
    case StFx::Gate: case StFx::Flanger: case StFx::Phaser: case StFx::Saturacion: case StFx::Distorsion: sp.mix = std::max(sp.mix.load(), 0.7f); break;
    case StFx::Riser: sp.amount = 0.6f; sp.mix = 1.f; break;
    case StFx::Spiral: sp.amount = 0.5f; sp.mix = 0.5f; break;
    case StFx::Shimmer: sp.amount = 0.5f; sp.mix = 0.45f; break;
    case StFx::Sidechain: sp.amount = 0.7f; sp.mix = 1.f; break;
    case StFx::Vinilo: case StFx::Telefono: sp.amount = 0.5f; sp.mix = 1.f; break;
    case StFx::Chorus: sp.amount = 0.5f; sp.mix = 0.7f; break;
    case StFx::Tremolo: case StFx::AutoPan: sp.amount = 0.7f; sp.mix = 1.f; break;
    case StFx::Freeze: sp.amount = 0.45f; sp.mix = 1.f; break;
    case StFx::Slicer: sp.amount = 0.f; sp.mix = 1.f; break;
    default: break;
    }
    if (stFxUsesTime(t) && (!stFxDivAllowed(t, sp.div.load()) || t >= (int)StFx::Riser)) sp.div = stFxDefaultDiv(t);
}

// Presets iniciales. Las ranuras sin "on" se encienden con la macro o con el pad XY.
std::string slotJ(const char* tipo, bool on, float inten, float mez, const char* div, float mInt, float mMez) {
    return json{{"tipo", tipo}, {"on", on}, {"intensidad", inten}, {"mezcla", mez}, {"division", div}, {"macroInt", mInt}, {"macroMez", mMez}}.dump();
}
std::string presetJ(bool stOn, const char* src, const char* modo, float ret, const std::vector<std::string>& slots, const char* x, const char* y,
                    const json& iso) {
    json r = json::array();
    for (auto& s : slots) r.push_back(json::parse(s));
    while (r.size() < kStSlots) r.push_back(json::parse(slotJ("ninguno", false, 0.5f, 0.5f, "1", 0, 0)));
    json j = {{"estacion", {{"encendida", stOn}, {"fuente", src}, {"modo", modo}, {"retorno", ret}, {"orden", {0, 1, 2, 3}}, {"macro", 0.f}, {"ranuras", r}}},
              {"xy", {{"x", x}, {"y", y}, {"tocarEnciende", true}}}};
    if (!iso.is_null()) j["aislador"] = iso;
    return j.dump();
}
json isoJ(bool on, const char* src, float lo, float mid, float hi, float xLo) {
    return {{"encendido", on}, {"fuente", src}, {"graves", lo}, {"medios", mid}, {"agudos", hi}, {"corte", {false, false, false}}, {"cruceBajo", xLo}, {"cruceAlto", 2500.f}};
}
} // namespace

void App::rackBuiltinPresets() {
    rackPresets_.clear();
    rackPresets_.push_back({"Vacío", presetJ(true, "mezcla", "insercion", 0.8f, {}, "int1", "mez2", isoJ(false, "mezcla", 0.5f, 0.5f, 0.5f, 200.f)), true,
                            "Todo a cero: sin efectos y aislador apagado."});
    rackPresets_.push_back({"Transición suave",
                            presetJ(true, "A", "insercion", 0.8f,
                                    {slotJ("filtro", false, 0.5f, 1.f, "1", -0.38f, 0.f), slotJ("eco", false, 0.55f, 0.f, "1/2", 0.f, 0.55f),
                                     slotJ("reverb", false, 0.6f, 0.f, "1", 0.f, 0.45f)},
                                    "int1", "mez2", json()),
                            true, "Sube la MACRO para despedir el plato A: filtro que cierra, eco de 1/2 pulso y reverb."});
    rackPresets_.push_back({"Subida techno",
                            presetJ(true, "mezcla", "insercion", 0.8f,
                                    {slotJ("filtro", false, 0.5f, 1.f, "1", 0.36f, 0.f), slotJ("delay", false, 0.35f, 0.f, "1/4", 0.3f, 0.5f),
                                     slotJ("reverb", false, 0.75f, 0.f, "1", 0.f, 0.4f), slotJ("gate", false, 0.f, 1.f, "1/8", 0.85f, 0.f)},
                                    "int1", "macro", json()),
                            true, "Sube la MACRO (o el eje Y del pad) durante la subida: filtro que abre, delay de 1/4, reverb y gate de 1/8."});
    rackPresets_.push_back({"Voz con eco",
                            presetJ(true, "A", "envio", 0.9f,
                                    {slotJ("filtro", true, 0.62f, 1.f, "1", 0.f, 0.f), slotJ("eco", true, 0.5f, 0.8f, "3/4", 0.f, 0.f),
                                     slotJ("reverb", true, 0.5f, 0.35f, "1", 0.f, 0.f)},
                                    "mez2", "int2", json()),
                            true, "Envío y retorno sobre el plato A: el original no cambia; vuelve un eco de 3/4 sin graves con algo de reverb."});
    rackPresets_.push_back({"Corte de graves", presetJ(true, "B", "insercion", 0.8f, {slotJ("filtro", false, 0.5f, 1.f, "1", 0.f, 0.f)}, "graves", "agudos",
                                                       isoJ(true, "B", 0.f, 0.5f, 0.5f, 180.f)),
                            true, "Aislador en el plato B con los graves cortados (para el cambio de bajo). El pad mueve graves y agudos."});
    // 0.11.0: presets con los efectos nuevos.
    rackPresets_.push_back({"Subida con riser",
                            presetJ(true, "mezcla", "insercion", 0.8f,
                                    {slotJ("riser", false, 0.6f, 0.f, "4", 0.f, 1.f), slotJ("filtro", false, 0.5f, 1.f, "1", 0.3f, 0.f),
                                     slotJ("reverblarga", false, 0.5f, 0.f, "1", 0.f, 0.5f)},
                                    "int2", "macro", json()),
                            true, "Sube la MACRO al empezar la subida: el ruido sube solo en 4 compases, el filtro abre y crece la reverb. Bájala en el drop."});
    rackPresets_.push_back({"Bombeo house",
                            presetJ(true, "mezcla", "insercion", 0.8f, {slotJ("sidechain", true, 0.6f, 1.f, "1", 0.f, 0.f)}, "int1", "mez1", json()),
                            true, "Sidechain en cada pulso sobre la mezcla. Eje X del pad: cuánto bombea; eje Y: cuánto se nota."});
    rackPresets_.push_back({"Lo-fi",
                            presetJ(true, "mezcla", "insercion", 0.8f,
                                    {slotJ("vinilo", true, 0.5f, 1.f, "1", 0.f, 0.f), slotJ("telefono", false, 0.3f, 0.f, "1", 0.f, 1.f)},
                                    "int1", "mez2", json()),
                            true, "Disco viejo encendido. La MACRO (o el eje Y del pad) mete la radio; el eje X, el desgaste del vinilo."});
    rackPresets_.push_back({"Salida spiral",
                            presetJ(true, "A", "insercion", 0.8f,
                                    {slotJ("filtro", false, 0.5f, 1.f, "1", -0.3f, 0.f), slotJ("spiral", false, 0.5f, 0.f, "3/4", 0.f, 0.65f)},
                                    "int2", "macro", json()),
                            true, "Sube la MACRO para despedir el plato A: el filtro cierra y un eco de 3/4 sube de tono en cada repetición."});
    rackPresets_.push_back({"Cortes al pad",
                            presetJ(true, "mezcla", "insercion", 0.8f, {slotJ("slicer", false, 0.3f, 1.f, "4", 0.f, 0.f), slotJ("freeze", false, 0.45f, 1.f, "1", 0.f, 0.f)},
                                    "int1", "mez2", json()),
                            true, "Toca el pad: el eje X elige el patrón del slicer (el último compás en 8 trozos) y el eje Y congela el sonido con el freeze."});
}

static std::filesystem::path rackFile() { return std::filesystem::path(appDataDir()) / L"rack.json"; }

void App::loadRack() {
    rackBuiltinPresets();
    std::ifstream f(rackFile(), std::ios::binary);
    if (!f) { initPads(); rackSaved_ = rackToJson() + instrToJson(); return; }
    json j = json::parse(f, nullptr, false);
    if (j.is_discarded() || !j.is_object()) { initPads(); rackSaved_ = rackToJson() + instrToJson(); return; }
    if (j.contains("presets") && j["presets"].is_array())
        for (auto& pj : j["presets"]) {
            if (!pj.is_object() || !pj.contains("datos")) continue;
            const std::string name = pj.value("nombre", std::string("Mi preset"));
            bool dup = false;
            for (auto& rp : rackPresets_) if (rp.name == name) dup = true;
            if (!dup) rackPresets_.push_back({name, pj["datos"].dump(), false, ""});
        }
    if (j.contains("actual")) rackFromJson(j["actual"].dump());
    rackPresetName_ = j.value("preset", std::string());
    if (j.contains("instrumentos")) instrFromJson(j["instrumentos"].dump());
    if (j.contains("escenas")) scenesFromJson(j["escenas"].dump());
    if (j.contains("loopsGuardados")) workbenchFromJson(j["loopsGuardados"].dump());
    if (j.contains("bancosBateria")) drumLibraryFromJson(j["bancosBateria"].dump());
    initPads();
    rackSaved_ = rackToJson() + instrToJson();
}

bool App::saveRack() {
    json j;
    j["actual"] = json::parse(rackToJson());
    j["preset"] = rackPresetName_;
    j["instrumentos"] = json::parse(instrToJson());
    j["escenas"] = json::parse(scenesToJson());
    j["loopsGuardados"] = json::parse(workbenchToJson());
    j["bancosBateria"] = json::parse(drumLibraryToJson());
    json arr = json::array();
    for (auto& p : rackPresets_) if (!p.builtin) arr.push_back({{"nombre", p.name}, {"datos", json::parse(p.data, nullptr, false)}});
    j["presets"] = arr;
    std::error_code ec;
    std::filesystem::create_directories(rackFile().parent_path(), ec);
    const auto tmp = rackFile().wstring() + L".tmp";
    {
        std::ofstream f(std::filesystem::path(tmp), std::ios::binary);
        f << j.dump(2);
        if (!f) { notify("No se pudo guardar el rack y las escenas.",true); return false; }
    }
#ifdef _WIN32
    if (!MoveFileExW(tmp.c_str(),rackFile().c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        notify("No se pudo actualizar el archivo del rack y las escenas.",true); return false;
    }
#else
    std::filesystem::rename(std::filesystem::path(tmp), rackFile(), ec);
    if(ec) { notify("No se pudo actualizar el rack y las escenas: " + ec.message(),true); return false; }
#endif
    rackSaved_ = rackToJson() + instrToJson();
    return true;
}

void App::rackApplyPreset(int i) {
    if (i < 0 || i >= (int)rackPresets_.size()) return;
    rackFromJson(rackPresets_[i].data);
    engine_.rack.params.fxCut = false;
    rackPresetName_ = rackPresets_[i].name;
    notify("Preset del rack: " + rackPresets_[i].name);
}

void App::rackPresetStep(int dir) {
    if (rackPresets_.empty()) return;
    int cur = -1;
    for (size_t i = 0; i < rackPresets_.size(); ++i) if (rackPresets_[i].name == rackPresetName_) cur = (int)i;
    const int n = (int)rackPresets_.size();
    rackApplyPreset(((cur < 0 ? (dir > 0 ? -1 : 0) : cur) + dir + n) % n);
}

void App::rackLoop(int c, int arg) {
    if (c == Rack::LoopRec) engine_.rack.allocLoop();   // la memoria del loop se reserva aquí, la primera vez (no en el audio)
    Command q{Cmd::RackLoop, 0};
    q.i = c; q.d = arg;
    engine_.send(q);
}

void App::rackLoopExport() {
    const RackStatus& st = engine_.rack.status;
    const int state = st.lpState.load();
    if (!engine_.rack.loopReady() || (state != (int)LoopState::Parado && state != (int)LoopState::Reproduciendo)) { notify("No hay un loop terminado para exportar.", true); return; }
    if (st.lpBusy.load()) { notify("Espera a que termine de grabar o de deshacer.", true); return; }
    const uint32_t gen = st.lpGen.load(std::memory_order_acquire);
    const int64_t n = engine_.rack.loopFrames();
    if (n <= 0) return;
    // Copia y comprobación: si el audio hubiera empezado a escribir en el loop, se descarta.
    std::vector<float> L(engine_.rack.loopL(), engine_.rack.loopL() + n), R(engine_.rack.loopR(), engine_.rack.loopR() + n);
    if (st.lpGen.load(std::memory_order_acquire) != gen || st.lpBusy.load()) { notify("El loop cambió mientras se exportaba; vuelve a intentarlo.", true); return; }
    std::time_t tt = std::time(nullptr);
    std::tm lt{};
    localtime_s(&lt, &tt);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H%M%S", &lt);
    char name[160];
    std::snprintf(name, sizeof name, "Loop %s - %.1f BPM - %d pulsos.wav", stamp, st.lpBpm.load(), st.lpBars.load() * 4);
    const std::filesystem::path dir = recordingFolder();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::wstring path = (dir / std::filesystem::path(fromUtf8(name))).wstring();
    std::string err;
    if (!writeWavStereoFloat(path, L.data(), R.data(), (uint64_t)n, (uint32_t)engine_.sampleRate(), err)) { notify(err, true); return; }
    library_.addFiles({path});
    library_.addToPlaylist(ensurePlaylist("Grabaciones"), {path});
    rackLastExport_ = path;
    notify("Loop exportado: " + toUtf8(path));
}

// Cada fotograma: pad XY por MIDI (se suelta solo), luces del mando y guardado del estado.
void App::rackTick() {
    RackParams& p = engine_.rack.params;
    padsTick();
    drumLibraryTick();
    motionTick();
    liveTick();
    if (rackMidiXyTouch_ && !rackMidiTouchHeld_ && !rackMouseXy_ && now_ - rackMidiXyAt_ > 0.8) {
        rackMidiXyTouch_ = false;
        p.xyTouch = false;
    }
    if (rackLearning_ >= 0 && !midi_.learning()) {
        const MidiBinding* b = midi_.findBinding(rackLearning_, 0);
        if (b) notify(std::string("Asignado: ") + midiActions()[rackLearning_].name + " → " + midiText(*b));
        rackLearning_ = -1;
    }
    if (now_ >= rackSaveCheck_ && !autotest_.active) {
        rackSaveCheck_ = now_ + 2.0;
        if (rackToJson() + instrToJson() != rackSaved_) saveRack();
    }
}

void App::rackLeds() {
    const RackParams& p = engine_.rack.params;
    const RackStatus& st = engine_.rack.status;
    const int ls = st.lpState.load();
    const bool blink = std::fmod(now_, 0.5) < 0.25;
    midi_.setLed(MA_RackFxOff, 0, p.fxCut.load());
    midi_.setLed(MA_FxClearAll, 0, !activeEffects().empty());
    for (int d = 0; d < kNumDecks; ++d) midi_.setLed(MA_DeckFxClear, d, !activeEffects(d).empty());
    midi_.setLed(MA_RackXyHold, 0, p.xyHold.load());
    for (int k = 0; k < kStSlots; ++k) midi_.setLed(MA_RackSlot1On + k, 0, st.slotOnEff[k].load());
    midi_.setLed(MA_RackIsoOn, 0, p.isoOn.load());
    for (int b = 0; b < 3; ++b) midi_.setLed(MA_RackIsoKillLow + b, 0, p.isoKill[b].load());
    midi_.setLed(MA_RackLoopRec, 0, ls == (int)LoopState::Grabando || (ls == (int)LoopState::Armado && blink));
    midi_.setLed(MA_RackLoopPlay, 0, ls == (int)LoopState::Reproduciendo || ls == (int)LoopState::Sobregrabando || (ls == (int)LoopState::ArmadoPlay && blink));
    midi_.setLed(MA_RackLoopDub, 0, ls == (int)LoopState::Sobregrabando);
    midi_.setLed(MA_RackLoopUndo, 0, st.lpCanUndo.load());
    const InstrStatus& is = engine_.rack.instr.status;
    midi_.setLed(MA_RackDrumPlay, 0, is.drumRunning.load() || (is.drumArmed.load() && blink));
    midi_.setLed(MA_RackBassPlay, 0, is.bassRunning.load() || (is.bassArmed.load() && blink));
    for (int k = 0; k < kPads; ++k) midi_.setLed(MA_RackPad1 + k, 0, is.padPlaying[k].load() || (is.padWaiting[k].load() && blink));
}

// Órdenes MIDI del rack. absolute: el ayudante de pollMidi con recogida de valor (soft takeover).
void App::rackMidi(const MidiCommand& c, const std::function<void(const MidiCommand&, float, const std::function<void(float)>&)>& absolute) {
    RackParams& p = engine_.rack.params;
    if(c.action>=MA_DrumHit1&&c.action<=MA_DrumHit8){if(c.pressed){Command q{Cmd::DrumHit};q.i=c.action-MA_DrumHit1;q.d=c.value>0?c.value:1;engine_.send(q);}return;}
    if(c.action>=MA_SavedLoop1 && c.action<=MA_SavedLoop8){if(c.pressed)recallDeckLoop(c.deck,c.action-MA_SavedLoop1);return;}
    if (c.action>=MA_Scene1 && c.action<=MA_Scene8) { if(c.pressed) sceneLaunch(c.action-MA_Scene1); return; }
    auto toggle = [](std::atomic<bool>& b) { b = !b.load(); };
    switch (c.action) {
    case MA_RackFxOff: if (c.pressed) toggle(p.fxCut); break;
    case MA_FxClearAll: if (c.pressed) clearAllEffects(); break;
    case MA_RackMacro: absolute(c, p.macro.load(), [&](float v) { p.macro = v; }); break;
    case MA_RackXyX: case MA_RackXyY:
        (c.action == MA_RackXyX ? p.xyX : p.xyY) = c.value;
        if (!rackMouseXy_) { p.xyTouch = true; rackMidiXyTouch_ = true; rackMidiXyAt_ = now_; }
        break;
    case MA_RackXyTouch:
        rackMidiTouchHeld_ = c.pressed;
        if (c.pressed) { p.xyTouch = true; rackMidiXyTouch_ = true; rackMidiXyAt_ = now_; }
        else if (!rackMouseXy_) { p.xyTouch = false; rackMidiXyTouch_ = false; }
        break;
    case MA_RackXyHold: if (c.pressed) toggle(p.xyHold); break;
    case MA_RackSlot1On: case MA_RackSlot2On: case MA_RackSlot3On: case MA_RackSlot4On:
        if (c.pressed) toggle(p.slot[c.action - MA_RackSlot1On].on);
        break;
    case MA_RackSlot1Amt: case MA_RackSlot2Amt: case MA_RackSlot3Amt: case MA_RackSlot4Amt: {
        auto& a = p.slot[c.action - MA_RackSlot1Amt].amount;
        absolute(c, a.load(), [&](float v) { a = v; });
        break;
    }
    case MA_RackSlot1Mix: case MA_RackSlot2Mix: case MA_RackSlot3Mix: case MA_RackSlot4Mix: {
        auto& m = p.slot[c.action - MA_RackSlot1Mix].mix;
        absolute(c, m.load(), [&](float v) { m = v; });
        break;
    }
    case MA_RackReturn: absolute(c, p.stReturn.load(), [&](float v) { p.stReturn = v; }); break;
    case MA_RackIsoOn: if (c.pressed) toggle(p.isoOn); break;
    case MA_RackIsoLow: case MA_RackIsoMid: case MA_RackIsoHigh: {
        auto& g = p.isoGain[c.action - MA_RackIsoLow];
        absolute(c, g.load(), [&](float v) { g = v; });
        break;
    }
    case MA_RackIsoKillLow: case MA_RackIsoKillMid: case MA_RackIsoKillHigh: if (c.pressed) toggle(p.isoKill[c.action - MA_RackIsoKillLow]); break;
    case MA_RackLoopRec: if (c.pressed) rackLoop(Rack::LoopRec, p.lpBars.load()); break;
    case MA_RackLoopPlay: if (c.pressed) rackLoop(Rack::LoopPlayStop); break;
    case MA_RackLoopDub: if (c.pressed) rackLoop(Rack::LoopDub); break;
    case MA_RackLoopUndo: if (c.pressed) rackLoop(Rack::LoopUndo); break;
    case MA_RackLoopClear: if (c.pressed) rackLoop(Rack::LoopClear); break;
    case MA_RackLoopVol: absolute(c, p.lpVol.load(), [&](float v) { p.lpVol = v; }); break;
    case MA_RackPresetNext: if (c.pressed) rackPresetStep(1); break;
    case MA_RackDrumPlay: { auto& ip = engine_.rack.instr.params; if (c.pressed) ip.drumPlay = !ip.drumPlay.load(); break; }
    case MA_RackBassPlay: { auto& ip = engine_.rack.instr.params; if (c.pressed) ip.bassPlay = !ip.bassPlay.load(); break; }
    case MA_RackDrumPatNext: { auto& ip = engine_.rack.instr.params; if (c.pressed) ip.drumPattern = (ip.drumPattern.load() + 1) % kDrumPatterns; break; }
    case MA_RackPresetPrev: if (c.pressed) rackPresetStep(-1); break;
    default:
        if (c.action >= MA_RackPad1 && c.action <= MA_RackPad16) {
            const int k = c.action - MA_RackPad1;
            if (pads_[k].track) padCmd(k, c.pressed ? 1 : 0);
        }
        break;
    }
}

// Clic derecho sobre un control: asignarlo a un control del mando con «Aprender».
void App::rackMidiMenu(const char* popup, std::initializer_list<int> actions) {
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup(popup);
    if (!ImGui::BeginPopup(popup)) return;
    ImGui::PushFont(fontHead, 13.f);
    ImGui::TextDisabled("MANDO MIDI");
    ImGui::PopFont();
    for (int a : actions) {
        ImGui::PushID(a);
        const MidiBinding* b = midi_.findBinding(a, 0);
        const std::string label = std::string("Aprender: ") + midiActions()[a].name;
        if (ImGui::MenuItem(label.c_str())) {
            wizStep_ = -1;
            midi_.startLearn(a, 0);
            rackLearning_ = a;
            notify(std::string("Mueve o pulsa el control del mando para «") + midiActions()[a].name + "»…");
        }
        if (b) {
            ImGui::TextDisabled("   ahora: %s", midiText(*b).c_str());
            if (ImGui::MenuItem("   Quitar la asignación")) midi_.clearBinding(a, 0);
        }
        ImGui::PopID();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Todas las asignaciones…")) showMidi_ = true;
    ImGui::EndPopup();
}

void App::rackDemo(const std::string& preset, int left, int right, float macro, int loopBars, bool touchXy) {
    for (size_t i = 0; i < rackPresets_.size(); ++i) if (rackPresets_[i].name == preset) rackApplyPreset((int)i);
    settings_.rackOpen = true;
    settings_.rackLeft = std::clamp(left, 0, 6);
    settings_.rackRight = std::clamp(right, 0, 6);
    engine_.rack.params.macro = std::clamp(macro, 0.f, 1.f);
    if (loopBars > 0) { engine_.rack.params.lpBars = loopBars; engine_.rack.params.lpAutoPlay = true; rackLoop(Rack::LoopRec, loopBars); }
    if (touchXy) { engine_.rack.params.xyX = 0.27f; engine_.rack.params.xyY = 0.68f; engine_.rack.params.xyHold = true; engine_.rack.params.xyTouch = true; }
}

void App::fxDemo(int fxA, int fxB, int color, float knobA, int tab) {
    const int fx[2] = {fxA, fxB};
    for (int d = 0; d < kNumDecks; ++d) {
        padMode_[d] = std::clamp(tab, 0, 7);   // 5: pestaña FX
        engine_.deck[d].fxType = std::clamp(fx[d], 0, (int)FxType::Count - 1);
        engine_.deck[d].fxOn = fx[d] > 0;
        engine_.deck[d].colorFx = std::clamp(color, 0, (int)ColorFxType::Count - 1);
    }
    engine_.deck[0].filter = std::clamp(knobA, -1.f, 1.f);
}

// =================================================================== dibujo

void App::drawRack(ImVec2 a, ImVec2 b, bool open) {
    drawDrumStudio();
    drawWorkbench();
    drawLiveWindow();
    const float s = S();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    RackParams& p = engine_.rack.params;
    const RackStatus& st = engine_.rack.status;
    Panel(dl, a, b, 10 * s, col::Panel, col::Line);
    BeginPanel("rack", a, b, 0.f);
    const float hh = 28 * s;
    // ---------------------------------------------------------- cabecera
    {
        float x = a.x + 6 * s;
        const float y = a.y + (hh - 22 * s) * 0.5f + 1 * s;
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        const bool noRoom = settings_.rackOpen && !open;   // quiere abrirse pero no cabe
        if (IconButton("##rackopen", open ? Ic::ChevDown : Ic::ChevRight, ImVec2(22 * s, 22 * s), col::Violet)) {
            if (noRoom) { settings_.deckHeight = 0; settings_.rackH = 0; settings_.waveRowH = 0; }   // alturas automáticas para que quepa
            else settings_.rackOpen = !settings_.rackOpen;
            settings_.save();
        }
        ImGui::SetItemTooltip(open ? "Plegar el rack" : noRoom ? "No cabe con las alturas elegidas: clic para ajustarlas solas y desplegarlo" : "Desplegar el rack");
        x += 28 * s;
        ImGui::SetCursorScreenPos(ImVec2(x,y));
        if (ImGui::Button("LIVE",ImVec2(48*s,22*s))) liveOpen_=!liveOpen_;
        ImGui::SetItemTooltip("Abrir banco de 8 escenas y transiciones de macro");
        x += fontBold->CalcTextSizeA(16 * s, FLT_MAX, 0, "RACK").x + 12 * s;
        // Pilotos de actividad de cada módulo
        bool fxAny = false;
        for (int k = 0; k < kStSlots; ++k) fxAny = fxAny || st.slotOnEff[k].load() || st.slotActive[k].load();
        const int ls = st.lpState.load();
        const bool blink = std::fmod(now_, 0.6) < 0.3;
        const ImU32 lpc = ls == (int)LoopState::Grabando || ls == (int)LoopState::Sobregrabando ? col::Red
                        : ls == (int)LoopState::Reproduciendo ? col::Green : col::Amber;
        const bool lpOn = ls == (int)LoopState::Grabando || ls == (int)LoopState::Sobregrabando || ls == (int)LoopState::Reproduciendo ||
                          ((ls == (int)LoopState::Armado || ls == (int)LoopState::ArmadoPlay) && blink);
        struct Pilot { const char* n; ImU32 c; bool on; };
        const bool mvOn = motion_.state == Motion::Grabando || motion_.state == Motion::Sonando || (motion_.state == Motion::Armado && blink);
        const Pilot pilots[5] = {{"FX", col::Violet, fxAny}, {"XY", col::DeckA, st.xyWeight.load() > 0.01f}, {"ISO", col::Amber, p.isoOn.load()}, {"LOOP", lpc, lpOn},
                                 {"MOV", motion_.state == Motion::Sonando ? col::Green : col::Red, mvOn}};
        for (const Pilot& pl : pilots) {
            Led(dl, ImVec2(x + 4 * s, a.y + hh * 0.5f), 3.5f * s, pl.c, pl.on);
            dl->AddText(fontHead, 12 * s, ImVec2(x + 11 * s, a.y + (hh - 12 * s) * 0.5f), pl.on ? col::Text : col::TextDim, pl.n);
            x += 18 * s + fontHead->CalcTextSizeA(12 * s, FLT_MAX, 0, pl.n).x;
        }
        // Presets
        x += 8 * s;
        const float presetW = std::clamp((b.x - x) * 0.32f, 150 * s, 230 * s);
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        ImGui::SetNextItemWidth(presetW);
        const std::string cur = rackPresetName_.empty() ? std::string("Presets…") : rackPresetName_;
        if (ImGui::BeginCombo("##rackpreset", cur.c_str(), ImGuiComboFlags_HeightLarge)) {
            for (size_t i = 0; i < rackPresets_.size(); ++i) {
                if (i > 0 && !rackPresets_[i].builtin && rackPresets_[i - 1].builtin) ImGui::Separator();
                ImGui::PushID((int)i);
                if (ImGui::Selectable(rackPresets_[i].name.c_str(), rackPresets_[i].name == rackPresetName_)) rackApplyPreset((int)i);
                if (!rackPresets_[i].note.empty()) ImGui::SetItemTooltip("%s", rackPresets_[i].note.c_str());
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("Presets: cadena de efectos, parámetros, fuentes y asignaciones del pad XY");
        rackMidiMenu("midipreset", {MA_RackPresetNext, MA_RackPresetPrev});
        ImGui::SameLine(0, 4 * s);
        if (NeonButton("Guardar…##rackpsave", col::Violet, false, ImVec2(70 * s, 22 * s))) {
            std::snprintf(rackNameBuf_, sizeof rackNameBuf_, "%s", rackPresetName_.empty() ? "Mi preset" : rackPresetName_.c_str());
            ImGui::OpenPopup("rackguardar");
        }
        ImGui::SetItemTooltip("Guardar el estado del rack como preset propio");
        if (ImGui::BeginPopup("rackguardar")) {
            ImGui::TextUnformatted("Nombre del preset");
            ImGui::SetNextItemWidth(220 * s);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            const bool enter = ImGui::InputText("##nombre", rackNameBuf_, sizeof rackNameBuf_, ImGuiInputTextFlags_EnterReturnsTrue);
            bool builtinName = false;
            for (auto& rp : rackPresets_) if (rp.builtin && rp.name == rackNameBuf_) builtinName = true;
            if (builtinName) ImGui::TextColored(toVec4(col::Amber), "Ese nombre es de un preset incluido.");
            if ((enter || ImGui::Button("Guardar")) && rackNameBuf_[0] && !builtinName) {
                bool found = false;
                for (auto& rp : rackPresets_) if (rp.name == rackNameBuf_) { rp.data = rackToJson(); found = true; }
                if (!found) rackPresets_.push_back({rackNameBuf_, rackToJson(), false, ""});
                rackPresetName_ = rackNameBuf_;
                saveRack();
                notify(std::string("Preset guardado: ") + rackNameBuf_);
                ImGui::CloseCurrentPopup();
            }
            int userIdx = -1;
            for (size_t i = 0; i < rackPresets_.size(); ++i) if (!rackPresets_[i].builtin && rackPresets_[i].name == rackPresetName_) userIdx = (int)i;
            if (userIdx >= 0) {
                ImGui::SameLine();
                if (ImGui::Button("Borrar el actual")) {
                    rackPresets_.erase(rackPresets_.begin() + userIdx);
                    notify("Preset borrado: " + rackPresetName_);
                    rackPresetName_.clear();
                    saveRack();
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
        // Derecha: consumo y EFECTOS OFF
        const float offW = 112 * s;
        ImGui::SetCursorScreenPos(ImVec2(b.x - offW - 8 * s, y));
        const bool cut = p.fxCut.load();
        if (NeonButton(cut ? "EFECTOS OFF##fxoff" : "EFECTOS ON##fxoff", cut ? col::Red : col::Green, cut, ImVec2(offW, 22 * s), true, fontHead, 13 * s))
            p.fxCut = !cut;
        ImGui::SetItemTooltip(cut ? "Efectos apagados (las colas de eco y reverb terminan solas). Pulsa para volver a encenderlos."
                                  : "Apagar todos los efectos y el aislador con un fundido suave (las colas siguen sonando)");
        rackMidiMenu("midifxoff", {MA_RackFxOff});
        char cpu[48];
        std::snprintf(cpu, sizeof cpu, "CPU rack %.1f %%", st.cpu.load() * 100.f);
        const float cw = ImGui::GetFont()->CalcTextSizeA(11.5f * s, FLT_MAX, 0, cpu).x;
        // 0.10.5: grabación de movimientos en la cabecera (siempre a mano, aunque no se vea el pad XY).
        const float mvX = x + presetW + 90 * s;
        const float mvMax = b.x - offW - 16 * s - mvX - 10 * s;
        const bool mvFits = mvMax >= 250 * s;
        if (mvFits) drawMotionControls(ImVec2(mvX, y), std::min(mvMax - (mvMax > 400 * s ? cw + 10 * s : 0.f), 330 * s), 22 * s);
        if (!mvFits || mvMax > 400 * s)
          if (b.x - offW - 16 * s - cw > x + presetW + 90 * s + (mvFits ? 340 * s : 0.f))
            dl->AddText(ImGui::GetFont(), 11.5f * s, ImVec2(b.x - offW - 16 * s - cw, a.y + (hh - 11.5f * s) * 0.5f), col::TextDim, cpu);
    }
    if (!open) { ImGui::EndChild(); return; }
    dl->AddLine(ImVec2(a.x + 1, a.y + hh), ImVec2(b.x - 1, a.y + hh), col::Line);
    // ---------------------------------------------------------- dos módulos (uno si no hay sitio)
    const float pad = 8 * s, gapW = 10 * s;
    const float bodyY = a.y + hh + 6 * s;
    const bool two = (b.x - a.x) > 900 * s;
    const float pw = two ? ((b.x - a.x) - 2 * pad - gapW) * 0.5f : (b.x - a.x) - 2 * pad;
    for (int panel = 0; panel < (two ? 2 : 1); ++panel) {
        int& sel = panel == 0 ? settings_.rackLeft : settings_.rackRight;
        int& other = panel == 0 ? settings_.rackRight : settings_.rackLeft;
        const float px = a.x + pad + panel * (pw + gapW);
        ImGui::PushID(panel);
        ImGui::SetCursorScreenPos(ImVec2(px, bodyY));
        int v = sel;
        const char* tips[7] = {"Estación de efectos: 4 ranuras encadenadas", "Pad XY: dos parámetros a la vez con el ratón", "Aislador de tres bandas",
                               "Loop station: graba compases del plato o de la mezcla", "Caja de ritmos de 16/32 pasos, sincronizada con el plato",
                               "Sampler de 16 pads (sonidos incluidos o tus archivos)", "Sintetizador de bajos tipo acid con secuenciador"};
        const float tabW = std::min(pw, 640 * s);
        if (Segmented("mods", &v, kModNames, 7, ImVec2(tabW, 22 * s), col::Violet, fontHead, std::min(12.5f * s, tabW / 7.f / 6.2f), tips)) {
            if (two && v == other) other = sel;   // el otro panel ya lo muestra: se intercambian
            sel = v;
            settings_.save();
        }
        const ImVec2 ma(px, bodyY + 28 * s), mb(px + pw, b.y - pad);
        if (mb.y - ma.y > 40 * s) {
            switch (sel) {
            case 0: drawRackStation(ma, mb); break;
            case 1: drawRackXy(ma, mb); break;
            case 2: drawRackIsolator(ma, mb); break;
            case 3: drawRackLooper(ma, mb); break;
            case 4: drawRackDrums(ma, mb); break;
            case 5: drawRackSampler(ma, mb); break;
            default: drawRackBass(ma, mb); break;
            }
        }
        ImGui::PopID();
        if (two && panel == 0) dl->AddLine(ImVec2(px + pw + gapW * 0.5f, bodyY), ImVec2(px + pw + gapW * 0.5f, b.y - pad), col::Line);
    }
    ImGui::EndChild();
}

// Fuente del módulo (A / B / MEZCLA) con el color de cada plato.
bool App::rackSource(const char* id, std::atomic<int>& v, float w, float h) {
    // Orden en pantalla: A, B, INSTR., MEZCLA (valores 0, 1, 3, 2).
    const char* items[4] = {"A", "B", "INSTR.", "MEZCLA"};
    const char* tips[4] = {"Plato A (antes de su fader)", "Plato B (antes de su fader)", "Los instrumentos del rack (caja de ritmos, bajo y sampler)",
                           "La mezcla completa (antes del volumen master)"};
    static const int toVal[4] = {0, 1, 3, 2}, toIdx[4] = {0, 1, 3, 2};
    int x = toIdx[std::clamp(v.load(), 0, 3)];
    const ImU32 acc = x == 0 ? col::DeckA : x == 1 ? col::DeckB : x == 2 ? col::Amber : col::Violet;
    if (Segmented(id, &x, items, 4, ImVec2(w, h), acc, fontHead, 11.5f * S(), tips)) { v = toVal[x]; return true; }
    return false;
}

void App::drawRackStation(ImVec2 a, ImVec2 b) {
    drawDrumStudio();
    drawWorkbench();
    drawLiveWindow();
    const float s = S();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    RackParams& p = engine_.rack.params;
    const RackStatus& st = engine_.rack.status;
    const ImU32 acc = col::Violet;
    const float rowH = 22 * s;
    // Fila de arriba: encendido, fuente, modo, retorno, BPM y nivel.
    float x = a.x;
    ImGui::SetCursorScreenPos(ImVec2(x, a.y));
    const bool on = p.stOn.load();
    if (NeonButton(on ? "ON##st" : "OFF##st", acc, on, ImVec2(40 * s, rowH), true, fontHead, 12 * s)) p.stOn = !on;
    ImGui::SetItemTooltip("Encender o apagar la estación (al apagar, las colas terminan solas)");
    x += 46 * s;
    ImGui::SetCursorScreenPos(ImVec2(x, a.y));
    rackSource("stsrc", p.stSrc, 172 * s, rowH);
    x += 178 * s;
    ImGui::SetCursorScreenPos(ImVec2(x, a.y));
    int mode = p.stMode.load();
    const char* modes[2] = {"INSERCIÓN", "ENVÍO"};
    const char* mtips[2] = {"Inserción: los efectos se aplican a la señal (mezcla original ↔ procesado)",
                            "Envío y retorno: el original no se toca; solo vuelven el eco, el delay y la reverb (y lo que colorean los efectos de antes)"};
    if (Segmented("stmode", &mode, modes, 2, ImVec2(140 * s, rowH), acc, fontHead, 12 * s, mtips)) p.stMode = mode;
    x += 146 * s;
    if (mode == 1) {
        float ret = p.stReturn.load();
        ImGui::SetCursorScreenPos(ImVec2(x, a.y - 1 * s));
        ImGui::SetNextItemWidth(80 * s);
        if (ImGui::SliderFloat("##ret", &ret, 0.f, 1.f, "RET %.2f")) p.stReturn = ret;
        ImGui::SetItemTooltip("Nivel del retorno de efectos");
        rackMidiMenu("midiret", {MA_RackReturn});
        x += 86 * s;
    }
    {
        // BPM al que van los tiempos (o 120 si la fuente no tiene rejilla) y nivel de salida.
        const float bpm = st.stBpm.load();
        const int bd = st.stBpmDeck.load();
        char t[48];
        if (bpm > 0) std::snprintf(t, sizeof t, "%.1f BPM · %s", bpm, bd == 0 ? "A" : bd == 1 ? "B" : "?");
        else std::snprintf(t, sizeof t, "120 BPM (sin rejilla)");
        const float tw = fontHead->CalcTextSizeA(12.5f * s, FLT_MAX, 0, t).x;
        const float mx = b.x - 60 * s;
        if (mx - tw - 8 * s > x) {
            dl->AddText(fontHead, 12.5f * s, ImVec2(mx - tw - 8 * s, a.y + 4 * s), bpm > 0 ? (bd == 1 ? col::DeckB : col::DeckA) : col::TextDim, t);
            ImGui::SetCursorScreenPos(ImVec2(mx - tw - 8 * s, a.y));
            ImGui::InvisibleButton("##bpmtip", ImVec2(tw, rowH));
            ImGui::SetItemTooltip("Los tiempos de eco, delay, flanger, phaser y gate siguen este BPM. Con la fuente MEZCLA manda el plato que más suena.");
        }
        float& lv = rackMeter_[0];
        lv = std::max(engine_.rack.status.stPeak.exchange(0.f), lv * 0.85f);
        HMeter(dl, ImVec2(b.x - 56 * s, a.y + 7 * s), ImVec2(b.x, a.y + 15 * s), lv, lv, lv > 1.f);
    }
    // Macro a la izquierda; las 4 ranuras en el orden de la cadena.
    const float bodyY = a.y + rowH + 8 * s;
    const float bodyH = b.y - bodyY;
    const float macroW = 70 * s;
    {
        const float r = std::clamp(bodyH * 0.22f, 16 * s, 26 * s);
        float m = p.macro.load();
        char tip[64];
        std::snprintf(tip, sizeof tip, "Macro %.0f %%", m * 100);
        ImGui::SetCursorScreenPos(ImVec2(a.x + (macroW - r * 2) * 0.5f, bodyY + 4 * s));
        float eff = m;
        if (p.xyTargetX.load() == XY_Macro || p.xyTargetY.load() == XY_Macro) {
            const float w = st.xyWeight.load(), xv = p.xyTargetX.load() == XY_Macro ? p.xyX.load() : p.xyY.load();
            eff = m + (xv - m) * w;
        }
        if (RackKnob("##macro", "MACRO", &m, 0.f, false, r, col::Amber, tip, eff)) p.macro = m;
        rackMidiMenu("midimacro", {MA_RackMacro});
        const float ty = bodyY + 4 * s + r * 2 + 18 * s;
        if (ty + 30 * s < b.y) {
            ImGui::SetCursorScreenPos(ImVec2(a.x, ty));
            ImGui::PushStyleColor(ImGuiCol_Text, toVec4(col::TextDim));
            ImGui::PushTextWrapPos(a.x + macroW);
            ImGui::PushFont(nullptr, 11.f);   // ImGui ya aplica la escala de la pantalla (antes 11·s: enorme al 200 %)
            ImGui::TextUnformatted("Clic derecho en una ranura: qué mueve la macro");
            ImGui::PopFont();
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
    }
    const float sx = a.x + macroW + 6 * s;
    const float gap = 6 * s;
    const float cardW = (b.x - sx - gap * (kStSlots - 1)) / kStSlots;
    const uint32_t order = p.stOrder.load();
    auto slotAt = [&](int pos) { return (int)((order >> (pos * 4)) & 0xF); };
    auto swapPos = [&](int i, int j) {
        uint32_t o = p.stOrder.load();
        const uint32_t ki = (o >> (i * 4)) & 0xF, kj = (o >> (j * 4)) & 0xF;
        o &= ~((0xFu << (i * 4)) | (0xFu << (j * 4)));
        o |= (kj << (i * 4)) | (ki << (j * 4));
        p.stOrder = o;
    };
    for (int pos = 0; pos < kStSlots; ++pos) {
        const int k = std::clamp(slotAt(pos), 0, kStSlots - 1);
        StSlotParams& sp = p.slot[k];
        const int type = sp.type.load();
        const bool effOn = st.slotOnEff[k].load(), active = st.slotActive[k].load();
        const ImVec2 ca(sx + pos * (cardW + gap), bodyY), cb(ca.x + cardW, b.y);
        const ImU32 border = effOn ? acc : active ? col::withAlpha(col::Amber, 160) : col::Line;
        dl->AddRectFilled(ca, cb, col::Panel2, 7 * s);
        dl->AddRect(ca, cb, border, 7 * s, 0, effOn ? 1.6f * s : 1.f);
        if (pos > 0) {   // flecha de la cadena
            const float yy = ca.y + 11 * s;
            dl->AddTriangleFilled(ImVec2(ca.x - gap + 1 * s, yy - 3 * s), ImVec2(ca.x - gap + 1 * s, yy + 3 * s), ImVec2(ca.x - 1 * s, yy), col::TextDim);
        }
        ImGui::PushID(k);
        // Botón de la ranura (número + encendido) y tipo
        ImGui::SetCursorScreenPos(ImVec2(ca.x + 4 * s, ca.y + 4 * s));
        char num[16];
        std::snprintf(num, sizeof num, "%d##on", k + 1);
        if (NeonButton(num, acc, effOn, ImVec2(24 * s, 20 * s), type != 0, fontHead, 13 * s)) sp.on = !sp.on.load();
        ImGui::SetItemTooltip(sp.on.load() ? "Ranura encendida (clic: apagar; la cola termina sola)" : effOn ? "Encendida por la macro o el pad XY" : "Encender la ranura");
        rackMidiMenu("midion", {MA_RackSlot1On + k});
        ImGui::SameLine(0, 4 * s);
        ImGui::SetNextItemWidth(cardW - 34 * s);
        if (ImGui::BeginCombo("##tipo", stFxName(type), ImGuiComboFlags_HeightLargest)) {
            // 0.11: por familias, con su descripción al pasar el ratón; los nuevos llevan «NUEVO».
            for (const StFxGroup& g : stFxGroups()) {
                if (g.title) ImGui::SeparatorText(g.title);
                for (int t : g.types) {
                    if (t < 0) break;
                    ImGui::PushID(t);
                    if (ImGui::Selectable(stFxName(t), t == type)) {
                        sp.type = t;
                        applyStFxDefaults(sp, t);
                    }
                    if (t != 0) ImGui::SetItemTooltip("%s", stFxDescription(t));
                    if (t >= (int)StFx::Riser) {
                        ImGui::SameLine();
                        ImGui::TextColored(toVec4(col::Amber), "NUEVO");
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndCombo();
        }
        if (type != 0) ImGui::SetItemTooltip("%s", stFxDescription(type));
        if (active && !effOn) {   // cola sonando
            dl->AddText(fontHead, 10.5f * s, ImVec2(cb.x - 28 * s, ca.y + 26 * s), col::Amber, "cola");
        }
        // Ruletas
        const float kr = std::clamp((cb.y - ca.y - 74 * s) * 0.5f, 12 * s, 20 * s);
        const float ky = ca.y + 30 * s;
        const float ls = ImGui::GetFontSize() * 0.72f;
        const float w1 = std::max(kr * 2, ImGui::GetFont()->CalcTextSizeA(ls, FLT_MAX, 0, stFxAmountLabel(type)).x);
        const float w2 = std::max(kr * 2, ImGui::GetFont()->CalcTextSizeA(ls, FLT_MAX, 0, "MEZCLA").x);
        const float kgap = 10 * s;
        const float kx = ca.x + (cardW - w1 - w2 - kgap) * 0.5f;
        ImGui::BeginDisabled(type == 0);
        {
            float am = sp.amount.load();
            char tip[96];
            std::snprintf(tip, sizeof tip, "%s", stFxAmountText(type, am).c_str());
            ImGui::SetCursorScreenPos(ImVec2(kx, ky));
            if (RackKnob("##amt", stFxAmountLabel(type), &am, 0.5f, type == (int)StFx::Filtro, kr, acc, tip, st.effAmount[k].load())) sp.amount = am;
            rackMidiMenu("midiamt", {MA_RackSlot1Amt + k});
            float mx = sp.mix.load();
            std::snprintf(tip, sizeof tip, "Mezcla %.0f %% procesado", mx * 100);
            ImGui::SetCursorScreenPos(ImVec2(kx + w1 + kgap, ky));
            if (RackKnob("##mix", "MEZCLA", &mx, 0.5f, false, kr, col::mix(acc, col::Text, 0.3f), tip, st.effMix[k].load())) sp.mix = mx;
            rackMidiMenu("midimix", {MA_RackSlot1Mix + k});
        }
        // Tiempo sincronizado y orden
        const float ry = cb.y - 24 * s;
        if (ry > ky + kr * 2 + 14 * s) {
            ImGui::SetCursorScreenPos(ImVec2(ca.x + 4 * s, ry));
            if (stFxUsesTime(type)) {
                int dv = sp.div.load();
                if (!stFxDivAllowed(type, dv)) { dv = stFxDefaultDiv(type); sp.div = dv; }   // p. ej. un preset viejo con 1/16 en el slicer
                ImGui::SetNextItemWidth(cardW - 8 * s - 2 * 18 * s - 8 * s);
                if (ImGui::BeginCombo("##div", stFxTimeText(type, dv).c_str(), ImGuiComboFlags_HeightLarge)) {
                    for (int i = 0; i < kStDivs; ++i)
                        if (stFxDivAllowed(type, i) && ImGui::Selectable(stFxTimeText(type, i).c_str(), i == dv)) sp.div = i;
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip("%s", type == (int)StFx::Riser ? "Cuántos compases tarda en subir (sincronizado con el BPM)"
                                          : type == (int)StFx::Slicer ? "Cuánto mide el trozo que se corta en 8 (4 pulsos = un compás)"
                                          : type == (int)StFx::Sidechain ? "Cada cuánto bombea (1 pulso = en cada bombo)"
                                          : "Tiempo sincronizado con el BPM (en pulsos: 1/4 = semicorchea, 1 = negra)");
            } else if (type != 0) {
                dl->AddText(ImGui::GetFont(), 11 * s, ImVec2(ca.x + 6 * s, ry + 4 * s), col::TextDim, "sin tiempo");
            }
            ImGui::EndDisabled();
            ImGui::SetCursorScreenPos(ImVec2(cb.x - 4 * s - 2 * 18 * s - 2 * s, ry));
            if (IconButton("##ml", Ic::ChevLeft, ImVec2(18 * s, 20 * s), col::TextDim, false, pos > 0)) swapPos(pos, pos - 1);
            ImGui::SetItemTooltip("Mover antes en la cadena (a la izquierda)");
            ImGui::SameLine(0, 2 * s);
            if (IconButton("##mr", Ic::ChevRight, ImVec2(18 * s, 20 * s), col::TextDim, false, pos < kStSlots - 1)) swapPos(pos, pos + 1);
            ImGui::SetItemTooltip("Mover después en la cadena (a la derecha)");
        } else ImGui::EndDisabled();
        // Clic derecho en la tarjeta: macro de esta ranura.
        ImGui::SetCursorScreenPos(ca);
        if (ImGui::IsMouseHoveringRect(ca, cb) && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !ImGui::IsAnyItemHovered()) ImGui::OpenPopup("macroslot");
        if (ImGui::BeginPopup("macroslot")) {
            ImGui::PushFont(fontHead, 13.f);
            ImGui::Text("RANURA %d · %s", k + 1, stFxName(type));
            ImGui::PopFont();
            ImGui::TextDisabled("Cuánto mueve la MACRO (de −100 %% a +100 %%):");
            float ma = sp.macroAmt.load() * 100.f, mm = sp.macroMix.load() * 100.f;
            ImGui::SetNextItemWidth(220 * s);
            if (ImGui::SliderFloat("intensidad", &ma, -100.f, 100.f, "%+.0f %%")) sp.macroAmt = ma / 100.f;
            ImGui::SetNextItemWidth(220 * s);
            if (ImGui::SliderFloat("mezcla", &mm, -100.f, 100.f, "%+.0f %%")) sp.macroMix = mm / 100.f;
            ImGui::TextDisabled("Con la macro subida, la ranura se enciende sola.");
            if (ImGui::Button("Sin macro")) { sp.macroAmt = 0.f; sp.macroMix = 0.f; }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
}

void App::drawRackXy(ImVec2 a, ImVec2 b) {
    drawDrumStudio();
    drawWorkbench();
    drawLiveWindow();
    const float s = S();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    RackParams& p = engine_.rack.params;
    const RackStatus& st = engine_.rack.status;
    const ImU32 acc = col::DeckA;
    const float side = std::min(b.y - a.y, (b.x - a.x) * 0.55f);
    const ImVec2 pa(a.x, a.y), pb(a.x + side * 1.35f > b.x - 200 * s ? a.x + side : a.x + side * 1.35f, a.y + side);
    // Superficie
    dl->AddRectFilled(pa, pb, col::mix(col::Bg, acc, 0.06f), 8 * s);
    for (int i = 1; i < 4; ++i) {
        const float gx = pa.x + (pb.x - pa.x) * i / 4.f, gy = pa.y + (pb.y - pa.y) * i / 4.f;
        dl->AddLine(ImVec2(gx, pa.y + 4 * s), ImVec2(gx, pb.y - 4 * s), col::withAlpha(col::Line, 160));
        dl->AddLine(ImVec2(pa.x + 4 * s, gy), ImVec2(pb.x - 4 * s, gy), col::withAlpha(col::Line, 160));
    }
    dl->AddRect(pa, pb, st.xyWeight.load() > 0.01f ? acc : col::Line, 8 * s, 0, 1.4f * s);
    ImGui::SetCursorScreenPos(pa);
    ImGui::InvisibleButton("##xypad", ImVec2(pb.x - pa.x, pb.y - pa.y));
    const bool active = ImGui::IsItemActive();
    if (active) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const float x = std::clamp((m.x - pa.x) / (pb.x - pa.x), 0.f, 1.f), y = std::clamp(1.f - (m.y - pa.y) / (pb.y - pa.y), 0.f, 1.f);
        p.xyX = x; p.xyY = y;
        p.xyTouch = true;
        rackMouseXy_ = true;
        rackTrail_.push_back({x, y, now_});
    } else if (rackMouseXy_) {
        rackMouseXy_ = false;
        if (!rackMidiTouchHeld_) p.xyTouch = false;
    }
    if (!active) ImGui::SetItemTooltip("Arrastra para mover los dos parámetros a la vez. Al soltar vuelven solos a su sitio (salvo con HOLD).");
    rackMidiMenu("midixypad", {MA_RackXyTouch, MA_RackXyX, MA_RackXyY});
    while (!rackTrail_.empty() && now_ - rackTrail_.front().t > 0.6) rackTrail_.pop_front();
    auto toScr = [&](float x, float y) { return ImVec2(pa.x + x * (pb.x - pa.x), pa.y + (1.f - y) * (pb.y - pa.y)); };
    for (size_t i = 1; i < rackTrail_.size(); ++i) {
        const float age = (float)((now_ - rackTrail_[i].t) / 0.6);
        dl->AddLine(toScr(rackTrail_[i - 1].x, rackTrail_[i - 1].y), toScr(rackTrail_[i].x, rackTrail_[i].y), col::withAlpha(acc, (int)(180 * (1 - age))), 3 * s);
    }
    // Punto: posición del dedo (con la fuerza con la que actúa) y punto de reposo (adonde vuelve).
    const int tx = p.xyTargetX.load(), ty = p.xyTargetY.load();
    auto baseOf = [&](int t) -> float {
        if (t == XY_Macro) return p.macro.load();
        if (t >= XY_Amount0 && t < XY_Amount0 + kStSlots) return p.slot[t - XY_Amount0].amount.load();
        if (t >= XY_Mix0 && t < XY_Mix0 + kStSlots) return p.slot[t - XY_Mix0].mix.load();
        if (t >= XY_IsoLow && t <= XY_IsoHigh) return p.isoGain[t - XY_IsoLow].load();
        return 0.5f;
    };
    const ImVec2 rest = toScr(baseOf(tx), baseOf(ty));
    dl->AddCircle(rest, 6 * s, col::withAlpha(col::TextDim, 200), 0, 1.5f * s);
    const float w = st.xyWeight.load();
    if (w > 0.01f) {
        const ImVec2 c = toScr(p.xyX.load(), p.xyY.load());
        dl->AddLine(ImVec2(c.x, pa.y + 2 * s), ImVec2(c.x, pb.y - 2 * s), col::withAlpha(acc, (int)(90 * w)));
        dl->AddLine(ImVec2(pa.x + 2 * s, c.y), ImVec2(pb.x - 2 * s, c.y), col::withAlpha(acc, (int)(90 * w)));
        if (col::Glow > 0.2f) dl->AddCircleFilled(c, 16 * s, col::withAlpha(acc, (int)(60 * w * col::Glow)));
        dl->AddCircleFilled(c, 8 * s, col::withAlpha(acc, (int)(255 * std::max(0.35f, w))));
    }
    // Rótulos de los ejes
    const std::string nx = "X: " + xyTargetName(tx), ny = "Y: " + xyTargetName(ty);
    dl->AddText(ImGui::GetFont(), 11 * s, ImVec2(pa.x + 6 * s, pb.y - 16 * s), col::TextDim, ellipsize(nx, pb.x - pa.x - 12 * s, ImGui::GetFont(), 11 * s).c_str());
    dl->AddText(ImGui::GetFont(), 11 * s, ImVec2(pa.x + 6 * s, pa.y + 4 * s), col::TextDim, ellipsize(ny, pb.x - pa.x - 12 * s, ImGui::GetFont(), 11 * s).c_str());
    // Columna derecha: destinos, HOLD y opciones
    const float cx = pb.x + 12 * s, cw = b.x - cx;
    if (cw < 120 * s) return;
    auto targetCombo = [&](const char* id, const char* label, std::atomic<int>& t, int midiAct) {
        dl->AddText(fontHead, 12 * s, ImGui::GetCursorScreenPos(), col::TextDim, label);
        ImGui::Dummy(ImVec2(0, 14 * s));
        ImGui::SetCursorScreenPos(ImVec2(cx, ImGui::GetCursorScreenPos().y));
        ImGui::SetNextItemWidth(std::min(cw, 240 * s));
        const std::string cur = xyTargetName(t.load());
        if (ImGui::BeginCombo(id, cur.c_str(), ImGuiComboFlags_HeightLarge)) {
            for (int i = 0; i < XY_Count; ++i) {
                std::string n = xyTargetName(i);
                if (i >= XY_Amount0 && i < XY_Amount0 + kStSlots) n += std::string(" (") + stFxName(p.slot[i - XY_Amount0].type.load()) + ")";
                if (i >= XY_Mix0 && i < XY_Mix0 + kStSlots) n += std::string(" (") + stFxName(p.slot[i - XY_Mix0].type.load()) + ")";
                if (ImGui::Selectable(n.c_str(), t.load() == i)) t = i;
            }
            ImGui::EndCombo();
        }
        rackMidiMenu(id, {midiAct});
    };
    ImGui::SetCursorScreenPos(ImVec2(cx, a.y));
    targetCombo("##ejex", "EJE X (horizontal)", p.xyTargetX, MA_RackXyX);
    ImGui::SetCursorScreenPos(ImVec2(cx, ImGui::GetCursorScreenPos().y + 6 * s));
    targetCombo("##ejey", "EJE Y (vertical)", p.xyTargetY, MA_RackXyY);
    ImGui::SetCursorScreenPos(ImVec2(cx, ImGui::GetCursorScreenPos().y + 8 * s));
    const bool hold = p.xyHold.load();
    if (NeonButton("HOLD##xyhold", acc, hold, ImVec2(70 * s, 26 * s), true, fontHead, 13 * s)) {
        if (hold) { p.xyHold = false; }
        else p.xyHold = true;
    }
    ImGui::SetItemTooltip("HOLD: el pad se queda donde lo sueltas (pulsa otra vez para volver)");
    rackMidiMenu("midihold", {MA_RackXyHold});
    ImGui::SameLine(0, 8 * s);
    bool autoOn = p.xyAutoOn.load();
    if (ImGui::Checkbox("Tocar enciende", &autoOn)) p.xyAutoOn = autoOn;
    ImGui::SetItemTooltip("Al tocar el pad se enciende la ranura (o el aislador) de destino, como en un Kaoss Pad");
    if (ImGui::GetCursorScreenPos().y + 16 * s < b.y) {
        ImGui::SetCursorScreenPos(ImVec2(cx, ImGui::GetCursorScreenPos().y + 4 * s));
        ImGui::TextDisabled("X %.2f · Y %.2f · %s", p.xyX.load(), p.xyY.load(), w > 0.01f ? (hold ? "HOLD" : "actuando") : "en reposo");
    }
}

void App::drawRackIsolator(ImVec2 a, ImVec2 b) {
    drawDrumStudio();
    drawWorkbench();
    drawLiveWindow();
    const float s = S();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    RackParams& p = engine_.rack.params;
    const RackStatus& st = engine_.rack.status;
    const ImU32 acc = col::Amber;
    const float rowH = 22 * s;
    ImGui::SetCursorScreenPos(a);
    const bool on = p.isoOn.load();
    if (NeonButton(on ? "ON##iso" : "OFF##iso", acc, on, ImVec2(40 * s, rowH), true, fontHead, 12 * s)) p.isoOn = !on;
    ImGui::SetItemTooltip("Encender el aislador (con todo a 0 dB no cambia el volumen)");
    rackMidiMenu("midiisoon", {MA_RackIsoOn});
    ImGui::SetCursorScreenPos(ImVec2(a.x + 46 * s, a.y));
    rackSource("isosrc", p.isoSrc, 172 * s, rowH);
    {
        float& lv = rackMeter_[1];
        lv = std::max(engine_.rack.status.isoPeak.exchange(0.f), lv * 0.85f);
        HMeter(dl, ImVec2(b.x - 56 * s, a.y + 7 * s), ImVec2(b.x, a.y + 15 * s), lv, lv, lv > 1.f);
    }
    // Tres ruletas grandes con su botón de corte
    const float bodyY = a.y + rowH + 8 * s;
    const float rightW = std::clamp((b.x - a.x) * 0.34f, 150 * s, 230 * s);
    const float kzW = (b.x - a.x) - rightW - 10 * s;
    const float colW = kzW / 3.f;
    const float r = std::clamp(std::min(colW * 0.32f, (b.y - bodyY - 50 * s) * 0.5f), 14 * s, 34 * s);
    const char* names[3] = {"GRAVES", "MEDIOS", "AGUDOS"};
    for (int band = 0; band < 3; ++band) {
        ImGui::PushID(band);
        const float cx = a.x + colW * band + colW * 0.5f;
        float g = p.isoGain[band].load();
        const bool kill = p.isoKill[band].load();
        const std::string tip = isoText(g, kill);
        ImGui::SetCursorScreenPos(ImVec2(cx - r, bodyY));
        const float effKnob = [&] {   // posición equivalente de la ganancia efectiva (pad XY)
            const float ge = st.isoEff[band].load();
            if (ge <= 1.f) return 0.5f * std::cbrt(std::max(0.f, ge));
            return 0.5f + lvlDb(ge) / 12.f;
        }();
        if (RackKnob("##g", names[band], &g, 0.5f, true, r, kill ? col::Red : acc, tip.c_str(), kill ? g : effKnob)) p.isoGain[band] = g;
        rackMidiMenu("midig", {MA_RackIsoLow + band});
        const float ky = bodyY + r * 2 + 18 * s;
        if (ky + 22 * s <= b.y) {
            ImGui::SetCursorScreenPos(ImVec2(cx - 32 * s, ky));
            if (NeonButton("CORTE##k", col::Red, kill, ImVec2(64 * s, 22 * s), true, fontHead, 12 * s)) p.isoKill[band] = !kill;
            ImGui::SetItemTooltip("Quitar la banda entera (vuelve suave al soltar)");
            rackMidiMenu("midik", {MA_RackIsoKillLow + band});
        }
        ImGui::PopID();
    }
    // Frecuencias de cruce (rangos seguros)
    const float rx = b.x - rightW;
    ImGui::SetCursorScreenPos(ImVec2(rx, bodyY));
    dl->AddText(fontHead, 12 * s, ImVec2(rx, bodyY), col::TextDim, "GRAVES | MEDIOS");
    ImGui::SetCursorScreenPos(ImVec2(rx, bodyY + 15 * s));
    ImGui::SetNextItemWidth(rightW);
    float lo = p.isoLowHz.load(), hi = p.isoHighHz.load();
    if (ImGui::SliderFloat("##xlo", &lo, kIsoLowMin, kIsoLowMax, "%.0f Hz", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp)) p.isoLowHz = lo;
    ImGui::SetItemTooltip("Cruce entre graves y medios (60–500 Hz). Doble clic para escribirlo.");
    dl->AddText(fontHead, 12 * s, ImVec2(rx, bodyY + 44 * s), col::TextDim, "MEDIOS | AGUDOS");
    ImGui::SetCursorScreenPos(ImVec2(rx, bodyY + 59 * s));
    ImGui::SetNextItemWidth(rightW);
    if (ImGui::SliderFloat("##xhi", &hi, kIsoHighMin, kIsoHighMax, "%.0f Hz", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp)) p.isoHighHz = hi;
    ImGui::SetItemTooltip("Cruce entre medios y agudos (1–8 kHz; como poco 2,5 veces el otro cruce)");
    if (bodyY + 92 * s < b.y) {
        ImGui::SetCursorScreenPos(ImVec2(rx, bodyY + 88 * s));
        if (ImGui::SmallButton("Todo a 0 dB")) { for (int band = 0; band < 3; ++band) { p.isoGain[band] = 0.5f; p.isoKill[band] = false; } }
        ImGui::SameLine();
        if (ImGui::SmallButton("Cruces por defecto")) { p.isoLowHz = 200.f; p.isoHighHz = 2500.f; }
    }
}

void App::drawRackLooper(ImVec2 a, ImVec2 b) {
    drawDrumStudio();
    drawWorkbench();
    drawLiveWindow();
    const float s = S();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    RackParams& p = engine_.rack.params;
    const RackStatus& st = engine_.rack.status;
    const float rowH = 22 * s;
    const int state = st.lpState.load();
    const bool blink = std::fmod(now_, 0.5) < 0.25;
    // Fila de arriba: fuente, compases y opciones
    ImGui::SetCursorScreenPos(a);
    rackSource("lpsrc", p.lpSrc, 172 * s, rowH);
    ImGui::SetCursorScreenPos(ImVec2(a.x + 180 * s, a.y));
    const int barsVals[4] = {1, 2, 4, 8};
    const char* barsTxt[4] = {"1", "2", "4", "8"};
    int bi = 1;
    for (int i = 0; i < 4; ++i) if (p.lpBars.load() == barsVals[i]) bi = i;
    if (Segmented("lpbars", &bi, barsTxt, 4, ImVec2(112 * s, rowH), col::Green, fontHead, 13 * s)) p.lpBars = barsVals[bi];
    ImGui::SetCursorScreenPos(ImVec2(a.x + 296 * s, a.y + 4 * s));
    dl->AddText(fontHead, 11.5f * s, ImGui::GetCursorScreenPos(), col::TextDim, "COMPASES");
    {
        float x = a.x + 362 * s;
        struct Opt { const char* label; std::atomic<bool>* v; const char* tip; };
        const Opt opts[4] = {{"CUANTIZAR", &p.lpQuant, "Empezar y entrar en el siguiente compás del plato"},
                             {"SEGUIR BPM", &p.lpFollow, "Si cambia el tempo del plato, el loop lo sigue (cambia también el tono, como un vinilo)"},
                             {"AURIC.", &p.lpCue, "El loop también suena por los auriculares"},
                             {"AUTO", &p.lpAutoPlay, "Al terminar de grabar, suena ya (se suma al plato: puede duplicar el sonido)"}};
        for (const Opt& o : opts) {
            const float w = fontHead->CalcTextSizeA(11.5f * s, FLT_MAX, 0, o.label).x + 14 * s;
            if (x + w > b.x) break;
            ImGui::SetCursorScreenPos(ImVec2(x, a.y));
            ImGui::PushID(o.label);
            if (NeonButton(o.label, col::Green, o.v->load(), ImVec2(w, rowH), true, fontHead, 11.5f * s)) *o.v = !o.v->load();
            ImGui::SetItemTooltip("%s", o.tip);
            ImGui::PopID();
            x += w + 4 * s;
        }
    }
    // Pantalla de estado: barra por compases, qué se graba y qué suena.
    const float dy = a.y + rowH + 8 * s;
    const float btnH = std::clamp((b.y - dy) * 0.3f, 24 * s, 32 * s);
    const float dh = std::max(26 * s, b.y - dy - btnH - 8 * s);
    const ImVec2 da(a.x, dy), db(b.x, dy + dh);
    dl->AddRectFilled(da, db, col::Bg, 6 * s);
    ImU32 sc = col::TextDim;
    std::string big = loopStateName(state);
    const int bars = st.lpBars.load() > 0 ? st.lpBars.load() : p.lpBars.load();
    switch ((LoopState)state) {
    case LoopState::Armado:
        sc = col::Amber;
        big = st.lpWaitingStopped.load() ? "ESPERANDO: EL PLATO ESTÁ PARADO" : "ESPERANDO EL COMPÁS PARA GRABAR";
        break;
    case LoopState::Grabando: sc = col::Red; big = "GRABANDO " + std::to_string(bars) + (bars == 1 ? " COMPÁS" : " COMPASES"); break;
    case LoopState::Parado: sc = col::Text; big = "LISTA · PULSA SONAR"; break;
    case LoopState::ArmadoPlay: sc = col::Amber; big = "ENTRA EN EL SIGUIENTE COMPÁS"; break;
    case LoopState::Reproduciendo: sc = col::Green; big = "SONANDO"; break;
    case LoopState::Sobregrabando: sc = col::Red; big = "SOBREGRABANDO"; break;
    default: sc = col::TextDim; big = "VACÍA · GRABAR " + std::to_string(p.lpBars.load()) + (p.lpBars.load() == 1 ? " COMPÁS" : " COMPASES"); break;
    }
    const bool flash = state == (int)LoopState::Armado || state == (int)LoopState::ArmadoPlay;
    const ImU32 tc = flash && !blink ? col::withAlpha(sc, 120) : sc;
    {
        // Icono del estado (dibujado, sin símbolos de texto)
        const ImVec2 ic(da.x + 17 * s, da.y + 14 * s);
        const bool rec = state == (int)LoopState::Armado || state == (int)LoopState::Grabando || state == (int)LoopState::Sobregrabando;
        if (rec) Led(dl, ic, 5 * s, sc, !flash || blink);
        else DrawIcon(dl, state == (int)LoopState::Vacio ? Ic::Record : state == (int)LoopState::Parado ? Ic::Stop : Ic::Play, ic, 13 * s, tc);
    }
    dl->AddText(fontHead, 15 * s, ImVec2(da.x + 28 * s, da.y + 6 * s), tc, big.c_str());
    if (st.lpTooLong.load() && state == (int)LoopState::Vacio)
        dl->AddText(ImGui::GetFont(), 11.5f * s, ImVec2(da.x + 10 * s, da.y + 26 * s), col::Amber, "No cabe: a ese BPM tantos compases pasan de 30 s. Elige menos compases.");
    if (st.lpBars.load() > 0 && state != (int)LoopState::Vacio) {
        char info[160];
        std::snprintf(info, sizeof info, "%d %s · %.1f BPM · %.2f s · %d %s%s", st.lpBars.load(), st.lpBars.load() == 1 ? "compás" : "compases", st.lpBpm.load(),
                      st.lpSeconds.load(), st.lpLayers.load(), st.lpLayers.load() == 1 ? "capa" : "capas",
                      std::fabs(st.lpRate.load() - 1.f) > 0.002f ? "" : "");
        std::string t = info;
        if (std::fabs(st.lpRate.load() - 1.f) > 0.002f) { char r[32]; std::snprintf(r, sizeof r, " · velocidad %.3f×", st.lpRate.load()); t += r; }
        const float tw = ImGui::GetFont()->CalcTextSizeA(11.5f * s, FLT_MAX, 0, t.c_str()).x;
        dl->AddText(ImGui::GetFont(), 11.5f * s, ImVec2(db.x - tw - 10 * s, da.y + 8 * s), col::TextDim, t.c_str());
    }
    // Barra con divisiones por compás y pulso
    if (dh > 40 * s) {
        const float by0 = db.y - 16 * s, by1 = db.y - 6 * s, bx0 = da.x + 10 * s, bx1 = db.x - 10 * s;
        dl->AddRectFilled(ImVec2(bx0, by0), ImVec2(bx1, by1), col::Panel2, 3 * s);
        const float pos = std::clamp(st.lpPos.load(), 0.f, 1.f);
        const bool showPos = state == (int)LoopState::Grabando || state == (int)LoopState::Reproduciendo || state == (int)LoopState::Sobregrabando;
        if (showPos) dl->AddRectFilled(ImVec2(bx0, by0), ImVec2(bx0 + (bx1 - bx0) * pos, by1), col::withAlpha(sc, 170), 3 * s);
        else if (state == (int)LoopState::Parado || state == (int)LoopState::ArmadoPlay)
            dl->AddRectFilled(ImVec2(bx0, by0), ImVec2(bx1, by1), col::withAlpha(col::Green, 50), 3 * s);
        const int beats = bars * 4;
        for (int i = 1; i < beats; ++i) {
            const float xx = bx0 + (bx1 - bx0) * i / beats;
            const bool barLine = i % 4 == 0;
            dl->AddLine(ImVec2(xx, barLine ? by0 - 3 * s : by0 + 2 * s), ImVec2(xx, by1), barLine ? col::Text : col::withAlpha(col::TextDim, 120), barLine ? 1.5f : 1.f);
        }
        float& lv = rackMeter_[2];
        lv = std::max(engine_.rack.status.lpPeak.exchange(0.f), lv * 0.85f);
        if (showPos) dl->AddRectFilled(ImVec2(bx0, by1 + 2 * s), ImVec2(bx0 + (bx1 - bx0) * std::min(1.f, lv), by1 + 4 * s), col::Green);
    }
    // Botones
    const float by = db.y + 8 * s;
    const float bw = ((b.x - a.x) - 5 * 6 * s - 90 * s) / 6.f;
    float x = a.x;
    auto btn = [&](const char* label, ImU32 c, bool on, bool enabled, const char* tip, int cmd, int midiAct, int arg = 0, Ic icon = Ic::None) {
        ImGui::SetCursorScreenPos(ImVec2(x, by));
        const char* hash = std::strstr(label, "##");
        const std::string text = hash ? std::string(label, hash - label) : std::string(label);
        bool clicked;
        if (icon != Ic::None) clicked = IconTextButton(hash ? hash : label, icon, text.c_str(), c, on, ImVec2(bw, btnH), enabled, fontHead, 13 * s);
        else clicked = NeonButton(label, c, on, ImVec2(bw, btnH), enabled, fontHead, 13 * s);
        if (clicked) rackLoop(cmd, arg);
        ImGui::SetItemTooltip("%s", tip);
        rackMidiMenu(label, {midiAct});
        x += bw + 6 * s;
    };
    const bool recOn = state == (int)LoopState::Grabando || (state == (int)LoopState::Armado && blink);
    btn(state == (int)LoopState::Armado || state == (int)LoopState::Grabando ? "CANCELAR##lrec" : "GRABAR##lrec", col::Red, recOn, true,
        "Grabar los compases elegidos de la fuente, desde el siguiente compás (pulsa otra vez para cancelar). Sustituye al loop anterior.", Rack::LoopRec,
        MA_RackLoopRec, p.lpBars.load(), Ic::Record);
    const bool playing = state == (int)LoopState::Reproduciendo || state == (int)LoopState::Sobregrabando || state == (int)LoopState::ArmadoPlay;
    btn(playing ? "PARAR##lplay" : "SONAR##lplay", col::Green, state == (int)LoopState::Reproduciendo || (state == (int)LoopState::ArmadoPlay && blink),
        state >= (int)LoopState::Parado, "Reproducir el loop (entra en el siguiente compás) o pararlo", Rack::LoopPlayStop, MA_RackLoopPlay, 0, playing ? Ic::Stop : Ic::Play);
    btn("CAPA##ldub", col::Red, state == (int)LoopState::Sobregrabando, state == (int)LoopState::Reproduciendo || state == (int)LoopState::Sobregrabando,
        "Sobregrabar una capa: lo que suena en la fuente se suma al loop mientras esté encendido", Rack::LoopDub, MA_RackLoopDub, 0, Ic::Layers);
    btn("DESHACER##lundo", col::Amber, false, st.lpCanUndo.load(), "Quitar la última capa sobregrabada", Rack::LoopUndo, MA_RackLoopUndo, 0, Ic::StepBack);
    btn("BORRAR##lclr", col::Red, false, state != (int)LoopState::Vacio, "Vaciar el loop", Rack::LoopClear, MA_RackLoopClear);
    {
        ImGui::SetCursorScreenPos(ImVec2(x, by));
        const bool canExport = (state == (int)LoopState::Parado || state == (int)LoopState::Reproduciendo) && !st.lpBusy.load();
        if (IconTextButton("##lexp", Ic::Download, "WAV", col::Violet, false, ImVec2(bw, btnH), canExport, fontHead, 13 * s)) rackLoopExport();
        ImGui::SetItemTooltip("Exportar el loop a WAV (carpeta de grabaciones y lista «Grabaciones»)");
        x += bw + 6 * s;
    }
    // Volumen
    ImGui::SetCursorScreenPos(ImVec2(b.x - 84 * s, by + (btnH - 20 * s) * 0.5f));
    float vol = p.lpVol.load();
    ImGui::SetNextItemWidth(84 * s);
    if (ImGui::SliderFloat("##lvol", &vol, 0.f, 1.f, "VOL %.2f")) p.lpVol = vol;
    ImGui::SetItemTooltip("Volumen del loop");
    rackMidiMenu("midilvol", {MA_RackLoopVol});
}

} // namespace rdj

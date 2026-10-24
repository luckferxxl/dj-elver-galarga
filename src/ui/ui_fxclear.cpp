// Botón «QUITAR FX» de la barra superior (0.11.1) y botón de efectos de cada plato (0.11.3): cuentan
// los efectos y filtros que se han quedado puestos (ruletas FILTER/COLOR de los canales, efectos de los
// platos, REV, ranuras del rack, macro, pad XY en HOLD, aislador y movimiento grabado), dicen cuáles
// son y los quitan de un clic.
// Las colas de eco y reverb terminan solas, como al apagar cada efecto a mano.
#include "app.h"
#include "ui_common.h"
#include "widgets.h"
#include <cmath>
#include <cstdio>

namespace rdj {

using namespace ui;

namespace {
float S() { return ImGui::GetFontSize() / 16.f; }
}

// onlyDeck < 0: todo lo puesto. onlyDeck = 0/1: lo que suena en ese plato (su ruleta, su efecto, REV,
// y el rack o el aislador cuando su fuente es ese plato).
std::vector<App::ActiveFx> App::activeEffects(int onlyDeck) const {
    std::vector<ActiveFx> v;
    char b[160];
    for (int d = 0; d < kNumDecks; ++d) {
        if (onlyDeck >= 0 && d != onlyDeck) continue;
        const DeckParams& p = engine_.deck[d];
        const int fx = p.fxType.load();
        if (p.fxOn.load() && fx > 0 && fx < (int)FxType::Count) {
            std::snprintf(b, sizeof b, "Plato %c: %s (pestaña FX)", d ? 'B' : 'A', fxName((FxType)fx));
            v.push_back({b, fxName((FxType)fx), ActiveFx::FxPlato, d});
        }
        if (p.reverse.load()) {
            std::snprintf(b, sizeof b, "Plato %c: REV, suena al revés (pestaña VINILO)", d ? 'B' : 'A');
            v.push_back({b, "REV", ActiveFx::Reverso, d});
        }
        // La ruleta después: en el botón del plato sale primero el efecto, que es el que se olvida.
        const float f = p.filter.load();
        if (std::fabs(f) >= 0.02f) {
            std::snprintf(b, sizeof b, "Canal %c: %s", d ? 'B' : 'A', colorFxTip(p.colorFx.load(), f).c_str());
            v.push_back({b, colorFxLabel(p.colorFx.load()), ActiveFx::Ruleta, d});
        }
    }
    const RackParams& rp = engine_.rack.params;
    const RackStatus& st = engine_.rack.status;
    const bool rackHere = onlyDeck < 0 || rp.stSrc.load() == onlyDeck;   // la estación del rack suena en este plato
    if (rackHere) {
        if (motion_.state == Motion::Sonando) v.push_back({"Rack: movimiento grabado sonando (mueve la macro o el pad)", "MOV.", ActiveFx::Movimiento, 0});
        if (rp.macro.load() > 0.01f) {
            std::snprintf(b, sizeof b, "Rack: MACRO al %.0f %%", rp.macro.load() * 100);
            v.push_back({b, "MACRO", ActiveFx::Macro, 0});
        }
        if (rp.xyHold.load()) v.push_back({"Rack: pad XY en HOLD", "XY HOLD", ActiveFx::PadXy, 0});
        for (int k = 0; k < kStSlots; ++k) {
            const int t = rp.slot[k].type.load();
            if (t == 0) continue;
            const bool on = rp.slot[k].on.load(), eff = st.slotOnEff[k].load();
            if (!on && !eff) continue;
            std::snprintf(b, sizeof b, "Rack, ranura %d: %s%s (sobre %s)", k + 1, stFxName(t), on ? "" : " (encendida por la macro o el pad)",
                          rackSrcName(rp.stSrc.load()));
            v.push_back({b, std::string("Rack: ") + stFxName(t), ActiveFx::Ranura, k});
        }
    }
    if (rp.isoOn.load() && (onlyDeck < 0 || rp.isoSrc.load() == onlyDeck)) {
        std::snprintf(b, sizeof b, "Rack: aislador encendido (sobre %s)", rackSrcName(rp.isoSrc.load()));
        v.push_back({b, "AISLADOR", ActiveFx::Aislador, 0});
    }
    return v;
}

void App::clearEffect(const ActiveFx& e) {
    RackParams& rp = engine_.rack.params;
    switch (e.kind) {
    case ActiveFx::Ruleta: engine_.deck[e.index].filter = 0.f; break;
    case ActiveFx::FxPlato: engine_.deck[e.index].fxOn = false; break;
    case ActiveFx::Reverso: engine_.deck[e.index].reverse = false; reverseHeld_[e.index] = false; break;
    case ActiveFx::Ranura: rp.slot[e.index].on = false; break;
    case ActiveFx::Macro: rp.macro = 0.f; break;
    case ActiveFx::PadXy: rp.xyHold = false; if (!rackMouseXy_ && !rackMidiTouchHeld_) rp.xyTouch = false; break;
    case ActiveFx::Aislador: rp.isoOn = false; break;
    case ActiveFx::Movimiento:
        // Como PARAR en la grabación de movimientos: deja de sonar y suelta el pad si lo tenía.
        motion_.state = Motion::Parado;
        if (motion_.xyOwned && !rackMouseXy_ && !rackMidiTouchHeld_) rp.xyTouch = false;
        motion_.xyOwned = false;
        break;
    }
}

// Quita lo de un plato (o todo con d < 0). Devuelve cuántos había.
int App::clearEffectsOf(int d) {
    const auto list = activeEffects(d);
    for (const auto& e : list) if (e.kind == ActiveFx::Movimiento) clearEffect(e);   // si siguiera sonando, volvería a subir la macro
    for (const auto& e : list) if (e.kind != ActiveFx::Movimiento) clearEffect(e);
    RackParams& rp = engine_.rack.params;
    const bool rackHere = d < 0 || rp.stSrc.load() == d;
    if (!list.empty() && rackHere) {
        // Lo que la macro o el pad encendían se apaga con ellos; las ranuras quedan apagadas.
        bool rackTouched = false;
        for (const auto& e : list) if (e.kind == ActiveFx::Ranura || e.kind == ActiveFx::Macro || e.kind == ActiveFx::PadXy || e.kind == ActiveFx::Movimiento) rackTouched = true;
        if (rackTouched || d < 0) {
            rp.macro = 0.f;
            rp.xyHold = false;
            if (!rackMouseXy_ && !rackMidiTouchHeld_) rp.xyTouch = false;
            for (auto& sl : rp.slot) sl.on = false;
        }
    }
    if (!list.empty()) {
        const std::string who = d < 0 ? "" : std::string(" del plato ") + (d ? "B" : "A");
        notify(list.size() == 1 ? "Efecto quitado" + who + ": " + list[0].what
                                : "Quitados " + std::to_string(list.size()) + " efectos y filtros" + who + " (las colas terminan solas)", false);
    }
    return (int)list.size();
}

int App::clearAllEffects() { return clearEffectsOf(-1); }

// Barra superior: se dibuja de derecha a izquierda (rx baja según se coloca).
void App::drawFxClearButton(float& rx, float by, float bh) {
    const float s = S();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const auto list = activeEffects();
    const int n = (int)list.size();
    char lbl[32];
    if (n) std::snprintf(lbl, sizeof lbl, "QUITAR FX  %d", n);
    else std::snprintf(lbl, sizeof lbl, "SIN FX");
    const float fs = 14 * s;
    const float w = fontHead->CalcTextSizeA(fs, FLT_MAX, 0, lbl).x + 26 * s;
    rx -= w + 8 * s;
    const ImVec2 p(rx, by), q(rx + w, by + bh);
    ImGui::SetCursorScreenPos(p);
    if (ImGui::InvisibleButton("##quitarfx", ImVec2(w, bh)) && n) clearAllEffects();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("listafx");
    const bool hov = ImGui::IsItemHovered();
    if (n) {
        // Encendido en ámbar mientras quede algo puesto; parpadea un poco para que se vea.
        const float pulse = 0.75f + 0.25f * (float)std::sin(now_ * 4.0);
        dl->AddRectFilled(p, q, col::withAlpha(col::Amber, (int)((hov ? 90 : 55) * pulse)), 7 * s);
        dl->AddRect(p, q, col::Amber, 7 * s, 0, 1.4f * s);
        CenterText(dl, p, q, col::Text, lbl, fontHead, fs);
    } else {
        dl->AddRectFilled(p, q, hov ? col::mix(col::Panel2, col::Line, 0.4f) : col::Panel2, 7 * s);
        dl->AddRect(p, q, col::Line, 7 * s, 0, 1.f);
        CenterText(dl, p, q, col::TextDim, lbl, fontHead, fs);
    }
    if (hov && !ImGui::IsPopupOpen("listafx")) {
        ImGui::BeginTooltip();
        if (n) {
            ImGui::TextUnformatted(n == 1 ? "Queda 1 efecto o filtro puesto:" : (std::to_string(n) + " efectos o filtros puestos:").c_str());
            for (const auto& e : list) ImGui::BulletText("%s", e.what.c_str());
            ImGui::Separator();
            ImGui::TextDisabled("Clic: quitarlos todos (las colas de eco y reverb terminan solas)");
            ImGui::TextDisabled("Clic derecho: elegir cuál quitar");
        } else {
            ImGui::TextUnformatted("No queda ningún efecto ni filtro puesto.");
            ImGui::TextDisabled("Aquí se encenderá en ámbar si se queda alguno puesto.");
        }
        ImGui::EndTooltip();
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(320 * s, 0), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::BeginPopup("listafx")) {
        ImGui::PushFont(fontHead, 13.f);
        ImGui::TextUnformatted("EFECTOS Y FILTROS PUESTOS");
        ImGui::PopFont();
        if (list.empty()) ImGui::TextDisabled("Ninguno.");
        for (size_t i = 0; i < list.size(); ++i) {
            ImGui::PushID((int)i);
            if (ImGui::MenuItem(("Quitar · " + list[i].what).c_str())) clearEffect(list[i]);
            ImGui::PopID();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quitar todos", nullptr, false, !list.empty())) clearAllEffects();
        ImGui::TextDisabled("También con un botón del mando: Mando MIDI → «Quitar todos los efectos».");
        ImGui::EndPopup();
    }
}

// Botón de efectos de cada plato, en la cabecera (a la derecha de la línea de tiempos). Siempre está:
// en gris «FX» si no hay nada; en ámbar con el nombre del efecto («Sidechain ×», «SPACE ×») o con
// cuántos hay («3 FX ×»). Clic: quita los de este plato. Clic derecho: elegir cuál. Devuelve la nueva x derecha.
float App::drawDeckFxButton(int d, float right, float y, float h, float maxW, float roomyW) {
    const float s = S();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const auto list = activeEffects(d);
    const int n = (int)list.size();
    const float fs = 12 * s, pad = 16 * s;
    auto width = [&](const std::string& t) { return fontHead->CalcTextSizeA(fs, FLT_MAX, 0, t.c_str()).x + pad; };
    const std::string x = "  \xC3\x97";   // ×
    std::string lbl = "FX";
    if (n == 1) {
        lbl = list[0].tag + x;
        if (width(lbl) > maxW) lbl = uic::ellipsize(list[0].tag, std::max(20 * s, maxW - width(x)), fontHead, fs) + x;
    } else if (n >= 2) {
        // Con sitio, los nombres («Sidechain + SPACE ×»); si no, el primero y cuántos más, o solo cuántos.
        std::string all = list[0].tag;
        for (int i = 1; i < n; ++i) all += " + " + list[i].tag;
        const std::string firstMore = list[0].tag + " +" + std::to_string(n - 1) + x;
        if (width(all + x) <= roomyW) lbl = all + x;
        else if (width(firstMore) <= roomyW) lbl = firstMore;
        else lbl = std::to_string(n) + " FX" + x;
    }
    const float w = width(lbl);
    const ImVec2 p(right - w, y), q(right, y + h);
    ImGui::SetCursorScreenPos(p);
    char id[24];
    std::snprintf(id, sizeof id, "##fxplato%d", d);
    if (ImGui::InvisibleButton(id, ImVec2(w, h)) && n) clearEffectsOf(d);
    std::snprintf(id, sizeof id, "listafx%c", d ? 'B' : 'A');
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup(id);
    const bool hov = ImGui::IsItemHovered();
    if (n) {
        const float pulse = 0.8f + 0.2f * (float)std::sin(now_ * 4.0);
        dl->AddRectFilled(p, q, col::mix(col::Panel2, col::Amber, (hov ? 0.95f : 0.85f) * pulse), 4 * s);
        CenterText(dl, p, q, col::onColor(col::Amber), lbl.c_str(), fontHead, fs);
    } else {
        dl->AddRect(p, q, hov ? col::TextDim : col::Line, 4 * s, 0, 1.f);
        CenterText(dl, p, q, hov ? col::Text : col::TextDim, lbl.c_str(), fontHead, fs);
    }
    if (hov && !ImGui::IsPopupOpen(id)) {
        ImGui::BeginTooltip();
        if (n) {
            ImGui::Text("En el plato %s %s:", d ? "B" : "A", n == 1 ? "queda puesto" : ("quedan puestos " + std::to_string(n)).c_str());
            for (const auto& e : list) ImGui::BulletText("%s", e.what.c_str());
            ImGui::Separator();
            ImGui::TextDisabled("Clic: quitarlos (solo los de este plato) · clic derecho: elegir cuál");
        } else {
            ImGui::Text("El plato %s no tiene ningún efecto ni filtro puesto.", d ? "B" : "A");
            ImGui::TextDisabled("Se pone en ámbar, con el nombre del efecto, si se queda alguno puesto.");
        }
        ImGui::EndTooltip();
    }
    if (ImGui::BeginPopup(id)) {
        ImGui::PushFont(fontHead, 13.f);
        ImGui::Text("EFECTOS DEL PLATO %s", d ? "B" : "A");
        ImGui::PopFont();
        if (list.empty()) ImGui::TextDisabled("Ninguno.");
        for (size_t i = 0; i < list.size(); ++i) {
            ImGui::PushID((int)i);
            if (ImGui::MenuItem(("Quitar · " + list[i].what).c_str())) clearEffect(list[i]);
            ImGui::PopID();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quitar todos los de este plato", nullptr, false, !list.empty())) clearEffectsOf(d);
        ImGui::TextDisabled("Con el mando: Mando MIDI → «Quitar los efectos del plato».");
        ImGui::EndPopup();
    }
    return right - w - 4 * s;
}

} // namespace rdj

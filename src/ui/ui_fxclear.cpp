// Botón «QUITAR FX» de la barra superior (0.11.1): cuenta los efectos y filtros que se han quedado
// puestos (ruletas FILTER/COLOR de los canales, efectos de los platos, ranuras del rack, macro,
// pad XY en HOLD, aislador y movimiento grabado), dice cuáles son y los quita de un clic.
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

std::vector<App::ActiveFx> App::activeEffects() const {
    std::vector<ActiveFx> v;
    char b[160];
    for (int d = 0; d < kNumDecks; ++d) {
        const DeckParams& p = engine_.deck[d];
        const float f = p.filter.load();
        if (std::fabs(f) >= 0.02f) {
            std::snprintf(b, sizeof b, "Canal %c: %s", d ? 'B' : 'A', colorFxTip(p.colorFx.load(), f).c_str());
            v.push_back({b, ActiveFx::Ruleta, d});
        }
        const int fx = p.fxType.load();
        if (p.fxOn.load() && fx > 0 && fx < (int)FxType::Count) {
            std::snprintf(b, sizeof b, "Plato %c: %s (pestaña FX)", d ? 'B' : 'A', fxName((FxType)fx));
            v.push_back({b, ActiveFx::FxPlato, d});
        }
    }
    const RackParams& rp = engine_.rack.params;
    const RackStatus& st = engine_.rack.status;
    if (motion_.state == Motion::Sonando) v.push_back({"Rack: movimiento grabado sonando (mueve la macro o el pad)", ActiveFx::Movimiento, 0});
    if (rp.macro.load() > 0.01f) {
        std::snprintf(b, sizeof b, "Rack: MACRO al %.0f %%", rp.macro.load() * 100);
        v.push_back({b, ActiveFx::Macro, 0});
    }
    if (rp.xyHold.load()) v.push_back({"Rack: pad XY en HOLD", ActiveFx::PadXy, 0});
    for (int k = 0; k < kStSlots; ++k) {
        const int t = rp.slot[k].type.load();
        if (t == 0) continue;
        const bool on = rp.slot[k].on.load(), eff = st.slotOnEff[k].load();
        if (!on && !eff) continue;
        std::snprintf(b, sizeof b, "Rack, ranura %d: %s%s (sobre %s)", k + 1, stFxName(t), on ? "" : " (encendida por la macro o el pad)",
                      rackSrcName(rp.stSrc.load()));
        v.push_back({b, ActiveFx::Ranura, k});
    }
    if (rp.isoOn.load()) {
        std::snprintf(b, sizeof b, "Rack: aislador encendido (sobre %s)", rackSrcName(rp.isoSrc.load()));
        v.push_back({b, ActiveFx::Aislador, 0});
    }
    return v;
}

void App::clearEffect(const ActiveFx& e) {
    RackParams& rp = engine_.rack.params;
    switch (e.kind) {
    case ActiveFx::Ruleta: engine_.deck[e.index].filter = 0.f; break;
    case ActiveFx::FxPlato: engine_.deck[e.index].fxOn = false; break;
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

int App::clearAllEffects() {
    const auto list = activeEffects();
    // El movimiento primero: si siguiera sonando volvería a subir la macro.
    for (const auto& e : list) if (e.kind == ActiveFx::Movimiento) clearEffect(e);
    for (const auto& e : list) if (e.kind != ActiveFx::Movimiento) clearEffect(e);
    // Lo que la macro o el pad encendían se apaga con ellos; las ranuras quedan apagadas.
    RackParams& rp = engine_.rack.params;
    if (!list.empty()) {
        rp.macro = 0.f;
        rp.xyHold = false;
        if (!rackMouseXy_ && !rackMidiTouchHeld_) rp.xyTouch = false;
        for (auto& sl : rp.slot) sl.on = false;
        notify(list.size() == 1 ? "Efecto quitado: " + list[0].what : "Quitados " + std::to_string(list.size()) + " efectos y filtros (las colas terminan solas)", false);
    }
    return (int)list.size();
}

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

} // namespace rdj

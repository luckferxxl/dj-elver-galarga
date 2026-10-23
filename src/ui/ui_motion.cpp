// Grabación de movimientos del rack (0.10.5): lo que haces con la MACRO y el pad XY durante 1, 2 o 4
// compases se graba y se repite en bucle, a tempo con la rejilla del plato que suena (como la
// «motion sequence» de los Kaoss Pad). Va en el hilo de la interfaz (60 veces por segundo, que basta
// para movimientos de la mano); el audio solo ve los mismos parámetros atómicos de siempre.
#include "app.h"
#include "ui_common.h"
#include "widgets.h"
#include <algorithm>
#include <cmath>

namespace rdj {

using namespace ui;

namespace {
constexpr int kSlotsPerBeat = 32;
float S() { return ImGui::GetFontSize() / 16.f; }
} // namespace

// Posición en pulsos de la rejilla del plato d (si suena y tiene rejilla).
bool App::motionBeat(int d, double& beat) const {
    if (d < 0 || d >= kNumDecks || !deck_[d].track || !engine_.deckStatus[d].playing.load()) return false;
    const double bpm = engine_.deck[d].bpm.load();
    if (bpm <= 0) return false;
    const double sec = engine_.deckStatus[d].posFrames.load() / deck_[d].track->sampleRate;
    beat = (sec - engine_.deck[d].gridAnchorSec.load()) * bpm / 60.0;
    return true;
}

void App::motionTick() {
    Motion& m = motion_;
    if (m.state == Motion::Vacio || m.state == Motion::Parado) return;
    RackParams& p = engine_.rack.params;
    double beat;
    if (!motionBeat(m.deck, beat)) {
        // El plato se ha parado: el otro, si suena, toma el relevo (el bucle sigue en su compás).
        const int o = 1 - m.deck;
        if (!motionBeat(o, beat)) return;
        m.deck = o;
        if (m.state == Motion::Grabando) { m.state = Motion::Armado; m.lastBeat = -1e9; return; }
        m.lastBeat = beat;
        return;
    }
    const int len = m.bars * 4 * kSlotsPerBeat;
    if (m.state == Motion::Armado) {
        // Empieza al principio del siguiente compás.
        const double bar = std::floor(beat / 4.0);
        if (m.lastBeat > -1e8 && std::floor(m.lastBeat / 4.0) != bar && beat - m.lastBeat < 1.0) {
            m.start = bar * 4.0;
            m.buf.assign(len, {p.macro.load(), p.xyX.load(), p.xyY.load(), 0.f});
            m.lastIdx = -1;
            m.hasXy = false;
            m.macroBefore = p.macro.load();
            m.state = Motion::Grabando;
        }
        m.lastBeat = beat;
        if (m.state != Motion::Grabando) return;
    }
    if (m.state == Motion::Grabando) {
        const int idx = (int)std::floor((beat - m.start) * kSlotsPerBeat);
        if (idx < 0 || beat - m.lastBeat > 2.0 || beat < m.lastBeat - 0.5) { m.state = Motion::Armado; m.lastBeat = beat; return; }   // salto: se vuelve a armar
        const bool touch = p.xyTouch.load();
        const std::array<float, 4> v{p.macro.load(), p.xyX.load(), p.xyY.load(), touch ? 1.f : 0.f};
        m.hasXy = m.hasXy || touch;
        for (int i = std::max(0, m.lastIdx + 1); i <= std::min(idx, len - 1); ++i) m.buf[i] = v;
        m.lastIdx = std::max(m.lastIdx, std::min(idx, len - 1));
        m.lastBeat = beat;
        if (idx >= len) { m.state = Motion::Sonando; m.lastMacro = -1; m.xyOwned = false; }
        return;
    }
    // Sonando. Se adelanta un fotograma (lo que tarda en aplicarse) para que caiga a tempo.
    const double ahead = std::clamp(beat - m.lastBeat, 0.0, 0.25);
    m.lastBeat = beat;
    double ph = std::fmod(beat + ahead - m.start, (double)m.bars * 4.0);
    if (ph < 0) ph += m.bars * 4.0;
    const int idx = std::clamp((int)(ph * kSlotsPerBeat), 0, len - 1);
    const auto& v = m.buf[idx];
    // Si tocas la macro (ratón o mando), mandas tú durante medio segundo.
    if (m.lastMacro >= 0 && std::fabs(p.macro.load() - m.lastMacro) > 1e-4f) m.userMacroAt = now_;
    if (now_ - m.userMacroAt > 0.5) { p.macro = v[0]; m.lastMacro = v[0]; }
    else m.lastMacro = p.macro.load();
    if (m.hasXy && !rackMouseXy_ && !rackMidiTouchHeld_) {
        if (v[3] > 0.5f) { p.xyX = v[1]; p.xyY = v[2]; p.xyTouch = true; m.xyOwned = true; }
        else if (m.xyOwned) { p.xyTouch = false; m.xyOwned = false; }
    }
}

void App::drawMotionControls(ImVec2 a, float w, float hIn) {
    const float s = S();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    Motion& m = motion_;
    RackParams& p = engine_.rack.params;
    const float h = hIn > 0 ? hIn : 24 * s;
    const bool blink = std::fmod(now_, 0.5) < 0.25;
    dl->AddText(fontHead, 11.5f * s, ImVec2(a.x, a.y + (h - 11.5f * s) * 0.5f), col::TextDim, "MOV.");
    float x = a.x + fontHead->CalcTextSizeA(11.5f * s, FLT_MAX, 0, "MOV.").x + 6 * s;
    const float bw = (a.x + w - x - 4 * 4 * s) / 5.f;
    // Compases
    static const int barsV[3] = {1, 2, 4};
    for (int i = 0; i < 3; ++i) {
        ImGui::SetCursorScreenPos(ImVec2(x, a.y));
        char l[16];
        std::snprintf(l, sizeof l, "%d##mvb%d", barsV[i], i);
        const bool busy = m.state == Motion::Grabando || m.state == Motion::Armado;
        if (NeonButton(l, col::Violet, m.bars == barsV[i], ImVec2(bw * 0.55f, h), !busy, fontHead, 12 * s)) {
            if (m.bars != barsV[i]) { m.bars = barsV[i]; if (m.state == Motion::Sonando || m.state == Motion::Parado) { m.state = Motion::Vacio; m.buf.clear(); } }
        }
        ImGui::SetItemTooltip("Grabar %d compás%s de movimiento", barsV[i], barsV[i] > 1 ? "es" : "");
        x += bw * 0.55f + 2 * s;
    }
    x += 4 * s;
    const float rest = a.x + w - x;
    const float b2 = (rest - 2 * 4 * s) / 3.f;
    // GRABAR
    ImGui::SetCursorScreenPos(ImVec2(x, a.y));
    const bool rec = m.state == Motion::Grabando, armed = m.state == Motion::Armado;
    if (NeonButton(rec ? "GRABANDO##mvrec" : armed ? "ESPERA##mvrec" : "GRABAR##mvrec", col::Red, rec || (armed && blink), ImVec2(b2, h), true, fontHead, 11.5f * s)) {
        if (rec || armed) { m.state = m.buf.empty() || rec ? Motion::Vacio : Motion::Parado; if (rec) m.buf.clear(); }
        else {
            int d = 0;
            double bt;
            const float a0 = engine_.deck[0].fader.load(), a1 = engine_.deck[1].fader.load();
            const bool p0 = motionBeat(0, bt), p1 = motionBeat(1, bt);
            d = p0 && p1 ? (a1 > a0 ? 1 : 0) : p1 ? 1 : 0;
            if (!p0 && !p1) notify("Para grabar un movimiento tiene que sonar un plato con su BPM (el bucle va a su compás).", true);
            else { m.deck = d; m.state = Motion::Armado; m.lastBeat = -1e9; }
        }
    }
    ImGui::SetItemTooltip("Graba lo que hagas con la MACRO y el pad XY durante %d compás%s.\nEmpieza en el siguiente compás y después suena en bucle.", m.bars, m.bars > 1 ? "es" : "");
    x += b2 + 4 * s;
    // SONAR / PARAR
    ImGui::SetCursorScreenPos(ImVec2(x, a.y));
    const bool playing = m.state == Motion::Sonando;
    const bool has = !m.buf.empty() && (m.state == Motion::Parado || playing);
    if (NeonButton(playing ? "PARAR##mvplay" : "SONAR##mvplay", col::Green, playing, ImVec2(b2, h), has, fontHead, 11.5f * s)) {
        if (playing) {
            m.state = Motion::Parado;
            p.macro = m.macroBefore;
            if (m.xyOwned && !rackMouseXy_ && !rackMidiTouchHeld_) p.xyTouch = false;
            m.xyOwned = false;
        } else { m.state = Motion::Sonando; m.lastMacro = -1; }
    }
    ImGui::SetItemTooltip(playing ? "Parar el movimiento (la macro vuelve a como estaba)" : "Repetir el movimiento grabado, a tempo");
    x += b2 + 4 * s;
    ImGui::SetCursorScreenPos(ImVec2(x, a.y));
    if (NeonButton("BORRAR##mvclr", col::Amber, false, ImVec2(b2, h), !m.buf.empty(), fontHead, 11.5f * s)) {
        if (m.state == Motion::Sonando) { p.macro = m.macroBefore; if (m.xyOwned && !rackMouseXy_) p.xyTouch = false; }
        m = Motion{};
    }
    // Barra de progreso del bucle
    if ((playing || rec) && !m.buf.empty()) {
        double bt;
        if (motionBeat(m.deck, bt)) {
            double ph = std::fmod(bt - m.start, (double)m.bars * 4.0);
            if (ph < 0) ph += m.bars * 4.0;
            const float f = (float)(ph / (m.bars * 4.0));
            dl->AddRectFilled(ImVec2(a.x, a.y + h + 1 * s), ImVec2(a.x + w * f, a.y + h + 3 * s), rec ? col::Red : col::Green, 1 * s);
        }
    }
}

} // namespace rdj

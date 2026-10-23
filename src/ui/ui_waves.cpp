// Formas de onda (0.6.1): ondas grandes por capas de bandas (graves, medios y
// agudos, como en el software de cabina profesional), rejilla de pulsos,
// compases y frases, cortes y subidones marcados, vista general de la pista,
// medidor de fase entre platos y la barra compacta de decks.
#include "app.h"
#include "covers.h"
#include "ui_common.h"
#include "widgets.h"
#include "../core/util.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rdj {

using namespace ui;
using uic::ellipsize;

namespace {
float S() { return ImGui::GetFontSize() / 16.f; }

const ImU32 kHotColors[kNumHotCues] = {IM_COL32(43, 227, 192, 255), IM_COL32(70, 145, 255, 255), IM_COL32(155, 110, 255, 255), IM_COL32(255, 90, 170, 255),
                                       IM_COL32(255, 92, 88, 255),  IM_COL32(255, 166, 77, 255), IM_COL32(250, 215, 80, 255),  IM_COL32(110, 220, 110, 255)};

// Curva de amplitud: realza lo bajo (charles, cortes) sin aplastar los golpes.
const float* ampCurve() {
    static float lut[256];
    static bool init = false;
    if (!init) { for (int i = 0; i < 256; ++i) lut[i] = std::pow(i / 255.f, 0.72f); init = true; }
    return lut;
}

ImU32 scaleRgb(ImU32 c, float k) {
    auto ch = [&](int sh) { return (int)std::clamp(((c >> sh) & 255) * k, 0.f, 255.f); };
    return IM_COL32(ch(IM_COL32_R_SHIFT), ch(IM_COL32_G_SHIFT), ch(IM_COL32_B_SHIFT), (c >> IM_COL32_A_SHIFT) & 255);
}

struct BandColors { ImU32 low, mid, high; };
BandColors bandColors(int style, ImU32 acc) {
    if (style == 1) return {col::WaveLow, col::WaveMid, col::WaveHigh};
    // Color del plato: graves en el color del plato, medios más claros, agudos casi blancos.
    return {col::mix(acc, IM_COL32(0, 0, 0, 255), 0.18f), col::mix(acc, IM_COL32(255, 255, 255, 255), 0.45f), col::mix(acc, IM_COL32(255, 255, 255, 255), 0.85f)};
}

// Color "espectro" (estilo RGB): graves rojo, medios verde, agudos azul, normalizado.
ImU32 rgbColor(float lo, float mi, float hi, float bright) {
    const float mx = std::max({lo, mi, hi, 1e-4f});
    const float r = lo / mx, g = mi / mx, b = hi / mx;
    auto chn = [&](float v, ImU32 c, int sh) { return v * ((c >> sh) & 255); };
    const float R = chn(r, col::WaveLow, IM_COL32_R_SHIFT) + chn(g, col::WaveMid, IM_COL32_R_SHIFT) + chn(b, col::WaveHigh, IM_COL32_R_SHIFT);
    const float G = chn(r, col::WaveLow, IM_COL32_G_SHIFT) + chn(g, col::WaveMid, IM_COL32_G_SHIFT) + chn(b, col::WaveHigh, IM_COL32_G_SHIFT);
    const float B = chn(r, col::WaveLow, IM_COL32_B_SHIFT) + chn(g, col::WaveMid, IM_COL32_B_SHIFT) + chn(b, col::WaveHigh, IM_COL32_B_SHIFT);
    const float m = std::max({R, G, B, 1.f});
    const float k = 255.f / m * bright;
    return IM_COL32((int)std::clamp(R * k, 0.f, 255.f), (int)std::clamp(G * k, 0.f, 255.f), (int)std::clamp(B * k, 0.f, 255.f), 255);
}

inline void vbar(ImDrawList* dl, float x, float cy, float h, ImU32 c) {
    if (h < 0.5f) return;
    dl->AddRectFilled(ImVec2(x, cy - h), ImVec2(x + 1.f, cy + h), c);
}
} // namespace

// =================================================================== utilidades de tempo y fase

float App::waveRate(int d) const {
    const float r = 1.f + engine_.deck[d].pitch.load() + engine_.deck[d].bend.load();
    return std::max(0.25f, r);
}

double App::phaseOffset(int d, bool* valid) const {
    if (valid) *valid = false;
    const int o = 1 - d;
    const DeckUi& a = deck_[d];
    const DeckUi& b = deck_[o];
    if (!a.track || !b.track || a.meta.bpm <= 0 || b.meta.bpm <= 0) return 0;
    double k = 1.0;
    if (a.sync) k = engine_.deck[d].syncFactor.load();
    else {
        // Sin SYNC: la relación (½, 1, 2) que deja los tempos más parecidos.
        const double ea = a.meta.bpm * waveRate(d), eb = b.meta.bpm * waveRate(o);
        double best = 1e9;
        for (double f : {0.5, 1.0, 2.0}) { const double dv = std::fabs(ea * f / eb - 1.0); if (dv < best) { best = dv; k = f; } }
        if (best > 0.08) return 0;  // tempos que no encajan: no tiene sentido medir fase
    }
    const double beatA = 60.0 / a.meta.bpm / k, beatB = 60.0 / b.meta.bpm;
    double ph = (deckPosSec(d) - a.meta.gridAnchorSec) / beatA - (deckPosSec(o) - b.meta.gridAnchorSec) / beatB;
    ph -= std::floor(ph + 0.5);
    if (valid) *valid = true;
    return ph;
}

void App::drawPhaseMeter(int d, ImVec2 a, ImVec2 b) {
    const float s = S();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool ok = false;
    const double off = phaseOffset(d, &ok);
    const float w = b.x - a.x, cx = (a.x + b.x) * 0.5f, cy = (a.y + b.y) * 0.5f;
    dl->AddRectFilled(a, b, col::withAlpha(col::Bg, 220), 2 * s);
    for (int i = 1; i < 4; ++i) {  // cuartos de pulso
        const float x = a.x + w * i / 4.f;
        dl->AddLine(ImVec2(x, a.y + 1), ImVec2(x, b.y - 1), col::withAlpha(col::TextDim, i == 2 ? 160 : 60), 1.f);
    }
    if (!ok) return;
    const double ao = std::fabs(off);
    const ImU32 c = ao < 0.02 ? col::Green : ao < 0.08 ? col::Amber : col::Red;
    const float x = cx + (float)off * w;
    if (ao < 0.02) dl->AddRectFilled(ImVec2(cx - 3 * s, a.y), ImVec2(cx + 3 * s, b.y), c, 2 * s);
    else {
        dl->AddRectFilled(ImVec2(std::min(cx, x), cy - 1.f * s), ImVec2(std::max(cx, x), cy + 1.f * s), col::withAlpha(c, 150));
        dl->AddRectFilled(ImVec2(x - 2.5f * s, a.y), ImVec2(x + 2.5f * s, b.y), c, 2 * s);
    }
    if (ImGui::IsMouseHoveringRect(a, ImVec2(b.x, b.y + 2 * s))) {
        const double ms = off * 60.0 / std::max(1.0, deck_[1 - d].meta.bpm * waveRate(1 - d)) * 1000.0;
        ImGui::SetTooltip("Fase con el plato %s: %+.0f ms (%+.2f pulsos)%s\nCentro = golpes a la vez. A la derecha: este plato va adelantado.", d ? "A" : "B", ms, off,
                          deck_[d].sync ? "\nSYNC lo mantiene en fase" : "");
    }
}

// =================================================================== estructura de la pista (cortes y subidones)

const App::TrackShape& App::trackShape(int d) {
    TrackShape& sh = shape_[d];
    const DeckUi& dk = deck_[d];
    const Track* t = dk.track.get();
    if (sh.track == t && sh.bpm == dk.meta.bpm && sh.anchor == dk.meta.gridAnchorSec) return sh;
    sh = TrackShape{};
    sh.track = t; sh.bpm = dk.meta.bpm; sh.anchor = dk.meta.gridAnchorSec;
    if (!t || t->wave.empty() || dk.meta.bpm <= 0) return sh;
    const double bar = 4 * 60.0 / dk.meta.bpm;
    const double sr = t->sampleRate;
    const int bins = (int)t->wave.size();
    sh.firstBar = (int)std::floor(-dk.meta.gridAnchorSec / bar);
    const int lastBar = (int)std::floor((t->lengthSec() - dk.meta.gridAnchorSec) / bar);
    const int nb = lastBar - sh.firstBar + 1;
    if (nb <= 0 || nb > 4000) return sh;
    std::vector<float> low(nb, 0.f), rms(nb, 0.f);
    for (int i = 0; i < nb; ++i) {
        const double t0 = dk.meta.gridAnchorSec + (sh.firstBar + i) * bar;
        const int b0 = std::max(0, (int)(t0 * sr / kWaveFramesPerBin)), b1 = std::min(bins, (int)((t0 + bar) * sr / kWaveFramesPerBin));
        double sl = 0, sr2 = 0;
        for (int k = b0; k < b1; ++k) { sl += t->wave[k].low; sr2 += t->wave[k].rms; }
        const int n = std::max(1, b1 - b0);
        low[i] = (float)(sl / n); rms[i] = (float)(sr2 / n);
    }
    // Referencia: graves típicos de las partes con bombo (percentil 70 de los compases con sonido).
    std::vector<float> loud;
    float maxRms = 1e-3f;
    for (int i = 0; i < nb; ++i) maxRms = std::max(maxRms, rms[i]);
    for (int i = 0; i < nb; ++i) if (rms[i] > 0.15f * maxRms) loud.push_back(low[i]);
    if (loud.size() < 8) return sh;
    std::sort(loud.begin(), loud.end());
    const float ref = loud[(size_t)(loud.size() * 0.7)];
    if (ref <= 1.f) return sh;
    sh.barKind.assign(nb, 0);
    for (int i = 0; i < nb; ++i) {
        if (rms[i] < 0.06f * maxRms) sh.barKind[i] = 2;          // silencio
        else if (low[i] < 0.38f * ref) sh.barKind[i] = 1;        // sin bombo: corte / break
    }
    // Solo cuentan los cortes de 4 compases o más; el subidón es donde vuelven los graves.
    for (int i = 0; i < nb;) {
        if (sh.barKind[i] != 1) { ++i; continue; }
        int j = i;
        while (j < nb && sh.barKind[j] == 1) ++j;
        if (j - i < 4) { for (int k = i; k < j; ++k) sh.barKind[k] = 0; }
        else if (j < nb && sh.barKind[j] == 0 && low[j] >= 0.6f * ref && i > 0) sh.drops.push_back(sh.firstBar + j);
        i = j;
    }
    return sh;
}

const App::OverviewData& App::overviewData(int d, int width) {
    TrackShape& sh = shape_[d];
    trackShape(d);
    for (auto& ov : sh.ov) if (ov.width == width && !ov.low.empty()) return ov;
    OverviewData& ov = sh.ov[sh.ovNext];
    sh.ovNext = (sh.ovNext + 1) % 3;
    ov = OverviewData{};
    ov.width = width;
    const Track* t = deck_[d].track.get();
    if (!t || t->wave.empty() || width <= 0) return ov;
    const int bins = (int)t->wave.size();
    ov.low.resize(width); ov.mid.resize(width); ov.high.resize(width); ov.peak.resize(width);
    for (int x = 0; x < width; ++x) {
        const int b0 = (int)((double)x / width * bins), b1 = std::max(b0 + 1, (int)((double)(x + 1) / width * bins));
        double lo = 0, mi = 0, hi = 0;
        int pk = 0, n = 0;
        for (int k = b0; k < b1 && k < bins; ++k, ++n) {
            const WaveBin& w = t->wave[k];
            lo += w.low; mi += w.mid; hi += w.high;
            pk = std::max(pk, std::max({(int)w.low, (int)w.mid, (int)w.high}));
        }
        n = std::max(1, n);
        // Media (energía: muestra la estructura) y máximo (contorno tenue de los golpes).
        ov.low[x] = (uint8_t)(lo / n); ov.mid[x] = (uint8_t)(mi / n); ov.high[x] = (uint8_t)(hi / n); ov.peak[x] = (uint8_t)pk;
    }
    return ov;
}

// =================================================================== ondas grandes (arriba)

void App::drawWaveRows(ImVec2 a, ImVec2 b) {
    const float s = S();
    const float g = 6 * s;
    const float rowH = (b.y - a.y - g) * 0.5f;
    const bool lib = mode_ == 2;
    const float labW = lib ? std::clamp((b.x - a.x) * 0.24f, 260 * s, 380 * s) : 64 * s;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int d = 0; d < kNumDecks; ++d) {
        DeckUi& dk = deck_[d];
        const ImU32 acc = col::deck(d);
        const bool playing = engine_.deckStatus[d].playing.load();
        const ImVec2 ra(a.x, a.y + d * (rowH + g)), rb(b.x, ra.y + rowH);
        waveRect_[d][0] = ra; waveRect_[d][1] = rb;
        Panel(dl, ra, rb, 8 * s, col::Panel, playing ? col::mix(col::Line, acc, 0.45f) : col::Line);
        const float lx = ra.x + labW;
        dl->AddLine(ImVec2(lx, ra.y + 1), ImVec2(lx, rb.y - 1), col::Line);
        // Insignia del plato
        const float bs = lib ? std::min(24 * s, rowH - 12 * s) : std::clamp(rowH * 0.34f, 16 * s, 22 * s);
        const ImVec2 bp(ra.x + 8 * s, lib ? ra.y + (rowH - bs) * 0.5f : ra.y + 6 * s);
        dl->AddRectFilled(bp, ImVec2(bp.x + bs, bp.y + bs), acc, 4 * s);
        CenterText(dl, bp, ImVec2(bp.x + bs, bp.y + bs), col::onColor(acc), d ? "B" : "A", fontBold, bs * 0.68f);
        const int key = dk.track && dk.meta.key >= 0 ? dk.meta.key : -1;
        if (!lib) {
            // BPM, tonalidad y fase con el otro plato.
            float ty = bp.y + bs + 3 * s;
            const float fs1 = std::clamp(rowH * 0.2f, 11 * s, 14 * s);
            if (dk.track && dk.meta.bpm > 0 && ty + fs1 < rb.y - 10 * s) {
                char bpm[16];
                std::snprintf(bpm, sizeof bpm, "%.1f", engine_.effectiveBpm(d));
                dl->AddText(fontHead, fs1 * 1.1f, ImVec2(bp.x, ty), dk.sync ? col::Green : col::Text, bpm);
                ty += fs1 * 1.1f;
            }
            if (key >= 0 && ty + fs1 < rb.y - 10 * s) dl->AddText(fontHead, fs1, ImVec2(bp.x, ty), col::camelot(camelotIndex(key)), keyCamelot(key));
            drawPhaseMeter(d, ImVec2(ra.x + 6 * s, rb.y - 9 * s), ImVec2(lx - 6 * s, rb.y - 4 * s));
        } else {
            // Resumen del plato: reproducir, carátula, título, BPM y tiempo restante.
            const float pbw = std::min(30 * s, rowH - 12 * s);
            ImGui::SetCursorScreenPos(ImVec2(bp.x + bs + 8 * s, ra.y + (rowH - pbw) * 0.5f));
            ImGui::PushID(d);
            if (IconButton("##playlib", playing ? Ic::Pause : Ic::Play, ImVec2(pbw, pbw), acc, playing, dk.track != nullptr, 0.5f)) togglePlay(d);
            ImGui::PopID();
            float x = bp.x + bs + 8 * s + pbw + 8 * s;
            const float art = rowH - 12 * s;
            if (dk.track) {
                DrawCover(dl, dk.path, ImVec2(x, ra.y + 6 * s), ImVec2(x + art, ra.y + 6 * s + art), 4 * s, dk.title.c_str());
                x += art + 8 * s;
                char right[48];
                std::snprintf(right, sizeof right, "%.1f  ·  -%s", dk.meta.bpm > 0 ? engine_.effectiveBpm(d) : 0.0,
                              formatTime(std::max(0.0, deckLenSec(d) - deckPosSec(d)), false).c_str());
                const float rw = fontHead->CalcTextSizeA(15 * s, FLT_MAX, 0, right).x;
                const float tw2 = lx - x - rw - 16 * s;
                const float ty = ra.y + rowH * 0.5f;
                dl->AddText(fontHead, 15 * s, ImVec2(x, ty - 17 * s), col::Text, ellipsize(dk.title, tw2, fontHead, 15 * s).c_str());
                dl->AddText(ImGui::GetFont(), 13 * s, ImVec2(x, ty + 1 * s), col::TextDim, ellipsize(dk.artist, tw2, ImGui::GetFont(), 13 * s).c_str());
                dl->AddText(fontHead, 15 * s, ImVec2(lx - rw - 10 * s, ty - 9 * s), dk.sync ? col::Green : col::Text, right);
            } else {
                dl->AddText(fontHead, 15 * s, ImVec2(x, ra.y + rowH * 0.5f - 9 * s), col::TextDim, loader_.busy(d) ? "Cargando…" : "Sin pista");
            }
        }
        drawZoomWave(d, ImVec2(lx + 1, ra.y + 1), ImVec2(rb.x - 1, rb.y - 1));
    }
}

void App::drawZoomWave(int d, ImVec2 a, ImVec2 b) {
    const float s = S();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    DeckUi& dk = deck_[d];
    ImGui::SetCursorScreenPos(a);
    char id[16];
    std::snprintf(id, sizeof id, "##zw%d", d);
    ImGui::InvisibleButton(id, ImVec2(std::max(1.f, b.x - a.x), std::max(1.f, b.y - a.y)));
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("RDJ_PISTA")) {
            auto paths = uic::PayloadPaths(pl);
            if (!paths.empty()) loadToDeck(d, paths[0]);
        }
        ImGui::EndDragDropTarget();
    }
    dl->AddRectFilled(a, b, col::WaveBg, 4 * s);
    Track* t = dk.track.get();
    if (!t) {
        CenterText(dl, a, b, col::TextDim, d == 0 ? "Plato A vacío · arrastra aquí una pista del navegador o del Explorador"
                                                  : "Plato B vacío · arrastra aquí una pista del navegador o del Explorador");
        return;
    }
    const ImU32 acc = col::deck(d);
    const float W = b.x - a.x, H = b.y - a.y;
    const float zoom = settings_.waveZoomSec;            // segundos reales a la vista
    const float rate = waveRate(d);
    const float pxPerSec = W / zoom / rate;              // píxeles por segundo de pista: a igual tempo, los pulsos de A y B coinciden en pantalla
    const float cx = a.x + W * 0.5f, cy = a.y + H * 0.5f;
    const double pos = deckPosSec(d);
    const double sr = t->sampleRate;
    const float halfH = H * 0.5f - 2 * s;
    const int style = settings_.waveStyle;
    const BandColors bc = bandColors(style, acc);
    const float* lut = ampCurve();
    const TrackShape& shape = trackShape(d);
    const bool hasGrid = dk.meta.bpm > 0;
    const double beat = hasGrid ? 60.0 / dk.meta.bpm : 0, bar = 4 * beat;
    auto xOf = [&](double tsec) { return cx + (float)((tsec - pos) * pxPerSec); };

    dl->PushClipRect(a, b, true);
    // Cortes (compases sin bombo): fondo tenue.
    if (hasGrid && settings_.waveStructure && !shape.barKind.empty()) {
        for (size_t i = 0; i < shape.barKind.size(); ++i) {
            if (shape.barKind[i] != 1) continue;
            const double t0 = dk.meta.gridAnchorSec + (shape.firstBar + (int)i) * bar;
            const float x0 = xOf(t0), x1 = xOf(t0 + bar);
            if (x1 < a.x || x0 > b.x) continue;
            dl->AddRectFilled(ImVec2(x0, a.y), ImVec2(x1, b.y), col::withAlpha(col::Violet, 22));
        }
    }
    // Loop
    const DeckStatus& st = engine_.deckStatus[d];
    if (st.loopIn.load() >= 0 && st.loopOut.load() > st.loopIn.load()) {
        const float x0 = xOf(st.loopIn.load() / sr), x1 = xOf(st.loopOut.load() / sr);
        dl->AddRectFilled(ImVec2(x0, a.y), ImVec2(x1, b.y), st.loopActive.load() ? col::withAlpha(col::Green, 46) : IM_COL32(120, 120, 140, 26));
        dl->AddLine(ImVec2(x0, a.y), ImVec2(x0, b.y), col::withAlpha(col::Green, 200), 1.5f * s);
        dl->AddLine(ImVec2(x1, a.y), ImVec2(x1, b.y), col::withAlpha(col::Green, 200), 1.5f * s);
        if (st.loopActive.load() && dk.meta.bpm > 0 && x1 - x0 > 34 * s) {
            // Etiqueta del loop abajo: "LOOP 4" (pulsos).
            const double beats = (st.loopOut.load() - st.loopIn.load()) / (60.0 / dk.meta.bpm * sr);
            char lb[24];
            std::snprintf(lb, sizeof lb, "LOOP %.3g", beats);
            const float fz = 10.5f * s;
            const ImVec2 ts = fontHead->CalcTextSizeA(fz, FLT_MAX, 0, lb);
            const float lx = std::max(x0, a.x) + 3 * s;
            dl->AddRectFilled(ImVec2(lx, b.y - ts.y - 6 * s), ImVec2(lx + ts.x + 8 * s, b.y - 2 * s), col::withAlpha(col::Green, 220), 2 * s);
            dl->AddText(fontHead, fz, ImVec2(lx + 4 * s, b.y - ts.y - 4 * s), col::onColor(col::Green), lb);
        }
    }
    // Onda: una columna por píxel con las tres bandas superpuestas (graves detrás).
    for (float x = a.x; x < b.x; x += 1.f) {
        const double t0 = pos + (x - cx) / pxPerSec, t1 = t0 + 1.0 / pxPerSec;
        int b0 = (int)std::floor(t0 * sr / kWaveFramesPerBin), b1 = (int)std::floor(t1 * sr / kWaveFramesPerBin);
        if (b1 <= b0) b1 = b0 + 1;
        const int bins = (int)t->wave.size();
        if (b1 <= 0 || b0 >= bins) continue;
        b0 = std::max(b0, 0); b1 = std::min(b1, bins);
        int lo = 0, mi = 0, hi = 0;
        for (int k = b0; k < b1; ++k) { const WaveBin& w = t->wave[k]; lo = std::max(lo, (int)w.low); mi = std::max(mi, (int)w.mid); hi = std::max(hi, (int)w.high); }
        const bool played = t0 < pos;
        const float dim = played ? 0.62f : 1.f;
        const float L = lut[lo] * halfH, M = lut[mi] * halfH * 0.86f, Hh = lut[hi] * halfH * 0.62f;
        if (style == 2) {
            const float amp = std::max({L, M / 0.86f, Hh / 0.62f});
            const ImU32 c = rgbColor(lut[lo], lut[mi], lut[hi], dim);
            vbar(dl, x, cy, amp, c);
            vbar(dl, x, cy, amp * 0.42f, col::mix(c, IM_COL32(255, 255, 255, 255), 0.35f * dim));
        } else {
            vbar(dl, x, cy, L, scaleRgb(bc.low, dim));
            vbar(dl, x, cy, M, scaleRgb(bc.mid, dim));
            vbar(dl, x, cy, Hh, scaleRgb(bc.high, dim));
        }
    }
    // Rejilla, encima de la onda: compases a toda altura (cada 8, más marcados), pulsos como marcas arriba y abajo.
    const double tStart = pos - (cx - a.x) / pxPerSec, tEnd = pos + (b.x - cx) / pxPerSec;
    if (hasGrid && settings_.waveGrid) {
        const long long k0 = (long long)std::ceil((tStart - dk.meta.gridAnchorSec) / beat);
        for (long long k = k0;; ++k) {
            const double tb = dk.meta.gridAnchorSec + k * beat;
            if (tb > tEnd) break;
            const float x = std::floor(xOf(tb)) + 0.5f;
            const long long barIdx = (long long)std::floor((double)k / 4.0);
            if (((k % 4) + 4) % 4 == 0) {
                const bool phrase = ((barIdx % 8) + 8) % 8 == 0;
                dl->AddLine(ImVec2(x, a.y), ImVec2(x, b.y), phrase ? col::withAlpha(col::Text, 150) : col::withAlpha(col::Text, 80), phrase ? 2.f * s : 1.f);
            } else {
                const float tick = std::max(5 * s, H * 0.14f);
                dl->AddLine(ImVec2(x, a.y), ImVec2(x, a.y + tick), col::withAlpha(col::Text, 130), 1.f);
                dl->AddLine(ImVec2(x, b.y - tick), ImVec2(x, b.y), col::withAlpha(col::Text, 130), 1.f);
                dl->AddLine(ImVec2(x, a.y + tick), ImVec2(x, b.y - tick), col::withAlpha(col::Text, 22), 1.f);
            }
        }
    }
    // Frases: número de compás (cada 8) y subidones, encima de la onda.
    if (hasGrid && settings_.waveGrid) {
        const long long bar0 = (long long)std::floor((tStart - dk.meta.gridAnchorSec) / bar), bar1 = (long long)std::ceil((tEnd - dk.meta.gridAnchorSec) / bar);
        for (long long bi = bar0; bi <= bar1; ++bi) {
            if (((bi % 8) + 8) % 8 != 0 || bi < 0) continue;
            const float x = xOf(dk.meta.gridAnchorSec + bi * bar);
            char n[16];
            std::snprintf(n, sizeof n, "%lld", bi + 1);
            const float fs = 11 * s;
            const ImVec2 ts = fontHead->CalcTextSizeA(fs, FLT_MAX, 0, n);
            dl->AddRectFilled(ImVec2(x + 2 * s, b.y - ts.y - 4 * s), ImVec2(x + ts.x + 7 * s, b.y - 1 * s), col::withAlpha(col::WaveBg, 200), 2 * s);
            dl->AddText(fontHead, fs, ImVec2(x + 4.5f * s, b.y - ts.y - 2.5f * s), col::withAlpha(col::Text, 190), n);
        }
    }
    if (hasGrid && settings_.waveStructure) {
        for (int barI : shape.drops) {
            const float x = xOf(dk.meta.gridAnchorSec + barI * bar);
            if (x < a.x - 60 * s || x > b.x + 4 * s) continue;
            const float fs = 11 * s;
            const char* lbl = "SUBIDÓN";
            const ImVec2 ts = fontHead->CalcTextSizeA(fs, FLT_MAX, 0, lbl);
            dl->AddLine(ImVec2(x, a.y), ImVec2(x, b.y), col::withAlpha(col::Amber, 170), 2.f * s);
            dl->AddRectFilled(ImVec2(x + 1, a.y + 2 * s), ImVec2(x + ts.x + 9 * s, a.y + ts.y + 5 * s), col::withAlpha(col::Amber, 230), 2 * s);
            dl->AddText(fontHead, fs, ImVec2(x + 5 * s, a.y + 3.5f * s), col::onColor(col::Amber), lbl);
        }
    }
    // Marcas: cue y hot cues (bandera numerada arriba)
    auto marker = [&](double tsec, ImU32 c, const char* label, const std::string& name = std::string()) {
        const float x = xOf(tsec);
        if (x < a.x - 160 * s || x > b.x + 20) return;
        dl->AddLine(ImVec2(x, a.y), ImVec2(x, b.y), col::withAlpha(c, 220), 1.5f * s);
        if (label) {
            // Bandera: número del hot cue y, si lo tiene, su nombre ("2 Drop").
            const float fsz = std::min(16 * s, H * 0.3f);
            const float nw = name.empty() ? 0.f : std::min(fontHead->CalcTextSizeA(fsz * 0.78f, FLT_MAX, 0, name.c_str()).x + 8 * s, 120 * s);
            dl->AddRectFilled(ImVec2(x, a.y), ImVec2(x + fsz + nw, a.y + fsz), c, 3 * s, ImDrawFlags_RoundCornersBottomRight);
            CenterText(dl, ImVec2(x, a.y), ImVec2(x + fsz, a.y + fsz), col::onColor(c), label, fontHead, fsz * 0.8f);
            if (nw > 0) {
                dl->AddLine(ImVec2(x + fsz, a.y + 3 * s), ImVec2(x + fsz, a.y + fsz - 3 * s), col::withAlpha(col::onColor(c), 90), 1.f);
                dl->AddText(fontHead, fsz * 0.78f, ImVec2(x + fsz + 4 * s, a.y + fsz * 0.1f), col::onColor(c),
                            ellipsize(name, nw - 6 * s, fontHead, fsz * 0.78f).c_str());
            }
        } else {
            dl->AddTriangleFilled(ImVec2(x - 6 * s, b.y), ImVec2(x + 6 * s, b.y), ImVec2(x, b.y - 8 * s), c);
            dl->AddTriangleFilled(ImVec2(x - 6 * s, a.y), ImVec2(x + 6 * s, a.y), ImVec2(x, a.y + 8 * s), c);
        }
    };
    marker(st.cueFrames.load() / sr, col::Amber, nullptr);
    for (int i = 0; i < kNumHotCues; ++i) {
        const double h = st.hotCues[i].load();
        if (h >= 0) { char n[4]; std::snprintf(n, sizeof n, "%d", i + 1); marker(h / sr, kHotColors[i], n, dk.meta.hotCueNames[i]); }
    }
    // Cabezal
    dl->AddRectFilled(ImVec2(cx - 1.5f * s, a.y), ImVec2(cx + 1.5f * s, b.y), IM_COL32(255, 255, 255, 240));
    dl->AddTriangleFilled(ImVec2(cx - 6 * s, a.y), ImVec2(cx + 6 * s, a.y), ImVec2(cx, a.y + 7 * s), IM_COL32(255, 255, 255, 255));
    dl->AddTriangleFilled(ImVec2(cx - 6 * s, b.y), ImVec2(cx + 6 * s, b.y), ImVec2(cx, b.y - 7 * s), IM_COL32(255, 255, 255, 255));
    dl->PopClipRect();

    ImGuiIO& io = ImGui::GetIO();
    DeckParams& p = engine_.deck[d];
    if (ImGui::IsItemActivated()) { p.scratchVelocity = 0.f; p.scratching = true; dk.scratching = true; }
    if (dk.scratching && ImGui::IsItemActive()) {
        const float vel = io.DeltaTime > 0 ? (float)((-io.MouseDelta.x / pxPerSec) / io.DeltaTime) : 0.f;
        p.scratchVelocity = std::clamp(vel * settings_.mouseScratchSensitivity, -8.f, 8.f);
    } else if (ImGui::IsItemDeactivated()) {
        p.scratching = false;
        p.scratchVelocity = 0.f;
        dk.scratching = false;
    }
    if (hovered && io.MouseWheel != 0.f) settings_.waveZoomSec = std::clamp(settings_.waveZoomSec * (io.MouseWheel > 0 ? 0.8f : 1.25f), 2.f, 32.f);
    // Clic derecho: poner marcas justo donde está el ratón (ajustadas al pulso si Q está activo).
    char pid[16];
    std::snprintf(pid, sizeof pid, "zwmenu%d", d);
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        menuTime_[d] = pos + (io.MousePos.x - cx) / pxPerSec;
        ImGui::OpenPopup(pid);
    }
    if (ImGui::BeginPopup(pid)) {
        const double tm = std::clamp(menuTime_[d], 0.0, t->lengthSec());
        double snapped = tm;
        if (hasGrid) snapped = dk.meta.gridAnchorSec + std::round((tm - dk.meta.gridAnchorSec) / beat) * beat;
        const bool q = p.quantize.load();
        const double at = q ? snapped : tm;
        ImGui::TextDisabled("En %s%s", formatTime(at).c_str(), q && hasGrid ? " (en el pulso)" : "");
        int freeCue = -1;
        for (int i = 0; i < kNumHotCues; ++i) if (st.hotCues[i].load() < 0) { freeCue = i; break; }
        char lbl[64];
        std::snprintf(lbl, sizeof lbl, freeCue >= 0 ? "Poner el hot cue %d aquí" : "Los 8 hot cues están ocupados", freeCue + 1);
        if (ImGui::MenuItem(lbl, nullptr, false, freeCue >= 0)) send(d, Cmd::SetHotCueAt, at * sr, freeCue);
        if (ImGui::BeginMenu("Poner el hot cue…")) {
            for (int i = 0; i < kNumHotCues; ++i) {
                std::snprintf(lbl, sizeof lbl, "%d%s", i + 1, st.hotCues[i].load() >= 0 ? " (sustituir)" : "");
                if (ImGui::MenuItem(lbl)) send(d, Cmd::SetHotCueAt, at * sr, i);
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Poner el CUE aquí")) send(d, Cmd::SetCueAt, at * sr);
        if (ImGui::MenuItem("Saltar aquí")) send(d, Cmd::Seek, at * sr);
        ImGui::Separator();
        if (ImGui::MenuItem("Empieza un compás aquí (ajustar la rejilla)", nullptr, false, hasGrid)) {
            // Mueve la rejilla para que caiga un primer tiempo en el punto (sin cuantizar: es para corregirla).
            dk.meta.gridAnchorSec = std::fmod(std::fmod(tm, bar) + bar, bar);
            dk.meta.gridSrc = 3;
            pushGrid(d);
            library_.updateMeta(dk.path, dk.meta);
            notify("Rejilla ajustada: primer tiempo de compás en " + formatTime(tm) + ".");
        }
        ImGui::SetItemTooltip("Haz clic derecho justo en el golpe con el que empieza un compás.\nUsa la rueda para acercar la onda y afinar.");
        ImGui::EndPopup();
    }
    if (hovered && !dk.scratching && !ImGui::IsPopupOpen(pid))
        ImGui::SetTooltip("Arrastra para hacer scratch · rueda: zoom (%.0f s) · clic derecho: hot cue, cue o rejilla aquí\nLíneas largas: compases (cada 8, con su número) · marcas cortas: pulsos", settings_.waveZoomSec);
}

// =================================================================== vista general (toda la pista)

void App::drawOverview(int d, ImVec2 a, ImVec2 b) {
    const float s = S();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    DeckUi& dk = deck_[d];
    ImGui::SetCursorScreenPos(a);
    char id[16];
    std::snprintf(id, sizeof id, "##ov%d", d);
    ImGui::InvisibleButton(id, ImVec2(std::max(1.f, b.x - a.x), std::max(1.f, b.y - a.y)));
    dl->AddRectFilled(a, b, col::WaveBg, 3 * s);
    Track* t = dk.track.get();
    if (!t || t->wave.empty()) {
        dl->AddLine(ImVec2(a.x + 4 * s, (a.y + b.y) * 0.5f), ImVec2(b.x - 4 * s, (a.y + b.y) * 0.5f), col::Line, 1.f);
        return;
    }
    const ImU32 acc = col::deck(d);
    const int W = std::max(1, (int)(b.x - a.x));
    const float H = b.y - a.y, cy = a.y + H * 0.5f;
    const double len = std::max(0.001, t->lengthSec());
    const double pos = deckPosSec(d);
    const float px = (float)(pos / len * W);
    const OverviewData& ov = overviewData(d, W);
    const TrackShape& shape = trackShape(d);
    const int style = settings_.waveStyle;
    const BandColors bc = bandColors(style, acc);
    const float* lut = ampCurve();
    const float halfH = H * 0.5f - 1.5f * s;
    const double bar = dk.meta.bpm > 0 ? 4 * 60.0 / dk.meta.bpm : 0;
    auto xOf = [&](double tsec) { return a.x + (float)(tsec / len * W); };
    dl->PushClipRect(a, b, true);
    if (bar > 0 && settings_.waveStructure)
        for (size_t i = 0; i < shape.barKind.size(); ++i)
            if (shape.barKind[i] == 1) {
                const double t0 = dk.meta.gridAnchorSec + (shape.firstBar + (int)i) * bar;
                dl->AddRectFilled(ImVec2(xOf(t0), a.y), ImVec2(xOf(t0 + bar) + 1, b.y), col::withAlpha(col::Violet, 30));
            }
    for (int x = 0; x < W && x < (int)ov.low.size(); ++x) {
        const bool played = x < px;
        const float dim = played ? 0.45f : 1.f;
        const float L = lut[ov.low[x]] * halfH, M = lut[ov.mid[x]] * halfH * 0.86f, Hh = lut[ov.high[x]] * halfH * 0.62f;
        const float P = lut[ov.peak[x]] * halfH;
        const float xx = a.x + x;
        vbar(dl, xx, cy, P, col::withAlpha(played ? col::Line : col::mix(col::Line, acc, 0.35f), 200));  // contorno de los golpes
        if (style == 2) {
            const ImU32 c = rgbColor(lut[ov.low[x]], lut[ov.mid[x]], lut[ov.high[x]], dim);
            vbar(dl, xx, cy, std::max({L, M, Hh}), c);
        } else {
            vbar(dl, xx, cy, L, scaleRgb(bc.low, dim));
            vbar(dl, xx, cy, M, scaleRgb(bc.mid, dim));
            vbar(dl, xx, cy, Hh, scaleRgb(bc.high, dim));
        }
    }
    // Frases (cada 8 compases): marcas abajo.
    if (bar > 0 && settings_.waveGrid) {
        for (double tb = dk.meta.gridAnchorSec; tb < len; tb += 8 * bar) {
            const float x = xOf(tb);
            dl->AddLine(ImVec2(x, b.y - 4 * s), ImVec2(x, b.y), col::withAlpha(col::Text, 110), 1.f);
        }
    }
    const DeckStatus& st = engine_.deckStatus[d];
    const double sr = t->sampleRate;
    if (st.loopIn.load() >= 0 && st.loopOut.load() > st.loopIn.load()) {
        dl->AddRectFilled(ImVec2(xOf(st.loopIn.load() / sr), a.y), ImVec2(xOf(st.loopOut.load() / sr) + 1, b.y),
                          st.loopActive.load() ? col::withAlpha(col::Green, 70) : IM_COL32(120, 120, 140, 40));
    }
    // Zona que muestran las ondas grandes.
    if (settings_.showWaves && mode_ != 1) {
        const double half = settings_.waveZoomSec * waveRate(d) * 0.5;
        dl->AddRect(ImVec2(xOf(pos - half), a.y), ImVec2(xOf(pos + half) + 1, b.y), col::withAlpha(col::Text, 70), 2 * s, 0, 1.f);
    }
    if (bar > 0 && settings_.waveStructure)
        for (int barI : shape.drops) {
            const float x = xOf(dk.meta.gridAnchorSec + barI * bar);
            dl->AddTriangleFilled(ImVec2(x - 4 * s, a.y), ImVec2(x + 4 * s, a.y), ImVec2(x, a.y + 6 * s), col::Amber);
        }
    for (int i = 0; i < kNumHotCues; ++i) {
        const double h = st.hotCues[i].load();
        if (h < 0) continue;
        const float x = xOf(h / sr);
        dl->AddLine(ImVec2(x, a.y), ImVec2(x, b.y), kHotColors[i], 1.5f * s);
        if (H > 26 * s) {
            const float fz = 11 * s;
            char n[4]; std::snprintf(n, sizeof n, "%d", i + 1);
            dl->AddRectFilled(ImVec2(x, a.y), ImVec2(x + fz, a.y + fz), kHotColors[i], 2 * s, ImDrawFlags_RoundCornersBottomRight);
            CenterText(dl, ImVec2(x, a.y), ImVec2(x + fz, a.y + fz), col::onColor(kHotColors[i]), n, fontHead, fz * 0.85f);
        }
    }
    const float cxq = xOf(st.cueFrames.load() / sr);
    dl->AddTriangleFilled(ImVec2(cxq - 4 * s, b.y), ImVec2(cxq + 4 * s, b.y), ImVec2(cxq, b.y - 6 * s), col::Amber);
    dl->AddRectFilled(ImVec2(a.x + px - 1.f * s, a.y), ImVec2(a.x + px + 1.f * s, b.y), IM_COL32(255, 255, 255, 245));
    dl->PopClipRect();
    if (ImGui::IsItemActive() && (ImGui::IsItemActivated() || ImGui::GetIO().MouseDelta.x != 0.f)) {
        const double f = std::clamp((ImGui::GetIO().MousePos.x - a.x) / W, 0.f, 1.f);
        send(d, Cmd::Seek, f * t->frames);
    }
    if (ImGui::IsItemHovered()) {
        const double f = std::clamp((ImGui::GetIO().MousePos.x - a.x) / W, 0.f, 1.f);
        if (bar > 0) {
            const long long barNo = (long long)std::floor((f * len - dk.meta.gridAnchorSec) / bar) + 1;
            ImGui::SetTooltip("Ir a %s · compás %lld%s", formatTime(f * len).c_str(), barNo,
                              settings_.waveStructure && !shape.barKind.empty() ? "\nZonas violeta: cortes sin bombo · triángulos ámbar: subidones" : "");
        } else ImGui::SetTooltip("Ir a %s", formatTime(f * len).c_str());
    }
}

// =================================================================== decks plegados (una línea)

void App::drawCompactDecks(ImVec2 a, ImVec2 b) {
    const float s = S(), gap = 8 * s, width = (b.x - a.x - gap) * 0.5f;
    const float h = b.y - a.y;
    for (int d = 0; d < 2; ++d) {
        DeckUi& dk = deck_[d];
        const ImVec2 p(a.x + d * (width + gap), a.y), q(p.x + width, b.y);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 acc = col::deck(d);
        const bool play = engine_.deckStatus[d].playing.load();
        const bool has = dk.track != nullptr;
        const bool ending = endWarning(d);
        const float blink = 0.5f + 0.5f * std::cos((float)now_ * 6.2832f * 1.4f);
        Panel(dl, p, q, 8 * s, col::Panel, ending ? col::mix(col::Red, col::Line, 0.7f * (1.f - blink)) : play ? col::mix(col::Line, acc, 0.5f) : col::Line);
        deckRect_[d][0] = p; deckRect_[d][1] = q;
        if (!settings_.showWaves || mode_ == 2) { waveRect_[d][0] = p; waveRect_[d][1] = p; }
        ImGui::PushID(d + 2300);
        if (ImGui::BeginDragDropTargetCustom(ImRect(p, q), ImGui::GetID("dropc"))) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("RDJ_PISTA")) {
                auto paths = uic::PayloadPaths(pl);
                if (!paths.empty()) loadToDeck(d, paths[0]);
            }
            ImGui::EndDragDropTarget();
        }
        const float art = h - 12 * s;
        if (has) DrawCover(dl, dk.path, ImVec2(p.x + 6 * s, p.y + 6 * s), ImVec2(p.x + 6 * s + art, p.y + 6 * s + art), 5 * s, dk.title.c_str());
        else { dl->AddRectFilled(ImVec2(p.x + 6 * s, p.y + 6 * s), ImVec2(p.x + 6 * s + art, p.y + 6 * s + art), col::Panel2, 5 * s); DrawIcon(dl, Ic::Disc, ImVec2(p.x + 6 * s + art * 0.5f, p.y + 6 * s + art * 0.5f), art * 0.5f, col::Line); }
        const float bs = 18 * s;
        dl->AddRectFilled(ImVec2(p.x + 6 * s, p.y + 6 * s), ImVec2(p.x + 6 * s + bs, p.y + 6 * s + bs), acc, 4 * s, ImDrawFlags_RoundCornersTopLeft | ImDrawFlags_RoundCornersBottomRight);
        CenterText(dl, ImVec2(p.x + 6 * s, p.y + 6 * s), ImVec2(p.x + 6 * s + bs, p.y + 6 * s + bs), col::onColor(acc), d ? "B" : "A", fontBold, 13 * s);
        // Botones a la derecha: CUE · PLAY · SYNC
        const float bw = std::clamp(width * 0.075f, 40 * s, 56 * s), bh2 = std::min(34 * s, h * 0.5f);
        float bx = q.x - 8 * s - bw;
        const float by = p.y + 7 * s;
        ImGui::SetCursorScreenPos(ImVec2(bx, by));
        if (NeonButton("SYNC##cs", col::Green, dk.sync, ImVec2(bw, bh2), has, fontHead, 13 * s)) setSync(d, !dk.sync);
        bx -= bw + 5 * s;
        ImGui::SetCursorScreenPos(ImVec2(bx, by));
        if (IconButton("##compactplay", play ? Ic::Pause : Ic::Play, ImVec2(bw, bh2), acc, true, has, 0.5f)) togglePlay(d);
        bx -= bw + 5 * s;
        ImGui::SetCursorScreenPos(ImVec2(bx, by));
        NeonButton("CUE##cc", col::Amber, false, ImVec2(bw, bh2), has, fontHead, 13 * s);
        if (ImGui::IsItemActivated()) send(d, Cmd::CueDown);
        if (ImGui::IsItemDeactivated()) send(d, Cmd::CueUp);
        // Texto: título, artista · BPM · tonalidad · tiempo restante
        const float x = p.x + 14 * s + art;
        const float textW = bx - x - 10 * s;
        char bpm[24];
        if (has && dk.meta.bpm > 0) std::snprintf(bpm, sizeof bpm, "%.2f", engine_.effectiveBpm(d)); else std::snprintf(bpm, sizeof bpm, "--.--");
        const float bpmW = fontBold->CalcTextSizeA(20 * s, FLT_MAX, 0, bpm).x;
        dl->AddText(fontBold, 20 * s, ImVec2(bx - 10 * s - bpmW, p.y + 6 * s), dk.sync ? col::Green : col::Text, bpm);
        const float tw = textW - bpmW - 12 * s;
        const std::string title = has ? dk.title : (loader_.busy(d) ? "Cargando…" : "Sin pista");
        dl->AddText(fontHead, 16 * s, ImVec2(x, p.y + 6 * s), has ? col::Text : col::TextDim, ellipsize(title, tw, fontHead, 16 * s).c_str());
        if (has) {
            char info[160];
            const std::string remT = "-" + formatTime(std::max(0.0, deckLenSec(d) - deckPosSec(d)), false);
            std::snprintf(info, sizeof info, "%s  ·  %s", dk.artist.empty() ? "Artista desconocido" : dk.artist.c_str(), keyText(dk.meta.key >= 0 ? soundingKey(d) : -1).c_str());
            // El tiempo restante va aparte para poder avisar del final en rojo.
            ImFont* bf = ImGui::GetFont();
            const float rw = fontHead->CalcTextSizeA(14 * s, FLT_MAX, 0, remT.c_str()).x;
            dl->AddText(bf, 13 * s, ImVec2(x, p.y + 26 * s), col::TextDim, ellipsize(info, tw - rw - 10 * s, bf, 13 * s).c_str());
            dl->AddText(fontHead, 14 * s, ImVec2(x + tw - rw, p.y + 25 * s), ending ? col::mix(col::Red, col::Text, 0.35f * (1.f - blink)) : col::TextDim, remT.c_str());
        }
        // Vista general y medidor de fase en la parte de abajo.
        const float oy = p.y + 46 * s;
        if (q.y - 6 * s - oy > 10 * s) {
            drawOverview(d, ImVec2(x, oy), ImVec2(q.x - 8 * s - (has && deck_[1 - d].track ? 70 * s : 0), q.y - 6 * s));
            if (has && deck_[1 - d].track) drawPhaseMeter(d, ImVec2(q.x - 72 * s, oy + (q.y - 6 * s - oy) * 0.5f - 3 * s), ImVec2(q.x - 8 * s, oy + (q.y - 6 * s - oy) * 0.5f + 3 * s));
        }
        ImGui::PopID();
    }
}

} // namespace rdj

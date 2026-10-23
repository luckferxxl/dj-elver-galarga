// Tonalidad en la interfaz (0.6.2): rueda Camelot interactiva, relación armónica
// entre platos, filtro de compatibles, tonalidad puesta a mano e importación de
// la biblioteca de VirtualDJ.
#include "app.h"
#include "ui_common.h"
#include "widgets.h"
#include "winutil.h"
#include "../audio/analysis.h"
#include "../core/util.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>

namespace rdj {

using namespace ui;
using uic::ellipsize;

namespace {
float S() { return ImGui::GetFontSize() / 16.f; }
constexpr float kWheelPi = 3.14159265358979f;

// Ángulo (radianes, sentido de las agujas en pantalla) del centro del número n de la rueda: el 12 arriba.
float wheelAngle(int n) { return (-90.f + n * 30.f) * kWheelPi / 180.f; }

ImU32 levelColor(int level) {
    switch (level) {
    case 0: return col::Green;
    case 1: return col::Amber;
    case 2: return col::mix(col::Amber, col::TextDim, 0.4f);
    default: return col::Red;
    }
}

std::string fileDate(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) return "";
    FILETIME lt{};
    SYSTEMTIME st{};
    FileTimeToLocalFileTime(&fa.ftLastWriteTime, &lt);
    FileTimeToSystemTime(&lt, &st);
    const double mb = ((double)fa.nFileSizeHigh * 4294967296.0 + fa.nFileSizeLow) / 1048576.0;
    char b[96];
    char sz[24];
    if (mb >= 0.1) std::snprintf(sz, sizeof sz, "%.1f MB", mb); else std::snprintf(sz, sizeof sz, "%.0f KB", std::max(1.0, mb * 1024));
    std::snprintf(b, sizeof b, "%s · modificado el %02d-%02d-%04d %02d:%02d", sz, st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute);
    return b;
}
} // namespace

// =================================================================== relación armónica

App::KeyRelation App::keyRelation(int from, int to) {
    KeyRelation r;
    if (from < 0 || to < 0) { r.level = 3; r.name = "Sin tonalidad"; r.hint = "Falta la tonalidad de alguna de las dos pistas."; return r; }
    const int na = keyCamelotNumber(from), nb = keyCamelotNumber(to);
    const bool ma = keyIsMajor(from), mb = keyIsMajor(to);
    const int d = ((nb - na) % 12 + 12) % 12;   // pasos en el sentido de la rueda
    if (from == to) { r.level = 0; r.name = "Misma tonalidad"; r.hint = "Mezcla perfecta: las dos usan las mismas notas."; }
    else if (ma == mb && d == 1) { r.level = 0; r.name = "+1 · sube la energía"; r.hint = "Vecina en la rueda: encaja y da un pequeño empujón."; }
    else if (ma == mb && d == 11) { r.level = 0; r.name = "-1 · relaja"; r.hint = "Vecina en la rueda: encaja y baja un poco la tensión."; }
    else if (ma != mb && d == 0) { r.level = 0; r.name = mb ? "Relativa mayor" : "Relativa menor"; r.hint = "Mismas notas: cambia el ánimo (de menor a mayor o al revés) sin chocar."; }
    else if ((!ma && mb && d == 1) || (ma && !mb && d == 11)) { r.level = 1; r.name = "Diagonal"; r.hint = "Cambia de ánimo y de energía a la vez: funciona en mezclas cortas."; }
    else if (ma == mb && d == 2) { r.level = 1; r.name = "+2 · subidón"; r.hint = "Salto de energía marcado: mejor en un corte o con poco solape."; }
    else if (ma == mb && d == 10) { r.level = 1; r.name = "-2 · bajón"; r.hint = "Baja bastante la energía: mejor en un corte."; }
    else if (ma == mb && d == 7) { r.level = 1; r.name = "+1 semitono"; r.hint = "Sube la tensión (cambio de tono clásico): mejor en un corte."; }
    else if (ma == mb && d == 5) { r.level = 1; r.name = "-1 semitono"; r.hint = "Baja medio tono: mejor en un corte."; }
    else if (keysClose(from, to)) { r.level = 2; r.name = "Casi"; r.hint = "Choca tal cual, pero encaja si mueves la tonalidad un semitono (botón Tono del plato)."; }
    else { r.level = 3; r.name = "Choca"; r.hint = "Las notas chocan: no solapes mucho las dos pistas."; }
    return r;
}

int App::soundingKey(int d) const {
    const DeckUi& dk = deck_[d];
    if (!dk.track || dk.meta.key < 0) return -1;
    const DeckParams& p = engine_.deck[d];
    const double semis = p.keyShift.load() + (p.keyLock.load() ? 0.0 : 12.0 * std::log2(std::max(0.25, 1.0 + (double)dk.pitch)));
    const int sh = (int)std::lround(semis);
    const int k = dk.meta.key;
    return k < 12 ? ((k + sh) % 12 + 12) % 12 : 12 + (((k - 12) + sh) % 12 + 12) % 12;
}

const char* App::keySourceText(int src) const {
    switch (src) {
    case 1: case 2: return "detectada por DJ Elver Galarga";
    case 3: return "de la etiqueta del archivo";
    case 4: return "de VirtualDJ";
    case 5: return "puesta a mano";
    default: return "";
    }
}

// =================================================================== rueda Camelot

int App::drawCamelotWheel(const char* id, int key, ImVec2 tl, float D, int* hoveredOut, bool picker) {
    const float s = S();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c(tl.x + D * 0.5f, tl.y + D * 0.5f);
    const float R = D * 0.5f - 9 * s;            // margen para las marcas de los platos
    const float rB0 = R * 0.69f, rB1 = R;        // anillo exterior: mayores (B)
    const float rA0 = R * 0.40f, rA1 = R * 0.67f; // anillo interior: menores (A)
    ImGui::SetCursorScreenPos(tl);
    ImGui::InvisibleButton(id, ImVec2(D, D));
    const bool hov = ImGui::IsItemHovered();
    int hk = -1;
    if (hov) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const float dx = m.x - c.x, dy = m.y - c.y, r = std::sqrt(dx * dx + dy * dy);
        const float deg = std::atan2(dy, dx) * 180.f / kWheelPi + 90.f;
        int n = (int)std::floor((deg + 15.f) / 30.f);
        n = ((n % 12) + 12) % 12;
        if (n == 0) n = 12;
        if (r >= rB0 && r <= rB1 + 2 * s) hk = keyFromCamelot(n, true);
        else if (r >= rA0 && r < rB0) hk = keyFromCamelot(n, false);
    }
    int deckKey[kNumDecks];
    for (int d = 0; d < kNumDecks; ++d) deckKey[d] = soundingKey(d);
    const float gap = 0.9f * kWheelPi / 180.f;
    for (int ring = 0; ring < 2; ++ring) {
        const bool major = ring == 0;
        const float r0 = major ? rB0 : rA0, r1 = major ? rB1 : rA1;
        for (int n = 1; n <= 12; ++n) {
            const int k = keyFromCamelot(n, major);
            const float a0 = wheelAngle(n) - 15.f * kWheelPi / 180.f + gap, a1 = wheelAngle(n) + 15.f * kWheelPi / 180.f - gap;
            const ImU32 base = col::camelot(camelotIndex(k));
            float lit;
            if (key < 0) lit = 0.62f;
            else if (k == key) lit = 1.f;
            else if (keysCompatible(key, k)) lit = 0.78f;
            else if (keysClose(key, k)) lit = 0.42f;
            else lit = 0.2f;
            if (k == hk) lit = std::min(1.f, lit + 0.25f);
            const ImU32 fill = col::mix(col::Panel2, base, lit);
            dl->PathArcTo(c, r1, a0, a1, 10);
            dl->PathArcTo(c, r0, a1, a0, 10);
            dl->PathFillConcave(fill);
            if (k == key || k == hk) {
                dl->PathArcTo(c, r1 - 1, a0, a1, 10);
                dl->PathArcTo(c, r0 + 1, a1, a0, 10);
                dl->PathStroke(k == key ? col::Text : col::withAlpha(col::Text, 140), ImDrawFlags_Closed, (k == key ? 2.f : 1.f) * s);
            }
            for (int d = 0; d < kNumDecks; ++d) {
                if (deckKey[d] != k) continue;
                // Plato que suena en esta tonalidad: contorno de su color y etiqueta fuera de la rueda.
                dl->PathArcTo(c, r1 - 2.5f * s, a0 + gap, a1 - gap, 10);
                dl->PathArcTo(c, r0 + 2.5f * s, a1 - gap, a0 + gap, 10);
                dl->PathStroke(col::deck(d), ImDrawFlags_Closed, 2.5f * s);
                const float am = wheelAngle(n) + (d == 0 ? -7.f : 7.f) * kWheelPi / 180.f;
                const ImVec2 tp(c.x + std::cos(am) * (R + 4.5f * s), c.y + std::sin(am) * (R + 4.5f * s));
                dl->AddCircleFilled(tp, 5.5f * s, col::deck(d), 16);
                CenterText(dl, ImVec2(tp.x - 6 * s, tp.y - 6 * s), ImVec2(tp.x + 6 * s, tp.y + 6 * s), col::onColor(col::deck(d)), d ? "B" : "A", fontBold, 9.5f * s);
            }
            char code[8];
            std::snprintf(code, sizeof code, "%d%c", n, major ? 'B' : 'A');
            const float rm = (r0 + r1) * 0.5f, am = wheelAngle(n);
            const ImVec2 lp(c.x + std::cos(am) * rm, c.y + std::sin(am) * rm);
            const float fz = std::clamp(D * (major ? 0.056f : 0.05f), 9.f * s, 14.f * s);
            const ImU32 tc = lit >= 0.7f ? col::onColor(fill) : col::mix(col::TextDim, col::Text, lit);
            CenterText(dl, ImVec2(lp.x - 14 * s, lp.y - 8 * s), ImVec2(lp.x + 14 * s, lp.y + 8 * s), tc, code, fontHead, fz);
        }
    }
    // Centro: la tonalidad elegida.
    const float rc = rA0 - 3 * s;
    dl->AddCircleFilled(c, rc, col::Panel, 48);
    dl->AddCircle(c, rc, col::Line, 48, 1.f);
    const int shown = hk >= 0 ? hk : key;
    if (shown >= 0) {
        const ImU32 kc = col::camelot(camelotIndex(shown));
        CenterText(dl, ImVec2(c.x - rc, c.y - rc * 0.62f), ImVec2(c.x + rc, c.y + rc * 0.12f), kc, keyCode(shown).c_str(), fontBold, std::clamp(rc * 0.62f, 12 * s, 26 * s));
        CenterText(dl, ImVec2(c.x - rc, c.y + rc * 0.08f), ImVec2(c.x + rc, c.y + rc * 0.5f), col::TextDim, keyShort(shown).c_str(), ImGui::GetFont(), std::clamp(rc * 0.3f, 9 * s, 13 * s));
    } else {
        CenterText(dl, ImVec2(c.x - rc, c.y - rc), ImVec2(c.x + rc, c.y + rc), col::TextDim, "?", fontBold, rc * 0.6f);
    }
    if (hk >= 0) {
        ImGui::BeginTooltip();
        ImGui::PushFont(fontHead, 15.f);
        ImGui::TextColored(toVec4(col::camelot(camelotIndex(hk))), "%s · %s (%s)", keyCode(hk).c_str(), keyNameEs(hk), keyName(hk));
        ImGui::PopFont();
        if (key >= 0 && hk != key) {
            const KeyRelation rel = keyRelation(key, hk);
            ImGui::TextColored(toVec4(levelColor(rel.level)), "Desde %s: %s", keyCode(key).c_str(), rel.name);
            ImGui::TextDisabled("%s", rel.hint);
        } else if (hk == key) ImGui::TextDisabled("Tonalidad de esta pista.");
        for (int d = 0; d < kNumDecks; ++d) {
            if (deckKey[d] < 0) continue;
            const KeyRelation rel = keyRelation(deckKey[d], hk);
            ImGui::TextColored(toVec4(levelColor(rel.level)), "Con el plato %s (%s): %s", d ? "B" : "A", keyCode(deckKey[d]).c_str(), rel.name);
        }
        ImGui::Separator();
        if (picker) ImGui::TextDisabled("Clic: esta pista está en %s", keyCode(hk).c_str());
        else ImGui::TextDisabled("Clic: ver en la lista las compatibles con %s · Clic derecho: más opciones", keyCode(hk).c_str());
        ImGui::EndTooltip();
    }
    if (hoveredOut) *hoveredOut = hk;
    return hov && ImGui::IsItemClicked(ImGuiMouseButton_Left) ? hk : -1;
}

void App::drawKeySection(const std::wstring& path, int key, float w) {
    const float s = S();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 cp = ImGui::GetCursorScreenPos();
    // Explicación y filtro activo (la cabecera "RUEDA ARMÓNICA" la pone el panel).
    ImGui::SetCursorScreenPos(ImVec2(cp.x, cp.y));
    ImGui::InvisibleButton("##ruedainfo", ImVec2(16 * s, 16 * s));
    DrawIcon(dl, Ic::Info, ImVec2(ImGui::GetItemRectMin().x + 8 * s, ImGui::GetItemRectMin().y + 8 * s), 13 * s, col::TextDim);
    ImGui::SetItemTooltip("Rueda Camelot: fuera las mayores (B), dentro las menores (A).\n"
                          "Encajan la misma, la de al lado (±1) y la de enfrente en el otro anillo (relativa).\n"
                          "Las tenues encajan moviendo la tonalidad un semitono. A y B marcan lo que suena en cada plato.\n"
                          "Clic: ver en la lista las compatibles · clic derecho: más opciones.");
    if (keyFilter_ >= 0) {
        char lb[48];
        std::snprintf(lb, sizeof lb, "Filtro: %s  ×##qf", keyCode(keyFilter_).c_str());
        const float bw = BadgeWidth(18 * s, lb, fontHead, 12 * s) + 6 * s;
        ImGui::SetCursorScreenPos(ImVec2(cp.x + w - bw, cp.y - 1 * s));
        if (NeonButton(lb, col::camelot(camelotIndex(keyFilter_)), true, ImVec2(bw, 18 * s), true, fontHead, 12 * s)) setKeyFilter(-1);
        ImGui::SetItemTooltip("Quitar el filtro de tonalidad de la lista");
    }
    cp.y -= 18 * s;   // la rueda empieza justo debajo de esta fila
    const float D = std::min(w, 250 * s);
    const ImVec2 tl(cp.x + (w - D) * 0.5f, cp.y + 22 * s);
    int hk = -1;
    const int clicked = drawCamelotWheel("##rueda", key, tl, D, &hk);
    if (clicked >= 0) setKeyFilter(clicked == keyFilter_ ? -1 : clicked);
    if (hk >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) { wheelMenuKey_ = hk; ImGui::OpenPopup("ruedamenu"); }
    if (ImGui::BeginPopup("ruedamenu")) {
        const int k = wheelMenuKey_;
        if (k >= 0) {
            ImGui::TextColored(toVec4(col::camelot(camelotIndex(k))), "%s · %s", keyCode(k).c_str(), keyNameEs(k));
            ImGui::Separator();
            char lb[96];
            std::snprintf(lb, sizeof lb, "Ver en la lista las compatibles con %s", keyCode(k).c_str());
            if (ImGui::MenuItem(lb)) setKeyFilter(k);
            std::snprintf(lb, sizeof lb, "Esta pista está en %s: ponerla a mano", keyCode(k).c_str());
            if (ImGui::MenuItem(lb, nullptr, false, k != key)) setManualKey(path, k);
            if (keyFilter_ >= 0 && ImGui::MenuItem("Quitar el filtro de tonalidad")) setKeyFilter(-1);
        }
        ImGui::EndPopup();
    }
    ImGui::SetCursorScreenPos(ImVec2(cp.x, tl.y + D + 4 * s));
    // Relación con lo que hay en los platos.
    for (int d = 0; d < kNumDecks; ++d) {
        if (!deck_[d].track || deck_[d].path == path) continue;
        const int dkKey = soundingKey(d);
        if (dkKey < 0) continue;
        const KeyRelation rel = keyRelation(dkKey, key);
        const ImVec2 rp = ImGui::GetCursorScreenPos();
        float x = rp.x;
        x += Badge(dl, ImVec2(x, rp.y), 18 * s, d ? "B" : "A", col::deck(d), true, fontBold, 12 * s) + 4 * s;
        x += Badge(dl, ImVec2(x, rp.y), 18 * s, keyCode(dkKey).c_str(), col::camelot(camelotIndex(dkKey)), true, fontHead, 12 * s) + 6 * s;
        DrawIcon(dl, rel.level == 0 ? Ic::Check : rel.level == 3 ? Ic::Close : Ic::Info, ImVec2(x + 6 * s, rp.y + 9 * s), 12 * s, levelColor(rel.level));
        x += 16 * s;
        dl->AddText(fontHead, 14 * s, ImVec2(x, rp.y + 1 * s), levelColor(rel.level), ellipsize(rel.name, rp.x + w - x, fontHead, 14 * s).c_str());
        ImGui::InvisibleButton(d ? "##relB" : "##relA", ImVec2(w, 20 * s));
        ImGui::SetItemTooltip("Plato %s → esta pista: %s\n%s", d ? "B" : "A", rel.name, rel.hint);
        ImGui::Dummy(ImVec2(w, 2 * s));
    }
}

void App::setKeyFilter(int key) {
    keyFilter_ = key;
    if (key >= 0) notify("Lista filtrada: solo las pistas que encajan con " + keyCode(key) + " (" + keyNameEs(key) + ").");
}

void App::setManualKey(const std::wstring& path, int key) {
    TrackMeta m;
    if (!library_.metaFor(path, m)) { notify("Esa pista no está en la colección.", true); return; }
    m.key = key; m.keyAnalyzed = true; m.keySrc = 5;
    library_.updateMeta(path, m);
    for (int d = 0; d < kNumDecks; ++d)
        if (deck_[d].path == path) { deck_[d].meta.key = key; deck_[d].meta.keyAnalyzed = true; deck_[d].meta.keySrc = 5; }
    notify("Tonalidad puesta a mano: " + keyCode(key) + " · " + keyNameEs(key) + ". Ni los análisis ni VirtualDJ la cambiarán.");
}

void App::syncDeckKey(int d) {
    DeckUi& dk = deck_[d];
    if (!dk.track || dk.path.empty()) return;
    TrackMeta m;
    if (!library_.metaFor(dk.path, m) || !m.keyAnalyzed) return;
    if (m.key != dk.meta.key || m.keySrc != dk.meta.keySrc) { dk.meta.key = m.key; dk.meta.keySrc = m.keySrc; dk.meta.keyAnalyzed = true; }
}

// =================================================================== píldoras del mezclador

void App::drawMixAssist(float x, float y, float w) {
    const float s = S();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = 18 * s, half = (w - 4 * s) * 0.5f;
    const bool both = deck_[0].track && deck_[1].track;
    // Tempo: diferencia de B respecto de A (acepta doble / mitad).
    {
        const ImVec2 a(x, y), b(x + half, y + h);
        char t[32] = "BPM —";
        ImU32 c = col::TextDim;
        std::string tip = "Diferencia de tempo entre los platos (con doble o mitad de tempo).";
        if (both && deck_[0].meta.bpm > 0 && deck_[1].meta.bpm > 0) {
            const double ea = engine_.effectiveBpm(0), eb = engine_.effectiveBpm(1);
            double r = eb / std::max(1.0, ea);
            const char* tag = "";
            if (r > 1.5) { r /= 2; tag = " ×2"; } else if (r < 0.75) { r *= 2; tag = " ½"; }
            const double pct = (r - 1.0) * 100.0;
            if (std::fabs(pct) < 0.05) { std::snprintf(t, sizeof t, "BPM =%s", tag); c = col::Green; }
            else { std::snprintf(t, sizeof t, "BPM %+.1f%%%s", pct, tag); c = std::fabs(pct) < 1.0 ? col::Green : std::fabs(pct) < 4.0 ? col::Amber : col::Red; }
            char tt[160];
            std::snprintf(tt, sizeof tt, "A %.2f · B %.2f BPM\nB va un %.2f %% %s que A%s.", ea, eb, std::fabs(pct), pct >= 0 ? "más rápido" : "más lento",
                          *tag ? " (comparando con el doble o la mitad)" : "");
            tip = tt;
        }
        dl->AddRectFilled(a, b, col::withAlpha(c, 34), h * 0.5f);
        dl->AddRect(a, b, col::withAlpha(c, 150), h * 0.5f, 0, 1.f);
        CenterText(dl, a, b, c, t, fontHead, 12 * s);
        ImGui::SetCursorScreenPos(a);
        ImGui::InvisibleButton("##asisbpm", ImVec2(half, h));
        ImGui::SetItemTooltip("%s", tip.c_str());
    }
    // Tonalidad: lo que suena en A frente a lo que suena en B.
    {
        const ImVec2 a(x + half + 4 * s, y), b(x + w, y + h);
        const int ka = both ? soundingKey(0) : -1, kb = both ? soundingKey(1) : -1;
        ImU32 c = col::TextDim;
        std::string tip = "Relación armónica entre lo que suena en los dos platos.";
        int level = -1;
        if (ka >= 0 && kb >= 0) {
            const KeyRelation rel = keyRelation(ka, kb);
            c = levelColor(rel.level);
            level = rel.level;
            tip = keyCode(ka) + " y " + keyCode(kb) + " · " + rel.name + ": " + rel.hint;
        }
        dl->AddRectFilled(a, b, col::withAlpha(c, 34), h * 0.5f);
        dl->AddRect(a, b, col::withAlpha(c, 150), h * 0.5f, 0, 1.f);
        if (level < 0) CenterText(dl, a, b, c, "KEY —", fontHead, 12 * s);
        else {
            // "8A  ✓  9A": icono de la relación en medio.
            const float mx = (a.x + b.x) * 0.5f, fz = 12 * s;
            const std::string ca = keyCode(ka), cb = keyCode(kb);
            const float wa = fontHead->CalcTextSizeA(fz, FLT_MAX, 0, ca.c_str()).x;
            dl->AddText(fontHead, fz, ImVec2(mx - 8 * s - wa, a.y + (h - fz) * 0.5f - 1 * s), col::camelot(camelotIndex(ka)), ca.c_str());
            dl->AddText(fontHead, fz, ImVec2(mx + 8 * s, a.y + (h - fz) * 0.5f - 1 * s), col::camelot(camelotIndex(kb)), cb.c_str());
            DrawIcon(dl, level == 0 ? Ic::Check : level == 3 ? Ic::Close : Ic::Info, ImVec2(mx, a.y + h * 0.5f), 10 * s, c);
        }
        ImGui::SetCursorScreenPos(a);
        ImGui::InvisibleButton("##asiskey", ImVec2(b.x - a.x, h));
        ImGui::SetItemTooltip("%s", tip.c_str());
    }
}

// =================================================================== importación de VirtualDJ

void App::openVdjImport() {
    showVdj_ = true;
    vdjFirstRun_ = false;
    vdjResult_.clear();
    vdjDbs_ = findVdjDatabases();
    vdjSel_ = 0;
}

void App::pollVdjImport() {
    // Primera vez: se mira (en segundo plano) si hay VirtualDJ en el equipo y se ofrece traer su análisis.
    if (vdjFind_.valid() && vdjFind_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto dbs = vdjFind_.get();
        settings_.vdjOffered = true;
        settings_.save();
        if (!dbs.empty() && !showVdj_ && !autotest_.active) { vdjDbs_ = std::move(dbs); vdjSel_ = 0; vdjResult_.clear(); showVdj_ = true; vdjFirstRun_ = true; }
    }
    if (!vdjFuture_.valid() || vdjFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    const VdjImportReport r = vdjFuture_.get();
    if (vdjCancel_) { vdjResult_ = "Importación cancelada (lo ya importado se queda)."; vdjResultErr_ = true; }
    else if (!r.error.empty()) { vdjResult_ = "No se pudo leer la base de datos: " + r.error; vdjResultErr_ = true; }
    else {
        char b[512];
        std::snprintf(b, sizeof b,
                      "%zu pistas en VirtualDJ, %zu coinciden con tu colección%s.\n"
                      "Tonalidad: %zu (%zu eran distintas de la detectada).  Rejillas: %zu.  Hot cues: %zu.  Listas: %zu.",
                      r.songs, r.matched, r.added ? (" (" + std::to_string(r.added) + " añadidas ahora)").c_str() : "", r.keys, r.keysChanged, r.grids, r.cues, r.playlists);
        vdjResult_ = b;
        vdjResultErr_ = false;
        if (r.songs > 0 && r.matched == 0)
            vdjResult_ += "\nNinguna ruta coincide: añade a la colección las carpetas que usas en VirtualDJ o marca «Añadir a la colección las pistas que falten».";
        if (r.missingFiles) vdjResult_ += "\n" + std::to_string(r.missingFiles) + " pistas de VirtualDJ ya no están en el disco.";
        // Platos cargados: la tonalidad se actualiza al momento; la rejilla y los cues, recargando
        // la pista si está parada (si suena, se respeta lo que se oye).
        for (int d = 0; d < kNumDecks; ++d) {
            DeckUi& dk = deck_[d];
            if (!dk.track || dk.path.empty()) continue;
            TrackMeta m;
            if (!library_.metaFor(dk.path, m)) continue;
            syncDeckKey(d);
            bool cuesNew = false;
            for (int i = 0; i < kNumHotCues; ++i) if (m.hotCuesSec[i] >= 0 && dk.meta.hotCuesSec[i] < 0) cuesNew = true;
            if (m.gridSrc != 4 && !cuesNew) continue;
            if (!engine_.deckStatus[d].playing.load()) {
                dk.restorePos = deckPosSec(d);
                dk.restorePath = dk.path;
                loadToDeck(d, dk.path, true);
            } else {
                m.bpm = dk.meta.bpm; m.gridAnchorSec = dk.meta.gridAnchorSec; m.gridSrc = dk.meta.gridSrc;
                m.hotCuesSec = dk.meta.hotCuesSec; m.cueSec = dk.meta.cueSec; m.importedCues = 0;
                library_.updateMeta(dk.path, m);
                vdjResult_ += std::string("\nEl plato ") + (d ? "B" : "A") + " está sonando: conserva su rejilla y sus cues (vuelve a importar cuando pare).";
            }
        }
        ++srcGen_;
        notify("VirtualDJ importado: " + std::to_string(r.matched) + " pistas al día" + (r.playlists ? ", " + std::to_string(r.playlists) + " listas" : std::string()) + ".");
    }
}

void App::drawVdjImportDialog() {
    if (!showVdj_) return;
    const float s = S();
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(520 * s, 0), ImVec2(760 * s, FLT_MAX));
    if (!ImGui::Begin("Importar de VirtualDJ", &showVdj_, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) { ImGui::End(); return; }
    const bool busy = vdjFuture_.valid();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 600 * s);
    if (vdjFirstRun_) {
        ImGui::PushFont(fontHead, 17.f);
        ImGui::TextColored(toVec4(col::DeckA), "Hemos encontrado VirtualDJ en este equipo");
        ImGui::PopFont();
    }
    ImGui::TextWrapped("VirtualDJ calcula la tonalidad con su propio detector, así que a veces no coincide con la de DJ Elver Galarga. "
                       "Si traes su análisis verás exactamente las mismas tonalidades, BPM, rejillas y hot cues que allí. "
                       "Solo se lee su database.xml: VirtualDJ no se modifica.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::SeparatorText("Base de datos");
    if (vdjDbs_.empty()) {
        ImGui::TextColored(toVec4(col::Amber), "No se ha encontrado ningún database.xml de VirtualDJ.");
        ImGui::TextDisabled("Suele estar en Documentos\\VirtualDJ o en la raíz de un disco externo (X:\\VirtualDJ).");
    }
    for (int i = 0; i < (int)vdjDbs_.size(); ++i) {
        ImGui::PushID(i);
        if (ImGui::RadioButton(toUtf8(vdjDbs_[i].file).c_str(), vdjSel_ == i)) vdjSel_ = i;
        const std::string info = fileDate(vdjDbs_[i].file);
        if (!info.empty()) { ImGui::SameLine(); ImGui::TextDisabled("%s", info.c_str()); }
        ImGui::PopID();
    }
    ImGui::BeginDisabled(busy);
    if (ImGui::SmallButton("Elegir otro database.xml…")) {
        const std::wstring f = win::pickFileToOpen(hwnd_, L"Base de datos de VirtualDJ", L"database.xml;*.xml");
        if (!f.empty()) {
            int found = -1;
            for (int i = 0; i < (int)vdjDbs_.size(); ++i) if (_wcsicmp(vdjDbs_[i].file.c_str(), f.c_str()) == 0) found = i;
            if (found < 0) { vdjDbs_.push_back(VdjDatabase{f, std::filesystem::path(f).parent_path().wstring(), 0}); found = (int)vdjDbs_.size() - 1; }
            vdjSel_ = found;
        }
    }
    if (ImGui::Checkbox("Usar siempre la rejilla de VirtualDJ al cargar una canción (sin importar)", &settings_.useVdjGrid)) settings_.save();
    ImGui::SetItemTooltip("Con esto no hace falta importar para el SYNC: al cargar una canción que VirtualDJ ya ha analizado, se usan su BPM y su rejilla.");
    ImGui::SeparatorText("Qué traer");
    ImGui::Checkbox("Tonalidad", &vdjOpt_.keys);
    ImGui::SameLine(200 * s);
    ImGui::Checkbox("BPM y rejilla", &vdjOpt_.grid);
    ImGui::SetItemTooltip("Su BPM y su primer tiempo de compás. Al cargar cada pista se comprueba el desfase real y se corrige.");
    ImGui::Checkbox("Hot cues", &vdjOpt_.cues);
    ImGui::SetItemTooltip("Solo en los huecos libres: nunca se pisa un hot cue tuyo.");
    ImGui::SameLine(200 * s);
    ImGui::Checkbox("Listas (carpeta Playlists)", &vdjOpt_.playlists);
    ImGui::SetItemTooltip("Se importan como «VDJ · nombre». Si ya existían, se sustituyen.");
    ImGui::Checkbox("Añadir a la colección las pistas que falten", &vdjOpt_.addMissing);
    ImGui::SetItemTooltip("Las pistas de VirtualDJ que DJ Elver Galarga aún no conoce se añaden a la colección.");
    ImGui::Checkbox("Respetar lo que ajustaste a mano en DJ Elver Galarga", &vdjOpt_.keepManual);
    ImGui::SetItemTooltip("Las rejillas corregidas a mano y las tonalidades puestas a mano no se tocan.");
    ImGui::EndDisabled();
    ImGui::Spacing();
    if (busy) {
        ImGui::ProgressBar(vdjProgress_.load(), ImVec2(420 * s, 0), "Importando…");
        ImGui::SameLine();
        if (ImGui::Button("Cancelar")) vdjCancel_ = true;
    } else {
        const bool can = !vdjDbs_.empty() && vdjSel_ >= 0 && vdjSel_ < (int)vdjDbs_.size() && (vdjOpt_.keys || vdjOpt_.grid || vdjOpt_.cues || vdjOpt_.playlists);
        if (NeonButton("Importar##vdjgo", col::DeckA, true, ImVec2(160 * s, 32 * s), can, fontHead, 15 * s)) {
            const std::wstring file = vdjDbs_[vdjSel_].file;
            const VdjImportOptions opt = vdjOpt_;
            vdjProgress_ = 0.f;
            vdjCancel_ = false;
            vdjResult_.clear();
            vdjFuture_ = std::async(std::launch::async, [this, file, opt] {
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
                return importVirtualDj(library_, file, opt, &vdjProgress_, &vdjCancel_);
            });
        }
        ImGui::SameLine();
        if (ImGui::Button(vdjFirstRun_ ? "Ahora no" : "Cerrar", ImVec2(110 * s, 32 * s))) showVdj_ = false;
        if (vdjFirstRun_) {
            ImGui::SameLine();
            ImGui::TextDisabled("Siempre podrás hacerlo en Preferencias (Tonalidad).");
        }
    }
    if (!vdjResult_.empty()) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 600 * s);
        ImGui::TextColored(toVec4(vdjResultErr_ ? col::Amber : col::Green), "%s", vdjResult_.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
}

} // namespace rdj

#include <windows.h>
#include <shellapi.h>
#include <filesystem>
// Dibujo de la interfaz principal de Ramiro DJ (diseño 0.5: azul noche,
// turquesa y coral). El navegador de pistas está en ui_browser.cpp.
#include "app.h"
#include "mandos_puente.h"
#include "covers.h"
#include "ui_common.h"
#include "widgets.h"
#include "winutil.h"
#include "../core/util.h"
#include "imgui_internal.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace rdj {

using namespace ui;
using uic::ellipsize;
using uic::BeginPanel;

namespace {
constexpr float kFxBeats[] = {0.25f, 0.5f, 0.75f, 1.f, 2.f, 4.f, 8.f};
const char* kFxBeatsTxt[] = {"1/4", "1/2", "3/4", "1", "2", "4", "8"};
const ImU32 kHotColors[kNumHotCues] = {IM_COL32(43, 227, 192, 255), IM_COL32(70, 145, 255, 255), IM_COL32(155, 110, 255, 255), IM_COL32(255, 90, 170, 255),
                                       IM_COL32(255, 92, 88, 255),  IM_COL32(255, 166, 77, 255), IM_COL32(250, 215, 80, 255),  IM_COL32(110, 220, 110, 255)};
constexpr float kVinylSecPerRev = 1.8f;  // 33⅓ rpm

float S() { return ImGui::GetFontSize() / 16.f; }

struct WizStep { int act; int deck; std::string txt; bool optional; };
const std::vector<WizStep>& wizardSteps() {
    static std::vector<WizStep> steps;
    if (!steps.empty()) return steps;
    for (int d = 0; d < 2; ++d) {
        const std::string P = d == 0 ? "Plato IZQUIERDO (A): " : "Plato DERECHO (B): ";
        const std::string C = d == 0 ? "Canal IZQUIERDO (A): " : "Canal DERECHO (B): ";
        steps.push_back({MA_Play, d, P + "pulsa PLAY / PAUSA", false});
        steps.push_back({MA_Cue, d, P + "pulsa CUE", false});
        steps.push_back({MA_Sync, d, P + "pulsa SYNC", true});
        steps.push_back({MA_Jog, d, P + "gira el BORDE EXTERIOR del jog, sin tocar la parte de arriba", false});
        steps.push_back({MA_JogTouch, d, P + "TOCA la parte de arriba del jog y suéltala", true});
        steps.push_back({MA_JogScratch, d, P + "gira el jog MIENTRAS tocas la parte de arriba", true});
        steps.push_back({MA_Pitch, d, P + "mueve el fader de TEMPO (pitch)", false});
        steps.push_back({MA_Hot1, d, P + "pulsa HOT CUE 1 (o el primer pad)", true});
        steps.push_back({MA_Hot2, d, P + "pulsa HOT CUE 2", true});
        steps.push_back({MA_Hot3, d, P + "pulsa HOT CUE 3", true});
        steps.push_back({MA_Hot4, d, P + "pulsa HOT CUE 4", true});
        steps.push_back({MA_Loop4, d, P + "pulsa el botón de LOOP (automático)", true});
        steps.push_back({MA_LoopHalve, d, P + "pulsa LOOP ½ (mitad)", true});
        steps.push_back({MA_LoopDouble, d, P + "pulsa LOOP ×2 (doble)", true});
        steps.push_back({MA_Load, d, P + "pulsa LOAD (cargar)", true});
        steps.push_back({MA_Pfl, d, C + "pulsa el botón de AURICULARES (CUE / PFL)", true});
        steps.push_back({MA_Volume, d, C + "mueve el FADER de volumen", false});
        steps.push_back({MA_EqHigh, d, C + "gira el EQ de AGUDOS (HI)", true});
        steps.push_back({MA_EqMid, d, C + "gira el EQ de MEDIOS (MID)", true});
        steps.push_back({MA_EqLow, d, C + "gira el EQ de GRAVES (LOW)", true});
        steps.push_back({MA_Filter, d, C + "gira el FILTRO (si tu mando lo tiene)", true});
        steps.push_back({MA_Gain, d, C + "gira la GANANCIA / TRIM (si tu mando la tiene)", true});
    }
    steps.push_back({MA_Crossfader, 0, "Mueve el CROSSFADER", false});
    steps.push_back({MA_BrowseScroll, 0, "Gira la rueda del NAVEGADOR (BROWSE)", true});
    steps.push_back({MA_BrowseLoad, 0, "PULSA la rueda del navegador (si se puede pulsar)", true});
    steps.push_back({MA_Shift, 0, "Pulsa SHIFT (si tu mando lo tiene)", true});
    return steps;
}

std::string describeMsg(uint8_t status, uint8_t d1, uint8_t d2) {
    char b[96];
    const int t = status >> 4, ch = (status & 15) + 1;
    if (t == 0xB) std::snprintf(b, sizeof b, "CC %d (0x%02X) · canal %d · valor %d", d1, d1, ch, d2);
    else std::snprintf(b, sizeof b, "Nota %d (0x%02X) · canal %d · %s", d1, d1, ch, (t == 0x9 && d2) ? "pulsada" : "soltada");
    return b;
}

std::string bindingText(const MidiBinding& b) {
    char t[64];
    std::snprintf(t, sizeof t, "%s%s %d · nº %d%s", b.shift ? "SHIFT+" : "", b.type == 0xB ? "CC" : "Nota", b.channel + 1, b.data1, b.hiRes ? " · 14 bits" : "");
    return t;
}

} // namespace

// =================================================================== lógica por fotograma

void App::logic() {
    // Reloj real: con la ventana minimizada o tapada no hay fotogramas de ImGui
    // y su reloj se detiene, pero Automix, el programador y la sesión deben seguir.
    static const auto t0 = std::chrono::steady_clock::now();
    now_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    pollLoader();
    pollEngineAcks();
    pollDevice();
    pollMidi();
    rackTick();
    syncFollow();
    static double prevNow = now_;
    const float dt = (float)std::clamp(now_ - prevNow, 0.0, 1.0);
    prevNow = now_;
    for (int d = 0; d < kNumDecks; ++d) {
        DeckUi& dk = deck_[d];
        if (reanalysis_[d].valid() && reanalysis_[d].wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            BeatInfo bi = reanalysis_[d].get();
            if (reanalysisPath_[d] == dk.path && bi.bpm > 0) {
                dk.meta.bpm = bi.bpm;
                dk.meta.gridAnchorSec = bi.anchorSec;
                dk.meta.analyzed = true;
                dk.meta.gridSrc = 6;
                pushGrid(d);
                library_.updateMeta(dk.path, dk.meta);
                char m[64];
                std::snprintf(m, sizeof m, "BPM recalculado: %.2f", bi.bpm);
                notify(m);
            } else if (bi.bpm <= 0) notify("No se detectó un pulso claro; usa TAP o escribe el BPM.", true);
            dk.reanalyzing.clear();
        }
        if (engine_.deckStatus[d].endReached.load() && !dk.endHandled) dk.endHandled = true;
        // Historial de la sesión: una pista entra tras sonar 30 s.
        if (dk.track && engine_.deckStatus[d].playing.load() && !autotest_.active) {
            dk.playedSec += dt;
            if (dk.playedSec >= 30.0 && !dk.historyLogged) {
                dk.historyLogged = true;
                std::time_t tt = std::time(nullptr);
                std::tm lt{};
                localtime_s(&lt, &tt);
                char name[48];
                std::strftime(name, sizeof name, "Historial %d-%m-%Y", &lt);
                const int pl = ensurePlaylist(name);
                library_.addToPlaylist(pl, {dk.path});
                library_.notePlayed(dk.path);
            }
        }
    }
    if (autoAnalyze_ && !library_.scanning()) { autoAnalyze_ = false; library_.analyzeMissing(settings_.minBpm, settings_.maxBpm); }
    automixTick();
    pollCoverFetch();
    coverSweepTick();
    recordingTick();
    schedulerTick();
    separatorTick();
    if (!autotest_.active && now_ - lastSessionSave_ > 5.0) { lastSessionSave_ = now_; saveSession(false); }
    static double lastMeta = 0;
    if (now_ - lastMeta > 1.0) {
        lastMeta = now_;
        for (int d = 0; d < kNumDecks; ++d) { syncDeckKey(d); persistDeckMeta(d); }
        if (library_.reloadExternalChanges()) {
            sortDirty_ = true;
            refreshView();
            notify("Listas, estilos y Hot Cues sincronizados en vivo desde ElverKey Studio ⚡", false);
        }
    }
    pollVdjImport();
    // Automatización en tiempo real del Ensayo de Transición A -> B (Mix Lab)
    if (mixLabRehearsal_) {
        const double posA = deck_[0].track ? deckPosSec(0) : 0.0;
        const double fade = std::max(2.0, mixLabRehearsalFadeSec_);
        if (!deck_[0].track || !deck_[1].track) {
            // Esperando a que terminen de cargar los dos platos
        } else if (posA >= mixLabRehearsalExitA_ && posA <= mixLabRehearsalExitA_ + fade) {
            if (!mixLabRehearsalBStarted_) {
                mixLabRehearsalBStarted_ = true;
                setSync(1, true);
                send(1, Cmd::Seek, mixLabRehearsalEntryB_ * deck_[1].track->sampleRate);
                if (!engine_.deckStatus[1].playing.load()) send(1, Cmd::TogglePlay);
            }
            const float prog = (float)std::clamp((posA - mixLabRehearsalExitA_) / fade, 0.0, 1.0);
            engine_.mixer.crossfader = prog;
            // Intercambio suave de graves (EQ Low swap) en el punto medio (prog = 0.5)
            if (prog < 0.5f) {
                engine_.deck[0].eqLow = 0.5f;
                engine_.deck[1].eqLow = prog * 0.6f;
            } else {
                engine_.deck[0].eqLow = std::max(0.05f, 0.5f * (1.0f - (prog - 0.5f) * 1.8f));
                engine_.deck[1].eqLow = 0.5f;
            }
        } else if (mixLabRehearsalBStarted_ && posA > mixLabRehearsalExitA_ + fade) {
            // Transición completada: dejar sonando el Plato B a pleno y restaurar EQ de A tras 5 s
            engine_.mixer.crossfader = 1.0f;
            engine_.deck[1].eqLow = 0.5f;
            if (posA > mixLabRehearsalExitA_ + fade + 5.0) {
                if (engine_.deckStatus[0].playing.load()) send(0, Cmd::TogglePlay);
                engine_.deck[0].eqLow = 0.5f;
                mixLabRehearsal_ = false;
                notify("Ensayo de transición A→B completado ⚡", false);
            }
        }
    }

    if (now_ - lastSave_ > 15.0) { lastSave_ = now_; library_.save(); if (!autotest_.active) settings_.save(); }
    updateAutotest();
}

void App::idle() {
    if (!ImGui::GetCurrentContext()) return;
    // Hidden window: no mouse gesture may keep a deck captured indefinitely.
    for (int d=0;d<2;++d) {
        if (rollHeld_[d]) { send(d,Cmd::RollEnd); rollHeld_[d]=false; }
        if (reverseHeld_[d]) { engine_.deck[d].reverse=false; reverseHeld_[d]=false; }
        if (deck_[d].scratching) { engine_.deck[d].scratching=false; engine_.deck[d].scratchVelocity=0; deck_[d].scratching=false; }
    }
    logic();
}

bool App::confirmClose() {
    if (!recorder_.active()) return true;
    if (!win::confirm(hwnd_, L"Se está grabando la mezcla. ¿Detener la grabación y salir?", L"DJ Elver Galarga")) return false;
    stopRecording();
    return true;
}

void App::reanalyze(int d) {
    DeckUi& dk = deck_[d];
    if (!dk.track || reanalysis_[d].valid()) return;
    TrackPtr t = dk.track;
    const double mn = settings_.minBpm, mx = settings_.maxBpm;
    reanalysisPath_[d] = dk.path;
    dk.reanalyzing = dk.path;
    reanalysis_[d] = std::async(std::launch::async, [t, mn, mx] {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        return detectBeats(t->pcm.data(), t->frames, t->sampleRate, mn, mx);
    });
}

std::wstring App::selectedLibraryPath() { return libSelectedPath_; }

bool App::endWarning(int d) const {
    if (settings_.endWarnSec <= 0 || !deck_[d].track) return false;
    const DeckStatus& st = engine_.deckStatus[d];
    if (!st.playing.load() || st.loopActive.load()) return false;
    const double rem = deckLenSec(d) - deckPosSec(d);
    return rem > 0 && rem < settings_.endWarnSec;
}

std::string App::keyCode(int key) const {
    if (key < 0) return "—";
    if (settings_.keyNotation == 4) { char b[8]; std::snprintf(b, sizeof b, "%02d%c", keyCamelotNumber(key), keyIsMajor(key) ? 'B' : 'A'); return b; }
    if (settings_.keyNotation == 5) return keyOpenKey(key);
    return keyCamelot(key);
}

std::string App::keyText(int key) const {
    if (key < 0) return "—";
    switch (settings_.keyNotation) {
    case 1: return keyCamelot(key);
    case 2: return keyName(key);
    case 3: return keyNameEs(key);
    case 4: return keyCode(key) + " · " + keyName(key);
    case 5: return keyCode(key) + " · " + keyName(key);
    default: return std::string(keyCamelot(key)) + " · " + keyNameEs(key);
    }
}

std::string App::keyShort(int key) const {
    if (key < 0) return "";
    if (settings_.keyNotation == 2) return keyName(key);
    std::string es = keyNameEs(key);
    if (settings_.keyNotation == 3) return es;
    // "La menor" -> "La m", "Reb Mayor" -> "Reb"
    if (auto p = es.find(" menor"); p != std::string::npos) return es.substr(0, p) + " m";
    if (auto p = es.find(" Mayor"); p != std::string::npos) return es.substr(0, p);
    return es;
}

int App::camelotIndex(int key) const {
    if (key < 0) return -1;
    return std::atoi(keyCamelot(key)) * 2 + (key < 12 ? 1 : 0);
}

float App::drawKeyBadges(ImDrawList* dl, ImVec2 p, float h, int key, bool compat) {
    // dl == nullptr: solo mide.
    const float s = S();
    if (key < 0) { if (dl) dl->AddText(fontHead, h * 0.72f, ImVec2(p.x, p.y + h * 0.12f), col::TextDim, "—"); return 12 * s; }
    float x = p.x;
    auto badge = [&](const char* t, ImU32 c, bool filled) { return dl ? Badge(dl, ImVec2(x, p.y), h, t, c, filled, fontHead) : BadgeWidth(h, t, fontHead); };
    const ImU32 kc = col::camelot(camelotIndex(key));
    if (settings_.keyNotation >= 4) {
        // Como VirtualDJ en modo armónico: el código con el color de su sitio en la rueda.
        x += badge(keyCode(key).c_str(), kc, true);
        return x - p.x;
    }
    const std::string nm = keyShort(key);
    if (settings_.keyNotation != 1 && !nm.empty()) x += badge(nm.c_str(), col::Amber, false) + 4 * s;
    if (settings_.keyNotation != 3) x += badge(keyCamelot(key), kc, compat);
    return x - p.x;
}

int App::ensurePlaylist(const std::string& name) {
    {
        std::lock_guard<std::recursive_mutex> lk(library_.mutex());
        auto& pls = library_.playlists();
        for (size_t i = 0; i < pls.size(); ++i) if (pls[i].name == name) return (int)i;
    }
    return library_.createPlaylist(name);
}

// =================================================================== fotograma

void App::frame() {
    logic();
    for (int d=0; d<2; ++d) {
        if (reverseHeld_[d] && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) { engine_.deck[d].reverse=false; reverseHeld_[d]=false; }
        if (rollHeld_[d] && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) { send(d,Cmd::RollEnd); rollHeld_[d]=false; }
        if (deck_[d].scratching && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) { engine_.deck[d].scratching=false; engine_.deck[d].scratchVelocity=0; deck_[d].scratching=false; }
    }
    if (!fsBuilt_) buildFsTree();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float s = S();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##principal", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);

    {
        ImDrawList* bg = ImGui::GetWindowDrawList();
        const ImVec2 a = vp->WorkPos, b(a.x + vp->WorkSize.x, a.y + vp->WorkSize.y);
        bg->AddRectFilled(a, b, col::Bg);
        if (col::TintLeft != col::Bg || col::TintRight != col::Bg) {
            const float mid = a.x + vp->WorkSize.x * 0.5f;
            bg->AddRectFilledMultiColor(a, ImVec2(mid, b.y), col::TintLeft, col::Bg, col::Bg, col::TintLeft);
            bg->AddRectFilledMultiColor(ImVec2(mid, a.y), b, col::Bg, col::TintRight, col::TintRight, col::Bg);
        }
    }

    // Medidores: tomar picos del audio y aplicar caída/retención.
    const float dt = ImGui::GetIO().DeltaTime;
    const float decay = std::exp(-dt / 0.25f);
    for (int d = 0; d < kNumDecks; ++d) {
        DeckUi& dk = deck_[d];
        const float pl = engine_.deckStatus[d].peakL.exchange(0.f), pr = engine_.deckStatus[d].peakR.exchange(0.f);
        dk.meterL = std::max(pl, dk.meterL * decay);
        dk.meterR = std::max(pr, dk.meterR * decay);
        for (int c = 0; c < 2; ++c) {
            const float v = c ? dk.meterR : dk.meterL;
            if (v >= dk.meterHold[c]) { dk.meterHold[c] = v; dk.holdTime[c] = now_; }
            else if (now_ - dk.holdTime[c] > 1.5) dk.meterHold[c] *= decay;
        }
    }
    {
        const float pl = engine_.status.masterPeakL.exchange(0.f), pr = engine_.status.masterPeakR.exchange(0.f);
        float& ml = masterMeter_[0];
        float& mr = masterMeter_[1];
        ml = std::max(pl, ml * decay); mr = std::max(pr, mr * decay);
        const float vals[2] = {ml, mr};
        for (int c = 0; c < 2; ++c) {
            if (vals[c] >= masterHold_[c]) { masterHold_[c] = vals[c]; masterHoldTime_[c] = now_; }
            else if (now_ - masterHoldTime_[c] > 1.5) masterHold_[c] *= decay;
        }
        if (engine_.status.limiterGain.exchange(1.f) < 0.891f) limitFlashUntil_ = now_ + 0.4;
        if (engine_.status.preLimiterPeak.exchange(0.f) > 1.0f) clipFlashUntil_ = now_ + 0.4;
    }

    const ImVec2 o = vp->WorkPos, sz = vp->WorkSize;
    const float gap = 8 * s;
    const float topH = 52 * s, statusH = 24 * s;
    float y = o.y;
    drawTopBar(topH);
    y += topH + gap;
    const float bottom0 = o.y + sz.y - statusH - gap;
    // 0.10.5: con el rack abierto en pantallas bajas el navegador puede encoger algo más (para que quepa el rack).
    const float browserMin = (mode_ == 0 && settings_.rackOpen && sz.y < 900 * s ? 118 : 150) * s;
    // Separador horizontal arrastrable (cambia el alto de lo que tiene encima).
    auto hsplit = [&](const char* id, float yLine, float* setting, float cur, float mn, float mx, bool* collapsedFlag) {
        ImGui::SetCursorScreenPos(ImVec2(o.x + gap, yLine - gap * 0.5f - 3 * s));
        ImGui::InvisibleButton(id, ImVec2(sz.x - 2 * gap, gap + 6 * s));
        const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
        if (hot) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float cxm = o.x + sz.x * 0.5f;
        dl->AddRectFilled(ImVec2(cxm - 22 * s, yLine - 1.5f * s), ImVec2(cxm + 22 * s, yLine + 1.5f * s), hot ? col::DeckA : col::Line, 2 * s);
        if (hot) dl->AddLine(ImVec2(o.x + gap + 10 * s, yLine), ImVec2(o.x + sz.x - gap - 10 * s, yLine), col::withAlpha(col::DeckA, 120), 1.f);
        ImGui::SetItemTooltip("Arrastra para cambiar el tamaño · doble clic: tamaño automático");
        if (ImGui::IsItemActive() && ImGui::GetIO().MouseDelta.y != 0.f) {
            float v = (cur + ImGui::GetIO().MouseDelta.y) / s;
            if (collapsedFlag) {
                // Decks: por debajo del mínimo se pliegan en una línea; al bajar, se despliegan.
                if (*collapsedFlag) { if (ImGui::GetMouseDragDelta(0).y > 60 * s) { *collapsedFlag = false; *setting = 200.f; ImGui::ResetMouseDragDelta(0); } return; }
                if (v < mn - 40.f) { *collapsedFlag = true; ImGui::ResetMouseDragDelta(0); return; }
            }
            *setting = std::clamp(v, mn, mx);
        }
        if (ImGui::IsItemDeactivated()) settings_.save();
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) { *setting = 0; if (collapsedFlag) *collapsedFlag = false; }
    };
    if (mode_ == 1) {
        // SEPARADOR: ocupa la zona de ondas, platos y mezclador (el audio de los platos sigue sonando).
        // Más alto que antes: los carriles de cada instrumento necesitan sitio (el navegador queda debajo).
        const float h = std::clamp((bottom0 - y) * 0.6f, std::min(320 * s, bottom0 - y - browserMin - gap), bottom0 - y - browserMin - gap);
        drawSeparator(ImVec2(o.x + gap, y), ImVec2(o.x + sz.x - gap, y + h));
        y += h + gap;
    } else if (mode_ == 3) {
        // ELVERKEY / MIX LAB a pantalla completa (ocupa todo el espacio hasta la barra de estado).
        drawElverKeyPanel(ImVec2(o.x + gap, y), ImVec2(o.x + sz.x - gap, bottom0));
        y = bottom0;
    } else if (mode_ == 2) {
        // BIBLIOTECA: navegador a pantalla completa; arriba, lo esencial de cada plato.
        if (settings_.showWaves) {
            const float waveH = std::clamp(sz.y * 0.058f, 46 * s, 64 * s);
            drawWaveRows(ImVec2(o.x + gap, y), ImVec2(o.x + sz.x - gap, y + waveH * 2 + 6 * s));
            y += waveH * 2 + 6 * s + gap;
        } else {
            drawCompactDecks(ImVec2(o.x + gap, y), ImVec2(o.x + sz.x - gap, y + 78 * s));
            y += 78 * s + gap;
        }
    } else {
        // MEZCLA: ondas grandes (opcionales), decks y mezclador, navegador.
        if (settings_.showWaves) {
            // 0.10.4: con el rack abierto y poca altura, ondas algo más finas para que los platos tengan sus dos filas.
            const bool tight = settings_.rackOpen && sz.y < 1000 * s;
            const float autoRow = tight ? std::clamp(sz.y * 0.058f - (sz.y < 860 * s ? 8 * s : 0.f), 38 * s, 96 * s) : std::clamp(sz.y * 0.072f, 52 * s, 96 * s);
            const float rowH = settings_.waveRowH > 0 ? settings_.waveRowH * s : autoRow;
            const float wavesH = rowH * 2 + 6 * s;
            drawWaveRows(ImVec2(o.x + gap, y), ImVec2(o.x + sz.x - gap, y + wavesH));
            y += wavesH + gap;
            hsplit("##wavesplit", y - gap * 0.5f, &settings_.waveRowH, rowH, 40.f, 200.f, nullptr);
        }
        // Rack bajo el mezclador: desplegado o en una línea. Si no cabe con lo mínimo de platos y
        // navegador, se queda en una línea (sin cambiar la preferencia).
        const float rackBar = 30 * s;
        // 0.10.5: el rack se abre siempre que quepa con los platos al mínimo (186 px); los platos se agrandan
        // con lo que sobre (con dos filas debajo cuando hay sitio). En la 0.10.4 se pedían 270 px y en
        // pantallas de ~850 px el rack ya no se desplegaba.
        const float deckMin = settings_.decksCollapsed ? 78 * s : 186 * s;
        const float rackRoom = bottom0 - y - browserMin - gap - deckMin - gap - gap;
        const float rackMinH = 200 * s;   // lo que necesita la estación completa
        const float rackAuto = std::clamp((bottom0 - y) * 0.28f, 200 * s, 250 * s);
        const bool rackOpen = settings_.rackOpen && rackRoom >= rackMinH;
        const float rackH = rackOpen ? std::clamp(settings_.rackH > 0 ? settings_.rackH * s : rackAuto, rackMinH, rackRoom) : rackBar;
        const float rackSpace = rackH + gap;
        if (settings_.decksCollapsed) {
            const float ch = 78 * s;
            drawCompactDecks(ImVec2(o.x + gap, y), ImVec2(o.x + sz.x - gap, y + ch));
            y += ch + gap;
            hsplit("##decksplit", y - gap * 0.5f, &settings_.deckHeight, ch, 186.f, 560.f, &settings_.decksCollapsed);
        } else {
            const float maxDeckH = std::max(216 * s, bottom0 - y - browserMin - gap - rackSpace);
            // 0.14.0 Pro: espacio para la barra permanente de STEMS en vivo + las dos filas de pads inferiores.
            const float defH = std::clamp((bottom0 - y - rackSpace) * 0.64f, 284 * s, 342 * s);
            const float deckH = std::clamp(settings_.deckHeight > 0 ? std::max(settings_.deckHeight * s, 284 * s) : defH, 216 * s, maxDeckH);
            const float mixW = std::clamp(sz.x * 0.175f, 236 * s, 300 * s);
            const float deckW = (sz.x - mixW - 4 * gap) * 0.5f;
            drawDeck(0, ImVec2(o.x + gap, y), ImVec2(o.x + gap + deckW, y + deckH));
            drawMixer(ImVec2(o.x + 2 * gap + deckW, y), ImVec2(o.x + 2 * gap + deckW + mixW, y + deckH));
            drawDeck(1, ImVec2(o.x + 3 * gap + deckW + mixW, y), ImVec2(o.x + sz.x - gap, y + deckH));
            y += deckH + gap;
            hsplit("##decksplit", y - gap * 0.5f, &settings_.deckHeight, deckH, 186.f, std::max(186.f, maxDeckH / s), &settings_.decksCollapsed);
        }
        const float rackHNow = rackOpen ? std::clamp(rackH, rackMinH, std::max(rackMinH, bottom0 - y - browserMin - gap)) : rackBar;
        drawRack(ImVec2(o.x + gap, y), ImVec2(o.x + sz.x - gap, y + rackHNow), rackOpen);
        y += rackHNow + gap;
        if (rackOpen) hsplit("##racksplit", y - gap * 0.5f, &settings_.rackH, rackHNow, 200.f, 420.f, nullptr);
    }
    if (mode_ != 3) drawBrowser(ImVec2(o.x + gap, y), ImVec2(o.x + sz.x - gap, bottom0));
    drawStatusBar(ImVec2(o.x, o.y + sz.y - statusH), ImVec2(o.x + sz.x, o.y + sz.y));
    {
        // Cambio de vista: fundido corto (150 ms) desde el fondo, sin coste cuando no hay cambio.
        static int lastMode = -1;
        static double changedAt = -10;
        if (mode_ != lastMode) { if (lastMode >= 0) changedAt = now_; lastMode = mode_; }
        const double t = (now_ - changedAt) / 0.15;
        if (t < 1.0) ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(o.x, o.y + topH), ImVec2(o.x + sz.x, o.y + sz.y - statusH),
                                                                col::withAlpha(col::Bg, (int)(200 * (1.0 - t) * (1.0 - t))));
    }

    drawConfirmLoad();
    if (showRecording_) drawRecordingDialog();
    if (showLibraryTools_) drawLibraryTools();
    if (showElverKey_ && mode_ != 3) drawElverKeyWindow();
    if (showAudio_) drawAudioDialog();
    if (showMidi_) drawMidiDialog();
    if (showPrefs_) drawPrefsDialog();
    if (showVdj_) drawVdjImportDialog();
    if (showAbout_) drawAboutDialog();
    if (showScheduler_) drawSchedulerDialog();
    ImGui::End();
}

// =================================================================== barra superior

void App::drawTopBar(float h) {
    const float s = S();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = vp->WorkPos, b(a.x + vp->WorkSize.x, a.y + h);
    dl->AddRectFilled(a, b, col::mix(col::Bg, col::Panel, 0.6f));
    dl->AddLine(ImVec2(a.x, b.y - 0.5f), ImVec2(b.x, b.y - 0.5f), col::Line);
    const float cy = a.y + h * 0.5f;

    // Logotipo: DJ ELVER GALARGA (desde la 0.10.6)
    const float x = a.x + 16 * s;
    const float logoEnd = x + DrawAppLogo(dl, ImVec2(x, cy), 28 * s, fontBold, fontHead) + 24 * s;

    // ---- Grupo derecho (de derecha a izquierda)
    const float bh = 34 * s, by = cy - bh * 0.5f;
    float rx = b.x - 14 * s;
    const bool ctrl = !midi_.devices().empty();
    rx -= 38 * s;
    ImGui::SetCursorScreenPos(ImVec2(rx, by));
    if (IconButton("##ajustes", Ic::Gear, ImVec2(38 * s, bh), col::DeckA, false, true, 0.56f)) ImGui::OpenPopup("menuajustes");
    ImGui::SetItemTooltip(ctrl ? "Ajustes · mando MIDI conectado" : "Ajustes: audio, mando MIDI, preferencias, diseño…");
    if (ctrl) dl->AddCircleFilled(ImVec2(rx + 31 * s, by + 8 * s), 3.5f * s, col::Green);
    ImGui::SetNextWindowSizeConstraints(ImVec2(300 * s, 0), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::BeginPopup("menuajustes")) {
        if (ImGui::MenuItem("Audio: salida, latencia y auriculares…")) { showAudio_ = true; pendingAudio_ = settings_.audio; audio_.refreshDevices(); }
        if (ImGui::MenuItem(ctrl ? "Mando MIDI (conectado)…" : "Mando MIDI…")) showMidi_ = true;
        if (ImGui::MenuItem("Preferencias…")) showPrefs_ = true;
        if (ImGui::MenuItem("Programador de eventos…", nullptr, fadeOutStart_ >= 0)) showScheduler_ = true;
        if (ImGui::MenuItem("Configurar grabación (carpeta, nombre, listado)…")) showRecording_ = true;
        if (ImGui::MenuItem("Organizar biblioteca: listas inteligentes y sugerencias…")) showLibraryTools_ = true;
        if (ImGui::MenuItem("Importar de VirtualDJ: tonalidad, BPM, cues y listas…")) openVdjImport();
        ImGui::Separator();
        if (ImGui::BeginMenu("Vista y diseño")) { drawViewMenu(); ImGui::EndMenu(); }
        ImGui::Separator();
        if (ImGui::MenuItem("Abrir la carpeta de grabaciones")) win::openFolder(recordingFolder());
        if (ImGui::MenuItem("Abrir la carpeta de stems separados")) win::openFolder(stems_.stemsRoot());
        ImGui::Separator();
        if (ImGui::MenuItem("Acerca de DJ Elver Galarga")) showAbout_ = true;
        ImGui::EndPopup();
    }

    // Grabar
    {
        const bool rec = recorder_.active();
        char lbl[64];
        if (rec) std::snprintf(lbl, sizeof lbl, "REC  %s", formatTime(recorder_.seconds(), false).c_str());
        else std::snprintf(lbl, sizeof lbl, "Grabar");
        const float tw = fontHead->CalcTextSizeA(15 * s, FLT_MAX, 0, lbl).x;
        const float w = tw + 44 * s;
        rx -= w + 8 * s;
        const ImVec2 p(rx, by), q(rx + w, by + bh);
        ImGui::SetCursorScreenPos(p);
        if (ImGui::InvisibleButton("##rec", ImVec2(w, bh))) { if (rec) stopRecording(); else startRecording(); }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) showRecording_=true;
        const bool hov = ImGui::IsItemHovered();
        if (rec) {
            dl->AddRectFilled(p, q, col::withAlpha(col::Red, 46), 7 * s);
            dl->AddRect(p, q, col::Red, 7 * s, 0, 1.2f * s);
        } else {
            dl->AddRectFilled(p, q, hov ? col::mix(col::Panel2, col::Red, 0.12f) : col::Panel2, 7 * s);
            dl->AddRect(p, q, col::mix(col::Line, col::Red, hov ? 0.7f : 0.15f), 7 * s, 0, 1.f);
        }
        const bool blink = !rec || std::fmod(now_, 1.0) < 0.6;
        dl->AddCircleFilled(ImVec2(p.x + 18 * s, cy), 5.5f * s, blink ? col::Red : col::withAlpha(col::Red, 90));
        dl->AddText(fontHead, 15 * s, ImVec2(p.x + 32 * s, cy - 9 * s), col::Text, lbl);
        if (rec) ImGui::SetItemTooltip("Grabando en %s · clic para detener", toUtf8(recorder_.path()).c_str());
        else ImGui::SetItemTooltip("Graba la salida master en WAV. Clic derecho: configurar carpeta, nombre y formato.");
    }

    // 0.11.1: QUITAR FX (cuántos efectos y filtros quedan puestos, cuáles son, y quitarlos de un clic)
    drawFxClearButton(rx, by, bh);

    // Botón directo ELVERKEY / MIX LAB (ventana interna integrada + selector de color)
    {
        const char* ekLbl = "ELVERKEY ⚡";
        const float ekTw = fontHead->CalcTextSizeA(14 * s, FLT_MAX, 0, ekLbl).x;
        const float ekW = ekTw + 24 * s;
        rx -= ekW + 8 * s;
        const ImVec2 ep(rx, by), eq(rx + ekW, by + bh);
        ImGui::SetCursorScreenPos(ep);
        const bool ekActive = (mode_ == 3) || showElverKey_;
        if (ImGui::InvisibleButton("##elverkey_btn", ImVec2(ekW, bh))) {
            if (showElverKey_) showElverKey_ = false;
            else mode_ = (mode_ == 3) ? 0 : 3;
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            if (mode_ == 3) mode_ = 0;
            showElverKey_ = !showElverKey_;
        }
        const bool ekHov = ImGui::IsItemHovered();
        dl->AddRectFilled(ep, eq, ekActive ? col::withAlpha(col::DeckA, 55) : (ekHov ? col::mix(col::Panel2, col::DeckA, 0.22f) : col::mix(col::Panel2, col::DeckA, 0.10f)), 7 * s);
        dl->AddRect(ep, eq, ekActive ? col::DeckA : col::mix(col::Line, col::DeckA, ekHov ? 0.85f : 0.45f), 7 * s, 0, 1.2f * s);
        dl->AddText(fontHead, 14 * s, ImVec2(ep.x + 12 * s, cy - 8 * s), ekActive ? col::DeckA : col::Text, ekLbl);
        ImGui::SetItemTooltip("Abrir/cerrar la ventana interna ELVERKEY / MIX LAB (rueda armónica, banco A→B, auto-set y colores).\nClic derecho: fijar como vista acoplada arriba.");
    }

    // Auriculares (preescucha)
    {
        rx -= 38 * s + 8 * s;
        ImGui::SetCursorScreenPos(ImVec2(rx, by));
        const bool pfl = engine_.deck[0].pfl.load() || engine_.deck[1].pfl.load();
        if (IconButton("##auriculares", Ic::Headphones, ImVec2(38 * s, bh), col::Amber, pfl, true, 0.56f)) ImGui::OpenPopup("auriculares");
        ImGui::SetItemTooltip("Auriculares: volumen, mezcla y preescucha");
        if (ImGui::BeginPopup("auriculares")) {
            ImGui::PushFont(fontHead, 15.f);
            ImGui::TextUnformatted("AURICULARES");
            ImGui::PopFont();
            const bool hpOn = engine_.mixer.hpRouting.load() != (int)HpRouting::Ninguna;
            if (!hpOn) {
                ImGui::TextDisabled("La preescucha necesita una salida de auriculares.");
                if (NeonButton("Configurar la salida…##hpcfg", col::Amber, false, ImVec2(240 * s, 28 * s))) {
                    showAudio_ = true; pendingAudio_ = settings_.audio; audio_.refreshDevices(); ImGui::CloseCurrentPopup();
                }
            } else {
                float hv = engine_.mixer.hpVol.load();
                if (Knob("##hpvol", "VOLUMEN", &hv, 0.f, 1.f, 0.7f, false, 18 * s, col::Amber, "Volumen de auriculares")) engine_.mixer.hpVol = hv;
                ImGui::SameLine(0, 24 * s);
                float hm = engine_.mixer.hpMix.load();
                char buf[80];
                std::snprintf(buf, sizeof buf, "Mezcla: %d %% preescucha / %d %% master", (int)std::lround((1 - hm) * 100), (int)std::lround(hm * 100));
                if (Knob("##hpmix", "CUE / MASTER", &hm, 0.f, 1.f, 0.f, false, 18 * s, col::Amber, buf)) engine_.mixer.hpMix = hm;
            }
            for (int d = 0; d < kNumDecks; ++d) {
                if (d) ImGui::SameLine();
                const bool on = engine_.deck[d].pfl.load();
                if (NeonButton(d ? "CUE B##pflb" : "CUE A##pfla", col::Amber, on, ImVec2(118 * s, 28 * s))) engine_.deck[d].pfl = !on;
            }
            ImGui::EndPopup();
        }
    }

    // Master: medidor horizontal, limitador y volumen
    {
        rx -= 16 * s;
        const float kr = 12 * s;
        rx -= 2 * kr;
        ImGui::SetCursorScreenPos(ImVec2(rx, cy - kr));
        float mv = engine_.mixer.masterVol.load();
        char buf[64];
        std::snprintf(buf, sizeof buf, "Master %.1f dB · doble clic: por defecto", linToDb(faderToGain(mv)));
        if (Knob("##master", nullptr, &mv, 0.f, 1.f, 0.8f, false, kr, col::Text, buf)) engine_.mixer.masterVol = mv;
        const float mw = std::clamp(vp->WorkSize.x * 0.085f, 90 * s, 150 * s);
        rx -= 10 * s + mw;
        const float mh = 5 * s;
        HMeter(dl, ImVec2(rx, cy - mh - 1 * s), ImVec2(rx + mw, cy - 1 * s), masterHold_[0], masterHold_[0], now_ < clipFlashUntil_);
        HMeter(dl, ImVec2(rx, cy + 1 * s), ImVec2(rx + mw, cy + mh + 1 * s), masterHold_[1], masterHold_[1], now_ < clipFlashUntil_);
        const bool lim = now_ < limitFlashUntil_;
        const char* lt = "LIM";
        const float lw = fontHead->CalcTextSizeA(12 * s, FLT_MAX, 0, lt).x + 10 * s;
        rx -= lw + 8 * s;
        const ImVec2 la(rx, cy - 8 * s), lb(rx + lw, cy + 8 * s);
        dl->AddRectFilled(la, lb, lim ? col::Amber : col::Panel2, 4 * s);
        CenterText(dl, la, lb, lim ? col::onColor(col::Amber) : col::TextDim, lt, fontHead, 12 * s);
        ImGui::SetCursorScreenPos(la);
        ImGui::InvisibleButton("##limtip", ImVec2(lw, 16 * s));
        ImGui::SetItemTooltip("Limitador del master: se enciende cuando evita la saturación (%u veces)", engine_.status.limiterHits.load());
        const char* mt = "Master";
        const float mtw = fontHead->CalcTextSizeA(14 * s, FLT_MAX, 0, mt).x;
        rx -= mtw + 8 * s;
        dl->AddText(fontHead, 14 * s, ImVec2(rx, cy - 8.5f * s), col::TextDim, mt);
    }
    // Vista: botón directo para las ondas grandes y menú de vista.
    {
        rx -= 18 * s + 38 * s;
        ImGui::SetCursorScreenPos(ImVec2(rx, by));
        if (IconButton("##vista", Ic::Layout, ImVec2(38 * s, bh), col::DeckA, false, true, 0.56f)) ImGui::OpenPopup("menuvista");
        ImGui::SetItemTooltip("Vista: ondas, decks, rejilla, colores y diseño");
        ImGui::SetNextWindowSizeConstraints(ImVec2(320 * s, 0), ImVec2(FLT_MAX, FLT_MAX));
        if (ImGui::BeginPopup("menuvista")) { drawViewMenu(); ImGui::EndPopup(); }
        rx -= 38 * s + 6 * s;
        ImGui::SetCursorScreenPos(ImVec2(rx, by));
        if (IconButton("##ondas", Ic::Wave, ImVec2(38 * s, bh), col::DeckA, settings_.showWaves, true, 0.6f)) { settings_.showWaves = !settings_.showWaves; settings_.save(); }
        ImGui::SetItemTooltip(settings_.showWaves ? "Ondas grandes: activadas (clic para ocultarlas y ganar sitio para la biblioteca)" : "Ondas grandes: ocultas (clic para mostrarlas arriba)");
    }
    const float rightStart = rx - 16 * s;

    // ---- Pestañas de modo, centradas en el espacio libre
    int pend = 0;
    for (auto& j : stems_.jobs()) if (!j.done && !j.failed) ++pend;
    char sepLbl[40];
    std::snprintf(sepLbl, sizeof sepLbl, pend ? "STEMS / SEPARADOR (%d)" : "STEMS / SEPARADOR", pend);
    const char* labels[4] = {"MEZCLA", "BIBLIOTECA", "ELVERKEY / MIX LAB", sepLbl};
    const int modes[4] = {0, 2, 3, 1};
    const char* tips[4] = {"Dos platos, mezclador y navegador", "Navegador a pantalla completa (carpetas, listas y Sidelist)",
                           "ElverKey Studio / Mix Lab integrado: rueda Camelot, banco de transiciones A→B, auto-set por estilo y temas de color",
                           "Separa una canción en voz e instrumentos (tipo Moises)"};
    const float tfs = 15 * s;
    float tw[4], total = 0;
    for (int i = 0; i < 4; ++i) { tw[i] = fontHead->CalcTextSizeA(tfs, FLT_MAX, 0, labels[i]).x + 30 * s; total += tw[i]; }
    float tx = std::max(logoEnd, std::min((a.x + b.x - total) * 0.5f, rightStart - total));
    for (int i = 0; i < 4; ++i) {
        ImGui::SetCursorScreenPos(ImVec2(tx, a.y));
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##modo", ImVec2(tw[i], h - 1))) { mode_ = modes[i]; showElverKey_ = false; }
        ImGui::PopID();
        const bool hov = ImGui::IsItemHovered(), on = mode_ == modes[i];
        ImGui::SetItemTooltip("%s", tips[i]);
        const ImU32 tc = on ? col::Text : hov ? col::mix(col::TextDim, col::Text, 0.6f) : col::TextDim;
        CenterText(dl, ImVec2(tx, a.y), ImVec2(tx + tw[i], b.y), tc, labels[i], fontHead, tfs);
        if (on) dl->AddRectFilled(ImVec2(tx + 14 * s, b.y - 3 * s), ImVec2(tx + tw[i] - 14 * s, b.y), col::DeckA, 2 * s);
        tx += tw[i];
    }
}

// =================================================================== menú de vista

void App::drawViewMenu() {
    const float s = S();
    bool ch = false;
    ImGui::PushFont(fontHead, 13.f);
    ImGui::TextDisabled("PANTALLA");
    ImGui::PopFont();
    if (ImGui::MenuItem("Ondas grandes arriba", nullptr, settings_.showWaves)) { settings_.showWaves = !settings_.showWaves; ch = true; }
    if (ImGui::MenuItem("Decks plegados en una línea (más biblioteca)", nullptr, settings_.decksCollapsed)) { settings_.decksCollapsed = !settings_.decksCollapsed; ch = true; }
    if (ImGui::MenuItem("Pads y efectos en los decks", nullptr, settings_.showPerformance)) { settings_.showPerformance = !settings_.showPerformance; ch = true; }
    if (ImGui::MenuItem("Rack de efectos, pad XY, aislador y loop desplegado", nullptr, settings_.rackOpen)) { settings_.rackOpen = !settings_.rackOpen; ch = true; }
    if (ImGui::BeginMenu("Tamaño de la interfaz")) {
        for (int v : {90, 100, 110, 125, 150}) {
            char l[32];
            std::snprintf(l, sizeof l, v == 100 ? "%d %% (como Windows)" : "%d %%", v);
            if (ImGui::MenuItem(l, nullptr, settings_.uiScale == v)) { settings_.uiScale = v; ch = true; }
        }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Restablecer tamaños")) { settings_.deckHeight = 0; settings_.waveRowH = 0; settings_.rackH = 0; settings_.treeW = settings_.sideW = settings_.infoW = 0; settings_.decksCollapsed = false; ch = true; }
    ImGui::TextDisabled("Arrastra las líneas entre zonas para cambiar su tamaño.");
    ImGui::Separator();
    ImGui::PushFont(fontHead, 13.f);
    ImGui::TextDisabled("ONDAS");
    ImGui::PopFont();
    if (ImGui::MenuItem("Rejilla: pulsos, compases y frases", nullptr, settings_.waveGrid)) { settings_.waveGrid = !settings_.waveGrid; ch = true; }
    if (ImGui::MenuItem("Marcar cortes (sin bombo) y subidones", nullptr, settings_.waveStructure)) { settings_.waveStructure = !settings_.waveStructure; ch = true; }
    {
        const char* ws[3] = {"Color de cada plato (graves, medios y agudos en capas)", "Tres bandas: graves, medios y agudos", "Espectro de color (rojo graves, verde medios, azul agudos)"};
        for (int i = 0; i < 3; ++i) if (ImGui::MenuItem(ws[i], nullptr, settings_.waveStyle == i)) { settings_.waveStyle = i; ch = true; }
    }
    if (ImGui::MenuItem("Carátula en el centro de los platos", nullptr, settings_.coverInJog)) { settings_.coverInJog = !settings_.coverInJog; ch = true; }
    ImGui::Separator();
    ImGui::PushFont(fontHead, 13.f);
    ImGui::TextDisabled("DISEÑO");
    ImGui::PopFont();
    for (int i = 0; i < ui::themeCount(); ++i) {
        const ui::Theme& th = ui::themeAt(i);
        ImGui::PushID(i + 700);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##th", ui::currentThemeIndex() == i, 0, ImVec2(360 * s, 42 * s))) { settings_.theme = i; ui::applyTheme(i); ch = true; }
        ImDrawList* pdl = ImGui::GetWindowDrawList();
        // Muestra: fondo, panel, plato A, plato B y una ondita en los colores de las bandas.
        const ImVec2 sa(p0.x + 6 * s, p0.y + 5 * s), sb(p0.x + 84 * s, p0.y + 37 * s);
        pdl->AddRectFilled(sa, sb, th.bg, 4 * s);
        pdl->AddRectFilled(ImVec2(sa.x + 3 * s, sa.y + 3 * s), ImVec2(sb.x - 3 * s, sa.y + 13 * s), th.waveBg, 2 * s);
        for (int k = 0; k < 18; ++k) {
            const float x = sa.x + 5 * s + k * 4 * s, hh = (k % 4 == 0 ? 4.5f : k % 2 ? 2.f : 3.f) * s;
            pdl->AddRectFilled(ImVec2(x, sa.y + 8 * s - hh), ImVec2(x + 2 * s, sa.y + 8 * s + hh), k % 4 == 0 ? th.waveLow : k % 2 ? th.waveHigh : th.waveMid);
        }
        pdl->AddRectFilled(ImVec2(sa.x + 3 * s, sa.y + 16 * s), ImVec2(sa.x + 37 * s, sb.y - 3 * s), th.panel, 2 * s);
        pdl->AddRectFilled(ImVec2(sa.x + 41 * s, sa.y + 16 * s), ImVec2(sb.x - 3 * s, sb.y - 3 * s), th.panel, 2 * s);
        pdl->AddRectFilled(ImVec2(sa.x + 7 * s, sa.y + 20 * s), ImVec2(sa.x + 25 * s, sa.y + 27 * s), th.deckA, 2 * s);
        pdl->AddRectFilled(ImVec2(sa.x + 45 * s, sa.y + 20 * s), ImVec2(sa.x + 63 * s, sa.y + 27 * s), th.deckB, 2 * s);
        pdl->AddText(fontHead, 15 * s, ImVec2(p0.x + 94 * s, p0.y + 3 * s), col::Text, th.name);
        const std::string desc = ellipsize(th.description, 266 * s, ImGui::GetFont(), 12.5f * s);
        pdl->AddText(ImGui::GetFont(), 12.5f * s, ImVec2(p0.x + 94 * s, p0.y + 22 * s), col::TextDim, desc.c_str());
        ImGui::SetItemTooltip("%s", th.description);
        ImGui::PopID();
    }
    if (ch) settings_.save();
}

// =================================================================== plato (jog virtual)

void App::drawPlatter(int d, ImVec2 c, float r) {
    const float s = S();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    DeckUi& dk = deck_[d];
    DeckParams& p = engine_.deck[d];
    const ImU32 acc = col::deck(d);
    const bool playing = engine_.deckStatus[d].playing.load();
    const bool touching = dk.scratching || dk.jogTouched || dk.platterNudge;

    ImGui::SetCursorScreenPos(ImVec2(c.x - r, c.y - r));
    char id[16];
    std::snprintf(id, sizeof id, "##plato%d", d);
    ImGui::InvisibleButton(id, ImVec2(2 * r, 2 * r));
    const bool hov = ImGui::IsItemHovered();

    const float ring = std::max(4.f, r * 0.06f);
    // Disco: superficie mate con surcos muy tenues y marcas de referencia cada 30°.
    dl->AddCircleFilled(c, r - ring * 0.5f, col::mix(col::Bg, col::Panel, 0.4f), 96);
    for (int i = 0; i < 10; ++i) dl->AddCircle(c, r * (0.5f + 0.045f * i), IM_COL32(255, 255, 255, (i % 2) ? 9 : 5), 72, 1.f);
    for (int i = 0; i < 12; ++i) {
        const float a = i * IM_PI / 6.f;
        const float r0 = r - ring * (i % 3 == 0 ? 3.4f : 2.6f), r1 = r - ring * 1.7f;
        dl->AddLine(ImVec2(c.x + std::cos(a) * r0, c.y + std::sin(a) * r0), ImVec2(c.x + std::cos(a) * r1, c.y + std::sin(a) * r1),
                    col::withAlpha(col::Text, i % 3 == 0 ? 70 : 34), 1.f * s);
    }
    // Aro: pista + progreso (rojo y latiendo en los últimos segundos)
    dl->PathArcTo(c, r - ring * 0.5f, 0, 2 * IM_PI, 96);
    dl->PathStroke(col::Line, 0, ring);
    Track* t = dk.track.get();
    const double pos = deckPosSec(d), len = deckLenSec(d);
    const float a0 = -IM_PI * 0.5f;
    if (t && len > 0) {
        const float frac = (float)std::clamp(pos / len, 0.0, 1.0);
        const bool ending = endWarning(d);
        const float blink = 0.5f + 0.5f * std::cos((float)now_ * 6.2832f * 1.4f);
        const ImU32 ringC = ending ? col::mix(col::Red, acc, 0.5f * (1.f - blink)) : acc;
        if (frac > 0.001f) {
            if (col::Glow > 0.3f || touching) {
                dl->PathArcTo(c, r - ring * 0.5f, a0, a0 + 2 * IM_PI * frac, 96);
                dl->PathStroke(col::withAlpha(ringC, 70), 0, ring * 2.2f);
            }
            dl->PathArcTo(c, r - ring * 0.5f, a0, a0 + 2 * IM_PI * frac, 96);
            dl->PathStroke(ringC, 0, ring);
        }
        // Hot cues en el aro (puntos de su color) y cue (ámbar, más grande).
        for (int i = 0; i < kNumHotCues; ++i) {
            const double h = engine_.deckStatus[d].hotCues[i].load();
            if (h < 0) continue;
            const float ha = a0 + 2 * IM_PI * (float)(h / t->sampleRate / len);
            dl->AddCircleFilled(ImVec2(c.x + std::cos(ha) * (r - ring * 0.5f), c.y + std::sin(ha) * (r - ring * 0.5f)), ring * 0.55f, kHotColors[i]);
        }
        const double cueF = engine_.deckStatus[d].cueFrames.load() / t->sampleRate / len;
        const float ca = a0 + 2 * IM_PI * (float)cueF;
        dl->AddCircleFilled(ImVec2(c.x + std::cos(ca) * (r - ring * 0.5f), c.y + std::sin(ca) * (r - ring * 0.5f)), ring * 0.8f, col::Amber);
        // Marca que gira con la pista (33⅓ rpm)
        const float ang = a0 + 2 * IM_PI * (float)std::fmod(pos / kVinylSecPerRev, 1.0);
        const ImVec2 p0(c.x + std::cos(ang) * r * 0.62f, c.y + std::sin(ang) * r * 0.62f), p1(c.x + std::cos(ang) * (r - ring * 2.2f), c.y + std::sin(ang) * (r - ring * 2.2f));
        dl->AddLine(p0, p1, touching ? acc : col::withAlpha(col::Text, 200), 2.5f * s);
    }
    if (touching) dl->AddCircle(c, r + 2 * s, col::withAlpha(acc, 160), 96, 2.f * s);
    // Etiqueta central: BPM y pitch
    const float lr = r * 0.5f;
    dl->AddCircleFilled(c, lr, col::Panel2, 64);
    if (t && settings_.coverInJog) {
        // Carátula en el centro del plato (como en Ramiro MixLab), velada para que se lean el BPM y el pitch.
        DrawCover(dl, dk.path, ImVec2(c.x - lr, c.y - lr), ImVec2(c.x + lr, c.y + lr), lr, dk.title.c_str());
        dl->AddCircleFilled(c, lr, col::withAlpha(col::Bg, 150), 64);
    }
    dl->AddCircle(c, lr, col::Line, 64, 1.f);
    if (t) {
        char bpm[24], pt[24];
        if (dk.meta.bpm > 0) std::snprintf(bpm, sizeof bpm, "%.2f", engine_.effectiveBpm(d));
        else std::snprintf(bpm, sizeof bpm, "--.--");
        if (dk.platterNudge) std::snprintf(pt, sizeof pt, "EMPUJE %+.1f%%", engine_.deck[d].bend.load() * 100.f);
        else std::snprintf(pt, sizeof pt, touching ? "SCRATCH" : "%+.1f%%", dk.pitch * 100.f);
        const float fs = std::min(18.f * s, lr * 0.5f);
        CenterText(dl, ImVec2(c.x - lr, c.y - lr * 0.55f), ImVec2(c.x + lr, c.y + lr * 0.05f), dk.sync ? col::Green : col::Text, bpm, fontHead, fs);
        CenterText(dl, ImVec2(c.x - lr, c.y + lr * 0.05f), ImVec2(c.x + lr, c.y + lr * 0.5f), touching ? acc : std::fabs(dk.pitch) > 1e-5f ? col::Amber : col::TextDim, pt,
                   fontHead, fs * 0.72f);
    } else {
        CenterText(dl, ImVec2(c.x - lr, c.y - lr), ImVec2(c.x + lr, c.y + lr), col::withAlpha(acc, 200), d == 0 ? "A" : "B", fontBold, lr * 0.8f);
    }

    if (!t) return;
    ImGuiIO& io = ImGui::GetIO();
    const float mouseAng = std::atan2(io.MousePos.y - c.y, io.MousePos.x - c.x);
    const float mouseDist = std::hypot(io.MousePos.x - c.x, io.MousePos.y - c.y);
    const bool onRing = mouseDist > r * 0.72f;
    if (ImGui::IsItemActivated()) {
        dk.platterAngle = mouseAng;
        if (playing && onRing) {
            dk.platterNudge = true;   // aro: empujar el tempo (cuadrar a mano), como el borde de un plato
        } else {
            p.scratchVelocity = 0.f; p.scratching = true; dk.scratching = true;
        }
    } else if (ImGui::IsItemActive() && dk.platterNudge) {
        float da = mouseAng - dk.platterAngle;
        if (da > IM_PI) da -= 2 * IM_PI;
        if (da < -IM_PI) da += 2 * IM_PI;
        dk.platterAngle = mouseAng;
        // Con el ratón los giros son más bruscos que con la mano: la mitad de sensibilidad que un jog.
        dk.nudgeAcc += da / (2 * IM_PI) * kJogBendPerRevSec * 0.5f * settings_.mouseScratchSensitivity;
    } else if (ImGui::IsItemDeactivated() && dk.platterNudge) {
        dk.platterNudge = false;
    } else if (ImGui::IsItemActive() && dk.scratching) {
        float da = mouseAng - dk.platterAngle;
        if (da > IM_PI) da -= 2 * IM_PI;
        if (da < -IM_PI) da += 2 * IM_PI;
        dk.platterAngle = mouseAng;
        const float sec = da / (2 * IM_PI) * kVinylSecPerRev;
        p.scratchVelocity = io.DeltaTime > 0 ? std::clamp(sec * settings_.mouseScratchSensitivity / io.DeltaTime, -8.f, 8.f) : 0.f;
    } else if (ImGui::IsItemDeactivated()) {
        p.scratching = false; p.scratchVelocity = 0.f; dk.scratching = false;
    }
    if (hov && !dk.scratching && !dk.platterNudge)
        ImGui::SetTooltip(playing && onRing ? "Aro: gíralo para adelantar o atrasar un poco la canción (cuadrar a mano)"
                                            : "Centro: gira el plato con el ratón para hacer scratch (al soltar sigue como estaba)");
}

// =================================================================== plato

void App::drawSyncMenu(int d) {
    const int o = 1 - d;
    const bool both = deck_[d].track && deck_[o].track && deck_[d].meta.bpm > 0 && deck_[o].meta.bpm > 0;
    const bool anySync = deck_[0].sync || deck_[1].sync;
    if (ImGui::MenuItem("Este plato marca el tempo (MASTER)", nullptr, anySync && syncMaster_ == d, both)) setSyncMaster(d);
    ImGui::SetItemTooltip("Pone los dos platos en SYNC y el otro sigue a este. También vale para cambiar de MASTER.");
    if (ImGui::MenuItem("Realinear la fase ahora", nullptr, false, anySync && both)) syncRealign(d);
    ImGui::SetItemTooltip("Vuelve a poner los golpes a la vez (quita el desfase que hayas dado empujando el plato).");
    ImGui::Separator();
    if (ImGui::MenuItem("Tempo y fase (golpes a la vez)", nullptr, settings_.syncPhase)) { settings_.syncPhase = true; if (anySync) syncRealign(d); settings_.save(); }
    if (ImGui::MenuItem("Solo tempo (la fase la pones tú)", nullptr, !settings_.syncPhase)) { settings_.syncPhase = false; settings_.save(); }
    ImGui::SetItemTooltip("Iguala el BPM pero no mueve la posición: como mezclar a mano con el tempo ya igualado.");
    if (ImGui::MenuItem("MASTER automático", nullptr, settings_.syncAutoMaster)) { settings_.syncAutoMaster = !settings_.syncAutoMaster; settings_.save(); }
    ImGui::SetItemTooltip("Con los dos platos en SYNC, si el MASTER se para o termina, manda el que sigue sonando (sin cambiar su tempo).");
    if (ImGui::MenuItem("Alinear compases completos (no solo pulsos)", nullptr, settings_.syncBar)) {
        settings_.syncBar = !settings_.syncBar;
        for (auto& dp : engine_.deck) dp.syncBar = settings_.syncBar;
        if (anySync) syncRealign(d);
        settings_.save();
    }
    ImGui::SetItemTooltip("Al sincronizar coinciden los primeros tiempos de compás de las dos pistas.\nNecesita que la rejilla de ambas esté bien puesta (pestaña REJILLA).");
    ImGui::Separator();
    if (ImGui::MenuItem("Usar las rejillas de VirtualDJ", nullptr, settings_.useVdjGrid)) { settings_.useVdjGrid = !settings_.useVdjGrid; settings_.save(); }
    ImGui::SetItemTooltip("Si VirtualDJ tiene la canción analizada, al cargarla se usan su BPM y su rejilla\n"
                          "(salvo las que hayas ajustado a mano aquí). Afecta a las canciones que cargues a partir de ahora.");
}

void App::drawDeckMenu(int d) {
    DeckUi& dk = deck_[d];
    DeckParams& p = engine_.deck[d];
    const bool has = dk.track != nullptr;
    const bool kl = p.keyLock.load();
    if (ImGui::MenuItem("Key lock (tempo sin cambiar el tono)", nullptr, kl)) p.keyLock = !kl;
    bool q = p.quantize.load();
    if (ImGui::MenuItem("Cuantizar cues y loops al pulso (Q)", nullptr, q)) { q = !q; for (auto& dp : engine_.deck) dp.quantize = q; settings_.quantize = q; }
    if (ImGui::BeginMenu("Rango del pitch")) {
        for (float r : {8.f, 16.f, 50.f}) {
            char l[32];
            std::snprintf(l, sizeof l, "±%d %%", (int)r);
            if (ImGui::MenuItem(l, nullptr, std::fabs(settings_.pitchRange[d] - r) < 0.5f)) {
                settings_.pitchRange[d] = r;
                dk.pitch = std::clamp(dk.pitch, -r / 100.f, r / 100.f);
                p.pitch = dk.pitch;
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Pitch a 0", nullptr, false, std::fabs(dk.pitch) > 1e-6f)) { dk.pitch = 0; p.pitch = 0; if (p.syncTo.load() >= 0) dk.sync = false; }
    if (ImGui::BeginMenu("SYNC")) { drawSyncMenu(d); ImGui::EndMenu(); }
    if (ImGui::MenuItem("Igualar el tono con el otro plato", nullptr, false, has && dk.meta.key >= 0 && soundingKey(1 - d) >= 0)) keyMatch(d);
    ImGui::SetItemTooltip("Transpone este plato lo mínimo para que su tonalidad encaje con la que suena en el otro (activa key lock).");
    ImGui::Separator();
    if (ImGui::MenuItem(isFavorite(dk.path) ? "Quitar de favoritos" : "Añadir a favoritos", nullptr, false, has)) toggleFavorite(dk.path);
    if (ImGui::MenuItem("Añadir a la Sidelist", nullptr, false, has)) addToSidelist({dk.path});
    if (ImGui::MenuItem("Volver a analizar BPM y rejilla", nullptr, false, has && dk.reanalyzing.empty())) reanalyze(d);
    if (ImGui::MenuItem("Mostrar en el Explorador", nullptr, false, has)) win::revealInExplorer(dk.path);
    ImGui::Separator();
    if (ImGui::MenuItem("Expulsar la pista", nullptr, false, has)) ejectDeck(d);
}

void App::drawDeck(int d, ImVec2 a, ImVec2 b) {
    const float s = S();
    deckRect_[d][0] = a; deckRect_[d][1] = b;
    DeckUi& dk = deck_[d];
    DeckParams& p = engine_.deck[d];
    const DeckStatus& st = engine_.deckStatus[d];
    const ImU32 acc = col::deck(d);
    const bool playing = st.playing.load();
    Panel(ImGui::GetWindowDrawList(), a, b, 10 * s, col::Panel, playing ? col::mix(col::Line, acc, 0.35f) : col::Line);
    {
        // Franja del color del plato arriba (más viva mientras suena).
        const float r = std::min(10.f * s, col::Radius * s);
        const bool ending = endWarning(d);
        const float blink = 0.5f + 0.5f * std::cos((float)now_ * 6.2832f * 1.4f);
        const ImU32 stripe = ending ? col::mix(col::Red, col::withAlpha(acc, 230), 0.6f * (1.f - blink)) : col::withAlpha(acc, playing ? 230 : 90);
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, a.y + (ending ? 4 : 3) * s), stripe, r, ImDrawFlags_RoundCornersTop);
    }

    char cid[16];
    std::snprintf(cid, sizeof cid, "plato%d", d);
    BeginPanel(cid, a, b, 12 * s);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (ImGui::BeginDragDropTargetCustom(ImRect(a, b), ImGui::GetID("drop"))) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("RDJ_PISTA")) {
            auto paths = uic::PayloadPaths(pl);
            if (!paths.empty()) loadToDeck(d, paths[0]);
        }
        ImGui::EndDragDropTarget();
    }
    const ImVec2 ca = ImGui::GetCursorScreenPos();
    const float cw = ImGui::GetContentRegionAvail().x, ch = ImGui::GetContentRegionAvail().y;
    const float pw = 32 * s;  // columna de pitch (lado exterior)
    const float cx0 = d == 0 ? ca.x + pw + 10 * s : ca.x;
    const float cx1 = d == 0 ? ca.x + cw : ca.x + cw - pw - 10 * s;
    const float pitchX = d == 0 ? ca.x : ca.x + cw - pw;
    const float W = cx1 - cx0;
    const bool has = dk.track != nullptr;
    const float sp = 6 * s;

    // ---- Cabecera: carátula, título, artista, BPM, tonalidad, tiempos + plato a la derecha
    const bool narrowDeck = W < 470 * s;  // ventana pequeña: carátula, BPM y botones más compactos
    const float headH = std::clamp(ch * 0.30f, 74 * s, 96 * s);
    const float ovH = std::clamp(ch * 0.15f, 28 * s, 48 * s);
    const float platR = std::min((headH + 8 * s + ovH) * 0.5f, W * (narrowDeck ? 0.15f : 0.16f));
    const ImVec2 platC(cx1 - platR, ca.y + platR);
    drawPlatter(d, platC, platR);
    const float ix0 = cx0, ix1 = cx1 - 2 * platR - 14 * s;
    const float art = narrowDeck ? std::min(headH - 6 * s, 58 * s) : headH - 6 * s;
    {
        const ImVec2 ap(ix0, ca.y), aq(ix0 + art, ca.y + art);
        if (has) DrawCover(dl, dk.path, ap, aq, 6 * s, dk.title.c_str());
        else {
            dl->AddRectFilled(ap, aq, col::Panel2, 6 * s);
            DrawIcon(dl, Ic::Disc, ImVec2((ap.x + aq.x) * 0.5f, (ap.y + aq.y) * 0.5f), art * 0.5f, col::Line);
        }
        // Insignia del plato sobre la esquina de la carátula
        const float bs = 22 * s;
        dl->AddRectFilled(ImVec2(ap.x - 1, ap.y - 1), ImVec2(ap.x + bs, ap.y + bs), acc, 5 * s, ImDrawFlags_RoundCornersTopLeft | ImDrawFlags_RoundCornersBottomRight);
        CenterText(dl, ap, ImVec2(ap.x + bs, ap.y + bs), col::onColor(acc), d ? "B" : "A", fontBold, 15 * s);
    }
    const float tx = ix0 + art + 12 * s;
    {
        // BPM (arriba a la derecha de la zona de información)
        char bpm[32];
        if (has && dk.meta.bpm > 0) std::snprintf(bpm, sizeof bpm, "%.2f", engine_.effectiveBpm(d));
        else std::snprintf(bpm, sizeof bpm, "--.--");
        const float bfs = narrowDeck ? 24 * s : 30 * s;
        const float bw = fontBold->CalcTextSizeA(bfs, FLT_MAX, 0, bpm).x;
        const float unitW = narrowDeck ? 0.f : fontHead->CalcTextSizeA(12 * s, FLT_MAX, 0, "BPM").x;
        const float bx = ix1 - unitW - (narrowDeck ? 0.f : 4 * s) - bw;
        dl->AddText(fontBold, bfs, ImVec2(bx, ca.y - 4 * s), dk.sync ? col::Green : col::Text, bpm);
        if (!narrowDeck) dl->AddText(fontHead, 12 * s, ImVec2(ix1 - unitW, ca.y + 12 * s), col::TextDim, "BPM");
        // Título + favorito
        const float tfs = 20 * s;
        const float heartW = has ? 22 * s : 0;
        const std::string title = has ? dk.title : (loader_.busy(d) ? "Cargando…" : "Sin pista");
        const std::string tt = ellipsize(title, bx - tx - heartW - 8 * s, fontHead, tfs);
        dl->AddText(fontHead, tfs, ImVec2(tx, ca.y - 1 * s), has ? col::Text : col::TextDim, tt.c_str());
        if (has) {
            const float hx = tx + fontHead->CalcTextSizeA(tfs, FLT_MAX, 0, tt.c_str()).x + 6 * s;
            ImGui::SetCursorScreenPos(ImVec2(hx, ca.y + 1 * s));
            const bool fav = isFavorite(dk.path);
            ImGui::PushID("fav");
            if (ImGui::InvisibleButton("##fav", ImVec2(18 * s, 18 * s))) toggleFavorite(dk.path);
            ImGui::PopID();
            const bool hh = ImGui::IsItemHovered();
            DrawIcon(dl, fav ? Ic::HeartFill : Ic::Heart, ImVec2(hx + 9 * s, ca.y + 10 * s), 16 * s, fav ? col::DeckB : (hh ? col::Text : col::TextDim));
            ImGui::SetItemTooltip(fav ? "Quitar de favoritos" : "Añadir a favoritos");
        }
        // Artista y tonalidad
        if (has) {
            ImFont* f = ImGui::GetFont();
            const float kh = 20 * s;
            float kw = 0;
            if (dk.meta.key >= 0) {
                // Tonalidad que suena: la de la pista + transposición (+ cambio por el pitch sin key lock).
                const int k = soundingKey(d);
                const int ok = soundingKey(1 - d);
                const bool compat = ok >= 0 && keysCompatible(k, ok);
                kw = drawKeyBadges(nullptr, ImVec2(0, 0), kh, k);  // medir
                const ImVec2 kp(ix1 - kw, ca.y + 34 * s);
                drawKeyBadges(dl, kp, kh, k, compat);
                ImGui::SetCursorScreenPos(kp);
                if (ImGui::InvisibleButton("##tonoplato", ImVec2(kw, kh))) setKeyFilter(keyFilter_ == k ? -1 : k);
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("%s · %s (%s)%s", keyCode(k).c_str(), keyNameEs(k), keyName(k), k != dk.meta.key ? "  ·  con el cambio de tono" : "");
                    if (dk.meta.keySrc) ImGui::TextDisabled("Tonalidad %s.", keySourceText(dk.meta.keySrc));
                    if (ok >= 0) {
                        const KeyRelation rel = keyRelation(ok, k);
                        ImGui::TextColored(toVec4(rel.level == 0 ? col::Green : rel.level == 3 ? col::Red : col::Amber), "Con el plato %s (%s): %s", d ? "A" : "B", keyCode(ok).c_str(), rel.name);
                    }
                    ImGui::TextDisabled("Clic: ver en la lista las pistas que encajan con %s", keyCode(k).c_str());
                    ImGui::EndTooltip();
                }
            }
            std::string artLine = dk.artist.empty() ? "Artista desconocido" : dk.artist;
            {
                std::lock_guard<std::recursive_mutex> lk(library_.mutex());
                if (const LibTrack* lt = library_.find(dk.path)) {
                    if (!lt->genre.empty()) artLine += "  ·  " + lt->genre;
                }
            }
            dl->AddText(f, 15 * s, ImVec2(tx, ca.y + 25 * s), col::TextDim,
                        ellipsize(artLine, ix1 - tx - kw - 10 * s, f, 15 * s).c_str());
        }
        // Tiempos y estado
        const float ty = ca.y + 58 * s;
        if (has) {
            const std::string el = formatTime(deckPosSec(d));
            dl->AddText(fontHead, 20 * s, ImVec2(tx, ty), col::Text, el.c_str());
            const float ew = fontHead->CalcTextSizeA(20 * s, FLT_MAX, 0, el.c_str()).x;
            const double remSec = std::max(0.0, deckLenSec(d) - deckPosSec(d));
            const std::string rem = "-" + formatTime(remSec);
            // Aviso de final: el tiempo restante parpadea en rojo en los últimos segundos.
            const bool ending = endWarning(d);
            const float blink = 0.5f + 0.5f * std::cos((float)now_ * 6.2832f * 1.4f);
            const ImU32 remCol = ending ? col::mix(col::Red, col::Text, 0.35f * (1.f - blink)) : col::TextDim;
            dl->AddText(fontHead, 16 * s, ImVec2(tx + ew + 10 * s, ty + 3 * s), remCol, rem.c_str());
            float bx0 = tx + ew + 10 * s + fontHead->CalcTextSizeA(16 * s, FLT_MAX, 0, rem.c_str()).x + 12 * s;
            // Chips de estado a la derecha. El primero (0.11.3): el botón de efectos del plato.
            float chx = drawDeckFxButton(d, ix1, ty + 3 * s, 18 * s, std::max(40 * s, ix1 - bx0 - 4 * s), ix1 - bx0 - 116 * s);
            // Chip rápido "🎛 STEMS" en la cabecera del plato
            if (chx - bx0 > 140 * s) {
                const bool stemChanged = p.stemMute[0].load() || p.stemMute[1].load() || p.stemMute[2].load() || p.stemMute[3].load();
                const char* stChipTxt = "🎛 STEMS";
                const float stw = BadgeWidth(18 * s, stChipTxt, fontHead, 11.5f * s);
                chx -= stw;
                const ImVec2 stp(chx, ty + 3 * s);
                Badge(dl, stp, 18 * s, stChipTxt, stemChanged ? col::Amber : col::Violet, stemChanged || padMode_[d] == 8, fontHead, 11.5f * s);
                ImGui::SetCursorScreenPos(stp);
                char stbid[24]; std::snprintf(stbid, sizeof stbid, "##stchip%d", d);
                if (ImGui::InvisibleButton(stbid, ImVec2(stw, 18 * s))) padMode_[d] = (padMode_[d] == 8 ? 0 : 8);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Abrir controles avanzados de STEMS en tiempo real (Voz, Batería, Bajo, Instrumentos)");
                chx -= 4 * s;
            }
            // Botón compacto "💡 Siguiente" (recomendador armónico instantáneo para este plato)
            if (has && chx - bx0 > 74 * s) {
                const char* recTxt = "💡 Siguiente";
                const float rw = BadgeWidth(18 * s, recTxt, fontHead, 11.5f * s);
                chx -= rw;
                const ImVec2 rp(chx, ty + 3 * s);
                Badge(dl, rp, 18 * s, recTxt, col::DeckA, false, fontHead, 11.5f * s);
                ImGui::SetCursorScreenPos(rp);
                char rbid[24]; std::snprintf(rbid, sizeof rbid, "##nextrec%d", d);
                if (ImGui::InvisibleButton(rbid, ImVec2(rw, 18 * s))) ImGui::OpenPopup("nextrec_popup");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ver las 8 mejores canciones compatibles en clave, BPM y estilo para mezclar con este plato");
                chx -= 4 * s;
            }
            if (ImGui::BeginPopup("nextrec_popup")) {
                ImGui::TextColored(toVec4(acc), "💡 SIGUIENTES RECOMENDADAS PARA PLATO %s (%s · %.1f BPM)",
                                   d ? "B" : "A", dk.meta.key >= 0 ? keyCamelot(dk.meta.key) : "—", engine_.effectiveBpm(d));
                ImGui::Separator();
                std::lock_guard<std::recursive_mutex> lk(library_.mutex());
                struct QuickRec { const LibTrack* t; int sc; std::string why; };
                std::vector<QuickRec> recs;
                const double refBpm = dk.meta.bpm > 0 ? engine_.effectiveBpm(d) : 126.0;
                const int refKey = soundingKey(d);
                for (const auto& cand : library_.tracks()) {
                    if (cand.missing || cand.path == dk.path || cand.path == deck_[1 - d].path || cand.meta.bpm <= 0) continue;
                    double r = cand.meta.bpm / refBpm;
                    if (r > 1.5) r /= 2.0; else if (r < 0.75) r *= 2.0;
                    double dPct = std::fabs(r - 1.0) * 100.0;
                    if (dPct > 6.5) continue;
                    int hPts = 50; const char* hWhy = "Libre";
                    if (refKey >= 0 && cand.meta.key >= 0) {
                        const int na = keyCamelotNumber(refKey), nb = keyCamelotNumber(cand.meta.key);
                        const bool ma = keyIsMajor(refKey), mb = keyIsMajor(cand.meta.key);
                        int df = (nb - na) % 12; if (df > 6) df -= 12; if (df < -6) df += 12;
                        if (ma == mb && df == 0) { hPts = 100; hWhy = "Misma clave"; }
                        else if (ma == mb && std::abs(df) == 1) { hPts = 92; hWhy = "Vecina ±1"; }
                        else if (ma != mb && df == 0) { hPts = 88; hWhy = "Relativa M/m"; }
                        else if (ma == mb && (df == 2 || df == -5)) { hPts = 75; hWhy = "Subida energía"; }
                        else continue;
                    }
                    int sc = std::clamp((int)std::lround(hPts * 0.68 + 25.0 * (1.0 - dPct / 7.0) + (!cand.genre.empty() ? 7.0 : 3.0)), 0, 100);
                    recs.push_back({&cand, sc, std::string(hWhy)});
                }
                std::sort(recs.begin(), recs.end(), [](const QuickRec& a, const QuickRec& b) { return a.sc > b.sc; });
                if (recs.empty()) {
                    ImGui::TextDisabled("No hay candidatas cercanas analizadas en la biblioteca.");
                } else {
                    for (size_t ri = 0; ri < std::min<size_t>(8, recs.size()); ++ri) {
                        const auto& qr = recs[ri];
                        ImGui::PushID((int)ri);
                        char line[256];
                        std::snprintf(line, sizeof line, "[%d pts] %s — %s  (%s · %.1f BPM · %s)",
                                      qr.sc, qr.t->artist.empty() ? "Artista" : qr.t->artist.c_str(), qr.t->title.c_str(),
                                      qr.t->meta.key >= 0 ? keyCamelot(qr.t->meta.key) : "—", qr.t->meta.bpm,
                                      qr.t->genre.empty() ? "—" : qr.t->genre.c_str());
                        if (ImGui::Button(d == 0 ? "→ Plato B" : "→ Plato A")) {
                            loadToDeck(1 - d, qr.t->path);
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Mix Lab")) {
                            mode_ = 3; showElverKey_ = false;
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::SameLine();
                        ImGui::TextUnformatted(line);
                        ImGui::PopID();
                    }
                }
                ImGui::EndPopup();
            }
            auto chip = [&](const char* txt, ImU32 c) {
                const float w = BadgeWidth(18 * s, txt, fontHead, 12 * s);
                chx -= w;
                Badge(dl, ImVec2(chx, ty + 3 * s), 18 * s, txt, c, true, fontHead, 12 * s);
                chx -= 4 * s;
            };
            if (st.loopActive.load() && dk.meta.bpm > 0) {
                const double beats = (st.loopOut.load() - st.loopIn.load()) / (60.0 / dk.meta.bpm * dk.track->sampleRate);
                char lb[24];
                std::snprintf(lb, sizeof lb, "LOOP %.3g", beats);
                chip(lb, col::Green);
            }
            if (p.keyLock.load()) chip("KEY", col::Amber);
            if (std::fabs(p.keyShift.load()) > 0.01f) { char k2[16]; std::snprintf(k2, sizeof k2, "%+d st", (int)std::lround(p.keyShift.load())); chip(k2, col::Amber); }
            if ((deck_[0].sync || deck_[1].sync) && syncMaster_ == d) chip("MASTER", col::Green);
            else if (dk.sync) {
                const double k = engine_.deck[d].syncFactor.load();
                const char* lb = !settings_.syncPhase ? "SYNC BPM" : k > 1.5 ? "SYNC ×2" : k < 0.75 ? "SYNC ×½" : "SYNC";
                chip(lb, engine_.deckStatus[d].syncLocked.load() || !engine_.deckStatus[d].playing.load() || !settings_.syncPhase ? col::Green : col::Amber);
            }
            if (dk.syncBadSince > 0 && now_ - dk.syncBadSince > 4.0) chip("¿REJILLA?", col::Amber);
            // Subidón cercano (vuelve el bombo tras un corte de 4+ compases).
            if (dk.meta.bpm > 0 && settings_.waveStructure) {
                const TrackShape& sh = trackShape(d);
                const double bar = 4 * 60.0 / dk.meta.bpm;
                const double barPos = (deckPosSec(d) - dk.meta.gridAnchorSec) / bar;
                for (int dropBar : sh.drops) {
                    const double left = dropBar - barPos;
                    if (left <= 0) continue;
                    if (left <= 16) {
                        char lb[32];
                        const int n = (int)std::ceil(left - 1e-6);
                        if (n <= 1) std::snprintf(lb, sizeof lb, "SUBIDÓN YA");
                        else std::snprintf(lb, sizeof lb, "SUBIDÓN EN %d", n);
                        chip(lb, n <= 4 ? col::mix(col::Amber, col::Red, 0.4f) : col::Amber);
                    }
                    break;
                }
            }
            // Compás.pulso y posición dentro de la frase de 8 compases.
            if (dk.meta.bpm > 0 && chx - bx0 > 110 * s) {
                const double beat = 60.0 / dk.meta.bpm;
                const double bpos = (deckPosSec(d) - dk.meta.gridAnchorSec) / beat;
                const long long bi = (long long)std::floor(bpos + 1e-6);
                const long long barI = bi >= 0 ? bi / 4 : (bi - 3) / 4;
                const int inBar = (int)(bi - barI * 4);
                const TrackShape& sh = trackShape(d);
                const int phase = sh.drops.empty() ? 0 : ((sh.drops[0] % 8) + 8) % 8;   // las frases suelen empezar en un subidón
                const int inPhrase = (int)(((barI - phase) % 8 + 8) % 8);
                char bb[24];
                if (bi >= 0) std::snprintf(bb, sizeof bb, "%lld.%d", barI + 1, inBar + 1);
                else std::snprintf(bb, sizeof bb, "0.%d", inBar + 1);
                const float bfz = 16 * s;
                dl->AddText(fontHead, bfz, ImVec2(bx0, ty + 3 * s), col::Text, bb);
                float px = bx0 + std::max(fontHead->CalcTextSizeA(bfz, FLT_MAX, 0, "000.0").x, fontHead->CalcTextSizeA(bfz, FLT_MAX, 0, bb).x) + 6 * s;
                const float py = ty + 6 * s, ph = 12 * s;
                // 4 pulsos del compás (el primero más grande)
                for (int k = 0; k < 4; ++k) {
                    const float w0 = k == 0 ? 6 * s : 4 * s;
                    const ImU32 c = k == inBar ? acc : k < inBar ? col::withAlpha(acc, 90) : col::Line;
                    dl->AddRectFilled(ImVec2(px, py + (ph - w0) * 0.5f), ImVec2(px + w0, py + (ph + w0) * 0.5f), c, 1.5f * s);
                    px += w0 + 3 * s;
                }
                px += 6 * s;
                if (chx - px > 8 * 6 * s) {
                    // Frase: 8 compases; el último de cada bloque de 4 se marca (ahí suelen entrar los cambios).
                    for (int k = 0; k < 8; ++k) {
                        const float sw = 5 * s;
                        const ImU32 c = k == inPhrase ? col::Text : k < inPhrase ? col::withAlpha(col::Text, 110) : col::withAlpha(col::Line, 255);
                        dl->AddRectFilled(ImVec2(px, py + 1 * s), ImVec2(px + sw, py + ph - 1 * s), c, 1 * s);
                        px += sw + (k == 3 ? 5 * s : 2 * s);
                    }
                }
                ImGui::SetCursorScreenPos(ImVec2(bx0, ty + 2 * s));
                ImGui::InvisibleButton("##compas", ImVec2(std::max(1.f, px - bx0), 18 * s));
                ImGui::SetItemTooltip("Compás %lld, pulso %d · compás %d de la frase de 8%s", bi >= 0 ? barI + 1 : 0LL, inBar + 1, inPhrase + 1,
                                      sh.drops.empty() ? "" : "\n(las frases se cuentan desde el primer subidón)");
            }
        }
        if (loader_.busy(d)) {
            const float pr = loader_.progress(d);
            dl->AddRectFilled(ImVec2(tx, ca.y + 50 * s), ImVec2(ix1, ca.y + 53 * s), col::Line, 1.5f * s);
            dl->AddRectFilled(ImVec2(tx, ca.y + 50 * s), ImVec2(tx + (ix1 - tx) * pr, ca.y + 53 * s), acc, 1.5f * s);
        } else if (!dk.error.empty() && now_ - dk.errorTime < 10.0) {
            const ImVec2 ea(tx - 4 * s, ca.y + 22 * s), eb(ix1, ca.y + 82 * s);
            dl->AddRectFilled(ea, eb, col::withAlpha(col::mix(col::Panel, col::Red, 0.25f), 245), 6 * s);
            dl->AddText(ImVec2(ea.x + 8 * s, ea.y + 6 * s), col::Red, "No se pudo cargar:");
            dl->AddText(ImVec2(ea.x + 8 * s, ea.y + 28 * s), col::Text, ellipsize(dk.error, eb.x - ea.x - 16 * s, ImGui::GetFont(), ImGui::GetFontSize()).c_str());
        }
    }
    float y = ca.y + headH + 8 * s;
    drawOverview(d, ImVec2(ix0, y), ImVec2(ix1, y + ovH));
    y = std::max(y + ovH, platC.y + platR) + 10 * s;

    // ---- Transporte: CUE · PLAY · SYNC · ▾ · hot cues 1-4 · ⋯
    const float bh = std::clamp(ch * 0.15f, 34 * s, 44 * s);
    {
        const float w3 = 32 * s;
        float wCue = W * 0.125f, wPlay = W * 0.16f, wSync = W * 0.125f;
        const float wHot = (W - wCue - wPlay - wSync - 2 * w3 - 9 * sp - 8 * s) / 4.f;
        const bool showHot = wHot >= 34 * s;  // sin sitio: los hot cues quedan en la pestaña CUES
        if (!showHot) { wCue = wSync = (W - 2 * w3 - 4 * sp) * 0.28f; wPlay = W - 2 * w3 - 4 * sp - 2 * wCue; }
        ImGui::SetCursorScreenPos(ImVec2(cx0, y));
        // CUE encendido: parado justo en el punto cue (como en los reproductores de cabina).
        const bool atCue = has && !playing && std::fabs(st.posFrames.load() - st.cueFrames.load()) < 0.01 * dk.track->sampleRate;
        NeonButton("CUE##cue", col::Amber, atCue, ImVec2(wCue, bh), has, fontHead, 17 * s);
        if (ImGui::IsItemActivated()) send(d, Cmd::CueDown);
        if (ImGui::IsItemDeactivated()) send(d, Cmd::CueUp);
        ImGui::SetItemTooltip("En pausa fija el punto cue; mantenlo pulsado para escuchar desde él. Sonando, vuelve al cue y para.");
        ImGui::SameLine(0, sp);
        {
            const ImVec2 pp = ImGui::GetCursorScreenPos();
            if (NeonButton("##play", acc, true, ImVec2(wPlay, bh), has)) togglePlay(d);
            DrawIcon(dl, playing ? Ic::Pause : Ic::Play, ImVec2(pp.x + wPlay * 0.5f, pp.y + bh * 0.5f), bh * 0.5f, has ? col::onColor(acc) : col::withAlpha(col::onColor(acc), 120));
            if (has && !playing) {
                // En pausa, el PLAY respira despacio: se ve de un vistazo qué plato está parado.
                const float pulse = 0.5f + 0.5f * std::sin((float)now_ * 3.1416f * 1.2f);
                dl->AddRect(ImVec2(pp.x - 1.5f * s, pp.y - 1.5f * s), ImVec2(pp.x + wPlay + 1.5f * s, pp.y + bh + 1.5f * s), col::withAlpha(acc, (int)(60 + 140 * pulse)),
                            col::BtnRadius * s + 1.5f * s, 0, 1.5f * s);
            }
            ImGui::SetItemTooltip("Reproducir / pausa");
        }
        ImGui::SameLine(0, sp);
        {
            const ImVec2 pp = ImGui::GetCursorScreenPos();
            if (NeonButton("SYNC##sync", col::Green, dk.sync, ImVec2(wSync, bh), has, fontHead, 17 * s)) setSync(d, !dk.sync);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("menusync");
            ImGui::SetItemTooltip("%s\nLa barrita de abajo es la fase: centrada = golpes a la vez.\nClic derecho: MASTER, realinear y modo (tempo y fase / solo tempo).",
                                  syncMaster_ == d && (deck_[0].sync || deck_[1].sync) ? "MASTER: este plato marca el tempo; el otro le sigue." :
                                  dk.sync ? "SYNC activo: este plato sigue el tempo y la fase del MASTER (también sus empujones)." :
                                            "Iguala el tempo y alinea los pulsos con el otro plato (acepta tempo doble o mitad).");
            if (ImGui::BeginPopup("menusync")) { drawSyncMenu(d); ImGui::EndPopup(); }
            if (has && deck_[1 - d].track) drawPhaseMeter(d, ImVec2(pp.x + 7 * s, pp.y + bh - 8 * s), ImVec2(pp.x + wSync - 7 * s, pp.y + bh - 4 * s));
        }
        ImGui::SameLine(0, sp);
        {
            const ImVec2 pp = ImGui::GetCursorScreenPos();
            if (NeonButton("##syncmenu", acc, false, ImVec2(w3, bh))) ImGui::OpenPopup("syncmenu");
            DrawIcon(dl, Ic::ChevDown, ImVec2(pp.x + w3 * 0.5f, pp.y + bh * 0.5f), 14 * s, col::Text);
            ImGui::SetItemTooltip("Key lock, cuantizar y rango del pitch");
            if (ImGui::BeginPopup("syncmenu")) { drawDeckMenu(d); ImGui::EndPopup(); }
        }
        if (showHot) ImGui::SameLine(0, sp + 8 * s);
        for (int i = 0; i < 4 && showHot; ++i) {
            if (i) ImGui::SameLine(0, sp);
            const double h = st.hotCues[i].load();
            char lbl[32];
            std::snprintf(lbl, sizeof lbl, "%d##hc%d", i + 1, i);
            const ImVec2 pp = ImGui::GetCursorScreenPos();
            if (NeonButton(lbl, kHotColors[i], false, ImVec2(wHot, bh), has, fontHead, 17 * s)) send(d, Cmd::HotCue, 0, i);
            if (h >= 0) dl->AddRect(pp, ImVec2(pp.x + wHot, pp.y + bh), kHotColors[i], 6 * s, 0, 1.6f * s);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && h >= 0) { hcMenuCue_[d] = i; ImGui::OpenPopup("hotcuemenu"); }
            if (h >= 0 && !dk.meta.hotCueNames[i].empty() && wHot > 46 * s)
                dl->AddText(ImGui::GetFont(), 10.5f * s, ImVec2(pp.x + 5 * s, pp.y + bh - 14 * s), col::withAlpha(kHotColors[i], 230),
                            ellipsize(dk.meta.hotCueNames[i], wHot - 10 * s, ImGui::GetFont(), 10.5f * s).c_str());
            ImGui::SetItemTooltip(h >= 0 ? "Hot cue %d%s%s (%s) · clic: saltar y reproducir · clic derecho: nombre o borrar" : "Hot cue %d%s%s%s · clic: guardarlo aquí", i + 1,
                                  dk.meta.hotCueNames[i].empty() ? "" : " · ", dk.meta.hotCueNames[i].c_str(),
                                  h >= 0 && has ? formatTime(h / dk.track->sampleRate, false).c_str() : "");
        }
        ImGui::SameLine(0, sp);
        {
            const ImVec2 pp = ImGui::GetCursorScreenPos();
            if (NeonButton("##menuplato", acc, false, ImVec2(w3, bh))) ImGui::OpenPopup("menuplato");
            DrawIcon(dl, Ic::Dots, ImVec2(pp.x + w3 * 0.5f, pp.y + bh * 0.5f), 16 * s, col::Text);
            ImGui::SetItemTooltip("Más: favoritos, Sidelist, análisis, expulsar…");
            if (ImGui::BeginPopup("menuplato")) { drawDeckMenu(d); ImGui::EndPopup(); }
        }
    }
    y += bh + 6 * s;

    // ---- BARRA FIJA SIEMPRE VISIBLE DE STEMS EN TIEMPO REAL (🎤 VOZ · 🥁 BATERÍA · 🎸 BAJO · 🎹 INSTRUM. + ACAPELLA / INSTRUM)
    {
        const float stemBarH = 25 * s;
        const char* stLabelsOn[4]  = {"🎤 VOZ", "🥁 BATERÍA", "🎸 BAJO", "🎹 INSTRUM."};
        const char* stLabelsOff[4] = {"🎤 VOZ [OFF]", "🥁 BAT. [OFF]", "🎸 BAJO [OFF]", "🎹 INST. [OFF]"};
        const ImU32 stColors[4] = {col::Violet, col::Amber, col::Red, col::Green};
        const bool isAcapella = !p.stemMute[0].load() && p.stemMute[1].load() && p.stemMute[2].load() && p.stemMute[3].load();
        const bool isInstrum  = p.stemMute[0].load() && !p.stemMute[1].load() && !p.stemMute[2].load() && !p.stemMute[3].load();
        const float tagW = 46 * s;
        const float quickW = 68 * s;
        const float padGap = 4 * s;
        const float availStW = W - tagW - 2 * quickW - 6 * padGap;
        const float btnStW = std::max(44 * s, availStW / 4.f);

        // Etiqueta "STEMS" a la izquierda
        dl->AddRectFilled(ImVec2(cx0, y), ImVec2(cx0 + tagW, y + stemBarH), col::Panel2, 4 * s);
        CenterText(dl, ImVec2(cx0, y), ImVec2(cx0 + tagW, y + stemBarH), col::DeckA, "STEMS", fontHead, 11.5f * s);

        float sx = cx0 + tagW + padGap;
        for (int k = 0; k < 4; ++k) {
            ImGui::SetCursorScreenPos(ImVec2(sx, y));
            ImGui::PushID(k + 880 + d * 10);
            const bool active = !p.stemMute[k].load();
            const ImU32 cStem = active ? stColors[k] : col::Red;
            const ImVec2 bp0 = ImGui::GetCursorScreenPos();
            if (NeonButton(active ? stLabelsOn[k] : stLabelsOff[k], cStem, active, ImVec2(btnStW, stemBarH), true, fontHead, 11.8f * s)) {
                p.stemMute[k] = active;
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("stem_bar_popup");
            // Vúmetro en vivo dentro del botón del Stem
            const float pk = std::clamp(st.stemPeak[k].load() * 1.45f, 0.f, 1.f);
            if (pk > 0.01f && active) {
                dl->AddRectFilled(ImVec2(bp0.x + 3 * s, bp0.y + stemBarH - 3.2f * s),
                                  ImVec2(bp0.x + 3 * s + (btnStW - 6 * s) * pk, bp0.y + stemBarH - 1.2f * s), stColors[k], 1.5f * s);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s (%s · Vol %.0f%% · FX %.0f%%)\nClic izquierdo: activar / silenciar en vivo (0 ms latencia)\nClic derecho: ajustar volumen, filtro/efecto o separar con IA Demucs",
                                  stLabelsOn[k], active ? "ACTIVO" : "SILENCIADO", p.stemVol[k].load() * 100.f, p.stemFx[k].load() * 100.f);
            }
            if (ImGui::BeginPopup("stem_bar_popup")) {
                ImGui::TextColored(toVec4(stColors[k]), "%s · CONTROL EN TIEMPO REAL", stLabelsOn[k]);
                ImGui::Separator();
                bool mNow = !p.stemMute[k].load();
                if (ImGui::Checkbox("Stem activo (sonando)", &mNow)) p.stemMute[k] = !mNow;
                float vPct = p.stemVol[k].load() * 100.f;
                if (ImGui::SliderFloat("Volumen (%)", &vPct, 0.f, 150.f, "%.0f %%")) p.stemVol[k] = vPct / 100.f;
                float fxPct = p.stemFx[k].load() * 100.f;
                if (ImGui::SliderFloat("Efecto / Filtro Stem (%)", &fxPct, 0.f, 100.f, "%.0f %%")) p.stemFx[k] = fxPct / 100.f;
                ImGui::Separator();
                if (ImGui::Button("Restablecer este stem (100%)")) { p.stemMute[k] = false; p.stemVol[k] = 1.f; p.stemFx[k] = 0.f; }
                ImGui::SameLine();
                if (ImGui::Button("Activar los 4 stems")) {
                    for (int j = 0; j < 4; ++j) { p.stemMute[j] = false; p.stemVol[j] = 1.f; p.stemFx[j] = 0.f; }
                }
                if (has && ImGui::Button("⚡ Separar canción con IA profunda (Demucs)…")) {
                    separateFile(dk.path);
                    mode_ = 1;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
            sx += btnStW + padGap;
        }

        // Botón rápido ACAPELLA (solo Voz)
        ImGui::SetCursorScreenPos(ImVec2(sx, y));
        char acaId[24]; std::snprintf(acaId, sizeof acaId, "ACAPELLA##aca%d", d);
        if (NeonButton(acaId, col::Violet, isAcapella, ImVec2(quickW, stemBarH), true, fontHead, 11.f * s)) {
            if (isAcapella) {
                for (int j = 0; j < 4; ++j) p.stemMute[j] = false;
            } else {
                p.stemMute[0] = false; p.stemMute[1] = true; p.stemMute[2] = true; p.stemMute[3] = true;
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Dejar solo la VOZ (Acapella) en vivo · Clic otra vez para reactivar los 4 stems");
        sx += quickW + padGap;

        // Botón rápido INSTRUM (sin Voz / Instrumental)
        ImGui::SetCursorScreenPos(ImVec2(sx, y));
        char insId[24]; std::snprintf(insId, sizeof insId, "INSTRUM##ins%d", d);
        if (NeonButton(insId, col::Green, isInstrum, ImVec2(quickW, stemBarH), true, fontHead, 11.f * s)) {
            if (isInstrum) {
                for (int j = 0; j < 4; ++j) p.stemMute[j] = false;
            } else {
                p.stemMute[0] = true; p.stemMute[1] = false; p.stemMute[2] = false; p.stemMute[3] = false;
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Quitar la VOZ (dejar base instrumental) en vivo · Clic otra vez para reactivar los 4 stems");

        y += stemBarH + 6 * s;
    }

    // Aviso interactivo si al cargar la pista había quedado activado el pitch anterior
    if (dk.hasPendingPitchPrompt && has) {
        const float barH = 24 * s;
        const ImVec2 pa(cx0, y - 4 * s), pb(cx0 + W, y - 4 * s + barH);
        dl->AddRectFilled(pa, pb, col::withAlpha(col::Amber, 42), 5 * s);
        dl->AddRect(pa, pb, col::Amber, 5 * s, 0, 1.2f * s);
        char msgP[96];
        std::snprintf(msgP, sizeof msgP, "Pitch ant. %+.1f%%:", dk.prevPitchVal * 100.f);
        dl->AddText(fontHead, 12 * s, ImVec2(pa.x + 8 * s, pa.y + 4 * s), col::Amber, msgP);
        float bx = pa.x + fontHead->CalcTextSizeA(12 * s, FLT_MAX, 0, msgP).x + 14 * s;
        if (dk.hasMatchedPitch) {
            ImGui::SetCursorScreenPos(ImVec2(bx, pa.y + 2 * s));
            char eqLbl[64];
            std::snprintf(eqLbl, sizeof eqLbl, "Mantener los 2 igualados (%+.1f%%)##eqp%d", dk.matchedPitchVal * 100.f, d);
            const float bw1 = fontHead->CalcTextSizeA(11.5f * s, FLT_MAX, 0, eqLbl).x - 20 * s;
            if (NeonButton(eqLbl, col::Green, std::fabs(dk.pitch - dk.matchedPitchVal) < 0.0005f, ImVec2(bw1, barH - 4 * s), true, fontHead, 11.5f * s)) {
                if (std::fabs(dk.matchedPitchVal) > settings_.pitchRange[d] / 100.f) {
                    for (float rng : {8.f, 16.f, 50.f}) if (std::fabs(dk.matchedPitchVal) <= rng / 100.f) { settings_.pitchRange[d] = rng; break; }
                }
                dk.pitch = dk.matchedPitchVal; p.pitch = dk.pitch;
                setSync(d, true);
                dk.hasPendingPitchPrompt = false;
            }
            bx += bw1 + 6 * s;
        }
        ImGui::SetCursorScreenPos(ImVec2(bx, pa.y + 2 * s));
        char oldLbl[48];
        std::snprintf(oldLbl, sizeof oldLbl, "Usar %+.1f%%##oldp%d", dk.prevPitchVal * 100.f, d);
        const float bw2 = 82 * s;
        if (NeonButton(oldLbl, col::Amber, false, ImVec2(bw2, barH - 4 * s), true, fontHead, 11.5f * s)) {
            dk.pitch = dk.prevPitchVal; p.pitch = dk.pitch;
            dk.hasPendingPitchPrompt = false;
        }
        bx += bw2 + 6 * s;
        ImGui::SetCursorScreenPos(ImVec2(bx, pa.y + 2 * s));
        char zeroLbl[32];
        std::snprintf(zeroLbl, sizeof zeroLbl, "Dejar 0%%%%##zp%d", d);
        if (NeonButton(zeroLbl, acc, std::fabs(dk.pitch) < 0.0005f, ImVec2(68 * s, barH - 4 * s), true, fontHead, 11.5f * s)) {
            dk.pitch = 0.f; p.pitch = 0.f;
            dk.hasPendingPitchPrompt = false;
        }
        y += barH + 2 * s;
    }

    // ---- Pads: cues 1-8, loop, salto, rejilla, tono y efecto
    drawDeckTabs(d, cx0, y, W, ca.y + ch - y);

    // ---- Columna de pitch (lado exterior)
    {
        const float bh2 = 22 * s;
        ImGui::SetCursorScreenPos(ImVec2(pitchX, ca.y));
        ImGui::BeginGroup();
        char rl[24];
        std::snprintf(rl, sizeof rl, "±%d##pr%d", (int)settings_.pitchRange[d], d);
        if (NeonButton(rl, acc, false, ImVec2(pw, bh2), true, fontHead, 13 * s)) {
            const float r = settings_.pitchRange[d];
            settings_.pitchRange[d] = r < 8.5f ? 16.f : r < 16.5f ? 50.f : 8.f;
            dk.rangeBeforeAuto = 0.f;
            const float rr = settings_.pitchRange[d] / 100.f;
            dk.pitch = std::clamp(dk.pitch, -rr, rr);
            p.pitch = dk.pitch;
        }
        ImGui::SetItemTooltip("Rango del pitch (±8, ±16, ±50 %%)");
        const float fh = ch - bh2 * 4 - 4 * ImGui::GetStyle().ItemSpacing.y;
        float pv = dk.pitch;
        const float range = pitchRange(d);
        char fid[16];
        std::snprintf(fid, sizeof fid, "##pitch%d", d);
        const ImVec2 faderPos = ImGui::GetCursorScreenPos();
        if (VFader(fid, &pv, -range, range, 0.f, ImVec2(pw, fh), acc, settings_.pitchInvertUi)) {
            dk.pitch = pv;
            p.pitch = pv;
            dk.hasPendingPitchPrompt = false;
            pickup_[MA_Pitch * 2 + d].engaged = false;
            if (dk.sync && p.syncTo.load() >= 0) dk.sync = false;  // el MASTER puede mover su pitch sin perder el SYNC
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("menupitch");
        // Dibujar signos + y − en los extremos del fader de pitch y marquita ámbar de soft-takeover del mando físico
        {
            const char* topSign = settings_.pitchInvertUi ? "−" : "+";
            const char* botSign = settings_.pitchInvertUi ? "+" : "−";
            dl->AddText(fontHead, 12 * s, ImVec2(faderPos.x + 3 * s, faderPos.y + 1 * s), col::withAlpha(col::TextDim, 190), topSign);
            dl->AddText(fontHead, 12 * s, ImVec2(faderPos.x + 3 * s, faderPos.y + fh - 14 * s), col::withAlpha(col::TextDim, 190), botSign);
            const auto& pk = pickup_[MA_Pitch * 2 + d];
            if (midi_.settings().softTakeover && pk.hasPrev && !pk.engaged) {
                const float tHw = settings_.pitchInvertUi ? pk.prev : (1.f - pk.prev);
                const float yHw = faderPos.y + 8 * s + std::clamp(tHw, 0.f, 1.f) * (fh - 16 * s);
                dl->AddLine(ImVec2(faderPos.x + 2 * s, yHw), ImVec2(faderPos.x + pw - 2 * s, yHw), col::Amber, 2.2f * s);
            }
        }
        ImGui::SetItemTooltip("Pitch %+.2f %% (%s) · doble clic: 0 · Mayús: ajuste fino · clic derecho: opciones de pitch",
                              dk.pitch * 100.f, settings_.pitchInvertUi ? "arriba − más lento, abajo + más rápido" : "arriba + más rápido, abajo − más lento");
        if (ImGui::BeginPopup("menupitch")) {
            ImGui::TextColored(toVec4(acc), "OPCIONES DEL PITCH (PLATO %s)", d ? "B" : "A");
            ImGui::Separator();
            if (ImGui::MenuItem("Arriba = + más rápido, Abajo = − más lento (normal)", nullptr, !settings_.pitchInvertUi)) {
                settings_.pitchInvertUi = false; settings_.save();
            }
            if (ImGui::MenuItem("Arriba = − más lento, Abajo = + más rápido (estilo tocadiscos)", nullptr, settings_.pitchInvertUi)) {
                settings_.pitchInvertUi = true; settings_.save();
            }
            if (ImGui::MenuItem("Invertir dirección del fader físico del mando MIDI", nullptr, midi_.settings().pitchInvert)) {
                midi_.settings().pitchInvert = !midi_.settings().pitchInvert;
                midi_.save();
            }
            if (ImGui::MenuItem("Evitar saltos bruscos del mando físico (Soft-Takeover)", nullptr, midi_.settings().softTakeover)) {
                midi_.settings().softTakeover = !midi_.settings().softTakeover;
                midi_.save();
            }
            ImGui::Separator();
            ImGui::TextDisabled("Al cargar canción si quedó pitch activo:");
            if (ImGui::MenuItem("Mantener siempre los 2 platos igualados y avisar", nullptr, settings_.pitchOnLoadMode == 0)) {
                settings_.pitchOnLoadMode = 0; settings_.save();
            }
            if (ImGui::MenuItem("Poner a 0 % y avisar por si quieres mantener el pitch anterior", nullptr, settings_.pitchOnLoadMode == 1)) {
                settings_.pitchOnLoadMode = 1; settings_.save();
            }
            if (ImGui::MenuItem("Conservar el % de pitch anterior sin preguntar", nullptr, settings_.pitchOnLoadMode == 2)) {
                settings_.pitchOnLoadMode = 2; settings_.save();
            }
            ImGui::EndPopup();
        }
        {
            const ImVec2 pp = ImGui::GetCursorScreenPos();
            NeonButton("##bendm", acc, dk.buttonBend < 0, ImVec2(pw, bh2), has);
            if (ImGui::IsItemActivated()) dk.buttonBend = -0.04f;
            if (ImGui::IsItemDeactivated()) dk.buttonBend = 0.f;
            DrawIcon(dl, Ic::ChevLeft, ImVec2(pp.x + pw * 0.4f, pp.y + bh2 * 0.5f), 12 * s, col::Text);
            DrawIcon(dl, Ic::ChevLeft, ImVec2(pp.x + pw * 0.62f, pp.y + bh2 * 0.5f), 12 * s, col::Text);
            ImGui::SetItemTooltip("Frenar un momento (−4 %%) mientras se mantiene");
        }
        {
            const ImVec2 pp = ImGui::GetCursorScreenPos();
            NeonButton("##bendp", acc, dk.buttonBend > 0, ImVec2(pw, bh2), has);
            if (ImGui::IsItemActivated()) dk.buttonBend = 0.04f;
            if (ImGui::IsItemDeactivated()) dk.buttonBend = 0.f;
            DrawIcon(dl, Ic::ChevRight, ImVec2(pp.x + pw * 0.38f, pp.y + bh2 * 0.5f), 12 * s, col::Text);
            DrawIcon(dl, Ic::ChevRight, ImVec2(pp.x + pw * 0.6f, pp.y + bh2 * 0.5f), 12 * s, col::Text);
            ImGui::SetItemTooltip("Acelerar un momento (+4 %%) mientras se mantiene");
        }
        if (NeonButton("0##pz", acc, false, ImVec2(pw, bh2), true, fontHead, 13 * s)) { dk.pitch = 0; p.pitch = 0; if (p.syncTo.load() >= 0) dk.sync = false; }
        ImGui::SetItemTooltip("Pitch a 0");
        ImGui::EndGroup();
    }
    hotCuePopup(d);
    ImGui::EndChild();
}

void App::hotCuePopup(int d) {
    if (!ImGui::BeginPopup("hotcuemenu")) return;
    const float s = S();
    const int i = std::clamp(hcMenuCue_[d], 0, kNumHotCues - 1);
    DeckUi& dk = deck_[d];
    static char buf[48];
    if (ImGui::IsWindowAppearing()) { std::snprintf(buf, sizeof buf, "%s", dk.meta.hotCueNames[i].c_str()); ImGui::SetKeyboardFocusHere(1); }
    ImGui::TextColored(toVec4(kHotColors[i]), "Hot cue %d", i + 1);
    auto apply = [&](const std::string& n) {
        dk.meta.hotCueNames[i] = n.substr(0, 40);
        library_.setHotCueName(dk.path, i, dk.meta.hotCueNames[i]);
        ImGui::CloseCurrentPopup();
    };
    ImGui::SetNextItemWidth(200 * s);
    if (ImGui::InputTextWithHint("##hcname", "Nombre (Intro, Drop…)", buf, sizeof buf, ImGuiInputTextFlags_EnterReturnsTrue)) apply(buf);
    int k = 0;
    for (const char* q : {"Intro", "Voz", "Break", "Subida", "Drop", "Outro"}) {
        if (k++ % 3) ImGui::SameLine();
        if (ImGui::SmallButton(q)) apply(q);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Guardar el nombre")) apply(buf);
    if (ImGui::MenuItem("Quitar el nombre", nullptr, false, !dk.meta.hotCueNames[i].empty())) apply("");
    if (ImGui::MenuItem("Borrar el hot cue")) {
        send(d, Cmd::HotCueDelete, 0, i);
        dk.meta.hotCueNames[i].clear();
        library_.setHotCueName(dk.path, i, "");
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void App::drawDeckTabs(int d, float x, float y, float w, float h) {
    const float s = S();
    if (h < 26 * s || !settings_.showPerformance) return;
    DeckUi& dk = deck_[d];
    DeckParams& p = engine_.deck[d];
    const DeckStatus& st = engine_.deckStatus[d];
    const ImU32 acc = col::deck(d);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool has = dk.track != nullptr;
    const float sp = 6 * s;
    // Selector de modo (control segmentado, incluye 0.14.0 Pro: STEMS en tiempo real)
    const char* names[9] = {"CUES", "LOOP", "SALTO", "REJILLA", "TONO", "FX", "ROLL", "VINILO", "STEMS"};
    const float th = 22 * s;
    const float tw = w / 9.f;
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + th), col::Bg, 6 * s);
    const bool anyStemMod = p.stemMute[0].load() || p.stemMute[1].load() || p.stemMute[2].load() || p.stemMute[3].load() ||
                            p.stemFx[0].load() > 0.01f || p.stemFx[1].load() > 0.01f || p.stemFx[2].load() > 0.01f || p.stemFx[3].load() > 0.01f;
    for (int i = 0; i < 9; ++i) {
        const ImVec2 ta(x + i * tw, y), tb(x + (i + 1) * tw, y + th);
        ImGui::SetCursorScreenPos(ta);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##padmode", ImVec2(tw, th))) padMode_[d] = i;
        ImGui::PopID();
        const bool on = padMode_[d] == i, hov = ImGui::IsItemHovered();
        if (on) dl->AddRectFilled(ImVec2(ta.x + 2 * s, ta.y + 2 * s), ImVec2(tb.x - 2 * s, tb.y - 2 * s), col::mix(col::Panel2, acc, 0.22f), 5 * s);
        CenterText(dl, ta, tb, on ? col::Text : hov ? col::mix(col::TextDim, col::Text, 0.5f) : (i == 8 ? col::DeckA : col::TextDim), names[i], fontHead, 12.2f * s);
        const bool lit = (i == 5 && p.fxOn.load() && p.fxType.load() != 0) || (i == 7 && p.reverse.load()) || (i == 8 && anyStemMod);
        if (lit) dl->AddCircleFilled(ImVec2(tb.x - 6 * s, ta.y + 6 * s), 3.2f * s, col::Amber);
    }
    y += th + sp;
    const float rest = h - th - sp;
    // 0.10.4: dos filas siempre que quepan con botones de 17 px (con el rack abierto los platos son más bajos).
    const float rowB = std::clamp((rest - sp) * 0.5f, 17 * s, 32 * s);
    const bool tworows = rest >= rowB * 2 + sp;
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::BeginGroup();
    ImGui::PushItemWidth(w);
    switch (padMode_[d]) {
    case 6: {
        const float beats[] = {0.0625f,0.125f,0.25f,0.5f,1,2,4,8};
        const char* labels[] = {"1/16","1/8","1/4","1/2","1","2","4","8"};
        const int perRow = tworows ? 4 : 8;
        const float bw=(w-(perRow-1)*sp)/perRow;
        for (int i=0;i<8;++i) {
            if (i%perRow) ImGui::SameLine(0,sp);
            ImGui::PushID(i);
            NeonButton(labels[i],acc,st.rolling.load() && rollHeld_[d],ImVec2(bw,rowB),has);
            if (ImGui::IsItemActivated()) { send(d,Cmd::RollStart,beats[i]); rollHeld_[d]=true; }
            ImGui::SetItemTooltip("Mantén pulsado: roll de %g pulsos. Al soltar recupera el avance original.",beats[i]);
            ImGui::PopID();
        }
        break;
    }
    case 7: ImGui::BeginDisabled(!has); drawPerformance(d); ImGui::EndDisabled(); break;
    case 8: {  // 0.14.0 Pro: Separación de 4 Stems en tiempo real por plato (VOZ, BATERÍA, BAJO, INSTRUM.)
        const char* stNames[4] = {"🎤 VOZ", "🥁 BATERÍA", "🎸 BAJO", "🎹 INSTRUM."};
        const ImU32 stCols[4] = {col::Violet, col::Amber, col::Red, col::Green};
        const float sw4 = (w - 3 * sp) / 4.f;
        for (int k = 0; k < 4; ++k) {
            if (k > 0) ImGui::SameLine(0, sp);
            ImGui::PushID(k + 400);
            const bool active = !p.stemMute[k].load();
            const float volPct = p.stemVol[k].load() * 100.f;
            char sLbl[48];
            std::snprintf(sLbl, sizeof sLbl, active ? "%s  %.0f%%" : "%s  [MUTE]", stNames[k], volPct);
            const ImVec2 sp0 = ImGui::GetCursorScreenPos();
            if (NeonButton(sLbl, stCols[k], active, ImVec2(sw4, rowB), true, fontHead, 12.5f * s)) {
                p.stemMute[k] = active;
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("stem_detail_pop");
            const float pk = std::clamp(st.stemPeak[k].load() * 1.4f, 0.f, 1.f);
            if (pk > 0.01f && active) {
                dl->AddRectFilled(ImVec2(sp0.x + 4 * s, sp0.y + rowB - 3.5f * s),
                                  ImVec2(sp0.x + 4 * s + (sw4 - 8 * s) * pk, sp0.y + rowB - 1.5f * s), stCols[k], 1.5f * s);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s (%s)\nClic izquierdo: activar / silenciar en tiempo real (0 ms latencia)\nClic derecho: ajustar volumen (0-150%%) y efecto individual del stem",
                                  stNames[k], active ? "ACTIVO" : "SILENCIADO");
            }
            if (ImGui::BeginPopup("stem_detail_pop")) {
                ImGui::TextColored(toVec4(stCols[k]), "%s · CONTROL INDIVIDUAL", stNames[k]);
                float v = p.stemVol[k].load() * 100.f;
                if (ImGui::SliderFloat("Volumen (%)", &v, 0.f, 150.f, "%.0f %%")) p.stemVol[k] = v / 100.f;
                float fxv = p.stemFx[k].load() * 100.f;
                if (ImGui::SliderFloat("Efecto / Filtro Stem (%)", &fxv, 0.f, 100.f, "%.0f %%")) p.stemFx[k] = fxv / 100.f;
                if (ImGui::Button("Restablecer este stem")) { p.stemMute[k] = false; p.stemVol[k] = 1.f; p.stemFx[k] = 0.f; }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (tworows) {
            if (NeonButton("SOLO VOZ (Acapella)", col::Violet, !p.stemMute[0].load() && p.stemMute[1].load() && p.stemMute[2].load() && p.stemMute[3].load(), ImVec2(sw4, rowB), true, fontHead, 11.5f * s)) {
                p.stemMute[0] = false; p.stemMute[1] = true; p.stemMute[2] = true; p.stemMute[3] = true;
            }
            ImGui::SameLine(0, sp);
            if (NeonButton("SOLO BASE (Instrumental)", col::Green, p.stemMute[0].load() && !p.stemMute[1].load() && !p.stemMute[2].load() && !p.stemMute[3].load(), ImVec2(sw4, rowB), true, fontHead, 11.5f * s)) {
                p.stemMute[0] = true; p.stemMute[1] = false; p.stemMute[2] = false; p.stemMute[3] = false;
            }
            ImGui::SameLine(0, sp);
            if (NeonButton("SOLO BATERÍA + BAJO", col::Amber, p.stemMute[0].load() && !p.stemMute[1].load() && !p.stemMute[2].load() && p.stemMute[3].load(), ImVec2(sw4, rowB), true, fontHead, 11.5f * s)) {
                p.stemMute[0] = true; p.stemMute[1] = false; p.stemMute[2] = false; p.stemMute[3] = true;
            }
            ImGui::SameLine(0, sp);
            if (NeonButton("RESTABLECER 4 STEMS", acc, !anyStemMod, ImVec2(sw4, rowB), true, fontHead, 11.5f * s)) {
                for (int k = 0; k < 4; ++k) { p.stemMute[k] = false; p.stemVol[k] = 1.f; p.stemFx[k] = 0.f; }
            }
        }
        break;
    }
    case 0: {  // Hot cues 1-8 (en una fila de 8 si no caben dos)
        const int perRow = tworows ? 4 : 8;
        const float hw = (w - (perRow - 1) * sp) / perRow;
        for (int i = 0; i < kNumHotCues; ++i) {
            if (i % perRow) ImGui::SameLine(0, sp);
            const double h = st.hotCues[i].load();
            char lbl[80];
            const std::string& nm = dk.meta.hotCueNames[i];
            if (h >= 0 && has && tworows) std::snprintf(lbl, sizeof lbl, "%d  ·  %s##hcp%d", i + 1, nm.empty() ? formatTime(h / dk.track->sampleRate, false).c_str() : nm.c_str(), i);
            else std::snprintf(lbl, sizeof lbl, "%d##hcp%d", i + 1, i);
            if (NeonButton(lbl, kHotColors[i], h >= 0, ImVec2(hw, rowB), has, fontHead, 14 * s)) send(d, Cmd::HotCue, 0, i);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && h >= 0) { hcMenuCue_[d] = i; ImGui::OpenPopup("hotcuemenu"); }
            ImGui::SetItemTooltip(h >= 0 ? "Clic: saltar y reproducir · clic derecho: ponerle nombre o borrarlo" : "Clic: guardar hot cue aquí");
        }
        break;
    }
    case 1: {  // Loop
        const double bl = dk.meta.bpm > 0 && has ? 60.0 / dk.meta.bpm * dk.track->sampleRate : 0;
        const double curLen = st.loopOut.load() - st.loopIn.load();
        const double beats[7] = {0.25, 0.5, 1, 2, 4, 8, 16};
        const char* bt[7] = {"1/4", "1/2", "1", "2", "4", "8", "16"};
        if (!tworows) {
            // Una sola fila (pantallas bajas): tamaños más usados + IN, OUT y RELOOP/SALIR.
            const float lw1 = (w - 8 * sp) / 9.f;
            for (int i = 1; i < 7; ++i) {
                if (i > 1) ImGui::SameLine(0, sp);
                const bool on = st.loopActive.load() && bl > 0 && std::fabs(curLen - bl * beats[i]) < 2.0;
                char l[32]; std::snprintf(l, sizeof l, "%s##al%d", bt[i], i);
                if (NeonButton(l, col::Green, on, ImVec2(lw1, rowB), has, fontHead, 13 * s)) send(d, Cmd::AutoLoop, beats[i]);
            }
            ImGui::SameLine(0, sp);
            if (NeonButton("IN##li", col::Green, false, ImVec2(lw1, rowB), has, fontHead, 13 * s)) send(d, Cmd::LoopIn);
            ImGui::SameLine(0, sp);
            if (NeonButton("OUT##lo", col::Green, false, ImVec2(lw1, rowB), has, fontHead, 13 * s)) send(d, Cmd::LoopOut);
            ImGui::SameLine(0, sp);
            if (st.loopActive.load()) { if (NeonButton("SALIR##lx", col::Green, true, ImVec2(lw1, rowB), has, fontHead, 13 * s)) send(d, Cmd::LoopExit); }
            else if (NeonButton("RELOOP##lr", col::Green, false, ImVec2(lw1, rowB), has, fontHead, 12 * s)) send(d, Cmd::Reloop);
            break;
        }
        const float lw = (w - 6 * sp) / 7.f;
        for (int i = 0; i < 7; ++i) {
            if (i) ImGui::SameLine(0, sp);
            const bool on = st.loopActive.load() && bl > 0 && std::fabs(curLen - bl * beats[i]) < 2.0;
            char l[32]; std::snprintf(l, sizeof l, "%s##al%d", bt[i], i);
            if (NeonButton(l, col::Green, on, ImVec2(lw, rowB), has, fontHead, 14 * s)) send(d, Cmd::AutoLoop, beats[i]);
        }
        if (!tworows) break;
        const float w6 = (w - 5 * sp) / 6.f;
        if (NeonButton("IN##li", col::Green, false, ImVec2(w6, rowB), has, fontHead, 14 * s)) send(d, Cmd::LoopIn);
        ImGui::SetItemTooltip("Entrada del loop");
        ImGui::SameLine(0, sp);
        if (NeonButton("OUT##lo", col::Green, false, ImVec2(w6, rowB), has, fontHead, 14 * s)) send(d, Cmd::LoopOut);
        ImGui::SetItemTooltip("Salida del loop");
        ImGui::SameLine(0, sp);
        if (NeonButton("½##lh", col::Green, false, ImVec2(w6, rowB), has, fontHead, 14 * s)) send(d, Cmd::LoopHalve);
        ImGui::SameLine(0, sp);
        if (NeonButton("×2##ld", col::Green, false, ImVec2(w6, rowB), has, fontHead, 14 * s)) send(d, Cmd::LoopDouble);
        ImGui::SameLine(0, sp);
        if (st.loopActive.load()) { if (NeonButton("SALIR##lx", col::Green, true, ImVec2(w6, rowB), has, fontHead, 14 * s)) send(d, Cmd::LoopExit); }
        else if (NeonButton("RELOOP##lr", col::Green, false, ImVec2(w6, rowB), has, fontHead, 14 * s)) send(d, Cmd::Reloop);
        ImGui::SameLine(0, sp);
        bool q = p.quantize.load();
        if (NeonButton("QUANT##q", col::Violet, q, ImVec2(w6, rowB), true, fontHead, 14 * s)) { q = !q; for (auto& dp : engine_.deck) dp.quantize = q; settings_.quantize = q; }
        ImGui::SetItemTooltip("Cuantizar: cues, hot cues y loops se ajustan al pulso");
        break;
    }
    case 2: {  // Salto de pulsos y desplazamiento
        const int jumps[8] = {-16, -8, -4, -1, 1, 4, 8, 16};
        const float jw = (w - 7 * sp) / 8.f;
        for (int i = 0; i < 8; ++i) {
            if (i) ImGui::SameLine(0, sp);
            char id[16], n[8];
            std::snprintf(id, sizeof id, "##bj%d", i);
            std::snprintf(n, sizeof n, "%d", std::abs(jumps[i]));
            if (IconTextButton(id, jumps[i] < 0 ? Ic::StepBack : Ic::StepFwd, n, col::Violet, false, ImVec2(jw, rowB), has && dk.meta.bpm > 0, fontHead, 14 * s, jumps[i] > 0))
                send(d, Cmd::BeatJump, jumps[i]);
            ImGui::SetItemTooltip("Saltar %d pulsos %s", std::abs(jumps[i]), jumps[i] < 0 ? "atrás" : "adelante");
        }
        if (!tworows) break;
        const float w4 = (w - 3 * sp) / 4.f;
        if (NeonButton("Al inicio##ini", acc, false, ImVec2(w4, rowB), has, fontHead, 14 * s)) send(d, Cmd::Seek, 0.0);
        ImGui::SameLine(0, sp);
        if (NeonButton("Al cue##alcue", col::Amber, false, ImVec2(w4, rowB), has, fontHead, 14 * s)) send(d, Cmd::Seek, (double)st.cueFrames.load());
        ImGui::SameLine(0, sp);
        if (NeonButton("1.er pulso##prim", acc, false, ImVec2(w4, rowB), has && dk.meta.bpm > 0, fontHead, 14 * s))
            send(d, Cmd::Seek, std::fmod(std::max(0.0, dk.meta.gridAnchorSec), 4 * 60.0 / std::max(1.0, dk.meta.bpm)) * dk.track->sampleRate);
        ImGui::SetItemTooltip("Ir al primer tiempo de compás de la rejilla");
        ImGui::SameLine(0, sp);
        if (NeonButton("Expulsar##ej", col::Violet, false, ImVec2(w4, rowB), has && !engine_.deckStatus[d].playing.load(), fontHead, 14 * s)) ejectDeck(d);
        break;
    }
    case 3: {  // Rejilla de pulsos
        if (!has) ImGui::BeginDisabled();
        double bpm = dk.meta.bpm;
        ImGui::SetNextItemWidth(w * 0.3f);
        if (ImGui::InputDouble("##bpmin", &bpm, 0.01, 0.1, "%.2f BPM")) {
            if (bpm >= 30 && bpm <= 300) { dk.meta.bpm = bpm; dk.meta.analyzed = true; dk.meta.gridSrc = 3; pushGrid(d); library_.updateMeta(dk.path, dk.meta); }
        }
        if (!has) ImGui::EndDisabled();
        const float fh = ImGui::GetFrameHeight();
        const float wb = (w * 0.7f - 4 * sp) / 4.f;
        ImGui::SameLine(0, sp);
        if (NeonButton("×2##b", col::Violet, false, ImVec2(wb, fh), has) && dk.meta.bpm > 0) { dk.meta.bpm *= 2; dk.meta.gridSrc = 3; pushGrid(d); library_.updateMeta(dk.path, dk.meta); }
        ImGui::SameLine(0, sp);
        if (NeonButton("÷2##b", col::Violet, false, ImVec2(wb, fh), has) && dk.meta.bpm > 0) { dk.meta.bpm /= 2; dk.meta.gridSrc = 3; pushGrid(d); library_.updateMeta(dk.path, dk.meta); }
        ImGui::SameLine(0, sp);
        if (NeonButton("TAP##tap", acc, false, ImVec2(wb, fh), has)) tapTempo(d);
        ImGui::SetItemTooltip("Pulsa al ritmo (4 veces o más) para fijar el BPM");
        ImGui::SameLine(0, sp);
        const bool busy = !dk.reanalyzing.empty();
        if (NeonButton(busy ? "…##ra" : "Analizar##ra", col::Violet, busy, ImVec2(wb, fh), has && !busy)) reanalyze(d);
        ImGui::SetItemTooltip("Volver a calcular BPM y rejilla");
        if (!tworows) break;
        auto shift = [&](double ms) { dk.meta.gridAnchorSec += ms / 1000.0; dk.meta.gridSrc = 3; pushGrid(d); library_.updateMeta(dk.path, dk.meta); };
        const float w5 = (w - 4 * sp) / 5.f;
        if (IconTextButton("##g1", Ic::Rewind, "10 ms", col::Violet, false, ImVec2(w5, rowB), has)) shift(-10);
        ImGui::SameLine(0, sp);
        if (IconTextButton("##g2", Ic::StepBack, "1 ms", col::Violet, false, ImVec2(w5, rowB), has)) shift(-1);
        ImGui::SameLine(0, sp);
        if (IconTextButton("##g3", Ic::StepFwd, "1 ms", col::Violet, false, ImVec2(w5, rowB), has, nullptr, 0, true)) shift(1);
        ImGui::SameLine(0, sp);
        if (IconTextButton("##g4", Ic::FastFwd, "10 ms", col::Violet, false, ImVec2(w5, rowB), has, nullptr, 0, true)) shift(10);
        ImGui::SameLine(0, sp);
        if (NeonButton("1.º aquí##g5", acc, false, ImVec2(w5, rowB), has && dk.meta.bpm > 0)) {
            const double beat = 60.0 / dk.meta.bpm;
            dk.meta.gridAnchorSec = std::fmod(deckPosSec(d), 4 * beat);
            dk.meta.gridSrc = 3;
            pushGrid(d);
            library_.updateMeta(dk.path, dk.meta);
        }
        ImGui::SetItemTooltip("Coloca el primer tiempo del compás en la posición actual");
        break;
    }
    case 4: {  // Tono
        const float w6 = (w - 5 * sp) / 6.f;
        const bool klk = p.keyLock.load();
        if (NeonButton(klk ? "KEY LOCK ON##kl2" : "KEY LOCK##kl2", col::Amber, klk, ImVec2(w6 * 2 + sp, rowB), has, fontHead, 14 * s)) p.keyLock = !klk;
        ImGui::SetItemTooltip("Key lock: cambiar el tempo sin cambiar el tono");
        ImGui::SameLine(0, sp);
        const float sh = p.keyShift.load();
        if (NeonButton("-1##ks", col::Violet, false, ImVec2(w6, rowB), has, fontHead, 14 * s)) p.keyShift = std::max(-6.f, sh - 1.f);
        ImGui::SameLine(0, sp);
        char kb[32];
        std::snprintf(kb, sizeof kb, "%+d st##ksv", (int)std::lround(sh));
        if (NeonButton(kb, col::Violet, std::fabs(sh) > 0.01f, ImVec2(w6, rowB), has, fontHead, 14 * s)) p.keyShift = 0.f;
        ImGui::SetItemTooltip("Transposición en semitonos (clic: volver a 0)");
        ImGui::SameLine(0, sp);
        if (NeonButton("+1##ks2", col::Violet, false, ImVec2(w6, rowB), has, fontHead, 14 * s)) p.keyShift = std::min(6.f, sh + 1.f);
        ImGui::SameLine(0, sp);
        const int o = 1 - d;
        const bool canMatch = has && deck_[o].track && dk.meta.key >= 0 && deck_[o].meta.key >= 0;
        if (NeonButton("Igualar##km", acc, false, ImVec2(w6, rowB), canMatch, fontHead, 14 * s)) keyMatch(d);
        ImGui::SetItemTooltip("Transporta este plato a una tonalidad compatible con el otro (rueda Camelot)");
        if (!tworows) break;
        ImGui::Dummy(ImVec2(0, 1 * s));
        if (has && dk.meta.key >= 0) {
            ImGui::TextDisabled("Tonalidad de la pista: %s", keyText(dk.meta.key).c_str());
            if (deck_[o].track && deck_[o].meta.key >= 0) {
                ImGui::SameLine();
                const bool ok = keysCompatible(dk.meta.key, deck_[o].meta.key);
                ImGui::TextColored(toVec4(ok ? col::Green : col::Amber), ok ? "· compatible con %s" : "· choca con %s", keyCamelot(deck_[o].meta.key));
            }
        } else if (has) ImGui::TextDisabled("Tonalidad sin detectar.");
        break;
    }
    case 5: {  // Efecto
        int fx = p.fxType.load();
        ImGui::SetNextItemWidth(w * 0.34f);
        // 0.11: los de siempre y los nuevos (que usan una ranura como las del rack), con su descripción.
        if (ImGui::BeginCombo("##fx", fxName((FxType)fx), ImGuiComboFlags_HeightLargest)) {
            auto item = [&](int t) {
                ImGui::PushID(t);
                if (ImGui::Selectable(fxName((FxType)t), t == fx)) {
                    p.fxType = t;
                    if (t == 0) p.fxOn = false;
                    // Tiempo de partida con sentido: el riser cuenta compases (4) y el slicer corta un compás.
                    const int st = deckFxToStation(t);
                    const float want = st == (int)StFx::Riser || st == (int)StFx::Slicer ? 4.f : st == (int)StFx::Tremolo ? 0.25f : -1.f;
                    if (want > 0) for (int i = 0; i < 7; ++i) if (kFxBeats[i] == want) { dk.fxBeatsIdx = i; p.fxBeats = want; }
                }
                if (const int st = deckFxToStation(t)) ImGui::SetItemTooltip("%s", stFxDescription(st));
                ImGui::PopID();
            };
            item(0);
            ImGui::SeparatorText("De siempre");
            for (int t = 1; t < (int)FxType::Riser; ++t) item(t);
            ImGui::SeparatorText("Nuevos");
            for (int t = (int)FxType::Riser; t < (int)FxType::Count; ++t) item(t);
            ImGui::EndCombo();
        }
        if (const int st = deckFxToStation(fx)) ImGui::SetItemTooltip("%s", stFxDescription(st));
        ImGui::SameLine(0, sp);
        const bool on = p.fxOn.load();
        // 0.10.5: toque corto = encender/apagar; mantener pulsado = solo mientras lo pulsas (para golpes en directo).
        NeonButton(on ? "ACTIVADO##fxon" : "ACTIVAR##fxon", acc, on, ImVec2(w * 0.3f, ImGui::GetFrameHeight()), fx != 0, fontHead, 14 * s);
        if (ImGui::IsItemActivated() && fx != 0) {
            dk.fxPressAt = now_;
            dk.fxPressTurnedOn = !on;
            if (!on) p.fxOn = true;
        }
        if (ImGui::IsItemDeactivated() && dk.fxPressAt >= 0) {
            const bool held = now_ - dk.fxPressAt > 0.35;
            if (!dk.fxPressTurnedOn || held) p.fxOn = false;   // apagar con un toque, o soltar tras mantener
            dk.fxPressAt = -1;
        }
        ImGui::SetItemTooltip("Toque: encender o apagar · mantener pulsado: suena solo mientras lo pulsas");
        ImGui::SameLine(0, sp);
        ImGui::SetNextItemWidth(w - w * 0.64f - 2 * sp);
        const int stFx = deckFxToStation(fx);
        if (ImGui::Combo(fx == (int)FxType::Reverb ? "##tam" : "##fb", &dk.fxBeatsIdx, kFxBeatsTxt, 7)) p.fxBeats = kFxBeats[dk.fxBeatsIdx];
        ImGui::SetItemTooltip("%s", fx == (int)FxType::Reverb ? "Tamaño de la sala"
                                    : stFx == (int)StFx::Riser ? "Compases que tarda en subir el ruido"
                                    : stFx == (int)StFx::Slicer ? "Pulsos del trozo que se corta en 8 (4 = un compás)"
                                    : stFx && !stFxUsesTime(stFx) ? "Este efecto no usa el tiempo"
                                    : "Pulsos (sincronizado al BPM)");
        if (!tworows) break;
        float amt = p.fxAmount.load();
        ImGui::SetNextItemWidth(w);
        if (stFx) {
            // En los nuevos, la barra dice qué mueve (patrón del slicer, semitonos del spiral…).
            float amount = 0.5f, mix = 1.f;
            deckFxUnitParams(stFx, amt, amount, mix);
            std::string txt = mix < 1.f || stFx == (int)StFx::Freeze ? "Cantidad " + std::to_string((int)std::lround(amt * 100)) + " %" : stFxAmountText(stFx, amount);
            std::string fmt;
            for (char c : txt) { fmt += c; if (c == '%') fmt += '%'; }   // ImGui: «%» literal se escribe «%%»
            if (ImGui::SliderFloat("##amt", &amt, 0.f, 1.f, fmt.c_str())) p.fxAmount = amt;
        } else if (ImGui::SliderFloat("##amt", &amt, 0.f, 1.f, "Cantidad %.2f")) p.fxAmount = amt;
        break;
    }
    }
    ImGui::PopItemWidth();
    ImGui::EndGroup();
}

// =================================================================== mezclador

void App::drawChannelStrip(int d, float x, float y, float w, float h) {
    // Canal de la mesa. A: de fuera hacia dentro potenciómetros · fader · medidor estéreo; B, en espejo.
    const float s = S();
    DeckParams& p = engine_.deck[d];
    DeckUi& dk = deck_[d];
    const ImU32 acc = col::deck(d);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* lf = fontHead;
    const float lfs = 11 * s;
    const float meterW = 12 * s;
    // Potenciómetros: siempre con rótulo; debajo si cabe, si no al lado (por fuera).
    const float rowH = h / 5.f;
    const bool below = rowH >= 2 * 9.5f * s + lfs + 3 * s;
    const float r = below ? std::clamp((rowH - lfs - 4 * s) * 0.5f, 8 * s, 14 * s) : std::clamp((rowH - 3 * s) * 0.5f, 7 * s, 12 * s);
    const float labelW = std::max(lf->CalcTextSizeA(lfs, FLT_MAX, 0, "FILTER").x, lf->CalcTextSizeA(lfs, FLT_MAX, 0, "SPACE").x);
    const float knobColW = below ? std::max(2 * r, labelW) + 4 * s : 2 * r + labelW + 8 * s;
    const float kx = d == 0 ? x : x + w - knobColW;
    const float mx = d == 0 ? x + w - meterW : x;
    const float fx = d == 0 ? x + knobColW + 4 * s : x + meterW + 4 * s;
    const float fw = w - knobColW - meterW - 8 * s;
    char buf[64], id[24];
    const int color = std::clamp(p.colorFx.load(), 0, (int)ColorFxType::Count - 1);
    const char* lbls[5] = {"GAIN", "HI", "MID", "LOW", colorFxLabel(color)};   // 0.11: la última es la ruleta de color
    auto knobRow = [&](int i, float* v, float vmin, float vmax, float vdef, ImU32 kc, const char* tip) {
        const float ry = y + i * rowH;
        float knobX;
        if (below) knobX = kx + (knobColW - 2 * r) * 0.5f;
        else knobX = d == 0 ? kx + knobColW - 2 * r - 2 * s : kx + 2 * s;
        const float ky = below ? ry + (rowH - (2 * r + lfs + 2 * s)) * 0.5f : ry + (rowH - 2 * r) * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(knobX, ky));
        std::snprintf(id, sizeof id, "##k%d%d", d, i);
        const bool ch = Knob(id, nullptr, v, vmin, vmax, vdef, true, r, kc, tip);
        const ImVec2 ts = lf->CalcTextSizeA(lfs, FLT_MAX, 0, lbls[i]);
        ImVec2 lp;
        if (below) lp = ImVec2(knobX + r - ts.x * 0.5f, ky + 2 * r + 1 * s);
        else lp = ImVec2(d == 0 ? knobX - 4 * s - ts.x : knobX + 2 * r + 4 * s, ky + r - ts.y * 0.5f);
        dl->AddText(lf, lfs, lp, i == 4 && color != 0 ? col::Amber : col::TextDim, lbls[i]);
        return ch;
    };
    float g = p.gainDb.load();
    std::snprintf(buf, sizeof buf, "Ganancia %+.1f dB · doble clic: 0", g);
    if (knobRow(0, &g, -12.f, 12.f, 0.f, col::Text, buf)) p.gainDb = g;
    const char* eqN[3] = {"Agudos", "Medios", "Graves"};
    std::atomic<float>* eqP[3] = {&p.eqHigh, &p.eqMid, &p.eqLow};
    for (int i = 0; i < 3; ++i) {
        float v = eqP[i]->load();
        const float gl = eqKnobToGain(v);
        if (gl < 0.0005f) std::snprintf(buf, sizeof buf, "%s: corte total · doble clic: 0 dB", eqN[i]);
        else std::snprintf(buf, sizeof buf, "%s %+.1f dB · doble clic: 0 dB", eqN[i], linToDb(gl));
        if (knobRow(1 + i, &v, 0.f, 1.f, 0.5f, v < 0.02f ? col::Red : acc, buf)) eqP[i]->store(v);
    }
    float f = p.filter.load();
    std::snprintf(buf, sizeof buf, "%s · clic derecho: elegir color", colorFxTip(color, f).c_str());
    if (knobRow(4, &f, -1.f, 1.f, 0.f, color != 0 ? col::Amber : col::Violet, buf)) p.filter = std::fabs(f) < 0.02f ? 0.f : f;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup(d ? "colorfxB" : "colorfxA");
    drawColorFxPopup(d ? "colorfxB" : "colorfxA");

    // Fader y preescucha
    const float pflH = 24 * s;
    const float fadeH = h - pflH - 8 * s;
    ImGui::SetCursorScreenPos(ImVec2(fx, y));
    float vol = p.fader.load();
    std::snprintf(id, sizeof id, "##vol%d", d);
    if (ChannelFader(id, &vol, 0.8f, ImVec2(fw, fadeH), acc, d == 0)) p.fader = vol;
    ImGui::SetItemTooltip("Volumen del canal %s: %s · doble clic: posición por defecto", d ? "B" : "A",
                          vol < 0.001f ? "silencio" : (std::to_string((int)std::lround(linToDb(faderToGain(vol)))) + " dB").c_str());
    // Medidor estéreo del canal
    StereoMeter(dl, ImVec2(mx, y), ImVec2(mx + meterW, y + fadeH), dk.meterL, dk.meterR, dk.meterHold[0], dk.meterHold[1],
                dk.meterHold[0] > 0.999f || dk.meterHold[1] > 0.999f);
    ImGui::SetCursorScreenPos(ImVec2(d == 0 ? fx : x, y + fadeH + 8 * s));
    const bool pfl = p.pfl.load();
    std::snprintf(id, sizeof id, "##pfl%d", d);
    if (IconTextButton(id, Ic::Headphones, "CUE", col::Amber, pfl, ImVec2(fw + meterW + 4 * s, pflH), true, fontHead, 12 * s)) p.pfl = !pfl;
    ImGui::SetItemTooltip(engine_.mixer.hpRouting.load() == (int)HpRouting::Ninguna
                              ? "Preescucha: configura la salida de auriculares en Audio para oírla"
                              : "Enviar este canal a los auriculares (antes del fader)");
}

// Selector del color de las ruletas FILTER (los dos canales a la vez, como en las mesas Pioneer).
void App::drawColorFxPopup(const char* id) {
    if (!ImGui::BeginPopup(id)) return;
    ImGui::PushFont(fontHead, 13.f);
    ImGui::TextUnformatted("COLOR DE LAS RULETAS FILTER");
    ImGui::PopFont();
    const int cur = engine_.deck[0].colorFx.load();
    for (int t = 0; t < (int)ColorFxType::Count; ++t) {
        if (ImGui::MenuItem(colorFxName(t), nullptr, cur == t))
            for (int d = 0; d < kNumDecks; ++d) engine_.deck[d].colorFx = t;
        ImGui::SetItemTooltip("%s", colorFxHelp(t));
    }
    ImGui::TextDisabled("Centro = sin efecto. Afecta a los dos canales.");
    ImGui::EndPopup();
}

void App::drawMixer(ImVec2 a, ImVec2 b) {
    const float s = S();
    Panel(ImGui::GetWindowDrawList(), a, b, 10 * s, col::Panel, col::Line);
    BeginPanel("mezclador", a, b, 10 * s);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 ca = ImGui::GetCursorScreenPos();
    const float cw = ImGui::GetContentRegionAvail().x, ch = ImGui::GetContentRegionAvail().y;
    // Cabecera: A · curva del crossfader · B, con el color de cada canal.
    const float headH = 22 * s;
    for (int d = 0; d < 2; ++d) {
        const ImU32 c = col::deck(d);
        const float bx = d == 0 ? ca.x : ca.x + cw - 20 * s;
        dl->AddRectFilled(ImVec2(bx, ca.y), ImVec2(bx + 20 * s, ca.y + 18 * s), c, 4 * s);
        CenterText(dl, ImVec2(bx, ca.y), ImVec2(bx + 20 * s, ca.y + 18 * s), col::onColor(c), d ? "B" : "A", fontBold, 13 * s);
        const float lx0 = d == 0 ? bx + 24 * s : ca.x + cw * 0.5f + 22 * s, lx1 = d == 0 ? ca.x + cw * 0.5f - 17 * s - 6 * s - 62 * s - 6 * s : bx - 4 * s;
        if (lx1 > lx0) dl->AddRectFilled(ImVec2(lx0, ca.y + 8 * s), ImVec2(lx1, ca.y + 10 * s), col::withAlpha(c, 70), 1 * s);
    }
    {
        const float bw = 34 * s;
        ImGui::SetCursorScreenPos(ImVec2(ca.x + (cw - bw) * 0.5f, ca.y - 1 * s));
        const ImVec2 pp = ImGui::GetCursorScreenPos();
        // 0.11: color de las ruletas FILTER (como los Sound Color FX), a la izquierda de la curva.
        {
            const int color = std::clamp(engine_.deck[0].colorFx.load(), 0, (int)ColorFxType::Count - 1);
            const float cbw = 62 * s;
            const ImVec2 save = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(ca.x + (cw - bw) * 0.5f - 6 * s - cbw, ca.y - 1 * s));
            if (NeonButton(std::string(colorFxLabel(color)).append("##colorbtn").c_str(), col::Amber, color != 0, ImVec2(cbw, 20 * s), true, fontHead, 11.5f * s))
                ImGui::OpenPopup("colorfx");
            ImGui::SetItemTooltip("Color de las ruletas FILTER de los dos canales: %s", colorFxName(color));
            drawColorFxPopup("colorfx");
            ImGui::SetCursorScreenPos(save);
        }
        if (NeonButton("##curva", col::DeckA, false, ImVec2(bw, 20 * s))) ImGui::OpenPopup("curvaxf");
        DrawIcon(dl, Ic::Sliders, ImVec2(pp.x + bw * 0.5f, pp.y + 10 * s), 13 * s, col::TextDim);
        ImGui::SetItemTooltip("Curva del crossfader");
        if (ImGui::BeginPopup("curvaxf")) {
            int curve = engine_.mixer.xfCurve.load();
            const char* curves[] = {"Curva suave", "Mezcla completa", "Corte (scratch)"};
            for (int i = 0; i < 3; ++i) if (ImGui::MenuItem(curves[i], nullptr, curve == i)) engine_.mixer.xfCurve = i;
            ImGui::EndPopup();
        }
    }
    const float xfH = 46 * s;
    // Ayuda de mezcla: tempo y tonalidad entre los platos (si hay sitio).
    const float assistH = ch > 190 * s ? 24 * s : 0.f;   // 0.10.3: se ve casi siempre (como en MixLab)
    if (assistH > 0) drawMixAssist(ca.x, ca.y + headH, cw);
    const float stripY = ca.y + headH + assistH + 6 * s;
    const float stripH = ch - headH - assistH - 6 * s - xfH - 8 * s;
    const float centerW = 26 * s;
    const float stripW = (cw - centerW) * 0.5f;
    drawChannelStrip(0, ca.x, stripY, stripW, stripH);
    drawChannelStrip(1, ca.x + stripW + centerW, stripY, stripW, stripH);
    {
        // Escala de los medidores (dB) entre los dos canales.
        const float fadeH = stripH - 24 * s - 8 * s;
        const float led = std::max(3.f, 4 * s), top = stripY + led + 2 * s, bot = stripY + fadeH;
        const float cx = ca.x + cw * 0.5f;
        const float fz = std::max(8.5f, 9.5f * s);
        for (int db : {0, -6, -12, -24, -48}) {
            const float y = bot - (bot - top) * MeterPos((float)db);
            char t[8];
            std::snprintf(t, sizeof t, "%d", db > 0 ? db : -db);
            const float tw = fontHead->CalcTextSizeA(fz, FLT_MAX, 0, t).x;
            dl->AddText(fontHead, fz, ImVec2(cx - tw * 0.5f, y - fz * 0.55f), db >= 0 ? col::withAlpha(col::Red, 200) : col::withAlpha(col::TextDim, 200), t);
        }
    }
    // Crossfader
    const float xy = ca.y + ch - xfH;
    dl->AddLine(ImVec2(ca.x, xy - 4 * s), ImVec2(ca.x + cw, xy - 4 * s), col::Line);
    const float xh = 30 * s;
    const float lyy = xy + (xfH - xh) * 0.5f + 1 * s;
    dl->AddText(fontBold, 15 * s, ImVec2(ca.x + 2 * s, lyy + 7 * s), col::DeckA, "A");
    dl->AddText(fontBold, 15 * s, ImVec2(ca.x + cw - 11 * s, lyy + 7 * s), col::DeckB, "B");
    ImGui::SetCursorScreenPos(ImVec2(ca.x + 18 * s, lyy));
    float xf = engine_.mixer.crossfader.load();
    if (XFader("##xf", &xf, ImVec2(cw - 36 * s, xh), col::DeckA, col::DeckB)) engine_.mixer.crossfader = xf;
    ImGui::SetItemTooltip("Crossfader · doble clic: centro · rueda: ajuste fino");
    ImGui::EndChild();
}

// =================================================================== barra de estado

void App::drawStatusBar(ImVec2 a, ImVec2 b) {
    const float s = S();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, col::mix(col::Bg, col::Panel, 0.6f));
    dl->AddLine(a, ImVec2(b.x, a.y), col::Line);
    const float fs = 13.5f * s;
    ImFont* f = ImGui::GetFont();
    const float ty = a.y + (b.y - a.y - fs) * 0.5f;
    // Derecha: audio · mando · versión
    const AudioState& st = audio_.state();
    char r[320];
    const double latMs = st.sampleRate ? 1000.0 * st.periodFrames / st.sampleRate : 0;
    char midi[160] = "";
    if (!midi_.devices().empty()) {
        std::string name = midi_.devices()[0];
        if (mandos_) for (auto& m : mandos_->info()) if (m.activo) { name = m.nombre; break; }   // el mando reconocido
        std::snprintf(midi, sizeof midi, "  ·  Mando: %s%s", name.c_str(), midi_.shiftHeld() ? " [SHIFT]" : "");
    }
    if (recorder_.active())
        std::snprintf(r, sizeof r, "● Grabando %s%s  ·  DJ Elver Galarga 0.14.0 Pro", formatTime(recorder_.seconds(), false).c_str(), engine_.status.recDropped.load() ? " · ¡pérdidas!" : "");
    else
        std::snprintf(r, sizeof r, "%s · %s · %u Hz · %.1f ms · CPU %d %%%s  ·  DJ Elver Galarga 0.14.0 Pro", st.running ? st.deviceName.c_str() : "Sin audio", st.backend.c_str(),
                      st.sampleRate, latMs, (int)std::lround(engine_.status.cpuLoad.load() * 100), midi);
    const std::string rs = ellipsize(r, (b.x - a.x) * 0.45f, f, fs);
    const float rw = f->CalcTextSizeA(fs, FLT_MAX, 0, rs.c_str()).x;
    dl->AddText(f, fs, ImVec2(b.x - rw - 12 * s, ty), recorder_.active() ? col::Red : (st.simulated || !st.running ? col::Amber : col::TextDim), rs.c_str());
    float leftW = b.x - a.x - rw - 40 * s;
    {
        // Trabajo en segundo plano: análisis, etiquetas y carátulas (con un indicador que gira).
        std::string bg;
        auto add = [&](const std::string& t) { if (!bg.empty()) bg += "  ·  "; bg += t; };
        if (int n = library_.analysisPending(); n > 0) add("Analizando " + std::to_string(n));
        if (int n = library_.tagKeysChecking(); n > 0) add("Leyendo etiquetas " + std::to_string(n));
        if (int n = CoverFetcher::get().pending(); n > 0) add("Carátulas " + std::to_string(n));
        if (library_.scanning()) add("Escaneando carpetas");
        if (!bg.empty()) {
            const float bw = f->CalcTextSizeA(fs, FLT_MAX, 0, bg.c_str()).x;
            const float bx = b.x - rw - 24 * s - bw;
            DrawIcon(dl, Ic::Spinner, ImVec2(bx - 10 * s, (a.y + b.y) * 0.5f), 12 * s, col::DeckA);
            dl->AddText(f, fs, ImVec2(bx, ty), col::TextDim, bg.c_str());
            leftW -= bw + 30 * s;
        }
    }
    if (now_ - statusTime_ < 12.0 && !statusMsg_.empty()) {
        dl->AddText(f, fs, ImVec2(a.x + 12 * s, ty), statusErr_ ? col::Amber : col::Text, ellipsize(statusMsg_, leftW, f, fs).c_str());
    } else if (now_ - midiPickupHint_ < 1.5) {
        dl->AddText(f, fs, ImVec2(a.x + 12 * s, ty), col::Amber, ellipsize("Mando: lleva el control hasta la posición de la pantalla para recogerlo (sin saltos).", leftW, f, fs).c_str());
    } else {
        dl->AddText(f, fs, ImVec2(a.x + 12 * s, ty), col::TextDim,
                    ellipsize("Doble clic: cargar en el plato libre · arrastra pistas a los platos, a la Sidelist o a una lista · clic derecho: más opciones", leftW, f, fs).c_str());
    }
}

// =================================================================== diálogos

void App::drawConfirmLoad() {
    if (confirmDeck_ >= 0 && !ImGui::IsPopupOpen("Plato en reproducción")) ImGui::OpenPopup("Plato en reproducción");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Plato en reproducción", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("El plato %s está sonando. ¿Cargar la pista igualmente?", confirmDeck_ == 1 ? "B" : "A");
        ImGui::TextDisabled("%s", toUtf8(fileNameOf(confirmPath_)).c_str());
        ImGui::Spacing();
        if (NeonButton("Cargar", col::Red, false, ImVec2(120 * S(), 28 * S()))) {
            const int d = confirmDeck_;
            confirmDeck_ = -1;
            loadToDeck(d, confirmPath_, true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (NeonButton("Cancelar", col::Violet, false, ImVec2(120 * S(), 28 * S())) || ImGui::IsKeyPressed(ImGuiKey_Escape)) { confirmDeck_ = -1; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

void App::drawAudioDialog() {
    const float s = S();
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(600 * s, 0), ImGuiCond_Appearing);
    if (!ImGui::Begin("Configuración de audio", &showAudio_, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) { ImGui::End(); return; }
    const auto& devs = audio_.devices();
    auto deviceCombo = [&](const char* label, std::string& name, bool allowDefault) {
        const std::string preview = name.empty() ? std::string("Predeterminado del sistema") : name;
        if (ImGui::BeginCombo(label, preview.c_str())) {
            if (allowDefault && ImGui::Selectable("Predeterminado del sistema", name.empty())) name.clear();
            for (auto& d : devs) {
                char l[300];
                std::snprintf(l, sizeof l, "%s%s  (%d canales)", d.name.c_str(), d.isDefault ? " · predeterminado" : "", d.maxChannels);
                if (ImGui::Selectable(l, name == d.name)) name = d.name;
            }
            ImGui::EndCombo();
        }
    };
    ImGui::SeparatorText("Salida principal (master)");
    ImGui::SetNextItemWidth(380 * s);
    deviceCombo("Dispositivo", pendingAudio_.deviceName, true);
    ImGui::SameLine();
    if (ImGui::SmallButton("Actualizar")) audio_.refreshDevices();
    const int periods[] = {3, 5, 10, 20, 40};
    int pi = 2;
    for (int i = 0; i < 5; ++i) if (pendingAudio_.periodMs == periods[i]) pi = i;
    ImGui::SetNextItemWidth(170 * s);
    const char* pn[] = {"3 ms", "5 ms", "10 ms (recomendado)", "20 ms", "40 ms (seguro)"};
    if (ImGui::Combo("Tamaño de búfer", &pi, pn, 5)) pendingAudio_.periodMs = periods[pi];
    ImGui::SetItemTooltip("Menos milisegundos = menos latencia pero más riesgo de cortes");
    {
        const int rates[] = {0, 44100, 48000, 88200, 96000};
        const char* rn[] = {"La del dispositivo (recomendado)", "44 100 Hz", "48 000 Hz", "88 200 Hz", "96 000 Hz"};
        int ri = 0;
        for (int i = 0; i < 5; ++i) if (pendingAudio_.sampleRate == rates[i]) ri = i;
        ImGui::SetNextItemWidth(250 * s);
        if (ImGui::Combo("Frecuencia de muestreo", &ri, rn, 5)) pendingAudio_.sampleRate = rates[ri];
        ImGui::SetItemTooltip("Si el dispositivo trabaja a otra frecuencia, se convierte (más CPU y algo de latencia)");
    }
    ImGui::Checkbox("Modo exclusivo WASAPI (menor latencia; bloquea el dispositivo para otras apps)", &pendingAudio_.exclusive);

    ImGui::SeparatorText("Auriculares (preescucha)");
    int maxCh = 2;
    for (auto& d : devs) if ((pendingAudio_.deviceName.empty() && d.isDefault) || d.name == pendingAudio_.deviceName) maxCh = d.maxChannels;
    const char* modes[] = {"Desactivada", "Canales 3-4 del mismo dispositivo", "Otro dispositivo de salida", "División mono (izq. = preescucha, der. = master)"};
    int mode = (int)pendingAudio_.hpRouting;
    ImGui::SetNextItemWidth(380 * s);
    if (ImGui::BeginCombo("Modo", modes[mode])) {
        for (int i = 0; i < 4; ++i) {
            const bool enabled = i != 1 || maxCh >= 4;
            if (!enabled) ImGui::BeginDisabled();
            if (ImGui::Selectable(modes[i], mode == i)) mode = i;
            if (!enabled) { ImGui::EndDisabled(); ImGui::SetItemTooltip("El dispositivo seleccionado solo tiene %d canales", maxCh); }
        }
        ImGui::EndCombo();
    }
    pendingAudio_.hpRouting = (HpRouting)mode;
    if (pendingAudio_.hpRouting == HpRouting::DispositivoAparte) {
        ImGui::SetNextItemWidth(380 * s);
        deviceCombo("Dispositivo de auriculares", pendingAudio_.hpDeviceName, false);
        ImGui::TextDisabled("Dos dispositivos tienen relojes distintos: la preescucha puede tener\nmicrocortes ocasionales (el master no se ve afectado).");
    } else if (pendingAudio_.hpRouting == HpRouting::DivisionMono) {
        ImGui::TextDisabled("Para auriculares en una salida estéreo: el master también sale\npor esta salida (en mono, canal derecho).");
    }
    ImGui::TextDisabled("Con un mando con tarjeta de sonido de 4 salidas (como el DDJ-WeGO), elige el\nmando como dispositivo y «Canales 3-4» para los auriculares.");
    ImGui::Spacing();
    if (NeonButton("Aplicar", col::Green, false, ImVec2(120 * s, 28 * s))) applyAudioConfig();
    ImGui::SameLine();
    if (NeonButton("Cerrar", col::Violet, false, ImVec2(120 * s, 28 * s))) showAudio_ = false;

    ImGui::SeparatorText("Estado actual");
    const AudioState& st = audio_.state();
    ImGui::Text("Sistema: %s · %s", st.backend.c_str(), st.deviceName.c_str());
    ImGui::Text("%u Hz · %d canales · búfer %d fotogramas (%.1f ms)%s", st.sampleRate, st.channels, st.periodFrames,
                st.sampleRate ? 1000.0 * st.periodFrames / st.sampleRate : 0.0, st.exclusive ? " · exclusivo" : "");
    const char* hpTxt[] = {"desactivada", "canales 3-4", "otro dispositivo", "división mono"};
    ImGui::Text("Preescucha: %s%s%s", hpTxt[(int)st.hpRouting], st.hpDeviceName.empty() ? "" : " → ", st.hpDeviceName.c_str());
    if (!st.message.empty()) ImGui::TextColored(toVec4(col::Amber), "%s", st.message.c_str());
    ImGui::TextDisabled("ASIO no está disponible en esta versión (ver LEEME).");
    ImGui::End();
}

// ---------------------------------------------------------------- MIDI

void App::drawMidiWizard() {
    const float s = S();
    const auto& steps = wizardSteps();
    const auto& acts = midiActions();
    if (wizStep_ < 0) {
        ImGui::TextWrapped("El asistente te pide que muevas cada control del mando, uno a uno, y lo asigna solo. "
                           "Así la configuración se adapta exactamente a tu mando. Si tu mando no tiene un control, pulsa «Saltar».");
        ImGui::Spacing();
        if (NeonButton("Empezar (mantiene lo que ya funciona)", col::Green, false, ImVec2(330 * s, 34 * s))) { wizStep_ = 0; wizOkUntil_ = 0; midi_.stopLearn(); }
        ImGui::SameLine();
        if (NeonButton("Empezar desde cero", col::Red, false, ImVec2(200 * s, 34 * s))) {
            midi_.bindings().clear();
            midi_.save();
            wizStep_ = 0;
            wizOkUntil_ = 0;
            midi_.stopLearn();
        }
        ImGui::SetItemTooltip("Borra todas las asignaciones antes de empezar");
        return;
    }
    if (wizStep_ >= (int)steps.size()) {
        midi_.stopLearn();
        GlowText(ImGui::GetWindowDrawList(), fontBig, 18 * s, ImGui::GetCursorScreenPos(), col::Green, "¡Mando configurado!");
        ImGui::Dummy(ImVec2(0, 28 * s));
        ImGui::TextWrapped("Prueba ahora los controles: en «Asignaciones» cada fila se ilumina al usarla. "
                           "Ajusta la sensibilidad del jog en «Ajustes del mando».");
        if (NeonButton("Volver a empezar", col::Violet, false, ImVec2(180 * s, 30 * s))) wizStep_ = -1;
        return;
    }
    const WizStep& st = steps[wizStep_];
    ImGui::ProgressBar((float)wizStep_ / steps.size(), ImVec2(-1, 6 * s), "");
    ImGui::TextDisabled("Paso %d de %zu%s", wizStep_ + 1, steps.size(), st.optional ? " · opcional" : "");
    ImGui::Spacing();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const bool ok = now_ < wizOkUntil_;
    NeonFrame(dl, p, ImVec2(p.x + w, p.y + 92 * s), ok ? col::Green : col::deck(st.deck), 8 * s, 1.f);
    ImGui::SetCursorScreenPos(ImVec2(p.x + 14 * s, p.y + 12 * s));
    ImGui::PushTextWrapPos(p.x + w - 14 * s - ImGui::GetWindowPos().x);
    ImGui::PushFont(fontBig, 15.f);
    ImGui::TextUnformatted(st.txt.c_str());
    ImGui::PopFont();
    ImGui::PopTextWrapPos();
    ImGui::SetCursorScreenPos(ImVec2(p.x + 14 * s, p.y + 64 * s));
    if (ok) ImGui::TextColored(toVec4(col::Green), "✔ %s", wizOkText_.c_str());
    else {
        const MidiBinding* b = midi_.findBinding(st.act, st.deck);
        if (b) ImGui::TextDisabled("Ahora: %s · mueve el control para cambiarlo o pulsa «Siguiente»", bindingText(*b).c_str());
        else ImGui::TextColored(toVec4(col::Amber), "Esperando el control… (%s)", acts[st.act].name);
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 102 * s));

    if (midi_.takeLearned()) {
        const MidiBinding* b = midi_.findBinding(st.act, st.deck);
        wizOkText_ = b ? bindingText(*b) : "asignado";
        wizOkUntil_ = now_ + 0.8;
    } else if (wizOkUntil_ > 0 && now_ >= wizOkUntil_) {
        wizOkUntil_ = 0;
        ++wizStep_;
        return;
    } else if (!ok && (!midi_.learning() || midi_.learnAction() != st.act || midi_.learnDeck() != st.deck)) {
        midi_.startLearn(st.act, st.deck);
    }
    if (IconTextButton("##atras", Ic::ChevLeft, "Atrás", col::Violet, false, ImVec2(110 * s, 30 * s), wizStep_ > 0)) { midi_.stopLearn(); wizOkUntil_ = 0; --wizStep_; }
    ImGui::SameLine();
    if (NeonButton(st.optional ? "Saltar (no lo tiene)" : "Saltar", col::Amber, false, ImVec2(170 * s, 30 * s))) { midi_.stopLearn(); wizOkUntil_ = 0; ++wizStep_; }
    ImGui::SameLine();
    if (IconTextButton("##siguiente", Ic::ChevRight, "Siguiente", col::Green, false, ImVec2(130 * s, 30 * s), midi_.findBinding(st.act, st.deck) != nullptr, nullptr, 0, true)) {
        midi_.stopLearn(); wizOkUntil_ = 0; ++wizStep_;
    }
    ImGui::SameLine();
    if (NeonButton("Terminar", col::Red, false, ImVec2(110 * s, 30 * s))) { midi_.stopLearn(); wizStep_ = -1; }
    if (!midi_.recent().empty()) {
        const MidiEvent& e = midi_.recent().back();
        ImGui::TextDisabled("Último mensaje recibido: %s", describeMsg(e.status, e.data1, e.data2).c_str());
    } else {
        ImGui::TextDisabled("Aún no llega nada del mando. ¿Está conectado y cerrado en otros programas?");
    }
}

void App::drawMidiAssignments() {
    const float s = S();
    const auto& acts = midiActions();
    ImGui::TextDisabled("Las filas se iluminan al usar el control. «Aprender»: pulsa y mueve el control del mando.");
    {
        const auto conflicts = midi_.detectConflicts();
        if (!conflicts.empty()) {
            const auto& acts = midiActions();
            ImGui::TextColored(toVec4(col::Red), "⚠ %zu conflicto(s) de asignación detectados (dos acciones comparten el mismo botón/CC):", conflicts.size());
            for (size_t ci = 0; ci < std::min<size_t>(3, conflicts.size()); ++ci) {
                const auto& cf = conflicts[ci];
                ImGui::TextDisabled("  • %s (%s) choca con %s (%s) en canal %d, nota/CC %d",
                                    acts[cf.actionA].name, cf.deckA ? "B" : "A",
                                    acts[cf.actionB].name, cf.deckB ? "B" : "A",
                                    (int)cf.channel + 1, (int)cf.data1);
            }
        } else {
            ImGui::TextColored(toVec4(col::Green), "✓ 0 conflictos MIDI en tus asignaciones personalizadas.");
        }
    }
    if (ImGui::BeginTable("asig", 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV, ImVec2(0, -1))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Acción", ImGuiTableColumnFlags_WidthFixed, 210 * s);
        ImGui::TableSetupColumn("Plato A / global");
        ImGui::TableSetupColumn("Plato B");
        ImGui::TableHeadersRow();
        for (int a = 0; a < (int)acts.size(); ++a) {
            ImGui::TableNextRow();
            const bool hit = now_ - std::max(midiHit_[a * 2], midiHit_[a * 2 + 1]) < 0.4;
            if (hit) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, col::withAlpha(col::Green, 70));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(acts[a].name);
            for (int d = 0; d < (acts[a].perDeck ? 2 : 1); ++d) {
                ImGui::TableNextColumn();
                ImGui::PushID(a * 2 + d);
                const bool learning = midi_.learning() && midi_.learnAction() == a && midi_.learnDeck() == d && wizStep_ < 0;
                if (NeonButton(learning ? "Esperando…##l" : "Aprender##l", col::Amber, learning, ImVec2(90 * s, 0))) {
                    if (learning) midi_.stopLearn(); else { wizStep_ = -1; midi_.startLearn(a, d); }
                }
                MidiBinding* b = nullptr;
                for (auto& x : midi_.bindings()) if (x.action == a && (!acts[a].perDeck || x.deck == d)) { b = &x; break; }
                ImGui::SameLine();
                if (b) {
                    ImGui::TextUnformatted(bindingText(*b).c_str());
                    if (acts[a].kind == MidiKind::Absoluto) {
                        ImGui::SameLine();
                        if (ImGui::Checkbox("invertir", &b->invert)) midi_.save();
                    } else if (acts[a].kind == MidiKind::Relativo) {
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(110 * s);
                        const char* rm[] = {"centro 64", "compl. a 2"};
                        if (ImGui::Combo("##rel", &b->relMode, rm, 2)) midi_.save();
                        ImGui::SetItemTooltip("Cómo codifica el mando el giro. Si el jog va al revés o a saltos, prueba la otra opción.");
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("×")) midi_.clearBinding(a, d);
                    ImGui::SetItemTooltip("Quitar asignación");
                } else ImGui::TextDisabled("—");
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
}

void App::drawMidiSettings() {
    const float s = S();
    MidiSettings& ms = midi_.settings();
    bool ch = false;
    ImGui::SeparatorText("Jog");
    ImGui::SetNextItemWidth(260 * s);
    ch |= ImGui::SliderFloat("Sensibilidad del aro (empuje de tempo)", &ms.jogBendSens, 0.25f, 4.f, "%.2f×", ImGuiSliderFlags_Logarithmic);
    ImGui::SetNextItemWidth(260 * s);
    ch |= ImGui::SliderFloat("Sensibilidad del scratch", &ms.jogScratchSens, 0.25f, 4.f, "%.2f×", ImGuiSliderFlags_Logarithmic);
    ImGui::SetItemTooltip("1× = una vuelta del jog mueve 1,8 s de audio (vinilo a 33⅓ rpm con 720 pasos por vuelta)");
    ImGui::SetNextItemWidth(260 * s);
    ch |= ImGui::SliderFloat("Desplazamiento con el plato en pausa", &ms.jogSeekSens, 0.25f, 8.f, "%.2f×", ImGuiSliderFlags_Logarithmic);
    ch |= ImGui::Checkbox("Tocar la parte de arriba del jog = scratch (modo vinilo)", &ms.touchScratch);
    ImGui::SeparatorText("Faders y potenciómetros");
    ch |= ImGui::Checkbox("Recoger el valor (soft takeover): no saltan si se movieron con el ratón", &ms.softTakeover);
    ch |= ImGui::Checkbox("Invertir el sentido de los faders de pitch", &ms.pitchInvert);
    ImGui::SetItemTooltip("Actívalo si el fader de tempo del mando va al revés que el de la pantalla");
    ImGui::SeparatorText("Luces");
    ch |= ImGui::Checkbox("Encender las luces del mando (PLAY, CUE, SYNC, auriculares, hot cues, loop)", &ms.leds);
    if (midi_.outputs().empty()) ImGui::TextDisabled("No se detecta salida MIDI del mando: las luces no se pueden controlar.");
    else {
        ImGui::TextDisabled("Salida de luces: %s", midi_.outputs()[0].c_str());
        if (NeonButton("Probar luces (1,5 s)", col::Amber, false, ImVec2(200 * s, 26 * s))) ledTestUntil_ = now_ + 1.5;
    }
    if (ch) midi_.save();
}

void App::drawMidiMonitor() {
    const float s = S();
    const auto& acts = midiActions();
    ImGui::TextDisabled("Mensajes recibidos (el más reciente arriba). Los que no hacen nada se pueden asignar desde aquí.");
    static int assignAct = 0, assignDeck = 0;
    static MidiEvent pending{};
    const auto& rec = midi_.recent();
    for (int i = (int)rec.size() - 1; i >= 0; --i) {
        const MidiEvent& e = rec[i];
        const uint8_t type = e.status >> 4, ch = e.status & 15;
        const uint8_t bt = type == 0x8 ? 0x9 : type;
        std::string target;
        for (auto& b : midi_.bindings())
            if (b.type == bt && b.channel == ch && b.data1 == e.data1) {
                target = acts[b.action].name;
                if (acts[b.action].perDeck) target += b.deck ? " (B)" : " (A)";
                break;
            }
        ImGui::PushID(i);
        ImGui::TextUnformatted(describeMsg(e.status, e.data1, e.data2).c_str());
        ImGui::SameLine(340 * s);
        if (!target.empty()) ImGui::TextColored(toVec4(col::Green), "→ %s", target.c_str());
        else {
            ImGui::TextColored(toVec4(col::Amber), "sin asignar");
            ImGui::SameLine();
            if (ImGui::SmallButton("Asignar…")) { pending = e; ImGui::OpenPopup("asignar"); }
        }
        if (ImGui::BeginPopup("asignar")) {
            ImGui::Text("%s", describeMsg(pending.status, pending.data1, pending.data2).c_str());
            ImGui::SetNextItemWidth(260 * s);
            if (ImGui::BeginCombo("Acción", acts[assignAct].name)) {
                for (int a = 0; a < (int)acts.size(); ++a) if (ImGui::Selectable(acts[a].name, a == assignAct)) assignAct = a;
                ImGui::EndCombo();
            }
            if (acts[assignAct].perDeck) { ImGui::RadioButton("Plato A", &assignDeck, 0); ImGui::SameLine(); ImGui::RadioButton("Plato B", &assignDeck, 1); }
            if (NeonButton("Asignar##ok", col::Green, false, ImVec2(120 * s, 26 * s))) {
                MidiBinding b;
                b.action = assignAct;
                b.deck = acts[assignAct].perDeck ? assignDeck : 0;
                b.type = (pending.status >> 4) == 0x8 ? 0x9 : (pending.status >> 4);
                b.channel = pending.status & 15;
                b.data1 = pending.data1;
                midi_.assign(b);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (rec.empty()) ImGui::TextDisabled("Mueve cualquier control del mando…");
}

void App::drawMidiDialog() {
    const float s = S();
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(840 * s, 640 * s), ImGuiCond_Appearing);
    if (!ImGui::Begin("Mando MIDI", &showMidi_, ImGuiWindowFlags_NoCollapse)) { ImGui::End(); return; }
    if (!showMidi_) { midi_.stopLearn(); wizStep_ = -1; }
    // Mandos conectados: reconocidos con su mapeo (de Mixxx) o con las asignaciones propias.
    const std::vector<MandoInfo> infos = mandos_ ? mandos_->info() : std::vector<MandoInfo>{};
    if (infos.empty()) ImGui::TextColored(toVec4(col::Amber), "No se detecta ningún mando MIDI. Conéctalo por USB (se reconoce solo en unos segundos).");
    for (size_t i = 0; i < infos.size(); ++i) {
        const MandoInfo& m = infos[i];
        ImGui::PushID((int)i);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        const float h = (m.activo ? 74.f : 56.f) * s;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), col::Panel2, 8 * s);
        dl->AddRectFilled(p0, ImVec2(p0.x + 4 * s, p0.y + h), m.activo ? (m.errores ? col::Amber : col::Green) : col::Line, 8 * s, ImDrawFlags_RoundCornersLeft);
        ImGui::SetCursorScreenPos(ImVec2(p0.x + 14 * s, p0.y + 8 * s));
        ImGui::BeginGroup();
        ImGui::PushFont(fontHead, 17.f);   // ImGui ya aplica la escala de la pantalla
        ImGui::TextUnformatted(m.activo ? m.nombre.c_str() : m.puerto.c_str());
        ImGui::PopFont();
        if (m.activo) {
            ImGui::TextColored(toVec4(col::Green), "Reconocido: listo para usar%s", m.luces ? ", con luces" : " (este puerto no tiene salida para luces)");
            ImGui::TextDisabled("Puerto «%s» · mapeo de Mixxx%s%s", m.puerto.c_str(), m.autor.empty() ? "" : " de ", m.autor.empty() ? "" : m.autor.substr(0, 60).c_str());
        } else if (m.eleccion == "ninguno") {
            ImGui::TextColored(toVec4(col::TextDim), "Con tus asignaciones (asistente, abajo)");
        } else {
            ImGui::TextColored(toVec4(col::Amber), "No está en la lista de mandos conocidos: usa el asistente de abajo o elige un mapeo parecido");
        }
        ImGui::EndGroup();
        // Mapeo elegido
        const float cw = 300 * s;
        ImGui::SetCursorScreenPos(ImVec2(p0.x + w - cw - 10 * s, p0.y + 10 * s));
        ImGui::SetNextItemWidth(cw);
        std::string autoName = "Automático";
        if (mandos_) {
            const std::string f = mandos_->mapeoAuto(m.puerto);
            for (auto& e : mandos::catalog()) if (e.file == f) autoName = "Automático (" + e.name + ")";
            if (f.empty()) autoName = "Automático (no reconocido)";
        }
        std::string cur = m.eleccion == "auto" ? autoName : m.eleccion == "ninguno" ? std::string("Ninguno: mis asignaciones") : m.eleccion;
        for (auto& e : mandos::catalog()) if (e.file == m.eleccion) cur = e.name;
        if (ImGui::BeginCombo("##mapeo", cur.c_str(), ImGuiComboFlags_HeightLarge)) {
            std::string pick;
            if (ImGui::Selectable(autoName.c_str(), m.eleccion == "auto")) pick = "auto";
            if (ImGui::Selectable("Ninguno: mis asignaciones", m.eleccion == "ninguno")) pick = "ninguno";
            ImGui::Separator();
            for (auto& e : mandos::catalog())
                if (ImGui::Selectable(e.name.c_str(), m.eleccion == e.file)) pick = e.file;
            ImGui::EndCombo();
            if (!pick.empty() && pick != m.eleccion) {
                midi_.setMappingChoice(m.puerto, pick);
                midiRescan(true);
            }
        }
        ImGui::SetItemTooltip("El mapeo dice qué hace cada botón y cada luz del mando.\nAutomático: el que corresponde a su nombre (como en VirtualDJ).");
        if (m.activo) {
            ImGui::SetCursorScreenPos(ImVec2(p0.x + w - cw - 10 * s, p0.y + 42 * s));
            char lbl[64];
            std::snprintf(lbl, sizeof lbl, m.errores ? "Registro (%d avisos)##reg" : "Registro##reg", m.errores);
            if (NeonButton(lbl, m.errores ? col::Amber : col::Violet, false, ImVec2(cw, 24 * s))) ImGui::OpenPopup("registro");
            if (ImGui::BeginPopup("registro")) {
                ImGui::TextDisabled("%s · %s", m.nombre.c_str(), m.archivo.c_str());
                if (!m.descripcion.empty()) ImGui::TextWrapped("%s", m.descripcion.c_str());
                ImGui::Separator();
                ImGui::BeginChild("lineas", ImVec2(620 * s, 260 * s), ImGuiChildFlags_None);
                if (m.registro.empty()) ImGui::TextDisabled("Sin mensajes: el mapeo arrancó sin problemas.");
                for (auto& l : m.registro) ImGui::TextWrapped("%s", l.c_str());
                ImGui::EndChild();
                ImGui::EndPopup();
            }
        }
        ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + h + 6 * s));
        ImGui::Dummy(ImVec2(1, 1));
        ImGui::PopID();
    }
    for (auto& e : midi_.errors()) ImGui::TextColored(toVec4(col::Amber), "%s", e.c_str());
    if (NeonButton("Buscar mandos", col::Violet, false, ImVec2(140 * s, 26 * s))) midiRescan(true);
    ImGui::SameLine();
    if (NeonButton("Preajuste Pioneer DDJ-WeGO", col::DeckA, false, ImVec2(230 * s, 26 * s))) {
        midi_.loadPresetWeGO();
        notify("Preajuste DDJ-WeGO cargado. Revísalo con el asistente.");
    }
    ImGui::SetItemTooltip("Combina lo que comprobaste con «Aprender» (volumen, pitch, crossfader, ganancia)\n"
                          "con el patrón de la familia DDJ (jog, tocar el plato, SHIFT, navegador).\nLo no comprobado conviene revisarlo con el asistente.");
    ImGui::SameLine();
    if (NeonButton("Borrar todo", col::Red, false, ImVec2(120 * s, 26 * s))) { midi_.bindings().clear(); midi_.save(); }
    if (midi_.anyControllerNamed("wego") && midi_.bindings().empty())
        ImGui::TextColored(toVec4(col::Amber), "Se ha detectado un DDJ-WeGO sin asignaciones: carga el preajuste o usa el asistente.");
    ImGui::Spacing();
    bool wizardVisible = false;
    if (ImGui::BeginTabBar("miditabs")) {
        if (ImGui::BeginTabItem("Mandos compatibles")) { drawMandosCompatibles(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Asistente")) { wizardVisible = true; drawMidiWizard(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Asignaciones")) { drawMidiAssignments(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Ajustes del mando")) { drawMidiSettings(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Monitor")) { drawMidiMonitor(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    if (!wizardVisible && wizStep_ >= 0) { wizStep_ = -1; midi_.stopLearn(); }
    ImGui::End();
}

void App::drawMandosCompatibles() {
    const float s = S();
    const auto& ucat = mandos::universalCatalog();
    const auto& xmlCat = mandos::catalog();
    ImGui::TextWrapped("Catálogo Universal Plug-and-Play: %d modelos registrados en 11 marcas (%d con mapeo nativo XML de Mixxx incluido "
                       "y soporte de Stems por Pads, LEDs y aprendizaje directo).", (int)ucat.size(), (int)xmlCat.size());
    static char filtro[64] = "";
    static int brandIdx = 0;
    const char* brands[] = {
        "Todas las marcas (11)", "Pioneer DJ / AlphaTheta", "Denon DJ", "Numark", "Hercules",
        "Native Instruments", "Reloop", "Roland", "Allen & Heath", "Akai Professional", "Behringer", "Vestax"
    };
    ImGui::SetNextItemWidth(220 * s);
    ImGui::Combo("Marca", &brandIdx, brands, 12);
    ImGui::SameLine(0, 10 * s);
    ImGui::SetNextItemWidth(260 * s);
    ImGui::InputTextWithHint("##filtro", "Buscar modelo (DDJ-FLX4, WeGO, Inpulse, Prime…)", filtro, sizeof filtro);
    std::string f = filtro;
    for (auto& c : f) c = (char)std::tolower((unsigned char)c);
    if (ImGui::BeginTable("mandoscat", 4, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp, ImVec2(0, -1))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("MARCA", ImGuiTableColumnFlags_WidthStretch, 0.24f);
        ImGui::TableSetupColumn("MODELO", ImGuiTableColumnFlags_WidthStretch, 0.28f);
        ImGui::TableSetupColumn("PROTOCOLO / DECKS", ImGuiTableColumnFlags_WidthStretch, 0.20f);
        ImGui::TableSetupColumn("ESTADO DE VALIDACIÓN", ImGuiTableColumnFlags_WidthStretch, 0.28f);
        ImGui::TableHeadersRow();
        for (const auto& m : ucat) {
            if (brandIdx > 0 && m.brand.find(brands[brandIdx]) == std::string::npos) continue;
            std::string hay = m.brand + " " + m.model + " " + m.validationStatus;
            for (auto& c : hay) c = (char)std::tolower((unsigned char)c);
            if (!f.empty() && hay.find(f) == std::string::npos) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextColored(toVec4(col::DeckA), "%s", m.brand.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(m.model.c_str());
            ImGui::TableNextColumn(); ImGui::TextDisabled("%s (%dD)", m.protocol.c_str(), m.decks);
            ImGui::TableNextColumn();
            const ImU32 stCol = m.validationStatus.find("Verificado") != std::string::npos ? col::Green :
                                (m.validationStatus.find("Nativo") != std::string::npos ? col::DeckA : col::Amber);
            ImGui::TextColored(toVec4(stCol), "%s", m.validationStatus.c_str());
        }
        ImGui::EndTable();
    }
}

void App::drawPrefsDialog() {
    const float s = S();
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Preferencias", &showPrefs_, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) { ImGui::End(); return; }
    ImGui::SeparatorText("Análisis de BPM");
    float mn = (float)settings_.minBpm, mx = (float)settings_.maxBpm;
    ImGui::SetNextItemWidth(220 * s);
    if (ImGui::DragFloatRange2("Rango de búsqueda", &mn, &mx, 0.5f, 40.f, 250.f, "%.0f BPM", "%.0f BPM")) {
        settings_.minBpm = mn;
        settings_.maxBpm = std::max(mx, mn + 30.f);
    }
    ImGui::TextDisabled("Afecta a los análisis nuevos. Por ejemplo, para drum & bass a 174 usa 100-190.");
    ImGui::SeparatorText("Grabación");
    if (ImGui::Button("Configurar nombre, carpeta y listado…")) showRecording_ = true;
    const char* rf[] = {"WAV 16 bits (con tramado)", "WAV 32 bits coma flotante"};
    ImGui::SetNextItemWidth(260 * s);
    ImGui::Combo("Formato", &settings_.recFormat, rf, 2);
    ImGui::TextDisabled("Carpeta: %s", toUtf8(recordingFolder()).c_str());
    if (ImGui::SmallButton("Abrir carpeta de grabaciones")) win::openFolder(recordingFolder());
    ImGui::SeparatorText("Tonalidad");
    {
        const char* kn[] = {"Camelot + español (8A · La menor)", "Solo Camelot (8A)", "Musical (Am)", "Español (La menor)",
                            "Como VirtualDJ en modo armónico (08A, en color)", "Open Key de Traktor (1m)"};
        ImGui::SetNextItemWidth(340 * s);
        ImGui::Combo("Notación", &settings_.keyNotation, kn, 6);
        ImGui::TextDisabled("Todas son la misma rueda: 8A = La menor = Am = 1m. Los colores son los de la rueda Camelot.");
        if (ImGui::Checkbox("Si el archivo trae la tonalidad en sus etiquetas, usarla", &settings_.useTagKeys)) {
            library_.setUseTagKeys(settings_.useTagKeys);
            if (settings_.useTagKeys) library_.checkTagKeys(true);
        }
        ImGui::SetItemTooltip("Mixed In Key, Beatport, rekordbox o VirtualDJ (si escribe etiquetas) la guardan en TKEY / INITIALKEY.\nSuele ser más fiable que la detección.");
        if (ImGui::Button("Importar de VirtualDJ: tonalidad, BPM, cues y listas…")) openVdjImport();
        ImGui::SetItemTooltip("Trae el análisis de VirtualDJ para ver las mismas tonalidades, BPM y cues que allí.");
    }
    ImGui::SeparatorText("Ayudas de mezcla");
    {
        ImGui::SetNextItemWidth(220 * s);
        ImGui::SliderInt("Aviso de final de pista", &settings_.endWarnSec, 0, 120, settings_.endWarnSec ? "últimos %d s" : "desactivado");
        ImGui::SetItemTooltip("El tiempo restante y la franja del plato parpadean en rojo cuando queda poco. 0 = sin aviso.");
        ImGui::TextDisabled("En la lista: BPM en verde = entra con menos de un 2 %% de pitch; marca verde junto a la tonalidad = encaja con lo que suena.");
    }
    ImGui::SeparatorText("Diseño");
    {
        static const int sizes[] = {90, 100, 110, 125, 150};
        int cur = 1;
        for (int i = 0; i < 5; ++i) if (settings_.uiScale == sizes[i]) cur = i;
        const char* sl[] = {"90 %", "100 % (como Windows)", "110 %", "125 %", "150 %"};
        ImGui::SetNextItemWidth(300 * s);
        if (ImGui::Combo("Tamaño de la interfaz", &cur, sl, 5)) settings_.uiScale = sizes[cur];
        ImGui::SetItemTooltip("Agranda o reduce textos y controles sobre la escala de Windows.\nÚtil en pantallas grandes (2560×1440) o portátiles pequeños.");
        int th = ui::currentThemeIndex();
        ImGui::SetNextItemWidth(300 * s);
        if (ImGui::BeginCombo("Tema", ui::themeAt(th).name)) {
            for (int i = 0; i < ui::themeCount(); ++i)
                if (ImGui::Selectable(ui::themeAt(i).name, i == th)) { settings_.theme = i; ui::applyTheme(i); }
            ImGui::EndCombo();
        }
        ImGui::Checkbox("Carátula en el centro de los platos", &settings_.coverInJog);
    }
    ImGui::SeparatorText("Formas de onda");
    {
        const char* ws[] = {"Color de cada plato (bandas en capas)", "Tres bandas: graves, medios y agudos", "Espectro de color (RGB)"};
        ImGui::SetNextItemWidth(300 * s);
        ImGui::Combo("Color", &settings_.waveStyle, ws, 3);
        ImGui::Checkbox("Ondas grandes arriba", &settings_.showWaves);
        ImGui::SameLine();
        ImGui::Checkbox("Rejilla de pulsos y compases", &settings_.waveGrid);
        ImGui::SameLine();
        ImGui::Checkbox("Cortes y subidones", &settings_.waveStructure);
    }
    ImGui::SeparatorText("SYNC");
    if (ImGui::Checkbox("Alinear compases completos (no solo pulsos)", &settings_.syncBar)) for (auto& dp : engine_.deck) dp.syncBar = settings_.syncBar;
    ImGui::TextDisabled("El plato con SYNC sigue el tempo y la fase del otro todo el rato, también sus empujones.\nSi lo empujas tú, se queda con el desfase que elijas.");
    ImGui::SeparatorText("Carátulas");
    {
        const char* cd[] = {"Solo cuando lo pido", "Lo que cargo en los platos", "Toda la colección, en segundo plano (como VirtualDJ)"};
        ImGui::SetNextItemWidth(340 * s);
        ImGui::Combo("Buscar carátulas en Internet", &settings_.coverDownload, cd, 3);
        ImGui::SetItemTooltip("Para las pistas sin imagen dentro ni en su carpeta. Se guardan aparte (el audio no se toca)\n"
                              "y se usan también las que VirtualDJ ya descargó (Documentos\\VirtualDJ\\Cache\\Covers).");
    }
    ImGui::TextDisabled("Se busca por artista y título (iTunes y Deezer) y se guarda aparte: el archivo de audio no se modifica.");
    ImGui::SeparatorText("Navegador");
    {
        int dens = settings_.rowDensity >= 0 ? settings_.rowDensity : (settings_.compactRows ? 1 : 0);
        const char* dn[] = {"Cómodas, con carátula", "Compactas", "Muy compactas (miles de pistas)"};
        ImGui::SetNextItemWidth(300 * s);
        if (ImGui::Combo("Filas de la lista", &dens, dn, 3)) settings_.rowDensity = dens;
    }
    ImGui::Checkbox("Al abrir una carpeta, leer también sus subcarpetas", &settings_.browseRecursive);
    ImGui::SeparatorText("Platos");
    ImGui::SliderFloat("Sensibilidad del scratch (ratón)",&settings_.mouseScratchSensitivity,0.25f,4.f,"%.2f×");
    ImGui::Checkbox("Pedir confirmación al cargar sobre un plato que suena", &settings_.confirmLoadWhilePlaying);
    if (NeonButton("Cerrar", col::Violet, false, ImVec2(120 * s, 28 * s))) showPrefs_ = false;
    ImGui::End();
}

void App::drawAboutDialog() {
    const float s = S();
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Acerca de DJ Elver Galarga", &showAbout_, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) { ImGui::End(); return; }
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    DrawAppLogo(dl, ImVec2(p.x, p.y + 22 * s), 44 * s, fontBold, fontHead, "0.11.3");
    ImGui::Dummy(ImVec2(0, 52 * s));
    ImGui::TextUnformatted("Aplicación de DJ para Windows 10/11 (64 bits).");
    ImGui::TextUnformatted("Software libre: licencia GPL versión 2 o posterior (COPYING.txt). El código fuente va con cada versión.");
    ImGui::Spacing();
    ImGui::TextDisabled("Componentes de terceros:");
    ImGui::BulletText("miniaudio (dominio público / MIT-0) — audio y decodificación");
    ImGui::BulletText("Dear ImGui (MIT) — interfaz");
    ImGui::BulletText("stb_vorbis (dominio público / MIT) — OGG Vorbis");
    ImGui::BulletText("nlohmann/json (MIT) — archivos de configuración");
    ImGui::BulletText("Fuentes Barlow Semi Condensed y Hanken Grotesk (SIL Open Font License)");
    ImGui::BulletText("Signalsmith Stretch y Signalsmith Linear (MIT) — key lock y transposición");
    ImGui::BulletText("SQLite (dominio público) — base de datos de la biblioteca");
    ImGui::BulletText("QuickJS-ng (MIT) — intérprete de los scripts de los mandos");
    ImGui::BulletText("Mapeos de mandos de Mixxx (GPL 2 o posterior) — mixxx.org");
    ImGui::BulletText("Separador (opcional, se instala aparte): Demucs (MIT), PyTorch (BSD), soundfile (BSD)");
    ImGui::Spacing();
    ImGui::TextDisabled("Datos: %s", toUtf8(appDataDir()).c_str());
    if (NeonButton("Cerrar", col::Violet, false, ImVec2(120 * s, 28 * s))) showAbout_ = false;
    ImGui::End();
}

} // namespace rdj

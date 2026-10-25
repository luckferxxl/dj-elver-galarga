// Ventana y espacio integrado ELVERKEY / MIX LAB dentro de DJ Elver Galarga:
// Rueda Camelot interactiva, buscador de combinaciones armónicas, banco de transiciones A->B,
// generador de sesiones por estilo, ideas guardadas, autogenerador de 8 Hot Cues y selector de color en vivo.
#include "app.h"
#include "ui_common.h"
#include "widgets.h"
#include "covers.h"
#include "../audio/analysis.h"
#include "../core/util.h"
#include <json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>

namespace rdj {

using namespace ui;
using namespace uic;
using json = nlohmann::json;

namespace {

struct MixIdeaItem {
    std::wstring pathA, pathB;
    std::string titleA, titleB, note;
    double exitSec = 0.0, entrySec = 0.0;
    int beats = 16, rating = 5, score = 90;
};

struct CandidateRow {
    std::wstring path;
    int score = 0;
    int harmScore = 0;
    std::string relation;
    double bpmDeltaPct = 0.0;
    double pitchAdjustPct = 0.0;
    int cueCount = 0;
};

struct ElverKeyState {
    bool initialized = false;
    int activeTab = 0;  // 0 = Descubrir, 1 = Banco A->B (Mix Lab), 2 = Auto-Set, 3 = Ideas y Herramientas
    bool showColorBar = false;
    float customDeckA[3] = {0.745f, 0.945f, 0.298f};
    float customDeckB[3] = {0.361f, 0.757f, 1.000f};
    float customBg[3] = {0.039f, 0.047f, 0.059f};

    // Referencia A y filtros de Descubrir
    std::wstring refPath;
    float maxBpmPct = 6.0f;
    bool sameStyle = false;
    bool allowHalfDouble = true;
    bool strictHarmony = false;
    int minScore = 70;
    int wheelKeyFilter = -1;  // -1 = usar la de Referencia A
    std::string styleFilter = "Todos los estilos";
    std::vector<CandidateRow> candidates;
    std::set<std::wstring> selectedCandidates;

    // Banco de transiciones A -> B
    std::wstring labPathA, labPathB;
    double labExitSec = 0.0, labEntrySec = 0.0;
    int labBeatsIdx = 1;  // 0->8, 1->16, 2->32
    int labRating = 5;
    char labNote[128] = "";

    // Generador Auto-Set
    std::string autoStyle = "Todos los estilos";
    int autoCurve = 0;  // 0 = Encaje armónico, 1 = Subida progresiva BPM, 2 = Calentamiento -> Pico -> Cierre
    int autoCount = 15;
    std::vector<std::wstring> autoSetPaths;
    char autoPlaylistName[128] = "Auto-Set ElverKey";

    // Selector de Playlist destino
    int targetPlaylistIdx = 0;
    char newPlaylistName[128] = "Mix Lab - ElverKey";
    bool appendToPlaylist = true;

    // Ideas guardadas
    std::vector<MixIdeaItem> ideas;
    bool ideasLoaded = false;

    // Editor manual de estilo
    char customStyleBuf[64] = "Tech House";
    char trackSearchBuf[96] = "";

    // Auditoría de duplicados y salud de la biblioteca
    struct DupPair { std::wstring pathA, pathB; std::string title, artist; double durA = 0, durB = 0; };
    struct HealthIssue { std::wstring path; std::string title, issue; double bpm = 0, dur = 0; int kind = 0; }; // kind: 1=low BPM, 2=high BPM, 3=short/unm
    std::vector<DupPair> dupPairs;
    std::vector<HealthIssue> healthIssues;
    bool auditDone = false;
    int auditSubTab = 0; // 0 = Duplicados, 1 = Salud / BPM mitad-doble
};

static ElverKeyState g_ek;

static std::wstring ideasFilePath() {
    return appDataDir() + L"\\ideas-mixlab.json";
}

static void loadIdeasIfNeeded() {
    if (g_ek.ideasLoaded) return;
    g_ek.ideasLoaded = true;
    std::ifstream f(std::filesystem::path(ideasFilePath()), std::ios::binary);
    if (!f) return;
    try {
        json j; f >> j;
        for (auto& item : j.value("ideas", json::array())) {
            MixIdeaItem m;
            m.pathA = fromUtf8(item.value("pathA", ""));
            m.pathB = fromUtf8(item.value("pathB", ""));
            m.titleA = item.value("titleA", "");
            m.titleB = item.value("titleB", "");
            m.note = item.value("note", "");
            m.exitSec = item.value("exitSec", 0.0);
            m.entrySec = item.value("entrySec", 0.0);
            m.beats = item.value("beats", 16);
            m.rating = item.value("rating", 5);
            m.score = item.value("score", 90);
            if (!m.pathA.empty() && !m.pathB.empty()) g_ek.ideas.push_back(m);
        }
    } catch (...) {}
}

static void saveIdeasToDisk() {
    json arr = json::array();
    for (const auto& m : g_ek.ideas) {
        arr.push_back({
            {"pathA", toUtf8(m.pathA)}, {"pathB", toUtf8(m.pathB)},
            {"titleA", m.titleA}, {"titleB", m.titleB}, {"note", m.note},
            {"exitSec", m.exitSec}, {"entrySec", m.entrySec},
            {"beats", m.beats}, {"rating", m.rating}, {"score", m.score}
        });
    }
    json j; j["ideas"] = arr;
    std::ofstream f(std::filesystem::path(ideasFilePath()), std::ios::binary | std::ios::trunc);
    if (f) f << j.dump(1);
}

// Puntuación armónica detallada 0..100 y nombre de la regla Camelot
static std::pair<int, std::string> camelotScore(int ka, int kb) {
    if (ka < 0 || ka >= 24 || kb < 0 || kb >= 24) return {50, "Sin tonalidad"};
    const int na = keyCamelotNumber(ka), nb = keyCamelotNumber(kb);
    const bool ma = keyIsMajor(ka), mb = keyIsMajor(kb);
    int d = (nb - na) % 12;
    if (d > 6) d -= 12;
    if (d < -6) d += 12;
    if (ma == mb) {
        if (d == 0) return {100, "Misma clave (=)"};
        if (d == 1) return {92, "Vecina dominante (+1)"};
        if (d == -1) return {92, "Vecina subdominante (-1)"};
        if (d == 2) return {72, "Subida de energía (+2)"};
        if (d == -2) return {62, "Bajada (-2)"};
        if (d == -5) return {68, "Subida semitono (+7)"};
        if (d == 5) return {52, "Bajada semitono (-7)"};
    } else {
        if (d == 0) return {88, "Relativa Mayor/Menor"};
        if ((!ma && d == 1) || (ma && d == -1)) return {70, "Cambio diagonal"};
    }
    return {15, "Choque armónico"};
}

static double tempoMatchPct(double bpmA, double bpmB, bool allowHalfDouble, double& outMult) {
    outMult = 1.0;
    if (bpmA <= 0.0 || bpmB <= 0.0) return 0.0;
    double r = bpmB / bpmA;
    double best = std::fabs(r - 1.0);
    if (allowHalfDouble) {
        for (double m : {2.0, 0.5}) {
            double d = std::fabs(r * m - 1.0);
            if (d < best) { best = d; outMult = m; }
        }
    }
    return best * 100.0;
}

static int countValidCues(const TrackMeta& m) {
    int c = 0;
    for (double s : m.hotCuesSec) if (s >= 0.0) ++c;
    return c;
}

// Genera 8 Hot Cues inteligentes cuantizados al compás si una pista no los tiene
static bool ensureEightHotCues(LibTrack& lt) {
    if (countValidCues(lt.meta) >= 4) return false;
    const double dur = lt.meta.durationSec > 10.0 ? lt.meta.durationSec : 240.0;
    const double bpm = lt.meta.bpm > 30.0 ? lt.meta.bpm : (lt.tagBpm > 30.0 ? lt.tagBpm : 126.0);
    const double beat = 60.0 / bpm;
    const double bar = 4.0 * beat;
    const double g0 = std::max(0.0, lt.meta.gridAnchorSec);
    const double fracs[8] = {0.04, 0.14, 0.26, 0.40, 0.54, 0.68, 0.78, 0.88};
    const char* labels[8] = {"Intro", "Subida", "Drop 1", "Break", "Drop 2", "Sección", "Drop 3", "Outro"};
    for (int i = 0; i < kNumHotCues; ++i) {
        double rawT = std::max(g0, dur * fracs[i]);
        double snapped = g0 + std::round((rawT - g0) / bar) * bar;
        snapped = std::clamp(snapped, 0.0, std::max(0.0, dur - 0.5));
        lt.meta.hotCuesSec[i] = snapped;
        lt.meta.hotCueNames[i] = labels[i];
    }
    if (lt.meta.cueSec <= 0.0) lt.meta.cueSec = lt.meta.hotCuesSec[0];
    return true;
}

static int compatibilityScore(const LibTrack& a, const LibTrack& b, bool allowHalfDouble,
                              int& outHarm, std::string& outRel, double& outDeltaPct, double& outAdjustPct) {
    auto [hScore, rel] = camelotScore(a.meta.key, b.meta.key);
    outHarm = hScore;
    outRel = rel;
    double mult = 1.0;
    const double bpmA = a.meta.bpm > 0 ? a.meta.bpm : a.tagBpm;
    const double bpmB = b.meta.bpm > 0 ? b.meta.bpm : b.tagBpm;
    outDeltaPct = tempoMatchPct(bpmA, bpmB, allowHalfDouble, mult);
    outAdjustPct = (bpmA > 0 && bpmB > 0) ? ((bpmA / (bpmB * mult)) - 1.0) * 100.0 : 0.0;
    double total = hScore * 0.65;
    if (bpmA > 0 && bpmB > 0) {
        total += 25.0 * std::max(0.0, 1.0 - outDeltaPct / 8.0) - (mult != 1.0 ? 3.0 : 0.0);
    } else {
        total += 12.0;
    }
    if (!a.genre.empty() && !b.genre.empty()) {
        if (foldForSearch(a.genre) == foldForSearch(b.genre)) total += 10.0;
        else total += 4.0;
    } else {
        total += 5.0;
    }
    return std::clamp((int)std::lround(total), 0, 100);
}

static void suggestTransitionPoints(const LibTrack& a, const LibTrack& b, int beats, double& outExitA, double& outEntryB) {
    const double bpmA = a.meta.bpm > 0 ? a.meta.bpm : 126.0;
    const double bpmB = b.meta.bpm > 0 ? b.meta.bpm : 126.0;
    const double fadeSec = beats * 60.0 / bpmA;
    const double durA = a.meta.durationSec > 30.0 ? a.meta.durationSec : 240.0;
    outExitA = std::max(0.0, durA - fadeSec - 16.0 * 60.0 / bpmA);
    for (int i = kNumHotCues - 1; i >= 0; --i) {
        double c = a.meta.hotCuesSec[i];
        if (c >= durA * 0.50 && c <= durA - fadeSec - 1.0) {
            outExitA = c;
            break;
        }
    }
    outEntryB = std::max(0.0, b.meta.gridAnchorSec);
    if (b.meta.hotCuesSec[0] >= 0.0 && b.meta.hotCuesSec[0] < 45.0) {
        outEntryB = b.meta.hotCuesSec[0];
    }
    (void)bpmB;
}

} // namespace

void App::drawElverKeyPanel(ImVec2 a, ImVec2 b) {
    const float s = S();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    Panel(dl, a, b, col::Radius * s, col::Panel, col::Line);
    BeginPanel("elverkey_docked", a, b, 10 * s, true);
    drawElverKeyContent();
    ImGui::EndChild();
}

void App::drawElverKeyWindow() {
    const float s = S();
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(1120 * s, 680 * s), ImGuiCond_FirstUseEver);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, toVec4(col::Panel));
    if (ImGui::Begin("ELVERKEY STUDIO / MIX LAB · Preparador Armónico y Banco A→B", &showElverKey_, ImGuiWindowFlags_NoCollapse)) {
        drawElverKeyContent();
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

void App::drawElverKeyContent() {
    const float s = S();
    loadIdeasIfNeeded();

    std::lock_guard<std::recursive_mutex> lk(library_.mutex());
    auto& tracks = library_.tracks();
    auto& playlists = library_.playlists();

    // Inicializar Referencia A y Banco A/B si están vacíos
    if (g_ek.refPath.empty() || !library_.find(g_ek.refPath)) {
        if (!deck_[0].path.empty() && library_.find(deck_[0].path)) g_ek.refPath = deck_[0].path;
        else if (!deck_[1].path.empty() && library_.find(deck_[1].path)) g_ek.refPath = deck_[1].path;
        else if (!libSelectedPath_.empty() && library_.find(libSelectedPath_)) g_ek.refPath = libSelectedPath_;
        else if (!tracks.empty()) g_ek.refPath = tracks[0].path;
    }
    if (g_ek.labPathA.empty() || !library_.find(g_ek.labPathA)) g_ek.labPathA = g_ek.refPath;
    if ((g_ek.labPathB.empty() || !library_.find(g_ek.labPathB)) && tracks.size() > 1) {
        for (const auto& t : tracks) if (t.path != g_ek.labPathA) { g_ek.labPathB = t.path; break; }
        if (const LibTrack* ta = library_.find(g_ek.labPathA))
            if (const LibTrack* tb = library_.find(g_ek.labPathB))
                suggestTransitionPoints(*ta, *tb, 16, g_ek.labExitSec, g_ek.labEntrySec);
    }

    // ------------------------------------------------------------------ CABECERA: Pestañas + Selector de Tema y Color en vivo
    {
        const char* tabNames[4] = {
            "01 · DESCUBRIR MEZCLAS",
            "02 · BANCO A→B (MIX LAB)",
            "03 · AUTO-SET POR ESTILO",
            "04 · IDEAS Y HERRAMIENTAS"
        };
        for (int i = 0; i < 4; ++i) {
            if (i > 0) ImGui::SameLine(0, 6 * s);
            const float tw = fontHead->CalcTextSizeA(13.5f * s, FLT_MAX, 0, tabNames[i]).x + 24 * s;
            if (NeonButton(tabNames[i], col::DeckA, g_ek.activeTab == i, ImVec2(tw, 28 * s), true, fontHead, 13.5f * s)) {
                g_ek.activeTab = i;
            }
        }

        // Botones a la derecha: Tema/Color y acoplar/ventana
        ImGui::SameLine(0, 14 * s);
        if (NeonButton("🎨 Cambiar Color / Tema", col::Amber, g_ek.showColorBar, ImVec2(178 * s, 28 * s), true, fontHead, 13 * s)) {
            g_ek.showColorBar = !g_ek.showColorBar;
            ImVec4 da = toVec4(col::DeckA), db = toVec4(col::DeckB), bg = toVec4(col::Panel);
            g_ek.customDeckA[0] = da.x; g_ek.customDeckA[1] = da.y; g_ek.customDeckA[2] = da.z;
            g_ek.customDeckB[0] = db.x; g_ek.customDeckB[1] = db.y; g_ek.customDeckB[2] = db.z;
            g_ek.customBg[0] = bg.x;    g_ek.customBg[1] = bg.y;    g_ek.customBg[2] = bg.z;
        }
        ImGui::SameLine(0, 6 * s);
        if (mode_ == 3) {
            if (NeonButton("Abrir en ventana flotante", col::Violet, showElverKey_, ImVec2(180 * s, 28 * s), true, fontHead, 13 * s)) {
                showElverKey_ = true;
                mode_ = 0;
            }
        } else {
            if (NeonButton("Acoplar arriba en pestaña", col::Violet, false, ImVec2(180 * s, 28 * s), true, fontHead, 13 * s)) {
                mode_ = 3;
                showElverKey_ = false;
            }
        }
    }

    // Barra desplegable de Temas y Colores personalizados en vivo
    if (g_ek.showColorBar) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, toVec4(col::Panel2));
        ImGui::BeginChild("colorbar_ek", ImVec2(0, 68 * s), ImGuiChildFlags_Borders);
        ImGui::TextColored(toVec4(col::Amber), "TEMAS DE COLOR (cambia toda la aplicación y esta ventana al instante):");
        for (int i = 0; i < themeCount(); ++i) {
            if (i > 0) ImGui::SameLine(0, 5 * s);
            const Theme& th = themeAt(i);
            const bool active = (settings_.theme == i);
            ImGui::PushID(i);
            if (NeonButton(th.name, th.deckA, active, ImVec2(0, 24 * s), true, fontHead, 12 * s)) {
                settings_.theme = i;
                applyTheme(i);
                settings_.save();
                ImVec4 da = toVec4(col::DeckA), db = toVec4(col::DeckB), bg = toVec4(col::Panel);
                g_ek.customDeckA[0] = da.x; g_ek.customDeckA[1] = da.y; g_ek.customDeckA[2] = da.z;
                g_ek.customDeckB[0] = db.x; g_ek.customDeckB[1] = db.y; g_ek.customDeckB[2] = db.z;
                g_ek.customBg[0] = bg.x;    g_ek.customBg[1] = bg.y;    g_ek.customBg[2] = bg.z;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", th.description);
            ImGui::PopID();
        }
        ImGui::SameLine(0, 14 * s);
        ImGui::SetNextItemWidth(110 * s);
        if (ImGui::ColorEdit3("Color A", g_ek.customDeckA, ImGuiColorEditFlags_NoInputs)) {
            col::DeckA = IM_COL32((int)(g_ek.customDeckA[0]*255), (int)(g_ek.customDeckA[1]*255), (int)(g_ek.customDeckA[2]*255), 255);
            col::Violet = col::DeckA;
        }
        ImGui::SameLine(0, 8 * s);
        ImGui::SetNextItemWidth(110 * s);
        if (ImGui::ColorEdit3("Color B", g_ek.customDeckB, ImGuiColorEditFlags_NoInputs)) {
            col::DeckB = IM_COL32((int)(g_ek.customDeckB[0]*255), (int)(g_ek.customDeckB[1]*255), (int)(g_ek.customDeckB[2]*255), 255);
        }
        ImGui::SameLine(0, 8 * s);
        ImGui::SetNextItemWidth(110 * s);
        if (ImGui::ColorEdit3("Fondo", g_ek.customBg, ImGuiColorEditFlags_NoInputs)) {
            col::Panel = IM_COL32((int)(g_ek.customBg[0]*255), (int)(g_ek.customBg[1]*255), (int)(g_ek.customBg[2]*255), 255);
            col::Bg = col::mix(col::Panel, IM_COL32(0, 0, 0, 255), 0.35f);
            col::Panel2 = col::mix(col::Panel, IM_COL32(255, 255, 255, 255), 0.06f);
            ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = toVec4(col::Bg);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // Barra de estado en vivo de DECK A (Plato 1) y DECK B (Plato 2) con sus colores diferenciados
    const std::wstring pathDeckA = deck_[0].path;
    const std::wstring pathDeckB = deck_[1].path;
    const bool playingDeckA = !pathDeckA.empty() && engine_.deckStatus[0].playing.load();
    const bool playingDeckB = !pathDeckB.empty() && engine_.deckStatus[1].playing.load();
    {
        ImGui::Spacing();
        const float stripW = ImGui::GetContentRegionAvail().x;
        const float boxW = (stripW - 8 * s) * 0.5f;
        for (int d = 0; d < 2; ++d) {
            if (d == 1) ImGui::SameLine(0, 8 * s);
            const std::wstring& dPath = (d == 0) ? pathDeckA : pathDeckB;
            const bool dPlay = (d == 0) ? playingDeckA : playingDeckB;
            const ImU32 dCol = (d == 0) ? col::DeckA : col::DeckB;
            const LibTrack* dt = dPath.empty() ? nullptr : library_.find(dPath);
            std::string badgeTxt = d == 0 ? (dPlay ? "DECK A ▶ SONANDO: " : "DECK A ⏸ CARGADO: ")
                                          : (dPlay ? "DECK B ▶ SONANDO: " : "DECK B ⏸ CARGADO: ");
            if (dt) {
                badgeTxt += (dt->artist.empty() ? "" : dt->artist + " — ") + dt->title +
                            "  [" + (dt->meta.key >= 0 ? std::string(keyCamelot(dt->meta.key)) : "—") +
                            " · " + (dt->meta.bpm > 0 ? std::to_string((int)std::lround(engine_.effectiveBpm(d))) + " BPM" : "—") + "]";
            } else if (!dPath.empty()) {
                badgeTxt += deck_[d].title;
            } else {
                badgeTxt = d == 0 ? "DECK A: Sin pista cargada" : "DECK B: Sin pista cargada";
            }
            ImGui::PushID(d + 7700);
            if (NeonButton(badgeTxt.c_str(), dCol, dPlay, ImVec2(boxW, 24 * s), !dPath.empty(), fontHead, 12 * s)) {
                if (g_ek.activeTab == 1) {
                    if (d == 0) g_ek.labPathA = dPath;
                    else g_ek.labPathB = dPath;
                } else {
                    g_ek.refPath = dPath;
                }
            }
            if (ImGui::IsItemHovered() && !dPath.empty()) {
                ImGui::SetTooltip("Canción en %s (%s).\nClic: usar como referencia / pista en Mix Lab.",
                                  d == 0 ? "DECK A (Plato 1)" : "DECK B (Plato 2)",
                                  dPlay ? "sonando ahora" : "en pausa");
            }
            ImGui::PopID();
        }
    }

    // Helper para resaltar y diferenciar en cualquier tabla la fila que está en DECK A vs DECK B
    auto decorateDeckRow = [&](const std::wstring& rowPath, const std::string& baseTitle, ImU32& outTextCol) -> std::string {
        const bool isA = (!pathDeckA.empty() && rowPath == pathDeckA);
        const bool isB = (!pathDeckB.empty() && rowPath == pathDeckB);
        if (isA && isB) {
            const ImU32 bg = col::withAlpha(col::DeckA, (playingDeckA || playingDeckB) ? 78 : 46);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, bg);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, bg);
            outTextCol = col::DeckA;
            return std::string((playingDeckA || playingDeckB) ? "[DECK A+B ▶] " : "[DECK A+B] ") + baseTitle;
        }
        if (isA) {
            const ImU32 bg = col::withAlpha(col::DeckA, playingDeckA ? 78 : 45);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, bg);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, bg);
            outTextCol = col::DeckA;
            return std::string(playingDeckA ? "[DECK A ▶] " : "[DECK A] ") + baseTitle;
        }
        if (isB) {
            const ImU32 bg = col::withAlpha(col::DeckB, playingDeckB ? 78 : 45);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, bg);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, bg);
            outTextCol = col::DeckB;
            return std::string(playingDeckB ? "[DECK B ▶] " : "[DECK B] ") + baseTitle;
        }
        outTextCol = col::Text;
        return baseTitle;
    };

    ImGui::Separator();

    // Recopilar lista de estilos únicos presentes en la biblioteca
    std::vector<std::string> uniqueStyles = {"Todos los estilos"};
    {
        std::set<std::string> stSet;
        for (const auto& t : tracks) if (!t.genre.empty()) stSet.insert(t.genre);
        for (const auto& g : stSet) uniqueStyles.push_back(g);
    }

    if (!g_ek.initialized && !playlists.empty()) {
        g_ek.initialized = true;
        int bestPl = -1;
        if (src_.kind == Src::Lista && src_.list >= 0 && src_.list < (int)playlists.size()) bestPl = src_.list;
        if (bestPl < 0) {
            for (int i = 0; i < (int)playlists.size(); ++i) {
                if (playlists[i].name.find("sesi") != std::string::npos || playlists[i].name.find("Sesi") != std::string::npos || playlists[i].name == "Mix Lab - ElverKey") {
                    bestPl = i; break;
                }
            }
        }
        if (bestPl < 0) {
            for (int i = 0; i < (int)playlists.size(); ++i) {
                if (playlists[i].name != "Grabaciones" && playlists[i].name.find("Historial") == std::string::npos) { bestPl = i; break; }
            }
        }
        if (bestPl >= 0) g_ek.targetPlaylistIdx = bestPl;
    }

    // Selector común de Playlist de destino para enviar con 1 clic
    auto drawPlaylistDestinationBar = [&](const std::vector<std::wstring>& pathsToSend, const char* btnLabel) {
        if (playlists.empty()) ensurePlaylist("Mix Lab - ElverKey");
        g_ek.targetPlaylistIdx = std::clamp(g_ek.targetPlaylistIdx, 0, std::max(0, (int)playlists.size() - 1));
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(toVec4(col::Amber), "Destino Playlist:");
        ImGui::SameLine(0, 6 * s);
        ImGui::SetNextItemWidth(180 * s);
        const char* curPlName = playlists.empty() ? "Mix Lab - ElverKey" : playlists[g_ek.targetPlaylistIdx].name.c_str();
        if (ImGui::BeginCombo("##destpl_combo", curPlName)) {
            for (int i = 0; i < (int)playlists.size(); ++i) {
                const bool sel = (g_ek.targetPlaylistIdx == i);
                if (ImGui::Selectable(playlists[i].name.c_str(), sel)) g_ek.targetPlaylistIdx = i;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine(0, 6 * s);
        if (NeonButton(btnLabel, col::Green, true, ImVec2(215 * s, 26 * s), !pathsToSend.empty(), fontHead, 12.5f * s)) {
            const int dstIdx = g_ek.targetPlaylistIdx;
            if (dstIdx >= 0 && dstIdx < (int)playlists.size()) {
                library_.addToPlaylist(dstIdx, pathsToSend);
                library_.save();
                notify(std::to_string(pathsToSend.size()) + " pista(s) añadidas a la playlist «" + playlists[dstIdx].name + "» ⚡", false);
            }
        }
        ImGui::SameLine(0, 6 * s);
        ImGui::SetNextItemWidth(150 * s);
        ImGui::InputTextWithHint("##newpl_ek", "Nueva playlist…", g_ek.newPlaylistName, sizeof g_ek.newPlaylistName);
        ImGui::SameLine(0, 4 * s);
        if (NeonButton("+ Crear y enviar", col::DeckA, false, ImVec2(130 * s, 26 * s), !pathsToSend.empty() && g_ek.newPlaylistName[0] != 0, fontHead, 12.5f * s)) {
            const int idx = ensurePlaylist(g_ek.newPlaylistName);
            g_ek.targetPlaylistIdx = idx;
            library_.addToPlaylist(idx, pathsToSend);
            library_.save();
            BrowseSrc bs; bs.kind = Src::Lista; bs.list = idx; selectSource(bs);
            notify("Playlist «" + std::string(g_ek.newPlaylistName) + "» creada con " + std::to_string(pathsToSend.size()) + " pista(s) ⚡", false);
        }
    };

    // ================================================================== PESTAÑA 1: DESCUBRIR MEZCLAS
    if (g_ek.activeTab == 0) {
        const LibTrack* refTrack = library_.find(g_ek.refPath);

        // Fila superior: Referencia A + botones para tomar de Plato A/B o Biblioteca + filtros
        ImGui::BeginGroup();
        ImGui::TextColored(toVec4(col::DeckA), "REFERENCIA A:");
        ImGui::SameLine(0, 6 * s);
        if (refTrack) {
            const std::string refDesc = (refTrack->artist.empty() ? "" : refTrack->artist + " — ") + refTrack->title +
                                        "   [" + (refTrack->meta.key >= 0 ? std::string(keyCamelot(refTrack->meta.key)) : "—") +
                                        " · " + (refTrack->meta.bpm > 0 ? std::to_string((int)std::lround(refTrack->meta.bpm)) + " BPM" : "—") +
                                        " · " + (refTrack->genre.empty() ? "Sin estilo" : refTrack->genre) + "]";
            ImGui::TextUnformatted(refDesc.c_str());
        } else {
            ImGui::TextDisabled("Selecciona una pista en la biblioteca o en un plato");
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("RDJ_PISTA")) {
                auto ps = uic::PayloadPaths(pl);
                if (!ps.empty()) g_ek.refPath = ps[0];
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine(0, 12 * s);
        if (ImGui::SmallButton("Elegir canción…##pick_ref")) ImGui::OpenPopup("ek_pick_ref_popup");
        if (ImGui::BeginPopup("ek_pick_ref_popup")) {
            ImGui::SetNextItemWidth(280 * s);
            ImGui::InputTextWithHint("##srch_ref", "Buscar canción, artista o estilo…", g_ek.trackSearchBuf, sizeof g_ek.trackSearchBuf);
            const std::string q = foldForSearch(g_ek.trackSearchBuf);
            ImGui::BeginChild("ref_list_pop", ImVec2(460 * s, 260 * s));
            int shown = 0;
            for (const auto& t : tracks) {
                if (t.missing) continue;
                if (!q.empty() && t.searchKey.find(q) == std::string::npos) continue;
                std::string lbl = (t.artist.empty() ? "" : t.artist + " - ") + t.title +
                                  "  [" + (t.meta.key >= 0 ? std::string(keyCamelot(t.meta.key)) : "—") +
                                  " · " + (t.meta.bpm > 0 ? std::to_string((int)std::lround(t.meta.bpm)) : "—") + " · " + t.genre + "]##" + std::to_string(shown);
                if (ImGui::Selectable(lbl.c_str(), t.path == g_ek.refPath)) {
                    g_ek.refPath = t.path;
                    ImGui::CloseCurrentPopup();
                }
                if (++shown >= 120) break;
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }
        ImGui::SameLine(0, 4 * s);
        if (ImGui::SmallButton("Tomar Plato A") && !deck_[0].path.empty()) g_ek.refPath = deck_[0].path;
        ImGui::SameLine(0, 4 * s);
        if (ImGui::SmallButton("Tomar Plato B") && !deck_[1].path.empty()) g_ek.refPath = deck_[1].path;
        ImGui::EndGroup();

        // Barra de filtros de compatibilidad
        ImGui::SetNextItemWidth(115 * s);
        ImGui::SliderFloat("BPM máx", &g_ek.maxBpmPct, 1.0f, 15.0f, "±%.1f %%");
        ImGui::SameLine(0, 10 * s);
        ImGui::SetNextItemWidth(105 * s);
        ImGui::SliderInt("Afinidad mín", &g_ek.minScore, 50, 95, "%d pts");
        ImGui::SameLine(0, 10 * s);
        ImGui::Checkbox("Mismo estilo", &g_ek.sameStyle);
        ImGui::SameLine(0, 8 * s);
        ImGui::Checkbox("Mitad/doble BPM", &g_ek.allowHalfDouble);
        ImGui::SameLine(0, 8 * s);
        ImGui::Checkbox("Armonía ≥85", &g_ek.strictHarmony);
        ImGui::SameLine(0, 10 * s);
        ImGui::SetNextItemWidth(160 * s);
        if (ImGui::BeginCombo("Estilo##ek_stf", g_ek.styleFilter.c_str())) {
            for (const auto& st : uniqueStyles) {
                if (ImGui::Selectable(st.c_str(), g_ek.styleFilter == st)) g_ek.styleFilter = st;
            }
            ImGui::EndCombo();
        }

        // Calcular candidatas compatibles
        g_ek.candidates.clear();
        refTrack = library_.find(g_ek.refPath);
        if (refTrack) {
            const int targetKey = (g_ek.wheelKeyFilter >= 0) ? g_ek.wheelKeyFilter : refTrack->meta.key;
            for (const auto& cand : tracks) {
                if (cand.path == refTrack->path || cand.missing) continue;
                if (g_ek.styleFilter != "Todos los estilos" && foldForSearch(cand.genre) != foldForSearch(g_ek.styleFilter)) continue;
                if (g_ek.sameStyle && !refTrack->genre.empty() && foldForSearch(cand.genre) != foldForSearch(refTrack->genre)) continue;
                LibTrack fakeRef = *refTrack;
                fakeRef.meta.key = targetKey;
                int hSc = 0; std::string rel; double dPct = 0, adjPct = 0;
                int sc = compatibilityScore(fakeRef, cand, g_ek.allowHalfDouble, hSc, rel, dPct, adjPct);
                if (dPct > g_ek.maxBpmPct && (fakeRef.meta.bpm > 0 && cand.meta.bpm > 0)) continue;
                if (g_ek.strictHarmony && hSc < 85) continue;
                if (sc < g_ek.minScore) continue;
                g_ek.candidates.push_back(CandidateRow{cand.path, sc, hSc, rel, dPct, adjPct, countValidCues(cand.meta)});
            }
            std::sort(g_ek.candidates.begin(), g_ek.candidates.end(), [&](const CandidateRow& x, const CandidateRow& y) {
                const bool xDeck = (!pathDeckA.empty() && x.path == pathDeckA) || (!pathDeckB.empty() && x.path == pathDeckB);
                const bool yDeck = (!pathDeckA.empty() && y.path == pathDeckA) || (!pathDeckB.empty() && y.path == pathDeckB);
                if (xDeck != yDeck) return xDeck > yDeck;
                if (x.score != y.score) return x.score > y.score;
                return x.bpmDeltaPct < y.bpmDeltaPct;
            });
            // Asegurar que las canciones cargadas en Deck A y Deck B aparezcan siempre al principio de la tabla para verlas diferenciadas
            for (int d = 1; d >= 0; --d) {
                const std::wstring& dp = (d == 0) ? pathDeckA : pathDeckB;
                if (dp.empty()) continue;
                const LibTrack* dt = library_.find(dp);
                if (!dt) continue;
                auto it = std::find_if(g_ek.candidates.begin(), g_ek.candidates.end(), [&](const CandidateRow& r) { return r.path == dp; });
                int hSc = 100; std::string rel = (d == 0 ? "En DECK A" : "En DECK B"); double dPct = 0, adjPct = 0;
                int sc = compatibilityScore(*refTrack, *dt, g_ek.allowHalfDouble, hSc, rel, dPct, adjPct);
                CandidateRow row{dp, sc, hSc, (d == 0 ? (playingDeckA ? "▶ SONANDO EN DECK A" : "⏸ CARGADA EN DECK A") : (playingDeckB ? "▶ SONANDO EN DECK B" : "⏸ CARGADA EN DECK B")), dPct, adjPct, countValidCues(dt->meta)};
                if (it != g_ek.candidates.end()) g_ek.candidates.erase(it);
                g_ek.candidates.insert(g_ek.candidates.begin(), row);
            }
        }

        const float availH = std::max(140 * s, ImGui::GetContentRegionAvail().y - 34 * s);
        const float wheelBoxW = 195 * s;

        // Columna izquierda: Rueda Camelot interactiva
        ImGui::BeginChild("wheel_col_ek", ImVec2(wheelBoxW, availH), ImGuiChildFlags_None);
        {
            ImDrawList* wdl = ImGui::GetWindowDrawList();
            const ImVec2 wp = ImGui::GetCursorScreenPos();
            const float wheelSz = 176 * s;
            const ImVec2 wc(wp.x + wheelSz * 0.5f, wp.y + wheelSz * 0.5f);
            const float rOut = wheelSz * 0.48f, rMid = wheelSz * 0.33f, rIn = wheelSz * 0.18f;
            const int activeKey = (g_ek.wheelKeyFilter >= 0) ? g_ek.wheelKeyFilter : (refTrack ? refTrack->meta.key : -1);

            for (int n = 1; n <= 12; ++n) {
                const float angDeg = 90.f - (n % 12) * 30.f;
                const float ang = angDeg * (3.14159265f / 180.f);
                for (int major = 1; major >= 0; --major) {
                    const int kIdx = keyFromCamelot(n, major == 1);
                    const float r0 = major ? rMid + 1.5f * s : rIn;
                    const float r1 = major ? rOut : rMid - 1.5f * s;
                    const float rm = (r0 + r1) * 0.5f;
                    const ImVec2 sc(wc.x + rm * std::cos(ang), wc.y - rm * std::sin(ang));
                    ImU32 baseCol = col::camelot(n - 1);
                    bool compat = (activeKey < 0);
                    if (activeKey >= 0) {
                        int hs = camelotScore(activeKey, kIdx).first;
                        compat = (hs >= 85);
                        if (!compat) baseCol = col::withAlpha(baseCol, 55);
                    }
                    wdl->AddCircleFilled(sc, (r1 - r0) * 0.48f, baseCol, 18);
                    if (kIdx == activeKey) wdl->AddCircle(sc, (r1 - r0) * 0.50f, col::Text, 18, 2.0f * s);
                    const int keyDeckA = (!pathDeckA.empty() && deck_[0].meta.key >= 0) ? soundingKey(0) : -1;
                    const int keyDeckB = (!pathDeckB.empty() && deck_[1].meta.key >= 0) ? soundingKey(1) : -1;
                    if (kIdx == keyDeckA) wdl->AddCircle(sc, (r1 - r0) * 0.56f, col::DeckA, 18, 2.5f * s);
                    if (kIdx == keyDeckB) wdl->AddCircle(sc, (r1 - r0) * 0.62f, col::DeckB, 18, 2.5f * s);
                    const char* code = keyCamelot(kIdx);
                    CenterText(wdl, ImVec2(sc.x - 12 * s, sc.y - 7 * s), ImVec2(sc.x + 12 * s, sc.y + 7 * s),
                               compat ? col::onColor(baseCol) : col::TextDim, code, fontHead, 10.5f * s);
                }
            }
            wdl->AddCircleFilled(wc, rIn - 2 * s, col::Bg, 24);
            const char* centerTxt = activeKey >= 0 ? keyCamelot(activeKey) : "CLAVE";
            CenterText(wdl, ImVec2(wc.x - rIn, wc.y - 8 * s), ImVec2(wc.x + rIn, wc.y + 8 * s), col::DeckA, centerTxt, fontBold, 14 * s);

            ImGui::InvisibleButton("##wheel_hit_ek", ImVec2(wheelSz, wheelSz));
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                const ImVec2 mp = ImGui::GetIO().MousePos;
                const float dx = mp.x - wc.x, dy = wc.y - mp.y;
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (dist < rIn) {
                    g_ek.wheelKeyFilter = -1;
                } else if (dist <= rOut) {
                    float deg = std::atan2(dy, dx) * (180.f / 3.14159265f);
                    int num = ((int)std::lround((90.f - deg) / 30.f) % 12 + 12) % 12;
                    if (num == 0) num = 12;
                    bool maj = (dist >= rMid);
                    int clickedK = keyFromCamelot(num, maj);
                    g_ek.wheelKeyFilter = (g_ek.wheelKeyFilter == clickedK) ? -1 : clickedK;
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Clic en una clave para filtrar compatibles · Clic en el centro para usar la clave de la Referencia A");
            }
            ImGui::TextDisabled("%zu compatibles", g_ek.candidates.size());
        }
        ImGui::EndChild();

        ImGui::SameLine(0, 8 * s);

        // Columna derecha: Tabla de candidatas compatibles (con Drag & Drop "RDJ_PISTA" hacia cualquier Playlist o Plato)
        ImGui::BeginChild("cand_table_ek", ImVec2(0, availH), ImGuiChildFlags_None);
        if (ImGui::BeginTable("ek_cands", 8, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("AFINIDAD", ImGuiTableColumnFlags_WidthFixed, 68 * s);
            ImGui::TableSetupColumn("TEMA", ImGuiTableColumnFlags_None, 1.6f);
            ImGui::TableSetupColumn("ARTISTA", ImGuiTableColumnFlags_None, 1.0f);
            ImGui::TableSetupColumn("ESTILO", ImGuiTableColumnFlags_None, 0.9f);
            ImGui::TableSetupColumn("BPM", ImGuiTableColumnFlags_WidthFixed, 58 * s);
            ImGui::TableSetupColumn("AJUSTE", ImGuiTableColumnFlags_WidthFixed, 64 * s);
            ImGui::TableSetupColumn("CLAVE", ImGuiTableColumnFlags_WidthFixed, 64 * s);
            ImGui::TableSetupColumn("RELACIÓN / CUES", ImGuiTableColumnFlags_None, 1.2f);
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < g_ek.candidates.size() && i < 250; ++i) {
                const CandidateRow& cr = g_ek.candidates[i];
                const LibTrack* ct = library_.find(cr.path);
                if (!ct) continue;
                ImGui::PushID((int)i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const bool isSel = g_ek.selectedCandidates.count(cr.path) > 0;
                char scBuf[32]; std::snprintf(scBuf, sizeof scBuf, "%d / 100", cr.score);
                if (ImGui::Selectable(scBuf, isSel, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::GetIO().KeyCtrl) {
                        if (isSel) g_ek.selectedCandidates.erase(cr.path);
                        else g_ek.selectedCandidates.insert(cr.path);
                    } else {
                        g_ek.selectedCandidates.clear();
                        g_ek.selectedCandidates.insert(cr.path);
                    }
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        g_ek.labPathA = g_ek.refPath;
                        g_ek.labPathB = cr.path;
                        if (refTrack) suggestTransitionPoints(*refTrack, *ct, 16, g_ek.labExitSec, g_ek.labEntrySec);
                        g_ek.activeTab = 1;
                    }
                }
                // Arrastrar selección directamente a cualquier Playlist del navegador, Sidelist, Automix o Platos A/B
                if (ImGui::BeginDragDropSource()) {
                    std::vector<std::wstring> dragList;
                    if (!g_ek.selectedCandidates.empty() && g_ek.selectedCandidates.count(cr.path)) {
                        for (const auto& r : g_ek.candidates) if (g_ek.selectedCandidates.count(r.path)) dragList.push_back(r.path);
                    } else {
                        dragList.push_back(cr.path);
                    }
                    uic::SetPathsPayload(dragList);
                    ImGui::Text("Arrastrando %zu pista(s) — suelta en una Playlist, Sidelist o Plato A/B", dragList.size());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginPopupContextItem("ctx_ek_cand")) {
                    if (ImGui::MenuItem("Planificar transición A→B en Mix Lab")) {
                        g_ek.labPathA = g_ek.refPath; g_ek.labPathB = cr.path;
                        if (refTrack) suggestTransitionPoints(*refTrack, *ct, 16, g_ek.labExitSec, g_ek.labEntrySec);
                        g_ek.activeTab = 1;
                    }
                    if (ImGui::MenuItem("Cargar en el Plato A")) loadToDeck(0, cr.path);
                    if (ImGui::MenuItem("Cargar en el Plato B")) loadToDeck(1, cr.path);
                    if (ImGui::MenuItem("Usar como nueva Referencia A")) g_ek.refPath = cr.path;
                    ImGui::Separator();
                    if (ImGui::BeginMenu("Enviar a Playlist")) {
                        for (int pi = 0; pi < (int)playlists.size(); ++pi) {
                            if (ImGui::MenuItem(playlists[pi].name.c_str())) {
                                std::vector<std::wstring> toAdd = {cr.path};
                                if (!g_ek.selectedCandidates.empty() && g_ek.selectedCandidates.count(cr.path)) {
                                    toAdd.clear();
                                    for (const auto& r : g_ek.candidates) if (g_ek.selectedCandidates.count(r.path)) toAdd.push_back(r.path);
                                }
                                library_.addToPlaylist(pi, toAdd);
                                library_.save();
                                notify(std::to_string(toAdd.size()) + " pista(s) añadidas a «" + playlists[pi].name + "» ⚡", false);
                            }
                        }
                        ImGui::EndMenu();
                    }
                    ImGui::EndPopup();
                }

                ImGui::TableNextColumn();
                {
                    ImU32 rowTCol = col::Text;
                    const std::string decTitle = decorateDeckRow(cr.path, ct->title, rowTCol);
                    ImGui::TextColored(toVec4(rowTCol), "%s", decTitle.c_str());
                }
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", ct->artist.c_str());
                ImGui::TableNextColumn();
                ImGui::TextColored(toVec4(col::mix(col::DeckA, col::Text, 0.55f)), "%s", ct->genre.empty() ? "—" : ct->genre.c_str());
                ImGui::TableNextColumn();
                if (ct->meta.bpm > 0) ImGui::Text("%.1f", ct->meta.bpm); else ImGui::TextDisabled("—");
                ImGui::TableNextColumn();
                ImGui::Text("%+.1f%%", cr.pitchAdjustPct);
                ImGui::TableNextColumn();
                if (ct->meta.key >= 0) ImGui::TextColored(toVec4(col::camelot(camelotIndex(ct->meta.key))), "%s", keyCamelot(ct->meta.key));
                else ImGui::TextDisabled("—");
                ImGui::TableNextColumn();
                ImGui::Text("%s · %d cues", cr.relation.c_str(), cr.cueCount);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        // Barra inferior de acciones rápidas de Descubrir
        std::vector<std::wstring> selPaths;
        if (!g_ek.refPath.empty()) selPaths.push_back(g_ek.refPath);
        for (const auto& r : g_ek.candidates) if (g_ek.selectedCandidates.count(r.path)) selPaths.push_back(r.path);
        if (selPaths.size() <= 1 && !g_ek.candidates.empty()) {
            for (size_t i = 0; i < std::min<size_t>(5, g_ek.candidates.size()); ++i) selPaths.push_back(g_ek.candidates[i].path);
        }
        if (NeonButton("Abrir pareja en Banco A→B", col::DeckA, false, ImVec2(175 * s, 26 * s), !g_ek.candidates.empty(), fontHead, 12.5f * s)) {
            g_ek.labPathA = g_ek.refPath;
            g_ek.labPathB = (!g_ek.selectedCandidates.empty()) ? *g_ek.selectedCandidates.begin() : g_ek.candidates[0].path;
            if (const LibTrack* ta = library_.find(g_ek.labPathA))
                if (const LibTrack* tb = library_.find(g_ek.labPathB))
                    suggestTransitionPoints(*ta, *tb, 16, g_ek.labExitSec, g_ek.labEntrySec);
            g_ek.activeTab = 1;
        }
        ImGui::SameLine(0, 8 * s);
        drawPlaylistDestinationBar(selPaths, "⚡ Enviar A + Selección a Playlist");
    }

    // ================================================================== PESTAÑA 2: BANCO DE TRANSICIONES A -> B (MIX LAB)
    else if (g_ek.activeTab == 1) {
        LibTrack* ta = library_.find(g_ek.labPathA);
        LibTrack* tb = library_.find(g_ek.labPathB);
        if (ta) ensureEightHotCues(*ta);
        if (tb) ensureEightHotCues(*tb);

        const int beatsValues[3] = {8, 16, 32};
        const int fadeBeats = beatsValues[std::clamp(g_ek.labBeatsIdx, 0, 2)];

        if (ImGui::BeginTable("mixlab_ab_cols", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
            for (int side = 0; side < 2; ++side) {
                ImGui::TableNextColumn();
                ImGui::PushID(side);
                std::wstring& curPath = (side == 0) ? g_ek.labPathA : g_ek.labPathB;
                double& curPoint = (side == 0) ? g_ek.labExitSec : g_ek.labEntrySec;
                LibTrack* tr = (side == 0) ? ta : tb;
                const ImU32 sideCol = (side == 0) ? col::DeckA : col::DeckB;

                ImGui::TextColored(toVec4(sideCol), side == 0 ? "PISTA A · SALIDA (OUTRO / BREAK)" : "PISTA B · ENTRADA (INTRO / SUBIDA)");
                ImGui::SameLine(0, 10 * s);
                if (ImGui::SmallButton("Elegir canción…")) ImGui::OpenPopup("ek_pick_lab_popup");
                if (ImGui::BeginPopup("ek_pick_lab_popup")) {
                    ImGui::SetNextItemWidth(280 * s);
                    ImGui::InputTextWithHint("##srch_lab", "Buscar canción, artista o estilo…", g_ek.trackSearchBuf, sizeof g_ek.trackSearchBuf);
                    const std::string q = foldForSearch(g_ek.trackSearchBuf);
                    ImGui::BeginChild("lab_list_pop", ImVec2(460 * s, 260 * s));
                    int shown = 0;
                    for (const auto& t : tracks) {
                        if (t.missing) continue;
                        if (!q.empty() && t.searchKey.find(q) == std::string::npos) continue;
                        std::string lbl = (t.artist.empty() ? "" : t.artist + " - ") + t.title +
                                          "  [" + (t.meta.key >= 0 ? std::string(keyCamelot(t.meta.key)) : "—") +
                                          " · " + (t.meta.bpm > 0 ? std::to_string((int)std::lround(t.meta.bpm)) : "—") + " · " + t.genre + "]##" + std::to_string(shown);
                        if (ImGui::Selectable(lbl.c_str(), t.path == curPath)) {
                            curPath = t.path;
                            tr = library_.find(curPath);
                            ImGui::CloseCurrentPopup();
                        }
                        if (++shown >= 120) break;
                    }
                    ImGui::EndChild();
                    ImGui::EndPopup();
                }
                ImGui::SameLine(0, 4 * s);
                if (ImGui::SmallButton(side == 0 ? "Tomar Plato A" : "Tomar Plato B") && !deck_[side].path.empty()) {
                    curPath = deck_[side].path;
                    tr = library_.find(curPath);
                }

                if (tr) {
                    ImGui::PushFont(fontHead, 16 * s);
                    ImGui::TextUnformatted(tr->title.c_str());
                    ImGui::PopFont();
                    ImGui::TextDisabled("%s   ·   %s   ·   %.2f BPM   ·   %s",
                                        tr->artist.empty() ? "Artista" : tr->artist.c_str(),
                                        tr->genre.empty() ? "Sin estilo" : tr->genre.c_str(),
                                        tr->meta.bpm,
                                        tr->meta.key >= 0 ? keyCamelot(tr->meta.key) : "—");
                } else {
                    ImGui::TextDisabled("Arrastra una pista aquí o cárgala desde un plato");
                }

                // Barra gráfica interactiva con los 8 Hot Cues y el solapamiento naranja
                const ImVec2 barPos = ImGui::GetCursorScreenPos();
                const float barW = std::max(180 * s, ImGui::GetContentRegionAvail().x - 6 * s), barH = 48 * s;
                ImDrawList* bdl = ImGui::GetWindowDrawList();
                bdl->AddRectFilled(barPos, ImVec2(barPos.x + barW, barPos.y + barH), col::WaveBg, 5 * s);
                bdl->AddRect(barPos, ImVec2(barPos.x + barW, barPos.y + barH), col::Line, 5 * s);
                const double dur = (tr && tr->meta.durationSec > 1.0) ? tr->meta.durationSec : 240.0;
                const double bpm = (tr && tr->meta.bpm > 30.0) ? tr->meta.bpm : 126.0;
                const double fadeSec = fadeBeats * 60.0 / bpm;

                if (tr) {
                    // Dibujar marcadores de los 8 Hot Cues
                    for (int c = 0; c < kNumHotCues; ++c) {
                        double cs = tr->meta.hotCuesSec[c];
                        if (cs >= 0.0 && cs <= dur) {
                            float cx = barPos.x + (float)(cs / dur) * barW;
                            bdl->AddLine(ImVec2(cx, barPos.y + 2), ImVec2(cx, barPos.y + barH - 2), col::withAlpha(sideCol, 150), 1.5f * s);
                            char cn[16]; std::snprintf(cn, sizeof cn, "%d", c + 1);
                            bdl->AddRectFilled(ImVec2(cx, barPos.y + 2), ImVec2(cx + 13 * s, barPos.y + 15 * s), sideCol, 2 * s);
                            bdl->AddText(fontHead, 10 * s, ImVec2(cx + 3 * s, barPos.y + 2), col::onColor(sideCol), cn);
                        }
                    }
                    // Rectángulo naranja del solapamiento de la transición
                    float x0 = barPos.x + (float)std::clamp(curPoint / dur, 0.0, 1.0) * barW;
                    float x1 = barPos.x + (float)std::clamp((curPoint + fadeSec) / dur, 0.0, 1.0) * barW;
                    bdl->AddRectFilled(ImVec2(x0, barPos.y + 4 * s), ImVec2(std::max(x0 + 4 * s, x1), barPos.y + barH - 4 * s), col::withAlpha(col::Amber, 75), 3 * s);
                    bdl->AddRect(ImVec2(x0, barPos.y + 4 * s), ImVec2(std::max(x0 + 4 * s, x1), barPos.y + barH - 4 * s), col::Amber, 3 * s, 0, 2.0f * s);
                }

                ImGui::InvisibleButton("##lab_wave_bar", ImVec2(barW, barH));
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && tr) {
                    float relX = (ImGui::GetIO().MousePos.x - barPos.x) / barW;
                    curPoint = std::clamp((double)relX * dur, 0.0, std::max(0.0, dur - fadeSec));
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("RDJ_PISTA")) {
                        auto ps = uic::PayloadPaths(pl);
                        if (!ps.empty()) curPath = ps[0];
                    }
                    ImGui::EndDragDropTarget();
                }

                // Selector de Hot Cue o ajuste fino en segundos
                float ptFloat = (float)curPoint;
                ImGui::SetNextItemWidth(130 * s);
                if (ImGui::SliderFloat(side == 0 ? "Salida A (s)" : "Entrada B (s)", &ptFloat, 0.0f, (float)std::max(10.0, dur - fadeSec), "%.1f s")) {
                    curPoint = ptFloat;
                }
                ImGui::SameLine(0, 8 * s);
                if (tr && ImGui::BeginCombo(side == 0 ? "Elegir Cue A" : "Elegir Cue B", "Saltar a Hot Cue…")) {
                    for (int c = 0; c < kNumHotCues; ++c) {
                        if (tr->meta.hotCuesSec[c] >= 0.0) {
                            char clbl[64];
                            std::snprintf(clbl, sizeof clbl, "Cue %d · %s (%s)", c + 1,
                                          tr->meta.hotCueNames[c].empty() ? "Punto" : tr->meta.hotCueNames[c].c_str(),
                                          formatTime(tr->meta.hotCuesSec[c], false).c_str());
                            if (ImGui::Selectable(clbl, false)) curPoint = tr->meta.hotCuesSec[c];
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        // Resumen del plan A -> B y controles de acción
        ImGui::Spacing();
        const char* beatsLabels[3] = {"8 pulsos (corte rápido)", "16 pulsos (estándar club)", "32 pulsos (progresivo largo)"};
        ImGui::SetNextItemWidth(190 * s);
        ImGui::Combo("Fundido", &g_ek.labBeatsIdx, beatsLabels, 3);
        ImGui::SameLine(0, 8 * s);
        if (NeonButton("Buscar mejor cruce entre Hot Cues", col::DeckA, false, ImVec2(220 * s, 26 * s), ta && tb, fontHead, 12.5f * s)) {
            suggestTransitionPoints(*ta, *tb, fadeBeats, g_ek.labExitSec, g_ek.labEntrySec);
        }
        ImGui::SameLine(0, 8 * s);
        if (NeonButton(mixLabRehearsal_ ? "⏹ Detener Ensayo en Vivo" : "▶ Ensayar Transición en Vivo (15 s)",
                       mixLabRehearsal_ ? col::Red : col::Amber, true, ImVec2(245 * s, 26 * s), ta && tb, fontHead, 12.5f * s)) {
            if (mixLabRehearsal_) {
                mixLabRehearsal_ = false;
                engine_.deck[0].eqLow = 0.5f;
                engine_.deck[1].eqLow = 0.5f;
                if (engine_.deckStatus[0].playing.load()) send(0, Cmd::TogglePlay);
                if (engine_.deckStatus[1].playing.load()) send(1, Cmd::TogglePlay);
                notify("Ensayo de transición detenido.", false);
            } else {
                const double bpmA = ta->meta.bpm > 30.0 ? ta->meta.bpm : 126.0;
                const double fadeS = fadeBeats * 60.0 / bpmA;
                const double startA = std::max(0.0, g_ek.labExitSec - 6.0);
                mixLabRehearsalExitA_ = g_ek.labExitSec;
                mixLabRehearsalEntryB_ = g_ek.labEntrySec;
                mixLabRehearsalFadeSec_ = fadeS;
                mixLabRehearsalBStarted_ = false;
                mixLabRehearsalStart_ = now_;
                mixLabRehearsal_ = true;
                engine_.mixer.crossfader = 0.0f;
                engine_.deck[0].eqLow = 0.5f;
                engine_.deck[1].eqLow = 0.0f;
                deck_[0].mixOutSec = g_ek.labExitSec;
                deck_[1].mixInSec = g_ek.labEntrySec;
                if (deck_[0].path == g_ek.labPathA && deck_[0].track) {
                    send(0, Cmd::Seek, startA * deck_[0].track->sampleRate);
                    if (!engine_.deckStatus[0].playing.load()) send(0, Cmd::TogglePlay);
                } else {
                    pendingSeekSec_[0] = startA;
                    pendingPlayOnLoad_[0] = true;
                    loadToDeck(0, g_ek.labPathA, true);
                }
                if (deck_[1].path == g_ek.labPathB && deck_[1].track) {
                    if (engine_.deckStatus[1].playing.load()) send(1, Cmd::TogglePlay);
                    send(1, Cmd::Seek, g_ek.labEntrySec * deck_[1].track->sampleRate);
                } else {
                    pendingSeekSec_[1] = g_ek.labEntrySec;
                    pendingPlayOnLoad_[1] = false;
                    loadToDeck(1, g_ek.labPathB, true);
                }
                notify("▶ Ensayando transición A→B en vivo: entra B en " + formatTime(g_ek.labExitSec, false) + " con cruce automático de crossfader y graves ⚡", false);
            }
        }
        ImGui::SameLine(0, 8 * s);
        if (NeonButton("★ Guardar en Ideas", col::Violet, false, ImVec2(145 * s, 26 * s), ta && tb, fontHead, 12.5f * s)) {
            int hSc = 0; std::string rel; double dPct = 0, adjPct = 0;
            int sc = compatibilityScore(*ta, *tb, true, hSc, rel, dPct, adjPct);
            g_ek.ideas.push_back(MixIdeaItem{
                g_ek.labPathA, g_ek.labPathB, ta->title, tb->title,
                g_ek.labNote[0] ? std::string(g_ek.labNote) : rel,
                g_ek.labExitSec, g_ek.labEntrySec, fadeBeats, g_ek.labRating, sc
            });
            saveIdeasToDisk();
            notify("Pareja A→B guardada en Ideas de Mezcla ★", false);
        }

        if (ta && tb) {
            int hSc = 0; std::string rel; double dPct = 0, adjPct = 0;
            int sc = compatibilityScore(*ta, *tb, true, hSc, rel, dPct, adjPct);
            ImGui::TextColored(toVec4(sc >= 75 ? col::Green : col::Amber),
                               "ENCAJE A→B: %d / 100   ·   %s (%s → %s)   ·   Ajuste de pitch en B: %+.2f %%   ·   Estilos: %s → %s",
                               sc, rel.c_str(),
                               ta->meta.key >= 0 ? keyCamelot(ta->meta.key) : "—",
                               tb->meta.key >= 0 ? keyCamelot(tb->meta.key) : "—",
                               adjPct,
                               ta->genre.empty() ? "—" : ta->genre.c_str(),
                               tb->genre.empty() ? "—" : tb->genre.c_str());

            // Visualizador gráfico de las curvas superpuestas A->B + EQ Low Swap + progreso del ensayo en vivo
            const ImVec2 ovP = ImGui::GetCursorScreenPos();
            const float ovW = std::max(240 * s, ImGui::GetContentRegionAvail().x - 4 * s), ovH = 42 * s;
            ImDrawList* odl = ImGui::GetWindowDrawList();
            odl->AddRectFilled(ovP, ImVec2(ovP.x + ovW, ovP.y + ovH), col::WaveBg, 5 * s);
            odl->AddRect(ovP, ImVec2(ovP.x + ovW, ovP.y + ovH), col::Line, 5 * s);
            const float xStart = ovP.x + ovW * 0.18f, xEnd = ovP.x + ovW * 0.82f, xMid = (xStart + xEnd) * 0.5f;
            // Curva de A (sale) y curva de B (entra)
            odl->AddLine(ImVec2(ovP.x + 6 * s, ovP.y + 8 * s), ImVec2(xStart, ovP.y + 8 * s), col::DeckA, 2.2f * s);
            odl->AddLine(ImVec2(xStart, ovP.y + 8 * s), ImVec2(xEnd, ovP.y + ovH - 7 * s), col::DeckA, 2.2f * s);
            odl->AddLine(ImVec2(xStart, ovP.y + ovH - 7 * s), ImVec2(xEnd, ovP.y + 8 * s), col::DeckB, 2.2f * s);
            odl->AddLine(ImVec2(xEnd, ovP.y + 8 * s), ImVec2(ovP.x + ovW - 6 * s, ovP.y + 8 * s), col::DeckB, 2.2f * s);
            // Línea central de EQ LOW SWAP
            odl->AddLine(ImVec2(xMid, ovP.y + 2 * s), ImVec2(xMid, ovP.y + ovH - 2 * s), col::withAlpha(col::Amber, 180), 1.5f * s);
            odl->AddText(fontHead, 10.5f * s, ImVec2(ovP.x + 10 * s, ovP.y + 12 * s), col::DeckA, ("A sale: " + formatTime(g_ek.labExitSec, false)).c_str());
            odl->AddText(fontHead, 10.5f * s, ImVec2(xMid - 46 * s, ovP.y + 3 * s), col::Amber, "⚡ EQ LOW SWAP");
            odl->AddText(fontHead, 10.5f * s, ImVec2(xEnd + 8 * s, ovP.y + 12 * s), col::DeckB, ("B entra: " + formatTime(g_ek.labEntrySec, false)).c_str());
            if (mixLabRehearsal_ && deck_[0].track) {
                const double posA = deckPosSec(0);
                const double tRel = (posA - (mixLabRehearsalExitA_ - 6.0)) / (6.0 + mixLabRehearsalFadeSec_ + 5.0);
                const float cx = ovP.x + (float)std::clamp(tRel, 0.0, 1.0) * ovW;
                odl->AddLine(ImVec2(cx, ovP.y), ImVec2(cx, ovP.y + ovH), IM_COL32(255, 255, 255, 255), 2.5f * s);
            }
            ImGui::Dummy(ImVec2(ovW, ovH + 4 * s));
        }

        // Si queda espacio vertical (p. ej. en ventana grande), mostrar candidatas armónicas rápidas para cambiar Pista B con 1 clic
        const float remH = ImGui::GetContentRegionAvail().y - 36 * s;
        if (remH > 85 * s && ta) {
            ImGui::TextDisabled("SUGERENCIAS ARMÓNICAS PARA PISTA A (clic para probar como Pista B):");
            if (ImGui::BeginTable("ek_lab_quick_b", 6, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp, ImVec2(0, remH - 18 * s))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("ENCAJE", ImGuiTableColumnFlags_WidthFixed, 64 * s);
                ImGui::TableSetupColumn("CANDIDATA B", ImGuiTableColumnFlags_WidthStretch, 1.6f);
                ImGui::TableSetupColumn("ARTISTA", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("ESTILO", ImGuiTableColumnFlags_WidthStretch, 0.9f);
                ImGui::TableSetupColumn("BPM / CLAVE", ImGuiTableColumnFlags_WidthFixed, 95 * s);
                ImGui::TableSetupColumn("RELACIÓN", ImGuiTableColumnFlags_WidthStretch, 1.1f);
                ImGui::TableHeadersRow();
                int shown = 0;
                for (const auto& cand : tracks) {
                    if (cand.path == ta->path || cand.missing) continue;
                    int hSc = 0; std::string rel; double dPct = 0, adjPct = 0;
                    int sc = compatibilityScore(*ta, cand, true, hSc, rel, dPct, adjPct);
                    if (sc < 78 || dPct > 6.0) continue;
                    ImGui::PushID(shown++);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    char sb[24]; std::snprintf(sb, sizeof sb, "%d / 100", sc);
                    if (ImGui::Selectable(sb, cand.path == g_ek.labPathB, ImGuiSelectableFlags_SpanAllColumns)) {
                        g_ek.labPathB = cand.path;
                        suggestTransitionPoints(*ta, cand, fadeBeats, g_ek.labExitSec, g_ek.labEntrySec);
                    }
                    if (ImGui::BeginDragDropSource()) {
                        uic::SetPathsPayload({ta->path, cand.path});
                        ImGui::Text("Arrastrando pareja A→B");
                        ImGui::EndDragDropSource();
                    }
                    ImGui::TableNextColumn();
                    {
                        ImU32 rowTCol = col::Text;
                        const std::string decTitle = decorateDeckRow(cand.path, cand.title, rowTCol);
                        ImGui::TextColored(toVec4(rowTCol), "%s", decTitle.c_str());
                    }
                    ImGui::TableNextColumn(); ImGui::TextDisabled("%s", cand.artist.c_str());
                    ImGui::TableNextColumn(); ImGui::TextColored(toVec4(col::DeckA), "%s", cand.genre.c_str());
                    ImGui::TableNextColumn(); ImGui::Text("%.1f · %s", cand.meta.bpm, cand.meta.key >= 0 ? keyCamelot(cand.meta.key) : "—");
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(rel.c_str());
                    ImGui::PopID();
                    if (shown >= 60) break;
                }
                ImGui::EndTable();
            }
        }

        std::vector<std::wstring> pairPaths;
        if (!g_ek.labPathA.empty()) pairPaths.push_back(g_ek.labPathA);
        if (!g_ek.labPathB.empty() && g_ek.labPathB != g_ek.labPathA) pairPaths.push_back(g_ek.labPathB);
        drawPlaylistDestinationBar(pairPaths, "⚡ Enviar Par A→B a Playlist");
    }

    // ================================================================== PESTAÑA 3: GENERADOR AUTO-SET POR ESTILO Y ARMONÍA + CURVA DE ENERGÍA
    else if (g_ek.activeTab == 2) {
        auto trackEnergy10 = [](const LibTrack& t) -> float {
            if (t.rating > 0) return std::clamp(t.rating * 2.0f, 2.0f, 10.0f);
            const double b = t.meta.bpm > 0 ? t.meta.bpm : 126.0;
            return (float)std::clamp(4.0 + (b - 115.0) * 0.22, 2.0, 9.5);
        };
        auto targetCurveEnergy = [](int curveIdx, float p) -> float {
            switch (curveIdx) {
            case 1: return 4.5f + 4.8f * p;                                        // Warm-Up -> Peak Time (4.5 -> 9.3)
            case 2: return 8.0f + 1.5f * std::sin(p * 3.14159f);                   // Peak Time (8.0 -> 9.5)
            case 3: return 6.5f + 2.5f * std::sin(p * 3.14159f * 2.5f);            // Montaña Rusa (olas)
            case 4: return 8.8f - 4.3f * p;                                        // After / Cierre (8.8 -> 4.5)
            default: return 7.0f;                                                  // Libre / Fluido
            }
        };

        ImGui::SetNextItemWidth(170 * s);
        if (ImGui::BeginCombo("Estilo del Set", g_ek.autoStyle.c_str())) {
            for (const auto& st : uniqueStyles) {
                if (ImGui::Selectable(st.c_str(), g_ek.autoStyle == st)) g_ek.autoStyle = st;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine(0, 8 * s);
        const char* curves[5] = {
            "1 · Libre / Fluido (Armónico natural)",
            "2 · Warm-Up → Peak Time (Subida 4 → 9)",
            "3 · Peak Time (Todo arriba 8 → 10)",
            "4 · Montaña Rusa (Olas de energía)",
            "5 · After / Cierre (Bajada 9 → 4)"
        };
        ImGui::SetNextItemWidth(235 * s);
        ImGui::Combo("Perfil de Curva", &g_ek.autoCurve, curves, 5);
        ImGui::SameLine(0, 8 * s);
        ImGui::SetNextItemWidth(90 * s);
        ImGui::SliderInt("Temas", &g_ek.autoCount, 6, 40);
        ImGui::SameLine(0, 8 * s);
        const bool triggerAutoGen = (g_ek.autoSetPaths.empty() && !tracks.empty());
        if (NeonButton("⚡ Generar Sesión Armónica", col::DeckA, true, ImVec2(195 * s, 26 * s), !tracks.empty(), fontHead, 13 * s) || triggerAutoGen) {
            g_ek.autoSetPaths.clear();
            std::vector<const LibTrack*> pool;
            for (const auto& t : tracks) {
                if (t.missing || t.meta.bpm <= 0) continue;
                if (g_ek.autoStyle != "Todos los estilos" && foldForSearch(t.genre) != foldForSearch(g_ek.autoStyle)) continue;
                pool.push_back(&t);
            }
            if (!pool.empty()) {
                if (g_ek.autoCurve == 1) {
                    std::sort(pool.begin(), pool.end(), [](const LibTrack* a, const LibTrack* b) { return a->meta.bpm < b->meta.bpm; });
                } else if (g_ek.autoCurve == 4) {
                    std::sort(pool.begin(), pool.end(), [](const LibTrack* a, const LibTrack* b) { return a->meta.bpm > b->meta.bpm; });
                }
                std::set<std::wstring> used;
                const LibTrack* cur = pool[0];
                if (!g_ek.refPath.empty() && g_ek.autoCurve == 0) {
                    for (const auto* p : pool) if (p->path == g_ek.refPath) { cur = p; break; }
                }
                g_ek.autoSetPaths.push_back(cur->path);
                used.insert(cur->path);
                while ((int)g_ek.autoSetPaths.size() < g_ek.autoCount && used.size() < pool.size()) {
                    const LibTrack* bestNext = nullptr;
                    double bestVal = -99999.0;
                    const float progress = (float)g_ek.autoSetPaths.size() / std::max(1, g_ek.autoCount - 1);
                    const float targetE = targetCurveEnergy(g_ek.autoCurve, progress);
                    for (const auto* cand : pool) {
                        if (used.count(cand->path)) continue;
                        int hSc = 0; std::string rel; double dPct = 0, adjPct = 0;
                        double val = compatibilityScore(*cur, *cand, true, hSc, rel, dPct, adjPct);
                        if (!cur->artist.empty() && cur->artist == cand->artist) val -= 22.0;
                        if (g_ek.autoCurve != 0) {
                            const float candE = trackEnergy10(*cand);
                            val -= std::fabs(candE - targetE) * 5.5;
                            if (g_ek.autoCurve == 1 && cand->meta.bpm >= cur->meta.bpm && cand->meta.bpm <= cur->meta.bpm + 3.0) val += 14.0;
                            if (g_ek.autoCurve == 4 && cand->meta.bpm <= cur->meta.bpm && cand->meta.bpm >= cur->meta.bpm - 3.0) val += 14.0;
                        }
                        if (val > bestVal) { bestVal = val; bestNext = cand; }
                    }
                    if (!bestNext) break;
                    g_ek.autoSetPaths.push_back(bestNext->path);
                    used.insert(bestNext->path);
                    cur = bestNext;
                }
                std::snprintf(g_ek.newPlaylistName, sizeof g_ek.newPlaylistName, "Set %s",
                              g_ek.autoStyle == "Todos los estilos" ? "Armónico" : g_ek.autoStyle.c_str());
            }
        }

        // Gráfico interactivo de Curva de Energía y BPM de la Sesión
        if (g_ek.autoSetPaths.size() >= 2) {
            const ImVec2 cp = ImGui::GetCursorScreenPos();
            const float cw = std::max(240 * s, ImGui::GetContentRegionAvail().x - 4 * s), ch = 64 * s;
            ImDrawList* cdl = ImGui::GetWindowDrawList();
            cdl->AddRectFilled(cp, ImVec2(cp.x + cw, cp.y + ch), col::WaveBg, 6 * s);
            cdl->AddRect(cp, ImVec2(cp.x + cw, cp.y + ch), col::Line, 6 * s);
            const size_t n = g_ek.autoSetPaths.size();
            double minB = 999.0, maxB = 0.0;
            for (const auto& p : g_ek.autoSetPaths) {
                if (const LibTrack* lt = library_.find(p)) {
                    if (lt->meta.bpm > 0) { minB = std::min(minB, lt->meta.bpm); maxB = std::max(maxB, lt->meta.bpm); }
                }
            }
            if (maxB - minB < 4.0) { minB -= 2.0; maxB += 2.0; }
            auto ptOf = [&](size_t idx) -> ImVec2 {
                const float x = cp.x + 24 * s + (cw - 48 * s) * (float)idx / (float)(n - 1);
                const LibTrack* lt = library_.find(g_ek.autoSetPaths[idx]);
                const float eNorm = lt ? (trackEnergy10(*lt) - 1.0f) / 9.0f : 0.5f;
                const float bNorm = (lt && lt->meta.bpm > 0) ? (float)((lt->meta.bpm - minB) / (maxB - minB)) : 0.5f;
                const float yMix = 0.55f * eNorm + 0.45f * bNorm;
                const float y = cp.y + ch - 12 * s - std::clamp(yMix, 0.0f, 1.0f) * (ch - 26 * s);
                return ImVec2(x, y);
            };
            for (size_t i = 1; i < n; ++i) {
                const LibTrack* ta2 = library_.find(g_ek.autoSetPaths[i - 1]);
                const LibTrack* tb2 = library_.find(g_ek.autoSetPaths[i]);
                int sc = 80;
                if (ta2 && tb2) { int hS = 0; std::string r; double dp = 0, ap = 0; sc = compatibilityScore(*ta2, *tb2, true, hS, r, dp, ap); }
                const ImU32 lCol = sc >= 80 ? col::Green : (sc >= 65 ? col::Amber : col::Red);
                cdl->AddLine(ptOf(i - 1), ptOf(i), lCol, 2.4f * s);
            }
            for (size_t i = 0; i < n; ++i) {
                const ImVec2 pt = ptOf(i);
                const LibTrack* lt = library_.find(g_ek.autoSetPaths[i]);
                cdl->AddCircleFilled(pt, 4.2f * s, col::DeckA, 14);
                if (lt && (n <= 18 || i % 2 == 0)) {
                    const char* kc = lt->meta.key >= 0 ? keyCamelot(lt->meta.key) : "";
                    cdl->AddText(fontHead, 10 * s, ImVec2(pt.x - 9 * s, pt.y - 14 * s), col::Text, kc);
                }
            }
            ImGui::Dummy(ImVec2(cw, ch + 4 * s));
        }

        const float availH = std::max(100 * s, ImGui::GetContentRegionAvail().y - 34 * s);
        int swapFrom = -1, swapTo = -1;
        if (ImGui::BeginTable("ek_autoset_tbl", 7, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp, ImVec2(0, availH))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 34 * s);
            ImGui::TableSetupColumn("TEMA", ImGuiTableColumnFlags_WidthStretch, 1.5f);
            ImGui::TableSetupColumn("ARTISTA", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("ESTILO", ImGuiTableColumnFlags_WidthStretch, 0.9f);
            ImGui::TableSetupColumn("BPM / CLAVE", ImGuiTableColumnFlags_WidthFixed, 110 * s);
            ImGui::TableSetupColumn("TRANSICIÓN", ImGuiTableColumnFlags_WidthStretch, 1.1f);
            ImGui::TableSetupColumn("ORDEN", ImGuiTableColumnFlags_WidthFixed, 68 * s);
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < g_ek.autoSetPaths.size(); ++i) {
                const LibTrack* t = library_.find(g_ek.autoSetPaths[i]);
                if (!t) continue;
                ImGui::PushID((int)i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                char posB[16]; std::snprintf(posB, sizeof posB, "%zu", i + 1);
                ImGui::Selectable(posB, false, ImGuiSelectableFlags_SpanAllColumns);
                if (ImGui::BeginDragDropSource()) {
                    uic::SetPathsPayload(g_ek.autoSetPaths);
                    ImGui::Text("Arrastrando sesión generada (%zu temas)", g_ek.autoSetPaths.size());
                    ImGui::EndDragDropSource();
                }
                ImGui::TableNextColumn();
                {
                    ImU32 rowTCol = col::Text;
                    const std::string decTitle = decorateDeckRow(t->path, t->title, rowTCol);
                    ImGui::TextColored(toVec4(rowTCol), "%s", decTitle.c_str());
                }
                ImGui::TableNextColumn(); ImGui::TextDisabled("%s", t->artist.c_str());
                ImGui::TableNextColumn(); ImGui::TextColored(toVec4(col::DeckA), "%s", t->genre.c_str());
                ImGui::TableNextColumn(); ImGui::Text("%.1f · %s", t->meta.bpm, t->meta.key >= 0 ? keyCamelot(t->meta.key) : "—");
                ImGui::TableNextColumn();
                if (i > 0) {
                    const LibTrack* prev = library_.find(g_ek.autoSetPaths[i - 1]);
                    if (prev) {
                        int hSc = 0; std::string rel; double dPct = 0, adjPct = 0;
                        int sc = compatibilityScore(*prev, *t, true, hSc, rel, dPct, adjPct);
                        ImGui::TextColored(toVec4(sc >= 75 ? col::Green : col::Amber), "%d pts · %s", sc, rel.c_str());
                    }
                } else ImGui::TextDisabled("Inicio del Set");
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("↑") && i > 0) { swapFrom = (int)i; swapTo = (int)i - 1; }
                ImGui::SameLine(0, 4 * s);
                if (ImGui::SmallButton("↓") && i + 1 < g_ek.autoSetPaths.size()) { swapFrom = (int)i; swapTo = (int)i + 1; }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (swapFrom >= 0 && swapTo >= 0) {
            std::swap(g_ek.autoSetPaths[swapFrom], g_ek.autoSetPaths[swapTo]);
        }
        drawPlaylistDestinationBar(g_ek.autoSetPaths, "⚡ Añadir Auto-Set a Playlist");
    }

    // ================================================================== PESTAÑA 4: IDEAS GUARDADAS Y HERRAMIENTAS (ESTILOS + 8 HOT CUES)
    else if (g_ek.activeTab == 3) {
        // Barra de herramientas masivas de Estilo y Hot Cues
        if (NeonButton("⚡ Asegurar Estilo + 8 Hot Cues en toda la Biblioteca", col::DeckA, true, ImVec2(320 * s, 28 * s), true, fontHead, 12.5f * s)) {
            int cuesAdded = 0;
            for (auto& t : tracks) {
                if (ensureEightHotCues(t)) ++cuesAdded;
            }
            library_.markDirty();
            library_.save();
            sortDirty_ = true;
            refreshView();
            notify("Biblioteca actualizada: todas las pistas tienen Estilo y 8 Hot Cues (" + std::to_string(cuesAdded) + " nuevas con Cues) ⚡", false);
        }
        ImGui::SameLine(0, 10 * s);
        ImGui::SetNextItemWidth(150 * s);
        ImGui::InputText("##style_assign_ek", g_ek.customStyleBuf, sizeof g_ek.customStyleBuf);
        ImGui::SameLine(0, 4 * s);
        if (NeonButton("Asignar Estilo a Selección", col::Violet, false, ImVec2(175 * s, 28 * s), !selectedPaths().empty(), fontHead, 12.5f * s)) {
            auto selP = selectedPaths();
            for (const auto& sp : selP) {
                if (LibTrack* lt = library_.find(sp)) {
                    lt->genre = g_ek.customStyleBuf;
                    lt->searchKey = foldForSearch(lt->artist + " " + lt->title + " " + lt->fileName + " " + lt->genre);
                }
            }
            library_.markDirty();
            library_.save();
            sortDirty_ = true;
            refreshView();
            notify("Estilo «" + std::string(g_ek.customStyleBuf) + "» asignado a " + std::to_string(selP.size()) + " pista(s).", false);
        }

        ImGui::SameLine(0, 10 * s);
        auto runLibraryAudit = [&]() {
            g_ek.dupPairs.clear();
            g_ek.healthIssues.clear();
            g_ek.auditDone = true;
            std::map<std::string, const LibTrack*> seenByNormTitle;
            for (const auto& t : tracks) {
                if (t.missing) continue;
                std::string normT = foldForSearch(t.artist + " " + t.title);
                if (!normT.empty() && normT.size() >= 5) {
                    auto it = seenByNormTitle.find(normT);
                    if (it != seenByNormTitle.end() && it->second->path != t.path) {
                        if (g_ek.dupPairs.size() < 200) {
                            g_ek.dupPairs.push_back({it->second->path, t.path, t.title, t.artist, it->second->meta.durationSec, t.meta.durationSec});
                        }
                    } else {
                        seenByNormTitle[normT] = &t;
                    }
                }
                if (t.meta.bpm > 0 && t.meta.bpm < 68.0) {
                    g_ek.healthIssues.push_back({t.path, t.title, "BPM bajo (<68): ¿mitad de tempo (×2)?", t.meta.bpm, t.meta.durationSec, 1});
                } else if (t.meta.bpm > 185.0) {
                    g_ek.healthIssues.push_back({t.path, t.title, "BPM muy alto (>185): ¿doble tempo (÷2)?", t.meta.bpm, t.meta.durationSec, 2});
                } else if (t.meta.durationSec > 0 && t.meta.durationSec < 45.0) {
                    g_ek.healthIssues.push_back({t.path, t.title, "Pista muy corta (<45 s: jingle/efecto)", t.meta.bpm, t.meta.durationSec, 3});
                } else if (t.meta.bpm <= 0 || t.meta.key < 0) {
                    g_ek.healthIssues.push_back({t.path, t.title, "Sin BPM o sin tonalidad analizada", t.meta.bpm, t.meta.durationSec, 0});
                }
            }
        };
        if (!g_ek.auditDone) runLibraryAudit();
        if (NeonButton("🔍 Auditar Biblioteca Ahora", col::Amber, false, ImVec2(195 * s, 28 * s), true, fontHead, 12.5f * s)) {
            runLibraryAudit();
            notify("Auditoría completada: " + std::to_string(g_ek.dupPairs.size()) + " posibles duplicados y " + std::to_string(g_ek.healthIssues.size()) + " avisos de salud.", false);
        }

        ImGui::Spacing();
        const float availH = std::max(100 * s, ImGui::GetContentRegionAvail().y - 34 * s);
        if (ImGui::BeginTable("ek_tab4_split", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchSame, ImVec2(0, availH))) {
            // Columna izquierda: Ideas de Mezcla Guardadas
            ImGui::TableNextColumn();
            ImGui::TextColored(toVec4(col::Amber), "★ IDEAS DE MEZCLA GUARDADAS (%zu parejas):", g_ek.ideas.size());
            int removeIdea = -1;
            if (ImGui::BeginTable("ek_ideas_tbl", 5, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp, ImVec2(0, availH - 26 * s))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("PISTA A", ImGuiTableColumnFlags_WidthStretch, 1.3f);
                ImGui::TableSetupColumn("PISTA B", ImGuiTableColumnFlags_WidthStretch, 1.3f);
                ImGui::TableSetupColumn("CRUCE", ImGuiTableColumnFlags_WidthFixed, 115 * s);
                ImGui::TableSetupColumn("NOTA", ImGuiTableColumnFlags_WidthStretch, 0.9f);
                ImGui::TableSetupColumn("ACCIÓN", ImGuiTableColumnFlags_WidthFixed, 155 * s);
                ImGui::TableHeadersRow();
                for (size_t i = 0; i < g_ek.ideas.size(); ++i) {
                    const MixIdeaItem& item = g_ek.ideas[i];
                    ImGui::PushID((int)i);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    {
                        ImU32 cA = col::Text;
                        std::string dA = decorateDeckRow(item.pathA, item.titleA, cA);
                        ImGui::TextColored(toVec4(cA), "%s", dA.c_str());
                    }
                    ImGui::TableNextColumn();
                    {
                        ImU32 cB = col::Text;
                        std::string dB = decorateDeckRow(item.pathB, item.titleB, cB);
                        ImGui::TextColored(toVec4(cB), "%s", dB.c_str());
                    }
                    ImGui::TableNextColumn(); ImGui::Text("%s→%s", formatTime(item.exitSec, false).c_str(), formatTime(item.entrySec, false).c_str());
                    ImGui::TableNextColumn(); ImGui::TextDisabled("%s", item.note.c_str());
                    ImGui::TableNextColumn();
                    if (ImGui::SmallButton("Mix Lab")) {
                        g_ek.labPathA = item.pathA; g_ek.labPathB = item.pathB;
                        g_ek.labExitSec = item.exitSec; g_ek.labEntrySec = item.entrySec;
                        g_ek.activeTab = 1;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Platos")) {
                        loadToDeck(0, item.pathA); loadToDeck(1, item.pathB);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("×")) removeIdea = (int)i;
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (removeIdea >= 0) {
                g_ek.ideas.erase(g_ek.ideas.begin() + removeIdea);
                saveIdeasToDisk();
            }

            // Columna derecha: Auditoría de Duplicados y Salud de la Biblioteca
            ImGui::TableNextColumn();
            char dupTabLbl[64], hlthTabLbl[64];
            std::snprintf(dupTabLbl, sizeof dupTabLbl, "Posibles Duplicados (%zu)", g_ek.dupPairs.size());
            std::snprintf(hlthTabLbl, sizeof hlthTabLbl, "Salud / BPM ×2÷2 (%zu)", g_ek.healthIssues.size());
            if (NeonButton(dupTabLbl, col::DeckA, g_ek.auditSubTab == 0, ImVec2(185 * s, 22 * s), true, fontHead, 12 * s)) g_ek.auditSubTab = 0;
            ImGui::SameLine(0, 6 * s);
            if (NeonButton(hlthTabLbl, col::Amber, g_ek.auditSubTab == 1, ImVec2(185 * s, 22 * s), true, fontHead, 12 * s)) g_ek.auditSubTab = 1;

            if (g_ek.auditSubTab == 0) {
                if (ImGui::BeginTable("ek_dups_tbl", 4, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp, ImVec2(0, availH - 28 * s))) {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("TEMA DUPLICADO", ImGuiTableColumnFlags_WidthStretch, 1.4f);
                    ImGui::TableSetupColumn("ARTISTA", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                    ImGui::TableSetupColumn("DURACIONES", ImGuiTableColumnFlags_WidthFixed, 95 * s);
                    ImGui::TableSetupColumn("COMPARAR", ImGuiTableColumnFlags_WidthFixed, 110 * s);
                    ImGui::TableHeadersRow();
                    for (size_t di = 0; di < g_ek.dupPairs.size(); ++di) {
                        const auto& dp = g_ek.dupPairs[di];
                        ImGui::PushID((int)di + 5000);
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(dp.title.c_str());
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A: %s\nB: %s", toUtf8(dp.pathA).c_str(), toUtf8(dp.pathB).c_str());
                        ImGui::TableNextColumn(); ImGui::TextDisabled("%s", dp.artist.c_str());
                        ImGui::TableNextColumn(); ImGui::Text("%s / %s", formatTime(dp.durA, false).c_str(), formatTime(dp.durB, false).c_str());
                        ImGui::TableNextColumn();
                        if (ImGui::SmallButton("Cargar A/B")) {
                            loadToDeck(0, dp.pathA); loadToDeck(1, dp.pathB);
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            } else {
                int fixIdx = -1; double fixMult = 1.0;
                if (ImGui::BeginTable("ek_hlth_tbl", 4, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp, ImVec2(0, availH - 28 * s))) {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("PISTA", ImGuiTableColumnFlags_WidthStretch, 1.3f);
                    ImGui::TableSetupColumn("DIAGNÓSTICO", ImGuiTableColumnFlags_WidthStretch, 1.3f);
                    ImGui::TableSetupColumn("BPM", ImGuiTableColumnFlags_WidthFixed, 58 * s);
                    ImGui::TableSetupColumn("CORREGIR", ImGuiTableColumnFlags_WidthFixed, 115 * s);
                    ImGui::TableHeadersRow();
                    for (size_t hi = 0; hi < g_ek.healthIssues.size() && hi < 200; ++hi) {
                        const auto& iss = g_ek.healthIssues[hi];
                        ImGui::PushID((int)hi + 9000);
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(iss.title.c_str());
                        ImGui::TableNextColumn(); ImGui::TextColored(toVec4(col::Amber), "%s", iss.issue.c_str());
                        ImGui::TableNextColumn(); if (iss.bpm > 0) ImGui::Text("%.1f", iss.bpm); else ImGui::TextDisabled("—");
                        ImGui::TableNextColumn();
                        if (iss.kind == 1 && ImGui::SmallButton("BPM ×2")) { fixIdx = (int)hi; fixMult = 2.0; }
                        else if (iss.kind == 2 && ImGui::SmallButton("BPM ÷2")) { fixIdx = (int)hi; fixMult = 0.5; }
                        else if (ImGui::SmallButton("Plato A")) { loadToDeck(0, iss.path); }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                if (fixIdx >= 0) {
                    const auto& iss = g_ek.healthIssues[fixIdx];
                    if (LibTrack* lt = library_.find(iss.path)) {
                        lt->meta.bpm = std::clamp(lt->meta.bpm * fixMult, 30.0, 300.0);
                        lt->meta.gridSrc = 3;
                        library_.markDirty();
                        library_.save();
                    }
                    runLibraryAudit();
                }
            }
            ImGui::EndTable();
        }
        std::vector<std::wstring> allIdeaPaths;
        std::set<std::wstring> seenI;
        for (const auto& it : g_ek.ideas) {
            if (!seenI.count(it.pathA)) { seenI.insert(it.pathA); allIdeaPaths.push_back(it.pathA); }
            if (!seenI.count(it.pathB)) { seenI.insert(it.pathB); allIdeaPaths.push_back(it.pathB); }
        }
        drawPlaylistDestinationBar(allIdeaPaths, "⚡ Volcar Ideas a Playlist");
    }
}

} // namespace rdj

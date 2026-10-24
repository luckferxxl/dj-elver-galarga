// Modo SEPARADOR: separar una canción en pistas (voz, batería, bajo, guitarra,
// piano, otros) con Demucs y escucharlas/mezclarlas/exportarlas o llevarlas a
// los platos.
#include "app.h"
#include "ui_common.h"
#include "widgets.h"
#include "winutil.h"
#include "../audio/analysis.h"
#include "../audio/ma.h"
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

namespace {
float S() { return ImGui::GetFontSize() / 16.f; }

ImU32 stemColor(const std::string& id) {
    if (id == "voz") return IM_COL32(255, 80, 170, 255);
    if (id == "bateria") return IM_COL32(255, 150, 50, 255);
    if (id == "bajo") return IM_COL32(70, 140, 255, 255);
    if (id == "guitarra") return IM_COL32(80, 220, 120, 255);
    if (id == "piano") return IM_COL32(250, 220, 70, 255);
    return IM_COL32(170, 120, 255, 255);
}

std::string dateText(int64_t t) {
    if (t <= 0) return "";
    std::tm lt{};
    const std::time_t tt = (std::time_t)t;
    localtime_s(&lt, &tt);
    char b[32];
    std::strftime(b, sizeof b, "%d-%m-%Y %H:%M", &lt);
    return b;
}
} // namespace

// =================================================================== lógica

void App::separateFile(const std::wstring& path) {
    if (!isSupportedAudioExt(extensionLower(path))) { notify("Formato no compatible para separar (MP3, WAV, FLAC u OGG).", true); return; }
    std::string title;
    {
        std::lock_guard<std::recursive_mutex> lk(library_.mutex());
        if (const LibTrack* t = library_.find(path)) title = t->artist.empty() ? t->title : t->artist + " - " + t->title;
    }
    if (title.empty()) {
        std::wstring n = fileNameOf(path);
        title = toUtf8(n.substr(0, n.find_last_of(L'.')));
    }
    stems_.enqueue(path, title, stemModel6_ ? "htdemucs_6s" : "htdemucs");
    mode_ = 1;
    if (stems_.engine() != StemSeparator::Engine::Listo)
        notify("Canción en cola. Instala el motor de separación (botón de la izquierda) para empezar.", true);
    else notify("Separando «" + title + "»… (en segundo plano)");
}

void App::openStemSong(const StemSong& s) {
    if (stemLoadFuture_.valid()) return;
    stemOpen_ = s;
    stemOpenFolder_ = s.folder;
    const std::vector<std::string> ids = s.stems;
    const std::wstring folder = s.folder;
    stemLoadFuture_ = std::async(std::launch::async, [ids, folder] {
        std::vector<TrackPtr> out;
        for (auto& id : ids) {
            std::string err;
            TrackPtr t = decodeFile(folder + L"\\" + fromUtf8(id) + L".flac", err);
            if (t) computeWaveform(t->pcm.data(), t->frames, t->sampleRate, t->wave, t->maxPeak);
            out.push_back(t);
        }
        return out;
    });
}

void App::separatorTick() {
    // Lista de canciones separadas
    if (stems_.songsVersion() != stemSongsVer_) {
        stemSongsVer_ = stems_.songsVersion();
        stemSongs_ = stems_.songs();
        // Abrir automáticamente la que acaba de terminar
        if (!stemSongs_.empty() && stemSongsVer_ > 1 && stemOpenFolder_ != stemSongs_[0].folder && mode_ == 1) openStemSong(stemSongs_[0]);
    }
    // Stems decodificados: pasarlos al motor
    if (stemLoadFuture_.valid() && stemLoadFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        std::vector<TrackPtr> tr = stemLoadFuture_.get();
        std::array<TrackPtr, kStemSlots> keep{};
        StemSet& set = stemSets_[(stemGen_ + 1) % 4];
        set = StemSet{};
        int n = 0;
        for (size_t i = 0; i < tr.size() && n < kStemSlots; ++i) {
            if (!tr[i]) continue;
            keep[n] = tr[i];
            set.tracks[n] = tr[i].get();
            stemIds_[n] = stemOpen_.stems[i];
            ++n;
        }
        set.count = n;
        stemCount_ = n;
        stemTracks_ = keep;
        Command c{Cmd::StemLoad, 0};
        c.stems = &set;
        c.gen = ++stemGen_;
        stemPending_.push_back({c.gen, keep});
        engine_.send(c);
        for (int k = 0; k < kStemSlots; ++k) { engine_.stem.mute[k] = false; engine_.stem.solo[k] = false; engine_.stem.gain[k] = 1.f; }
        if (n == 0) notify("No se pudieron abrir las pistas de esta canción.", true);
    }
    // Liberar las pistas que el motor ya no usa
    const uint32_t ack = engine_.stemStatus.loadedGen.load(std::memory_order_acquire);
    for (size_t i = 0; i < stemPending_.size();) {
        if (stemPending_[i].first <= ack) {
            if (stemPending_[i].first == ack) stemActive_ = stemPending_[i].second;
            stemPending_.erase(stemPending_.begin() + i);
        } else ++i;
    }
    // Exportación terminada
    if (stemExport_.valid() && stemExport_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const std::string err = stemExport_.get();
        if (!err.empty()) notify(err, true);
        else {
            notify("Mezcla de pistas exportada: " + toUtf8(stemExportFile_));
            if (stemExportLoadDeck_ >= 0) {
                stemTitles_[stemExportFile_] = stemOpen_.title + " (Instrumental)";
                mode_ = 0;
                loadToDeck(stemExportLoadDeck_, stemExportFile_, true);
            }
        }
        stemExportLoadDeck_ = -1;
    }
}

void App::loadStemToDeck(int deck, const std::string& stemId) {
    const std::wstring path = stemOpenFolder_ + L"\\" + fromUtf8(stemId) + L".flac";
    stemTitles_[path] = stemOpen_.title + " (" + stemLabel(stemId) + ")";
    mode_ = 0;
    loadToDeck(deck, path);
}

void App::exportStemMix(const std::wstring& outFile, bool onlyInstrumental, int loadDeckAfter) {
    if (stemExport_.valid() || stemCount_ == 0) return;
    // Copia de lo necesario: pistas y ganancias actuales (lo que se oye).
    std::vector<TrackPtr> tr;
    std::vector<float> gains;
    bool anySolo = false;
    for (int k = 0; k < stemCount_; ++k) if (engine_.stem.solo[k].load()) anySolo = true;
    for (int k = 0; k < stemCount_; ++k) {
        float g = engine_.stem.gain[k].load();
        if (onlyInstrumental) g = stemIds_[k] == "voz" ? 0.f : 1.f;
        else if (engine_.stem.mute[k].load() || (anySolo && !engine_.stem.solo[k].load())) g = 0.f;
        tr.push_back(stemTracks_[k]);
        gains.push_back(g);
    }
    stemExportFile_ = outFile;
    stemExportLoadDeck_ = loadDeckAfter;
    stemExport_ = std::async(std::launch::async, [tr, gains, outFile]() -> std::string {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        if (tr.empty() || !tr[0]) return "No hay pistas que exportar.";
        const uint32_t sr = tr[0]->sampleRate;
        uint64_t frames = 0;
        for (auto& t : tr) if (t) frames = std::max(frames, t->frames);
        ma_encoder enc;
        ma_encoder_config ec = ma_encoder_config_init(ma_encoding_format_wav, ma_format_s16, 2, sr);
        if (ma_encoder_init_file_w(outFile.c_str(), &ec, &enc) != MA_SUCCESS) return "No se pudo crear el archivo de exportación.";
        std::vector<int16_t> buf(8192 * 2);
        for (uint64_t pos = 0; pos < frames; pos += 8192) {
            const uint64_t n = std::min<uint64_t>(8192, frames - pos);
            for (uint64_t i = 0; i < n; ++i) {
                float l = 0, r = 0;
                for (size_t k = 0; k < tr.size(); ++k) {
                    if (!tr[k] || gains[k] <= 0 || pos + i >= tr[k]->frames) continue;
                    l += tr[k]->pcm[(pos + i) * 2] * gains[k];
                    r += tr[k]->pcm[(pos + i) * 2 + 1] * gains[k];
                }
                buf[i * 2] = (int16_t)std::lround(std::clamp(l, -1.f, 1.f) * 32767.f);
                buf[i * 2 + 1] = (int16_t)std::lround(std::clamp(r, -1.f, 1.f) * 32767.f);
            }
            ma_encoder_write_pcm_frames(&enc, buf.data(), n, nullptr);
        }
        ma_encoder_uninit(&enc);
        return "";
    });
}

// =================================================================== interfaz

void App::drawSeparator(ImVec2 a, ImVec2 b) {
    const float s = S();
    sepRect_[0] = a; sepRect_[1] = b;
    const float sideW = std::clamp((b.x - a.x) * 0.28f, 330 * s, 420 * s);
    drawSeparatorSide(a, ImVec2(a.x + sideW, b.y));
    drawStemPlayer(ImVec2(a.x + sideW + 8 * s, a.y), b);
}

void App::drawSeparatorSide(ImVec2 a, ImVec2 b) {
    const float s = S();
    Panel(ImGui::GetWindowDrawList(), a, b, 10 * s, col::Panel, col::Line);
    ImGui::SetCursorScreenPos(a);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * s, 12 * s));
    ImGui::BeginChild("sepside", ImVec2(b.x - a.x, b.y - a.y), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x;
    {
        const ImVec2 hp = ImGui::GetCursorScreenPos();
        DrawIcon(dl, Ic::Layers, ImVec2(hp.x + 9 * s, hp.y + 10 * s), 17 * s, col::Violet);
        dl->AddText(fontHead, 16 * s, ImVec2(hp.x + 24 * s, hp.y + 1 * s), col::Text, "SEPARADOR DE PISTAS");
        dl->AddText(ImGui::GetFont(), 12.5f * s, ImVec2(hp.x + 24 * s, hp.y + 20 * s), col::TextDim, "Voz e instrumentos de cualquier canción");
        ImGui::Dummy(ImVec2(w, 40 * s));
    }

    // ---- Motor: una línea si está listo; si no, cómo instalarlo.
    const auto eng = stems_.engine();
    if (eng == StemSeparator::Engine::Listo) {
        const ImVec2 mp = ImGui::GetCursorScreenPos();
        dl->AddCircleFilled(ImVec2(mp.x + 5 * s, mp.y + 8 * s), 4 * s, col::Green);
        dl->AddText(ImGui::GetFont(), 13 * s, ImVec2(mp.x + 15 * s, mp.y), col::mix(col::Text, col::TextDim, 0.3f), "Motor listo · Demucs (procesador)");
        ImGui::Dummy(ImVec2(w, 20 * s));
    } else if (eng == StemSeparator::Engine::Instalando) {
        ImGui::TextColored(toVec4(col::Amber), "Instalando el motor… %s", stems_.engineMessage().c_str());
        const auto lg = stems_.installLog();
        ImGui::BeginChild("instlog", ImVec2(w, 80 * s), ImGuiChildFlags_Borders);
        for (size_t i = lg.size() > 40 ? lg.size() - 40 : 0; i < lg.size(); ++i) ImGui::TextDisabled("%s", lg[i].c_str());
        ImGui::SetScrollHereY(1.f);
        ImGui::EndChild();
        if (NeonButton("Cancelar instalación", col::Red, false, ImVec2(w, 28 * s))) stems_.cancelInstall();
    } else {
        ImGui::PushTextWrapPos(a.x + w - ImGui::GetWindowPos().x);
        if (eng == StemSeparator::Engine::Error) ImGui::TextColored(toVec4(col::Red), "%s", stems_.engineMessage().c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(col::TextDim));
        ImGui::TextWrapped("Hace falta el motor Demucs (código abierto, MIT). Se instala una vez (unos 1,2 GB) y después funciona sin Internet.");
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        if (IconTextButton("##instalar", Ic::Download, eng == StemSeparator::Engine::Error ? "Reintentar la instalación" : "Instalar el motor", col::Green, false, ImVec2(w, 32 * s), true, fontHead, 14 * s))
            stems_.install();
    }
    ImGui::Dummy(ImVec2(0, 4 * s));

    // ---- Zona para soltar
    const ImVec2 zp = ImGui::GetCursorScreenPos();
    const float zh = 72 * s;
    ImGui::InvisibleButton("##zonasep", ImVec2(w, zh));
    const bool dragging = ImGui::GetDragDropPayload() != nullptr;
    const bool hov = ImGui::IsItemHovered() || dragging;
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("RDJ_PISTA"))
            for (auto& f : uic::PayloadPaths(pl)) separateFile(f);
        ImGui::EndDragDropTarget();
    }
    if (ImGui::IsItemClicked()) {
        auto files = win::pickAudioFiles(hwnd_, true);
        for (auto& f : files) separateFile(f);
    }
    dl->AddRectFilled(zp, ImVec2(zp.x + w, zp.y + zh), dragging ? col::withAlpha(col::Violet, 30) : col::withAlpha(col::Panel2, 160), 8 * s);
    const ImU32 zc = hov ? col::Violet : col::Line;
    for (float x = zp.x + 8 * s; x < zp.x + w - 8 * s; x += 10 * s) {
        dl->AddLine(ImVec2(x, zp.y), ImVec2(std::min(x + 5 * s, zp.x + w - 8 * s), zp.y), zc, 1.5f);
        dl->AddLine(ImVec2(x, zp.y + zh), ImVec2(std::min(x + 5 * s, zp.x + w - 8 * s), zp.y + zh), zc, 1.5f);
    }
    for (float y = zp.y + 8 * s; y < zp.y + zh - 8 * s; y += 10 * s) {
        dl->AddLine(ImVec2(zp.x, y), ImVec2(zp.x, std::min(y + 5 * s, zp.y + zh - 8 * s)), zc, 1.5f);
        dl->AddLine(ImVec2(zp.x + w, y), ImVec2(zp.x + w, std::min(y + 5 * s, zp.y + zh - 8 * s)), zc, 1.5f);
    }
    DrawIcon(dl, Ic::Download, ImVec2(zp.x + w * 0.5f, zp.y + 20 * s), 18 * s, hov ? col::Violet : col::TextDim);
    CenterText(dl, ImVec2(zp.x, zp.y + 30 * s), ImVec2(zp.x + w, zp.y + 48 * s), col::Text, "Arrastra aquí una canción", fontHead, 15 * s);
    CenterText(dl, ImVec2(zp.x, zp.y + 48 * s), ImVec2(zp.x + w, zp.y + zh - 4 * s), col::TextDim, "desde la biblioteca o el Explorador · o haz clic", ImGui::GetFont(), 12 * s);
    ImGui::Dummy(ImVec2(0, 4 * s));
    {
        // Modelo: control segmentado.
        const float bw = (w - 4 * s) * 0.5f;
        if (NeonButton("6 pistas##m6", col::Violet, stemModel6_, ImVec2(bw, 26 * s), true, fontHead, 13 * s)) stemModel6_ = true;
        ImGui::SetItemTooltip("Voz, batería, bajo, guitarra, piano y otros");
        ImGui::SameLine(0, 4 * s);
        if (NeonButton("4 pistas · rápido##m4", col::Violet, !stemModel6_, ImVec2(bw, 26 * s), true, fontHead, 13 * s)) stemModel6_ = false;
        ImGui::SetItemTooltip("Voz, batería, bajo y otros");
    }
    ImGui::Dummy(ImVec2(0, 6 * s));

    auto heading = [&](const char* t) {
        const ImVec2 hp = ImGui::GetCursorScreenPos();
        dl->AddText(fontHead, 12 * s, hp, col::TextDim, t);
        const float tw = fontHead->CalcTextSizeA(12 * s, FLT_MAX, 0, t).x;
        dl->AddLine(ImVec2(hp.x + tw + 8 * s, hp.y + 7 * s), ImVec2(hp.x + w, hp.y + 7 * s), col::Line);
        ImGui::Dummy(ImVec2(w, 18 * s));
    };
    // ---- Cola: tarjetas con progreso
    const auto jobs = stems_.jobs();
    if (!jobs.empty()) {
        heading("EN PROCESO");
        bool anyFinished = false;
        for (size_t i = 0; i < jobs.size(); ++i) {
            const auto& j = jobs[i];
            ImGui::PushID((int)i);
            const ImVec2 cp = ImGui::GetCursorScreenPos();
            const float chH = 46 * s;
            dl->AddRectFilled(cp, ImVec2(cp.x + w, cp.y + chH), col::Panel2, 7 * s);
            dl->AddText(fontHead, 13.5f * s, ImVec2(cp.x + 10 * s, cp.y + 6 * s), col::Text, ellipsize(j.title, w - 50 * s, fontHead, 13.5f * s).c_str());
            const ImVec2 pa(cp.x + 10 * s, cp.y + chH - 13 * s), pb(cp.x + w - 40 * s, cp.y + chH - 9 * s);
            dl->AddRectFilled(pa, pb, col::Bg, 2 * s);
            const ImU32 pc = j.failed ? col::Red : j.done ? col::Green : col::Violet;
            const float frac = j.done ? 1.f : std::clamp(j.progress, 0.f, 1.f);
            if (!j.done && !j.failed && j.progress <= 0.f) DrawIcon(dl, Ic::Spinner, ImVec2(cp.x + w - 24 * s, cp.y + chH * 0.5f), 14 * s, col::Violet);
            dl->AddRectFilled(pa, ImVec2(pa.x + (pb.x - pa.x) * frac, pb.y), pc, 2 * s);
            dl->AddText(ImGui::GetFont(), 11.5f * s, ImVec2(cp.x + 10 * s, cp.y + 22 * s), j.failed ? col::Red : col::TextDim,
                        ellipsize(j.failed ? j.state : j.done ? std::string("Terminado") : j.state, w - 60 * s, ImGui::GetFont(), 11.5f * s).c_str());
            if (!j.done && !j.failed && j.state != "En cola") {
                ImGui::SetCursorScreenPos(ImVec2(cp.x + w - 30 * s, cp.y + 6 * s));
                if (IconButton("##cancelar", Ic::Close, ImVec2(22 * s, 22 * s), col::Red, false, true, 0.5f)) stems_.cancelCurrent();
                ImGui::SetItemTooltip("Cancelar la separación");
            }
            ImGui::SetCursorScreenPos(ImVec2(cp.x, cp.y + chH + 6 * s));
            ImGui::Dummy(ImVec2(w, 0));
            anyFinished |= j.done || j.failed;
            ImGui::PopID();
        }
        if (anyFinished && ImGui::SmallButton("Limpiar terminados")) stems_.clearFinished();
    }

    // ---- Separadas: tarjetas con los instrumentos de cada canción
    heading("CANCIONES SEPARADAS");
    if (stemSongs_.empty()) ImGui::TextDisabled("Aún no hay ninguna.");
    ImGui::BeginChild("seplist", ImVec2(0, 0));
    int removeIdx = -1;
    const float lw = ImGui::GetContentRegionAvail().x;
    for (size_t i = 0; i < stemSongs_.size(); ++i) {
        const StemSong& sg = stemSongs_[i];
        ImGui::PushID((int)i);
        const ImVec2 cp = ImGui::GetCursorScreenPos();
        const float chH = 46 * s;
        const bool open = sg.folder == stemOpenFolder_;
        ImGui::InvisibleButton("##cancion", ImVec2(lw, chH));
        const bool h2 = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) openStemSong(sg);
        ImDrawList* ldl = ImGui::GetWindowDrawList();
        ldl->AddRectFilled(cp, ImVec2(cp.x + lw, cp.y + chH), open ? col::mix(col::Panel2, col::Violet, 0.18f) : h2 ? col::mix(col::Panel2, col::Line, 0.5f) : col::Panel2, 7 * s);
        if (open) ldl->AddRectFilled(cp, ImVec2(cp.x + 3 * s, cp.y + chH), col::Violet, 2 * s);
        ldl->AddText(fontHead, 13.5f * s, ImVec2(cp.x + 10 * s, cp.y + 6 * s), col::Text, ellipsize(sg.title, lw - 20 * s, fontHead, 13.5f * s).c_str());
        float x = cp.x + 10 * s;
        for (auto& id : sg.stems) { ldl->AddCircleFilled(ImVec2(x + 4 * s, cp.y + 32 * s), 4 * s, stemColor(id)); x += 11 * s; }
        ldl->AddText(ImGui::GetFont(), 11.5f * s, ImVec2(x + 4 * s, cp.y + 25 * s), col::TextDim, dateText(sg.created).c_str());
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Abrir")) openStemSong(sg);
            if (ImGui::MenuItem("Abrir la carpeta")) win::openFolder(sg.folder);
            if (ImGui::MenuItem("Separar de nuevo")) separateFile(sg.source);
            ImGui::Separator();
            if (ImGui::MenuItem("Eliminar las pistas separadas")) removeIdx = (int)i;
            ImGui::EndPopup();
        }
        ImGui::SetItemTooltip("Clic: abrir · clic derecho: más opciones");
        ImGui::SetCursorScreenPos(ImVec2(cp.x, cp.y + chH + 5 * s));
        ImGui::Dummy(ImVec2(lw, 0));
        ImGui::PopID();
    }
    if (removeIdx >= 0) {
        if (stemSongs_[removeIdx].folder == stemOpenFolder_) {
            engine_.send(Command{Cmd::StemPause, 0});
            Command c{Cmd::StemLoad, 0}; c.stems = nullptr; c.gen = ++stemGen_;
            stemPending_.push_back({c.gen, {}});
            engine_.send(c);
            stemCount_ = 0; stemOpenFolder_.clear();
        }
        stems_.removeSong(stemSongs_[removeIdx].folder);
    }
    ImGui::EndChild();
    ImGui::EndChild();
}

namespace {
Ic stemIcon(const std::string& id) {
    if (id == "voz") return Ic::Mic2;
    if (id == "bateria") return Ic::Drum;
    if (id == "bajo") return Ic::Bass;
    if (id == "guitarra") return Ic::Guitar;
    if (id == "piano") return Ic::Piano;
    return Ic::Waveform;
}
} // namespace

void App::drawStemPlayer(ImVec2 a, ImVec2 b) {
    const float s = S();
    Panel(ImGui::GetWindowDrawList(), a, b, 10 * s, col::Panel, col::Line);
    ImGui::SetCursorScreenPos(a);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * s, 12 * s));
    ImGui::BeginChild("stemplayer", ImVec2(b.x - a.x, b.y - a.y), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 ca = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetContentRegionAvail().y;

    if (stemLoadFuture_.valid()) {
        DrawIcon(dl, Ic::Spinner, ImVec2(ca.x + w * 0.5f, ca.y + h * 0.42f), 26 * s, col::Violet);
        CenterText(dl, ImVec2(ca.x, ca.y + h * 0.48f), ImVec2(ca.x + w, ca.y + h * 0.56f), col::TextDim, "Abriendo las pistas…");
        ImGui::EndChild();
        return;
    }
    if (stemCount_ == 0 || !stemTracks_[0]) {
        // Vacío: qué se puede hacer aquí, con los colores de cada instrumento.
        const float cy = ca.y + h * 0.36f;
        const char* ids[] = {"voz", "bateria", "bajo", "guitarra", "piano", "otros"};
        const float iw = 34 * s, total = 6 * iw;
        for (int i = 0; i < 6; ++i) {
            const ImVec2 c(ca.x + (w - total) * 0.5f + iw * (i + 0.5f), cy);
            dl->AddCircleFilled(c, 13 * s, col::withAlpha(stemColor(ids[i]), 40), 24);
            DrawIcon(dl, stemIcon(ids[i]), c, 16 * s, stemColor(ids[i]));
        }
        CenterText(dl, ImVec2(ca.x, cy + 24 * s), ImVec2(ca.x + w, cy + 50 * s), col::Text, "Separa una canción o abre una de la lista", fontHead, 18 * s);
        CenterText(dl, ImVec2(ca.x, cy + 50 * s), ImVec2(ca.x + w, cy + 72 * s), col::TextDim,
                   "Podrás escuchar cada instrumento por separado, silenciar la voz, exportar y llevar cualquier pista a un plato.");
        ImGui::EndChild();
        return;
    }
    Track* t0 = stemTracks_[0].get();
    const double sr = t0->sampleRate;
    const double len = t0->lengthSec();
    const double pos = engine_.stemStatus.posFrames.load() / sr;
    const bool playing = engine_.stemStatus.playing.load();

    // ---- Cabecera y transporte
    const float th = 34 * s;
    {
        const ImVec2 hp = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(hp);
        if (IconButton("##sp", playing ? Ic::Pause : Ic::Play, ImVec2(46 * s, th), col::Green, true, true, 0.5f)) engine_.send(Command{playing ? Cmd::StemPause : Cmd::StemPlay, 0});
        ImGui::SetItemTooltip("Reproducir / pausa (barra espaciadora)");
        ImGui::SameLine(0, 6 * s);
        if (IconButton("##sst", Ic::Stop, ImVec2(th, th), col::Violet, false, true, 0.42f)) {
            engine_.send(Command{Cmd::StemPause, 0});
            Command sk{Cmd::StemSeek, 0}; sk.d = 0; engine_.send(sk);
        }
        ImGui::SetItemTooltip("Parar y volver al principio");
        float x = hp.x + 46 * s + th + 18 * s;
        dl->AddText(fontHead, 17 * s, ImVec2(x, hp.y - 1 * s), col::Text, ellipsize(stemOpen_.title, w * 0.4f, fontHead, 17 * s).c_str());
        const std::string tt = formatTime(pos) + "  /  " + formatTime(len, false);
        dl->AddText(fontHead, 13.5f * s, ImVec2(x, hp.y + 18 * s), col::TextDim, tt.c_str());
        // Derecha: volumen, auriculares, carpeta
        float rx = hp.x + w;
        rx -= th;
        ImGui::SetCursorScreenPos(ImVec2(rx, hp.y));
        if (IconButton("##sfold", Ic::Folder, ImVec2(th, th), col::Violet, false, true, 0.5f)) win::openFolder(stemOpenFolder_);
        ImGui::SetItemTooltip("Abrir la carpeta de las pistas");
        rx -= th + 6 * s;
        const bool pfl = engine_.stem.pfl.load();
        ImGui::SetCursorScreenPos(ImVec2(rx, hp.y));
        if (IconButton("##spfl", Ic::Headphones, ImVec2(th, th), col::Amber, pfl, true, 0.52f)) engine_.stem.pfl = !pfl;
        ImGui::SetItemTooltip("Enviar también a la preescucha (si hay salida de auriculares)");
        const float vw = std::min(170 * s, w * 0.2f);
        rx -= vw + 12 * s;
        float vol = engine_.stem.volume.load();
        ImGui::SetCursorScreenPos(ImVec2(rx, hp.y + 4 * s));
        if (HFader("##svol", &vol, 0.f, 1.f, 0.8f, ImVec2(vw, th - 8 * s), col::Violet)) engine_.stem.volume = vol;
        ImGui::SetItemTooltip("Volumen del separador: %d %% · doble clic: por defecto", (int)std::lround(vol * 100));
        dl->AddText(fontHead, 11 * s, ImVec2(rx - 30 * s, hp.y + 11 * s), col::TextDim, "VOL");
        ImGui::SetCursorScreenPos(ImVec2(hp.x, hp.y + th + 8 * s));
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Space) && !ImGui::GetIO().WantTextInput) engine_.send(Command{playing ? Cmd::StemPause : Cmd::StemPlay, 0});

    // ---- Atajos: todo, aislar voz, sin voz, silenciar todo (usan el silencio/solo de cada pista).
    int voice = -1;
    for (int k = 0; k < stemCount_; ++k) if (stemIds_[k] == "voz") voice = k;
    {
        const ImVec2 qp = ImGui::GetCursorScreenPos();
        const float qh = 26 * s, qw = std::min(150 * s, (w - 3 * 6 * s) / 4.f);
        bool anyMute = false, anySolo = false;
        for (int k = 0; k < stemCount_; ++k) { anyMute |= engine_.stem.mute[k].load(); anySolo |= engine_.stem.solo[k].load(); }
        const bool isAll = !anyMute && !anySolo;
        const bool isIsolated = voice >= 0 && anySolo && engine_.stem.solo[voice].load();
        const bool isNoVoice = voice >= 0 && !anySolo && engine_.stem.mute[voice].load();
        bool allMuted = stemCount_ > 0;
        for (int k = 0; k < stemCount_; ++k) allMuted &= engine_.stem.mute[k].load();
        auto clearAll = [&] { for (int k = 0; k < stemCount_; ++k) { engine_.stem.mute[k] = false; engine_.stem.solo[k] = false; } };
        ImGui::SetCursorScreenPos(qp);
        if (IconTextButton("##todo", Ic::Waveform, "Todo", col::Violet, isAll, ImVec2(qw, qh), true, fontHead, 13 * s)) clearAll();
        ImGui::SetItemTooltip("Que suenen todas las pistas");
        ImGui::SameLine(0, 6 * s);
        if (IconTextButton("##aislar", Ic::Mic2, "Aislar voz", stemColor("voz"), isIsolated, ImVec2(qw, qh), voice >= 0, fontHead, 13 * s)) { clearAll(); engine_.stem.solo[voice] = true; }
        ImGui::SetItemTooltip("Solo la voz (acapella)");
        ImGui::SameLine(0, 6 * s);
        if (IconTextButton("##sinvoz", Ic::Mute, "Sin voz", stemColor("voz"), isNoVoice, ImVec2(qw, qh), voice >= 0, fontHead, 13 * s)) { clearAll(); engine_.stem.mute[voice] = true; }
        ImGui::SetItemTooltip("Todo menos la voz (instrumental)");
        ImGui::SameLine(0, 6 * s);
        if (IconTextButton("##silenciar", Ic::Mute, "Silenciar todo", col::Red, allMuted, ImVec2(qw, qh), true, fontHead, 13 * s)) {
            clearAll();
            for (int k = 0; k < stemCount_; ++k) engine_.stem.mute[k] = true;
        }
        ImGui::SetItemTooltip("Silencia todas las pistas (luego activa las que quieras con M)");
        ImGui::SetCursorScreenPos(ImVec2(qp.x, qp.y + qh + 8 * s));
    }

    // ---- Carriles
    const float actionsH = 36 * s;
    const ImVec2 lp = ImGui::GetCursorScreenPos();
    const float laneArea = (ca.y + h) - lp.y - actionsH - 10 * s;
    const float laneH = std::clamp((laneArea - (stemCount_ - 1) * 6 * s) / std::max(1, stemCount_), 40 * s, 110 * s);
    const float ctrlW = std::clamp(w * 0.26f, 230 * s, 300 * s);
    bool anySolo = false;
    for (int k = 0; k < stemCount_; ++k) if (engine_.stem.solo[k].load()) anySolo = true;
    for (int k = 0; k < stemCount_; ++k) {
        const std::string& id = stemIds_[k];
        const ImU32 sc = stemColor(id);
        const float y = lp.y + k * (laneH + 6 * s);
        const ImVec2 la(lp.x, y), lb(lp.x + w, y + laneH);
        const bool muted = engine_.stem.mute[k].load(), solo = engine_.stem.solo[k].load();
        const bool audible = !muted && (!anySolo || solo);
        dl->AddRectFilled(la, lb, col::Panel2, 7 * s);
        dl->AddRectFilled(la, ImVec2(la.x + 4 * s, lb.y), audible ? sc : col::withAlpha(sc, 80), 3 * s, ImDrawFlags_RoundCornersLeft);
        // Icono, nombre y nivel
        const float row1 = la.y + std::min(8 * s, laneH * 0.12f);
        dl->AddCircleFilled(ImVec2(la.x + 24 * s, row1 + 10 * s), 11 * s, col::withAlpha(sc, audible ? 50 : 20), 24);
        DrawIcon(dl, stemIcon(id), ImVec2(la.x + 24 * s, row1 + 10 * s), 14 * s, audible ? sc : col::withAlpha(sc, 110));
        dl->AddText(fontHead, 15 * s, ImVec2(la.x + 42 * s, row1 + 1 * s), audible ? col::Text : col::TextDim, stemLabel(id));
        const float pk = engine_.stemStatus.peak[k].exchange(0.f);
        const float mw = std::min(1.f, pk) * (ctrlW - 56 * s);
        dl->AddRectFilled(ImVec2(la.x + 42 * s, row1 + 21 * s), ImVec2(la.x + ctrlW - 14 * s, row1 + 24 * s), col::withAlpha(col::Bg, 200), 1.f);
        dl->AddRectFilled(ImVec2(la.x + 42 * s, row1 + 21 * s), ImVec2(la.x + 42 * s + mw, row1 + 24 * s), col::withAlpha(sc, audible ? 230 : 90), 1.f);
        // M · S · volumen · menú
        ImGui::PushID(k);
        const float by = std::min(lb.y - 30 * s, row1 + 32 * s);
        ImGui::SetCursorScreenPos(ImVec2(la.x + 12 * s, by));
        if (NeonButton("M##m", col::Red, muted, ImVec2(30 * s, 24 * s), true, fontHead, 13 * s)) engine_.stem.mute[k] = !muted;
        ImGui::SetItemTooltip("Silenciar %s", stemLabel(id));
        ImGui::SameLine(0, 4 * s);
        if (NeonButton("S##s", col::Amber, solo, ImVec2(30 * s, 24 * s), true, fontHead, 13 * s)) engine_.stem.solo[k] = !solo;
        ImGui::SetItemTooltip("Solo: escuchar solo esta pista (se pueden combinar)");
        ImGui::SameLine(0, 8 * s);
        float g = engine_.stem.gain[k].load() / 1.5f;
        const float gw = ctrlW - 12 * s - 64 * s - 8 * s - 34 * s - 10 * s;
        if (HFader("##g", &g, 0.f, 1.f, 1.f / 1.5f, ImVec2(gw, 24 * s), sc)) engine_.stem.gain[k] = g * 1.5f;
        ImGui::SetItemTooltip("Volumen de %s: %d %% · doble clic: 100 %%", stemLabel(id), (int)std::lround(g * 150));
        ImGui::SameLine(0, 6 * s);
        if (IconButton("##menu", Ic::Dots, ImVec2(28 * s, 24 * s), sc, false, true, 0.6f)) ImGui::OpenPopup("stemmenu");
        if (ImGui::BeginPopup("stemmenu")) {
            ImGui::TextDisabled("%s · EQ y FX en directo", stemLabel(id));
            auto& sf = engine_.stem.fx[k];
            auto slider = [](const char* label, std::atomic<float>& param) {
                float v = param.load(); if (ImGui::SliderFloat(label,&v,0.f,1.f,"%.2f")) param = v;
            };
            slider("Graves",engine_.stem.low[k]); slider("Medios",engine_.stem.mid[k]); slider("Agudos",engine_.stem.high[k]);
            if (ImGui::BeginCombo("Efecto",stFxName(sf.type.load()))) {
                for (int t=0;t<(int)StFx::Count;++t) {
                    if (ImGui::Selectable(stFxName(t),sf.type.load()==t)) { sf.type=t; sf.div=stFxDefaultDiv(t); }
                }
                ImGui::EndCombo();
            }
            bool on=sf.on.load(); if (ImGui::Checkbox("FX encendido",&on)) sf.on=on;
            slider("Intensidad",sf.amount); slider("Mezcla FX",sf.mix);
            int div=sf.div.load();
            if (ImGui::BeginCombo("Tiempo FX",stDivName(div))) {
                for (int d=0;d<kStDivs;++d) if (stFxDivAllowed(sf.type.load(),d) && ImGui::Selectable(stDivName(d),div==d)) sf.div=d;
                ImGui::EndCombo();
            }
            float bpm=engine_.stem.bpm.load();
            if (ImGui::SliderFloat("BPM efectos",&bpm,40.f,220.f,"%.1f")) engine_.stem.bpm=bpm;
            ImGui::TextWrapped("BPM solo para los efectos: no cambia el tempo del audio. Exportar WAV usa la mezcla seca.");
            if (ImGui::Button("Restablecer EQ y FX")) {
                engine_.stem.low[k]=engine_.stem.mid[k]=engine_.stem.high[k]=.5f;
                sf.on=false;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Cargar en el plato A")) loadStemToDeck(0, id);
            if (ImGui::MenuItem("Cargar en el plato B")) loadStemToDeck(1, id);
            if (ImGui::MenuItem("Guardar como WAV…")) {
                const std::wstring f = win::pickSaveFile(hwnd_, fromUtf8(stemOpen_.title + " (" + stemLabel(id) + ").wav"), L"Audio WAV", L"*.wav", L"wav");
                if (!f.empty()) {
                    bool savedSolo[kStemSlots];
                    for (int j=0;j<stemCount_;++j) { savedSolo[j]=engine_.stem.solo[j].load(); engine_.stem.solo[j]=(j==k); }
                    exportStemMix(f, false, -1);
                    for (int j=0;j<stemCount_;++j) engine_.stem.solo[j]=savedSolo[j];
                }
            }
            if (ImGui::MenuItem("Mostrar el archivo")) win::revealInExplorer(stemOpenFolder_ + L"\\" + fromUtf8(id) + L".flac");
            ImGui::EndPopup();
        }
        ImGui::PopID();
        // Forma de onda del instrumento: lo ya sonado más apagado; las pistas que no suenan, en gris.
        Track* t = stemTracks_[k].get();
        const ImVec2 wa(la.x + ctrlW, la.y + 5 * s), wb(lb.x - 8 * s, lb.y - 5 * s);
        if (t && !t->wave.empty()) {
            const float W = wb.x - wa.x, cy = (wa.y + wb.y) * 0.5f, hh = (wb.y - wa.y) * 0.5f;
            const int bins = (int)t->wave.size();
            const float px = (float)(pos / len * W);
            const ImU32 on = audible ? sc : col::mix(col::TextDim, col::Panel2, 0.4f);
            dl->AddRectFilled(wa, wb, col::withAlpha(col::WaveBg, 200), 4 * s);
            for (int x = 0; x < (int)W; ++x) {
                const int b0 = (int)((double)x / W * bins), b1 = std::max(b0 + 1, (int)((double)(x + 1) / W * bins));
                int p = 0, r = 0;
                for (int q = b0; q < b1 && q < bins; ++q) { p = std::max(p, (int)t->wave[q].peak); r = std::max(r, (int)t->wave[q].rms); }
                const float amp = std::min(1.f, p / 255.f / std::max(0.05f, t->maxPeak)) * hh;
                const float ramp = std::min(amp, r / 255.f * hh);
                const bool past = x < px;
                dl->AddLine(ImVec2(wa.x + x, cy - amp), ImVec2(wa.x + x, cy + amp + 1), col::withAlpha(on, past ? 90 : 170));
                dl->AddLine(ImVec2(wa.x + x, cy - ramp), ImVec2(wa.x + x, cy + ramp + 1), col::withAlpha(on, past ? 130 : 255));
            }
            dl->AddLine(ImVec2(wa.x + px, wa.y), ImVec2(wa.x + px, wb.y), col::Text, 1.5f * s);
            dl->AddTriangleFilled(ImVec2(wa.x + px - 4 * s, wa.y), ImVec2(wa.x + px + 4 * s, wa.y), ImVec2(wa.x + px, wa.y + 5 * s), col::Text);
            ImGui::SetCursorScreenPos(wa);
            ImGui::InvisibleButton(("##wf" + std::to_string(k)).c_str(), ImVec2(W, wb.y - wa.y));
            if (ImGui::IsItemActive()) {
                const double f = std::clamp((ImGui::GetIO().MousePos.x - wa.x) / W, 0.f, 1.f);
                Command sk{Cmd::StemSeek, 0}; sk.d = f * t0->frames; engine_.send(sk);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clic para ir a %s", formatTime(std::clamp((ImGui::GetIO().MousePos.x - wa.x) / W, 0.f, 1.f) * len).c_str());
        }
    }

    // ---- Acciones: a los platos y exportar
    ImGui::SetCursorScreenPos(ImVec2(ca.x, ca.y + h - actionsH));
    const float bw = (w - 3 * 6 * s) / 4.f;
    const bool hasVoice = voice >= 0;
    if (IconTextButton("##aca", Ic::Mic2, "Voz al plato A", col::DeckA, false, ImVec2(bw, actionsH), hasVoice, fontHead, 13.5f * s)) loadStemToDeck(0, "voz");
    ImGui::SetItemTooltip("Carga solo la voz (acapella) en el plato A");
    ImGui::SameLine(0, 6 * s);
    const bool busy = stemExport_.valid();
    if (IconTextButton("##ins", busy ? Ic::Spinner : Ic::Waveform, busy ? "Preparando…" : "Instrumental al plato B", col::DeckB, busy, ImVec2(bw, actionsH), hasVoice && !busy, fontHead, 13.5f * s)) {
        const std::wstring f = stemOpenFolder_ + L"\\instrumental.wav";
        exportStemMix(f, true, 1);
    }
    ImGui::SetItemTooltip("Mezcla todo menos la voz y lo carga en el plato B");
    ImGui::SameLine(0, 6 * s);
    if (IconTextButton("##exp", Ic::Download, "Exportar lo que suena…", col::Violet, false, ImVec2(bw, actionsH), !busy, fontHead, 13.5f * s)) {
        const std::wstring f = win::pickSaveFile(hwnd_, fromUtf8(stemOpen_.title + " (mezcla de pistas).wav"), L"Audio WAV", L"*.wav", L"wav");
        if (!f.empty()) exportStemMix(f, false, -1);
    }
    ImGui::SetItemTooltip("Guarda un WAV con las pistas tal como están ahora (volúmenes, silencio y solo)");
    ImGui::SameLine(0, 6 * s);
    if (IconTextButton("##orig", Ic::Disc, "Original al plato A", col::Green, false, ImVec2(bw, actionsH), GetFileAttributesW(stemOpen_.source.c_str()) != INVALID_FILE_ATTRIBUTES, fontHead, 13.5f * s)) {
        mode_ = 0;
        loadToDeck(0, stemOpen_.source);
    }
    ImGui::SetItemTooltip("Carga la canción completa original en el plato A");
    ImGui::EndChild();
}

} // namespace rdj

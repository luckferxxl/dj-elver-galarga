// Navegador de pistas al estilo VirtualDJ: árbol de carpetas y listas a la
// izquierda, lista de archivos en el centro, Sidelist (lista contigua) con
// pestañas abajo y detalles de la pista a la derecha. Los paneles se pueden
// ocultar y cambiar de ancho.
#include "app.h"
#include "covers.h"
#include "ui_common.h"
#include "widgets.h"
#include "winutil.h"
#include "../audio/analysis.h"
#include "../core/util.h"
#include "imgui_internal.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <functional>
#include <set>
#include <filesystem>
#include "../core/tags.h"

namespace rdj {

using namespace ui;
using uic::ellipsize;
using uic::BeginPanel;

namespace {
float S() { return ImGui::GetFontSize() / 16.f; }

std::string dateText(int64_t t, bool withTime) {
    if (t <= 0) return "";
    std::tm lt{};
    const std::time_t tt = (std::time_t)t;
    localtime_s(&lt, &tt);
    char b[32];
    std::strftime(b, sizeof b, withTime ? "%d-%m-%Y %H:%M" : "%d-%m-%Y", &lt);
    return b;
}

// "Track (Extended Mix)" -> "Extended Mix" (columna Remix, como en VirtualDJ).
std::string remixOf(const std::string& title) {
    if (title.empty()) return "";
    const char close = title.back();
    if (close != ')' && close != ']') return "";
    const char open = close == ')' ? '(' : '[';
    const size_t p = title.find_last_of(open);
    if (p == std::string::npos || p + 2 >= title.size()) return "";
    return title.substr(p + 1, title.size() - p - 2);
}

bool isHistoryName(const std::string& n) { return n.rfind("Historial ", 0) == 0; }
bool isHiddenList(const std::string& n) { return n == "Sidelist" || n == "Automix" || n == "Favoritos" || n == "Grabaciones" || isHistoryName(n); }
int64_t historyKey(const std::string& n) {
    int d = 0, m = 0, y = 0;
    if (std::sscanf(n.c_str() + 10, "%d-%d-%d", &d, &m, &y) == 3) return y * 10000LL + m * 100 + d;
    return 0;
}
std::string todayHistoryName() {
    std::time_t tt = std::time(nullptr);
    std::tm lt{};
    localtime_s(&lt, &tt);
    char name[48];
    std::strftime(name, sizeof name, "Historial %d-%m-%Y", &lt);
    return name;
}
bool samePath(const std::wstring& a, const std::wstring& b) {
    auto trim = [](std::wstring s) { while (s.size() > 3 && (s.back() == L'\\' || s.back() == L'/')) s.pop_back(); return s; };
    return _wcsicmp(trim(a).c_str(), trim(b).c_str()) == 0;
}
std::wstring parentDir(const std::wstring& p) {
    const size_t k = p.find_last_of(L"\\/");
    return k == std::wstring::npos ? L"" : p.substr(0, k);
}
std::string durText(double sec) {
    const int h = (int)(sec / 3600);
    char b[48];
    if (h > 0) std::snprintf(b, sizeof b, "%d h %02d min", h, (int)(sec / 60) % 60);
    else std::snprintf(b, sizeof b, "%d min", (int)std::lround(sec / 60));
    return b;
}
} // namespace

// =================================================================== utilidades

ImGuiID App::rowStorageId(ImGuiSelectionBasicStorage* self, int idx) {
    const App* a = (const App*)self->UserData;
    if (idx < 0 || idx >= (int)a->view_.size()) return 0;
    const ViewRow& r = a->view_[idx];
    if (!a->listView_ && r.track >= 0) return (ImGuiID)r.track + 1;
    return 0x80000000u | (ImGuiID)(idx + 1);
}

std::vector<std::wstring> App::selectedPaths() {
    std::vector<std::wstring> out;
    for (int i = 0; i < (int)view_.size(); ++i)
        if (sel_.Contains(rowStorageId(&sel_, i))) out.push_back(view_[i].path);
    return out;
}

bool App::isFavorite(const std::wstring& path) {
    if (path.empty()) return false;
    std::lock_guard<std::recursive_mutex> lk(library_.mutex());
    for (auto& pl : library_.playlists())
        if (pl.name == "Favoritos") { for (auto& p : pl.paths) if (p == path) return true; return false; }
    return false;
}

void App::toggleFavorite(const std::wstring& path) {
    if (path.empty()) return;
    const int fav = ensurePlaylist("Favoritos");
    std::lock_guard<std::recursive_mutex> lk(library_.mutex());
    auto& v = library_.playlists()[fav].paths;
    auto it = std::find(v.begin(), v.end(), path);
    if (it != v.end()) { v.erase(it); notify("Quitada de favoritos."); }
    else { v.push_back(path); notify("Añadida a favoritos."); }
    library_.markDirty();
}

void App::addToSidelist(const std::vector<std::wstring>& paths) {
    if (paths.empty()) return;
    library_.addToPlaylist(ensurePlaylist("Sidelist"), paths);
    notify(paths.size() == 1 ? "Añadida a la Sidelist." : std::to_string(paths.size()) + " pistas añadidas a la Sidelist.");
}

void App::loadFree(const std::wstring& path) {
    const int target = !engine_.deckStatus[0].playing.load() ? 0 : !engine_.deckStatus[1].playing.load() ? 1 : -1;
    if (target >= 0) loadToDeck(target, path);
    else notify("Los dos platos están sonando: arrastra la pista al plato que quieras sustituir.", true);
}

void App::deletePlaylistAt(int i) {
    if (src_.kind == Src::Lista) {
        if (src_.list == i) { src_ = BrowseSrc{}; ++srcGen_; }
        else if (src_.list > i) { --src_.list; ++srcGen_; }
    }
    library_.deletePlaylist(i);
    ++srcGen_;
}

void App::selectSource(const BrowseSrc& s) {
    if (s == src_) return;
    src_ = s;
    ++srcGen_;
    sel_.Clear();
    cursorRow_ = -1;
    if (s.kind == Src::Carpeta) library_.browseFolder(s.folder, settings_.browseRecursive);
    rememberSource();
}

void App::rememberSource() {
    settings_.browseKind = src_.kind == Src::Carpeta ? 1 : src_.kind == Src::Lista ? 2 : src_.kind == Src::Sugerencias ? 3 : src_.kind == Src::Smart ? 4 : 0;
    settings_.browseRef.clear();
    if (src_.kind==Src::Smart && src_.list>=0 && src_.list<(int)settings_.smartLists.size()) settings_.browseRef=settings_.smartLists[src_.list].name;
    if (src_.kind == Src::Carpeta) settings_.browseRef = toUtf8(src_.folder);
    else if (src_.kind == Src::Lista) {
        std::lock_guard<std::recursive_mutex> lk(library_.mutex());
        if (src_.list >= 0 && src_.list < (int)library_.playlists().size()) settings_.browseRef = library_.playlists()[src_.list].name;
    }
}

void App::restoreSource() {
    BrowseSrc s;
    if (settings_.browseKind == 1 && win::folderExists(fromUtf8(settings_.browseRef))) { s.kind = Src::Carpeta; s.folder = fromUtf8(settings_.browseRef); }
    else if (settings_.browseKind == 2) {
        const int i = library_.findPlaylist(settings_.browseRef);
        if (i >= 0) { s.kind = Src::Lista; s.list = i; }
    } else if (settings_.browseKind == 3) s.kind = Src::Sugerencias;
    else if (settings_.browseKind==4) for (int i=0;i<(int)settings_.smartLists.size();++i) if (settings_.smartLists[i].name==settings_.browseRef) { s.kind=Src::Smart; s.list=i; break; }
    src_ = s;
    ++srcGen_;
    if (s.kind == Src::Carpeta) library_.browseFolder(s.folder, settings_.browseRecursive);
}

void App::buildFsTree() {
    fsBuilt_ = true;
    fsRoots_.clear();
    const struct { int which; const char* name; Ic ic; } user[] = {
        {0, "Escritorio", Ic::Monitor}, {1, "Música", Ic::Note}, {2, "Descargas", Ic::Download}, {3, "Documentos", Ic::Document}, {4, "Vídeos", Ic::Disc}};
    for (auto& u : user) {
        const std::wstring p = win::userFolder(u.which);
        if (!p.empty()) fsRoots_.push_back(FsNode{p, u.name, (int)u.ic});
    }
    for (auto& d : win::drives())
        fsRoots_.push_back(FsNode{d.root, d.label, (int)(d.type == 1 ? Ic::Usb : d.type == 2 ? Ic::Globe : d.type == 3 ? Ic::Disc : Ic::Drive)});
}

void App::openNameDialog(int mode, const std::string& initial, std::vector<std::wstring> paths) {
    nameMode_ = mode;
    std::snprintf(renameBuf_, sizeof renameBuf_, "%s", initial.c_str());
    namePaths_ = std::move(paths);
    openNamePopup_ = true;
}

void App::drawNameDialog() {
    const float s = S();
    const char* titles[4] = {"Renombrar la lista", "Nueva lista", "Guardar la Sidelist como lista", "Nueva lista con las pistas"};
    if (openNamePopup_) { ImGui::OpenPopup("##nombrelista"); openNamePopup_ = false; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("##nombrelista", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::PushFont(fontHead, 18.f);
        ImGui::TextUnformatted(titles[std::clamp(nameMode_, 0, 3)]);
        ImGui::PopFont();
        ImGui::Spacing();
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(320 * s);
        const bool enter = ImGui::InputTextWithHint("##nombre", "Nombre de la lista", renameBuf_, sizeof renameBuf_,
                                                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        ImGui::Spacing();
        const bool ok = NeonButton("Guardar##nl", col::DeckA, true, ImVec2(156 * s, 30 * s), renameBuf_[0] != 0, fontHead, 15 * s) || (enter && renameBuf_[0]);
        ImGui::SameLine();
        const bool cancel = NeonButton("Cancelar##nl", col::DeckA, false, ImVec2(156 * s, 30 * s), true, fontHead, 15 * s) || ImGui::IsKeyPressed(ImGuiKey_Escape);
        if (ok) {
            if (nameMode_ == 0) {
                std::lock_guard<std::recursive_mutex> lk(library_.mutex());
                auto& pls = library_.playlists();
                if (renameList_ >= 0 && renameList_ < (int)pls.size()) pls[renameList_].name = renameBuf_;
                library_.markDirty();
            } else {
                const int i = library_.createPlaylist(renameBuf_);
                std::vector<std::wstring> paths = namePaths_;
                if (nameMode_ == 2) {
                    std::lock_guard<std::recursive_mutex> lk(library_.mutex());
                    const int sl = library_.findPlaylist("Sidelist");
                    if (sl >= 0) paths = library_.playlists()[sl].paths;
                }
                library_.addToPlaylist(i, paths);
                selectSource(BrowseSrc{Src::Lista, L"", i});
                treeOpen_["listas"] = true;
                notify("Lista «" + std::string(renameBuf_) + "» creada.");
            }
            library_.save();
            rememberSource();
            ImGui::CloseCurrentPopup();
        } else if (cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void App::removeRowsFromList(int list, std::vector<int> positions) {
    std::lock_guard<std::recursive_mutex> lk(library_.mutex());
    auto& pls = library_.playlists();
    if (list < 0 || list >= (int)pls.size()) return;
    std::sort(positions.rbegin(), positions.rend());
    positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
    for (int p : positions) if (p >= 0 && p < (int)pls[list].paths.size()) pls[list].paths.erase(pls[list].paths.begin() + p);
    library_.markDirty();
    sel_.Clear();
}

// Menú de una lista (árbol y cabecera).
void App::listMenu(int list) {
    std::string name;
    size_t count = 0;
    {
        std::lock_guard<std::recursive_mutex> lk(library_.mutex());
        if (list < 0 || list >= (int)library_.playlists().size()) return;
        name = library_.playlists()[list].name;
        count = library_.playlists()[list].paths.size();
    }
    if (ImGui::MenuItem("Renombrar…", nullptr, false, !isHistoryName(name) && name != "Sidelist" && name != "Automix" && name != "Favoritos")) {
        renameList_ = list;
        openNameDialog(0, name);
    }
    if (ImGui::MenuItem("Duplicar como lista nueva…")) {
        std::vector<std::wstring> paths;
        { std::lock_guard<std::recursive_mutex> lk(library_.mutex()); paths = library_.playlists()[list].paths; }
        openNameDialog(3, name + " (copia)", paths);
    }
    if (ImGui::MenuItem("Añadir todo a la Sidelist", nullptr, false, count > 0)) {
        std::vector<std::wstring> paths;
        { std::lock_guard<std::recursive_mutex> lk(library_.mutex()); paths = library_.playlists()[list].paths; }
        addToSidelist(paths);
    }
    if (ImGui::MenuItem("Pasar a la lista de Automix", nullptr, false, count > 0)) {
        std::vector<std::wstring> paths;
        { std::lock_guard<std::recursive_mutex> lk(library_.mutex()); paths = library_.playlists()[list].paths; }
        const int am = ensurePlaylist("Automix");
        { std::lock_guard<std::recursive_mutex> lk(library_.mutex()); library_.playlists()[am].paths = paths; library_.markDirty(); }
        automixIndex_ = 0;
        settings_.sideTab = 1;
        settings_.showSide = true;
        notify("Lista de Automix preparada con «" + name + "».");
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Exportar M3U8…")) {
        const std::wstring f = win::pickSaveFile(hwnd_, fromUtf8(name) + L".m3u8", L"Lista M3U8", L"*.m3u8", L"m3u8");
        if (!f.empty()) notify(library_.exportM3u(list, f) ? "Lista exportada a " + toUtf8(f) : "No se pudo exportar la lista.", false);
    }
    if (ImGui::MenuItem("Vaciar la lista", nullptr, false, count > 0)) {
        std::lock_guard<std::recursive_mutex> lk(library_.mutex());
        library_.playlists()[list].paths.clear();
        library_.markDirty();
    }
    if (ImGui::MenuItem("Eliminar la lista", nullptr, false, name != "Sidelist" && name != "Automix")) deletePlaylistAt(list);
}

// Menú de una o varias pistas (lista central, Sidelist).
void App::trackMenu(const std::vector<std::wstring>& paths, bool inList) {
    if (paths.empty()) return;
    const bool one = paths.size() == 1;
    if (ImGui::MenuItem("Cargar en el plato A", nullptr, false, one)) loadToDeck(0, paths[0]);
    if (ImGui::MenuItem("Cargar en el plato B", nullptr, false, one)) loadToDeck(1, paths[0]);
    ImGui::Separator();
    if (ImGui::MenuItem("Añadir a la Sidelist")) addToSidelist(paths);
    if (ImGui::MenuItem("Añadir al Automix")) { library_.addToPlaylist(ensurePlaylist("Automix"), paths); notify("Añadidas al Automix."); }
    if (ImGui::BeginMenu("Añadir a una lista")) {
        std::vector<std::pair<int, std::string>> lists;
        {
            std::lock_guard<std::recursive_mutex> lk(library_.mutex());
            auto& pls = library_.playlists();
            for (size_t i = 0; i < pls.size(); ++i) if (!isHiddenList(pls[i].name)) lists.push_back({(int)i, pls[i].name});
        }
        for (auto& [i, n] : lists) if (ImGui::MenuItem(n.c_str())) { library_.addToPlaylist(i, paths); notify("Añadidas a «" + n + "»."); }
        if (!lists.empty()) ImGui::Separator();
        if (ImGui::MenuItem("Lista nueva con estas pistas…")) openNameDialog(3, "Nueva lista", paths);
        ImGui::EndMenu();
    }
    if (!one && ImGui::MenuItem("Buscar en Internet las carátulas que falten")) fetchCovers(paths, true);
    if (one && ImGui::BeginMenu("Carátula")) {
        if (ImGui::MenuItem("Buscar en Internet (iTunes / Deezer)")) fetchCovers(paths, true);
        if (ImGui::MenuItem("Asignar imagen…")) {
            const auto image = win::pickImage(hwnd_);
            if (!image.empty()) {
                std::error_code ec;
                const std::filesystem::path dest(coverOverridePath(paths[0]));
                const auto bytes=std::filesystem::file_size(image,ec);
                if (!ec && bytes>16u*1024*1024) ec=std::make_error_code(std::errc::file_too_large);
                if (!ec) std::filesystem::create_directories(dest.parent_path(), ec);
                if (!ec) std::filesystem::copy_file(image, dest, std::filesystem::copy_options::overwrite_existing, ec);
                if (ec) notify("No se pudo asignar la carátula: " + ec.message(), true);
                else { covers().invalidate(paths[0]); notify("Carátula asignada sin modificar el audio."); }
            }
        }
        if (ImGui::MenuItem("Recargar carátulas")) covers().clear();
        if (ImGui::MenuItem("Quitar la asignada / descargada (usar la del archivo)")) {
            std::error_code ec; std::filesystem::remove(coverOverridePath(paths[0]), ec);
            if (ec) notify("No se pudo quitar la carátula asignada.", true); else covers().invalidate(paths[0]);
        }
        ImGui::EndMenu();
    }
    if (one) {
        TrackMeta km;
        const bool inLib = library_.metaFor(paths[0], km);
        if (ImGui::BeginMenu("Tonalidad", inLib)) {
            if (km.key >= 0) {
                ImGui::TextColored(toVec4(col::camelot(camelotIndex(km.key))), "%s · %s", keyCode(km.key).c_str(), keyNameEs(km.key));
                if (km.keySrc) ImGui::TextDisabled("%s", keySourceText(km.keySrc));
                ImGui::Separator();
                if (ImGui::MenuItem("Ver en la lista las que encajan con esta")) setKeyFilter(km.key);
            }
            if (keyFilter_ >= 0 && ImGui::MenuItem("Quitar el filtro de tonalidad")) setKeyFilter(-1);
            if (ImGui::BeginMenu("Ponerla a mano")) {
                ImGui::TextDisabled("Elige en la rueda la tonalidad correcta:");
                const float D = 230 * S();
                const ImVec2 tl = ImGui::GetCursorScreenPos();
                const int k = drawCamelotWheel("##ruedamano", km.key, tl, D, nullptr, true);
                ImGui::SetCursorScreenPos(ImVec2(tl.x, tl.y + D + 2 * S()));
                ImGui::Dummy(ImVec2(D, 0));
                if (k >= 0) { setManualKey(paths[0], k); ImGui::CloseCurrentPopup(); }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
    }
    if (ImGui::BeginMenu("Etiquetas y color")) {
        for (const char* label : {"Apertura", "Vocal", "Subidón", "Cierre", "Techno"}) {
            if (ImGui::MenuItem(label)) {
                for (const auto& path : paths) {
                    auto& tags = settings_.trackTags[toUtf8(path)];
                    if (foldForSearch(tags).find(foldForSearch(label)) == std::string::npos) tags += (tags.empty() ? "" : ", ") + std::string(label);
                }
                sortDirty_ = true; settings_.save();
            }
        }
        const char* colors[] = {"Sin color", "Turquesa", "Ámbar", "Violeta", "Coral"};
        for (int c = 0; c < 5; ++c) if (ImGui::MenuItem(colors[c])) {
            for (const auto& path : paths) settings_.trackColors[toUtf8(path)] = c;
            settings_.save();
        }
        if (ImGui::MenuItem("Quitar etiquetas")) {
            for (const auto& path : paths) settings_.trackTags.erase(toUtf8(path));
            sortDirty_ = true; settings_.save();
        }
        ImGui::EndMenu();
    }
    if (one) {
        const bool fav = isFavorite(paths[0]);
        if (ImGui::MenuItem(fav ? "Quitar de favoritos" : "Añadir a favoritos")) toggleFavorite(paths[0]);
    } else if (ImGui::MenuItem("Añadir a favoritos")) {
        library_.addToPlaylist(ensurePlaylist("Favoritos"), paths);
        notify(std::to_string(paths.size()) + " pistas añadidas a favoritos.");
    }
    if (ImGui::MenuItem("Separar en pistas (voz e instrumentos)…", nullptr, false, one)) separateFile(paths[0]);
    if (inList && ImGui::MenuItem("Quitar de la lista", "Supr")) {
        if (src_.kind == Src::Lista) {
            std::vector<int> pos;
            for (int i = 0; i < (int)view_.size(); ++i)
                if (sel_.Contains(rowStorageId(&sel_, i)) && view_[i].listPos >= 0) pos.push_back(view_[i].listPos);
            removeRowsFromList(src_.list, pos);
        }
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Ir a su carpeta", nullptr, false, one)) {
        BrowseSrc s;
        s.kind = Src::Carpeta;
        s.folder = parentDir(paths[0]);
        selectSource(s);
    }
    if (ImGui::MenuItem("Mostrar en el Explorador", nullptr, false, one)) win::revealInExplorer(paths[0]);
}

void App::testAddFolder(const std::wstring& f) {
    library_.addFolder(f);
    autoAnalyze_ = true;
}
void App::testBrowse(const std::wstring& f) {
    if (f.rfind(L"lista:", 0) == 0) {
        const int i = library_.findPlaylist(toUtf8(f.substr(6)));
        if (i >= 0) selectSource(BrowseSrc{Src::Lista, L"", i});
        treeOpen_["listas"] = true;
        return;
    }
    BrowseSrc s;
    s.kind = Src::Carpeta;
    s.folder = f;
    selectSource(s);
    // Desplegar el árbol hasta esa carpeta.
    treeOpen_["pc"] = true;
    for (std::wstring p = parentDir(f); p.size() > 2; p = parentDir(p)) treeOpen_["fs:" + toUtf8(p)] = true;
}
void App::testSidelist(const std::wstring& m3u) {
    const int i = library_.importM3u(m3u);
    if (i < 0) return;
    std::vector<std::wstring> paths;
    { std::lock_guard<std::recursive_mutex> lk(library_.mutex()); paths = library_.playlists()[i].paths; }
    library_.addToPlaylist(ensurePlaylist("Sidelist"), paths);
    treeOpen_["listas"] = true;
}

// =================================================================== vista (filas de la lista central)

void App::suggestions(std::vector<ViewRow>& out) {
    // Plato de referencia: el que suena (si suenan los dos, el del lado del crossfader).
    const bool p0 = engine_.deckStatus[0].playing.load() && deck_[0].track, p1 = engine_.deckStatus[1].playing.load() && deck_[1].track;
    int ref = -1;
    if (p0 && p1) ref = engine_.mixer.crossfader.load() <= 0.5f ? 0 : 1;
    else if (p0) ref = 0;
    else if (p1) ref = 1;
    else if (deck_[0].track) ref = 0;
    else if (deck_[1].track) ref = 1;
    if (settings_.suggestDeck > 0) ref = settings_.suggestDeck - 1;
    if (ref < 0 || !deck_[ref].track) return;
    const double refBpm = deck_[ref].meta.bpm > 0 ? engine_.effectiveBpm(ref) : 0;
    const int refKey = deck_[ref].meta.key;
    std::set<std::wstring> today;
    auto& pls = library_.playlists();
    for (auto& pl : pls) if (pl.name == todayHistoryName()) today.insert(pl.paths.begin(), pl.paths.end());
    std::vector<std::pair<double, int>> cand;
    auto& tr = library_.tracks();
    for (int i = 0; i < (int)tr.size(); ++i) {
        const LibTrack& t = tr[i];
        if (t.missing || t.path == deck_[0].path || t.path == deck_[1].path) continue;
        double score = 0;
        if (refBpm > 0) {
            if (t.meta.bpm <= 0) continue;
            double r = t.meta.bpm / refBpm;
            if (r > 1.5) r /= 2; else if (r < 0.75) r *= 2;
            const double diff = std::fabs(r - 1.0) * 100.0;
            if (diff > settings_.suggestBpmPercent) continue;
            score += diff;
        }
        if (settings_.suggestKey && (refKey < 0 || t.meta.key < 0 || !keysCompatible(refKey,t.meta.key))) continue;
        if (refKey >= 0 && t.meta.key >= 0) {
            if (t.meta.key != refKey) score += 1.0;
        } else score += 2.5;
        if (today.count(t.path)) score += 4.0;
        cand.push_back({score, i});
    }
    std::sort(cand.begin(), cand.end());
    for (size_t k = 0; k < cand.size() && k < 200; ++k) out.push_back(ViewRow{cand[k].second, -1, tr[cand[k].second].path, {}});
}

void App::refreshView() {
    const std::string q = search_;
    uint64_t sugKey = 0;
    if (src_.kind == Src::Sugerencias) {
        for (int d = 0; d < kNumDecks; ++d) {
            sugKey = sugKey * 1315423911u + std::hash<std::wstring>()(deck_[d].path);
            sugKey = sugKey * 31 + (engine_.deckStatus[d].playing.load() ? 7 : 3) + (uint64_t)(deck_[d].meta.key + 2) * 101 + (uint64_t)std::lround(engine_.effectiveBpm(d) * 2);
        }
        sugKey = sugKey * 31 + (engine_.mixer.crossfader.load() <= 0.5f ? 1 : 2);
    }
    if (src_.kind == Src::Stems) sugKey = stemSongsVer_;
    if (library_.version() == viewVersion_ && q == viewQuery_ && srcGen_ == viewSrcGen_ && searchAll_ == viewSearchAll_ &&
        settings_.browseRecursive == viewRecursive_ && sugKey == viewSuggestKey_ && keyFilter_ == viewKeyFilter_ && !sortDirty_)
        return;
    const bool srcChanged = srcGen_ != viewSrcGen_;
    viewVersion_ = library_.version();
    viewQuery_ = q;
    viewSrcGen_ = srcGen_;
    viewSearchAll_ = searchAll_;
    viewRecursive_ = settings_.browseRecursive;
    viewSuggestKey_ = sugKey;
    viewKeyFilter_ = keyFilter_;
    sortDirty_ = false;

    std::lock_guard<std::recursive_mutex> lk(library_.mutex());
    auto& tr = library_.tracks();
    auto& pls = library_.playlists();
    const auto terms = searchTerms(q);
    auto match = [&](const LibTrack& t) {
        const auto it = settings_.trackTags.find(toUtf8(t.path));
        const std::string text = t.searchKey + " " + (it == settings_.trackTags.end() ? "" : foldForSearch(it->second));
        for (auto& w : terms) if (text.find(w) == std::string::npos) return false;
        return true;
    };
    auto matchText = [&](const std::string& s) {
        const std::string f = foldForSearch(s);
        for (auto& w : terms) if (f.find(w) == std::string::npos) return false;
        return true;
    };
    view_.clear();
    const bool global = searchAll_ && !terms.empty();
    listView_ = !global && src_.kind == Src::Lista && src_.list >= 0 && src_.list < (int)pls.size();
    if (global || src_.kind == Src::Coleccion) {
        for (int i = 0; i < (int)tr.size(); ++i) if (match(tr[i])) view_.push_back(ViewRow{i, -1, tr[i].path, {}});
    } else if (src_.kind == Src::Carpeta) {
        for (size_t i : library_.inFolder(src_.folder, settings_.browseRecursive))
            if (match(tr[i])) view_.push_back(ViewRow{(int)i, -1, tr[i].path, {}});
        if (!settings_.browseRecursive) {
            // Orden natural de una carpeta: por nombre de archivo.
            std::sort(view_.begin(), view_.end(), [&](const ViewRow& x, const ViewRow& y) { return _stricmp(tr[x.track].fileName.c_str(), tr[y.track].fileName.c_str()) < 0; });
        }
    } else if (listView_) {
        const auto& paths = pls[src_.list].paths;
        for (int k = 0; k < (int)paths.size(); ++k) {
            const LibTrack* t = library_.find(paths[k]);
            if (t) { if (match(*t)) view_.push_back(ViewRow{(int)(t - tr.data()), k, paths[k], {}}); }
            else {
                const std::string nm = toUtf8(fileNameOf(paths[k]));
                if (matchText(nm)) view_.push_back(ViewRow{-1, k, paths[k], nm});
            }
        }
    } else if (src_.kind == Src::Smart && src_.list >= 0 && src_.list < (int)settings_.smartLists.size()) {
        const auto& rule = settings_.smartLists[src_.list];
        const auto words = searchTerms(rule.tag);
        for (int i=0;i<(int)tr.size();++i) {
            const auto& t=tr[i];
            if (t.missing || !match(t) || t.meta.bpm < rule.minBpm || t.meta.bpm > rule.maxBpm || (rule.unplayed && t.playCount>0) || t.rating < rule.minRating) continue;
            const auto it=settings_.trackTags.find(toUtf8(t.path));
            const std::string text=t.searchKey+" "+(it==settings_.trackTags.end()?"":foldForSearch(it->second));
            bool ok=true; for (const auto& word:words) if (text.find(word)==std::string::npos) { ok=false; break; }
            if (ok) view_.push_back(ViewRow{i,-1,t.path,{}});
        }
    } else if (src_.kind == Src::Sugerencias) {
        std::vector<ViewRow> all;
        suggestions(all);
        for (auto& r : all) if (match(tr[r.track])) view_.push_back(r);
    } else if (src_.kind == Src::Stems) {
        for (auto& song : stemSongs_) {
            if (!samePath(song.folder, src_.folder)) continue;
            for (auto& id : song.stems) {
                const std::wstring p = song.folder + L"\\" + fromUtf8(id) + L".flac";
                const LibTrack* t = library_.find(p);
                const std::string title = song.title + " (" + stemLabel(id) + ")";
                if (!matchText(title)) continue;
                view_.push_back(ViewRow{t ? (int)(t - tr.data()) : -1, -1, p, title});
                stemTitles_[p] = title;
            }
        }
    }
    if (keyFilter_ >= 0) {
        // Filtro armónico: solo las pistas que encajan con la tonalidad elegida (rueda Camelot).
        view_.erase(std::remove_if(view_.begin(), view_.end(), [&](const ViewRow& r) {
            return r.track < 0 || !keysCompatible(keyFilter_, tr[r.track].meta.key);
        }), view_.end());
    }
    if (viewSortCol_ > 0) {
        const int c = viewSortCol_;
        const bool asc = viewSortAsc_;
        auto title = [&](const ViewRow& r) -> std::string { return r.track >= 0 ? tr[r.track].title : r.title; };
        std::stable_sort(view_.begin(), view_.end(), [&](const ViewRow& x, const ViewRow& y) {
            const LibTrack* A = x.track >= 0 ? &tr[x.track] : nullptr;
            const LibTrack* B = y.track >= 0 ? &tr[y.track] : nullptr;
            int r = 0;
            auto num = [&](double a, double b) { return a < b ? -1 : a > b ? 1 : 0; };
            switch (c) {
            case 1: r = foldForSearch(title(x)).compare(foldForSearch(title(y))); break;
            case 2: r = foldForSearch(A ? A->artist : "").compare(foldForSearch(B ? B->artist : "")); break;
            case 3: r = foldForSearch(remixOf(title(x))).compare(foldForSearch(remixOf(title(y)))); break;
            case 4: r = num(A ? A->meta.bpm : 0, B ? B->meta.bpm : 0); break;
            case 5: r = num(A && A->meta.key >= 0 ? camelotIndex(A->meta.key) : 99, B && B->meta.key >= 0 ? camelotIndex(B->meta.key) : 99); break;
            case 6: r = num(A ? A->meta.durationSec : 0, B ? B->meta.durationSec : 0); break;
            case 7: r = num(A ? A->playCount : 0, B ? B->playCount : 0); break;
            case 8: r = num(A ? (double)A->firstSeen : 0, B ? (double)B->firstSeen : 0); break;
            case 9: r = _wcsicmp(fileNameOf(x.path).c_str(), fileNameOf(y.path).c_str()); break;
            case 11: r = foldForSearch(A ? A->genre : "").compare(foldForSearch(B ? B->genre : "")); break;
            case 12: r = num(A ? A->rating : 0, B ? B->rating : 0); break;
            default: break;
            }
            return asc ? r < 0 : r > 0;
        });
    }
    if (srcChanged) sel_.Clear();
    cursorRow_ = -1;
    if (!libSelectedPath_.empty())
        for (int i = 0; i < (int)view_.size(); ++i) if (view_[i].path == libSelectedPath_) { cursorRow_ = i; break; }
}

// =================================================================== navegador

void App::drawBrowser(ImVec2 a, ImVec2 b) {
    const float s = S();
    browserRect_[0] = a; browserRect_[1] = b;
    if (b.y - a.y < 60 * s) return;
    const float gap = 8 * s;
    const float W = b.x - a.x;
    // Anchos (se pueden cambiar arrastrando los separadores)
    float treeW = settings_.treeW > 0 ? settings_.treeW * s : std::clamp(W * 0.16f, 200 * s, 260 * s);
    float sideW = settings_.sideW > 0 ? settings_.sideW * s : std::clamp(W * 0.22f, 280 * s, 360 * s);
    float infoW = settings_.infoW > 0 ? settings_.infoW * s : std::clamp(W * 0.17f, 240 * s, 290 * s);
    bool showTree = settings_.showTree, showSide = settings_.showSide, showInfo = settings_.showInfo;
    const float minList = 420 * s;
    auto listW = [&] { return W - (showTree ? treeW + gap : 0) - (showSide ? sideW + gap : 0) - (showInfo ? infoW + gap : 0); };
    if (listW() < minList && showInfo) showInfo = false;     // sin sitio: se ocultan primero los detalles
    if (listW() < minList && showSide) showSide = false;
    if (listW() < minList && showTree) treeW = std::max(160 * s, treeW - (minList - listW()));

    float x = a.x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto splitter = [&](const char* id, float sx, float* width, float* setting, float dir, float mn, float mx) {
        ImGui::SetCursorScreenPos(ImVec2(sx - gap * 0.5f - 3 * s, a.y));
        ImGui::InvisibleButton(id, ImVec2(6 * s, b.y - a.y));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            dl->AddLine(ImVec2(sx - gap * 0.5f, a.y + 10 * s), ImVec2(sx - gap * 0.5f, b.y - 10 * s), col::withAlpha(col::DeckA, 160), 2 * s);
        }
        if (ImGui::IsItemActive() && ImGui::GetIO().MouseDelta.x != 0) {
            *width = std::clamp(*width + dir * ImGui::GetIO().MouseDelta.x, mn, mx);
            *setting = *width / s;
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) *setting = 0;
    };
    if (showTree) {
        drawTree(ImVec2(x, a.y), ImVec2(x + treeW, b.y));
        x += treeW + gap;
        splitter("##splittree", x, &treeW, &settings_.treeW, 1.f, 170 * s, 420 * s);
    }
    float rx = b.x;
    if (showInfo) {
        drawInfoView(ImVec2(rx - infoW, a.y), ImVec2(rx, b.y));
        splitter("##splitinfo", rx - infoW, &infoW, &settings_.infoW, -1.f, 220 * s, 420 * s);
        rx -= infoW + gap;
    } else if (!settings_.showInfo) {
        // Detalles plegados: franja estrecha con el botón para desplegarlos.
        const float railW = 30 * s;
        Panel(dl, ImVec2(rx - railW, a.y), ImVec2(rx, b.y), 8 * s, col::Panel, col::Line);
        ImGui::SetCursorScreenPos(ImVec2(rx - railW + 3 * s, a.y + 6 * s));
        if (IconButton("##abririnfo", Ic::ChevLeft, ImVec2(railW - 6 * s, railW - 6 * s), col::DeckA, false, true, 0.5f)) settings_.showInfo = true;
        ImGui::SetItemTooltip("Mostrar los detalles del tema");
        DrawIcon(dl, Ic::Info, ImVec2(rx - railW * 0.5f, a.y + railW + 14 * s), 15 * s, col::TextDim);
        // Carátula pequeña de lo seleccionado, como recordatorio.
        if (!libSelectedPath_.empty()) DrawCover(dl, libSelectedPath_, ImVec2(rx - railW + 4 * s, a.y + railW + 30 * s), ImVec2(rx - 4 * s, a.y + railW + 30 * s + railW - 8 * s), 3 * s, "");
        rx -= railW + gap;
    }
    if (showSide) {
        drawSideView(ImVec2(rx - sideW, a.y), ImVec2(rx, b.y));
        splitter("##splitside", rx - sideW, &sideW, &settings_.sideW, -1.f, 240 * s, 520 * s);
        rx -= sideW + gap;
    }
    drawTrackList(ImVec2(x, a.y), ImVec2(rx, b.y));
    drawNameDialog();
}

// =================================================================== árbol

int App::treeRow(const char* id, int depth, int icon, ImU32 iconCol, const std::string& label, const char* count, bool selected, bool expandable, bool* open) {
    const float s = S();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = treeRowH_;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float x0 = treeX0_, x1 = treeX1_;
    const float ind = x0 + 4 * s + depth * 14 * s;
    ImGui::PushID(id);
    int res = 0;
    // Flecha para desplegar
    if (expandable) {
        ImGui::SetCursorScreenPos(ImVec2(ind, p.y));
        if (ImGui::InvisibleButton("##abrir", ImVec2(16 * s, h)) && open) *open = !*open;
        ImGui::SameLine(0, 0);
    } else ImGui::SetCursorScreenPos(ImVec2(ind + 16 * s, p.y));
    const ImVec2 lp = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##fila", ImVec2(std::max(10.f, x1 - lp.x), h))) res |= 1;
    const bool hov = ImGui::IsItemHovered();
    if (hov && ImGui::IsMouseDoubleClicked(0)) { res |= 2; if (expandable && open) *open = !*open; }
    ImGui::PopID();
    // Dibujo
    const ImVec2 ra(x0, p.y), rb(x1, p.y + h);
    const ImU32 tc = selected ? col::onColor(col::DeckA) : col::Text;
    if (selected) dl->AddRectFilled(ra, rb, col::DeckA, 6 * s);
    else if (hov) dl->AddRectFilled(ra, rb, col::Panel2, 6 * s);
    const float cy = p.y + h * 0.5f;
    if (expandable) DrawIcon(dl, open && *open ? Ic::ChevDown : Ic::ChevRight, ImVec2(ind + 8 * s, cy), 11 * s, selected ? tc : col::TextDim);
    const float ix = ind + 16 * s;
    DrawIcon(dl, (Ic)icon, ImVec2(ix + 9 * s, cy), 16 * s, selected ? tc : iconCol);
    float cw = 0;
    if (count && *count) {
        cw = ImGui::CalcTextSize(count).x;
        dl->AddText(ImVec2(x1 - cw - 8 * s, cy - ImGui::GetFontSize() * 0.5f), selected ? col::withAlpha(tc, 170) : col::TextDim, count);
    }
    const float lx = ix + 24 * s;
    dl->AddText(ImVec2(lx, cy - ImGui::GetFontSize() * 0.5f), tc, ellipsize(label, x1 - lx - cw - 14 * s, ImGui::GetFont(), ImGui::GetFontSize()).c_str());
    ImGui::SetCursorScreenPos(ImVec2(x0, p.y + h));
    return res;
}

void App::drawFsNode(FsNode& n, int depth, const char* prefix) {
    const std::string id = std::string(prefix) + toUtf8(n.path);
    bool& open = treeOpen_[id];
    const bool sel = src_.kind == Src::Carpeta && samePath(src_.folder, n.path);
    const int r = treeRow(id.c_str(), depth, n.icon, depth == 0 && std::string(prefix) == "fv:" ? col::Amber : col::TextDim, n.name, nullptr, sel,
                          !n.listed || !n.kids.empty(), &open);
    if (r & 1) { BrowseSrc s; s.kind = Src::Carpeta; s.folder = n.path; selectSource(s); }
    ImGui::SetItemTooltip("%s", toUtf8(n.path).c_str());
    ImGui::PushID(id.c_str());
    if (ImGui::BeginPopupContextItem("ctxfs")) {
        bool isFav = false;
        {
            std::lock_guard<std::recursive_mutex> lk(library_.mutex());
            for (auto& f : library_.folders()) if (samePath(f, n.path)) isFav = true;
        }
        if (!isFav && ImGui::MenuItem("Añadir a carpetas favoritas")) {
            library_.addFolder(n.path);
            notify("Carpeta favorita: " + n.name + " (se escanea en segundo plano)");
        }
        if (isFav && ImGui::MenuItem("Volver a escanear")) library_.rescan();
        if (ImGui::MenuItem("Leer también las subcarpetas", nullptr, settings_.browseRecursive)) {
            settings_.browseRecursive = !settings_.browseRecursive;
            if (src_.kind == Src::Carpeta) library_.browseFolder(src_.folder, settings_.browseRecursive);
        }
        if (ImGui::MenuItem("Actualizar subcarpetas")) { n.listed = false; n.kids.clear(); }
        if (ImGui::MenuItem("Abrir en el Explorador")) win::openFolder(n.path);
        if (isFav) {
            ImGui::Separator();
            if (ImGui::MenuItem("Quitar de carpetas favoritas")) {
                std::lock_guard<std::recursive_mutex> lk(library_.mutex());
                auto& fo = library_.folders();
                for (size_t i = 0; i < fo.size(); ++i) if (samePath(fo[i], n.path)) { library_.removeFolder(i); break; }
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    if (!open) return;
    if (!n.listed) {
        for (auto& p : win::subfolders(n.path)) n.kids.push_back(FsNode{p, toUtf8(fileNameOf(p)), (int)Ic::Folder});
        n.listed = true;
    }
    for (auto& k : n.kids) drawFsNode(k, depth + 1, "fs:");
}

void App::drawTree(ImVec2 a, ImVec2 b) {
    const float s = S();
    ImDrawList* dl0 = ImGui::GetWindowDrawList();
    Panel(dl0, a, b, 10 * s, col::Panel, col::Line);
    BeginPanel("arbol", a, b, 8 * s);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x;
    // Buscar carpetas y listas
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::SetNextItemWidth(w);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(28 * s, 6 * s));
        ImGui::InputTextWithHint("##filtroarbol", "Buscar carpetas y listas", treeFilter_, sizeof treeFilter_);
        ImGui::PopStyleVar();
        DrawIcon(dl, Ic::Search, ImVec2(p.x + 14 * s, p.y + ImGui::GetItemRectSize().y * 0.5f), 14 * s, col::TextDim);
    }
    ImGui::Dummy(ImVec2(0, 2 * s));
    // Lista con desplazamiento + pie de estado
    const float footH = (library_.scanning() || library_.analysisPending() > 0 || library_.browsing()) ? 46 * s : 34 * s;
    ImGui::BeginChild("arbolscroll", ImVec2(w, std::max(40 * s, ImGui::GetContentRegionAvail().y - footH)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    treeX0_ = ImGui::GetCursorScreenPos().x;
    treeX1_ = treeX0_ + ImGui::GetContentRegionAvail().x;
    treeRowH_ = 26 * s;
    dl = ImGui::GetWindowDrawList();
    std::lock_guard<std::recursive_mutex> lk(library_.mutex());
    auto& pls = library_.playlists();
    char cnt[32];
    auto acceptTracks = [&](int list) {
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("RDJ_PISTA")) {
                auto paths = uic::PayloadPaths(pl);
                library_.addToPlaylist(list, paths);
                notify(std::to_string(paths.size()) + " pista(s) añadidas a «" + pls[list].name + "».");
            }
            ImGui::EndDragDropTarget();
        }
    };
    auto caption = [&](const char* txt, const char* addId, const char* tip) -> bool {
        ImGui::Dummy(ImVec2(0, 6 * s));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddText(fontHead, 13 * s, ImVec2(p.x + 6 * s, p.y + 2 * s), col::TextDim, txt);
        bool clicked = false;
        if (addId) {
            ImGui::SetCursorScreenPos(ImVec2(treeX1_ - 22 * s, p.y - 2 * s));
            clicked = IconButton(addId, Ic::Plus, ImVec2(22 * s, 20 * s), col::DeckA, false, true, 0.55f);
            ImGui::SetItemTooltip("%s", tip);
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 20 * s));
        return clicked;
    };
    const std::string filter = foldForSearch(treeFilter_);
    if (!filter.empty()) {
        // Resultados: listas y carpetas (ya desplegadas) cuyo nombre coincide.
        int shown = 0;
        for (size_t i = 0; i < pls.size(); ++i) {
            if (foldForSearch(pls[i].name).find(filter) == std::string::npos) continue;
            std::snprintf(cnt, sizeof cnt, "%zu", pls[i].paths.size());
            ImGui::PushID((int)i);
            if (treeRow("res", 0, (int)Ic::List, col::TextDim, pls[i].name, cnt, src_.kind == Src::Lista && src_.list == (int)i, false, nullptr) & 1)
                selectSource(BrowseSrc{Src::Lista, L"", (int)i});
            acceptTracks((int)i);
            ImGui::PopID();
            ++shown;
        }
        std::function<void(FsNode&)> walk = [&](FsNode& n) {
            if (foldForSearch(n.name).find(filter) != std::string::npos) {
                ImGui::PushID(toUtf8(n.path).c_str());
                if (treeRow("resfs", 0, n.icon, col::TextDim, n.name, nullptr, src_.kind == Src::Carpeta && samePath(src_.folder, n.path), false, nullptr) & 1) {
                    BrowseSrc bs; bs.kind = Src::Carpeta; bs.folder = n.path; selectSource(bs);
                }
                ImGui::SetItemTooltip("%s", toUtf8(n.path).c_str());
                ImGui::PopID();
                ++shown;
            }
            for (auto& k : n.kids) walk(k);
        };
        for (auto& n : favRoots_) walk(n);
        for (auto& n : fsRoots_) walk(n);
        if (!shown) ImGui::TextDisabled("  Sin resultados");
        ImGui::Dummy(ImVec2(0, 0));  // tras SetCursorScreenPos hay que enviar un elemento antes de cerrar
        ImGui::EndChild();
        ImGui::EndChild();
        return;
    }

    // ---- Playlists inteligentes: reglas dinámicas, no copias de pistas.
    for (int i=0;i<(int)settings_.smartLists.size();++i) {
        ImGui::PushID(50000+i);
        if (treeRow("smart",0,(int)Ic::Bulb,col::Violet,settings_.smartLists[i].name,nullptr,src_.kind==Src::Smart && src_.list==i,false,nullptr)&1)
            selectSource(BrowseSrc{Src::Smart,L"",i});
        ImGui::SetItemTooltip("Lista inteligente: se actualiza al analizar o etiquetar pistas.");
        ImGui::PopID();
    }
    // ---- Biblioteca
    std::snprintf(cnt, sizeof cnt, "%zu", library_.tracks().size());
    if (treeRow("coleccion", 0, (int)Ic::Note, col::TextDim, "Colección", cnt, src_.kind == Src::Coleccion, false, nullptr) & 1) selectSource(BrowseSrc{});
    ImGui::SetItemTooltip("Todas las pistas conocidas (carpetas favoritas y carpetas que has abierto)");
    if (ImGui::BeginPopupContextItem("ctxcol")) {
        if (ImGui::MenuItem("🔍 Buscar y relocalizar toda la música del PC")) {
            library_.scanPcMusic(true);
            notify("Buscando toda la música del PC, relocalizando pistas movidas y limpiando borradas…");
        }
        if (ImGui::MenuItem("🧹 Quitar temas no encontrados de la colección")) {
            const int n = library_.relocateAndPurgeMissing(true);
            library_.save();
            ++srcGen_;
            notify(n > 0 ? ("Relocalizadas / limpiadas " + std::to_string(n) + " pistas no encontradas.") : "La colección está 100% limpia (0 pistas no encontradas).");
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Añadir carpeta favorita…")) {
            const std::wstring f = win::pickFolder(hwnd_);
            if (!f.empty()) { library_.addFolder(f); notify("Escaneando " + toUtf8(f) + "…"); }
        }
        if (ImGui::MenuItem("Añadir archivos…")) { auto files = win::pickAudioFiles(hwnd_, true); if (!files.empty()) library_.addFiles(files); }
        if (ImGui::MenuItem("Volver a escanear las carpetas favoritas")) library_.rescan();
        if (ImGui::MenuItem("Analizar BPM y tono de lo pendiente")) library_.analyzeMissing(settings_.minBpm, settings_.maxBpm);
        if (ImGui::MenuItem("Importar de VirtualDJ (tonalidad, BPM, cues, listas)…")) openVdjImport();
        ImGui::EndPopup();
    }
    {
        const int fav = library_.findPlaylist("Favoritos");
        std::snprintf(cnt, sizeof cnt, "%zu", fav >= 0 ? pls[fav].paths.size() : (size_t)0);
        if (treeRow("favoritos", 0, (int)Ic::HeartFill, col::DeckB, "Favoritos", cnt, fav >= 0 && src_.kind == Src::Lista && src_.list == fav, false, nullptr) & 1)
            selectSource(BrowseSrc{Src::Lista, L"", ensurePlaylist("Favoritos")});
        ImGui::SetItemTooltip("Marca una pista con el corazón del plato o con el botón derecho");
        acceptTracks(fav >= 0 ? fav : ensurePlaylist("Favoritos"));
    }
    if (treeRow("ideas", 0, (int)Ic::Bulb, col::Amber, "Sugerencias", nullptr, src_.kind == Src::Sugerencias, false, nullptr) & 1)
        selectSource(BrowseSrc{Src::Sugerencias});
    ImGui::SetItemTooltip("Sugerencias: margen ±%.0f %%. Cambia referencia y tonalidad en Organizar biblioteca.",settings_.suggestBpmPercent);
    {
        std::vector<std::pair<int64_t, int>> hist;
        for (size_t i = 0; i < pls.size(); ++i) if (isHistoryName(pls[i].name)) hist.push_back({historyKey(pls[i].name), (int)i});
        std::sort(hist.rbegin(), hist.rend());
        bool& open = treeOpen_["historial"];
        const bool selHist = src_.kind == Src::Lista && src_.list >= 0 && src_.list < (int)pls.size() && isHistoryName(pls[src_.list].name);
        std::snprintf(cnt, sizeof cnt, "%zu", hist.size());
        const int r = treeRow("historial", 0, (int)Ic::Clock, col::TextDim, "Historial", hist.empty() ? nullptr : cnt, selHist && !open, !hist.empty(), &open);
        if ((r & 1) && !hist.empty()) { selectSource(BrowseSrc{Src::Lista, L"", hist[0].second}); }
        ImGui::SetItemTooltip("Lo que ha sonado más de 30 s, una lista por día");
        if (open)
            for (auto& [k, i] : hist) {
                std::snprintf(cnt, sizeof cnt, "%zu", pls[i].paths.size());
                ImGui::PushID(i);
                if (treeRow("dia", 1, (int)Ic::Calendar, col::TextDim, pls[i].name.substr(10), cnt, src_.kind == Src::Lista && src_.list == i, false, nullptr) & 1)
                    selectSource(BrowseSrc{Src::Lista, L"", i});
                if (ImGui::BeginPopupContextItem("ctxh")) { listMenu(i); ImGui::EndPopup(); }
                ImGui::PopID();
            }
    }
    {
        const std::wstring rd = recordingFolder();
        if (treeRow("grabaciones", 0, (int)Ic::Record, col::Red, "Grabaciones", nullptr, src_.kind == Src::Carpeta && samePath(src_.folder, rd), false, nullptr) & 1) {
            BrowseSrc bs; bs.kind = Src::Carpeta; bs.folder = rd; selectSource(bs);
        }
        ImGui::SetItemTooltip("Tus mezclas grabadas (%s)", toUtf8(rd).c_str());
    }

    // ---- Carpetas
    if (caption("CARPETAS", "##addfav", "Añadir una carpeta favorita (se escanea entera y entra en la colección)")) {
        const std::wstring f = win::pickFolder(hwnd_);
        if (!f.empty()) { library_.addFolder(f); notify("Escaneando " + toUtf8(f) + "…"); }
    }
    {
        // Carpetas favoritas (como en VirtualDJ, con estrella)
        auto& fo = library_.folders();
        bool same = fo.size() == favRoots_.size();
        for (size_t i = 0; same && i < fo.size(); ++i) same = fo[i] == favRoots_[i].path;
        if (!same) {
            favRoots_.clear();
            for (auto& f : fo) favRoots_.push_back(FsNode{f, toUtf8(fileNameOf(f)), (int)Ic::StarFill});
        }
        for (auto& n : favRoots_) drawFsNode(n, 0, "fv:");
    }
    {
        bool& open = treeOpen_["pc"];
        treeRow("pc", 0, (int)Ic::Monitor, col::TextDim, "Este equipo", nullptr, false, true, &open);
        ImGui::SetItemTooltip("Navega por cualquier carpeta o unidad: sus pistas aparecen en el centro");
        if (ImGui::IsItemClicked()) open = !open;
        if (ImGui::BeginPopupContextItem("ctxpc")) {
            if (ImGui::MenuItem("Actualizar unidades")) buildFsTree();
            ImGui::EndPopup();
        }
        if (open) for (auto& n : fsRoots_) drawFsNode(n, 1, "fs:");
    }

    // ---- Listas
    if (caption("LISTAS", "##addlist", "Nueva lista")) openNameDialog(1, "Nueva lista");
    {
        int shown = 0;
        for (size_t i = 0; i < pls.size(); ++i) {
            if (isHiddenList(pls[i].name)) continue;
            ++shown;
            std::snprintf(cnt, sizeof cnt, "%zu", pls[i].paths.size());
            ImGui::PushID(2000 + (int)i);
            if (treeRow("lista", 0, (int)Ic::List, col::TextDim, pls[i].name, cnt, src_.kind == Src::Lista && src_.list == (int)i, false, nullptr) & 1)
                selectSource(BrowseSrc{Src::Lista, L"", (int)i});
            acceptTracks((int)i);
            if (ImGui::BeginPopupContextItem("ctxl")) { listMenu((int)i); ImGui::EndPopup(); }
            ImGui::PopID();
        }
        if (!shown) ImGui::TextDisabled("   Aún no hay listas");
    }
    {
        bool& open = treeOpen_["stems"];
        std::snprintf(cnt, sizeof cnt, "%zu", stemSongs_.size());
        treeRow("stems", 0, (int)Ic::Layers, col::Violet, "Pistas separadas", stemSongs_.empty() ? nullptr : cnt, false, !stemSongs_.empty(), &open);
        ImGui::SetItemTooltip("Canciones separadas con el Separador (voz, batería, bajo…)");
        if (ImGui::IsItemClicked()) open = !open;
        if (open)
            for (size_t i = 0; i < stemSongs_.size(); ++i) {
                const StemSong& sg = stemSongs_[i];
                ImGui::PushID(3000 + (int)i);
                std::snprintf(cnt, sizeof cnt, "%zu", sg.stems.size());
                if (treeRow("song", 1, (int)Ic::Layers, col::TextDim, sg.title, cnt, src_.kind == Src::Stems && samePath(src_.folder, sg.folder), false, nullptr) & 1) {
                    BrowseSrc bs; bs.kind = Src::Stems; bs.folder = sg.folder; selectSource(bs);
                }
                if (ImGui::BeginPopupContextItem("ctxs")) {
                    if (ImGui::MenuItem("Abrir en el Separador")) { mode_ = 1; openStemSong(sg); }
                    if (ImGui::MenuItem("Abrir la carpeta")) win::openFolder(sg.folder);
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
    }
    ImGui::Dummy(ImVec2(0, 4 * s));
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::PushID("nuevalista");
        if (ImGui::InvisibleButton("##nl", ImVec2(treeX1_ - treeX0_, 24 * s))) openNameDialog(1, "Nueva lista");
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        DrawIcon(dl, Ic::Plus, ImVec2(p.x + 14 * s, p.y + 12 * s), 13 * s, hov ? col::DeckA : col::TextDim);
        dl->AddText(ImVec2(p.x + 30 * s, p.y + 12 * s - ImGui::GetFontSize() * 0.5f), hov ? col::DeckA : col::TextDim, "Nueva lista");
    }
    ImGui::EndChild();

    // ---- Pie: escaneo y análisis
    ImGui::Dummy(ImVec2(0, 2 * s));
    size_t pendingBpm = 0;
    for (auto& t : library_.tracks()) if ((!t.meta.analyzed || !t.meta.keyAnalyzed) && !t.missing) ++pendingBpm;
    const int pend = library_.analysisPending();
    if (library_.scanning()) ImGui::TextColored(toVec4(col::Amber), " Escaneando… %d", library_.scannedCount());
    else if (library_.browsing()) ImGui::TextColored(toVec4(col::Amber), " Leyendo carpeta…");
    if (pend > 0) {
        char l[64];
        std::snprintf(l, sizeof l, "Analizando: %d · detener##anstop", pend);
        if (NeonButton(l, col::Amber, false, ImVec2(w, 26 * s), true, fontHead, 13 * s)) library_.cancelAnalysis();
    } else if (pendingBpm > 0) {
        char l[64];
        std::snprintf(l, sizeof l, "Analizar BPM y tono (%zu)##abpm", pendingBpm);
        if (NeonButton(l, col::Violet, false, ImVec2(w, 26 * s), true, fontHead, 13 * s)) library_.analyzeMissing(settings_.minBpm, settings_.maxBpm);
        ImGui::SetItemTooltip("Calcula BPM, rejilla y tonalidad de las pistas sin analizar, en segundo plano");
    } else {
        if (NeonButton("+ Carpeta favorita##addf2", col::DeckA, false, ImVec2(w, 26 * s), true, fontHead, 13 * s)) {
            const std::wstring f = win::pickFolder(hwnd_);
            if (!f.empty()) { library_.addFolder(f); notify("Escaneando " + toUtf8(f) + "…"); }
        }
        ImGui::SetItemTooltip("La carpeta se escanea entera (con subcarpetas) y aparece con estrella en el árbol");
    }
    ImGui::EndChild();
}

// =================================================================== lista central

void App::drawTrackList(ImVec2 a, ImVec2 b) {
    const float s = S();
    listRect_[0] = a; listRect_[1] = b;
    Panel(ImGui::GetWindowDrawList(), a, b, 10 * s, col::Panel, col::Line);
    BeginPanel("lista", a, b, 10 * s);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    refreshView();
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 hp = ImGui::GetCursorScreenPos();
    // Cabecera en una fila (ancho) o en dos (estrecho: título arriba, herramientas abajo).
    const bool narrow = w < 860 * s;
    const float headH = narrow ? 80 * s : 42 * s;
    const float bh = 30 * s;
    const float by1 = hp.y + 3 * s;
    const float by = narrow ? hp.y + 44 * s : by1;

    // ---- Título del origen
    std::string title, sub;
    int listIdx = -1;
    {
        std::lock_guard<std::recursive_mutex> lk(library_.mutex());
        auto& pls = library_.playlists();
        const bool global = searchAll_ && search_[0];
        if (global) title = "Resultados en la colección";
        else switch (src_.kind) {
        case Src::Coleccion: title = "Colección"; break;
        case Src::Carpeta: title = samePath(src_.folder, recordingFolder()) ? "Grabaciones" : toUtf8(fileNameOf(src_.folder)); if (title.empty()) title = toUtf8(src_.folder); break;
        case Src::Lista:
            if (src_.list >= 0 && src_.list < (int)pls.size()) { title = pls[src_.list].name; listIdx = src_.list; }
            break;
        case Src::Sugerencias: title = "Sugerencias"; break;
        case Src::Smart: title = src_.list>=0 && src_.list<(int)settings_.smartLists.size() ? settings_.smartLists[src_.list].name : "Lista inteligente"; break;
        case Src::Stems:
            for (auto& sg : stemSongs_) if (samePath(sg.folder, src_.folder)) title = sg.title;
            break;
        }
        double tot = 0;
        for (auto& r : view_) if (r.track >= 0) tot += library_.tracks()[r.track].meta.durationSec;
        char b2[96];
        std::snprintf(b2, sizeof b2, "%zu %s · %s", view_.size(), view_.size() == 1 ? "tema" : "temas", durText(tot).c_str());
        sub = b2;
        if (src_.kind == Src::Carpeta && !global && library_.browsing()) sub += " · leyendo…";
        if (src_.kind == Src::Sugerencias && !global) sub += " · según lo que suena";
    }
    float x = hp.x;
    ImGui::SetCursorScreenPos(ImVec2(x, by1));
    if (IconButton("##vertree", Ic::PanelLeft, ImVec2(bh, bh), col::DeckA, false, true, 0.55f)) settings_.showTree = !settings_.showTree;
    ImGui::SetItemTooltip(settings_.showTree ? "Ocultar carpetas" : "Mostrar carpetas");
    x += bh + 8 * s;

    // Botones de la derecha (de derecha a izquierda)
    float rx = hp.x + w;
    rx -= bh;
    ImGui::SetCursorScreenPos(ImVec2(rx, by1));
    if (IconButton("##verinfo", Ic::Info, ImVec2(bh, bh), col::DeckA, settings_.showInfo, true, 0.55f)) settings_.showInfo = !settings_.showInfo;
    ImGui::SetItemTooltip("Detalles de la pista");
    rx -= bh + 4 * s;
    ImGui::SetCursorScreenPos(ImVec2(rx, by1));
    if (IconButton("##verside", Ic::PanelRight, ImVec2(bh, bh), col::DeckA, settings_.showSide, true, 0.55f)) settings_.showSide = !settings_.showSide;
    ImGui::SetItemTooltip("Sidelist, Automix y estadísticas");
    rx -= 10 * s;
    const float titleLimit = rx;
    const float titleReserve = std::min(fontHead->CalcTextSizeA(22 * s, FLT_MAX, 0, title.c_str()).x + 34 * s, w * 0.34f);
    if (narrow) rx = hp.x + w;
    rx -= bh;
    ImGui::SetCursorScreenPos(ImVec2(rx, by));
    {
        const ImVec2 pp = ImGui::GetCursorScreenPos();
        if (NeonButton("##menulista", col::DeckA, false, ImVec2(bh, bh))) ImGui::OpenPopup("menulista");
        DrawIcon(dl, Ic::Dots, ImVec2(pp.x + bh * 0.5f, pp.y + bh * 0.5f), 16 * s, col::Text);
        ImGui::SetItemTooltip("Opciones de la lista");
    }
    if (ImGui::BeginPopup("menulista")) {
        if (ImGui::MenuItem("🔍 Buscar y relocalizar toda la música del PC")) {
            library_.scanPcMusic(true);
            notify("Buscando toda la música del PC, relocalizando movidos y limpiando borrados…");
        }
        if (ImGui::MenuItem("🧹 Quitar temas no encontrados de la colección")) {
            const int n = library_.relocateAndPurgeMissing(true);
            library_.save();
            ++srcGen_;
            notify(n > 0 ? ("Relocalizadas / limpiadas " + std::to_string(n) + " pistas no encontradas.") : "La colección está 100% limpia (0 pistas no encontradas).");
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Playlists inteligentes y sugerencias…")) showLibraryTools_=true;
        if (keyFilter_ >= 0 && ImGui::MenuItem("Quitar el filtro de tonalidad")) keyFilter_ = -1;
        if (ImGui::MenuItem("Importar de VirtualDJ (tonalidad, BPM, cues, listas)…")) openVdjImport();
        if (listIdx >= 0) { listMenu(listIdx); ImGui::Separator(); }
        if (src_.kind == Src::Carpeta) {
            if (ImGui::MenuItem("Leer también las subcarpetas", nullptr, settings_.browseRecursive)) {
                settings_.browseRecursive = !settings_.browseRecursive;
                library_.browseFolder(src_.folder, settings_.browseRecursive);
            }
            if (ImGui::MenuItem("Añadir a carpetas favoritas")) library_.addFolder(src_.folder);
            if (ImGui::MenuItem("Abrir en el Explorador")) win::openFolder(src_.folder);
            ImGui::Separator();
        }
        if (ImGui::MenuItem("Añadir todo a la Sidelist", nullptr, false, !view_.empty())) {
            std::vector<std::wstring> p2;
            for (auto& r : view_) p2.push_back(r.path);
            addToSidelist(p2);
        }
        if (ImGui::MenuItem("Lista nueva con todo lo que se ve…", nullptr, false, !view_.empty())) {
            std::vector<std::wstring> p2;
            for (auto& r : view_) p2.push_back(r.path);
            openNameDialog(3, title, p2);
        }
        if (ImGui::MenuItem("Importar lista M3U / M3U8…")) {
            const std::wstring f = win::pickPlaylistToOpen(hwnd_);
            if (!f.empty()) {
                const int i = library_.importM3u(f);
                if (i >= 0) { selectSource(BrowseSrc{Src::Lista, L"", i}); notify("Lista importada."); }
                else notify("No se pudo leer la lista.", true);
            }
        }
        if (ImGui::MenuItem("Buscar en Internet las carátulas que faltan", nullptr, false, !view_.empty())) {
            std::vector<std::wstring> p2;
            for (auto& r : view_) p2.push_back(r.path);
            fetchCovers(p2, true);
        }
        if (ImGui::MenuItem("Volver a leer las carátulas")) covers().clear();
        ImGui::Separator();
        {
            const int dens = settings_.rowDensity >= 0 ? settings_.rowDensity : (settings_.compactRows ? 1 : 0);
            ImGui::TextDisabled("Filas");
            if (ImGui::MenuItem("Cómodas, con carátula", nullptr, dens == 0)) settings_.rowDensity = 0;
            if (ImGui::MenuItem("Compactas", nullptr, dens == 1)) settings_.rowDensity = 1;
            if (ImGui::MenuItem("Muy compactas (miles de pistas)", nullptr, dens == 2)) settings_.rowDensity = 2;
        }
        ImGui::TextDisabled("Columnas: clic derecho en la cabecera de la tabla");
        ImGui::EndPopup();
    }
    // Subir / bajar (listas en orden natural)
    const bool canMove = listView_ && viewSortCol_ <= 0 && !viewFiltered() && sel_.Size > 0;
    auto moveSel = [&](int dir) {
        std::lock_guard<std::recursive_mutex> lk(library_.mutex());
        if (src_.kind != Src::Lista || src_.list < 0 || src_.list >= (int)library_.playlists().size()) return;
        auto& paths = library_.playlists()[src_.list].paths;
        std::vector<int> rows;
        for (int i = 0; i < (int)view_.size(); ++i) if (sel_.Contains(rowStorageId(&sel_, i))) rows.push_back(i);
        if (rows.empty()) return;
        if (dir < 0 && rows.front() == 0) return;
        if (dir > 0 && rows.back() == (int)paths.size() - 1) return;
        if (dir > 0) std::reverse(rows.begin(), rows.end());
        for (int r : rows) std::swap(paths[r], paths[r + dir]);
        sel_.Clear();
        for (int r : rows) sel_.SetItemSelected(0x80000000u | (ImGuiID)(r + dir + 1), true);
        library_.markDirty();
    };
    rx -= bh + 4 * s;
    ImGui::SetCursorScreenPos(ImVec2(rx, by));
    if (IconButton("##bajar", Ic::ArrowDown, ImVec2(bh, bh), col::DeckA, false, canMove, 0.5f)) moveSel(1);
    ImGui::SetItemTooltip("Bajar las pistas seleccionadas");
    rx -= bh + 2 * s;
    ImGui::SetCursorScreenPos(ImVec2(rx, by));
    if (IconButton("##subir", Ic::ArrowUp, ImVec2(bh, bh), col::DeckA, false, canMove, 0.5f)) moveSel(-1);
    ImGui::SetItemTooltip("Subir las pistas seleccionadas");
    // Añadir y Relocalizar PC
    {
        const float aw = 118 * s;
        rx -= aw + 6 * s;
        ImGui::SetCursorScreenPos(ImVec2(rx, by));
        const ImVec2 pp = ImGui::GetCursorScreenPos();
        if (NeonButton("Añadir temas  ##anadir", col::DeckA, false, ImVec2(aw, bh), true, fontHead, 14 * s)) ImGui::OpenPopup("anadir");
        DrawIcon(dl, Ic::ChevDown, ImVec2(pp.x + aw - 14 * s, pp.y + bh * 0.5f), 11 * s, col::Text);
        if (ImGui::BeginPopup("anadir")) {
            if (ImGui::MenuItem("🔍 Buscar toda la música del PC y relocalizar movidos")) {
                library_.scanPcMusic(true);
                notify("Escaneando música del PC y relocalizando canciones movidas…");
            }
            if (ImGui::MenuItem("🧹 Limpiar temas no encontrados / borrados")) {
                const int n = library_.relocateAndPurgeMissing(true);
                library_.save();
                ++srcGen_;
                notify(n > 0 ? ("Relocalizadas / limpiadas " + std::to_string(n) + " pistas.") : "0 pistas no encontradas: colección limpia.");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Archivos…")) {
                auto files = win::pickAudioFiles(hwnd_, true);
                if (!files.empty()) {
                    auto ok = library_.addFiles(files);
                    if (listIdx >= 0) library_.addToPlaylist(listIdx, ok);
                    notify(std::to_string(ok.size()) + " archivo(s) añadidos.");
                }
            }
            if (ImGui::MenuItem("Carpeta favorita (con subcarpetas)…")) {
                const std::wstring f = win::pickFolder(hwnd_);
                if (!f.empty()) { library_.addFolder(f); notify("Escaneando " + toUtf8(f) + "…"); }
            }
            if (listIdx >= 0 && ImGui::MenuItem("Las pistas de la Sidelist")) {
                std::vector<std::wstring> p2;
                {
                    std::lock_guard<std::recursive_mutex> lk(library_.mutex());
                    const int sl = library_.findPlaylist("Sidelist");
                    if (sl >= 0) p2 = library_.playlists()[sl].paths;
                }
                library_.addToPlaylist(listIdx, p2);
            }
            ImGui::EndPopup();
        }
        ImGui::SetItemTooltip("Añadir archivos, carpetas o escanear y relocalizar toda la música del PC");

        // Botón directo para Relocalizar / Escanear PC
        const float rw = 128 * s;
        if (rx - rw - 8 * s > x + 220 * s) {
            rx -= rw + 6 * s;
            ImGui::SetCursorScreenPos(ImVec2(rx, by));
            if (NeonButton("🔄 Relocalizar PC##relocpc", col::Green, library_.scanning(), ImVec2(rw, bh), !library_.scanning(), fontHead, 12.5f * s)) {
                const int instant = library_.relocateAndPurgeMissing(true);
                library_.save();
                library_.scanPcMusic(true);
                ++srcGen_;
                notify("Buscando música en el PC y relocalizando canciones movidas (" + std::to_string(instant) + " ajustadas al instante)…");
            }
            ImGui::SetItemTooltip("Busca todos los MP3/audios del PC, actualiza automáticamente las canciones que hayas movido de carpeta\nconservando tus Cues, BPM, Tonalidad y Playlists, y quita las borradas.");
        }
    }
    // Buscar
    {
        const float gw = bh;
        const float sw = narrow ? std::max(120 * s, rx - gw - 10 * s - hp.x) : std::clamp(rx - gw - 10 * s - x - titleReserve, 150 * s, 320 * s);
        rx -= gw + 6 * s;
        ImGui::SetCursorScreenPos(ImVec2(rx, by));
        if (IconButton("##todo", Ic::Globe, ImVec2(gw, bh), col::DeckA, searchAll_, true, 0.55f)) searchAll_ = !searchAll_;
        ImGui::SetItemTooltip(searchAll_ ? "Buscando en toda la colección (clic: solo aquí)" : "Buscar solo aquí (clic: en toda la colección)");
        rx -= sw + 4 * s;
        ImGui::SetCursorScreenPos(ImVec2(rx, by));
        ImGui::SetNextItemWidth(sw);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(30 * s, (bh - ImGui::GetFontSize()) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, toVec4(col::Bg));
        ImGui::SetNextItemAllowOverlap();  // para que la × de borrar reciba el clic
        ImGui::InputTextWithHint("##buscar", searchAll_ ? "Buscar en la colección…" : "Buscar aquí…", search_, sizeof search_);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        DrawIcon(dl, Ic::Search, ImVec2(rx + 15 * s, by + bh * 0.5f), 14 * s, col::TextDim);
        if (search_[0]) {
            ImGui::SetCursorScreenPos(ImVec2(rx + sw - 24 * s, by + (bh - 20 * s) * 0.5f));
            if (IconButton("##limpiar", Ic::Close, ImVec2(20 * s, 20 * s), col::DeckA, false, true, 0.5f)) search_[0] = 0;
        }
    }
    // Título y subtítulo (en el espacio que queda)
    {
        const float maxW = (narrow ? titleLimit : rx) - x - 12 * s;
        const std::string t = ellipsize(title, maxW * 0.65f, fontHead, 22 * s);
        dl->AddText(fontHead, 22 * s, ImVec2(x, hp.y - 2 * s), col::Text, t.c_str());
        float tx = x + fontHead->CalcTextSizeA(22 * s, FLT_MAX, 0, t.c_str()).x + 8 * s;
        if (listIdx >= 0 && !isHistoryName(title) && title != "Favoritos" && title != "Sidelist") {
            ImGui::SetCursorScreenPos(ImVec2(tx, hp.y + 2 * s));
            if (IconButton("##renombrar", Ic::Pencil, ImVec2(20 * s, 20 * s), col::DeckA, false, true, 0.6f)) { renameList_ = listIdx; openNameDialog(0, title); }
            ImGui::SetItemTooltip("Renombrar la lista");
        }
        const std::string st = src_.kind == Src::Carpeta && !(searchAll_ && search_[0]) ? sub + "  ·  " + toUtf8(src_.folder) : sub;
        float sx = x;
        if (keyFilter_ >= 0) {
            // Filtro armónico activo: insignia con su color Camelot; clic = quitarlo.
            const std::string vis = "Encajan con " + keyCode(keyFilter_) + "   ×";
            const std::string lb = vis + "##quitarfiltro";
            const float bw = BadgeWidth(18 * s, vis.c_str(), fontHead, 12 * s) + 6 * s;
            ImGui::SetCursorScreenPos(ImVec2(sx, hp.y + 21 * s));
            if (NeonButton(lb.c_str(), col::camelot(camelotIndex(keyFilter_)), true, ImVec2(bw, 18 * s), true, fontHead, 12 * s)) keyFilter_ = -1;
            ImGui::SetItemTooltip("Solo se ven las pistas compatibles con %s (%s). Clic para quitar el filtro.", keyCode(keyFilter_ < 0 ? 0 : keyFilter_).c_str(),
                                  keyNameEs(keyFilter_ < 0 ? 0 : keyFilter_));
            sx += bw + 8 * s;
        }
        dl->AddText(ImGui::GetFont(), 13.5f * s, ImVec2(sx, hp.y + 22 * s), col::TextDim, ellipsize(st, maxW - (sx - x), ImGui::GetFont(), 13.5f * s).c_str());
    }
    ImGui::SetCursorScreenPos(ImVec2(hp.x, hp.y + headH + 4 * s));
    {
        size_t missingCount = 0;
        {
            std::lock_guard<std::recursive_mutex> lk(library_.mutex());
            for (const auto& t : library_.tracks()) if (t.missing) ++missingCount;
        }
        if (missingCount > 0) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, toVec4(col::withAlpha(col::Amber, 34)));
            ImGui::BeginChild("missing_bar", ImVec2(w, 28 * s), ImGuiChildFlags_None);
            ImGui::SetCursorPos(ImVec2(8 * s, 4 * s));
            ImGui::TextColored(toVec4(col::Amber), "⚠ Hay %zu tema(s) movidos o no encontrados en su ruta antigua:", missingCount);
            ImGui::SameLine(0, 10 * s);
            if (NeonButton("🔄 Relocalizar movidos y limpiar borrados ahora", col::Green, true, ImVec2(290 * s, 20 * s), true, fontHead, 11.5f * s)) {
                const int n = library_.relocateAndPurgeMissing(true);
                library_.save();
                library_.scanPcMusic(true);
                ++srcGen_;
                notify("Relocalizadas / limpiadas " + std::to_string(n) + " pistas y escaneando el PC…");
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
    }

    // ---- Tabla
    // Densidad: cómodas (con carátula), compactas o muy compactas (cientos o miles de pistas).
    const int density = settings_.rowDensity >= 0 ? settings_.rowDensity : (settings_.compactRows ? 1 : 0);
    const bool compact = density > 0;
    const float rowH = density == 0 ? 36 * s : density == 1 ? 26 * s : 21 * s;
    const ImGuiTableFlags tf = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable |
                               ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_NoBordersInBody;
    int insertAt = -1, moveTo = -1;
    std::vector<std::wstring> insertPaths;
    std::vector<int> movedRows;
    int loadCursor = -1;
    if (ImGui::BeginTable("pistas", 13, tf)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_PreferSortAscending, 38 * s, 0);
        ImGui::TableSetupColumn("TEMA", ImGuiTableColumnFlags_NoHide, 1.7f, 1);
        ImGui::TableSetupColumn("ARTISTA", ImGuiTableColumnFlags_None, 1.0f, 2);
        ImGui::TableSetupColumn("REMIX", ImGuiTableColumnFlags_DefaultHide, 0.7f, 3);
        ImGui::TableSetupColumn("BPM", ImGuiTableColumnFlags_WidthFixed, 60 * s, 4);
        ImGui::TableSetupColumn("TONALIDAD", ImGuiTableColumnFlags_WidthFixed, (settings_.keyNotation == 1 ? 78 : 106) * s, 5);
        ImGui::TableSetupColumn("DURACIÓN", ImGuiTableColumnFlags_WidthFixed, 72 * s, 6);
        ImGui::TableSetupColumn("ESTILO", ImGuiTableColumnFlags_NoHide, 0.75f, 11);
        ImGui::TableSetupColumn("VALORACIÓN", ImGuiTableColumnFlags_WidthFixed, 92 * s, 12);
        ImGui::TableSetupColumn("REPROD.", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultHide, 64 * s, 7);
        ImGui::TableSetupColumn("AÑADIDA", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultHide, 88 * s, 8);
        ImGui::TableSetupColumn("ARCHIVO", ImGuiTableColumnFlags_DefaultHide, 0.9f, 9);
        ImGui::TableSetupColumn("##menu", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_NoResize |
                                              ImGuiTableColumnFlags_NoReorder, 30 * s, 10);
        {
            // ESTILO siempre visible (aunque en imgui.ini anterior estuviera oculto).
            ImGui::TableSetColumnEnabled(7, true);
            const bool narrow = ImGui::GetContentRegionAvail().x < 720 * s;
            if (narrow) {
                const bool v = ImGui::TableGetColumnFlags(8) & ImGuiTableColumnFlags_IsEnabled;
                if (v) { autoHidCols_ = 2; ImGui::TableSetColumnEnabled(8, false); }
            } else if (autoHidCols_) {
                if (autoHidCols_ & 2) ImGui::TableSetColumnEnabled(8, true);
                autoHidCols_ = 0;
            }
        }
        ImGui::PushFont(fontHead, 13.f);
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(col::TextDim));
        ImGui::TableHeadersRow();
        ImGui::PopStyleColor();
        ImGui::PopFont();
        if (ImGuiTableSortSpecs* ss = ImGui::TableGetSortSpecs()) {
            if (ss->SpecsDirty) {
                if (ss->SpecsCount > 0) { viewSortCol_ = (int)ss->Specs[0].ColumnUserID; viewSortAsc_ = ss->Specs[0].SortDirection == ImGuiSortDirection_Ascending; }
                else viewSortCol_ = -1;
                if (viewSortCol_ == 0) viewSortCol_ = -1;  // "#" = orden natural
                sortDirty_ = true;
                ss->SpecsDirty = false;
                refreshView();
            }
        }
        // Selección múltiple (Ctrl/Mayús, arrastrar un recuadro)
        sel_.UserData = this;
        sel_.AdapterIndexToStorageId = &App::rowStorageId;
        ImGuiMultiSelectIO* ms = ImGui::BeginMultiSelect(ImGuiMultiSelectFlags_ClearOnEscape | ImGuiMultiSelectFlags_BoxSelect1d | ImGuiMultiSelectFlags_SelectOnClickRelease, sel_.Size, (int)view_.size());
        sel_.ApplyRequests(ms);

        std::lock_guard<std::recursive_mutex> lk(library_.mutex());
        auto& tr = library_.tracks();
        std::set<std::wstring> today;
        for (auto& pl : library_.playlists()) if (pl.name == todayHistoryName()) today.insert(pl.paths.begin(), pl.paths.end());
        // Referencia para las marcas ✓ (como VirtualDJ): lo que suena; si no suena nada, lo cargado.
        int playingKeys[2] = {-1, -1};
        double playingBpm[2] = {0, 0};
        bool anyPlaying = false;
        for (int d = 0; d < kNumDecks; ++d) if (deck_[d].track && engine_.deckStatus[d].playing.load()) anyPlaying = true;
        for (int d = 0; d < kNumDecks; ++d) {
            if (!deck_[d].track || (anyPlaying && !engine_.deckStatus[d].playing.load())) continue;
            playingKeys[d] = soundingKey(d);
            if (deck_[d].meta.bpm > 0) playingBpm[d] = engine_.effectiveBpm(d);
        }
        ImGuiListClipper clip;
        clip.Begin((int)view_.size());
        if (ms->RangeSrcItem != -1) clip.IncludeItemByIndex((int)ms->RangeSrcItem);
        if (scrollToCursor_ && cursorRow_ >= 0) clip.IncludeItemByIndex(cursorRow_);
        ImFont* body = ImGui::GetFont();
        const float fs = ImGui::GetFontSize();
        while (clip.Step()) {
            for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row) {
                if (row >= (int)view_.size()) break;
                const ViewRow& vr = view_[row];
                const LibTrack* t = vr.track >= 0 && vr.track < (int)tr.size() ? &tr[vr.track] : nullptr;
                ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
                ImGui::TableNextColumn();
                ImGui::PushID(row);
                const ImGuiID sid = rowStorageId(&sel_, row);
                const bool selected = sel_.Contains(sid);
                ImGui::SetNextItemSelectionUserData(row);
                const ImVec2 c0 = ImGui::GetCursorScreenPos();
                ImGui::Selectable("##fila", selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick,
                                  ImVec2(0, rowH));
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) loadCursor = row;
                {
                    // La fila con el foco (clic o teclado) es la que ven Info y el mando.
                    static ImGuiID lastFocus = 0;
                    const ImGuiID me = ImGui::GetItemID();
                    if ((ImGui::IsItemFocused() && lastFocus != me) || ImGui::IsItemClicked(ImGuiMouseButton_Right) || ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                        lastFocus = me;
                        cursorRow_ = row;
                        libSelectedPath_ = vr.path;
                    }
                }
                if (scrollToCursor_ && row == cursorRow_) { ImGui::SetScrollHereY(); scrollToCursor_ = false; }
                if (ImGui::BeginDragDropSource()) {
                    std::vector<std::wstring> paths;
                    dragRows_.clear();
                    if (selected) {
                        for (int i = 0; i < (int)view_.size(); ++i)
                            if (sel_.Contains(rowStorageId(&sel_, i))) { paths.push_back(view_[i].path); dragRows_.push_back(view_[i].listPos); }
                    } else { paths.push_back(vr.path); dragRows_.push_back(vr.listPos); }
                    dragList_ = listView_ && viewSortCol_ <= 0 && !viewFiltered() ? src_.list : -2;
                    uic::SetPathsPayload(paths);
                    if (paths.size() == 1) ImGui::Text("%s", t ? (t->artist.empty() ? t->title : t->artist + " - " + t->title).c_str() : vr.title.c_str());
                    else ImGui::Text("%zu pistas", paths.size());
                    ImGui::TextDisabled("%s", dragList_ >= 0 ? "Suelta en otra fila para reordenar, o en un plato o lista" : "Suelta en un plato, en la Sidelist o en una lista");
                    ImGui::EndDragDropSource();
                }
                if (listView_ && ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("RDJ_PISTA")) {
                        if (dragList_ == src_.list && viewSortCol_ <= 0 && !viewFiltered()) { moveTo = vr.listPos; movedRows = dragRows_; }
                        else { insertAt = vr.listPos; insertPaths = uic::PayloadPaths(pl); }
                    }
                    ImGui::EndDragDropTarget();
                }
                if (ImGui::BeginPopupContextItem("ctx")) {
                    if (!selected) { sel_.Clear(); sel_.SetItemSelected(sid, true); }
                    trackMenu(selectedPaths().empty() ? std::vector<std::wstring>{vr.path} : selectedPaths(), listView_);
                    ImGui::EndPopup();
                }
                ImDrawList* cdl = ImGui::GetWindowDrawList();
                const float ty = c0.y + (rowH - fs) * 0.5f;
                // Estado: plato cargado / número
                const bool onA = (!deck_[0].path.empty() && deck_[0].path == vr.path);
                const bool onB = (!deck_[1].path.empty() && deck_[1].path == vr.path);
                int onDeck = onA ? 0 : (onB ? 1 : -1);
                if (onDeck >= 0 && !selected) {
                    const bool plNow = engine_.deckStatus[onDeck].playing.load();
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, col::withAlpha(col::deck(onDeck), plNow ? 62 : 36));
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, col::withAlpha(col::deck(onDeck), plNow ? 62 : 36));
                }
                const bool missing = !t || t->missing;
                const bool played = today.count(vr.path) > 0;
                if (auto ci=settings_.trackColors.find(toUtf8(vr.path)); ci!=settings_.trackColors.end() && ci->second>0) {
                    const ImU32 colors[]={col::TextDim,col::DeckA,col::Amber,col::Violet,col::DeckB};
                    cdl->AddRectFilled(ImVec2(c0.x,c0.y+2*s),ImVec2(c0.x+3*s,c0.y+rowH-2*s),colors[std::clamp(ci->second,0,4)]);
                }
                if (onDeck >= 0) {
                    const ImU32 dc = col::deck(onDeck);
                    cdl->AddRectFilled(ImVec2(c0.x - 4 * s, c0.y + 3 * s), ImVec2(c0.x - 1 * s, c0.y + rowH - 3 * s), dc, 1.5f * s);
                    DrawIcon(cdl, engine_.deckStatus[onDeck].playing.load() ? Ic::Play : Ic::Pause, ImVec2(c0.x + 9 * s, c0.y + rowH * 0.5f), 12 * s, dc);
                    if (listView_) { char n[12]; std::snprintf(n, sizeof n, "%d", vr.listPos + 1); cdl->AddText(body, fs * 0.85f, ImVec2(c0.x + 18 * s, ty + 1 * s), dc, n); }
                } else if (listView_) {
                    char n[12];
                    std::snprintf(n, sizeof n, "%d", vr.listPos + 1);
                    cdl->AddText(body, fs * 0.85f, ImVec2(c0.x + 4 * s, ty + 1 * s), col::TextDim, n);
                } else if (played) DrawIcon(cdl, Ic::Check, ImVec2(c0.x + 10 * s, c0.y + rowH * 0.5f), 12 * s, col::TextDim);
                const ImU32 tcol = missing ? col::withAlpha(col::Red, 200) : played && onDeck < 0 ? col::mix(col::Text, col::TextDim, 0.6f) : col::Text;
                // Tema (carátula + título)
                ImGui::TableNextColumn();
                {
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    const float cw = ImGui::GetContentRegionAvail().x;
                    float tx = p.x;
                    if (!compact) {
                        const float art = rowH - 8 * s;
                        DrawCover(cdl, vr.path, ImVec2(p.x, c0.y + 4 * s), ImVec2(p.x + art, c0.y + 4 * s + art), 4 * s, t ? t->title.c_str() : vr.title.c_str());
                        tx += art + 10 * s;
                    }
                    std::string nm = t ? t->title : vr.title;
                    if (onA && onB) nm = "[DECK A+B] " + nm;
                    else if (onA) nm = std::string(engine_.deckStatus[0].playing.load() ? "[DECK A ▶] " : "[DECK A] ") + nm;
                    else if (onB) nm = std::string(engine_.deckStatus[1].playing.load() ? "[DECK B ▶] " : "[DECK B] ") + nm;
                    if (missing && t) nm += "  (no encontrado)";
                    const ImU32 titleColor = onDeck >= 0 ? col::deck(onDeck) : tcol;
                    cdl->AddText(fontHead, fs * 1.02f, ImVec2(tx, ty - 1 * s), titleColor, ellipsize(nm, p.x + cw - tx, fontHead, fs * 1.02f).c_str());
                }
                ImGui::TableNextColumn();
                if (t) {
                    const float cw = ImGui::GetContentRegionAvail().x;
                    cdl->AddText(body, fs, ImVec2(ImGui::GetCursorScreenPos().x, ty), played ? col::TextDim : col::mix(col::Text, col::TextDim, 0.25f),
                                 ellipsize(t->artist, cw, body, fs).c_str());
                }
                ImGui::TableNextColumn();
                if (t) {
                    const float cw = ImGui::GetContentRegionAvail().x;
                    cdl->AddText(body, fs, ImVec2(ImGui::GetCursorScreenPos().x, ty), col::TextDim, ellipsize(remixOf(t->title), cw, body, fs).c_str());
                }
                ImGui::TableNextColumn();
                if (t) {
                    char bb[16];
                    if (t->meta.bpm > 0) std::snprintf(bb, sizeof bb, "%.2f", t->meta.bpm);
                    else std::snprintf(bb, sizeof bb, "%s", t->meta.analyzed ? "?" : "…");
                    // Verde: entra con menos de un 2 % de pitch respecto a lo que suena (o al doble / la mitad).
                    bool bpmNear = false;
                    for (int d = 0; d < kNumDecks; ++d) {
                        if (playingBpm[d] <= 0 || t->meta.bpm <= 0 || deck_[d].path == t->path) continue;
                        double r = t->meta.bpm / playingBpm[d];
                        if (r > 1.5) r /= 2; else if (r < 0.75) r *= 2;
                        if (std::fabs(r - 1.0) <= 0.02) bpmNear = true;
                    }
                    // Cifras alineadas a la derecha; sin analizar: reloj (pendiente).
                    const float cx1 = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x - 4 * s;
                    if (!t->meta.analyzed && !t->missing) {
                        DrawIcon(cdl, library_.analysisPending() > 0 ? Ic::Spinner : Ic::Clock, ImVec2(cx1 - 7 * s, c0.y + rowH * 0.5f), 13 * s, col::TextDim);
                    } else {
                        const float bw2 = fontHead->CalcTextSizeA(fs, FLT_MAX, 0, bb).x;
                        cdl->AddText(fontHead, fs, ImVec2(cx1 - bw2, ty), t->meta.bpm > 0 ? (bpmNear ? col::Green : col::Text) : col::TextDim, bb);
                    }
                }
                ImGui::TableNextColumn();
                if (t) {
                    if (t->meta.key >= 0) {
                        // Semáforo armónico en vivo respecto al plato activo:
                        // 3 = Verde (armónica perfecta + BPM <=6%), 2 = Azul (Energy Boost +2/+7/Diagonal), 1 = Ámbar (tono compatible pero BPM lejano), 0 = neutro/choque
                        int semState = -1;
                        const char* semDesc = "";
                        for (int d = 0; d < kNumDecks; ++d) {
                            if (playingKeys[d] < 0 || deck_[d].path == t->path) continue;
                            const int rk = playingKeys[d], tk = t->meta.key;
                            const int na = keyCamelotNumber(rk), nb = keyCamelotNumber(tk);
                            const bool ma = keyIsMajor(rk), mb = keyIsMajor(tk);
                            int diff = (nb - na) % 12;
                            if (diff > 6) diff -= 12;
                            if (diff < -6) diff += 12;
                            bool exactOrNeighbor = (ma == mb && std::abs(diff) <= 1) || (ma != mb && diff == 0);
                            bool energyBoost = (ma == mb && (diff == 2 || diff == -5)) || ((!ma && diff == 1) || (ma && diff == -1));
                            bool bpmOk = true;
                            if (playingBpm[d] > 0 && t->meta.bpm > 0) {
                                double r = t->meta.bpm / playingBpm[d];
                                if (r > 1.5) r /= 2.0; else if (r < 0.75) r *= 2.0;
                                bpmOk = (std::fabs(r - 1.0) <= 0.06);
                            }
                            int candState = 0;
                            const char* candDesc = "Choque armónico con el plato activo";
                            if (exactOrNeighbor && bpmOk) { candState = 3; candDesc = "VERDE · Mezcla armónica perfecta (misma clave, ±1 o relativa + BPM compatible)"; }
                            else if (energyBoost && bpmOk) { candState = 2; candDesc = "AZUL · Subida de energía (+2, +7 semitono o cambio diagonal)"; }
                            else if (exactOrNeighbor || energyBoost || keysClose(rk, tk)) { candState = 1; candDesc = "ÁMBAR · Tono compatible o cercano (requiere ajustar BPM o puente)"; }
                            if (candState > semState) { semState = candState; semDesc = candDesc; }
                        }
                        const ImVec2 kp(ImGui::GetCursorScreenPos().x, c0.y + (rowH - 19 * s) * 0.5f);
                        const float kw = drawKeyBadges(cdl, kp, 19 * s, t->meta.key, semState == 3);
                        if (semState >= 0) {
                            const ImU32 semCol = semState == 3 ? col::Green : semState == 2 ? IM_COL32(56, 189, 248, 255) : semState == 1 ? col::Amber : col::withAlpha(col::Red, 95);
                            const ImVec2 dotC(kp.x + kw + 8 * s, kp.y + 9.5f * s);
                            cdl->AddCircleFilled(dotC, semState >= 2 ? 4.8f * s : 3.8f * s, semCol, 14);
                            if (semState >= 2) cdl->AddCircle(dotC, 6.2f * s, col::withAlpha(semCol, 90), 14, 1.2f * s);
                            if (ImGui::IsMouseHoveringRect(ImVec2(kp.x, c0.y), ImVec2(kp.x + kw + 18 * s, c0.y + rowH))) {
                                ImGui::SetTooltip("%s", semDesc);
                            }
                        }
                    } else cdl->AddText(body, fs, ImVec2(ImGui::GetCursorScreenPos().x, ty), col::TextDim, t->meta.keyAnalyzed ? "?" : "…");
                }
                ImGui::TableNextColumn();
                if (t && t->meta.durationSec > 0) {
                    const std::string du = formatTime(t->meta.durationSec, false);
                    const float cx1 = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x - 8 * s;
                    cdl->AddText(fontHead, fs, ImVec2(cx1 - fontHead->CalcTextSizeA(fs, FLT_MAX, 0, du.c_str()).x, ty), col::Text, du.c_str());
                }
                ImGui::TableNextColumn();  // Estilo
                if (t && !t->genre.empty()) {
                    const float cw = ImGui::GetContentRegionAvail().x;
                    const ImVec2 gp = ImGui::GetCursorScreenPos();
                    const ImU32 gc = col::mix(col::DeckA, col::Text, 0.62f);
                    cdl->AddText(body, fs, ImVec2(gp.x, ty), gc, ellipsize(t->genre, cw, body, fs).c_str());
                    if (ImGui::IsMouseHoveringRect(ImVec2(gp.x, c0.y), ImVec2(gp.x + cw, c0.y + rowH))) {
                        ImGui::SetTooltip("Estilo: %s\nClic con botón central para filtrar por este estilo", t->genre.c_str());
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
                            std::snprintf(search_, sizeof search_, "%s", t->genre.c_str());
                        }
                    }
                } else {
                    cdl->AddText(body, fs, ImVec2(ImGui::GetCursorScreenPos().x, ty), col::withAlpha(col::TextDim, 110), "—");
                }
                ImGui::TableNextColumn();  // Valoración (clic en las estrellas)
                if (t) {
                    int rating = t->rating;
                    const float st = std::min(14 * s, rowH - 8 * s);
                    const bool showAll = rating > 0 || ImGui::IsMouseHoveringRect(ImVec2(ImGui::GetCursorScreenPos().x, c0.y), ImVec2(ImGui::GetCursorScreenPos().x + 90 * s, c0.y + rowH));
                    if (showAll && RatingStars("##val", &rating, ImVec2(ImGui::GetCursorScreenPos().x, c0.y + (rowH - st) * 0.5f), st, col::Amber, col::withAlpha(col::TextDim, 120)))
                        library_.setRating(t->path, rating);
                }
                ImGui::TableNextColumn();
                if (t && t->playCount > 0) { char n[16]; std::snprintf(n, sizeof n, "%d", t->playCount); cdl->AddText(body, fs, ImVec2(ImGui::GetCursorScreenPos().x, ty), col::TextDim, n); }
                ImGui::TableNextColumn();
                if (t && t->firstSeen > 0) cdl->AddText(body, fs, ImVec2(ImGui::GetCursorScreenPos().x, ty), col::TextDim, dateText(t->firstSeen, false).c_str());
                ImGui::TableNextColumn();
                {
                    const float cw = ImGui::GetContentRegionAvail().x;
                    cdl->AddText(body, fs, ImVec2(ImGui::GetCursorScreenPos().x, ty), col::TextDim, ellipsize(toUtf8(fileNameOf(vr.path)), cw, body, fs).c_str());
                }
                ImGui::TableNextColumn();
                {
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    ImGui::SetCursorScreenPos(ImVec2(p.x, c0.y + (rowH - 24 * s) * 0.5f));
                    if (IconButton("##mas", Ic::Dots, ImVec2(24 * s, 24 * s), col::DeckA, false, true, 0.6f)) {
                        if (!selected) { sel_.Clear(); sel_.SetItemSelected(sid, true); }
                        ImGui::OpenPopup("ctx");
                    }
                }
                ImGui::PopID();
            }
        }
        ms = ImGui::EndMultiSelect();
        sel_.ApplyRequests(ms);
        // Teclado: Supr quita de la lista, Intro carga en el plato libre
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !ImGui::IsAnyItemActive()) {
            if (listView_ && ImGui::IsKeyPressed(ImGuiKey_Delete) && sel_.Size > 0) {
                std::vector<int> pos;
                for (int i = 0; i < (int)view_.size(); ++i) if (sel_.Contains(rowStorageId(&sel_, i)) && view_[i].listPos >= 0) pos.push_back(view_[i].listPos);
                removeRowsFromList(src_.list, pos);
            }
            if ((ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) && cursorRow_ >= 0) loadCursor = cursorRow_;
        }
        ImGui::EndTable();
    }
    // Soltar en una lista (insertar / reordenar)
    if (listView_ && (insertAt >= 0 || moveTo >= 0) && src_.kind == Src::Lista && src_.list >= 0 && src_.list < (int)library_.playlists().size()) {
        std::lock_guard<std::recursive_mutex> lk(library_.mutex());
        auto& paths = library_.playlists()[src_.list].paths;
        if (insertAt >= 0 && !insertPaths.empty()) {
            paths.insert(paths.begin() + std::min(insertAt, (int)paths.size()), insertPaths.begin(), insertPaths.end());
        } else if (moveTo >= 0 && !movedRows.empty() && std::find(movedRows.begin(), movedRows.end(), moveTo) == movedRows.end()) {
            std::vector<int> rows = movedRows;
            std::sort(rows.begin(), rows.end());
            std::vector<std::wstring> moving;
            for (int r : rows) if (r >= 0 && r < (int)paths.size()) moving.push_back(paths[r]);
            int target = moveTo;
            for (int r : rows) if (r < moveTo) --target;
            for (auto it = rows.rbegin(); it != rows.rend(); ++it) if (*it >= 0 && *it < (int)paths.size()) paths.erase(paths.begin() + *it);
            target = std::clamp(target + (moveTo > rows.front() ? 1 : 0), 0, (int)paths.size());
            paths.insert(paths.begin() + target, moving.begin(), moving.end());
            sel_.Clear();
            for (size_t k = 0; k < moving.size(); ++k) sel_.SetItemSelected(0x80000000u | (ImGuiID)(target + k + 1), true);
        }
        library_.markDirty();
        dragList_ = -2;
    }
    // Soltar en el hueco de debajo de la lista: añadir al final
    if (listView_ && ImGui::BeginDragDropTargetCustom(ImRect(a, b), ImGui::GetID("finlista"))) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("RDJ_PISTA")) {
            if (dragList_ != src_.list) library_.addToPlaylist(src_.list, uic::PayloadPaths(pl));
            dragList_ = -2;
        }
        ImGui::EndDragDropTarget();
    }
    if (loadCursor >= 0 && loadCursor < (int)view_.size()) loadFree(view_[loadCursor].path);
    if (view_.empty()) {
        const ImVec2 c(hp.x, hp.y + headH + 60 * s);
        const char* msg = "No hay pistas que coincidan.";
        if (!search_[0]) switch (src_.kind) {
            case Src::Coleccion: msg = "La colección está vacía. Abre una carpeta en el árbol (Este equipo), añade una carpeta favorita o arrastra archivos aquí."; break;
            case Src::Carpeta: msg = library_.browsing() ? "Leyendo la carpeta…" : "No hay audio en esta carpeta (MP3, WAV, FLAC u OGG). Activa «Leer también las subcarpetas» en el menú de opciones de la lista (los tres puntos) si están dentro."; break;
            case Src::Lista: msg = "Lista vacía: arrastra aquí pistas de la colección, de una carpeta, de la Sidelist o del Explorador."; break;
            case Src::Sugerencias: msg = "Carga o reproduce una pista para ver cuáles encajan (BPM y tonalidad)."; break;
            case Src::Stems: msg = "Esta canción no tiene pistas separadas."; break;
            case Src::Smart: msg = "Ninguna pista cumple las reglas de esta lista inteligente."; break;
        }
        ImGui::SetCursorScreenPos(ImVec2(c.x + 16 * s, c.y));
        ImGui::PushTextWrapPos(b.x - 30 * s - ImGui::GetWindowPos().x);
        ImGui::TextDisabled("%s", msg);
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
}

// =================================================================== panel derecho: Sidelist / Automix / Estadísticas

void App::drawSidelistPanel() {
    const float s = S();
    const int sl = ensurePlaylist("Sidelist");
    ImDrawList* dl = ImGui::GetWindowDrawList();
    std::lock_guard<std::recursive_mutex> lk(library_.mutex());
    auto& pl = library_.playlists()[sl];
    int moveFrom = -1, moveTo = -1, removeAt = -1, insertAt = -1, loadRow = -1;
    std::vector<std::wstring> insertPaths;
    const float rowH = 30 * s;
    const float fs = ImGui::GetFontSize();
    if (ImGui::BeginTable("sidelist", 4, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH,
                          ImVec2(0, ImGui::GetContentRegionAvail().y))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 24 * s);
        ImGui::TableSetupColumn("TEMA", ImGuiTableColumnFlags_None, 1.f);
        ImGui::TableSetupColumn("BPM", ImGuiTableColumnFlags_WidthFixed, 44 * s);
        ImGui::TableSetupColumn("TONO", ImGuiTableColumnFlags_WidthFixed, 40 * s);
        ImGui::PushFont(fontHead, 13.f);
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(col::TextDim));
        ImGui::TableHeadersRow();
        ImGui::PopStyleColor();
        ImGui::PopFont();
        for (int i = 0; i < (int)pl.paths.size(); ++i) {
            const LibTrack* lt = library_.find(pl.paths[i]);
            ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            const ImVec2 c0 = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##s", sideSel_ == i, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, rowH))) {
                sideSel_ = i;
                libSelectedPath_ = pl.paths[i];
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) loadRow = i;
            }
            if (ImGui::BeginDragDropSource()) {
                uic::SetPathsPayload({pl.paths[i]});
                dragList_ = sl;
                dragRows_ = {i};
                ImGui::TextUnformatted(lt ? (lt->artist + " - " + lt->title).c_str() : toUtf8(fileNameOf(pl.paths[i])).c_str());
                ImGui::TextDisabled("Suelta en otra fila para reordenar, o en un plato o lista");
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("RDJ_PISTA")) {
                    if (dragList_ == sl && dragRows_.size() == 1) { moveFrom = dragRows_[0]; moveTo = i; }
                    else { insertAt = i; insertPaths = uic::PayloadPaths(p); }
                    dragList_ = -2;
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::BeginPopupContextItem("ctx")) {
                sideSel_ = i;
                if (ImGui::MenuItem("Cargar en el plato A")) loadToDeck(0, pl.paths[i]);
                if (ImGui::MenuItem("Cargar en el plato B")) loadToDeck(1, pl.paths[i]);
                if (ImGui::MenuItem("Subir", nullptr, false, i > 0)) { moveFrom = i; moveTo = i - 1; }
                if (ImGui::MenuItem("Bajar", nullptr, false, i + 1 < (int)pl.paths.size())) { moveFrom = i; moveTo = i + 1; }
                if (ImGui::MenuItem("Quitar de la Sidelist", "Supr")) removeAt = i;
                ImGui::Separator();
                if (ImGui::MenuItem("Ir a su carpeta")) { BrowseSrc bs; bs.kind = Src::Carpeta; bs.folder = parentDir(pl.paths[i]); selectSource(bs); }
                if (ImGui::MenuItem("Mostrar en el Explorador")) win::revealInExplorer(pl.paths[i]);
                ImGui::EndPopup();
            }
            ImDrawList* cdl = ImGui::GetWindowDrawList();
            int onDeck = -1;
            for (int d = 0; d < kNumDecks; ++d) if (deck_[d].path == pl.paths[i]) onDeck = d;
            char n[12];
            std::snprintf(n, sizeof n, "%d", i + 1);
            cdl->AddText(ImVec2(c0.x + 2 * s, c0.y + (rowH - fs) * 0.5f), onDeck >= 0 ? col::deck(onDeck) : col::TextDim, n);
            ImGui::TableNextColumn();
            {
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float cw = ImGui::GetContentRegionAvail().x;
                const std::string t1 = lt ? lt->title : toUtf8(fileNameOf(pl.paths[i]));
                const std::string t2 = lt ? lt->artist : std::string("(no encontrado)");
                cdl->AddText(fontHead, fs * 0.98f, ImVec2(p.x, c0.y + 1 * s), lt && !lt->missing ? col::Text : col::Red, ellipsize(t1, cw, fontHead, fs * 0.98f).c_str());
                cdl->AddText(ImGui::GetFont(), fs * 0.8f, ImVec2(p.x, c0.y + rowH * 0.5f + 0.5f * s), col::TextDim, ellipsize(t2, cw, ImGui::GetFont(), fs * 0.8f).c_str());
            }
            ImGui::TableNextColumn();
            if (lt && lt->meta.bpm > 0) { char bb[16]; std::snprintf(bb, sizeof bb, "%.1f", lt->meta.bpm); cdl->AddText(fontHead, fs, ImVec2(ImGui::GetCursorScreenPos().x, c0.y + (rowH - fs) * 0.5f), col::Text, bb); }
            ImGui::TableNextColumn();
            if (lt && lt->meta.key >= 0)
                Badge(cdl, ImVec2(ImGui::GetCursorScreenPos().x, c0.y + (rowH - 18 * s) * 0.5f), 18 * s, keyCamelot(lt->meta.key), col::camelot(camelotIndex(lt->meta.key)), false, fontHead);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (pl.paths.empty()) {
        const ImVec2 p = ImGui::GetItemRectMin(), q = ImGui::GetItemRectMax();
        ImGui::SetCursorScreenPos(ImVec2(p.x + 10 * s, p.y + 40 * s));
        ImGui::PushTextWrapPos(q.x - 10 * s - ImGui::GetWindowPos().x);
        ImGui::TextDisabled("La Sidelist es tu lista de preparación: arrastra aquí pistas del navegador o del Explorador para tenerlas a mano.");
        ImGui::PopTextWrapPos();
    }
    if (sideSel_ >= 0 && sideSel_ < (int)pl.paths.size() && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete) && !ImGui::IsAnyItemActive())
        removeAt = sideSel_;
    if (moveFrom >= 0 && moveTo >= 0 && moveFrom != moveTo && moveFrom < (int)pl.paths.size()) {
        std::wstring item = pl.paths[moveFrom];
        pl.paths.erase(pl.paths.begin() + moveFrom);
        pl.paths.insert(pl.paths.begin() + std::min(moveTo, (int)pl.paths.size()), item);
        sideSel_ = moveTo;
        library_.markDirty();
    }
    if (insertAt >= 0 && !insertPaths.empty()) { pl.paths.insert(pl.paths.begin() + insertAt, insertPaths.begin(), insertPaths.end()); library_.markDirty(); }
    if (removeAt >= 0 && removeAt < (int)pl.paths.size()) { pl.paths.erase(pl.paths.begin() + removeAt); sideSel_ = -1; library_.markDirty(); }
    if (loadRow >= 0 && loadRow < (int)pl.paths.size()) loadFree(pl.paths[loadRow]);
    (void)dl;
}

void App::drawSideView(ImVec2 a, ImVec2 b) {
    const float s = S();
    sideRect_[0] = a; sideRect_[1] = b;
    Panel(ImGui::GetWindowDrawList(), a, b, 10 * s, col::Panel, col::Line);
    BeginPanel("lateral", a, b, 10 * s);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetContentRegionAvail().y;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const int tab = std::clamp(settings_.sideTab, 0, 2);
    // Cabecera
    const int sl = ensurePlaylist("Sidelist"), am = ensurePlaylist("Automix");
    size_t nSide = 0, nAm = 0;
    {
        std::lock_guard<std::recursive_mutex> lk(library_.mutex());
        nSide = library_.playlists()[sl].paths.size();
        nAm = library_.playlists()[am].paths.size();
    }
    const char* titles[3] = {"LISTA CONTIGUA (SIDELIST)", "AUTOMIX", "ESTADÍSTICAS"};
    dl->AddText(fontHead, 14 * s, ImVec2(p.x, p.y + 4 * s), col::Text, titles[tab]);
    if (tab < 2) {
        char n[24];
        std::snprintf(n, sizeof n, "%zu", tab == 0 ? nSide : nAm);
        dl->AddText(fontHead, 14 * s, ImVec2(p.x + fontHead->CalcTextSizeA(14 * s, FLT_MAX, 0, titles[tab]).x + 8 * s, p.y + 4 * s), col::TextDim, n);
        ImGui::SetCursorScreenPos(ImVec2(p.x + w - 26 * s, p.y));
        if (IconButton("##sidemenu", Ic::Dots, ImVec2(26 * s, 24 * s), col::DeckA, false, true, 0.6f)) ImGui::OpenPopup("sidemenu");
        if (ImGui::BeginPopup("sidemenu")) {
            const int list = tab == 0 ? sl : am;
            if (tab == 0 && ImGui::MenuItem("Guardar como lista nueva…", nullptr, false, nSide > 0)) openNameDialog(2, "Sesión " + std::string(todayHistoryName().substr(10)));
            if (tab == 0 && ImGui::MenuItem("Pasar todo al Automix", nullptr, false, nSide > 0)) {
                std::lock_guard<std::recursive_mutex> lk(library_.mutex());
                auto& v = library_.playlists()[am].paths;
                auto& src = library_.playlists()[sl].paths;
                v.insert(v.end(), src.begin(), src.end());
                library_.markDirty();
            }
            if (ImGui::MenuItem("Exportar M3U8…")) {
                const std::wstring f = win::pickSaveFile(hwnd_, tab == 0 ? L"Sidelist.m3u8" : L"Automix.m3u8", L"Lista M3U8", L"*.m3u8", L"m3u8");
                if (!f.empty()) notify(library_.exportM3u(list, f) ? "Lista exportada a " + toUtf8(f) : "No se pudo exportar la lista.", false);
            }
            if (ImGui::MenuItem("Vaciar", nullptr, false, (tab == 0 ? nSide : nAm) > 0)) {
                std::lock_guard<std::recursive_mutex> lk(library_.mutex());
                library_.playlists()[list].paths.clear();
                library_.markDirty();
                if (tab == 1) automixIndex_ = 0;
            }
            ImGui::EndPopup();
        }
    }
    // Contenido
    const float tabH = 30 * s;
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 28 * s));
    ImGui::BeginChild("lateralcontenido", ImVec2(w, h - 28 * s - tabH - 6 * s), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    if (tab == 0) drawSidelistPanel();
    else if (tab == 1) drawAutomixPanel();
    else drawStatsPanel();
    ImGui::EndChild();
    // Soltar pistas en el panel
    if (tab < 2 && ImGui::BeginDragDropTargetCustom(ImRect(a, b), ImGui::GetID("ladrop"))) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("RDJ_PISTA")) {
            if (dragList_ != (tab == 0 ? sl : am)) {
                auto paths = uic::PayloadPaths(pl);
                if (tab == 0) addToSidelist(paths);
                else library_.addToPlaylist(am, paths);
            }
            dragList_ = -2;
        }
        ImGui::EndDragDropTarget();
    }
    // Pestañas abajo (como en VirtualDJ)
    const float ty = p.y + h - tabH;
    dl->AddLine(ImVec2(p.x - 10 * s, ty - 4 * s), ImVec2(p.x + w + 10 * s, ty - 4 * s), col::Line);
    const struct { const char* lbl; Ic ic; } tabs[3] = {{"Sidelist", Ic::List}, {"Automix", Ic::Shuffle}, {"Estadísticas", Ic::Stats}};
    const float tw = w / 3.f;
    for (int i = 0; i < 3; ++i) {
        const ImVec2 ta(p.x + i * tw, ty), tb(p.x + (i + 1) * tw, ty + tabH);
        ImGui::SetCursorScreenPos(ta);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##sidetab", ImVec2(tw, tabH))) settings_.sideTab = i;
        ImGui::PopID();
        const bool on = tab == i, hov = ImGui::IsItemHovered();
        // Soltar sobre la pestaña: añadir a esa lista
        if (i < 2 && ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("RDJ_PISTA")) {
                auto paths = uic::PayloadPaths(pl);
                if (i == 0) addToSidelist(paths);
                else library_.addToPlaylist(am, paths);
                dragList_ = -2;
            }
            ImGui::EndDragDropTarget();
        }
        if (on) dl->AddRectFilled(ImVec2(ta.x + 2 * s, ta.y), ImVec2(tb.x - 2 * s, tb.y), col::Panel2, 6 * s);
        const ImU32 c = on ? col::Text : hov ? col::mix(col::TextDim, col::Text, 0.5f) : col::TextDim;
        const float lw = fontHead->CalcTextSizeA(13 * s, FLT_MAX, 0, tabs[i].lbl).x;
        const float cx = (ta.x + tb.x) * 0.5f - (lw + 18 * s) * 0.5f;
        DrawIcon(dl, tabs[i].ic, ImVec2(cx + 7 * s, ty + tabH * 0.5f), 13 * s, on ? col::DeckA : c);
        dl->AddText(fontHead, 13 * s, ImVec2(cx + 18 * s, ty + (tabH - 13 * s) * 0.5f - 1 * s), c, tabs[i].lbl);
        if (on) dl->AddRectFilled(ImVec2(ta.x + 12 * s, tb.y - 2 * s), ImVec2(tb.x - 12 * s, tb.y), col::DeckA, 1 * s);
    }
    ImGui::EndChild();
}

// =================================================================== panel de detalles

void App::drawInfoView(ImVec2 a, ImVec2 b) {
    const float s = S();
    Panel(ImGui::GetWindowDrawList(), a, b, 10 * s, col::Panel, col::Line);
    BeginPanel("detalles", a, b, 12 * s, true);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x;
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddText(fontHead, 13 * s, ImVec2(p.x, p.y + 3 * s), col::TextDim, "DETALLES DEL TEMA");
    ImGui::SetCursorScreenPos(ImVec2(p.x + w - 22 * s, p.y));
    if (IconButton("##plegarinfo", Ic::ChevRight, ImVec2(22 * s, 22 * s), col::DeckA, false, true, 0.5f)) settings_.showInfo = false;
    ImGui::SetItemTooltip("Plegar los detalles (queda una franja para volver a abrirlos)");
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 28 * s));

    std::wstring path = libSelectedPath_;
    if (path.empty()) for (int d = 0; d < kNumDecks; ++d) if (engine_.deckStatus[d].playing.load()) path = deck_[d].path;
    if (path.empty()) {
        ImGui::PushTextWrapPos(p.x + w - ImGui::GetWindowPos().x);
        ImGui::TextDisabled("Selecciona una pista en la lista para ver su carátula, tonalidad, compatibilidad y más datos.");
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        return;
    }
    std::lock_guard<std::recursive_mutex> lk(library_.mutex());
    const LibTrack* t = library_.find(path);
    std::string title = t ? t->title : toUtf8(fileNameOf(path));
    if (auto it = stemTitles_.find(path); it != stemTitles_.end() && !t) title = it->second;
    ImFont* f = ImGui::GetFont();

    // ---- Cabecera: carátula + título (hasta 2 líneas) + artista + valoración
    p = ImGui::GetCursorScreenPos();
    const float art = std::clamp(w * 0.34f, 64 * s, 96 * s);
    DrawCover(dl, path, p, ImVec2(p.x + art, p.y + art), 8 * s, title.c_str());
    {
        const float tx = p.x + art + 12 * s, tw = w - art - 12 * s;
        std::string l1 = title, l2;
        const float tfs = 17 * s;
        if (fontHead->CalcTextSizeA(tfs, FLT_MAX, 0, title.c_str()).x > tw) {
            size_t cut = title.size();
            while (cut > 0 && fontHead->CalcTextSizeA(tfs, FLT_MAX, 0, title.c_str(), title.c_str() + cut).x > tw) {
                const size_t sp = title.rfind(' ', cut - 1);
                if (sp == std::string::npos || sp == 0) break;
                cut = sp;
            }
            l1 = ellipsize(title.substr(0, cut), tw, fontHead, tfs);
            l2 = ellipsize(title.substr(std::min(title.size(), cut + 1)), tw, fontHead, tfs);
        }
        float y = p.y;
        dl->AddText(fontHead, tfs, ImVec2(tx, y), col::Text, l1.c_str());
        y += 19 * s;
        if (!l2.empty()) { dl->AddText(fontHead, tfs, ImVec2(tx, y), col::Text, l2.c_str()); y += 19 * s; }
        if (t) {
            dl->AddText(f, 13.5f * s, ImVec2(tx, y + 1 * s), col::mix(col::Text, col::TextDim, 0.4f), ellipsize(t->artist.empty() ? "Artista desconocido" : t->artist, tw, f, 13.5f * s).c_str());
            y += 21 * s;
            if (!t->genre.empty()) { dl->AddText(f, 12 * s, ImVec2(tx, y), col::TextDim, ellipsize(t->genre, tw, f, 12 * s).c_str()); y += 17 * s; }
            int rating = t->rating;
            if (RatingStars("##valinfo", &rating, ImVec2(tx, y + 2 * s), 14 * s, col::Amber, col::withAlpha(col::TextDim, 150))) library_.setRating(path, rating);
            ImGui::SetItemTooltip("Valoración (clic en una estrella; otra vez en la misma para quitarla). Solo se guarda en DJ Elver Galarga.");
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + art + 10 * s));
        ImGui::Dummy(ImVec2(w, 0));
    }
    // ---- Tres datos grandes: BPM, tonalidad y duración
    if (t) {
        const ImVec2 q = ImGui::GetCursorScreenPos();
        const float tg = 6 * s, th = 52 * s, tw3 = (w - 2 * tg) / 3.f;
        for (int k = 0; k < 3; ++k) {
            const ImVec2 ta(q.x + k * (tw3 + tg), q.y), tb(ta.x + tw3, q.y + th);
            dl->AddRectFilled(ta, tb, col::Panel2, 7 * s);
            const char* cap = k == 0 ? "BPM" : k == 1 ? "TONALIDAD" : "DURACIÓN";
            dl->AddText(fontHead, 10.5f * s, ImVec2(ta.x + 8 * s, ta.y + 6 * s), col::TextDim, cap);
            const float vy = ta.y + 21 * s, vfs = 21 * s;
            if (k == 0) {
                char b2[24];
                if (t->meta.bpm > 0) std::snprintf(b2, sizeof b2, "%.1f", t->meta.bpm); else std::snprintf(b2, sizeof b2, "%s", t->meta.analyzed ? "?" : "…");
                dl->AddText(fontBold, vfs, ImVec2(ta.x + 8 * s, vy), t->meta.bpm > 0 ? col::Text : col::TextDim, b2);
            } else if (k == 1) {
                if (t->meta.key >= 0) {
                    const float bw = Badge(dl, ImVec2(ta.x + 8 * s, vy + 1 * s), 21 * s, keyCode(t->meta.key).c_str(), col::camelot(camelotIndex(t->meta.key)), true, fontHead, 14 * s);
                    if (tw3 - bw - 20 * s > 18 * s)
                        dl->AddText(fontHead, 13 * s, ImVec2(ta.x + 12 * s + bw, vy + 4 * s), col::Amber, ellipsize(keyShort(t->meta.key), tw3 - bw - 20 * s, fontHead, 13 * s).c_str());
                } else dl->AddText(fontBold, vfs, ImVec2(ta.x + 8 * s, vy), col::TextDim, t->meta.keyAnalyzed ? "?" : "…");
            } else {
                dl->AddText(fontBold, vfs, ImVec2(ta.x + 8 * s, vy), col::Text, t->meta.durationSec > 0 ? formatTime(t->meta.durationSec, false).c_str() : "—");
            }
            if (k == 1 && t->meta.keySrc) {
                ImGui::SetCursorScreenPos(ta);
                ImGui::InvisibleButton("##tilekey", ImVec2(tw3, th));
                if (t->meta.key >= 0) ImGui::SetItemTooltip("%s · %s (%s)\nTonalidad %s.", keyCode(t->meta.key).c_str(), keyNameEs(t->meta.key), keyName(t->meta.key), keySourceText(t->meta.keySrc));
            }
        }
        ImGui::SetCursorScreenPos(ImVec2(q.x, q.y + th + 10 * s));
        ImGui::Dummy(ImVec2(w, 0));
    }
    // ---- Vista general en capas (si la pista está en un plato): estructura, subidones y hot cues; clic = ir ahí.
    for (int d = 0; d < kNumDecks; ++d) {
        if (deck_[d].path != path || !deck_[d].track || deck_[d].track->wave.empty()) continue;
        const ImVec2 wa = ImGui::GetCursorScreenPos(), wb(wa.x + w, wa.y + 40 * s);
        drawOverview(d, wa, wb);
        ImGui::SetCursorScreenPos(ImVec2(wa.x, wb.y + 10 * s));
        ImGui::Dummy(ImVec2(w, 0));
        break;
    }
    // Secciones plegables (se recuerda si están abiertas).
    auto section = [&](const char* id, const char* label, bool* open) {
        const ImVec2 sp = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(id, ImVec2(w, 22 * s));
        if (ImGui::IsItemClicked()) *open = !*open;
        const bool hov = ImGui::IsItemHovered();
        DrawIcon(dl, *open ? Ic::ChevDown : Ic::ChevRight, ImVec2(sp.x + 6 * s, sp.y + 11 * s), 11 * s, hov ? col::Text : col::TextDim);
        dl->AddText(fontHead, 13 * s, ImVec2(sp.x + 16 * s, sp.y + 4 * s), hov ? col::Text : col::mix(col::Text, col::TextDim, 0.3f), label);
        dl->AddLine(ImVec2(sp.x + 22 * s + fontHead->CalcTextSizeA(13 * s, FLT_MAX, 0, label).x, sp.y + 11 * s), ImVec2(sp.x + w, sp.y + 11 * s), col::Line);
        return *open;
    };
    // ---- Rueda armónica (Camelot): compatibles, platos y filtro de la lista.
    if (t && section("##secrueda", "RUEDA ARMÓNICA", &settings_.infoWheelOpen)) { drawKeySection(path, t->meta.key, w); ImGui::Dummy(ImVec2(0, 4 * s)); }
    // ---- Datos: compactos, en dos columnas.
    if (t && section("##secdatos", "DATOS", &settings_.infoMetaOpen)) {
        auto row = [&](const char* k, const std::string& v, ImU32 vc = 0) {
            const ImVec2 rp = ImGui::GetCursorScreenPos();
            dl->AddText(f, 12.5f * s, rp, col::TextDim, k);
            dl->AddText(f, 12.5f * s, ImVec2(rp.x + w * 0.4f, rp.y), vc ? vc : col::Text, ellipsize(v, w * 0.6f, f, 12.5f * s).c_str());
            ImGui::Dummy(ImVec2(w, 17 * s));
        };
        if (t->meta.analyzed && t->meta.bpm > 0)
            row("Rejilla", t->meta.gridSrc == 3 ? "ajustada a mano" : t->meta.gridSrc == 4 ? "de VirtualDJ (se comprueba al cargar)" : t->meta.gridSrc == 7 || t->meta.gridSrc == 8 ? "de VirtualDJ" :
                          t->meta.gridSrc == 5 ? "de VirtualDJ (se afina al cargar)" : t->meta.gridSrc == 6 ? "automática" : "automática (se afina al cargar)");
        if (t->meta.key >= 0 && t->meta.keySrc) row("Tonalidad", keySourceText(t->meta.keySrc));
        row("Añadida", t->firstSeen > 0 ? dateText(t->firstSeen, false) : "—");
        char b2[96];
        std::snprintf(b2, sizeof b2, "%d", t->playCount);
        row("Reproducida", t->playCount ? std::string(b2) + (t->playCount == 1 ? " vez" : " veces") : "nunca");
        if (t->lastPlayed > 0) row("Última vez", dateText(t->lastPlayed, true));
        int hot = 0;
        std::string names;
        for (int i = 0; i < kNumHotCues; ++i) if (t->meta.hotCuesSec[i] >= 0) { ++hot; if (!t->meta.hotCueNames[i].empty()) names += (names.empty() ? "" : ", ") + t->meta.hotCueNames[i]; }
        std::snprintf(b2, sizeof b2, "%d", hot);
        row("Hot cues", hot ? std::string(b2) + (names.empty() ? "" : " · " + names) : "ninguno");
        if (auto it = settings_.trackTags.find(toUtf8(path)); it != settings_.trackTags.end()) row("Etiquetas", it->second);
        std::snprintf(b2, sizeof b2, "%.1f MB", t->size / 1048576.0);
        row("Tamaño", b2);
        {
            const ImVec2 rp = ImGui::GetCursorScreenPos();
            dl->AddText(f, 12.5f * s, rp, col::TextDim, "Carpeta");
            const std::string pth = toUtf8(parentDir(path));
            dl->AddText(f, 12.5f * s, ImVec2(rp.x + w * 0.4f, rp.y), col::Text, ellipsize(pth, w * 0.6f - 22 * s, f, 12.5f * s).c_str());
            ImGui::SetCursorScreenPos(ImVec2(rp.x + w - 20 * s, rp.y - 3 * s));
            if (IconButton("##revelar", Ic::Folder, ImVec2(20 * s, 20 * s), col::DeckA, false, true, 0.7f)) win::revealInExplorer(path);
            ImGui::SetItemTooltip("%s", toUtf8(path).c_str());
            ImGui::SetCursorScreenPos(ImVec2(rp.x, rp.y + 18 * s));
            ImGui::Dummy(ImVec2(w, 0));
        }
        ImGui::Dummy(ImVec2(0, 4 * s));
    }
    // ---- Carátula: de dónde sale o por qué falta (y buscarla / asignarla).
    drawCoverInfo(path, t ? t->artist : std::string(), title, w);
    if (!t) ImGui::TextDisabled("Esta pista aún no está en la colección.");
    ImGui::EndChild();
}

// =================================================================== carátulas: diagnóstico y búsqueda en Internet

void App::fetchCovers(const std::vector<std::wstring>& paths, bool manual) {
    int n = 0;
    for (const auto& p : paths) {
        std::string artist, title;
        {
            std::lock_guard<std::recursive_mutex> lk(library_.mutex());
            if (const LibTrack* t = library_.find(p)) { artist = t->artist; title = t->title; }
        }
        if (title.empty()) {
            const TagInfo ti = readTags(p);
            artist = ti.artist; title = ti.title;
        }
        CoverFetcher::get().request(p, artist, title, manual);
        ++n;
    }
    if (manual && n > 1) {
        coverBatch_ = true; coverFound_ = coverMissing_ = 0;
        notify("Buscando carátulas en Internet para " + std::to_string(n) + " pistas (las que ya tienen se saltan)…");
    }
}

void App::pollCoverFetch() {
    for (auto& r : CoverFetcher::get().takeFinished()) {
        using St = CoverFetcher::State;
        if (!r.manual) {
            // Sin conexión: tras varios fallos seguidos se para 10 minutos.
            if (r.state == St::Failed) { if (++coverFailRun_ >= 4) { CoverFetcher::get().cancelAll(); coverOfflineUntil_ = now_ + 600; coverFailRun_ = 0; } }
            else coverFailRun_ = 0;
            if (r.state == St::Found) ++coverSweepFound_;
        }
        if (r.state == St::Found) { covers().invalidate(r.path); ++coverFound_; }
        else if (r.state == St::NotFound || r.state == St::Failed) ++coverMissing_;
        if (r.manual && !coverBatch_) {
            const std::string name = toUtf8(fileNameOf(r.path));
            if (r.state == St::Found) notify("Carátula descargada (" + r.detail + ") para " + name + ".");
            else if (r.state == St::HasCover) notify("Esa pista ya tiene carátula (en el archivo o en su carpeta).");
            else if (r.state == St::NotFound) notify("No se encontró una carátula fiable para " + name + (r.detail.empty() ? "." : " (" + r.detail + ")."), true);
            else notify("No se pudo buscar la carátula: " + (r.detail.empty() ? std::string("sin conexión") : r.detail) + ".", true);
        }
    }
    if (coverBatch_ && CoverFetcher::get().pending() == 0) {
        coverBatch_ = false;
        notify("Carátulas: " + std::to_string(coverFound_) + " descargadas, " + std::to_string(coverMissing_) + " sin encontrar.", coverFound_ == 0 && coverMissing_ > 0);
    }
}

void App::coverSweepTick() {
    if (settings_.coverDownload < 2 || autotest_.active) return;
    if (now_ - coverSweepLast_ < 1.0) return;
    coverSweepLast_ = now_;
    CoverFetcher& cf = CoverFetcher::get();
    if (now_ - coverMemSaved_ > 30) { coverMemSaved_ = now_; cf.saveMemory(); }
    if (now_ < coverOfflineUntil_) return;
    if (coverOfflineUntil_ > 0) { coverOfflineUntil_ = 0; cf.resetFailed(); }
    if (cf.pending() > 20) return;
    struct Req { std::wstring path; std::string artist, title; };
    std::vector<Req> batch;
    {
        std::lock_guard<std::recursive_mutex> lk(library_.mutex());
        const auto& tr = library_.tracks();
        if (coverSweepDone_ && tr.size() != coverSweepCount_) { coverSweepDone_ = false; coverSweepPos_ = 0; }  // pistas nuevas
        if (coverSweepDone_) return;
        int scanned = 0;
        while (coverSweepPos_ < tr.size() && batch.size() < 30 && scanned < 500) {
            const LibTrack& t = tr[coverSweepPos_++];
            ++scanned;
            if (t.missing || cf.state(t.path) != CoverFetcher::State::None || cf.skipAuto(t.path)) continue;
            batch.push_back(Req{t.path, t.artist, t.title});
        }
        if (coverSweepPos_ >= tr.size()) { coverSweepDone_ = true; coverSweepCount_ = tr.size(); }
    }
    for (auto& r : batch) cf.request(r.path, r.artist, r.title, false);
}

void App::drawCoverInfo(const std::wstring& path, const std::string& artist, const std::string& title, float w) {
    const float s = S();
    const CoverStatus cs = covers().status(path);
    const CoverFetcher::State fs = CoverFetcher::get().state(path);
    std::string line;
    ImU32 c = col::TextDim;
    switch (cs.state) {
    case CoverStatus::Ready: line = "Carátula: " + cs.source + (cs.format.empty() ? "" : " · " + cs.format); break;
    case CoverStatus::NoImage: line = "Sin carátula en el archivo, en su carpeta ni en VirtualDJ."; c = col::Amber; break;
    case CoverStatus::Undecodable:
        line = "La imagen (" + cs.format + ", " + cs.source + ") no se puede mostrar.";
        if (cs.format == "WebP") line += " Instala «Extensiones de imagen WebP» desde Microsoft Store.";
        c = col::Amber;
        break;
    default: line = "Carátula: leyendo…"; break;
    }
    if (fs == CoverFetcher::State::Queued || fs == CoverFetcher::State::Searching) { line = "Buscando la carátula en Internet…"; c = col::DeckA; }
    else if (fs == CoverFetcher::State::NotFound && cs.state != CoverStatus::Ready) line += " En Internet no apareció una fiable.";
    else if (fs == CoverFetcher::State::Failed && cs.state != CoverStatus::Ready) line += " No se pudo conectar para buscarla.";
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(c));
    ImGui::TextWrapped("%s", line.c_str());
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    if (cs.state == CoverStatus::NoImage || cs.state == CoverStatus::Undecodable || ImGui::GetIO().KeyShift) {
        const bool busy = fs == CoverFetcher::State::Queued || fs == CoverFetcher::State::Searching;
        if (NeonButton(busy ? "Buscando…##cvnet" : "Buscar en Internet##cvnet", col::DeckA, false, ImVec2((w - 6 * s) * 0.55f, 26 * s), !busy, fontHead, 13 * s))
            CoverFetcher::get().request(path, artist, title, true);
        ImGui::SetItemTooltip("Busca la portada por artista y título (iTunes y Deezer) y la guarda aparte, sin tocar el archivo de audio.");
        ImGui::SameLine(0, 6 * s);
        if (NeonButton("Asignar…##cvset", col::DeckA, false, ImVec2((w - 6 * s) * 0.45f, 26 * s), true, fontHead, 13 * s)) {
            const auto image = win::pickImage(hwnd_);
            if (!image.empty()) {
                std::error_code ec;
                const std::filesystem::path dest(coverOverridePath(path));
                std::filesystem::create_directories(dest.parent_path(), ec);
                if (!ec) std::filesystem::copy_file(image, dest, std::filesystem::copy_options::overwrite_existing, ec);
                if (ec) notify("No se pudo asignar la carátula: " + ec.message(), true);
                else { covers().invalidate(path); notify("Carátula asignada sin modificar el audio."); }
            }
        }
        ImGui::SetItemTooltip("Elegir una imagen del disco como carátula de esta pista");
    }
    ImGui::Dummy(ImVec2(0, 4 * s));
}

} // namespace rdj

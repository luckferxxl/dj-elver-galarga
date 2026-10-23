#include "vdjimport.h"
#include "library.h"
#include "util.h"
#include "../audio/analysis.h"
#include <windows.h>
#include <shlobj.h>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <unordered_map>

namespace rdj {

namespace {

std::wstring lowerPath(std::wstring p) {
    for (auto& c : p) { if (c == L'/') c = L'\\'; c = (wchar_t)std::towlower(c); }
    return p;
}

std::wstring knownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)) && p) out = p;
    if (p) CoTaskMemFree(p);
    return out;
}

std::wstring envVar(const wchar_t* name) {
    wchar_t buf[1024];
    const DWORD n = GetEnvironmentVariableW(name, buf, 1024);
    return n > 0 && n < 1024 ? std::wstring(buf, n) : std::wstring();
}

// ------------------------------------------------------------------ XML mínimo (lo que usa database.xml)

std::string xmlUnescape(const char* b, const char* e) {
    std::string o;
    o.reserve(e - b);
    while (b < e) {
        if (*b != '&') { o.push_back(*b++); continue; }
        const char* semi = (const char*)std::memchr(b, ';', std::min<size_t>(12, e - b));
        if (!semi) { o.push_back(*b++); continue; }
        const std::string ent(b + 1, semi);
        if (ent == "amp") o.push_back('&');
        else if (ent == "lt") o.push_back('<');
        else if (ent == "gt") o.push_back('>');
        else if (ent == "quot") o.push_back('"');
        else if (ent == "apos") o.push_back('\'');
        else if (!ent.empty() && ent[0] == '#') {
            unsigned long cp = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X') ? std::strtoul(ent.c_str() + 2, nullptr, 16) : std::strtoul(ent.c_str() + 1, nullptr, 10);
            // UTF-8
            if (cp < 0x80) o.push_back((char)cp);
            else if (cp < 0x800) { o.push_back((char)(0xC0 | (cp >> 6))); o.push_back((char)(0x80 | (cp & 0x3F))); }
            else if (cp < 0x10000) { o.push_back((char)(0xE0 | (cp >> 12))); o.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); o.push_back((char)(0x80 | (cp & 0x3F))); }
            else { o.push_back((char)(0xF0 | (cp >> 18))); o.push_back((char)(0x80 | ((cp >> 12) & 0x3F))); o.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); o.push_back((char)(0x80 | (cp & 0x3F))); }
        } else { o.append(b, semi + 1); }
        b = semi + 1;
    }
    return o;
}

using Attrs = std::vector<std::pair<std::string, std::string>>;

// Atributos entre p (justo tras el nombre de la etiqueta) y end ('>').
void parseAttrs(const char* p, const char* end, Attrs& out) {
    out.clear();
    while (p < end) {
        while (p < end && (std::isspace((unsigned char)*p) || *p == '/')) ++p;
        const char* n0 = p;
        while (p < end && *p != '=' && !std::isspace((unsigned char)*p)) ++p;
        const std::string name(n0, p);
        while (p < end && std::isspace((unsigned char)*p)) ++p;
        if (p >= end || *p != '=') break;
        ++p;
        while (p < end && std::isspace((unsigned char)*p)) ++p;
        if (p >= end || (*p != '"' && *p != '\'')) break;
        const char q = *p++;
        const char* v0 = p;
        while (p < end && *p != q) ++p;
        out.push_back({name, xmlUnescape(v0, p)});
        if (p < end) ++p;
    }
}

const std::string* attr(const Attrs& a, const char* name) {
    for (auto& kv : a) if (kv.first == name) return &kv.second;
    return nullptr;
}

double num(const std::string* s) { return s ? std::atof(s->c_str()) : 0.0; }

// BPM de VirtualDJ: en Scan va como segundos por pulso; en Tags puede ir en BPM.
double vdjBpm(const std::string* s) {
    const double v = num(s);
    if (v <= 0) return 0;
    return v < 5.0 ? 60.0 / v : v;
}

} // namespace

std::vector<VdjDatabase> findVdjDatabases() {
    // Sin ventanas de "no hay disco" al mirar lectores vacíos.
    const UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    std::vector<std::wstring> homes;
    if (auto e = envVar(L"RDJ_VDJ_DIR"); !e.empty()) homes.push_back(e);  // pruebas
    if (auto d = knownFolder(FOLDERID_Documents); !d.empty()) homes.push_back(d + L"\\VirtualDJ");
    if (auto u = envVar(L"USERPROFILE"); !u.empty()) { homes.push_back(u + L"\\Documents\\VirtualDJ"); homes.push_back(u + L"\\OneDrive\\Documents\\VirtualDJ"); homes.push_back(u + L"\\OneDrive\\Documentos\\VirtualDJ"); }
    if (auto l = envVar(L"LOCALAPPDATA"); !l.empty()) homes.push_back(l + L"\\VirtualDJ");
    if (auto a = envVar(L"APPDATA"); !a.empty()) homes.push_back(a + L"\\VirtualDJ");
    const DWORD drives = GetLogicalDrives();
    for (int i = 2; i < 26; ++i) {  // desde C:
        if (!(drives & (1u << i))) continue;
        const std::wstring root = std::wstring(1, (wchar_t)(L'A' + i)) + L":\\";
        const UINT t = GetDriveTypeW(root.c_str());
        if (t != DRIVE_FIXED && t != DRIVE_REMOVABLE) continue;
        homes.push_back(root + L"VirtualDJ");
    }
    std::vector<VdjDatabase> out;
    std::vector<std::wstring> seen;
    for (auto& h : homes) {
        const std::wstring f = h + L"\\database.xml";
        if (GetFileAttributesW(f.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        // La misma carpeta puede llegar por dos caminos (Documentos y USERPROFILE).
        wchar_t full[MAX_PATH * 2];
        const std::wstring key = lowerPath(GetFullPathNameW(f.c_str(), MAX_PATH * 2, full, nullptr) ? full : f.c_str());
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
        seen.push_back(key);
        out.push_back(VdjDatabase{f, h, 0});
    }
    SetErrorMode(oldMode);
    return out;
}

namespace {
unsigned long long fileStamp(const std::wstring& f) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(f.c_str(), GetFileExInfoStandard, &d)) return 0;
    return ((unsigned long long)d.ftLastWriteTime.dwHighDateTime << 32 | d.ftLastWriteTime.dwLowDateTime) ^ ((unsigned long long)d.nFileSizeLow << 1);
}
} // namespace

void VdjLookup::refresh() {
    const unsigned long long now = GetTickCount64();
    if (loaded_ && now - checkedAt_ < 30000) return;
    checkedAt_ = now;
    std::vector<Db> cur;
    for (const VdjDatabase& d : findVdjDatabases()) cur.push_back({d.file, fileStamp(d.file)});
    bool same = loaded_ && cur.size() == dbs_.size();
    for (size_t i = 0; same && i < cur.size(); ++i) same = cur[i].file == dbs_[i].file && cur[i].stamp == dbs_[i].stamp;
    if (same) return;
    dbs_ = cur;
    songs_.clear();
    index_.clear();
    for (const Db& d : dbs_) {
        std::vector<VdjSong> v;
        std::string err;
        if (!parseVdjDatabase(d.file, v, err)) continue;
        for (auto& s : v) songs_.push_back(std::move(s));
    }
    index_.reserve(songs_.size());
    for (size_t i = 0; i < songs_.size(); ++i) index_.push_back({lowerPath(songs_[i].path), i});
    std::sort(index_.begin(), index_.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    loaded_ = true;
}

bool VdjLookup::find(const std::wstring& path, VdjSong& out) {
    refresh();
    const std::wstring key = lowerPath(path);
    auto it = std::lower_bound(index_.begin(), index_.end(), key, [](const auto& a, const std::wstring& k) { return a.first < k; });
    // La primera base de datos que la tiene con rejilla (la de Documentos va antes que las de los discos).
    for (; it != index_.end() && it->first == key; ++it) {
        const VdjSong& s = songs_[it->second];
        if (s.bpm > 30 && s.bpm < 300 && s.gridSec >= 0) { out = s; return true; }
    }
    return false;
}

bool parseVdjDatabase(const std::wstring& file, std::vector<VdjSong>& out, std::string& err, const std::atomic<bool>* cancel) {
    out.clear();
    std::string data;
    {
        FILE* f = _wfopen(file.c_str(), L"rb");
        if (!f) { err = "No se puede abrir " + toUtf8(file); return false; }
        fseek(f, 0, SEEK_END);
        const long n = ftell(f);
        if (n <= 0 || n > 512L * 1024 * 1024) { fclose(f); err = "El archivo está vacío o es demasiado grande."; return false; }
        data.resize((size_t)n);
        fseek(f, 0, SEEK_SET);
        data.resize(fread(data.data(), 1, (size_t)n, f));
        fclose(f);
    }
    if (data.find("<VirtualDJ_Database") == std::string::npos) { err = "No parece un database.xml de VirtualDJ."; return false; }
    // Unidad del propio database.xml (para las rutas relativas de los discos externos).
    std::wstring drive;
    if (file.size() > 2 && file[1] == L':') drive = file.substr(0, 2);
    const char* s = data.data();
    const char* end = s + data.size();
    Attrs a;
    size_t count = 0;
    while (s < end) {
        const char* p = std::strstr(s, "<Song");
        if (!p || p >= end) break;
        if (p[5] != ' ' && p[5] != '\t' && p[5] != '\r' && p[5] != '\n' && p[5] != '>') { s = p + 5; continue; }
        const char* tagEnd = (const char*)std::memchr(p, '>', end - p);
        if (!tagEnd) break;
        parseAttrs(p + 5, tagEnd, a);
        const char* contentEnd = tagEnd;
        const bool selfClosing = tagEnd > p && tagEnd[-1] == '/';
        if (!selfClosing) {
            const char* close = std::strstr(tagEnd, "</Song>");
            contentEnd = close ? close : end;
            s = close ? close + 7 : end;
        } else s = tagEnd + 1;
        if (cancel && cancel->load() ) return false;
        const std::string* fp = attr(a, "FilePath");
        if (!fp || fp->empty()) continue;
        VdjSong song;
        std::wstring path = fromUtf8(*fp);
        for (auto& c : path) if (c == L'/') c = L'\\';
        if (!(path.size() > 1 && path[1] == L':') && path.rfind(L"\\\\", 0) != 0 && !drive.empty())
            path = drive + (path.empty() || path[0] != L'\\' ? L"\\" : L"") + path;
        song.path = path;
        int tagKey = -1, scanKey = -1;
        double tagBpm = 0, scanBpm = 0;
        for (const char* q = tagEnd + 1; q < contentEnd;) {
            const char* lt = (const char*)std::memchr(q, '<', contentEnd - q);
            if (!lt) break;
            const char* gt = (const char*)std::memchr(lt, '>', contentEnd - lt);
            if (!gt) break;
            const char* n0 = lt + 1;
            const char* n1 = n0;
            while (n1 < gt && !std::isspace((unsigned char)*n1) && *n1 != '/' && *n1 != '>') ++n1;
            const std::string name(n0, n1);
            if (name == "Tags" || name == "Scan" || name == "Poi") {
                parseAttrs(n1, gt, a);
                if (name == "Tags") {
                    if (auto v = attr(a, "Author")) song.artist = *v;
                    if (auto v = attr(a, "Title")) song.title = *v;
                    if (auto v = attr(a, "Key")) tagKey = parseKey(*v);
                    tagBpm = vdjBpm(attr(a, "Bpm"));
                } else if (name == "Scan") {
                    if (auto v = attr(a, "Key")) scanKey = parseKey(*v);
                    scanBpm = vdjBpm(attr(a, "Bpm"));
                } else {
                    const std::string* type = attr(a, "Type");
                    const double pos = num(attr(a, "Pos"));
                    if (type && *type == "beatgrid") { if (song.gridSec < 0) song.gridSec = pos; }
                    else if (type && *type == "cue") {
                        int n = (int)num(attr(a, "Num"));
                        if (n <= 0) if (auto nm = attr(a, "Name"); nm && nm->rfind("Cue ", 0) == 0) n = std::atoi(nm->c_str() + 4);
                        if (n >= 1 && n <= 8 && pos >= 0) {
                            song.cues.push_back({n, pos});
                            const std::string* nm = attr(a, "Name");
                            // "Cue 3" es el nombre por defecto: no aporta nada.
                            const bool generic = !nm || nm->empty() || (nm->rfind("Cue ", 0) == 0 && std::atoi(nm->c_str() + 4) > 0);
                            song.cueNames.push_back(generic ? std::string() : *nm);
                        }
                    }
                }
            }
            q = gt + 1;
        }
        // VirtualDJ enseña la tonalidad de la etiqueta si existe; si no, la de su análisis.
        song.key = tagKey >= 0 ? tagKey : scanKey;
        song.bpm = scanBpm > 0 ? scanBpm : tagBpm;
        out.push_back(std::move(song));
        ++count;
    }
    if (out.empty()) { err = "El database.xml no tiene pistas."; return false; }
    return true;
}

VdjImportReport importVirtualDj(Library& lib, const std::wstring& dbFile, const VdjImportOptions& opt, std::atomic<float>* progress, const std::atomic<bool>* cancel) {
    VdjImportReport rep;
    auto prog = [&](float v) { if (progress) progress->store(v); };
    prog(0.02f);
    std::vector<VdjSong> songs;
    if (!parseVdjDatabase(dbFile, songs, rep.error, cancel)) return rep;
    rep.songs = songs.size();
    prog(0.3f);
    // Pistas de VirtualDJ que no están en la colección: se añaden si se pide.
    std::unordered_map<std::wstring, size_t> byPath;
    auto rebuild = [&] {
        byPath.clear();
        std::lock_guard<std::recursive_mutex> lk(lib.mutex());
        auto& tr = lib.tracks();
        for (size_t i = 0; i < tr.size(); ++i) byPath[lowerPath(tr[i].path)] = i;
    };
    rebuild();
    if (opt.addMissing) {
        std::vector<std::wstring> missing;
        for (auto& s : songs) {
            if (byPath.count(lowerPath(s.path))) continue;
            if (!isSupportedAudioExt(extensionLower(s.path))) continue;
            if (GetFileAttributesW(s.path.c_str()) == INVALID_FILE_ATTRIBUTES) { ++rep.missingFiles; continue; }
            missing.push_back(s.path);
        }
        for (size_t i = 0; i < missing.size(); i += 100) {
            if (cancel && cancel->load()) return rep;
            std::vector<std::wstring> chunk(missing.begin() + i, missing.begin() + std::min(missing.size(), i + 100));
            rep.added += lib.addFiles(chunk).size();
            prog(0.3f + 0.4f * (float)(i + chunk.size()) / (float)missing.size());
        }
        rebuild();
    }
    prog(0.75f);
    {
        std::lock_guard<std::recursive_mutex> lk(lib.mutex());
        auto& tr = lib.tracks();
        for (auto& s : songs) {
            auto it = byPath.find(lowerPath(s.path));
            if (it == byPath.end() || it->second >= tr.size()) continue;
            ++rep.matched;
            TrackMeta& m = tr[it->second].meta;
            if (opt.keys && s.key >= 0 && !(opt.keepManual && m.keySrc == 5)) {
                if (m.keyAnalyzed && m.key != s.key) ++rep.keysChanged;
                m.key = s.key; m.keyAnalyzed = true; m.keySrc = 4;
                ++rep.keys;
            }
            bool gridDone = false;
            if (opt.grid && s.bpm > 30 && s.bpm < 300 && !(opt.keepManual && m.gridSrc == 3)) {
                const double beat = 60.0 / s.bpm, bar = 4 * beat;
                m.bpm = s.bpm;
                if (s.gridSec >= 0) { m.gridAnchorSec = std::fmod(s.gridSec, bar); if (m.gridAnchorSec < 0) m.gridAnchorSec += bar; }
                else if (!m.analyzed) m.gridAnchorSec = 0;
                m.analyzed = true;
                m.gridSrc = 4;  // al cargar la pista se mide el desfase real
                gridDone = true;
                ++rep.grids;
            }
            if (opt.cues) {
                for (size_t c = 0; c < s.cues.size(); ++c) {
                    const auto [n, pos] = s.cues[c];
                    const int i = n - 1;
                    if (m.hotCuesSec[i] >= 0) continue;  // no se pisa ningún hot cue propio
                    m.hotCuesSec[i] = pos;
                    if (c < s.cueNames.size() && m.hotCueNames[i].empty()) m.hotCueNames[i] = s.cueNames[c];
                    if (gridDone) m.importedCues |= (uint8_t)(1u << i);
                    ++rep.cues;
                }
            }
        }
        lib.markDirty();
    }
    prog(0.85f);
    if (opt.playlists) {
        const std::wstring home = std::filesystem::path(dbFile).parent_path().wstring();
        std::error_code ec;
        const std::filesystem::path pl = std::filesystem::path(home) / L"Playlists";
        std::vector<std::filesystem::path> files;
        if (std::filesystem::is_directory(pl, ec))
            for (auto it = std::filesystem::recursive_directory_iterator(pl, std::filesystem::directory_options::skip_permission_denied, ec);
                 !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                const std::wstring e = extensionLower(it->path().wstring());
                if (e == L".m3u" || e == L".m3u8") files.push_back(it->path());
                if (files.size() > 500) break;
            }
        for (auto& f : files) {
            if (cancel && cancel->load()) break;
            const std::string name = "VDJ · " + toUtf8(f.stem().wstring());
            const int old = lib.findPlaylist(name);
            if (old >= 0) lib.deletePlaylist(old);  // se vuelve a importar entera
            const int idx = lib.importM3u(f.wstring());
            if (idx >= 0) {
                std::lock_guard<std::recursive_mutex> lk(lib.mutex());
                lib.playlists()[idx].name = name;
                lib.markDirty();
                ++rep.playlists;
            }
        }
    }
    prog(1.f);
    return rep;
}

} // namespace rdj

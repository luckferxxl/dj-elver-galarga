#include "library.h"
#include "tags.h"
#include "util.h"
#include "../audio/analysis.h"
#include "../audio/loader.h"
#include <json.hpp>
#include <sqlite3.h>
#include <ctime>
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace rdj {

using json = nlohmann::json;

namespace {
bool fileStat(const std::wstring& p, uint64_t& size, int64_t& mtime) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a)) return false;
    if (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return false;
    size = ((uint64_t)a.nFileSizeHigh << 32) | a.nFileSizeLow;
    mtime = (int64_t)(((uint64_t)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime);
    return true;
}

void metaFromJson(const json& j, TrackMeta& m) {
    m.bpm = j.value("bpm", 0.0); m.gridAnchorSec = j.value("anchor", 0.0); m.analyzed = j.value("analyzed", false);
    m.gridSrc = m.analyzed ? 1 : 0;
    m.durationSec = j.value("dur", 0.0); m.cueSec = j.value("cue", 0.0);
    if (j.contains("hot") && j["hot"].is_array())
        for (size_t i = 0; i < j["hot"].size() && i < m.hotCuesSec.size(); ++i) m.hotCuesSec[i] = j["hot"][i].get<double>();
}
std::wstring jsonPath() { return appDataDir() + L"\\biblioteca.json"; }
int64_t unixNow() { return (int64_t)std::time(nullptr); }

// Pequeño envoltorio de sentencias SQLite.
struct Stmt {
    sqlite3_stmt* s = nullptr;
    Stmt(sqlite3* db, const char* sql) { sqlite3_prepare_v2(db, sql, -1, &s, nullptr); }
    ~Stmt() { if (s) sqlite3_finalize(s); }
    Stmt& text(int i, const std::string& v) { sqlite3_bind_text(s, i, v.c_str(), (int)v.size(), SQLITE_TRANSIENT); return *this; }
    Stmt& i64(int i, int64_t v) { sqlite3_bind_int64(s, i, v); return *this; }
    Stmt& dbl(int i, double v) { sqlite3_bind_double(s, i, v); return *this; }
    bool step() { return sqlite3_step(s) == SQLITE_ROW; }
    void run() { sqlite3_step(s); sqlite3_reset(s); sqlite3_clear_bindings(s); }
    std::string colText(int c) { const unsigned char* t = sqlite3_column_text(s, c); return t ? (const char*)t : ""; }
    int64_t colI64(int c) { return sqlite3_column_int64(s, c); }
    double colDbl(int c) { return sqlite3_column_double(s, c); }
};

const char* kSchema = R"SQL(
PRAGMA journal_mode=WAL;
CREATE TABLE IF NOT EXISTS info (k TEXT PRIMARY KEY, v TEXT);
CREATE TABLE IF NOT EXISTS folders (path TEXT PRIMARY KEY);
CREATE TABLE IF NOT EXISTS tracks (
  path TEXT PRIMARY KEY, title TEXT, artist TEXT, size INTEGER, mtime INTEGER, tag_bpm REAL,
  bpm REAL, anchor REAL, analyzed INTEGER, duration REAL, cue REAL, hotcues TEXT,
  musical_key INTEGER, key_analyzed INTEGER, play_count INTEGER, last_played INTEGER, first_seen INTEGER);
CREATE TABLE IF NOT EXISTS playlists (id INTEGER PRIMARY KEY, name TEXT, pos INTEGER);
CREATE TABLE IF NOT EXISTS playlist_items (playlist_id INTEGER, pos INTEGER, path TEXT);
CREATE INDEX IF NOT EXISTS idx_items ON playlist_items(playlist_id, pos);
CREATE TABLE IF NOT EXISTS track_extra (path TEXT PRIMARY KEY, genre TEXT, rating INTEGER, cue_names TEXT, tags_ver INTEGER);
)SQL";
// track_extra (0.7) va en una tabla aparte: así una versión anterior sigue pudiendo abrir y guardar la biblioteca.

std::string cueNamesToText(const TrackMeta& m) {
    std::string s;
    bool any = false;
    for (int i = 0; i < kNumHotCues; ++i) { if (i) s += '\x1f'; s += m.hotCueNames[i]; if (!m.hotCueNames[i].empty()) any = true; }
    return any ? s : std::string();
}
void cueNamesFromText(const std::string& s, TrackMeta& m) {
    size_t i = 0, st = 0;
    for (size_t k = 0; k <= s.size() && i < (size_t)kNumHotCues; ++k)
        if (k == s.size() || s[k] == '\x1f') { m.hotCueNames[i++] = s.substr(st, k - st); st = k + 1; }
}

std::string hotToText(const TrackMeta& m) {
    std::string s;
    for (double h : m.hotCuesSec) { char b[32]; std::snprintf(b, sizeof b, "%.6f;", h); s += b; }
    if (m.importedCues) s += "m" + std::to_string((int)m.importedCues) + ";";  // cues de VirtualDJ pendientes de ajustar
    return s;
}
void hotFromText(const std::string& s, TrackMeta& m) {
    size_t i = 0, st = 0;
    for (size_t k = 0; k <= s.size(); ++k)
        if (k == s.size() || s[k] == ';') {
            if (k > st) {
                if (s[st] == 'm') m.importedCues = (uint8_t)std::atoi(s.substr(st + 1, k - st - 1).c_str());
                else if (i < m.hotCuesSec.size()) m.hotCuesSec[i++] = std::atof(s.substr(st, k - st).c_str());
            }
            st = k + 1;
        }
}
} // namespace

Library::Library() {
    anTh_ = std::thread(&Library::analysisThread, this);
    brTh_ = std::thread(&Library::browseThread, this);
}

Library::~Library() {
    quit_ = true;
    anCancel_ = true;
    anCv_.notify_all();
    { std::lock_guard<std::mutex> lk(brM_); }
    brCv_.notify_all();
    if (brTh_.joinable()) brTh_.join();
    if (scanTh_.joinable()) scanTh_.join();
    if (tagTh_.joinable()) tagTh_.join();
    if (anTh_.joinable()) anTh_.join();
    save();
}

LibTrack Library::makeTrack(const std::wstring& path) {
    LibTrack t;
    t.path = path;
    fileStat(path, t.size, t.mtime);
    TagInfo tg = readTags(path);
    t.title = tg.title; t.artist = tg.artist; t.tagBpm = tg.bpm;
    t.genre = tg.genre; t.rating = tg.rating; t.tagsV2 = true;
    if (tg.key >= 0 && useTagKeys_) { t.meta.key = tg.key; t.meta.keyAnalyzed = true; t.meta.keySrc = 3; }
    t.fileName = toUtf8(fileNameOf(path));
    t.searchKey = foldForSearch(t.artist + " " + t.title + " " + t.fileName + " " + t.genre);
    t.firstSeen = unixNow();
    return t;
}

void Library::insertLocked(LibTrack t) {
    auto it = index_.find(t.path);
    if (it != index_.end()) {
        LibTrack& old = tracks_[it->second];
        // Si el archivo cambió, el análisis anterior ya no vale.
        const bool changed = old.size != t.size || old.mtime != t.mtime;
        TrackMeta keep = old.meta;
        const int plays = old.playCount, rating = old.rating;
        const int64_t last = old.lastPlayed, first = old.firstSeen;
        old = std::move(t);
        if (!changed) old.meta = keep;
        else old.meta.hotCueNames = keep.hotCueNames;
        if (rating) old.rating = rating;   // la valoración puesta en Ramiro DJ se conserva
        old.playCount = plays; old.lastPlayed = last; old.firstSeen = first ? first : old.firstSeen;
        old.missing = false;
        return;
    }
    index_[t.path] = tracks_.size();
    tracks_.push_back(std::move(t));
}

LibTrack* Library::find(const std::wstring& path) {
    std::lock_guard<std::recursive_mutex> lk(m_);
    auto it = index_.find(path);
    return it == index_.end() ? nullptr : &tracks_[it->second];
}

std::wstring Library::dbFile() const { return appDataDir() + L"\\biblioteca.db"; }

bool Library::loadJson() {
    std::ifstream f(std::filesystem::path(jsonPath()), std::ios::binary);
    if (!f) return false;
    json j;
    try { f >> j; } catch (...) { return false; }
    std::lock_guard<std::recursive_mutex> lk(m_);
    tracks_.clear(); index_.clear(); folders_.clear(); playlists_.clear();
    try {
        for (auto& fo : j.value("folders", json::array())) folders_.push_back(fromUtf8(fo.get<std::string>()));
        for (auto& jt : j.value("tracks", json::array())) {
            LibTrack t;
            t.path = fromUtf8(jt.value("path", ""));
            if (t.path.empty()) continue;
            t.title = jt.value("title", ""); t.artist = jt.value("artist", "");
            t.size = jt.value("size", (uint64_t)0); t.mtime = jt.value("mtime", (int64_t)0);
            t.tagBpm = jt.value("tagBpm", 0.0);
            if (jt.contains("meta")) metaFromJson(jt["meta"], t.meta);
            t.firstSeen = unixNow();
            index_[t.path] = tracks_.size();
            tracks_.push_back(std::move(t));
        }
        for (auto& jp : j.value("playlists", json::array())) {
            Playlist p;
            p.name = jp.value("name", "Lista");
            for (auto& x : jp.value("paths", json::array())) p.paths.push_back(fromUtf8(x.get<std::string>()));
            playlists_.push_back(std::move(p));
        }
    } catch (...) {
    }
    return true;
}

void Library::load() {
    const bool hasDb = GetFileAttributesW(dbFile().c_str()) != INVALID_FILE_ATTRIBUTES;
    if (!hasDb) {
        // Migración desde la versión 0.1/0.2 (biblioteca.json).
        if (loadJson()) {
            dirty_ = true;
            save();
            MoveFileExW(jsonPath().c_str(), (jsonPath() + L".migrado").c_str(), MOVEFILE_REPLACE_EXISTING);
        }
        finishLoad();
        return;
    }
    sqlite3* db = nullptr;
    if (sqlite3_open16(dbFile().c_str(), &db) != SQLITE_OK) { if (db) sqlite3_close(db); return; }
    sqlite3_exec(db, kSchema, nullptr, nullptr, nullptr);
    std::lock_guard<std::recursive_mutex> lk(m_);
    tracks_.clear(); index_.clear(); folders_.clear(); playlists_.clear();
    {
        Stmt q(db, "SELECT path FROM folders");
        while (q.step()) folders_.push_back(fromUtf8(q.colText(0)));
    }
    {
        Stmt q(db, "SELECT path,title,artist,size,mtime,tag_bpm,bpm,anchor,analyzed,duration,cue,hotcues,musical_key,key_analyzed,"
                   "play_count,last_played,first_seen FROM tracks");
        while (q.step()) {
            LibTrack t;
            t.path = fromUtf8(q.colText(0));
            t.title = q.colText(1); t.artist = q.colText(2);
            t.size = (uint64_t)q.colI64(3); t.mtime = q.colI64(4); t.tagBpm = q.colDbl(5);
            t.meta.bpm = q.colDbl(6); t.meta.gridAnchorSec = q.colDbl(7); t.meta.analyzed = q.colI64(8) != 0;
            // La columna "analyzed" guarda también el origen de la rejilla (1 = hasta la 0.6, 2 = precisa, 3 = a mano).
            t.meta.gridSrc = t.meta.analyzed ? (int)std::clamp<int64_t>(q.colI64(8), 1, 8) : 0;
            t.meta.durationSec = q.colDbl(9); t.meta.cueSec = q.colDbl(10); hotFromText(q.colText(11), t.meta);
            // key_analyzed: 0/1 = sin analizar o método antiguo (se reanaliza); 2.. = origen de la tonalidad + 1 (ver TrackMeta::keySrc).
            t.meta.key = (int)q.colI64(12); t.meta.keyAnalyzed = q.colI64(13) >= 2;
            t.meta.keySrc = t.meta.keyAnalyzed ? (int)std::clamp<int64_t>(q.colI64(13) - 1, 1, 5) : 0;
            t.playCount = (int)q.colI64(14); t.lastPlayed = q.colI64(15); t.firstSeen = q.colI64(16);
            index_[t.path] = tracks_.size();
            tracks_.push_back(std::move(t));
        }
    }
    {
        Stmt q(db, "SELECT path,genre,rating,cue_names,tags_ver FROM track_extra");
        while (q.step()) {
            auto it = index_.find(fromUtf8(q.colText(0)));
            if (it == index_.end()) continue;
            LibTrack& t = tracks_[it->second];
            t.genre = q.colText(1);
            t.rating = (int)std::clamp<int64_t>(q.colI64(2), 0, 5);
            cueNamesFromText(q.colText(3), t.meta);
            t.tagsV2 = q.colI64(4) >= 1;
        }
    }
    {
        Stmt q(db, "SELECT id,name FROM playlists ORDER BY pos");
        std::vector<std::pair<int64_t, std::string>> ids;
        while (q.step()) ids.push_back({q.colI64(0), q.colText(1)});
        for (auto& [id, name] : ids) {
            Playlist p;
            p.name = name;
            Stmt it(db, "SELECT path FROM playlist_items WHERE playlist_id=? ORDER BY pos");
            it.i64(1, id);
            while (it.step()) p.paths.push_back(fromUtf8(it.colText(0)));
            playlists_.push_back(std::move(p));
        }
    }
    sqlite3_close(db);
    finishLoad();
}

void Library::finishLoad() {
    std::lock_guard<std::recursive_mutex> lk(m_);
    for (auto& t : tracks_) {
        t.fileName = toUtf8(fileNameOf(t.path));
        t.searchKey = foldForSearch(t.artist + " " + t.title + " " + t.fileName + " " + t.genre);
        uint64_t s; int64_t m;
        t.missing = !fileStat(t.path, s, m);
    }
    version_++;
}

void Library::save() {
    if (!dirty_) return;
    // Copia de seguridad diaria de la base de datos (se guardan 7).
    const std::wstring bdir = appDataDir() + L"\\copias";
    CreateDirectoryW(bdir.c_str(), nullptr);
    {
        std::time_t tt = std::time(nullptr);
        std::tm lt{};
        localtime_s(&lt, &tt);
        wchar_t name[64];
        wcsftime(name, 64, L"biblioteca-%Y-%m-%d.db", &lt);
        const std::wstring dst = bdir + L"\\" + name;
        if (GetFileAttributesW(dbFile().c_str()) != INVALID_FILE_ATTRIBUTES && GetFileAttributesW(dst.c_str()) == INVALID_FILE_ATTRIBUTES) {
            CopyFileW(dbFile().c_str(), dst.c_str(), TRUE);
            std::vector<std::wstring> all;
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW((bdir + L"\\biblioteca-*.db").c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) { do all.push_back(fd.cFileName); while (FindNextFileW(h, &fd)); FindClose(h); }
            std::sort(all.begin(), all.end());
            for (size_t i = 0; i + 7 < all.size(); ++i) DeleteFileW((bdir + L"\\" + all[i]).c_str());
        }
    }
    // Copia instantánea bajo el mutex; la escritura a disco va fuera de él.
    std::vector<LibTrack> tracks;
    std::vector<std::wstring> folders;
    std::vector<Playlist> pls;
    {
        std::lock_guard<std::recursive_mutex> lk(m_);
        tracks = tracks_; folders = folders_; pls = playlists_;
        dirty_ = false;
    }
    sqlite3* db = nullptr;
    if (sqlite3_open16(dbFile().c_str(), &db) != SQLITE_OK) { if (db) sqlite3_close(db); dirty_ = true; return; }
    sqlite3_busy_timeout(db, 2000);
    sqlite3_exec(db, kSchema, nullptr, nullptr, nullptr);
    sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "DELETE FROM folders; DELETE FROM tracks; DELETE FROM playlists; DELETE FROM playlist_items;", nullptr, nullptr, nullptr);
    {
        Stmt q(db, "INSERT INTO folders(path) VALUES(?)");
        for (auto& f : folders) q.text(1, toUtf8(f)).run();
    }
    {
        Stmt q(db, "INSERT OR REPLACE INTO tracks VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
        for (auto& t : tracks) {
            q.text(1, toUtf8(t.path)).text(2, t.title).text(3, t.artist).i64(4, (int64_t)t.size).i64(5, t.mtime).dbl(6, t.tagBpm)
                .dbl(7, t.meta.bpm).dbl(8, t.meta.gridAnchorSec).i64(9, t.meta.analyzed ? std::clamp(t.meta.gridSrc, 1, 8) : 0).dbl(10, t.meta.durationSec).dbl(11, t.meta.cueSec)
                .text(12, hotToText(t.meta)).i64(13, t.meta.key).i64(14, t.meta.keyAnalyzed ? std::clamp(t.meta.keySrc, 1, 5) + 1 : 0).i64(15, t.playCount).i64(16, t.lastPlayed)
                .i64(17, t.firstSeen)
                .run();
        }
    }
    sqlite3_exec(db, "DELETE FROM track_extra;", nullptr, nullptr, nullptr);
    {
        Stmt q(db, "INSERT OR REPLACE INTO track_extra(path,genre,rating,cue_names,tags_ver) VALUES(?,?,?,?,?)");
        for (auto& t : tracks) {
            const std::string names = cueNamesToText(t.meta);
            if (t.genre.empty() && !t.rating && names.empty() && !t.tagsV2) continue;
            q.text(1, toUtf8(t.path)).text(2, t.genre).i64(3, t.rating).text(4, names).i64(5, t.tagsV2 ? 1 : 0).run();
        }
    }
    {
        Stmt qp(db, "INSERT INTO playlists(id,name,pos) VALUES(?,?,?)");
        Stmt qi(db, "INSERT INTO playlist_items(playlist_id,pos,path) VALUES(?,?,?)");
        for (size_t i = 0; i < pls.size(); ++i) {
            qp.i64(1, (int64_t)i + 1).text(2, pls[i].name).i64(3, (int64_t)i).run();
            for (size_t k = 0; k < pls[i].paths.size(); ++k) qi.i64(1, (int64_t)i + 1).i64(2, (int64_t)k).text(3, toUtf8(pls[i].paths[k])).run();
        }
    }
    sqlite3_exec(db, "INSERT OR REPLACE INTO info VALUES('version','3')", nullptr, nullptr, nullptr);
    if (sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        dirty_ = true;
    }
    sqlite3_close(db);
}

void Library::setRating(const std::wstring& path, int stars) {
    std::lock_guard<std::recursive_mutex> lk(m_);
    auto it = index_.find(path);
    if (it == index_.end()) return;
    tracks_[it->second].rating = std::clamp(stars, 0, 5);
    markDirty();
}

void Library::setHotCueName(const std::wstring& path, int cue, const std::string& name) {
    if (cue < 0 || cue >= kNumHotCues) return;
    std::lock_guard<std::recursive_mutex> lk(m_);
    auto it = index_.find(path);
    if (it == index_.end()) return;
    tracks_[it->second].meta.hotCueNames[cue] = name.substr(0, 40);
    markDirty();
}

void Library::notePlayed(const std::wstring& path) {
    std::lock_guard<std::recursive_mutex> lk(m_);
    auto it = index_.find(path);
    if (it == index_.end()) return;
    tracks_[it->second].playCount++;
    tracks_[it->second].lastPlayed = unixNow();
    markDirty();
}

LibraryStats Library::stats() {
    std::lock_guard<std::recursive_mutex> lk(m_);
    LibraryStats st;
    st.tracks = tracks_.size();
    std::vector<std::pair<int, size_t>> played;
    for (size_t i = 0; i < tracks_.size(); ++i) {
        const LibTrack& t = tracks_[i];
        if (t.missing) ++st.missing;
        if (t.meta.bpm > 0) {
            ++st.analyzed;
            st.bpmHist[std::clamp((int)(t.meta.bpm - 60) / 10, 0, 12)]++;
        }
        if (t.meta.key >= 0) { ++st.withKey; st.keyHist[t.meta.key]++; }
        st.totalSec += t.meta.durationSec;
        if (t.playCount > 0) { ++st.played; played.push_back({t.playCount, i}); }
    }
    std::sort(played.rbegin(), played.rend());
    for (size_t i = 0; i < played.size() && i < 10; ++i) st.topPlayed.push_back({tracks_[played[i].second].path, played[i].first});
    return st;
}

std::vector<std::wstring> Library::addFiles(const std::vector<std::wstring>& paths) {
    std::vector<std::wstring> ok;
    std::vector<LibTrack> batch;
    for (auto& p : paths) {
        if (!isSupportedAudioExt(extensionLower(p))) continue;
        uint64_t s; int64_t m;
        if (!fileStat(p, s, m)) continue;
        batch.push_back(makeTrack(p));
        ok.push_back(p);
    }
    std::lock_guard<std::recursive_mutex> lk(m_);
    for (auto& t : batch) insertLocked(std::move(t));
    if (!batch.empty()) markDirty();
    return ok;
}

void Library::addFolder(const std::wstring& folder) {
    {
        std::lock_guard<std::recursive_mutex> lk(m_);
        for (auto& f : folders_) if (_wcsicmp(f.c_str(), folder.c_str()) == 0) { rescan(); return; }
        folders_.push_back(folder);
        markDirty();
    }
    rescan();
}

void Library::removeFolder(size_t i) {
    std::lock_guard<std::recursive_mutex> lk(m_);
    if (i >= folders_.size()) return;
    const std::wstring root = folders_[i] + L"\\";
    folders_.erase(folders_.begin() + i);
    // Quitar las pistas de esa carpeta que no estén en ninguna lista.
    std::vector<LibTrack> keep;
    for (auto& t : tracks_) {
        bool inside = _wcsnicmp(t.path.c_str(), root.c_str(), root.size()) == 0;
        bool inList = false;
        for (auto& p : playlists_) for (auto& x : p.paths) if (x == t.path) inList = true;
        if (!inside || inList) keep.push_back(std::move(t));
    }
    tracks_ = std::move(keep);
    index_.clear();
    for (size_t k = 0; k < tracks_.size(); ++k) index_[tracks_[k].path] = k;
    markDirty();
}

void Library::rescan() {
    if (scanning_) return;
    if (scanTh_.joinable()) scanTh_.join();
    std::vector<std::wstring> roots;
    { std::lock_guard<std::recursive_mutex> lk(m_); roots = folders_; }
    scanning_ = true;
    scanned_ = 0;
    scanTh_ = std::thread(&Library::scanThread, this, roots);
}

void Library::scanThread(std::vector<std::wstring> roots) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    std::vector<std::wstring> stack = roots;
    std::vector<LibTrack> batch;
    auto flush = [&] {
        std::lock_guard<std::recursive_mutex> lk(m_);
        for (auto& t : batch) insertLocked(std::move(t));
        batch.clear();
        markDirty();
    };
    while (!stack.empty() && !quit_) {
        const std::wstring dir = stack.back();
        stack.pop_back();
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            const std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            const std::wstring full = dir + L"\\" + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) stack.push_back(full);
                continue;
            }
            if (!isSupportedAudioExt(extensionLower(name))) continue;
            // Saltar archivos ya conocidos y sin cambios.
            const uint64_t size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            const int64_t mt = (int64_t)(((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime);
            {
                std::lock_guard<std::recursive_mutex> lk(m_);
                auto it = index_.find(full);
                if (it != index_.end() && tracks_[it->second].size == size && tracks_[it->second].mtime == mt) {
                    tracks_[it->second].missing = false;
                    ++scanned_;
                    continue;
                }
            }
            batch.push_back(makeTrack(full));
            ++scanned_;
            if (batch.size() >= 200) flush();
        } while (!quit_ && FindNextFileW(h, &fd));
        FindClose(h);
    }
    flush();
    scanning_ = false;
}

void Library::browseFolder(const std::wstring& dir, bool recursive) {
    if (dir.empty()) return;
    {
        std::lock_guard<std::mutex> lk(brM_);
        for (auto& q : brQueue_) if (q.first == dir && q.second == recursive) return;
        brQueue_.push_back({dir, recursive});
        ++browsing_;
    }
    brCv_.notify_all();
}

void Library::browseThread() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    for (;;) {
        std::pair<std::wstring, bool> job;
        {
            std::unique_lock<std::mutex> lk(brM_);
            brCv_.wait(lk, [&] { return quit_ || !brQueue_.empty(); });
            if (quit_) return;
            job = brQueue_.front();
            brQueue_.pop_front();
        }
        std::wstring root = job.first;
        while (root.size() > 3 && (root.back() == L'\\' || root.back() == L'/')) root.pop_back();
        std::vector<std::wstring> stack{root};
        std::vector<LibTrack> batch;
        int files = 0;
        auto flush = [&] {
            std::lock_guard<std::recursive_mutex> lk(m_);
            for (auto& t : batch) insertLocked(std::move(t));
            batch.clear();
            markDirty();
        };
        while (!stack.empty() && !quit_ && files < 20000) {
            const std::wstring dir = stack.back();
            stack.pop_back();
            const std::wstring base = dir.back() == L'\\' ? dir : dir + L"\\";
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileExW((base + L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
            if (h == INVALID_HANDLE_VALUE) continue;
            do {
                const std::wstring name = fd.cFileName;
                if (name == L"." || name == L"..") continue;
                const std::wstring full = base + name;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    if (job.second && !(fd.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))) stack.push_back(full);
                    continue;
                }
                if (!isSupportedAudioExt(extensionLower(name))) continue;
                ++files;
                const uint64_t size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                const int64_t mt = (int64_t)(((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime);
                {
                    std::lock_guard<std::recursive_mutex> lk(m_);
                    auto it = index_.find(full);
                    if (it != index_.end() && tracks_[it->second].size == size && tracks_[it->second].mtime == mt) {
                        if (tracks_[it->second].missing) { tracks_[it->second].missing = false; markDirty(); }
                        continue;
                    }
                }
                batch.push_back(makeTrack(full));
                if (batch.size() >= 100) flush();
            } while (!quit_ && FindNextFileW(h, &fd));
            FindClose(h);
        }
        if (!batch.empty()) flush();
        --browsing_;
    }
}

std::vector<size_t> Library::inFolder(const std::wstring& dir, bool recursive) {
    std::vector<size_t> out;
    if (dir.empty()) return out;
    std::wstring d = dir;
    if (d.back() != L'\\') d += L'\\';
    std::lock_guard<std::recursive_mutex> lk(m_);
    for (size_t i = 0; i < tracks_.size(); ++i) {
        const std::wstring& p = tracks_[i].path;
        if (p.size() <= d.size() || _wcsnicmp(p.c_str(), d.c_str(), d.size()) != 0) continue;
        if (!recursive && p.find(L'\\', d.size()) != std::wstring::npos) continue;
        out.push_back(i);
    }
    return out;
}

int Library::findPlaylist(const std::string& name) {
    std::lock_guard<std::recursive_mutex> lk(m_);
    for (size_t i = 0; i < playlists_.size(); ++i) if (playlists_[i].name == name) return (int)i;
    return -1;
}

void Library::updateMeta(const std::wstring& path, const TrackMeta& meta) {
    std::lock_guard<std::recursive_mutex> lk(m_);
    auto it = index_.find(path);
    if (it == index_.end()) return;
    tracks_[it->second].meta = meta;
    markDirty();
}

bool Library::metaFor(const std::wstring& path, TrackMeta& out) {
    std::lock_guard<std::recursive_mutex> lk(m_);
    auto it = index_.find(path);
    if (it == index_.end()) return false;
    out = tracks_[it->second].meta;
    return true;
}

std::vector<size_t> Library::query(const std::string& text, int list) {
    std::lock_guard<std::recursive_mutex> lk(m_);
    const auto terms = searchTerms(text);
    auto match = [&](const LibTrack& t) {
        for (auto& w : terms) if (t.searchKey.find(w) == std::string::npos) return false;
        return true;
    };
    std::vector<size_t> out;
    if (list >= 0 && list < (int)playlists_.size()) {
        for (auto& p : playlists_[list].paths) {
            auto it = index_.find(p);
            if (it != index_.end() && match(tracks_[it->second])) out.push_back(it->second);
        }
    } else {
        for (size_t i = 0; i < tracks_.size(); ++i) if (match(tracks_[i])) out.push_back(i);
    }
    return out;
}

int Library::createPlaylist(const std::string& name) {
    std::lock_guard<std::recursive_mutex> lk(m_);
    playlists_.push_back(Playlist{name, {}});
    markDirty();
    return (int)playlists_.size() - 1;
}

void Library::deletePlaylist(int i) {
    std::lock_guard<std::recursive_mutex> lk(m_);
    if (i < 0 || i >= (int)playlists_.size()) return;
    playlists_.erase(playlists_.begin() + i);
    markDirty();
}

void Library::addToPlaylist(int i, const std::vector<std::wstring>& paths) {
    std::lock_guard<std::recursive_mutex> lk(m_);
    if (i < 0 || i >= (int)playlists_.size()) return;
    for (auto& p : paths) playlists_[i].paths.push_back(p);
    markDirty();
}

bool Library::exportM3u(int i, const std::wstring& file) {
    std::lock_guard<std::recursive_mutex> lk(m_);
    if (i < 0 || i >= (int)playlists_.size()) return false;
    std::ofstream f(std::filesystem::path(file), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << "\xEF\xBB\xBF#EXTM3U\r\n";
    for (auto& p : playlists_[i].paths) {
        auto it = index_.find(p);
        if (it != index_.end()) {
            const LibTrack& t = tracks_[it->second];
            f << "#EXTINF:" << (int)t.meta.durationSec << "," << t.artist << (t.artist.empty() ? "" : " - ") << t.title << "\r\n";
        }
        f << toUtf8(p) << "\r\n";
    }
    return (bool)f;
}

int Library::importM3u(const std::wstring& file) {
    std::ifstream f(std::filesystem::path(file), std::ios::binary);
    if (!f) return -1;
    std::wstring base = file.substr(0, file.find_last_of(L"\\/") + 1);
    std::vector<std::wstring> paths;
    std::string line;
    const bool m3u8 = extensionLower(file) == L".m3u8";
    auto validUtf8 = [](const std::string& x) {
        for (size_t i = 0; i < x.size();) {
            const unsigned char c = (unsigned char)x[i];
            const int n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
            if (n < 0 || i + (size_t)n >= x.size() + (n == 0 ? 1 : 0)) return false;
            for (int k = 1; k <= n; ++k) if (((unsigned char)x[i + k] >> 6) != 2) return false;
            i += (size_t)n + 1;
        }
        return true;
    };
    while (std::getline(f, line)) {
        if (line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF) line = line.substr(3);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::wstring p;
        // UTF-8 si es .m3u8 o si los bytes son UTF-8 válido (VirtualDJ y otros lo usan también en .m3u).
        if (m3u8 || validUtf8(line)) p = fromUtf8(line);
        else { const int n = MultiByteToWideChar(CP_ACP, 0, line.data(), (int)line.size(), nullptr, 0); p.resize(n); MultiByteToWideChar(CP_ACP, 0, line.data(), (int)line.size(), p.data(), n); }
        if (p.size() < 2 || (p[1] != L':' && p.rfind(L"\\\\", 0) != 0)) p = base + p;
        for (auto& c : p) if (c == L'/') c = L'\\';
        paths.push_back(p);
    }
    auto added = addFiles(paths);
    std::wstring name = fileNameOf(file);
    name = name.substr(0, name.find_last_of(L'.'));
    const int idx = createPlaylist(toUtf8(name));
    addToPlaylist(idx, added);
    return idx;
}

void Library::checkTagKeys(bool includeDetected) {
    if (tagTh_.joinable()) { if (tagKeysPending_ > 0) return; tagTh_.join(); }
    std::vector<std::wstring> todo;
    {
        std::lock_guard<std::recursive_mutex> lk(m_);
        for (auto& t : tracks_)
            if (!t.missing && ((t.meta.keyAnalyzed && (t.meta.keySrc == 1 || (includeDetected && t.meta.keySrc == 2))) || !t.tagsV2)) todo.push_back(t.path);
    }
    if (todo.empty()) return;
    tagKeysPending_ = (int)todo.size();
    tagTh_ = std::thread([this, todo] {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
        for (auto& p : todo) {
            if (quit_) break;
            const TagInfo tg = readTags(p);
            std::lock_guard<std::recursive_mutex> lk(m_);
            auto it = index_.find(p);
            if (it != index_.end()) {
                LibTrack& lt = tracks_[it->second];
                if (!lt.tagsV2) {
                    // Género y valoración de las pistas que entraron antes de la 0.7.
                    if (lt.genre.empty()) lt.genre = tg.genre;
                    if (!lt.rating) lt.rating = tg.rating;
                    lt.tagsV2 = true;
                    lt.searchKey = foldForSearch(lt.artist + " " + lt.title + " " + lt.fileName + " " + lt.genre);
                    markDirty();
                }
                TrackMeta& m = lt.meta;
                if (m.keyAnalyzed && (m.keySrc == 1 || m.keySrc == 2)) {
                    if (tg.key >= 0 && useTagKeys_) { if (m.key != tg.key || m.keySrc != 3) { m.key = tg.key; m.keySrc = 3; markDirty(); } }
                    else if (m.keySrc == 1) { m.keySrc = 2; markDirty(); }
                }
            }
            --tagKeysPending_;
        }
        tagKeysPending_ = 0;
    });
}

void Library::analyzeMissing(double minBpm, double maxBpm) {
    std::lock_guard<std::mutex> lk(anM_);
    anMin_ = minBpm; anMax_ = maxBpm;
    anCancel_ = false;
    std::lock_guard<std::recursive_mutex> lk2(m_);
    for (auto& t : tracks_)
        if ((!t.meta.analyzed || !t.meta.keyAnalyzed) && !t.missing && std::find(anQueue_.begin(), anQueue_.end(), t.path) == anQueue_.end())
            anQueue_.push_back(t.path);
    analysisPending_ = (int)anQueue_.size();
    anCv_.notify_all();
}

void Library::cancelAnalysis() {
    std::lock_guard<std::mutex> lk(anM_);
    anQueue_.clear();
    anCancel_ = true;
    analysisPending_ = 0;
}

void Library::analysisThread() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    for (;;) {
        std::wstring path;
        double mn, mx;
        {
            std::unique_lock<std::mutex> lk(anM_);
            anCv_.wait(lk, [&] { return quit_ || !anQueue_.empty(); });
            if (quit_) return;
            path = anQueue_.front();
            anQueue_.pop_front();
            mn = anMin_; mx = anMax_;
            anCancel_ = false;
        }
        std::string err;
        TrackPtr t = decodeFile(path, err, nullptr, &anCancel_);
        TrackMeta meta;
        const bool have = metaFor(path, meta);
        if (t && have && (!meta.analyzed || !meta.keyAnalyzed)) {
            BeatInfo bi;
            KeyInfo ki;
            if (!meta.analyzed) bi = detectBeats(t->pcm.data(), t->frames, t->sampleRate, mn, mx, &anCancel_);
            if (!meta.keyAnalyzed && !anCancel_) ki = detectKey(t->pcm.data(), t->frames, t->sampleRate, &anCancel_);
            if (!anCancel_) {
                // Releer: la interfaz pudo modificar cues mientras tanto.
                metaFor(path, meta);
                if (!meta.analyzed) { meta.bpm = bi.bpm; meta.gridAnchorSec = bi.anchorSec; meta.analyzed = true; meta.gridSrc = 6; }
                if (!meta.keyAnalyzed) { meta.key = ki.key; meta.keyAnalyzed = true; meta.keySrc = 2; }
                meta.durationSec = t->lengthSec();
                updateMeta(path, meta);
            }
        } else if (!t && have) {
            // Archivo ilegible: marcar como analizado para no reintentar en bucle.
            meta.analyzed = true;
            meta.keyAnalyzed = true;
            updateMeta(path, meta);
        }
        analysisPending_ = std::max(0, analysisPending_.load() - 1);
    }
}

} // namespace rdj

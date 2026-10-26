// Biblioteca musical: pistas, carpetas vigiladas, listas y caché de análisis.
// Se guarda en %APPDATA%\RamiroDJ\biblioteca.json. Los hilos de escaneo y de
// análisis comparten los datos con la interfaz mediante mutex().
#pragma once
#include "../audio/track.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rdj {

struct LibTrack {
    std::wstring path;
    std::string title, artist, fileName;  // UTF-8
    std::string searchKey;                // normalizado
    uint64_t size = 0;
    int64_t mtime = 0;
    double tagBpm = 0;
    TrackMeta meta;
    bool missing = false;
    std::string genre;        // de la etiqueta (TCON / GENRE)
    int rating = 0;           // 0..5 estrellas (de la etiqueta o puestas en Ramiro DJ)
    bool tagsV2 = false;      // género y valoración ya leídos de la etiqueta (0.7)
    // Estadísticas
    int playCount = 0;
    int64_t lastPlayed = 0;   // segundos Unix; 0 = nunca
    int64_t firstSeen = 0;    // cuándo entró en la biblioteca
};

struct LibraryStats {
    size_t tracks = 0, analyzed = 0, withKey = 0, missing = 0, played = 0;
    double totalSec = 0;
    int bpmHist[13] = {};     // 60-69, 70-79 ... 180+ BPM
    int keyHist[24] = {};
    std::vector<std::pair<std::wstring, int>> topPlayed;  // (ruta, reproducciones)
};

struct Playlist {
    std::string name;
    std::vector<std::wstring> paths;
};

class Library {
public:
    Library();
    ~Library();
    void load();
    void save();
    bool reloadExternalChanges();                   // escribe si hay cambios
    std::recursive_mutex& mutex() { return m_; }

    // Requieren mutex() tomado por el llamador si se usan desde la interfaz
    // mientras otros hilos trabajan.
    std::vector<LibTrack>& tracks() { return tracks_; }
    std::vector<Playlist>& playlists() { return playlists_; }
    std::vector<std::wstring>& folders() { return folders_; }
    LibTrack* find(const std::wstring& path);
    uint64_t version() const { return version_.load(); }

    // Añade archivos sueltos (lee etiquetas en el acto). Devuelve los admitidos.
    std::vector<std::wstring> addFiles(const std::vector<std::wstring>& paths);
    void addFolder(const std::wstring& folder);     // escanea en segundo plano
    void removeFolder(size_t i);
    void rescan();
    void scanPcMusic(bool purgeUnfound = true);     // auto-descubre música del PC, relocaliza pistas movidas y limpia borradas
    int relocateAndPurgeMissing(bool purgeUnfound = true); // fusiona pistas movidas con sus nuevas rutas y purga no encontradas
    int purgeMissingTracks() { return relocateAndPurgeMissing(true); }
    bool scanning() const { return scanning_.load(); }
    int scannedCount() const { return scanned_.load(); }

    // Navegador de archivos (como VirtualDJ): lee una carpeta cualquiera en
    // segundo plano y sus pistas pasan a la colección. Sin subcarpetas salvo
    // que se pida (máximo 20 000 archivos).
    void browseFolder(const std::wstring& dir, bool recursive);
    bool browsing() const { return browsing_.load() > 0; }
    // Pistas de la colección que están en esa carpeta (o debajo, si recursive).
    std::vector<size_t> inFolder(const std::wstring& dir, bool recursive);
    int findPlaylist(const std::string& name);  // -1 si no existe

    void updateMeta(const std::wstring& path, const TrackMeta& meta);
    bool metaFor(const std::wstring& path, TrackMeta& out);

    // Búsqueda: list = -1 toda la biblioteca, >=0 una lista. Devuelve índices de tracks().
    std::vector<size_t> query(const std::string& text, int list);

    // Listas
    int createPlaylist(const std::string& name);
    void deletePlaylist(int i);
    void addToPlaylist(int i, const std::vector<std::wstring>& paths);
    bool exportM3u(int i, const std::wstring& file);
    int importM3u(const std::wstring& file);

    // Análisis por lotes en segundo plano (prioridad baja).
    void analyzeMissing(double minBpm, double maxBpm);
    int analysisPending() const { return analysisPending_.load(); }
    void cancelAnalysis();

    void markDirty() { dirty_ = true; version_++; }
    // Tonalidad de las etiquetas (TKEY / INITIALKEY): si está, manda sobre la detectada.
    void setUseTagKeys(bool on) { useTagKeys_ = on; }
    // Repasa en segundo plano las pistas con tonalidad detectada que aún no se
    // han mirado en la etiqueta. Devuelve cuántas cambian (al terminar).
    // Lee en segundo plano la tonalidad de las etiquetas (TKEY / INITIALKEY) de las pistas
    // detectadas sin mirarla; con includeDetected, también de las detectadas porque no la traían.
    void checkTagKeys(bool includeDetected = false);
    int tagKeysChecking() const { return tagKeysPending_.load(); }
    void notePlayed(const std::wstring& path);   // suma una reproducción
    void setRating(const std::wstring& path, int stars);   // 0..5 (solo en la biblioteca: el archivo no se toca)
    void setHotCueName(const std::wstring& path, int cue, const std::string& name);
    LibraryStats stats();
    std::wstring dbFile() const;

private:
    void scanThread(std::vector<std::wstring> roots, bool purgeAfter = false);
    void browseThread();
    void analysisThread();
    LibTrack makeTrack(const std::wstring& path);
    void insertLocked(LibTrack t);
    bool loadJson();     // migración desde biblioteca.json (versiones 0.1/0.2)
    void finishLoad();

    std::recursive_mutex m_;
    std::vector<LibTrack> tracks_;
    std::unordered_map<std::wstring, size_t> index_;
    std::vector<std::wstring> folders_;
    std::vector<Playlist> playlists_;
    std::atomic<bool> dirty_{false};
    std::atomic<uint64_t> version_{1};

    std::thread scanTh_;
    std::atomic<bool> scanning_{false}, quit_{false};
    std::atomic<int> scanned_{0};

    std::thread brTh_;
    std::mutex brM_;
    std::condition_variable brCv_;
    std::deque<std::pair<std::wstring, bool>> brQueue_;
    std::atomic<int> browsing_{0};

    std::atomic<bool> useTagKeys_{true};
    std::thread tagTh_;
    std::atomic<int> tagKeysPending_{0};
    std::thread anTh_;
    std::mutex anM_;
    std::condition_variable anCv_;
    std::deque<std::wstring> anQueue_;
    std::atomic<int> analysisPending_{0};
    std::atomic<bool> anCancel_{false};
    double anMin_ = 70, anMax_ = 180;
};

} // namespace rdj

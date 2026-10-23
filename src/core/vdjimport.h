// Importación de la biblioteca de VirtualDJ (database.xml y sus listas .m3u):
// tonalidad, BPM y rejilla, hot cues y listas. Solo se lee: VirtualDJ no se toca.
#pragma once
#include <atomic>
#include <string>
#include <utility>
#include <vector>

namespace rdj {

class Library;

struct VdjSong {
    std::wstring path;
    std::string title, artist;
    int key = -1;                 // como en analysis.h
    double bpm = 0;               // pulsos por minuto
    double gridSec = -1;          // primer pulso de la rejilla (s); <0 = no hay
    std::vector<std::pair<int, double>> cues;  // (número 1..8, segundos)
    std::vector<std::string> cueNames;         // nombre de cada cue (paralelo a cues; "" si no tiene)
};

struct VdjDatabase {
    std::wstring file;            // ruta del database.xml
    std::wstring home;            // carpeta de VirtualDJ (la del database.xml)
    size_t songs = 0;             // pistas que contiene (tras leerlo)
};

// database.xml que hay en este equipo: Documentos\VirtualDJ, %LOCALAPPDATA%,
// %APPDATA% y la raíz de cada unidad (X:\VirtualDJ, discos externos).
std::vector<VdjDatabase> findVdjDatabases();

// Lee un database.xml. Las rutas relativas (bases de datos de discos externos)
// se completan con la unidad del propio archivo.
bool parseVdjDatabase(const std::wstring& file, std::vector<VdjSong>& out, std::string& err, const std::atomic<bool>* cancel = nullptr);

// Consulta rápida de la rejilla de VirtualDJ por ruta, para usarla al cargar una pista
// (sin importar nada). Lee los database.xml de este equipo la primera vez y vuelve a
// leerlos si VirtualDJ los ha guardado de nuevo (como mucho una comprobación cada 30 s).
// Solo para un hilo (el del cargador).
class VdjLookup {
public:
    bool find(const std::wstring& path, VdjSong& out);
private:
    void refresh();
    struct Db { std::wstring file; unsigned long long stamp = 0; };
    std::vector<Db> dbs_;
    std::vector<VdjSong> songs_;
    std::vector<std::pair<std::wstring, size_t>> index_;   // ruta en minúsculas → canción (ordenado)
    unsigned long long checkedAt_ = 0;
    bool loaded_ = false;
};

struct VdjImportOptions {
    bool keys = true;             // tonalidad de VirtualDJ
    bool grid = true;             // BPM y rejilla
    bool cues = true;             // hot cues (solo en huecos libres)
    bool playlists = true;        // listas de VirtualDJ\Playlists (*.m3u)
    bool addMissing = false;      // añadir a la colección las pistas que aún no están
    bool keepManual = true;       // no tocar lo ajustado a mano en Ramiro DJ
};

struct VdjImportReport {
    size_t songs = 0, matched = 0, added = 0, missingFiles = 0;
    size_t keys = 0, keysChanged = 0, grids = 0, cues = 0, playlists = 0;
    std::string error;
};

// Hace la importación (pensada para un hilo aparte). progress: 0..1.
VdjImportReport importVirtualDj(Library& lib, const std::wstring& dbFile, const VdjImportOptions& opt,
                                std::atomic<float>* progress = nullptr, const std::atomic<bool>* cancel = nullptr);

} // namespace rdj

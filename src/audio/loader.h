// Carga de pistas en un hilo de fondo: decodificación completa a memoria,
// forma de onda y análisis de tempo. La interfaz sondea los resultados.
#pragma once
#include "track.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <vector>
#include <mutex>
#include <string>
#include <thread>

namespace rdj {

// Decodifica un archivo completo (estéreo float, frecuencia original).
// Devuelve nullptr y rellena err (en español) si falla.
TrackPtr decodeFile(const std::wstring& path, std::string& err, std::atomic<float>* progress = nullptr,
                    const std::atomic<bool>* cancel = nullptr);

struct LoadJob {
    int deck = 0;
    uint32_t id = 0;
    std::wstring path;
    TrackMeta meta;          // metadatos conocidos (caché); si !analyzed se analiza
    std::string title, artist;
    double minBpm = 70, maxBpm = 180;
    bool useExternalGrid = true;   // usar la rejilla de VirtualDJ si la tiene (ver TrackLoader::externalGrid)
};

// Rejilla de otro programa (VirtualDJ) para una ruta: BPM y primer pulso en segundos.
struct ExternalGrid { double bpm = 0, gridSec = -1; };

// Desfases posibles entre el decodificador de otro programa y el nuestro (s), por formato:
// MP3 con cabecera LAME: 0 y ± el retardo del codificador que nosotros quitamos; MP3 sin ella:
// 0 y los retardos típicos (529 y 1105 muestras); otros formatos: solo 0.
std::vector<double> decoderOffsetCandidates(const std::wstring& path, double sampleRate);
// Elige el desfase: el candidato que la medida confirma (a menos de tol); si ninguno, 0
// (se confía en la rejilla del otro programa antes que en una medida dudosa).
double chooseDecoderOffset(double measured, const std::vector<double>& candidates, double tol = 0.004);

struct LoadResult {
    int deck = 0;
    uint32_t id = 0;
    TrackPtr track;          // nullptr si error
    std::string error;
    bool analyzedNow = false;  // el BPM se calculó en esta carga (guardar en caché)
};

class TrackLoader {
public:
    TrackLoader();
    ~TrackLoader();
    // Encola una carga; cancela cualquier carga pendiente del mismo plato.
    uint32_t request(LoadJob job);
    bool poll(LoadResult& out);
    float progress(int deck) const { return progress_[deck & 1].load(); }
    bool busy(int deck) const { return busy_[deck & 1].load(); }
    // Consulta de rejillas externas (se llama desde el hilo del cargador). Ponerla antes de cargar.
    std::function<bool(const std::wstring&, ExternalGrid&)> externalGrid;

private:
    void run();
    std::thread th_;
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<LoadJob> jobs_;
    std::deque<LoadResult> results_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> cancel_[2];
    std::atomic<float> progress_[2];
    std::atomic<bool> busy_[2];
    uint32_t nextId_ = 1;
};

} // namespace rdj

// Separador de pistas (stems) tipo Moises: voz, batería, bajo, guitarra,
// piano y otros. Usa Demucs (Meta, MIT) en un proceso aparte (Python +
// PyTorch CPU) instalado bajo demanda en %LOCALAPPDATA%\RamiroDJ\separador.
// La interfaz nunca se bloquea: instalación y separación van en hilos propios.
#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rdj {

constexpr int kMaxStems = 6;

struct StemSong {
    std::wstring folder;        // carpeta con los FLAC
    std::wstring source;        // archivo original
    std::string title;          // UTF-8
    std::string model;          // htdemucs_6s / htdemucs
    int64_t created = 0;
    std::vector<std::string> stems;  // nombres de archivo sin extensión (voz, bateria…)
};

struct StemJob {
    std::wstring source;
    std::string title;
    std::string model;
    std::atomic<float> progress{0.f};
    std::string state;          // texto para la interfaz
    bool done = false, failed = false;
};

// Copia para la interfaz (tomada bajo el mutex).
struct StemJobInfo {
    std::wstring source;
    std::string title, model, state;
    float progress = 0;
    bool done = false, failed = false;
};

// Nombre visible de cada stem.
const char* stemLabel(const std::string& id);

class StemSeparator {
public:
    enum class Engine { Desconocido, NoInstalado, Instalando, Listo, Error };
    StemSeparator();
    ~StemSeparator();

    void checkEngine();                    // comprueba si el motor está instalado (rápido)
    Engine engine() const { return engine_.load(); }
    std::string engineMessage();
    std::vector<std::string> installLog(); // últimas líneas
    void install();                        // descarga e instala (hilo propio)
    void cancelInstall();

    // Cola de separación.
    void enqueue(const std::wstring& path, const std::string& title, const std::string& model);
    std::vector<StemJobInfo> jobs();
    void cancelCurrent();
    void clearFinished();

    // Canciones ya separadas.
    std::vector<StemSong> songs();
    uint64_t songsVersion() const { return songsVersion_.load(); }
    void removeSong(const std::wstring& folder);
    std::wstring stemsRoot() const;
    std::wstring folderFor(const std::wstring& source) const;  // dónde irían sus stems

private:
    void installThread();
    void workerThread();
    bool runProcess(const std::wstring& cmdLine, const std::wstring& cwd, const std::function<void(const std::string&)>& onLine, std::atomic<bool>* cancel);
    std::wstring pythonExe() const;        // python del entorno virtual
    std::wstring baseDir() const;
    void log(const std::string& line);
    void writeScript();

    std::atomic<Engine> engine_{Engine::Desconocido};
    std::string engineMsg_;
    std::mutex m_;
    std::deque<std::string> log_;
    std::thread installTh_, workerTh_;
    std::atomic<bool> quit_{false}, cancelInstall_{false}, cancelJob_{false};
    std::deque<std::shared_ptr<StemJob>> jobs_;
    std::condition_variable cv_;
    std::atomic<uint64_t> songsVersion_{1};
    void* currentProcess_ = nullptr;      // HANDLE del proceso en curso
};

} // namespace rdj

// Grabación de la mezcla: el hilo de audio deja el master en un anillo SPSC y
// este hilo lo escribe a disco (WAV), sin E/S en el hilo de audio.
#pragma once
#include "engine.h"
#include <atomic>
#include <string>
#include <thread>

namespace rdj {

enum class RecFormat : int { Wav16 = 0, WavFloat32 = 1 };

// Escribe un WAV estéreo de 32 bits en coma flotante (exportar loops del rack). Hilo de interfaz.
bool writeWavStereoFloat(const std::wstring& path, const float* L, const float* R, uint64_t frames, uint32_t sampleRate, std::string& err);

class Recorder {
public:
    ~Recorder() { stop(); }
    bool start(Engine& eng, const std::wstring& path, RecFormat fmt, std::string& err);
    void stop();
    bool active() const { return running_.load(); }
    double seconds() const { return sr_ ? (double)framesWritten_.load() / sr_ : 0.0; }
    uint64_t framesWritten() const { return framesWritten_.load(); }
    const std::wstring& path() const { return path_; }
    std::string error() const { return writeError_ ? "Error de escritura en disco: la grabación puede estar incompleta." : ""; }

private:
    void run(RecFormat fmt);
    Engine* eng_ = nullptr;
    std::thread th_;
    std::atomic<bool> running_{false}, stopReq_{false}, writeError_{false};
    std::atomic<uint64_t> framesWritten_{0};
    std::wstring path_;
    uint32_t sr_ = 0;
    void* encoder_ = nullptr;  // ma_encoder*
};

} // namespace rdj

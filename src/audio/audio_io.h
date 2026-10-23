// Gestión de dispositivos de salida (miniaudio: WASAPI, DirectSound o WinMM).
#pragma once
#include "engine.h"
#include <memory>
#include <string>
#include <vector>

namespace rdj {

struct OutputDevice {
    std::string name;      // UTF-8
    bool isDefault = false;
    int maxChannels = 2;   // 0 = desconocido
    int index = 0;         // posición en la lista de miniaudio
};

struct AudioConfig {
    std::string deviceName;     // vacío = predeterminado del sistema
    int periodMs = 10;
    int sampleRate = 0;         // 0 = la del dispositivo; si no la admite, miniaudio convierte
    bool exclusive = false;
    HpRouting hpRouting = HpRouting::Ninguna;
    std::string hpDeviceName;   // para DispositivoAparte
};

struct AudioState {
    bool running = false;
    bool simulated = false;     // backend nulo (sin salida real)
    std::string backend;
    std::string deviceName;
    std::string hpDeviceName;
    uint32_t sampleRate = 0;
    int channels = 0;
    int periodFrames = 0;
    bool exclusive = false;
    HpRouting hpRouting = HpRouting::Ninguna;  // la que realmente está activa
    std::string message;        // avisos para el usuario
};

class AudioIO {
public:
    AudioIO();
    ~AudioIO();
    bool init(std::string& err, bool forceNull = false);
    void refreshDevices();
    const std::vector<OutputDevice>& devices() const { return devices_; }
    // Abre (o reabre) la salida. Si falla, intenta el predeterminado y por último
    // el modo simulado para que la aplicación siga funcionando.
    bool open(Engine& eng, const AudioConfig& cfg);
    void close();
    const AudioState& state() const { return state_; }
    bool deviceLost() const;
    void clearLost();

    struct Impl;
private:
    bool openOn(Engine& eng, const AudioConfig& cfg, bool useNull, std::string& err);
    std::unique_ptr<Impl> d_;
    std::vector<OutputDevice> devices_;
    AudioState state_;
};

} // namespace rdj

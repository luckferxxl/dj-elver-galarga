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
    std::string deviceName;     // vacío = predeterminado del sistema (Altavoces / Salida 1)
    int periodMs = 10;
    int sampleRate = 0;         // 0 = la del dispositivo; si no la admite, miniaudio convierte
    bool exclusive = false;
    HpRouting hpRouting = HpRouting::Canales34;
    std::string hpDeviceName;   // para Audífonos / Salida 2 (vacío o igual a deviceName = mismo dispositivo)
    int outMode = 1;            // 0 = SOLO ALTAVOCES, 1 = ALTAVOCES + AUDIFONOS, 2 = DECKS SEPARADOS
    int hwMode = 0;             // 0 = AUDIO INTERCONSTRUIDO, 1 = TARJETA(S) DE AUDIO USB, 2 = DIVISOR EN "Y" DE ESTEREO A MONOAURAL
    int spkChan = 0;            // 0 = chan 1 & 2, 1 = chan 3 & 4, 2 = chan 5 & 6, 3 = chan 7 & 8, 4 = chan 1 (Mono L), 5 = chan 2 (Mono R)
    int hpChan = 1;             // 1 = chan 3 & 4 por defecto (como en VirtualDJ)
    std::string configProfile = "config 1";
};

struct AudioState {
    bool running = false;
    bool simulated = false;     // backend nulo (sin salida real)
    std::string backend;
    std::string deviceName;
    std::string hpDeviceName;
    uint32_t sampleRate = 0;
    int channels = 0;
    int nativeChannels = 2;
    int periodFrames = 0;
    bool exclusive = false;
    HpRouting hpRouting = HpRouting::Ninguna;  // la que realmente está activa
    int spkChan = 0;
    int hpChan = 1;
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

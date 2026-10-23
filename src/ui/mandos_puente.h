// Mandos reconocidos al enchufarlos (como VirtualDJ): hace funcionar los mapeos de Mixxx
// incluidos en res/mandos. Cada mando reconocido lleva su propio intérprete de JavaScript
// (QuickJS) con la API de Mixxx (engine, midi, console, ColorMapper) y sus controles se
// traducen a los de Ramiro DJ (mandos_controles.cpp). Luces: las salidas del XML y lo que
// mande el script; SysEx en los dos sentidos.
#pragma once
#include "../core/mandos.h"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace rdj {

class App;
class MandoActivo;

// Cómo trata Mixxx un control: rango, curva y qué hace un valor MIDI directo.
struct MxBehavior {
    enum Kind { Num, Pot, Audio, Push, Toggle, PowerWin, Encoder } kind = Num;
    double min = 0, max = 1, def = 0;
    double minDb = -20, maxDb = 0, neutral = 1;   // Audio (curva en dB, como ControlAudioTaperPot)
    double valueToParam(double v) const;
    double paramToValue(double p) const;
    double midiToParam(double midi) const;
    double valueToMidi(double v) const;
    bool isButton() const { return kind == Push || kind == Toggle || kind == PowerWin; }
};

struct MandoInfo {   // para la interfaz
    std::string puerto, archivo, nombre, autor, descripcion, eleccion;
    int dispositivo = -1;
    bool activo = false, luces = false;
    int errores = 0;
    std::vector<std::string> registro;
};

// Resultado de probar un mapeo sin el mando (--mandos-prueba).
struct MandoPrueba {
    std::string archivo, nombre;
    bool cargado = false, iniciado = false;
    int errores = 0, entradas = 0, salidas = 0, luces = 0, sysex = 0;
    int controles = 0, sinEquivalente = 0;
    std::vector<std::string> registro;
    std::vector<std::string> noTraducidos;
};

class MandoPuente {
public:
    explicit MandoPuente(App& app);
    ~MandoPuente();

    void arrancar(bool avisar);   // tras abrir los puertos: arranca los mapeos que tocan
    void parar();                 // antes de cerrar los puertos (shutdown() del script: apaga luces)
    void tick();                  // cada fotograma: mensajes, temporizadores, scratch y luces
    std::vector<MandoInfo> info() const;
    std::string mapeoAuto(const std::string& puerto) const;   // el que se elegiría solo

    // Registro de controles de Mixxx traducidos a Ramiro DJ (mandos_controles.cpp).
    double get(const std::string& g, const std::string& k);
    void set(const std::string& g, const std::string& k, double v);
    MxBehavior behavior(const std::string& g, const std::string& k) const;
    bool real(const std::string& g, const std::string& k) const;    // se lee y escribe sobre Ramiro DJ
    bool known(const std::string& g, const std::string& k) const;   // real, o aceptado sin efecto (balance, foco de efecto…)

    // Para MandoActivo (no tiene acceso a App).
    void midiShort(int dev, uint8_t s, uint8_t d1, uint8_t d2);
    void midiSysex(int dev, const std::vector<uint8_t>& d);
    bool playerInfo(int d, std::string& title, std::string& artist, int& key) const;

    // Pruebas: arranca un mapeo sin mando, le manda todos sus controles y lo para.
    MandoPrueba probar(const std::string& file, bool pulsar);
    // Pruebas funcionales: mapeo simulado al que se le inyectan mensajes a mano.
    MandoActivo* simular(const std::string& file);
    void simularMidi(MandoActivo* m, uint8_t status, uint8_t d1, uint8_t d2);
    void terminarSimulacion();
    void limpiarSalidas(MandoActivo* m);
    std::vector<std::vector<uint8_t>> salidasSimuladas(MandoActivo* m) const;

private:
    friend class MandoActivo;
    bool readReal(const std::string& g, const std::string& k, double& v);
    bool writeReal(const std::string& g, const std::string& k, double v);
    void deckTick(float dt);
    App& app_;
    std::vector<std::unique_ptr<MandoActivo>> activos_;
    std::unique_ptr<MandoActivo> simulado_;
    std::unordered_map<std::string, double> virtual_;
    // Valores pedidos que el motor aplica unos milisegundos después (play, loop, hot cues):
    // si el script los vuelve a leer enseguida, ve lo que pidió (en Mixxx es inmediato).
    struct Pending { double value, until; };
    std::unordered_map<std::string, Pending> pending_;
    void setPending(const std::string& g, const std::string& k, double v);
    double lastTick_ = 0;
};

} // namespace rdj

// Entrada/salida MIDI (WinMM), mapeo con aprendizaje, capa SHIFT, controles de
// 14 bits y luces del mando. El callback de WinMM solo encola mensajes; el
// mapeo se resuelve en el hilo de la interfaz.
#pragma once
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace rdj {

enum class MidiKind { Boton, Absoluto, Relativo };

struct MidiActionInfo {
    const char* id;      // identificador estable (para guardar)
    const char* name;    // texto en español
    MidiKind kind;
    bool perDeck;
};

// Índices en midiActions(). Las nuevas acciones se añaden al final.
enum MidiAct : int {
    MA_Play, MA_Cue, MA_Sync, MA_Pfl, MA_Hot1, MA_Hot2, MA_Hot3, MA_Hot4, MA_Hot5, MA_Hot6, MA_Hot7, MA_Hot8,
    MA_LoopIn, MA_LoopOut, MA_Loop4, MA_LoopHalve, MA_LoopDouble, MA_LoopExit, MA_BendUp, MA_BendDown, MA_Load, MA_FxOn,
    MA_Volume, MA_Gain, MA_EqHigh, MA_EqMid, MA_EqLow, MA_Filter, MA_Pitch, MA_FxAmount,
    MA_Jog,
    MA_Crossfader, MA_MasterVol, MA_HpVol, MA_HpMix,
    MA_BrowseScroll, MA_BrowseUp, MA_BrowseDown,
    MA_JogTouch, MA_JogScratch, MA_Shift, MA_Reloop, MA_BrowseLoad,
    MA_Slip, MA_Reverse, MA_ReverseHold, MA_RollQuarter, MA_RollOne, MA_Backspin, MA_Brake, MA_Vinyl,
    // Rack de hardware virtual (0.9). Globales. También funcionan en mandos con mapeo de Mixxx:
    // un control asignado aquí deja de ir al mapeo.
    MA_RackFxOff, MA_RackMacro, MA_RackXyX, MA_RackXyY, MA_RackXyTouch, MA_RackXyHold,
    MA_RackSlot1On, MA_RackSlot2On, MA_RackSlot3On, MA_RackSlot4On,
    MA_RackSlot1Amt, MA_RackSlot2Amt, MA_RackSlot3Amt, MA_RackSlot4Amt,
    MA_RackSlot1Mix, MA_RackSlot2Mix, MA_RackSlot3Mix, MA_RackSlot4Mix,
    MA_RackReturn, MA_RackIsoOn, MA_RackIsoLow, MA_RackIsoMid, MA_RackIsoHigh,
    MA_RackIsoKillLow, MA_RackIsoKillMid, MA_RackIsoKillHigh,
    MA_RackLoopRec, MA_RackLoopPlay, MA_RackLoopDub, MA_RackLoopUndo, MA_RackLoopClear, MA_RackLoopVol,
    MA_RackPresetNext, MA_RackPresetPrev,
    // Instrumentos (0.10)
    MA_RackDrumPlay, MA_RackBassPlay, MA_RackDrumPatNext,
    MA_RackPad1, MA_RackPad2, MA_RackPad3, MA_RackPad4, MA_RackPad5, MA_RackPad6, MA_RackPad7, MA_RackPad8,
    MA_RackPad9, MA_RackPad10, MA_RackPad11, MA_RackPad12, MA_RackPad13, MA_RackPad14, MA_RackPad15, MA_RackPad16,
    // 0.11.1: quitar de golpe todos los efectos y filtros que queden puestos (botón QUITAR FX). Global; va por rackMidi.
    MA_FxClearAll,
    MA_Count
};
inline bool isRackAction(int a) { return a >= MA_RackFxOff && a < MA_Count; }

const std::vector<MidiActionInfo>& midiActions();

struct MidiBinding {
    int action = 0;
    int deck = 0;          // 0/1 (ignorado en acciones globales)
    uint8_t type = 0x9;    // 0x9 nota, 0xB control
    uint8_t channel = 0;   // 0..15
    uint8_t data1 = 0;
    int relMode = 0;       // relativos: 0 = centro 64, 1 = complemento a 2
    bool invert = false;   // absolutos: invertir el sentido
    bool shift = false;    // solo con SHIFT pulsado
    bool hiRes = false;    // 14 bits: el byte bajo llega en data1 + 0x20 (se detecta solo)
};

struct MidiEvent {
    int device = 0;
    uint8_t status = 0, data1 = 0, data2 = 0;
    std::vector<uint8_t> sysex;   // status 0xF0: mensaje SysEx completo (F0 … F7)
};

// Resultado ya interpretado para la interfaz.
struct MidiCommand {
    int action = 0;
    int deck = 0;
    bool pressed = false;  // botones
    float value = 0;       // absolutos 0..1
    int delta = 0;         // relativos
};

// Ajustes del mando (se guardan en midi.json junto a las asignaciones).
struct MidiSettings {
    float jogBendSens = 1.f;      // aro del jog: empuje de tempo
    float jogScratchSens = 1.f;   // plato del jog: scratch
    float jogSeekSens = 1.f;      // jog con el plato en pausa: desplazamiento
    bool touchScratch = true;     // tocar el plato activa el scratch
    bool softTakeover = true;     // faders/potenciómetros no saltan al tocarlos
    bool leds = true;             // encender las luces del mando
    bool pitchInvert = false;     // invierte todos los pitch (si el mando va al revés)
};

class MidiManager;
struct MidiInContext {
    MidiManager* mm;
    int device;
    void* handle = nullptr;              // HMIDIIN (para devolver los búferes SysEx)
    std::vector<std::vector<uint8_t>> buffers;
    std::vector<std::vector<uint8_t>> headers;   // MIDIHDR
    std::vector<uint8_t> partial;        // SysEx que llega en varios trozos
};

class MidiManager {
public:
    ~MidiManager();
    void refresh();                          // reabre todas las entradas y salidas
    bool devicesChanged() const;             // ¿se ha conectado o desconectado algún mando?
    const std::vector<std::string>& devices() const { return names_; }
    const std::vector<std::string>& outputs() const { return outNames_; }
    const std::vector<std::string>& errors() const { return errors_; }
    std::vector<MidiBinding>& bindings() { return bindings_; }
    MidiSettings& settings() { return settings_; }

    void startLearn(int action, int deck, bool shift = false) { learnAction_ = action; learnDeck_ = deck; learnShift_ = shift; learnedNow_ = false; }
    void stopLearn() { learnAction_ = -1; }
    bool learning() const { return learnAction_ >= 0; }
    int learnAction() const { return learnAction_; }
    int learnDeck() const { return learnDeck_; }
    bool takeLearned() { bool r = learnedNow_; learnedNow_ = false; return r; }  // para el asistente
    void clearBinding(int action, int deck);
    void assign(const MidiBinding& b);  // sustituye asignaciones en conflicto y guarda
    const MidiBinding* findBinding(int action, int deck) const;

    // Procesa los mensajes pendientes. Devuelve las órdenes resultantes.
    std::vector<MidiCommand> poll();
    const std::deque<MidiEvent>& recent() const { return recent_; }
    bool shiftHeld() const { return shiftHeld_; }

    // Luces: enciende o apaga la luz asociada a una acción (si el mando tiene salida).
    void setLed(int action, int deck, bool on);
    void resetLeds();

    void load();
    void save();
    void loadPresetWeGO();
    bool anyControllerNamed(const char* fragment) const;

    // Mandos con mapeo integrado (de Mixxx): sus mensajes no pasan por las asignaciones
    // propias; se recogen tal cual con takeIntegrated() y sus luces las manda el mapeo.
    void setIntegrated(int device, bool on);
    bool integrated(int device) const { return device >= 0 && device < (int)integrated_.size() && integrated_[device]; }
    std::vector<MidiEvent> takeIntegrated();
    bool hasOutput(int device) const;
    void sendShortTo(int device, uint8_t status, uint8_t d1, uint8_t d2);
    void sendSysexTo(int device, const std::vector<uint8_t>& data);
    // Mapeo elegido para cada puerto: "auto" (por defecto), "ninguno" o el archivo .midi.xml.
    std::string mappingChoice(const std::string& port) const;
    void setMappingChoice(const std::string& port, const std::string& choice);

    // Uso interno del callback.
    void pushEvent(const MidiEvent& e);
    void pushSysex(MidiInContext* ctx, const uint8_t* data, size_t n);

private:
    void closeAll();
    void sendShort(uint8_t status, uint8_t d1, uint8_t d2);
    std::vector<void*> handles_;  // HMIDIIN
    std::vector<void*> outHandles_;  // HMIDIOUT
    std::vector<int> outDevice_;     // entrada emparejada con cada salida (mismo mando)
    std::vector<bool> integrated_;
    std::vector<MidiEvent> integratedQueue_;
    std::map<std::string, std::string> mappingChoice_;
    std::vector<std::unique_ptr<MidiInContext>> contexts_;
    std::vector<std::string> names_, outNames_, errors_;
    std::vector<MidiBinding> bindings_;
    MidiSettings settings_;
    std::mutex m_;
    std::deque<MidiEvent> queue_, recent_;
    int learnAction_ = -1, learnDeck_ = 0;
    bool learnShift_ = false, learnedNow_ = false;
    bool shiftHeld_ = false;
    std::map<uint32_t, uint8_t> msb_;      // (canal, cc) -> último byte alto
    std::map<uint32_t, uint8_t> ledState_; // (estado, nota) -> valor enviado
};

} // namespace rdj

// Rack de hardware virtual (0.9): estación de efectos, pad XY, aislador de tres bandas y
// loop station. Vive en el motor de audio: la interfaz escribe RackParams (atómicos) y
// manda órdenes por la cola del motor; el rack publica RackStatus.
//
// Reglas (como el resto del motor): process*() no reserva memoria, no bloquea y no hace
// E/S. Todo se reserva en prepare() salvo la memoria de la loop station, que la reserva
// la interfaz la primera vez que se usa (allocLoop) para no gastarla si no se usa.
#pragma once
#include "dsp.h"
#include "creative_fx.h"
#include "effects.h"
#include "instruments.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace rdj {

constexpr int kRackBlock = 256;   // = kMaxBlock del motor

// Fuente de cada módulo.
enum class RackSrc : int { PlatoA = 0, PlatoB = 1, Mezcla = 2, Instr = 3, Count };   // Instr: caja de ritmos, bajo y sampler
const char* rackSrcName(int s);

// ------------------------------------------------------------------ estación de efectos
enum class StFx : int { Ninguno = 0, Filtro, Eco, Delay, Reverb, Flanger, Phaser, Gate, Saturacion, Distorsion,
                       EcoCinta, ReverbLarga, Bitcrusher, Repetidor,
                       // 0.11.0
                       Riser, Spiral, Sidechain, Shimmer, Vinilo, Telefono, Chorus, Tremolo, AutoPan, Freeze, Slicer,
                       Granular, Resonador, Multibanda,
                       Count };   // los nuevos van al final (los presets guardan el id)
const char* stFxName(int t);
const char* stFxId(int t);                 // identificador estable (presets)
int stFxFromId(const std::string& id);
const char* stFxAmountLabel(int t);        // qué hace la ruleta de intensidad
const char* stFxDescription(int t);        // una línea para la interfaz
bool stFxHasTail(int t);                   // eco, delay, reverb: suman su señal y dejan cola
bool stFxUsesTime(int t);                  // tiempo sincronizado con el BPM
bool stFxNeedsHistory(int t);              // repetidor, freeze y slicer: graben siempre lo último que sonó
bool stFxDivAllowed(int t, int div);       // divisiones de tiempo con sentido para ese efecto
int stFxDefaultDiv(int t);
std::string stFxTimeText(int t, int div);  // «1/2 pulso», «4 compases» (el riser cuenta en compases)
std::string stFxAmountText(int t, float amount);   // valor de la ruleta de intensidad (tooltip)
// Ritmo del slicer: 8 patrones de 8 trozos.
constexpr int kSlicerPatterns = 8;
int slicerPattern(float amount);

// Efecto del plato (FxType) → efecto de la estación equivalente (0 = lo procesa FxSlot, como siempre).
inline int deckFxToStation(int fxt) {
    static_assert((int)FxType::Count - (int)FxType::Riser == (int)StFx::Count - (int)StFx::Riser, "FxType y StFx deben ir en el mismo orden");
    return fxt >= (int)FxType::Riser && fxt < (int)FxType::Count ? (int)StFx::Riser + (fxt - (int)FxType::Riser) : 0;
}
// «Cantidad» del plato → intensidad y mezcla de la ranura: mueve la ruleta principal; en los de cola, cuánto se oye.
inline void deckFxUnitParams(int st, float cantidad, float& amount, float& mix) {
    switch ((StFx)st) {
    case StFx::Spiral: amount = 0.5f; mix = cantidad; break;
    case StFx::Shimmer: amount = 0.6f; mix = cantidad; break;
    case StFx::Freeze: amount = 0.45f; mix = cantidad; break;
    default: amount = cantidad; mix = 1.f; break;
    }
}

constexpr int kStSlots = 4;
constexpr int kStDivs = 10;
// Divisiones musicales en pulsos (negras).
constexpr float kStDivBeats[kStDivs] = {1.f / 16, 1.f / 8, 1.f / 4, 1.f / 3, 1.f / 2, 3.f / 4, 1.f, 2.f, 4.f, 8.f};
const char* stDivName(int i);

struct StSlotParams {
    std::atomic<int> type{0};
    std::atomic<bool> on{false};
    std::atomic<float> amount{0.5f};    // intensidad 0..1 (su sentido depende del efecto)
    std::atomic<float> mix{0.5f};       // original ↔ procesado
    std::atomic<int> div{6};            // índice en kStDivBeats (6 = 1 pulso)
    std::atomic<float> macroAmt{0.f};   // -1..1: cuánto mueve la macro la intensidad
    std::atomic<float> macroMix{0.f};   // -1..1: cuánto mueve la macro la mezcla
};

// Destinos de los ejes del pad XY.
enum XyTarget : int {
    XY_None = 0, XY_Macro = 1,
    XY_Amount0 = 2,                      // 2..5: intensidad de la ranura 1..4
    XY_Mix0 = 6,                         // 6..9: mezcla de la ranura 1..4
    XY_IsoLow = 10, XY_IsoMid = 11, XY_IsoHigh = 12,
    XY_Count = 13
};
std::string xyTargetName(int t);
const char* xyTargetId(int t);
int xyTargetFromId(const std::string& id);

// ------------------------------------------------------------------ loop station
enum class LoopState : int { Vacio = 0, Armado, Grabando, Parado, ArmadoPlay, Reproduciendo, Sobregrabando };
const char* loopStateName(int s);
constexpr double kLoopMaxSec = 30.0;      // 8 compases a 64 BPM

// ------------------------------------------------------------------ parámetros (interfaz → audio)
struct RackParams {
    std::atomic<bool> fxCut{false};       // EFECTOS OFF: todo se apaga con fundido; las colas siguen
    // Estación
    std::atomic<bool> stOn{true};
    std::atomic<int> stSrc{(int)RackSrc::Mezcla};
    std::atomic<int> stMode{0};           // 0 = inserción, 1 = envío y retorno
    std::atomic<float> stReturn{0.8f};    // nivel de retorno (envío)
    std::atomic<uint32_t> stOrder{0x3210};// ranura en cada posición, 4 bits por posición (posición 0 = bits 0-3)
    std::atomic<float> macro{0.f};
    StSlotParams slot[kStSlots];
    // Pad XY
    std::atomic<float> xyX{0.5f}, xyY{0.5f};
    std::atomic<bool> xyTouch{false}, xyHold{false};
    std::atomic<int> xyTargetX{XY_Amount0}, xyTargetY{XY_Mix0 + 1};
    std::atomic<bool> xyAutoOn{true};     // tocar el pad enciende la ranura de destino
    // Aislador
    std::atomic<bool> isoOn{false};
    std::atomic<int> isoSrc{(int)RackSrc::Mezcla};
    std::atomic<float> isoGain[3] = {0.5f, 0.5f, 0.5f};   // 0 = corte, 0,5 = 0 dB, 1 = +6 dB
    std::atomic<bool> isoKill[3] = {false, false, false};
    std::atomic<float> isoLowHz{200.f}, isoHighHz{2500.f};
    // Loop station
    std::atomic<int> lpSrc{(int)RackSrc::Mezcla};
    std::atomic<int> lpBars{2};
    std::atomic<float> lpVol{0.8f};
    std::atomic<bool> lpQuant{true}, lpFollow{true}, lpCue{false}, lpAutoPlay{false};
};

// Límites seguros de las frecuencias de cruce del aislador.
constexpr float kIsoLowMin = 60.f, kIsoLowMax = 500.f, kIsoHighMin = 1000.f, kIsoHighMax = 8000.f;

// ------------------------------------------------------------------ estado (audio → interfaz)
struct RackStatus {
    std::atomic<int> lpState{0};
    std::atomic<float> lpPos{0.f};        // 0..1: avance de la grabación o de la reproducción
    std::atomic<double> lpBpm{0.0};       // BPM con el que se grabó
    std::atomic<int> lpBars{0};
    std::atomic<float> lpSeconds{0.f};
    std::atomic<float> lpRate{1.f};       // velocidad de reproducción (seguir el BPM)
    std::atomic<int> lpLayers{0};
    std::atomic<bool> lpCanUndo{false};
    std::atomic<bool> lpBusy{false};      // escribiendo en el búfer (no se puede exportar)
    std::atomic<uint32_t> lpGen{0};       // cambia cada vez que cambia el contenido del loop
    std::atomic<bool> lpTooLong{false};   // la última grabación pedida no cabía (BPM muy lento)
    std::atomic<bool> lpWaitingStopped{false};  // armado, pero el plato de la fuente está parado
    std::atomic<float> stBpm{0.f};        // BPM al que se sincronizan los tiempos (0 = sin rejilla: 120)
    std::atomic<int> stBpmDeck{-1};       // plato del que sale ese BPM
    std::atomic<float> stPeak{0.f}, isoPeak{0.f}, lpPeak{0.f}, retPeak{0.f};
    std::atomic<bool> slotActive[kStSlots] = {false, false, false, false};   // procesando (encendida o con cola)
    std::atomic<bool> slotOnEff[kStSlots] = {false, false, false, false};    // encendida (con macro y XY)
    std::atomic<float> effAmount[kStSlots] = {0, 0, 0, 0}, effMix[kStSlots] = {0, 0, 0, 0};
    std::atomic<float> isoEff[3] = {1.f, 1.f, 1.f};
    std::atomic<float> xyWeight{0.f};
    std::atomic<uint32_t> nonFinite{0};   // bloques corregidos por valores no finitos o desbocados
    std::atomic<float> cpu{0.f};          // fracción del tiempo de cada bloque que usa el rack
};

// Contexto de tempo que el motor calcula en cada bloque.
struct RackCtx {
    double bpm[2] = {0, 0};               // BPM efectivo (0 = desconocido)
    double beat0[2] = {0, 0}, beat1[2] = {0, 0};   // posición en pulsos (desde el ancla de la rejilla) al empezar y al acabar el bloque
    bool grid[2] = {false, false};        // hay pista con BPM
    bool playing[2] = {false, false};
    int lead = -1;                        // plato que manda para la fuente «Mezcla»
};

// ------------------------------------------------------------------ procesadores

// Cambio de tono por granos (dos cabezas de lectura con ventana de Hann que suman 1):
// sube o baja el tono sin cambiar la duración. Latencia media: la mitad de la ventana.
class GrainShifter {
public:
    void prepare(int size);              // potencia de 2, mayor que la ventana
    void reset();
    inline void process(float inL, float inR, float& outL, float& outR, float ratio, float window);
private:
    std::vector<float> l_, r_;
    int mask_ = 0;
    long long w_ = 0;
    double ph_ = 0;
};

// Una ranura de la estación: un efecto con fundidos al encender, apagar y cambiar de tipo.
class StUnit {
public:
    void prepare(double fs, uint32_t seed = 0x9E3779B9u);
    void reset();
    // Inserción: procesa en el sitio. Envío: s = señal que entra (ya coloreada), ret = retorno acumulado.
    // beat0/beat1: posición en pulsos de la fuente al empezar y al acabar el bloque (sidechain, trémolo, slicer…).
    void processInsert(float* L, float* R, int n, int type, bool on, float amount, float mix, float beats, double bpm, double beat0 = 0, double beat1 = 0);
    void processSend(float* sL, float* sR, float* retL, float* retR, int n, int type, bool on, float amount, float mix, float beats, double bpm,
                     double beat0 = 0, double beat1 = 0);
    bool active() const { return curType_ != 0 && (gOn_ > 1e-4f || tail_ > 0); }
    bool busy(int type, bool on) const {
        return on || active() || (type != curType_ && curType_ != 0 && xg_ < 1.f) || (stFxNeedsHistory(type) && curType_ == type);
    }
private:
    void run(float* L, float* R, int n, float amt0, float amt1, float beats, double bpm, bool feedOn);   // procesa tmp_
    void wet(const float* inL, const float* inR, int n, float a0, float a1, float beats, double bpm);
    void advanceGates(int type, bool on, int n);
    void tailCheck(float e, int n);
    double fs_ = 48000;
    int curType_ = 0;
    float gOn_ = 0.f, xg_ = 1.f, amt_ = 0.5f, mix_ = 0.5f;   // encendido, fundido de cambio de tipo
    int tail_ = 0;                        // muestras de cola que faltan por comprobar
    int quiet_ = 0;                       // muestras seguidas casi en silencio
    float tmpL_[kRackBlock], tmpR_[kRackBlock], wL_[kRackBlock], wR_[kRackBlock], gArr_[kRackBlock], xArr_[kRackBlock];
    // Efectos (los de los platos se reutilizan: filtro, reverb, flanger, phaser, gate)
    CreativeFx creative_;
    DjFilter filter_;
    Reverb reverb_;
    Flanger flanger_;
    PerformanceFx perf_;
    struct Line { std::vector<float> l, r; int size = 1; long long w = 0; double cur = 0; float lpL = 0, lpR = 0, hpL = 0, hpR = 0; } line_;
    float satLpL_ = 0, satLpR_ = 0;
    // Eco de cinta (modulación) y reverb larga (pre-retardo y filtro de entrada)
    double wow_ = 0, flutter_ = 0;
    float revHpL_ = 0, revHpR_ = 0;
    // Bitcrusher y repetidor
    float crushL_ = 0, crushR_ = 0, crushCount_ = 0;
    bool repeating_ = false;
    long long repStart_ = 0, repLen_ = 0, repPos_ = 0;
    int repCount_ = 0;
    // 0.11.0 ------------------------------------------------------------
    double beat0_ = 0, beat1_ = 0;        // posición en pulsos de este bloque
    uint32_t seed_ = 1, rng_ = 1;
    inline float noise() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return (float)(int32_t)rng_ * (1.f / 2147483648.f); }
    Svf svf_[8];                          // filtros de uso general (cada efecto usa los suyos)
    GrainShifter shift_;                  // spiral y shimmer
    float fbRing_[2][1024] = {};          // shimmer: realimentación con un bloque de retraso
    long long fbW_ = 0;
    double riseT_ = 0;                    // riser: avance de la subida (0..1)
    float smG_ = 1.f, smP_ = 0.f;         // sidechain/trémolo y auto-pan suavizados
    double modPh_ = 0;                    // chorus
    float clickEnv_ = 0, clickAmp_ = 0;   // vinilo: crujidos
    float envIn_ = 0, envOut_ = 0;        // teléfono: compensación de nivel
    // Freeze y slicer: graban siempre lo último que suena (en line_) y, al encenderse, copian el
    // trozo a hb* para tocarlo a su manera mientras siguen grabando (se pueden encender y apagar seguido).
    long long fill_ = 0;                  // muestras grabadas (hasta el tamaño de line_)
    bool held_ = false;                   // congelado o troceando
    std::vector<float> hbL_, hbR_;        // trozo copiado (hasta ≈ 2,7 s)
    int hbCap_ = 0, hbLen_ = 0;
    double heldLen_ = 0, heldPh_ = 0, heldRegPh_ = 0;
};

// Aislador de tres bandas con cruces LR4 ajustables (suma plana con las tres a 0 dB).
class Isolator {
public:
    void prepare(double fs);
    void reset();
    // g*: ganancias objetivo (0 = corte). gate: 0..1 entrada/salida del módulo.
    void process(float* L, float* R, int n, const float g[3], float lowHz, float highHz, bool on);
    bool active() const { return gate_ > 1e-4f; }
    const float* gains() const { return g_; }
private:
    void setFreqs(double lo, double hi);
    double fs_ = 48000;
    LR4 lp1_, hp1_, lp2_, hp2_;
    Biquad apLow_;
    double lo_ = 200, hi_ = 2500;
    float g_[3] = {1, 1, 1};
    float gate_ = 0.f;
};

// ------------------------------------------------------------------ rack
class Rack {
public:
    void prepare(double fs);
    // Memoria de la loop station (hilo de interfaz, antes de la primera grabación).
    void allocLoop();
    bool loopReady() const { return loopReady_.load(std::memory_order_acquire); }
    // Lectura para exportar (hilo de interfaz): solo si lpBusy es falso y el estado no graba.
    int64_t loopFrames() const { return lpLen_; }
    const float* loopL() const { return lpL_.data(); }
    const float* loopR() const { return lpR_.data(); }

    // Hilo de audio.
    void command(int cmd, int arg);       // órdenes de la loop station (Cmd::RackLoop*)
    void beginBlock(const RackCtx& c, int n);
    void processInsert(int src, float* L, float* R, int n);   // aislador y estación en inserción en esa fuente
    void tapSend(int src, const float* L, const float* R, int n, float gain0, float gain1);  // señal de envío (tras el fader)
    void tapLoop(int src, const float* L, const float* R, int n);
    // Suma retornos y loop al master (y la preescucha si se pide). Llamar una vez por bloque.
    void finishBlock(float* mL, float* mR, float* cL, float* cR, int n);
    bool touches(int src) const { return isoSrcCur_ == src || (stSrcCur_ == src && stModeCur_ == 0); }
    // Bus de instrumentos: lo genera, le aplica el rack si es su fuente y lo suma a la mezcla (y a la preescucha).
    void mixInstruments(float* mL, float* mR, float* cL, float* cR, int n);
    void padCommand(int pad, int action, const Track* t, uint32_t gen);   // Cmd::RackPad
    Instruments instr;

    RackParams params;
    RackStatus status;

    enum LoopCmd { LoopRec = 0, LoopPlay, LoopStop, LoopClear, LoopDub, LoopUndo, LoopPlayStop };

private:
    float slotParam(int slot, bool mix) const;        // con macro (sin XY)
    void evalTargets(int n);
    void runStation(float* L, float* R, int n);
    void processInsertImpl(int src, float* L, float* R, int n);
    void runLoop(int n, float* mL, float* mR, float* cL, float* cR);
    bool sourceBeat(int src, double& b0, double& b1, double& bpm, bool& playing) const;
    bool sanitize(float* L, float* R, int n);

    double fs_ = 48000;
    RackCtx ctx_;
    // Estación
    StUnit units_[kStSlots];
    int stSrcCur_ = (int)RackSrc::Mezcla, stModeCur_ = 0;
    float stGate_ = 1.f;                  // fundido al cambiar de fuente o de modo
    float effAmt_[kStSlots] = {}, effMix_[kStSlots] = {};
    bool effOn_[kStSlots] = {};
    float xyW_ = 0.f;
    float macroCut_ = 1.f;                // EFECTOS OFF (1 = normal)
    double stBpm_ = 120;
    double stBeat0_ = 0, stBeat1_ = 0, freeBeat_ = 0;   // reloj de la estación (sin rejilla o parado: libre a stBpm_)
    float sendL_[kRackBlock], sendR_[kRackBlock], retL_[kRackBlock], retR_[kRackBlock], dryL_[kRackBlock], dryR_[kRackBlock];
    bool sendTapped_ = false;
    float lastRet_ = 0.f;
    // Aislador
    Isolator iso_;
    int isoSrcCur_ = (int)RackSrc::Mezcla;
    bool isoSwitching_ = false;           // cambio de fuente pendiente: primero se apaga con fundido
    double accTime_ = 0;                  // tiempo de proceso del rack en este bloque
    // Loop station
    std::atomic<bool> loopReady_{false};
    std::vector<float> lpL_, lpR_, undoL_, undoR_;
    int64_t lpCap_ = 0, lpLen_ = 0, lpRec_ = 0;
    double lpPlay_ = 0, lpRate_ = 1, lpBpm_ = 0;
    int lpBarsRec_ = 0, lpBarsPending_ = 2, lpLayers_ = 0;
    LoopState lpState_ = LoopState::Vacio, lpBeforeArm_ = LoopState::Vacio;
    float lpGain_ = 0.f, lpVol_ = 0.8f;
    int lpSeam_ = -1, lpSeamLen_ = 0;     // fundido de la unión (fin → principio) tras grabar
    // Sobregrabación y deshacer (una capa)
    int64_t dubStart_ = -1, dubSpan_ = 0, dubLast_ = -1;
    bool canUndo_ = false;
    int64_t undoPos_ = -1, undoLeft_ = 0;  // restauración en curso (por trozos)
    float capL_[kRackBlock], capR_[kRackBlock];
    float insL_[kRackBlock], insR_[kRackBlock];   // bus de instrumentos
    bool capTapped_ = false;
    uint32_t gen_ = 0;
};

} // namespace rdj

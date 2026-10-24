// Instrumentos del rack (0.10): caja de ritmos, sintetizador de bajos y sampler de 16 pads.
// Suenan por su propio bus («instrumentos»), que va a la mezcla y que los módulos del rack
// pueden usar como fuente. Sus secuenciadores van con un reloj que sigue el pulso del plato
// que manda (o un tempo propio). Mismas reglas que el resto del motor: nada de memoria,
// bloqueos ni disco en el hilo de audio; las muestras de los pads las decodifica la interfaz.
#pragma once
#include "dsp.h"
#include "track.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rdj {

struct RackCtx;

constexpr int kDrumTracks = 8;
constexpr int kDrumPatterns = 8;
constexpr int kMaxSteps = 32;
constexpr int kBassSteps = 16;
constexpr int kBassPatterns = 8;
constexpr int kPads = 16;

enum DrumVoice : int { DV_Bombo = 0, DV_Caja, DV_Palmas, DV_CharlesC, DV_CharlesA, DV_Tom, DV_Rim, DV_Platillo };
const char* drumVoiceName(int v);    // «Bombo», «Caja»…
const char* drumVoiceId(int v);      // identificador estable (presets)

// Paso del bajo: nota (0..24 semitonos sobre la tónica) y banderas.
constexpr int kBassGate = 1 << 8, kBassAccent = 1 << 9, kBassSlide = 1 << 10;
inline int bassNote(int v) { return v & 0x3F; }

// Reloj de los secuenciadores: 0 = el plato que manda, 1 = plato A, 2 = plato B, 3 = tempo propio.
enum class SeqSync : int { Lider = 0, PlatoA, PlatoB, Propio };

enum PadMode : int { PM_Disparo = 0, PM_Mantener, PM_Loop };

struct InstrParams {
    // Reloj
    std::atomic<int> sync{0};
    std::atomic<float> ownBpm{124.f};
    std::atomic<bool> quantStart{true};      // arrancar en el siguiente compás
    // Caja de ritmos
    std::atomic<bool> drumPlay{false};
    std::atomic<int> drumPattern{0};         // el que se pide (entra al acabar el que suena)
    std::atomic<int> drumSteps[kDrumPatterns];
    std::atomic<float> swing{0.5f};          // 0,5 recto … 0,75
    std::atomic<float> drumVol{0.8f};
    std::atomic<uint8_t> drum[kDrumPatterns][kDrumTracks][kMaxSteps];   // 0 nada, 1 golpe, 2 acento
    std::atomic<float> dLevel[kDrumTracks], dTune[kDrumTracks], dDecay[kDrumTracks];   // 0..1 (0,5 = normal)
    std::atomic<bool> dMute[kDrumTracks];
    // Bajo
    std::atomic<bool> bassPlay{false};
    std::atomic<int> bassPattern{0};
    std::atomic<int16_t> bass[kBassPatterns][kBassSteps];
    std::atomic<int> bassRoot{9};            // 0 Do … 11 Si (La por defecto)
    std::atomic<int> bassOct{0};             // -1, 0, +1
    std::atomic<int> bassWave{0};            // 0 sierra, 1 cuadrada
    std::atomic<float> cutoff{0.3f}, reso{0.6f}, envMod{0.55f}, decay{0.35f}, accent{0.6f}, drive{0.25f}, bassVol{0.7f};
    // Sampler
    std::atomic<int> padMode[kPads];
    std::atomic<float> padVol[kPads];
    std::atomic<bool> padQuant{true};        // los loops entran en el siguiente pulso
    std::atomic<float> samplerVol{0.8f};
    // Bus de instrumentos
    std::atomic<float> instrVol{0.8f};
    std::atomic<bool> instrCue{false};
    InstrParams();
};

struct InstrStatus {
    std::atomic<int> drumStep{-1}, bassStep{-1};
    std::atomic<bool> drumRunning{false}, bassRunning{false}, drumArmed{false}, bassArmed{false};
    std::atomic<int> drumPatternNow{0}, bassPatternNow{0};
    std::atomic<float> bpm{0.f};
    std::atomic<int> clockDeck{-1};          // plato del que sale el pulso (-1 = tempo propio)
    std::atomic<bool> padPlaying[kPads];
    std::atomic<bool> padWaiting[kPads];     // loop esperando el pulso
    std::atomic<float> padPos[kPads];
    std::atomic<uint32_t> padGen[kPads];     // muestra que usa el audio (acuse de recibo)
    std::atomic<uint32_t> drumHits[kDrumTracks];
    std::atomic<float> peak{0.f};
    InstrStatus();
};

// Voces de la caja de ritmos (síntesis propia; no hay muestras de terceros).
class DrumVoices {
public:
    bool any() const { for (auto& v : v_) if (v.on) return true; return false; }
    void prepare(double fs);
    void trigger(int v, float vel, float tune, float decay);
    void render(float* L, float* R, int n, const float* level, const bool* mute);
private:
    double fs_ = 48000;
    struct V {
        bool on = false;
        float vel = 1, tune = 0.5f, decay = 0.5f;
        double ph = 0, ph2 = 0, t = 0;
        float env = 0, env2 = 0, penv = 0;
        double mph[6] = {};
        float hp = 0, hpIn = 0, bpz1 = 0, bpz2 = 0;
        uint32_t rng = 1;
    } v_[kDrumTracks];
    float noise(V& v) { v.rng ^= v.rng << 13; v.rng ^= v.rng >> 17; v.rng ^= v.rng << 5; return (float)(int32_t)v.rng * (1.f / 2147483648.f); }
};

// Sintetizador de bajos tipo acid: oscilador (sierra/cuadrada), filtro escalera de 4 polos
// con resonancia y saturación, envolventes, acento y slide.
class BassVoice {
public:
    void prepare(double fs);
    void noteOn(double freq, bool accent, bool slide);
    void noteOff();
    void render(float* out, int n, const InstrParams& p);
    bool active() const { return amp_ > 1e-5f || gate_; }
private:
    double fs_ = 48000, ph_ = 0, freq_ = 55, target_ = 55;
    bool gate_ = false, accent_ = false;
    float amp_ = 0, fenv_ = 0, accEnv_ = 0;
    float y_[4] = {}, ylast_ = 0;
    double glide_ = 0;
};

// Muestra de un pad (la decodifica la interfaz; el audio solo la lee).
struct PadSample { const float* pcm = nullptr; int64_t frames = 0; double rate = 48000; };

class Instruments {
public:
    void prepare(double fs);
    // Órdenes de los pads (desde la cola del motor): pulsar, soltar, parar todo, cambiar de muestra.
    void padPress(int pad);
    void padRelease(int pad);
    void padStopAll();
    void padSet(int pad, const Track* t, uint32_t gen);
    // Genera el bus de instrumentos (ya con sus volúmenes) a partir del reloj.
    void render(float* L, float* R, int n, const RackCtx& ctx);
    // Reloj del último bloque (para cuantizar la loop station y sincronizar efectos con la fuente «instrumentos»).
    void clockInfo(double& b0, double& b1, double& bpm) const { b0 = blockB0_; b1 = blockB1_; bpm = bpm_; }

    InstrParams params;
    InstrStatus status;
private:
    void clock(int n, const RackCtx& ctx, double* beat);
    void drumStepAt(long long k);
    void bassStepAt(long long k);
    double fs_ = 48000;
    double beat_ = 0, bpm_ = 124;
    bool following_ = false;
    double blockB0_ = 0, blockB1_ = 0;
    double beatArr_[256];
    // Caja de ritmos
    DrumVoices drums_;
    bool drumRun_ = false, drumArm_ = false;
    int drumPat_ = 0, lastDrumEvent_ = -1;
    long long drumBase_ = 0;                 // semicorchea en la que empezó el patrón actual
    // Bajo
    BassVoice bassV_;
    bool bassRun_ = false, bassArm_ = false;
    int bassPat_ = 0;
    long long bassBase_ = 0;
    int bassGateOff_ = -1;                   // muestra del bloque en la que suelta la nota
    double bassGateBeat_ = -1;               // posición (en semicorcheas) en la que se suelta
    // Sampler
    struct Pad { PadSample s; double pos = 0; bool playing = false, held = false, waiting = false, fading = false; float gain = 0, fade = 1; } pads_[kPads];
    float tmpL_[256], tmpR_[256], bass_[256];
    float lastVol_[4] = {0, 0, 0, 0};
    bool silent_ = true;                     // nada sonando: el bloque es silencio
    double lastQ_ = -1e9;
};

// Sonidos del sampler que trae el programa (sintetizados aquí; no hay muestras de terceros).
std::shared_ptr<Track> makeFxSample(int kind, uint32_t sr);
constexpr int kFxSamples = 8;
const char* fxSampleName(int kind);

} // namespace rdj

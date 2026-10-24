// Motor de mezcla. process() se llama desde el hilo de audio: no reserva memoria,
// no bloquea y solo se comunica por atómicos y colas SPSC.
#pragma once
#include "../core/spsc.h"
#include "color_fx.h"
#include "dsp.h"
#include "effects.h"
#include "rack.h"
#include "track.h"
#include "retrospective.h"
#include <array>
#include <atomic>
#include "stretch.h"
#include <memory>
#include <vector>

namespace rdj {

constexpr int kNumDecks = 2;
constexpr int kMaxBlock = 256;

// Parámetros que la interfaz escribe y el audio lee (valores sueltos, sin orden).
struct DeckParams {
    std::atomic<float> pitch{0.f};        // fracción: 0,05 = +5 %
    std::atomic<float> bend{0.f};         // empuje temporal (botones ±)
    std::atomic<float> gainDb{0.f};       // ganancia de entrada, -12..+12
    std::atomic<float> fader{0.8f};       // volumen de canal, 0..1
    std::atomic<float> eqLow{0.5f}, eqMid{0.5f}, eqHigh{0.5f};  // 0..1, 0,5 = 0 dB
    std::atomic<float> filter{0.f};       // -1..1 (la ruleta FILTER/COLOR del canal)
    std::atomic<int> colorFx{0};          // 0.11: color de esa ruleta (ColorFxType: filtro, space, noise, crush)
    std::atomic<bool> pfl{false};         // preescucha por auriculares
    std::atomic<double> bpm{0.0};         // BPM de la pista (sin pitch); 0 = desconocido
    std::atomic<double> gridAnchorSec{0.0};
    std::atomic<bool> quantize{true};
    std::atomic<bool> scratching{false};
    std::atomic<float> scratchVelocity{0.f};  // 1 = velocidad normal
    std::atomic<bool> reverse{false}, slip{false}, vinyl{true};
    std::atomic<int> fxType{0};
    std::atomic<bool> fxOn{false};
    std::atomic<float> fxAmount{0.5f};
    std::atomic<float> fxBeats{1.f};
    std::atomic<bool> keyLock{false};     // mantener el tono al cambiar el tempo
    std::atomic<float> keyShift{0.f};     // semitonos (-6..+6)
    // SYNC: lo mantiene el motor bloque a bloque (tempo y fase siguen al otro plato,
    // incluidos sus empujones). Con SYNC el motor escribe "pitch".
    std::atomic<int> syncTo{-1};          // plato al que sigue; -1 = sin SYNC
    std::atomic<double> syncFactor{1.0};  // BPM propio × factor = BPM del otro (½, 1 o 2: tempo doble/mitad)
    std::atomic<bool> syncBar{false};     // alinear compases (4 pulsos) en vez de pulsos
    std::atomic<bool> syncPhase{true};    // false = SYNC solo de tempo (no corrige la fase)
};

// Estado que el audio publica para la interfaz.
struct DeckStatus {
    std::atomic<double> posFrames{0};     // posición en fotogramas de la pista
    std::atomic<bool> playing{false};
    std::atomic<bool> hasTrack{false};
    std::atomic<bool> endReached{false};
    std::atomic<double> cueFrames{0};
    std::array<std::atomic<double>, kNumHotCues> hotCues;  // <0 vacío
    std::atomic<bool> loopActive{false};
    std::atomic<double> loopIn{-1}, loopOut{-1};
    std::atomic<bool> rolling{false}, slipActive{false};
    std::atomic<double> slipFrames{0};
    std::atomic<float> peakL{0}, peakR{0};
    std::atomic<uint32_t> loadedGen{0};
    std::atomic<float> syncError{0};      // desfase que corrige el SYNC (pulsos, + = adelantado)
    std::atomic<bool> syncLocked{false};  // SYNC activo y en fase
    DeckStatus() { for (auto& h : hotCues) h.store(-1.0); }
};

enum class Cmd : uint8_t {
    Load, Unload, TogglePlay, Play, Pause, CueDown, CueUp, Seek, SetCueHere,
    HotCue, HotCueDelete, LoopIn, LoopOut, AutoLoop, LoopHalve, LoopDouble, LoopExit, Reloop,
    BeatJump, SyncPhase, SetCueAt,
    StemLoad, StemPlay, StemPause, StemSeek, RollStart, RollEnd, Backspin, Brake,
    SetHotCueAt,  // i = hot cue, d = posición (fotogramas; se cuantiza si Q está activo)
    RackLoop,     // loop station del rack: i = orden (Rack::LoopCmd), d = argumento (compases)
    RackPad,      // sampler del rack
    RecallLoop,   // d = inicio, end = final en fotogramas
    DrumSample, DrumHit
};

struct Command {
    Cmd cmd;
    uint8_t deck = 0;
    int i = 0;           // índice (hot cue) o número
    double d = 0;        // valor (posición, pulsos...)
    Track* track = nullptr;
    uint32_t gen = 0;
    const struct StemSet* stems = nullptr;  // StemLoad
    double end = 0; // RecallLoop
};

// Reproductor de stems (modo Separador): hasta 6 pistas sincronizadas.
constexpr int kStemSlots = 6;
struct StemSet {
    Track* tracks[kStemSlots] = {};
    int count = 0;
};
struct StemParams {
    std::array<std::atomic<float>, kStemSlots> gain;   // 0..1.5
    std::array<std::atomic<bool>, kStemSlots> mute, solo;
    std::atomic<float> volume{0.9f};
    std::atomic<bool> pfl{false};                     // también a los auriculares
    std::atomic<float> bpm{120.f}; // reloj de efectos del Separador, ajustable
    StSlotParams fx[kStemSlots];
    std::array<std::atomic<float>, kStemSlots> low{}, mid{}, high{};
    StemParams() { for (int i = 0; i < kStemSlots; ++i) { gain[i] = 1.f; mute[i] = false; solo[i] = false; low[i] = mid[i] = high[i] = .5f; } }
};
struct StemStatus {
    std::atomic<double> posFrames{0};
    std::atomic<bool> playing{false};
    std::atomic<uint32_t> loadedGen{0};
    std::array<std::atomic<float>, kStemSlots> peak;
    StemStatus() { for (auto& p : peak) p = 0.f; }
};

enum class HpRouting : int { Ninguna = 0, Canales34 = 1, DispositivoAparte = 2, DivisionMono = 3 };

struct MixerParams {
    std::atomic<float> crossfader{0.5f};
    std::atomic<int> xfCurve{(int)XfCurve::Suave};
    std::atomic<float> masterVol{0.8f};
    std::atomic<float> hpVol{0.7f};
    std::atomic<float> hpMix{0.0f};  // 0 = solo preescucha, 1 = solo master
    std::atomic<int> hpRouting{(int)HpRouting::Ninguna};
    std::atomic<bool> cueClick{false};
    std::atomic<int> cueClickDeck{0};
    std::atomic<float> cueClickVol{.15f};
    std::atomic<bool> recording{false};
};

struct MixerStatus {
    std::atomic<float> masterPeakL{0}, masterPeakR{0}, masterRmsL{0}, masterRmsR{0};
    std::atomic<float> limiterGain{1.f};          // ganancia mínima del último bloque
    std::atomic<uint32_t> limiterHits{0};         // bloques con reducción > 1 dB
    std::atomic<float> preLimiterPeak{0};
    std::atomic<float> cpuLoad{0};                // fracción del tiempo disponible
    std::atomic<uint64_t> framesRendered{0};
    std::atomic<uint64_t> recDropped{0};          // muestras de grabación perdidas
    std::atomic<uint64_t> hpDropped{0};
};

class DeckEngine {
public:
    DeckEngine() { hot_.fill(-1.0); }
    void prepare(double fs);
    void handle(const Command& c, DeckParams& p, DeckStatus& s);
    // Renderiza n fotogramas estéreo a L/R (ya procesados: gain, EQ, filtro, FX).
    void render(float* L, float* R, int n, DeckParams& p, DeckStatus& s);
    double beatLenFrames(const DeckParams& p) const;   // en fotogramas de la pista
    double beatPhase(const DeckParams& p) const;       // [0,1)
    double effectiveBpm(const DeckParams& p) const;
    double beatPosition(const DeckParams& p) const;    // pulsos desde el ancla de la rejilla (0 sin BPM)
    double position() const { return pos_; }
    void jumpTo(double f);
    bool playing() const { return playing_; }
    // Gesto en curso que manda sobre el tempo (scratch, reverse, roll, freno…): el SYNC no corrige.
    bool inGesture(const DeckParams& p) const;
    uint32_t jumpCount() const { return jumps_; }
    void setSyncCorrection(double c) { syncCorr_ = c; }

    Track* track() const { return track_; }

private:
    double snapToBeat(double f, const DeckParams& p) const;
    inline void sampleAt(double pos, float& l, float& r) const;
    void publish(DeckStatus& s);

    double fs_ = 48000;
    Track* track_ = nullptr;
    double pos_ = 0;
    uint32_t jumps_ = 0;         // saltos de posición (el SYNC realinea si hace falta)
    double syncCorr_ = 0;        // corrección de tempo del SYNC (fracción, pequeña)
    bool playing_ = false;
    bool cueHeld_ = false;       // reproducción mientras se mantiene CUE
    double cue_ = 0;
    std::array<double, kNumHotCues> hot_{};
    bool loopActive_ = false;
    double loopIn_ = -1, loopOut_ = -1;
    float playGain_ = 0;         // rampa de arranque/parada (anticlic)
    bool rolling_ = false, savedLoop_ = false, rollWasPlaying_ = false;
    double savedIn_ = -1, savedOut_ = -1, rollPos_ = 0;
    bool slipping_ = false, slipPlaying_ = false, lastReverse_ = false;
    double slipPos_ = 0;
    int motion_ = 0;
    double motionLeft_ = 0, motionTotal_ = 1;
    bool motionWasPlaying_ = false;
    float scratchVel_ = 0;
    bool wasScratching_ = false;
    double lastRate_ = 1.0;
    // Fundido cruzado tras un salto (anticlic).
    double xfPos_ = 0, xfRate_ = 0;
    float xfGain_ = 1.f;
    int xfRemain_ = 0;
    void startTail() { xfPos_ = pos_; xfRate_ = lastRate_; xfGain_ = playGain_; xfRemain_ = kXf; }
    static constexpr int kXf = 192;
    // Key lock / cambio de tono (Signalsmith Stretch). El lector "feed" va por
    // delante de lo que se oye exactamente la latencia del estirador, así la
    // posición publicada (pos_) y la sincronización siguen siendo exactas.
    void primeStretch(double rate);
    bool advanceWrap(double& p, double rate) const;  // avanza con loop; true si dio la vuelta
    PitchStretcher stretch_;
    void restartStretch();  // tras cambiar el loop: volver a cebar desde lo que se oye
    bool stretchActive_ = false, needPrime_ = true;
    double feedPos_ = 0;
    std::vector<double> hist_;  // posición de lectura de cada fotograma enviado al estirador
    uint64_t histW_ = 0;
    int primeLen_ = 0;
    std::vector<float> primeL_, primeR_;
    float inL_[kMaxBlock], inR_[kMaxBlock], gArr_[kMaxBlock], tArr_[kMaxBlock], tailL_[kMaxBlock], tailR_[kMaxBlock], dirL_[kMaxBlock], dirR_[kMaxBlock];
    // Cadena de procesado.
    ThreeBandEq eq_;
    ColorFx color_;              // 0.11: la ruleta FILTER del canal, con sus colores (antes, solo DjFilter)
    FxSlot fx_;
    StUnit fxUnit_;              // 0.11: efectos nuevos del plato (riser, spiral, sidechain…)
    double fxFreeBeat_ = 0;      // reloj libre de esos efectos si el plato no tiene rejilla o está parado
    float lastGl_ = 1, lastGm_ = 1, lastGh_ = 1, lastTrim_ = 1;
};

class Engine {
public:
    Engine();
    void prepare(double fs);          // llamar solo con el audio parado
    double sampleRate() const { return fs_; }

    // Hilo de interfaz.
    bool send(const Command& c) { return queue_.push(c); }
    DeckParams deck[kNumDecks];
    DeckStatus deckStatus[kNumDecks];
    MixerParams mixer;
    MixerStatus status;
    SpscFloatRing recRing;   // master post-limitador, estéreo intercalado
    SpscFloatRing hpRing;    // auriculares cuando van a otro dispositivo
    StemParams stem;
    StemStatus stemStatus;
    Retrospective retrospective; // últimos 30 segundos del master
    Rack rack;               // rack de hardware virtual (rack.h): params/status y memoria de loops

    // Hilo de audio. out tiene outChannels canales intercalados.
    void process(float* out, int frames, int outChannels);

    // Lectura segura del BPM efectivo desde la interfaz (usa atómicos).
    double effectiveBpm(int d) const;

private:
    void processBlock(float* out, int n, int outChannels);
    // SYNC: iguala el tempo y corrige la fase de los platos que siguen a otro.
    void updateSync(int n);
    // Desfase de "me" respecto a "other" en unidades de alineación (pulso o compás del otro), en [-0,5, 0,5).
    bool phaseError(int me, int other, double factor, double& err, double& unitFrames) const;
    static double alignedPosition(double pos, double err, double unit);
    struct SyncState { int target = -1; double offset = 0; bool nudging = false, ownGesture = false, otherGesture = false; uint32_t otherJumps = 0, ownJumps = 0; int bigErrBlocks = 0; };
    SyncState sync_[kNumDecks];

    double fs_ = 48000;
    SpscQueue<Command> queue_{1024};
    DeckEngine decks_[kNumDecks];
    CueMetronome cueClick_;
    Limiter limiter_, hpLimiter_;
    float dL_[kNumDecks][kMaxBlock], dR_[kNumDecks][kMaxBlock];
    float mL_[kMaxBlock], mR_[kMaxBlock], cL_[kMaxBlock], cR_[kMaxBlock], hL_[kMaxBlock], hR_[kMaxBlock];
    float inter_[kMaxBlock * 2];
    float lastXfA_ = 1, lastXfB_ = 1, lastMaster_ = 0.8f, lastFader_[kNumDecks] = {0.8f, 0.8f};
    // Reproductor de stems
    void renderStems(int n);
    StUnit stemFx_[kStemSlots];
    ThreeBandEq stemEq_[kStemSlots];
    float stemEqGain_[kStemSlots][3] = {};
    StemSet stemSet_;
    double stemPos_ = 0;
    bool stemPlaying_ = false;
    float stemGain_ = 0.f, stemLastGain_[kStemSlots] = {};
    float sL_[kMaxBlock], sR_[kMaxBlock];
};

} // namespace rdj

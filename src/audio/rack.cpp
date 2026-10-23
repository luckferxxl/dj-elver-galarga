#include "rack.h"
#include <chrono>
#include <cstdio>
#include <cstring>

namespace rdj {

// ================================================================== nombres

const char* rackSrcName(int s) {
    switch (s) {
    case 0: return "Plato A";
    case 1: return "Plato B";
    case 2: return "Mezcla";
    case 3: return "Instrumentos";
    default: return "?";
    }
}

const char* stFxName(int t) {
    switch ((StFx)t) {
    case StFx::Ninguno: return "Vacía";
    case StFx::Filtro: return "Filtro";
    case StFx::Eco: return "Eco";
    case StFx::Delay: return "Delay";
    case StFx::Reverb: return "Reverb";
    case StFx::Flanger: return "Flanger";
    case StFx::Phaser: return "Phaser";
    case StFx::Gate: return "Gate";
    case StFx::Saturacion: return "Saturación";
    case StFx::Distorsion: return "Distorsión";
    case StFx::EcoCinta: return "Eco de cinta";
    case StFx::ReverbLarga: return "Reverb larga";
    case StFx::Bitcrusher: return "Bitcrusher";
    case StFx::Repetidor: return "Repetidor";
    case StFx::Riser: return "Riser de ruido";
    case StFx::Spiral: return "Spiral";
    case StFx::Sidechain: return "Sidechain";
    case StFx::Shimmer: return "Shimmer";
    case StFx::Vinilo: return "Vinilo lo-fi";
    case StFx::Telefono: return "Teléfono / radio";
    case StFx::Chorus: return "Chorus";
    case StFx::Tremolo: return "Trémolo";
    case StFx::AutoPan: return "Auto-pan";
    case StFx::Freeze: return "Freeze";
    case StFx::Slicer: return "Slicer";
    default: return "?";
    }
}

const char* stFxDescription(int t) {
    switch ((StFx)t) {
    case StFx::Ninguno: return "Ranura sin efecto.";
    case StFx::Filtro: return "Filtro DJ: a la izquierda paso bajo, a la derecha paso alto.";
    case StFx::Eco: return "Eco sincronizado; cada repetición, algo más oscura.";
    case StFx::Delay: return "Delay que rebota entre izquierda y derecha.";
    case StFx::Reverb: return "Reverb de sala.";
    case StFx::Flanger: return "Flanger sincronizado con el BPM.";
    case StFx::Phaser: return "Phaser sincronizado con el BPM.";
    case StFx::Gate: return "Corta el sonido a tempo (trance gate).";
    case StFx::Saturacion: return "Saturación cálida con compensación de nivel.";
    case StFx::Distorsion: return "Distorsión fuerte con recorte asimétrico.";
    case StFx::EcoCinta: return "Eco de cinta con vaivén y saturación.";
    case StFx::ReverbLarga: return "Reverb enorme para transiciones, sin graves.";
    case StFx::Bitcrusher: return "Menos bits y menos muestras: sonido roto.";
    case StFx::Repetidor: return "Repite el último trozo mientras está encendido.";
    case StFx::Riser: return "Ruido blanco que sube con un filtro durante los compases elegidos. Al apagarlo se corta: suéltalo en el drop.";
    case StFx::Spiral: return "Eco cuyas repeticiones suben de tono una y otra vez. Para salir de un tema.";
    case StFx::Sidechain: return "Bombeo: baja el volumen en cada pulso y lo recupera, como el house con compresor.";
    case StFx::Shimmer: return "Reverb con una octava por encima que crece sola. Para los breaks.";
    case StFx::Vinilo: return "Disco viejo: crujido, vaivén de velocidad y agudos recortados.";
    case StFx::Telefono: return "Deja solo los medios: de radio AM (izquierda) a teléfono (derecha).";
    case StFx::Chorus: return "Engorda y abre el sonido con dos voces que oscilan.";
    case StFx::Tremolo: return "Sube y baja el volumen a tempo.";
    case StFx::AutoPan: return "Lleva el sonido de un lado a otro a tempo.";
    case StFx::Freeze: return "Congela un trozo de lo que suena y lo deja sonando mientras está encendido.";
    case StFx::Slicer: return "Trocea el último compás en 8 partes y las reordena a tempo.";
    default: return "";
    }
}

const char* stFxId(int t) {
    static const char* ids[] = {"ninguno", "filtro", "eco", "delay", "reverb", "flanger", "phaser", "gate", "saturacion", "distorsion",
                                "ecocinta", "reverblarga", "bitcrusher", "repetidor",
                                "riser", "spiral", "sidechain", "shimmer", "vinilo", "telefono", "chorus", "tremolo", "autopan", "freeze", "slicer"};
    static_assert(sizeof(ids) / sizeof(ids[0]) == (size_t)StFx::Count, "falta el identificador de algún efecto");
    return t >= 0 && t < (int)StFx::Count ? ids[t] : "ninguno";
}

int stFxFromId(const std::string& id) {
    for (int t = 0; t < (int)StFx::Count; ++t) if (id == stFxId(t)) return t;
    return 0;
}

const char* stFxAmountLabel(int t) {
    switch ((StFx)t) {
    case StFx::Filtro: return "FILTRO";
    case StFx::Eco: case StFx::Delay: return "REPETIC.";
    case StFx::Reverb: return "TAMAÑO";
    case StFx::Flanger: case StFx::Phaser: return "PROFUND.";
    case StFx::Gate: return "CORTE";
    case StFx::Saturacion: case StFx::Distorsion: return "DRIVE";
    case StFx::EcoCinta: return "REPETIC.";
    case StFx::ReverbLarga: return "TAMAÑO";
    case StFx::Bitcrusher: return "ROMPER";
    case StFx::Repetidor: return "CAÍDA";
    case StFx::Riser: return "RUIDO";
    case StFx::Spiral: return "SUBIDA";
    case StFx::Sidechain: return "BOMBEO";
    case StFx::Shimmer: return "BRILLO";
    case StFx::Vinilo: return "DESGASTE";
    case StFx::Telefono: return "BANDA";
    case StFx::Chorus: case StFx::Tremolo: return "PROFUND.";
    case StFx::AutoPan: return "ANCHO";
    case StFx::Freeze: return "TAMAÑO";
    case StFx::Slicer: return "PATRÓN";
    default: return "INTENS.";
    }
}

bool stFxHasTail(int t) {
    return t == (int)StFx::Eco || t == (int)StFx::Delay || t == (int)StFx::Reverb || t == (int)StFx::EcoCinta || t == (int)StFx::ReverbLarga ||
           t == (int)StFx::Spiral || t == (int)StFx::Shimmer;
}
bool stFxUsesTime(int t) {
    return t == (int)StFx::Eco || t == (int)StFx::Delay || t == (int)StFx::Flanger || t == (int)StFx::Phaser || t == (int)StFx::Gate ||
           t == (int)StFx::EcoCinta || t == (int)StFx::Repetidor || t == (int)StFx::Riser || t == (int)StFx::Spiral || t == (int)StFx::Sidechain ||
           t == (int)StFx::Tremolo || t == (int)StFx::AutoPan || t == (int)StFx::Slicer;
}
bool stFxNeedsHistory(int t) { return t == (int)StFx::Repetidor || t == (int)StFx::Freeze || t == (int)StFx::Slicer; }

bool stFxDivAllowed(int t, int div) {
    if (div < 0 || div >= kStDivs) return false;
    switch ((StFx)t) {
    case StFx::Riser: return div >= 6;                  // 1, 2, 4 u 8 compases
    case StFx::Slicer: return div >= 6 && div <= 8;     // 1, 2 o 4 pulsos (un compás)
    case StFx::Sidechain: return div >= 2 && div <= 8;  // de semicorchea a compás
    default: return true;
    }
}

int stFxDefaultDiv(int t) {
    switch ((StFx)t) {
    case StFx::Riser: return 8;      // 4 compases
    case StFx::Slicer: return 8;     // 4 pulsos = 1 compás
    case StFx::Spiral: return 5;     // 3/4
    case StFx::Tremolo: return 2;    // 1/4
    case StFx::AutoPan: return 7;    // 2 pulsos
    default: return 6;               // 1 pulso
    }
}

std::string stFxTimeText(int t, int div) {
    const char* n = stDivName(div);
    if (t == (int)StFx::Riser) return std::string(n) + (div > 6 ? " compases" : " compás");
    return std::string(n) + (div > 6 ? " pulsos" : " pulso");
}

int slicerPattern(float amount) { return std::clamp((int)(std::clamp(amount, 0.f, 1.f) * kSlicerPatterns), 0, kSlicerPatterns - 1); }

namespace {
// Semitonos que sube cada repetición del spiral (la ruleta va a saltos musicales).
float spiralSemis(float a) { return a < 0.2f ? 1.f : a < 0.4f ? 2.f : a < 0.6f ? 5.f : a < 0.8f ? 7.f : 12.f; }
const int kSlicerSeq[kSlicerPatterns][8] = {
    {0, 1, 2, 3, 4, 5, 6, 7},   // en orden (repite el compás)
    {0, 1, 2, 3, 0, 1, 2, 3},   // la primera mitad dos veces
    {0, 0, 2, 2, 4, 4, 6, 6},   // cada trozo doble
    {0, 1, 0, 1, 4, 5, 6, 7},   // tartamudeo al principio
    {6, 7, 4, 5, 2, 3, 0, 1},   // parejas al revés
    {0, 3, 6, 1, 4, 7, 2, 5},   // salteado
    {7, 6, 5, 4, 3, 2, 1, 0},   // trozos de atrás adelante
    {0, 0, 0, 0, 0, 0, 6, 7},   // redoble
};
}

std::string stFxAmountText(int t, float a) {
    char b[64];
    switch ((StFx)t) {
    case StFx::Filtro:
        std::snprintf(b, sizeof b, a < 0.48f ? "Paso bajo %.0f %%" : a > 0.52f ? "Paso alto %.0f %%" : "Sin filtro", std::fabs(a - 0.5f) * 200);
        break;
    case StFx::Spiral: std::snprintf(b, sizeof b, "Sube %+.0f semitonos por repetición", spiralSemis(a)); break;
    case StFx::Slicer: std::snprintf(b, sizeof b, "Patrón %d de %d", slicerPattern(a) + 1, kSlicerPatterns); break;
    case StFx::Freeze: std::snprintf(b, sizeof b, "Trozo de %.0f ms", 30.0 * std::pow(500.0 / 30.0, (double)a)); break;
    case StFx::Telefono: std::snprintf(b, sizeof b, a < 0.5f ? "Radio %.0f %%" : "Teléfono %.0f %%", a < 0.5f ? (1 - a * 2) * 100 : (a * 2 - 1) * 100); break;
    default: std::snprintf(b, sizeof b, "%s %.0f %%", stFxAmountLabel(t), a * 100); break;
    }
    return b;
}

const char* stDivName(int i) {
    static const char* n[kStDivs] = {"1/16", "1/8", "1/4", "1/3", "1/2", "3/4", "1", "2", "4", "8"};
    return i >= 0 && i < kStDivs ? n[i] : "?";
}

std::string xyTargetName(int t) {
    if (t == XY_None) return "Nada";
    if (t == XY_Macro) return "Macro";
    if (t >= XY_Amount0 && t < XY_Amount0 + kStSlots) return "Ranura " + std::to_string(t - XY_Amount0 + 1) + ": intensidad";
    if (t >= XY_Mix0 && t < XY_Mix0 + kStSlots) return "Ranura " + std::to_string(t - XY_Mix0 + 1) + ": mezcla";
    if (t == XY_IsoLow) return "Aislador: graves";
    if (t == XY_IsoMid) return "Aislador: medios";
    if (t == XY_IsoHigh) return "Aislador: agudos";
    return "?";
}

const char* xyTargetId(int t) {
    static const char* ids[XY_Count] = {"nada", "macro", "int1", "int2", "int3", "int4", "mez1", "mez2", "mez3", "mez4", "graves", "medios", "agudos"};
    return t >= 0 && t < XY_Count ? ids[t] : "nada";
}

int xyTargetFromId(const std::string& id) {
    for (int t = 0; t < XY_Count; ++t) if (id == xyTargetId(t)) return t;
    return XY_None;
}

const char* loopStateName(int s) {
    switch ((LoopState)s) {
    case LoopState::Vacio: return "Vacía";
    case LoopState::Armado: return "Esperando el compás";
    case LoopState::Grabando: return "Grabando";
    case LoopState::Parado: return "Lista";
    case LoopState::ArmadoPlay: return "Entra en el compás";
    case LoopState::Reproduciendo: return "Sonando";
    case LoopState::Sobregrabando: return "Sobregrabando";
    default: return "?";
    }
}

// ================================================================== StUnit

namespace {
inline float fin(float v, float def) { return std::isfinite(v) ? v : def; }   // parámetro no finito → valor seguro
constexpr int kTailMaxSec = 12;      // tope de cola tras apagar (la reverb más larga cabe)
constexpr float kQuiet = 1e-5f;      // ≈ -100 dB: la cola se da por terminada
inline float dryGain(float mix) { return std::min(1.f, 2.f * (1.f - mix)); }
inline float wetGain(float mix) { return std::min(1.f, 2.f * mix); }
}

// ================================================================== GrainShifter

void GrainShifter::prepare(int size) {
    int s = 1;
    while (s < size) s <<= 1;
    l_.assign(s, 0.f); r_.assign(s, 0.f);
    mask_ = s - 1;
    reset();
}

void GrainShifter::reset() {
    std::fill(l_.begin(), l_.end(), 0.f);
    std::fill(r_.begin(), r_.end(), 0.f);
    w_ = 0; ph_ = 0;
}

inline void GrainShifter::process(float inL, float inR, float& outL, float& outR, float ratio, float window) {
    l_[w_ & mask_] = inL; r_[w_ & mask_] = inR;
    // El retardo de cada cabeza cambia a razón de (1 − ratio) muestras por muestra: así suena ratio veces más agudo.
    ph_ += (1.0 - ratio) / window;
    ph_ -= std::floor(ph_);
    float ol = 0, orr = 0;
    for (int h = 0; h < 2; ++h) {
        double p = ph_ + 0.5 * h;
        p -= std::floor(p);
        const double rp = (double)w_ - (1.0 + p * window);
        const long long i0 = (long long)std::floor(rp);
        const float fr = (float)(rp - (double)i0);
        const float sn = (float)std::sin(kPi * p), win = sn * sn;   // las dos ventanas suman 1
        ol += win * (l_[i0 & mask_] + (l_[(i0 + 1) & mask_] - l_[i0 & mask_]) * fr);
        orr += win * (r_[i0 & mask_] + (r_[(i0 + 1) & mask_] - r_[i0 & mask_]) * fr);
    }
    ++w_;
    outL = ol; outR = orr;
}

// ================================================================== StUnit (efectos)

void StUnit::prepare(double fs, uint32_t seed) {
    fs_ = fs;
    seed_ = seed ? seed : 1u;
    shift_.prepare(8192);
    hbCap_ = (int)(fs * 2.75) + 8;
    hbL_.assign(hbCap_, 0.f); hbR_.assign(hbCap_, 0.f);
    filter_.prepare(fs);
    reverb_.prepare(fs);
    flanger_.prepare(fs);
    perf_.prepare(fs);
    line_.size = 1;
    while (line_.size < (int)(fs * 4.5)) line_.size <<= 1;   // 4 s de retardo como máximo
    line_.l.assign(line_.size, 0.f);
    line_.r.assign(line_.size, 0.f);
    curType_ = 0;
    gOn_ = 0.f;
    reset();
}

void StUnit::reset() {
    filter_.reset();
    reverb_.reset();
    flanger_.reset();
    perf_.reset();
    std::fill(line_.l.begin(), line_.l.end(), 0.f);
    std::fill(line_.r.begin(), line_.r.end(), 0.f);
    line_.w = 0; line_.cur = 0; line_.lpL = line_.lpR = line_.hpL = line_.hpR = 0;
    satLpL_ = satLpR_ = 0;
    wow_ = flutter_ = 0; revHpL_ = revHpR_ = 0;
    crushL_ = crushR_ = 0; crushCount_ = 0;
    repeating_ = false; repStart_ = repLen_ = repPos_ = 0; repCount_ = 0;
    tail_ = 0; quiet_ = 0;
    for (auto& f : svf_) f.reset();
    shift_.reset();
    std::memset(fbRing_, 0, sizeof fbRing_);
    fbW_ = 0;
    riseT_ = 0; smG_ = 1.f; smP_ = 0.f; modPh_ = 0;
    clickEnv_ = clickAmp_ = 0;
    envIn_ = envOut_ = 0;
    rng_ = seed_;
    fill_ = 0; held_ = false; hbLen_ = 0; heldLen_ = heldPh_ = heldRegPh_ = 0;
}

// Rampas por muestra: encendido (15 ms) y cambio de tipo (10 ms de fundido de todo el efecto,
// también de la cola; luego se cambia, se limpia y vuelve a entrar).
void StUnit::advanceGates(int type, bool on, int n) {
    const float onStep = (float)(1.0 / (0.015 * fs_)), xStep = (float)(1.0 / (0.010 * fs_));
    const bool switching = type != curType_;
    const float target = on && !switching ? 1.f : 0.f;
    for (int i = 0; i < n; ++i) {
        if (switching) {
            xg_ = std::max(0.f, xg_ - xStep);
            if (xg_ <= 0.f && curType_ != type) { curType_ = type; reset(); gOn_ = 0.f; }
        } else xg_ = std::min(1.f, xg_ + xStep);
        if (gOn_ < target) gOn_ = std::min(target, gOn_ + onStep);
        else if (gOn_ > target) gOn_ = std::max(target, gOn_ - onStep);
        gArr_[i] = gOn_; xArr_[i] = xg_;
    }
}

// Señal del efecto para la entrada in: completa (filtro, flanger…) o solo la parte húmeda (eco, delay, reverb).
void StUnit::wet(const float* inL, const float* inR, int n, float a0, float a1, float beats, double bpm) {
    std::memcpy(wL_, inL, sizeof(float) * n);
    std::memcpy(wR_, inR, sizeof(float) * n);
    const float amt = a1;
    const float inv = n > 0 ? 1.f / n : 0.f;
    FxContext c; c.fs = fs_; c.bpm = bpm;
    switch ((StFx)curType_) {
    case StFx::Filtro: filter_.process(wL_, wR_, n, std::clamp(amt * 2.f - 1.f, -1.f, 1.f)); break;
    case StFx::Flanger: flanger_.process(wL_, wR_, n, c, beats, 1.f, 0.3f + 0.7f * amt, 0.3f + 0.55f * amt); break;
    case StFx::Phaser: perf_.process(wL_, wR_, n, FxType::Phaser, 1.f, beats, bpm, 0.3f + 0.7f * amt); break;
    case StFx::Gate: perf_.process(wL_, wR_, n, FxType::Gate, 1.f, beats, bpm, amt); break;
    case StFx::Saturacion:
        for (int i = 0; i < n; ++i) {
            const float a = a0 + (a1 - a0) * (i + 1) * inv;
            const float d = 1.f + a * 5.f, mk = 1.f + a * 0.6f;
            wL_[i] = std::tanh(wL_[i] * d) / d * mk;
            wR_[i] = std::tanh(wR_[i] * d) / d * mk;
        }
        break;
    case StFx::Distorsion: {
        const float k = (float)(1.0 - std::exp(-2 * kPi * 6500.0 / fs_));   // paso bajo suave para quitar aspereza
        for (int i = 0; i < n; ++i) {
            const float a = a0 + (a1 - a0) * (i + 1) * inv;
            const float d = 2.f + a * 28.f, out = 0.55f;
            float l = wL_[i] * d, r = wR_[i] * d;
            l = (l >= 0 ? l / (1.f + l) : l / (1.f - 0.7f * l)) * out;   // recorte asimétrico
            r = (r >= 0 ? r / (1.f + r) : r / (1.f - 0.7f * r)) * out;
            satLpL_ += (l - satLpL_) * k; satLpR_ += (r - satLpR_) * k;
            wL_[i] = satLpL_; wR_[i] = satLpR_;
        }
        break;
    }
    case StFx::Eco: case StFx::Delay: {
        // Retardo sincronizado: la duración se desliza al cambiar (como una cinta), sin saltos.
        const bool pingPong = curType_ == (int)StFx::Delay;
        double target = 60.0 / (bpm > 1 ? bpm : 120.0) * beats * fs_;
        target = std::clamp(target, 32.0, (double)line_.size - 8);
        if (line_.cur <= 0) line_.cur = target;
        const int mask = line_.size - 1;
        const float hpK = (float)(1.0 - std::exp(-2 * kPi * 90.0 / fs_));
        for (int i = 0; i < n; ++i) {
            const float a = a0 + (a1 - a0) * (i + 1) * inv;
            const float fb = pingPong ? 0.15f + 0.7f * a : 0.2f + 0.7f * a;   // tope 0,85 / 0,9
            line_.cur += (target - line_.cur) * 0.0005;
            const double rp = (double)line_.w - line_.cur;
            const long long i0 = (long long)std::floor(rp);
            const float fr = (float)(rp - (double)i0);
            const float dl = line_.l[i0 & mask] + (line_.l[(i0 + 1) & mask] - line_.l[i0 & mask]) * fr;
            const float dr = line_.r[i0 & mask] + (line_.r[(i0 + 1) & mask] - line_.r[i0 & mask]) * fr;
            float wl, wr;
            if (pingPong) {
                // Rebota entre canales; filtro paso alto en la realimentación para que no se acumulen graves.
                line_.hpL += (dl - line_.hpL) * hpK; line_.hpR += (dr - line_.hpR) * hpK;
                const float fl = dl - line_.hpL, fr2 = dr - line_.hpR;
                line_.l[line_.w & mask] = 0.5f * (inL[i] + inR[i]) + std::tanh(fr2 * fb);
                line_.r[line_.w & mask] = std::tanh(fl * fb);
                wl = dl; wr = dr;
            } else {
                // Eco de cinta: realimentación más oscura cada vuelta.
                line_.lpL += (dl - line_.lpL) * 0.55f; line_.lpR += (dr - line_.lpR) * 0.55f;
                line_.l[line_.w & mask] = inL[i] + std::tanh(line_.lpL * fb);
                line_.r[line_.w & mask] = inR[i] + std::tanh(line_.lpR * fb);
                wl = dl; wr = dr;
            }
            ++line_.w;
            wL_[i] = wl; wR_[i] = wr;
        }
        break;
    }
    case StFx::EcoCinta: {
        // Eco de cinta: retardo sincronizado con vaivén lento (wow) y rápido (flutter) de la
        // velocidad, saturación de cinta en la realimentación y cada vuelta más oscura.
        double target = 60.0 / (bpm > 1 ? bpm : 120.0) * beats * fs_;
        target = std::clamp(target, 64.0, (double)line_.size - fs_ * 0.01);
        if (line_.cur <= 0) line_.cur = target;
        const int mask = line_.size - 1;
        const double wInc = kPi * 2 * 0.55 / fs_, fInc = kPi * 2 * 6.3 / fs_;
        for (int i = 0; i < n; ++i) {
            const float a = a0 + (a1 - a0) * (i + 1) * inv;
            const float fb = 0.3f + 0.65f * a;   // hasta 0,95 (la saturación lo mantiene a raya)
            line_.cur += (target - line_.cur) * 0.0004;
            wow_ += wInc; flutter_ += fInc;
            if (wow_ > kPi * 2) wow_ -= kPi * 2;
            if (flutter_ > kPi * 2) flutter_ -= kPi * 2;
            const double mod = (0.0016 * std::sin(wow_) + 0.00025 * std::sin(flutter_)) * fs_;
            const double rp = (double)line_.w - line_.cur - mod;
            const long long i0 = (long long)std::floor(rp);
            const float fr = (float)(rp - (double)i0);
            const float dl = line_.l[i0 & mask] + (line_.l[(i0 + 1) & mask] - line_.l[i0 & mask]) * fr;
            const float dr = line_.r[i0 & mask] + (line_.r[(i0 + 1) & mask] - line_.r[i0 & mask]) * fr;
            line_.lpL += (dl - line_.lpL) * 0.42f; line_.lpR += (dr - line_.lpR) * 0.42f;
            line_.l[line_.w & mask] = std::tanh(1.3f * (inL[i] + line_.lpL * fb)) / 1.3f;
            line_.r[line_.w & mask] = std::tanh(1.3f * (inR[i] + line_.lpR * fb)) / 1.3f;
            ++line_.w;
            wL_[i] = std::tanh(dl * 1.2f) / 1.2f; wR_[i] = std::tanh(dr * 1.2f) / 1.2f;
        }
        break;
    }
    case StFx::ReverbLarga: {
        // Reverb para transiciones: sala enorme, sin graves en la entrada (no embarra el bombo)
        // y con pre-retardo de 50 ms. Con la intensidad al máximo la cola casi no cae.
        const int mask = line_.size - 1;
        const int pre = (int)(0.05 * fs_);
        const float hpK = (float)(1.0 - std::exp(-kPi * 2 * 250.0 / fs_));
        for (int i = 0; i < n; ++i) {
            revHpL_ += (inL[i] - revHpL_) * hpK; revHpR_ += (inR[i] - revHpR_) * hpK;
            line_.l[line_.w & mask] = inL[i] - revHpL_;
            line_.r[line_.w & mask] = inR[i] - revHpR_;
            wL_[i] = line_.l[(line_.w - pre) & mask];
            wR_[i] = line_.r[(line_.w - pre) & mask];
            ++line_.w;
        }
        float tl[kRackBlock], tr[kRackBlock];
        std::memcpy(tl, wL_, sizeof(float) * n); std::memcpy(tr, wR_, sizeof(float) * n);
        reverb_.process(wL_, wR_, n, 0.6f + 0.4f * amt, 1.f, true);
        for (int i = 0; i < n; ++i) { wL_[i] = (wL_[i] - tl[i]) * 1.15f; wR_[i] = (wR_[i] - tr[i]) * 1.15f; }
        break;
    }
    case StFx::Bitcrusher: {
        // Menos bits y menos frecuencia de muestreo, a la vez, con la ruleta.
        for (int i = 0; i < n; ++i) {
            const float a = a0 + (a1 - a0) * (i + 1) * inv;
            const float levels = std::pow(2.f, 12.f - 9.f * a);       // 12 → 3 bits
            const float hold = 1.f + 23.f * a * a;                     // 1 → 24 muestras
            if ((crushCount_ += 1.f) >= hold) {
                crushCount_ -= hold;
                crushL_ = std::round(wL_[i] * levels) / levels;
                crushR_ = std::round(wR_[i] * levels) / levels;
            }
            wL_[i] = crushL_; wR_[i] = crushR_;
        }
        break;
    }
    case StFx::Repetidor: {
        // Repetidor (beat repeat): mientras está encendida, repite el último trozo de la
        // duración elegida (sincronizado), cada vez un poco más bajo según la CAÍDA.
        const int mask = line_.size - 1;
        const long long want = (long long)std::clamp(60.0 / (bpm > 1 ? bpm : 120.0) * beats * fs_, 64.0, (double)line_.size - 64);
        for (int i = 0; i < n; ++i) {
            if (!repeating_) {
                line_.l[line_.w & mask] = inL[i]; line_.r[line_.w & mask] = inR[i];
                ++line_.w;
                if (gOn_ > 0.f && line_.w > want) { repeating_ = true; repLen_ = want; repStart_ = line_.w - want; repPos_ = 0; repCount_ = 0; }
                wL_[i] = inL[i]; wR_[i] = inR[i];
                continue;
            }
            if (gOn_ <= 0.f) { repeating_ = false; wL_[i] = inL[i]; wR_[i] = inR[i]; continue; }
            const float g = std::pow(1.f - 0.6f * a1, (float)repCount_);
            // Fundido de 2 ms en los bordes de cada repetición (sin chasquidos).
            const long long edge = std::min<long long>(96, repLen_ / 4);
            float w = 1.f;
            if (repPos_ < edge) w = (float)repPos_ / edge;
            else if (repPos_ > repLen_ - edge) w = (float)(repLen_ - repPos_) / edge;
            const long long idx = (repStart_ + repPos_) & mask;
            wL_[i] = line_.l[idx] * g * w; wR_[i] = line_.r[idx] * g * w;
            if (++repPos_ >= repLen_) { repPos_ = 0; ++repCount_; }
        }
        break;
    }
    case StFx::Reverb: {
        // La reverb de los platos suma su señal a la entrada: se queda solo con lo añadido.
        reverb_.process(wL_, wR_, n, 0.15f + 0.85f * amt, 1.f, true);
        for (int i = 0; i < n; ++i) { wL_[i] -= inL[i]; wR_[i] -= inR[i]; }
        break;
    }
    case StFx::Riser: {
        // Ruido blanco por un paso banda que sube de 300 Hz a 12 kHz durante los compases elegidos
        // (los «pulsos» de la división cuentan como compases) y luego se queda arriba. La música
        // pierde graves según sube. Cada vez que se enciende empieza desde abajo.
        const double bars = std::max(0.25, (double)beats);
        const double len = bars * 4.0 * 60.0 / (bpm > 1 ? bpm : 120.0) * fs_;
        const double inc = 1.0 / std::max(1.0, len);
        for (int i = 0; i < n; ++i) {
            const float a = a0 + (a1 - a0) * (i + 1) * inv;
            if (gArr_[i] > 0.f) riseT_ = std::min(1.0, riseT_ + inc);
            const float t = (float)riseT_;
            const float g = svfG(300.0 * std::pow(40.0, (double)t), fs_), k = 1.4f - 0.9f * t;
            const float lvl = (0.04f + 0.36f * a) * (0.1f + 0.9f * t * t);
            const float nl = svf_[0].bp(noise(), g, k) * lvl, nr = svf_[1].bp(noise(), g, k) * lvl;
            const float gh = svfG(20.0 + 230.0 * t * t, fs_);
            wL_[i] = svf_[2].hp(inL[i], gh, 1.41f) + nl;
            wR_[i] = svf_[3].hp(inR[i], gh, 1.41f) + nr;
        }
        break;
    }
    case StFx::Spiral: {
        // Eco sincronizado con un cambio de tono dentro del bucle: cada repetición sube un intervalo.
        constexpr float W = 1024.f;   // ventana del cambio de tono (≈ 21 ms a 48 kHz)
        double target = 60.0 / (bpm > 1 ? bpm : 120.0) * beats * fs_ - W * 0.5;   // compensa la latencia del cambio de tono
        target = std::clamp(target, 64.0, (double)line_.size - 8);
        if (line_.cur <= 0) line_.cur = target;
        const int mask = line_.size - 1;
        const float ratio = std::pow(2.f, spiralSemis(amt) / 12.f);
        const float gHp = svfG(160.0, fs_), gLp = svfG(9000.0, fs_);
        constexpr float fb = 0.72f;
        for (int i = 0; i < n; ++i) {
            line_.cur += (target - line_.cur) * 0.0005;
            const double rp = (double)line_.w - line_.cur;
            const long long i0 = (long long)std::floor(rp);
            const float fr = (float)(rp - (double)i0);
            const float dl = line_.l[i0 & mask] + (line_.l[(i0 + 1) & mask] - line_.l[i0 & mask]) * fr;
            const float dr = line_.r[i0 & mask] + (line_.r[(i0 + 1) & mask] - line_.r[i0 & mask]) * fr;
            // La repetición vuelve a entrar sin graves ni agudos extremos (no se embarra ni chilla).
            const float fl = svf_[2].lp(svf_[0].hp(dl, gHp, 1.2f), gLp, 1.2f);
            const float frr = svf_[3].lp(svf_[1].hp(dr, gHp, 1.2f), gLp, 1.2f);
            float sl, sr;
            shift_.process(inL[i] + fl * fb, inR[i] + frr * fb, sl, sr, ratio, W);
            line_.l[line_.w & mask] = std::tanh(sl);
            line_.r[line_.w & mask] = std::tanh(sr);
            ++line_.w;
            wL_[i] = dl; wR_[i] = dr;
        }
        break;
    }
    case StFx::Shimmer: {
        // Reverb que vuelve a entrar una octava más aguda (con un bloque de retraso): crece y brilla.
        constexpr int fm = 1023;
        const float fbG = 0.12f + 0.2f * amt;   // realimentación de la octava (la ganancia del bucle se queda muy por debajo de 1)
        const float gHp = svfG(400.0, fs_);
        for (int i = 0; i < n; ++i) {
            const long long r = fbW_ + i - kRackBlock;
            wL_[i] = inL[i] + fbRing_[0][r & fm];
            wR_[i] = inR[i] + fbRing_[1][r & fm];
        }
        float tl[kRackBlock], tr[kRackBlock];
        std::memcpy(tl, wL_, sizeof(float) * n); std::memcpy(tr, wR_, sizeof(float) * n);
        reverb_.process(wL_, wR_, n, 0.5f + 0.3f * amt, 1.f, true);
        for (int i = 0; i < n; ++i) {
            const float rl = wL_[i] - tl[i], rr = wR_[i] - tr[i];
            float sl, sr;
            shift_.process(rl, rr, sl, sr, 2.f, 2048.f);
            sl = svf_[0].hp(sl, gHp, 1.2f); sr = svf_[1].hp(sr, gHp, 1.2f);
            fbRing_[0][(fbW_ + i) & fm] = std::tanh(sl * fbG);
            fbRing_[1][(fbW_ + i) & fm] = std::tanh(sr * fbG);
            wL_[i] = (rl + sl * 0.5f * amt) * 0.6f;   // al nivel de la reverb larga
            wR_[i] = (rr + sr * 0.5f * amt) * 0.6f;
        }
        fbW_ += n;
        break;
    }
    case StFx::Vinilo: {
        // Disco gastado: vaivén de la velocidad (33 rpm = 0,55 Hz), imagen más estrecha, banda
        // recortada, algo de saturación, crujidos sueltos y siseo. Más «desgaste», más de todo.
        const int mask = line_.size - 1;
        const double wInc = kPi * 2 * 0.55 / fs_, fInc = kPi * 2 * 7.0 / fs_;
        const float gl = svfG(11000.0 * std::pow(0.3, (double)amt), fs_), gh = svfG(30.0 + 120.0 * amt, fs_);
        const float gc = svfG(1500.0, fs_), gs = svfG(5000.0, fs_);
        const float clickDecay = (float)std::exp(-1.0 / (0.0004 * fs_));
        for (int i = 0; i < n; ++i) {
            const float a = a0 + (a1 - a0) * (i + 1) * inv;
            line_.l[line_.w & mask] = inL[i]; line_.r[line_.w & mask] = inR[i];
            wow_ += wInc; flutter_ += fInc;
            if (wow_ > kPi * 2) wow_ -= kPi * 2;
            if (flutter_ > kPi * 2) flutter_ -= kPi * 2;
            const double mod = (0.0002 + 0.0022 * a) * std::sin(wow_) + 0.00012 * a * std::sin(flutter_);
            const double rp = (double)line_.w - (0.006 + mod) * fs_;
            const long long i0 = (long long)std::floor(rp);
            const float fr = (float)(rp - (double)i0);
            float l = line_.l[i0 & mask] + (line_.l[(i0 + 1) & mask] - line_.l[i0 & mask]) * fr;
            float r = line_.r[i0 & mask] + (line_.r[(i0 + 1) & mask] - line_.r[i0 & mask]) * fr;
            ++line_.w;
            const float m = 0.5f * (l + r), sd = 0.5f * (l - r) * (1.f - 0.6f * a);
            l = svf_[2].hp(svf_[0].lp(m + sd, gl, 1.1f), gh, 1.41f);
            r = svf_[3].hp(svf_[1].lp(m - sd, gl, 1.1f), gh, 1.41f);
            const float rate = 2.f + 38.f * a;   // crujidos por segundo
            if (noise() * 0.5f + 0.5f < rate / (float)fs_) { clickAmp_ = (0.04f + 0.14f * std::fabs(noise())) * (0.5f + a); clickEnv_ = 1.f; }
            float c = 0.f;
            if (clickEnv_ > 1e-4f) { c = clickAmp_ * clickEnv_ * noise(); clickEnv_ *= clickDecay; }
            c = svf_[4].hp(c, gc, 1.f);
            const float hiss = svf_[5].lp(noise(), gs, 1.f) * 0.008f * a;
            wL_[i] = std::tanh(l * 1.15f) / 1.15f + c + hiss;
            wR_[i] = std::tanh(r * 1.15f) / 1.15f + c * 0.8f + hiss;
        }
        break;
    }
    case StFx::Telefono: {
        // Solo los medios, en mono: radio AM a la izquierda (250–5000 Hz), teléfono a la derecha
        // (600–2800 Hz, con más resonancia y distorsión). Una compensación lenta (≈ 0,3 s, hasta
        // +18 dB) devuelve el nivel que se pierde al quitar graves y agudos, sea cual sea la pista.
        const double hpF = 250.0 * std::pow(600.0 / 250.0, (double)amt), lpF = 5000.0 * std::pow(2800.0 / 5000.0, (double)amt);
        const float gh = svfG(hpF, fs_), gl = svfG(lpF, fs_), k = 1.25f - 0.55f * amt;
        const float drive = 1.2f + 2.3f * amt;
        const float ke = (float)(1.0 - std::exp(-1.0 / (0.3 * fs_)));
        if (smG_ < 1.f || !std::isfinite(smG_)) smG_ = 1.f;
        for (int i = 0; i < n; ++i) {
            const float m = 0.5f * (inL[i] + inR[i]);
            float x = svf_[1].hp(svf_[0].hp(m, gh, k), gh, k);
            x = svf_[3].lp(svf_[2].lp(x, gl, k), gl, k);
            envIn_ += (m * m - envIn_) * ke;
            envOut_ += (x * x - envOut_) * ke;
            const float want = envOut_ > 1e-9f ? std::clamp(std::sqrt(envIn_ / envOut_) * 0.85f, 1.f, 8.f) : smG_;
            smG_ += (want - smG_) * ke * 4.f;
            x = std::tanh(x * smG_ * drive) / drive;   // saturación suave: nunca pasa de 1/drive·tanh
            wL_[i] = wR_[i] = x;
        }
        break;
    }
    case StFx::Chorus: {
        // Dos voces por canal, retardadas 12-16 ms y moduladas en direcciones opuestas.
        const int mask = line_.size - 1;
        const double inc = kPi * 2 * (0.3 + 0.5 * amt) / fs_;
        const double base = 0.012 * fs_, depth = (0.0015 + 0.0045 * amt) * fs_;
        auto rd = [&](const std::vector<float>& b, double d) {
            const double rp = (double)line_.w - d;
            const long long i0 = (long long)std::floor(rp);
            const float fr = (float)(rp - (double)i0);
            return b[i0 & mask] + (b[(i0 + 1) & mask] - b[i0 & mask]) * fr;
        };
        for (int i = 0; i < n; ++i) {
            line_.l[line_.w & mask] = inL[i]; line_.r[line_.w & mask] = inR[i];
            modPh_ += inc;
            if (modPh_ > kPi * 2) modPh_ -= kPi * 2;
            const float v1l = rd(line_.l, base + depth * std::sin(modPh_));
            const float v2l = rd(line_.l, base * 1.33 + depth * std::sin(modPh_ + 2.0944));
            const float v1r = rd(line_.r, base + depth * std::sin(modPh_ + 1.5708));
            const float v2r = rd(line_.r, base * 1.33 + depth * std::sin(modPh_ + 3.6652));
            ++line_.w;
            wL_[i] = 0.72f * inL[i] + 0.42f * (v1l + v2r);
            wR_[i] = 0.72f * inR[i] + 0.42f * (v1r + v2l);
        }
        break;
    }
    case StFx::Tremolo: case StFx::AutoPan: case StFx::Sidechain: {
        // Siguen la rejilla de la fuente: el ciclo empieza en el pulso.
        const double per = curType_ == (int)StFx::Sidechain ? std::clamp((double)beats, 0.25, 4.0) : std::max(1.0 / 16, (double)beats);
        const double spb = 60.0 / (bpm > 1 ? bpm : 120.0) * fs_;
        const float ks = (float)(1.0 - std::exp(-1.0 / (0.0015 * fs_)));   // suavizado (saltos de la rejilla sin chasquidos)
        const double atk = std::min(0.25, 0.006 * fs_ / (per * spb));      // bajada del sidechain: 6 ms
        for (int i = 0; i < n; ++i) {
            const float a = a0 + (a1 - a0) * (i + 1) * inv;
            const double b = beat0_ + (beat1_ - beat0_) * (i + 1) * inv;
            double ph = b / per;
            ph -= std::floor(ph);
            if (curType_ == (int)StFx::AutoPan) {
                const float pan = a * (float)std::sin(kPi * 2 * ph);
                smP_ += (pan - smP_) * ks;
                const float ang = (smP_ + 1.f) * (float)(kPi / 4);
                const float gL = std::cos(ang) * 1.41421356f, gR = std::sin(ang) * 1.41421356f;
                const float m = 0.5f * (inL[i] + inR[i]), sd = 0.5f * (inL[i] - inR[i]) * (1.f - 0.5f * std::fabs(smP_));
                wL_[i] = m * gL + sd;
                wR_[i] = m * gR - sd;
                continue;
            }
            float g;
            if (curType_ == (int)StFx::Tremolo) g = 1.f - a * 0.5f * (1.f - (float)std::cos(kPi * 2 * ph));
            else {
                const float depth = 0.95f * a;
                if (ph < atk) g = 1.f - depth * (float)(ph / atk);
                else { const float u = 1.f - (float)((ph - atk) / (1.0 - atk)); g = 1.f - depth * u * u; }
            }
            smG_ += (g - smG_) * ks;
            wL_[i] = inL[i] * smG_; wR_[i] = inR[i] * smG_;
        }
        break;
    }
    case StFx::Freeze: {
        // Graba siempre lo que suena. Al encenderlo copia el último medio segundo y repite su final
        // (TAMAÑO: 30-500 ms) con dos cabezas cruzadas con ventana de Hann: un sonido continuo.
        const int mask = line_.size - 1;
        const double G = 0.03 * std::pow(500.0 / 30.0, (double)amt) * fs_, minG = 0.02 * fs_;
        for (int i = 0; i < n; ++i) {
            line_.l[line_.w & mask] = inL[i]; line_.r[line_.w & mask] = inR[i];
            ++line_.w;
            fill_ = std::min(fill_ + 1, (long long)line_.size - 16);
            if (!held_) {
                wL_[i] = inL[i]; wR_[i] = inR[i];
                if (gArr_[i] > 0.f && fill_ >= (long long)minG + 8) {
                    hbLen_ = (int)std::min<long long>(fill_ - 4, std::min<long long>(hbCap_, (long long)(0.5 * fs_) + 64));
                    for (int k = 0; k < hbLen_; ++k) {
                        const long long src = line_.w - hbLen_ + k;
                        hbL_[k] = line_.l[src & mask]; hbR_[k] = line_.r[src & mask];
                    }
                    heldLen_ = std::clamp(G, minG, (double)hbLen_ - 2);
                    heldPh_ = 0;
                    held_ = true;
                }
                continue;
            }
            if (gArr_[i] <= 0.f) { held_ = false; wL_[i] = inL[i]; wR_[i] = inR[i]; continue; }
            heldLen_ += (std::clamp(G, minG, (double)hbLen_ - 2) - heldLen_) * 0.0005;   // TAMAÑO en marcha: se desliza
            float ol = 0, orr = 0;
            for (int h = 0; h < 2; ++h) {
                double p = heldPh_ + 0.5 * h;
                p -= std::floor(p);
                const double rp = (double)hbLen_ - 1 - heldLen_ + p * heldLen_;
                const int i0 = std::clamp((int)rp, 0, hbLen_ - 2);
                const float fr = (float)(rp - (double)i0);
                const float sn = (float)std::sin(kPi * p), win = sn * sn;
                ol += win * (hbL_[i0] + (hbL_[i0 + 1] - hbL_[i0]) * fr);
                orr += win * (hbR_[i0] + (hbR_[i0 + 1] - hbR_[i0]) * fr);
            }
            heldPh_ += 1.0 / heldLen_;
            heldPh_ -= std::floor(heldPh_);
            wL_[i] = ol; wR_[i] = orr;
        }
        break;
    }
    case StFx::Slicer: {
        // Graba siempre lo que suena. Al encenderlo copia el último ciclo completo de la rejilla
        // (1, 2 o 4 pulsos), lo parte en 8 trozos y los toca en el orden del PATRÓN, en la rejilla.
        // hb* = [2 ms de antes del ciclo][el ciclo]: con esos 2 ms el final del ciclo enlaza con su
        // principio sin chasquido (fundido cruzado, como un loop del plato).
        const int mask = line_.size - 1;
        const int P = std::max(16, (int)(0.002 * fs_));
        const double spb = 60.0 / (bpm > 1 ? bpm : 120.0) * fs_;
        double cyc = std::max(0.25, (double)beats);
        while (cyc > 0.25 && cyc * spb > hbCap_ - P - 8) cyc *= 0.5;   // que quepa en la memoria
        const double C = cyc * spb;
        const int* seq = kSlicerSeq[slicerPattern(amt)];
        auto at = [&](const std::vector<float>& hb, double rp) {   // rp en [0, C): posición dentro del ciclo
            int i0 = (int)rp;
            const float fr = (float)(rp - (double)i0);
            i0 = std::min(i0, hbLen_ - 1);
            const int i1 = std::min(i0 + 1, hbLen_ - 1);
            float v = hb[P + i0] + (hb[P + i1] - hb[P + i0]) * fr;
            const double u = rp - (hbLen_ - P);   // últimos 2 ms: se funde con lo que sonaba justo antes del ciclo
            if (u > 0) {
                const int j0 = std::min((int)u, P - 1), j1 = std::min(j0 + 1, P);   // hb[P] = principio del ciclo
                const float pre = hb[j0] + (hb[j1] - hb[j0]) * (float)(u - j0);
                const float w = (float)(u / P);
                v += (pre - v) * w;
            }
            return v;
        };
        for (int i = 0; i < n; ++i) {
            line_.l[line_.w & mask] = inL[i]; line_.r[line_.w & mask] = inR[i];
            ++line_.w;
            fill_ = std::min(fill_ + 1, (long long)line_.size - 16);
            const double b = beat0_ + (beat1_ - beat0_) * (i + 1) * inv;
            if (!held_) {
                wL_[i] = inL[i]; wR_[i] = inR[i];
                if (gArr_[i] > 0.f && fill_ >= (long long)C + P + 8) {
                    double ph = b / cyc;
                    ph -= std::floor(ph);
                    // El ciclo que acaba de terminar empieza en un pulso: así los cortes caen en la rejilla.
                    // Si aún no hay tanto grabado, se usa lo último (y se recuerda en qué punto del ciclo empieza).
                    long long end = line_.w - (long long)std::llround(ph * C);
                    heldRegPh_ = 0;
                    if ((double)(line_.w - end) + C + P > (double)fill_ - 4) { end = line_.w; heldRegPh_ = ph; }
                    hbLen_ = (int)std::llround(C);
                    for (int k = 0; k < hbLen_ + P; ++k) {
                        const long long src = end - hbLen_ - P + k;
                        hbL_[k] = line_.l[src & mask]; hbR_[k] = line_.r[src & mask];
                    }
                    heldLen_ = hbLen_;
                    heldPh_ = ph;
                    held_ = true;
                }
                continue;
            }
            if (gArr_[i] <= 0.f) { held_ = false; wL_[i] = inL[i]; wR_[i] = inR[i]; continue; }
            const double s8 = heldPh_ * 8.0;
            const int step = std::min(7, (int)s8);
            const int src = seq[step];
            const double S = heldLen_ / 8.0, pos = (s8 - step) * S;
            double off = (src + pos / S) / 8.0 - heldRegPh_;   // fase en la rejilla → posición en el ciclo copiado
            off -= std::floor(off);
            const double rp = off * heldLen_;
            // Fundido de 2 ms en cada corte, salvo si el trozo siguiente continúa donde acaba este
            // (también del 8.º al 1.º: el ciclo enlaza consigo mismo).
            const bool contIn = (seq[(step + 7) & 7] + 1) % 8 == src;
            const bool contOut = (src + 1) % 8 == seq[(step + 1) & 7];
            const double edge = std::min(0.002 * fs_, S * 0.25);
            float w = 1.f;
            if (!contIn && pos < edge) w = (float)(pos / edge);
            if (!contOut && S - pos < edge) w = std::min(w, (float)((S - pos) / edge));
            wL_[i] = at(hbL_, rp) * w; wR_[i] = at(hbR_, rp) * w;
            heldPh_ += 1.0 / heldLen_;
            if (heldPh_ >= 1.0) heldPh_ -= 1.0;
        }
        break;
    }
    default: break;
    }
}

void StUnit::processInsert(float* L, float* R, int n, int type, bool on, float amount, float mix, float beats, double bpm, double beat0, double beat1) {
    beat0_ = beat0; beat1_ = beat1;
    if (type != curType_ && !active()) { curType_ = type; reset(); gOn_ = 0.f; xg_ = 1.f; }   // parada: cambio directo
    if (curType_ == 0 || !busy(type, on)) return;    // parada y sin cola: no gasta nada
    // Entra desde parado: sin restos (salvo repetidor, freeze y slicer, que necesitan lo que acaba de sonar).
    if (gOn_ <= 0.f && on && tail_ <= 0 && type == curType_ && !stFxNeedsHistory(type)) reset();
    advanceGates(type, on, n);
    if (curType_ == 0) return;
    // Suavizado de intensidad y mezcla (30 ms).
    const float k = (float)(1.0 - std::exp(-n / (0.03 * fs_)));
    const float a0 = amt_, m0 = mix_;
    amt_ += (amount - amt_) * k;
    mix_ += (mix - mix_) * k;
    const float inv = 1.f / n;
    if (stFxHasTail(curType_)) {
        for (int i = 0; i < n; ++i) { tmpL_[i] = L[i] * gArr_[i]; tmpR_[i] = R[i] * gArr_[i]; }
        wet(tmpL_, tmpR_, n, a0, amt_, beats, bpm);
        float e = 0;
        for (int i = 0; i < n; ++i) {
            const float m = m0 + (mix_ - m0) * (i + 1) * inv;
            // El original baja solo mientras el efecto está encendido (mezcla > 50 %); la cola suma.
            const float dry = 1.f - gArr_[i] * xArr_[i] * (1.f - dryGain(m));
            const float w = wetGain(m) * xArr_[i];
            L[i] = L[i] * dry + wL_[i] * w;
            R[i] = R[i] * dry + wR_[i] * w;
            e = std::max(e, std::max(std::fabs(wL_[i]), std::fabs(wR_[i])));
        }
        tailCheck(e, n);
    } else {
        wet(L, R, n, a0, amt_, beats, bpm);
        for (int i = 0; i < n; ++i) {
            const float m = (m0 + (mix_ - m0) * (i + 1) * inv) * gArr_[i] * xArr_[i];
            L[i] += (wL_[i] - L[i]) * m;
            R[i] += (wR_[i] - R[i]) * m;
        }
        tail_ = 0;
    }
}

void StUnit::processSend(float* sL, float* sR, float* retL, float* retR, int n, int type, bool on, float amount, float mix, float beats, double bpm,
                         double beat0, double beat1) {
    beat0_ = beat0; beat1_ = beat1;
    if (type != curType_ && !active()) { curType_ = type; reset(); gOn_ = 0.f; xg_ = 1.f; }
    if (curType_ == 0 || !busy(type, on)) return;
    if (gOn_ <= 0.f && on && tail_ <= 0 && type == curType_ && !stFxNeedsHistory(type)) reset();
    advanceGates(type, on, n);
    if (curType_ == 0) return;
    const float k = (float)(1.0 - std::exp(-n / (0.03 * fs_)));
    const float a0 = amt_, m0 = mix_;
    amt_ += (amount - amt_) * k;
    mix_ += (mix - mix_) * k;
    const float inv = 1.f / n;
    if (stFxHasTail(curType_)) {
        // Entra la señal de envío (con su fundido) y lo que ya devuelven las ranuras anteriores.
        for (int i = 0; i < n; ++i) { tmpL_[i] = sL[i] * gArr_[i] + retL[i]; tmpR_[i] = sR[i] * gArr_[i] + retR[i]; }
        wet(tmpL_, tmpR_, n, a0, amt_, beats, bpm);
        float e = 0;
        for (int i = 0; i < n; ++i) {
            const float m = (m0 + (mix_ - m0) * (i + 1) * inv) * xArr_[i];
            retL[i] += wL_[i] * m;
            retR[i] += wR_[i] * m;
            e = std::max(e, std::max(std::fabs(wL_[i]), std::fabs(wR_[i])));
        }
        tailCheck(e, n);
    } else {
        // Efectos de color: dan forma a lo que entra en los efectos con cola que vienen detrás.
        wet(sL, sR, n, a0, amt_, beats, bpm);
        for (int i = 0; i < n; ++i) {
            const float m = (m0 + (mix_ - m0) * (i + 1) * inv) * gArr_[i] * xArr_[i];
            sL[i] += (wL_[i] - sL[i]) * m;
            sR[i] += (wR_[i] - sR[i]) * m;
        }
        tail_ = 0;
    }
}

// Cola: mientras está encendido se da margen; apagado, termina cuando lleva 0,3 s en silencio
// (o a los 12 s como mucho). Al terminar se limpia y deja de procesar.
void StUnit::tailCheck(float e, int n) {
    if (gOn_ > 0.f) { tail_ = (int)(fs_ * kTailMaxSec); quiet_ = 0; return; }
    tail_ -= n;
    quiet_ = e < kQuiet ? quiet_ + n : 0;
    if (quiet_ > (int)(0.3 * fs_)) tail_ = 0;
    if (tail_ <= 0) { tail_ = 0; reset(); }
}

// ================================================================== Isolator

void Isolator::prepare(double fs) {
    fs_ = fs;
    setFreqs(lo_, hi_);
    reset();
}

void Isolator::reset() {
    lp1_.reset(); hp1_.reset(); lp2_.reset(); hp2_.reset(); apLow_.reset();
}

void Isolator::setFreqs(double lo, double hi) {
    lo_ = lo; hi_ = hi;
    lp1_.setLowpass(lo, fs_); hp1_.setHighpass(lo, fs_);
    lp2_.setLowpass(hi, fs_); hp2_.setHighpass(hi, fs_);
    apLow_.k = BiquadCoefs::allpass(hi, M_SQRT1_2, fs_);
}

void Isolator::process(float* L, float* R, int n, const float g[3], float lowHz, float highHz, bool on) {
    if (gate_ <= 0.f && !on) return;
    const double lo = std::clamp((double)lowHz, (double)kIsoLowMin, (double)kIsoLowMax);
    const double hi = std::clamp((double)highHz, std::max((double)kIsoHighMin, lo * 2.5), (double)kIsoHighMax);
    if (gate_ <= 0.f && on) {   // entra limpio
        reset();
        setFreqs(lo, hi);
        for (int b = 0; b < 3; ++b) g_[b] = g[b];
    }
    const float gateStep = (float)(1.0 / (0.020 * fs_));
    const float kDown = (float)(1.0 - std::exp(-1.0 / (0.004 * fs_)));   // cortar: rápido
    const float kUp = (float)(1.0 - std::exp(-1.0 / (0.025 * fs_)));     // recuperar: suave
    constexpr int kSub = 32;
    const double glide = 1.0 - std::exp(-kSub / (0.05 * fs_));
    for (int off = 0; off < n; off += kSub) {
        const int m = std::min(kSub, n - off);
        // Cruces: se deslizan en escala logarítmica, sin reiniciar los filtros.
        const double nlo = lo_ * std::pow(lo / lo_, glide), nhi = hi_ * std::pow(hi / hi_, glide);
        if (std::fabs(nlo - lo_) > lo_ * 1e-4 || std::fabs(nhi - hi_) > hi_ * 1e-4) setFreqs(nlo, nhi);
        for (int i = off; i < off + m; ++i) {
            for (int b = 0; b < 3; ++b) g_[b] += (g[b] - g_[b]) * (g[b] < g_[b] ? kDown : kUp);
            gate_ = on ? std::min(1.f, gate_ + gateStep) : std::max(0.f, gate_ - gateStep);
            float lowL = L[i], lowR = R[i];   lp1_.process(lowL, lowR);
            float restL = L[i], restR = R[i]; hp1_.process(restL, restR);
            apLow_.process(lowL, lowR);
            float midL = restL, midR = restR; lp2_.process(midL, midR);
            float hiL = restL, hiR = restR;   hp2_.process(hiL, hiR);
            const float yl = lowL * g_[0] + midL * g_[1] + hiL * g_[2];
            const float yr = lowR * g_[0] + midR * g_[1] + hiR * g_[2];
            L[i] += (yl - L[i]) * gate_;
            R[i] += (yr - R[i]) * gate_;
        }
    }
}

// ================================================================== Rack

void Rack::prepare(double fs) {
    const bool sameRate = fs == fs_;
    fs_ = fs;
    for (int k = 0; k < kStSlots; ++k) units_[k].prepare(fs, 0x9E3779B9u * (uint32_t)(k + 1));
    freeBeat_ = 0;
    iso_.prepare(fs);
    instr.prepare(fs);
    stSrcCur_ = params.stSrc.load(); stModeCur_ = params.stMode.load();
    stGate_ = 1.f;
    isoSrcCur_ = params.isoSrc.load();
    isoSwitching_ = false;
    // Loop: se conserva si no cambia la frecuencia de muestreo (el audio está parado aquí).
    if (loopReady_.load() && !sameRate) {
        lpCap_ = (int64_t)std::ceil(kLoopMaxSec * fs) + 16;
        lpL_.assign(lpCap_, 0.f); lpR_.assign(lpCap_, 0.f); undoL_.assign(lpCap_, 0.f); undoR_.assign(lpCap_, 0.f);
        lpState_ = LoopState::Vacio; lpLen_ = 0; lpGain_ = 0; canUndo_ = false; undoLeft_ = 0; lpLayers_ = 0; ++gen_;
    }
    if (lpState_ == LoopState::Armado || lpState_ == LoopState::Grabando) { lpState_ = LoopState::Vacio; lpLen_ = 0; }
    if (lpState_ == LoopState::Sobregrabando || lpState_ == LoopState::ArmadoPlay) lpState_ = LoopState::Reproduciendo;
    lpSeam_ = -1;
}

void Rack::allocLoop() {
    if (loopReady_.load(std::memory_order_acquire)) return;
    lpCap_ = (int64_t)std::ceil(kLoopMaxSec * fs_) + 16;
    lpL_.assign(lpCap_, 0.f); lpR_.assign(lpCap_, 0.f); undoL_.assign(lpCap_, 0.f); undoR_.assign(lpCap_, 0.f);
    loopReady_.store(true, std::memory_order_release);
}

bool Rack::sourceBeat(int src, double& b0, double& b1, double& bpm, bool& playing) const {
    if (src == (int)RackSrc::Instr) {   // el reloj de la caja de ritmos y el bajo
        instr.clockInfo(b0, b1, bpm);
        playing = b1 > b0;
        return bpm > 0;
    }
    int d = src;
    if (src == (int)RackSrc::Mezcla) d = ctx_.lead;
    if (d < 0 || d > 1) { b0 = b1 = bpm = 0; playing = false; return false; }
    b0 = ctx_.beat0[d]; b1 = ctx_.beat1[d]; bpm = ctx_.bpm[d]; playing = ctx_.playing[d];
    return ctx_.grid[d] && bpm > 0;
}

float Rack::slotParam(int k, bool mix) const {
    const StSlotParams& p = params.slot[k];
    const float macro = std::clamp(params.macro.load(std::memory_order_relaxed), 0.f, 1.f);
    const float base = mix ? p.mix.load(std::memory_order_relaxed) : p.amount.load(std::memory_order_relaxed);
    const float m = mix ? p.macroMix.load(std::memory_order_relaxed) : p.macroAmt.load(std::memory_order_relaxed);
    return std::clamp(base + m * macro, 0.f, 1.f);
}

// Valores efectivos de este bloque: mando + macro + pad XY, EFECTOS OFF y encendidos.
void Rack::evalTargets(int n) {
    const bool cut = params.fxCut.load(std::memory_order_relaxed);
    const bool touch = params.xyTouch.load(std::memory_order_relaxed) || params.xyHold.load(std::memory_order_relaxed);
    const float wTarget = touch && !cut ? 1.f : 0.f;
    // Pulsar: 10 ms. Soltar: vuelta suave (≈ 0,4 s).
    const float tau = wTarget > xyW_ ? 0.010f : 0.080f;
    xyW_ += (wTarget - xyW_) * (float)(1.0 - std::exp(-n / (tau * fs_)));
    if (xyW_ < 1e-4f) xyW_ = 0.f;
    const float x = std::clamp(fin(params.xyX.load(std::memory_order_relaxed), 0.5f), 0.f, 1.f);
    const float y = std::clamp(fin(params.xyY.load(std::memory_order_relaxed), 0.5f), 0.f, 1.f);
    const int tx = params.xyTargetX.load(std::memory_order_relaxed), ty = params.xyTargetY.load(std::memory_order_relaxed);
    const bool autoOn = params.xyAutoOn.load(std::memory_order_relaxed);
    auto over = [&](int target, float base, bool* engaged) {
        float v = base;
        if (tx == target) { v = v + (x - v) * xyW_; if (engaged && autoOn && xyW_ > 1e-3f) *engaged = true; }
        if (ty == target) { v = v + (y - v) * xyW_; if (engaged && autoOn && xyW_ > 1e-3f) *engaged = true; }
        return v;
    };
    float macro = std::clamp(fin(params.macro.load(std::memory_order_relaxed), 0.f), 0.f, 1.f);
    macro = over(XY_Macro, macro, nullptr);
    const bool stOn = params.stOn.load(std::memory_order_relaxed);
    for (int k = 0; k < kStSlots; ++k) {
        const StSlotParams& p = params.slot[k];
        const float ma = std::clamp(fin(p.macroAmt.load(std::memory_order_relaxed), 0.f), -1.f, 1.f);
        const float mm = std::clamp(fin(p.macroMix.load(std::memory_order_relaxed), 0.f), -1.f, 1.f);
        bool engaged = p.on.load(std::memory_order_relaxed) || (macro > 0.01f && (ma != 0.f || mm != 0.f));
        float a = std::clamp(fin(p.amount.load(std::memory_order_relaxed), 0.5f) + ma * macro, 0.f, 1.f);
        float m = std::clamp(fin(p.mix.load(std::memory_order_relaxed), 0.5f) + mm * macro, 0.f, 1.f);
        a = over(XY_Amount0 + k, a, &engaged);
        m = over(XY_Mix0 + k, m, &engaged);
        effAmt_[k] = a; effMix_[k] = m;
        effOn_[k] = engaged && stOn && !cut && p.type.load(std::memory_order_relaxed) != 0;
        status.effAmount[k].store(a, std::memory_order_relaxed);
        status.effMix[k].store(m, std::memory_order_relaxed);
        status.slotOnEff[k].store(effOn_[k], std::memory_order_relaxed);
    }
    status.xyWeight.store(xyW_, std::memory_order_relaxed);
}

void Rack::beginBlock(const RackCtx& c, int n) {
    ctx_ = c;
    accTime_ = 0;
    evalTargets(n);
    // BPM de la estación: el de su fuente (o el del plato que manda en la mezcla).
    double b0, b1, bpm; bool pl;
    const bool grid = sourceBeat(stSrcCur_, b0, b1, bpm, pl);
    stBpm_ = grid ? bpm : 120.0;
    // Reloj de la estación (sidechain, trémolo, auto-pan, slicer): la rejilla de la fuente si suena;
    // si no, sigue solo al mismo tempo para que el bombeo no se quede clavado.
    if (grid && pl && b1 != b0 && std::isfinite(b0) && std::isfinite(b1)) { stBeat0_ = b0; stBeat1_ = b1; freeBeat_ = b1; }
    else { stBeat0_ = freeBeat_; freeBeat_ += n * stBpm_ / 60.0 / fs_; stBeat1_ = freeBeat_; }
    if (freeBeat_ > 1e6) freeBeat_ = std::fmod(freeBeat_, 64.0);
    status.stBpm.store(grid ? (float)bpm : 0.f, std::memory_order_relaxed);
    status.stBpmDeck.store(stSrcCur_ == (int)RackSrc::Mezcla ? ctx_.lead : stSrcCur_, std::memory_order_relaxed);
    // Cambio de fuente o de modo: fundido de salida, cambio y fundido de entrada (10 ms).
    const int wantSrc = std::clamp(params.stSrc.load(std::memory_order_relaxed), 0, 3);
    const int wantMode = params.stMode.load(std::memory_order_relaxed) ? 1 : 0;
    const float step = (float)(n / (0.010 * fs_));
    if (wantSrc != stSrcCur_ || wantMode != stModeCur_) {
        stGate_ -= step;
        if (stGate_ <= 0.f) {
            stGate_ = 0.f;
            stSrcCur_ = wantSrc; stModeCur_ = wantMode;
            for (auto& u : units_) u.reset();
        }
    } else stGate_ = std::min(1.f, stGate_ + step);
    // Aislador: al cambiar de fuente primero se apaga con su fundido (20 ms) y luego se cambia.
    const int wantIso = std::clamp(params.isoSrc.load(std::memory_order_relaxed), 0, 3);
    isoSwitching_ = wantIso != isoSrcCur_;
    if (isoSwitching_ && !iso_.active()) { isoSrcCur_ = wantIso; iso_.reset(); isoSwitching_ = false; }
    std::memset(sendL_, 0, sizeof(float) * n); std::memset(sendR_, 0, sizeof(float) * n);
    sendTapped_ = false;
    capTapped_ = false;
}

bool Rack::sanitize(float* L, float* R, int n) {
    for (int i = 0; i < n; ++i)
        if (!std::isfinite(L[i]) || !std::isfinite(R[i]) || std::fabs(L[i]) > 64.f || std::fabs(R[i]) > 64.f) return false;
    return true;
}

void Rack::runStation(float* L, float* R, int n) {
    const uint32_t order = params.stOrder.load(std::memory_order_relaxed);
    bool used[kStSlots] = {};
    for (int pos = 0; pos < kStSlots; ++pos) {
        int k = (order >> (pos * 4)) & 0xF;
        if (k >= kStSlots || used[k]) continue;   // orden incoherente: se ignora esa posición
        used[k] = true;
        const StSlotParams& p = params.slot[k];
        const int type = std::clamp(p.type.load(std::memory_order_relaxed), 0, (int)StFx::Count - 1);
        const float beats = kStDivBeats[std::clamp(p.div.load(std::memory_order_relaxed), 0, kStDivs - 1)];
        units_[k].processInsert(L, R, n, type, effOn_[k], effAmt_[k], effMix_[k], beats, stBpm_, stBeat0_, stBeat1_);
    }
    // Las que quedaran fuera del orden siguen detrás (no se pierde ninguna).
    for (int k = 0; k < kStSlots; ++k) if (!used[k]) {
        const StSlotParams& p = params.slot[k];
        const int type = std::clamp(p.type.load(std::memory_order_relaxed), 0, (int)StFx::Count - 1);
        units_[k].processInsert(L, R, n, type, effOn_[k], effAmt_[k], effMix_[k], kStDivBeats[std::clamp(p.div.load(), 0, kStDivs - 1)], stBpm_,
                                stBeat0_, stBeat1_);
    }
}

void Rack::processInsert(int src, float* L, float* R, int n) {
    if (isoSrcCur_ != src && stSrcCur_ != src) return;
    const auto t0 = std::chrono::steady_clock::now();
    processInsertImpl(src, L, R, n);
    accTime_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

void Rack::processInsertImpl(int src, float* L, float* R, int n) {
    // Aislador
    if (isoSrcCur_ == src) {
        const bool cut = params.fxCut.load(std::memory_order_relaxed);
        const float x = std::clamp(fin(params.xyX.load(std::memory_order_relaxed), 0.5f), 0.f, 1.f);
        const float y = std::clamp(fin(params.xyY.load(std::memory_order_relaxed), 0.5f), 0.f, 1.f);
        const int tx = params.xyTargetX.load(std::memory_order_relaxed), ty = params.xyTargetY.load(std::memory_order_relaxed);
        bool xyIso = false;
        float g[3];
        for (int b = 0; b < 3; ++b) {
            float knob = fin(params.isoGain[b].load(std::memory_order_relaxed), 0.5f);
            if (tx == XY_IsoLow + b) { knob += (x - knob) * xyW_; xyIso = xyIso || xyW_ > 1e-3f; }
            if (ty == XY_IsoLow + b) { knob += (y - knob) * xyW_; xyIso = xyIso || xyW_ > 1e-3f; }
            g[b] = params.isoKill[b].load(std::memory_order_relaxed) ? 0.f : eqKnobToGain(std::clamp(knob, 0.f, 1.f));
            if (cut) g[b] = 1.f;
            status.isoEff[b].store(g[b], std::memory_order_relaxed);
        }
        const bool on = (params.isoOn.load(std::memory_order_relaxed) || (xyIso && params.xyAutoOn.load(std::memory_order_relaxed))) && !isoSwitching_;
        if (on || iso_.active()) {
            iso_.process(L, R, n, g, fin(params.isoLowHz.load(std::memory_order_relaxed), 200.f), fin(params.isoHighHz.load(std::memory_order_relaxed), 2500.f), on);
            if (!sanitize(L, R, n)) { std::memset(L, 0, sizeof(float) * n); std::memset(R, 0, sizeof(float) * n); iso_.reset(); status.nonFinite.fetch_add(1); }
            float pk = 0;
            for (int i = 0; i < n; ++i) pk = std::max(pk, std::max(std::fabs(L[i]), std::fabs(R[i])));
            status.isoPeak.store(std::max(pk, status.isoPeak.load(std::memory_order_relaxed)), std::memory_order_relaxed);
        }
    }
    // Estación en inserción
    if (stModeCur_ == 0 && stSrcCur_ == src) {
        bool any = false;
        for (int k = 0; k < kStSlots; ++k)
            if (effOn_[k] || units_[k].active() || stFxNeedsHistory(params.slot[k].type.load(std::memory_order_relaxed))) any = true;
        if (!any) return;
        std::memcpy(dryL_, L, sizeof(float) * n);
        std::memcpy(dryR_, R, sizeof(float) * n);
        runStation(L, R, n);
        if (!sanitize(L, R, n)) {
            std::memcpy(L, dryL_, sizeof(float) * n); std::memcpy(R, dryR_, sizeof(float) * n);
            for (auto& u : units_) u.reset();
            status.nonFinite.fetch_add(1);
        }
        if (stGate_ < 1.f)
            for (int i = 0; i < n; ++i) { L[i] = dryL_[i] + (L[i] - dryL_[i]) * stGate_; R[i] = dryR_[i] + (R[i] - dryR_[i]) * stGate_; }
        float pk = 0;
        for (int i = 0; i < n; ++i) pk = std::max(pk, std::max(std::fabs(L[i]), std::fabs(R[i])));
        status.stPeak.store(std::max(pk, status.stPeak.load(std::memory_order_relaxed)), std::memory_order_relaxed);
    }
}

void Rack::mixInstruments(float* mL, float* mR, float* cL, float* cR, int n) {
    const auto t0 = std::chrono::steady_clock::now();
    instr.render(insL_, insR_, n, ctx_);
    accTime_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    processInsert((int)RackSrc::Instr, insL_, insR_, n);
    tapLoop((int)RackSrc::Instr, insL_, insR_, n);
    const bool cue = instr.params.instrCue.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i) {
        mL[i] += insL_[i]; mR[i] += insR_[i];
        if (cue) { cL[i] += insL_[i]; cR[i] += insR_[i]; }
    }
    tapSend((int)RackSrc::Instr, insL_, insR_, n, 1.f, 1.f);
}

void Rack::padCommand(int pad, int action, const Track* t, uint32_t gen) {
    switch (action) {
    case 0: instr.padRelease(pad); break;
    case 1: instr.padPress(pad); break;
    case 2: instr.padStopAll(); break;
    case 3: instr.padSet(pad, t, gen); break;
    default: break;
    }
}

void Rack::tapSend(int src, const float* L, const float* R, int n, float g0, float g1) {
    if (stModeCur_ != 1 || stSrcCur_ != src) return;
    const float inv = 1.f / n;
    for (int i = 0; i < n; ++i) {
        const float g = g0 + (g1 - g0) * (i + 1) * inv;
        sendL_[i] += L[i] * g; sendR_[i] += R[i] * g;
    }
    sendTapped_ = true;
}

void Rack::tapLoop(int src, const float* L, const float* R, int n) {
    if (params.lpSrc.load(std::memory_order_relaxed) != src) return;
    std::memcpy(capL_, L, sizeof(float) * n);
    std::memcpy(capR_, R, sizeof(float) * n);
    capTapped_ = true;
}

void Rack::command(int cmd, int arg) {
    const bool playingState = lpState_ == LoopState::Reproduciendo || lpState_ == LoopState::Sobregrabando || lpState_ == LoopState::ArmadoPlay;
    auto endDub = [&] {
        if (lpState_ != LoopState::Sobregrabando) return;
        lpState_ = LoopState::Reproduciendo;
        canUndo_ = dubSpan_ > 0;
        if (dubSpan_ > 0) ++lpLayers_;
    };
    switch (cmd) {
    case LoopRec:
        if (!loopReady_.load(std::memory_order_acquire)) return;
        if (lpState_ == LoopState::Armado) { lpState_ = lpBeforeArm_; return; }            // cancelar la espera
        if (lpState_ == LoopState::Grabando) { lpState_ = LoopState::Vacio; lpLen_ = 0; lpLayers_ = 0; ++gen_; return; }  // descartar
        endDub();
        lpBeforeArm_ = (playingState || lpState_ == LoopState::Parado) && lpLen_ > 0 ? LoopState::Parado : LoopState::Vacio;
        undoLeft_ = 0;
        lpBarsPending_ = std::clamp(arg, 1, 8);
        lpState_ = LoopState::Armado;
        status.lpTooLong.store(false);
        break;
    case LoopPlay:
        if (lpState_ == LoopState::Parado && lpLen_ > 0) lpState_ = LoopState::ArmadoPlay;
        else if (lpState_ == LoopState::ArmadoPlay) lpState_ = LoopState::Parado;
        break;
    case LoopStop:
        if (lpState_ == LoopState::Armado) { lpState_ = lpBeforeArm_; break; }
        endDub();
        if (playingState) lpState_ = LoopState::Parado;
        break;
    case LoopPlayStop:
        if (playingState) { endDub(); lpState_ = LoopState::Parado; }
        else if (lpState_ == LoopState::Parado && lpLen_ > 0) lpState_ = LoopState::ArmadoPlay;
        break;
    case LoopClear:
        if (lpState_ == LoopState::Vacio) break;
        lpState_ = LoopState::Vacio;
        canUndo_ = false; undoLeft_ = 0; lpLayers_ = 0; lpSeam_ = -1;
        ++gen_;
        break;
    case LoopDub:
        if (lpState_ == LoopState::Reproduciendo && undoLeft_ == 0 && lpSeam_ < 0) {
            lpState_ = LoopState::Sobregrabando;
            dubStart_ = -1; dubSpan_ = 0; dubLast_ = -1; canUndo_ = false;
            ++gen_;
        } else endDub();
        break;
    case LoopUndo:
        if (canUndo_ && (lpState_ == LoopState::Reproduciendo || lpState_ == LoopState::Parado) && dubStart_ >= 0) {
            undoPos_ = dubStart_; undoLeft_ = dubSpan_;
            canUndo_ = false;
            lpLayers_ = std::max(1, lpLayers_ - 1);
            ++gen_;
        }
        break;
    default: break;
    }
}

void Rack::runLoop(int n, float* mL, float* mR, float* cL, float* cR) {
    if (!loopReady_.load(std::memory_order_acquire)) return;
    static const float zeros[kRackBlock] = {};
    const float* inL = capTapped_ ? capL_ : zeros;
    const float* inR = capTapped_ ? capR_ : zeros;
    double b0, b1, bpm; bool playing;
    const int src = std::clamp(params.lpSrc.load(std::memory_order_relaxed), 0, 3);
    const bool grid = sourceBeat(src, b0, b1, bpm, playing);
    const bool quant = params.lpQuant.load(std::memory_order_relaxed);
    // Deshacer: se restaura por trozos (32768 muestras por bloque, muy por delante de la reproducción).
    if (undoLeft_ > 0 && lpLen_ > 0) {
        const int64_t chunk = std::min<int64_t>(undoLeft_, 32768);
        for (int64_t k = 0; k < chunk; ++k) {
            const int64_t idx = (undoPos_ + k) % lpLen_;
            lpL_[idx] = undoL_[idx]; lpR_[idx] = undoR_[idx];
        }
        undoPos_ += chunk; undoLeft_ -= chunk;
    }
    // Seguir el BPM: velocidad = BPM actual / BPM al grabar (como un vinilo).
    const double rateTarget = params.lpFollow.load(std::memory_order_relaxed) && lpBpm_ > 0 && grid ? std::clamp(bpm / lpBpm_, 0.5, 2.0) : 1.0;
    lpRate_ += (rateTarget - lpRate_) * 0.08;
    if (std::fabs(lpRate_ - rateTarget) < 1e-6) lpRate_ = rateTarget;
    // Inicio cuantizado: muestra del bloque en la que empieza el siguiente compás de la fuente.
    int startAt = -1;
    bool waitingStopped = false;
    if (lpState_ == LoopState::Armado || lpState_ == LoopState::ArmadoPlay) {
        if (!quant || !grid) startAt = 0;
        else if (playing && b1 > b0) {
            double target = std::ceil(b0 / 4.0 - 1e-9) * 4.0;
            if (b1 >= target) startAt = std::clamp((int)std::floor((target - b0) / (b1 - b0) * n), 0, n - 1);
        } else waitingStopped = true;
    }
    const float volT = std::clamp(fin(params.lpVol.load(std::memory_order_relaxed), 0.f), 0.f, 1.f);
    const float volTarget = volT * volT;
    const bool cue = params.lpCue.load(std::memory_order_relaxed);
    const float gUp = (float)(1.0 / (0.001 * fs_)), gDown = (float)(1.0 / (0.005 * fs_));   // entrar: 1 ms (no ablanda el primer golpe); salir: 5 ms
    float pk = 0;
    for (int i = 0; i < n; ++i) {
        if (i == startAt) {
            if (lpState_ == LoopState::Armado) {
                const double bpmRec = grid ? bpm : (bpm > 0 ? bpm : 120.0);
                const int64_t len = (int64_t)std::llround(lpBarsPending_ * 4.0 * 60.0 / bpmRec * fs_);
                if (len > lpCap_ - 8 || len < 64) { lpState_ = lpBeforeArm_; status.lpTooLong.store(true, std::memory_order_relaxed); }
                else {
                    lpState_ = LoopState::Grabando;
                    lpLen_ = len; lpRec_ = 0; lpBpm_ = bpmRec; lpBarsRec_ = lpBarsPending_;
                    lpLayers_ = 0; canUndo_ = false; undoLeft_ = 0; lpSeam_ = -1; lpPlay_ = 0; lpRate_ = 1.0;
                    ++gen_;
                }
            } else { lpState_ = LoopState::Reproduciendo; lpPlay_ = 0; }
        }
        if (lpState_ == LoopState::Grabando) {
            lpL_[lpRec_] = inL[i]; lpR_[lpRec_] = inR[i];
            if (++lpRec_ >= lpLen_) {
                lpState_ = params.lpAutoPlay.load(std::memory_order_relaxed) ? LoopState::Reproduciendo : LoopState::Parado;
                lpPlay_ = 0; lpLayers_ = 1;
                lpSeam_ = 0; lpSeamLen_ = (int)std::min<int64_t>(240, lpLen_ / 4);
                ++gen_;
                continue;
            }
        } else if (lpSeam_ >= 0) {
            // Unión sin chasquido: lo que sigue sonando tras el final se funde con el principio.
            if (lpSeam_ < lpSeamLen_) {
                const float w = (float)lpSeam_ / (float)lpSeamLen_;
                lpL_[lpSeam_] = inL[i] * (1.f - w) + lpL_[lpSeam_] * w;
                lpR_[lpSeam_] = inR[i] * (1.f - w) + lpR_[lpSeam_] * w;
                ++lpSeam_;
            }
            if (lpSeam_ >= lpSeamLen_) lpSeam_ = -1;
        }
        const bool audible = lpState_ == LoopState::Reproduciendo || lpState_ == LoopState::Sobregrabando;
        if (audible) lpGain_ = std::min(1.f, lpGain_ + gUp);
        else if (lpGain_ > 0.f) lpGain_ = std::max(0.f, lpGain_ - gDown);
        lpVol_ += (volTarget - lpVol_) * 0.002f;
        if (lpGain_ <= 0.f || lpLen_ < 2 || lpState_ == LoopState::Grabando) {
            if (lpState_ == LoopState::Vacio && lpGain_ <= 0.f) lpLen_ = 0;
            continue;
        }
        const int64_t j = (int64_t)lpPlay_;
        const float fr = (float)(lpPlay_ - (double)j);
        const int64_t j1 = j + 1 >= lpLen_ ? 0 : j + 1;
        const float l = lpL_[j] + (lpL_[j1] - lpL_[j]) * fr;
        const float r = lpR_[j] + (lpR_[j1] - lpR_[j]) * fr;
        if (lpState_ == LoopState::Sobregrabando) {
            const int64_t wj = fr < 0.5f ? j : j1;
            if (wj != dubLast_) {
                // Se guarda lo que había (solo la primera vuelta) para poder deshacer esta capa.
                if (dubStart_ < 0) { dubStart_ = wj; dubLast_ = (wj - 1 + lpLen_) % lpLen_; }
                int64_t k = (dubLast_ + 1) % lpLen_;
                for (int guard = 0; dubSpan_ < lpLen_ && guard < 8; ++guard) {
                    undoL_[k] = lpL_[k]; undoR_[k] = lpR_[k]; ++dubSpan_;
                    if (k == wj) break;
                    k = (k + 1) % lpLen_;
                }
                lpL_[wj] += inL[i]; lpR_[wj] += inR[i];
                dubLast_ = wj;
            }
        }
        const float g = lpGain_ * lpVol_;
        mL[i] += l * g; mR[i] += r * g;
        if (cue) { cL[i] += l * g; cR[i] += r * g; }
        pk = std::max(pk, std::max(std::fabs(l * g), std::fabs(r * g)));
        lpPlay_ += lpRate_;
        if (lpPlay_ >= (double)lpLen_) lpPlay_ -= (double)lpLen_;
    }
    // Estado para la interfaz
    status.lpState.store((int)lpState_, std::memory_order_relaxed);
    float pos = 0;
    if (lpState_ == LoopState::Grabando && lpLen_ > 0) pos = (float)lpRec_ / (float)lpLen_;
    else if (lpLen_ > 0) pos = (float)(lpPlay_ / (double)lpLen_);
    status.lpPos.store(pos, std::memory_order_relaxed);
    status.lpBpm.store(lpBpm_, std::memory_order_relaxed);
    status.lpBars.store(lpLen_ > 0 ? lpBarsRec_ : 0, std::memory_order_relaxed);
    status.lpSeconds.store(lpLen_ > 0 ? (float)(lpLen_ / fs_) : 0.f, std::memory_order_relaxed);
    status.lpRate.store((float)lpRate_, std::memory_order_relaxed);
    status.lpLayers.store(lpLayers_, std::memory_order_relaxed);
    status.lpCanUndo.store(canUndo_, std::memory_order_relaxed);
    status.lpBusy.store(lpState_ == LoopState::Armado || lpState_ == LoopState::Grabando || lpState_ == LoopState::Sobregrabando || lpSeam_ >= 0 || undoLeft_ > 0,
                        std::memory_order_relaxed);
    status.lpGen.store(gen_, std::memory_order_release);
    status.lpWaitingStopped.store(waitingStopped, std::memory_order_relaxed);
    status.lpPeak.store(std::max(pk, status.lpPeak.load(std::memory_order_relaxed)), std::memory_order_relaxed);
}

void Rack::finishBlock(float* mL, float* mR, float* cL, float* cR, int n) {
    const auto t0 = std::chrono::steady_clock::now();
    // Envío y retorno: la cadena procesa la señal de envío y solo vuelve lo que añaden los efectos.
    if (stModeCur_ == 1) {
        bool any = false;
        for (int k = 0; k < kStSlots; ++k)
            if (effOn_[k] || units_[k].active() || stFxNeedsHistory(params.slot[k].type.load(std::memory_order_relaxed))) any = true;
        if (any) {
            std::memset(retL_, 0, sizeof(float) * n); std::memset(retR_, 0, sizeof(float) * n);
            const uint32_t order = params.stOrder.load(std::memory_order_relaxed);
            bool used[kStSlots] = {};
            for (int pos = 0; pos < kStSlots * 2; ++pos) {
                int k = pos < kStSlots ? (int)((order >> (pos * 4)) & 0xF) : pos - kStSlots;
                if (k >= kStSlots || used[k]) continue;
                used[k] = true;
                const StSlotParams& p = params.slot[k];
                const int type = std::clamp(p.type.load(std::memory_order_relaxed), 0, (int)StFx::Count - 1);
                const float beats = kStDivBeats[std::clamp(p.div.load(std::memory_order_relaxed), 0, kStDivs - 1)];
                units_[k].processSend(sendL_, sendR_, retL_, retR_, n, type, effOn_[k], effAmt_[k], effMix_[k], beats, stBpm_, stBeat0_, stBeat1_);
            }
            if (!sanitize(retL_, retR_, n)) {
                for (auto& u : units_) u.reset();
                std::memset(retL_, 0, sizeof(float) * n); std::memset(retR_, 0, sizeof(float) * n);
                status.nonFinite.fetch_add(1);
            }
            const float ret = std::clamp(fin(params.stReturn.load(std::memory_order_relaxed), 0.f), 0.f, 1.f);
            const float target = ret * ret * 1.5f * stGate_;
            const float inv = 1.f / n;
            float pk = 0;
            for (int i = 0; i < n; ++i) {
                const float g = lastRet_ + (target - lastRet_) * (i + 1) * inv;
                mL[i] += retL_[i] * g; mR[i] += retR_[i] * g;
                pk = std::max(pk, std::max(std::fabs(retL_[i] * g), std::fabs(retR_[i] * g)));
            }
            lastRet_ = target;
            status.retPeak.store(std::max(pk, status.retPeak.load(std::memory_order_relaxed)), std::memory_order_relaxed);
            status.stPeak.store(std::max(pk, status.stPeak.load(std::memory_order_relaxed)), std::memory_order_relaxed);
        } else lastRet_ = 0.f;
    }
    for (int k = 0; k < kStSlots; ++k) status.slotActive[k].store(units_[k].active(), std::memory_order_relaxed);
    runLoop(n, mL, mR, cL, cR);
    const double el = accTime_ + std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const float load = (float)(el / (n / fs_));
    const float prev = status.cpu.load(std::memory_order_relaxed);
    status.cpu.store(prev + (load - prev) * 0.05f, std::memory_order_relaxed);
}

} // namespace rdj

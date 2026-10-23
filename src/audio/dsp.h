// Bloques de procesado de señal usados por el mezclador. Todo es sin reservas
// de memoria durante process(): los búferes se preparan en prepare().
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace rdj {

constexpr double kPi = 3.14159265358979323846;

inline float dbToLin(float db) { return std::pow(10.0f, db / 20.0f); }
inline float linToDb(float lin) { return lin > 1e-9f ? 20.0f * std::log10(lin) : -180.0f; }

// ---------------------------------------------------------------- Biquad
struct BiquadCoefs {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    static BiquadCoefs lowpass(double fc, double q, double fs);
    static BiquadCoefs highpass(double fc, double q, double fs);
    static BiquadCoefs allpass(double fc, double q, double fs);
};

inline BiquadCoefs BiquadCoefs::lowpass(double fc, double q, double fs) {
    const double w = 2 * kPi * fc / fs, c = std::cos(w), s = std::sin(w), al = s / (2 * q);
    const double a0 = 1 + al;
    BiquadCoefs k;
    k.b0 = (1 - c) / 2 / a0; k.b1 = (1 - c) / a0; k.b2 = k.b0;
    k.a1 = -2 * c / a0; k.a2 = (1 - al) / a0;
    return k;
}
inline BiquadCoefs BiquadCoefs::highpass(double fc, double q, double fs) {
    const double w = 2 * kPi * fc / fs, c = std::cos(w), s = std::sin(w), al = s / (2 * q);
    const double a0 = 1 + al;
    BiquadCoefs k;
    k.b0 = (1 + c) / 2 / a0; k.b1 = -(1 + c) / a0; k.b2 = k.b0;
    k.a1 = -2 * c / a0; k.a2 = (1 - al) / a0;
    return k;
}
inline BiquadCoefs BiquadCoefs::allpass(double fc, double q, double fs) {
    const double w = 2 * kPi * fc / fs, c = std::cos(w), s = std::sin(w), al = s / (2 * q);
    const double a0 = 1 + al;
    BiquadCoefs k;
    k.b0 = (1 - al) / a0; k.b1 = -2 * c / a0; k.b2 = 1.0;  // b2 = a0/a0
    k.a1 = -2 * c / a0; k.a2 = (1 - al) / a0;
    return k;
}

// Forma directa transpuesta II, estéreo, estado en double para estabilidad en graves.
struct Biquad {
    BiquadCoefs k;
    double zL1 = 0, zL2 = 0, zR1 = 0, zR2 = 0;

    void reset() { zL1 = zL2 = zR1 = zR2 = 0; }
    inline void process(float& l, float& r) {
        const double xl = l, xr = r;
        const double yl = k.b0 * xl + zL1;
        zL1 = k.b1 * xl - k.a1 * yl + zL2;
        zL2 = k.b2 * xl - k.a2 * yl;
        const double yr = k.b0 * xr + zR1;
        zR1 = k.b1 * xr - k.a1 * yr + zR2;
        zR2 = k.b2 * xr - k.a2 * yr;
        l = (float)yl; r = (float)yr;
    }
};

// Linkwitz-Riley de 4.º orden: dos Butterworth de 2.º orden en cascada.
struct LR4 {
    Biquad s1, s2;
    void setLowpass(double fc, double fs) { s1.k = s2.k = BiquadCoefs::lowpass(fc, M_SQRT1_2, fs); }
    void setHighpass(double fc, double fs) { s1.k = s2.k = BiquadCoefs::highpass(fc, M_SQRT1_2, fs); }
    void reset() { s1.reset(); s2.reset(); }
    inline void process(float& l, float& r) { s1.process(l, r); s2.process(l, r); }
};

// ---------------------------------------------------------------- EQ de 3 bandas
// Ecualizador tipo aislador: separa graves/medios/agudos con cruces LR4 y aplica
// una ganancia por banda. Con las tres bandas a 0 dB la suma es un paso todo
// (respuesta en magnitud plana). Ganancia 0 = corte total de la banda ("kill").
class ThreeBandEq {
public:
    void prepare(double fs, double lowHz = 250.0, double highHz = 2500.0) {
        fs_ = fs;
        lp1_.setLowpass(lowHz, fs);  hp1_.setHighpass(lowHz, fs);
        lp2_.setLowpass(highHz, fs); hp2_.setHighpass(highHz, fs);
        apLow_.k = BiquadCoefs::allpass(highHz, M_SQRT1_2, fs);
        reset();
    }
    void reset() { lp1_.reset(); hp1_.reset(); lp2_.reset(); hp2_.reset(); apLow_.reset(); }

    // gl/gm/gh: ganancias lineales al inicio y final del bloque (rampa lineal).
    void process(float* L, float* R, int n, float gl0, float gl1, float gm0, float gm1, float gh0, float gh1) {
        if (gl0 == 1.f && gl1 == 1.f && gm0 == 1.f && gm1 == 1.f && gh0 == 1.f && gh1 == 1.f && bypassable_) {
            // Mantener los filtros "calientes" para que al mover un mando no haya salto.
            for (int i = 0; i < n; ++i) {
                float l = L[i], r = R[i];
                splitAndSum(l, r, 1.f, 1.f, 1.f);
                L[i] = l; R[i] = r;
            }
            return;
        }
        const float inv = n > 0 ? 1.0f / n : 0.f;
        for (int i = 0; i < n; ++i) {
            const float t = (i + 1) * inv;
            float l = L[i], r = R[i];
            splitAndSum(l, r, gl0 + (gl1 - gl0) * t, gm0 + (gm1 - gm0) * t, gh0 + (gh1 - gh0) * t);
            L[i] = l; R[i] = r;
        }
    }

private:
    inline void splitAndSum(float& l, float& r, float gl, float gm, float gh) {
        float lowL = l, lowR = r;   lp1_.process(lowL, lowR);
        float restL = l, restR = r; hp1_.process(restL, restR);
        apLow_.process(lowL, lowR);  // alinea la fase de graves con el segundo cruce
        float midL = restL, midR = restR; lp2_.process(midL, midR);
        float hiL = restL, hiR = restR;   hp2_.process(hiL, hiR);
        l = lowL * gl + midL * gm + hiL * gh;
        r = lowR * gl + midR * gm + hiR * gh;
    }
    double fs_ = 48000;
    LR4 lp1_, hp1_, lp2_, hp2_;
    Biquad apLow_;
    bool bypassable_ = true;
};

// ---------------------------------------------------------------- Filtro DJ
// Un solo mando: negativo = paso bajo, positivo = paso alto, centro = sin filtro.
// Dos biquads en cascada (24 dB/oct) con ligera resonancia. Los coeficientes se
// recalculan cada 32 muestras para evitar escalones audibles.
class DjFilter {
public:
    void prepare(double fs) { fs_ = fs; a_.reset(); b_.reset(); cur_ = 0; }
    void reset() { a_.reset(); b_.reset(); }

    void process(float* L, float* R, int n, float target) {
        constexpr int kSub = 32;
        for (int off = 0; off < n; off += kSub) {
            const int m = std::min(kSub, n - off);
            // Suavizado del valor del mando.
            cur_ += (target - cur_) * 0.25f;
            if (std::fabs(cur_ - target) < 1e-4f) cur_ = target;
            const float v = cur_;
            const float av = std::fabs(v);
            if (av < 0.02f) {
                if (active_) { a_.reset(); b_.reset(); active_ = false; }
                continue;
            }
            active_ = true;
            const double q = 0.9;
            BiquadCoefs k;
            if (v < 0) {
                // 20 kHz -> 70 Hz, exponencial
                const double fc = 20000.0 * std::pow(70.0 / 20000.0, (av - 0.02) / 0.98);
                k = BiquadCoefs::lowpass(std::min(fc, fs_ * 0.45), q, fs_);
            } else {
                const double fc = 20.0 * std::pow(9000.0 / 20.0, (av - 0.02) / 0.98);
                k = BiquadCoefs::highpass(std::min(fc, fs_ * 0.45), q, fs_);
            }
            if ((v < 0) != wasLow_) { a_.reset(); b_.reset(); wasLow_ = v < 0; }
            a_.k = k; b_.k = k;
            // Mezcla seca/filtrada cerca del centro para entrar sin chasquido.
            const float wet = std::min(1.0f, (av - 0.02f) / 0.06f);
            for (int i = off; i < off + m; ++i) {
                float l = L[i], r = R[i];
                a_.process(l, r); b_.process(l, r);
                L[i] = L[i] + (l - L[i]) * wet;
                R[i] = R[i] + (r - R[i]) * wet;
            }
        }
    }
    float current() const { return cur_; }

private:
    double fs_ = 48000;
    Biquad a_, b_;
    float cur_ = 0;
    bool active_ = false, wasLow_ = true;
};

// ---------------------------------------------------------------- Filtro de estado variable
// Filtro de estado variable (TPT, Zavalishin/Cytomic), mono: estable aunque se mueva la frecuencia muestra a muestra.
struct Svf {
    float s1 = 0, s2 = 0;
    void reset() { s1 = s2 = 0; }
    // g = tan(π·fc/fs), k = 1/Q.
    inline void run(float x, float g, float k, float& lp, float& bp, float& hp) {
        const float a1 = 1.f / (1.f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float v3 = x - s2;
        const float v1 = a1 * s1 + a2 * v3;
        const float v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2 * v1 - s1; s2 = 2 * v2 - s2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
    inline float lp(float x, float g, float k) { float l, b, h; run(x, g, k, l, b, h); return l; }
    inline float hp(float x, float g, float k) { float l, b, h; run(x, g, k, l, b, h); return h; }
    inline float bp(float x, float g, float k) { float l, b, h; run(x, g, k, l, b, h); return b; }
};
inline float svfG(double fc, double fs) { return (float)std::tan(kPi * std::clamp(fc, 10.0, fs * 0.45) / fs); }

// ---------------------------------------------------------------- Limitador
// Limitador de picos con anticipación (1,5 ms). Garantiza que la salida no
// supera el umbral: la ganancia es la media móvil (longitud L) de una
// envolvente que nunca supera el mínimo de la ventana de L+1 muestras, así que
// cuando el pico sale del retardo de L muestras la ganancia ya es suficiente.
// Como última protección se recorta en ±umbral (no debería actuar).
class Limiter {
public:
    void prepare(double fs, float thresholdDb = -0.3f) {
        thr_ = dbToLin(thresholdDb);
        look_ = std::max(1, (int)std::lround(fs * 0.0015));
        delayL_.assign(look_, 0.f); delayR_.assign(look_, 0.f);
        want_.assign(look_ + 1, 1.f);
        hist_.assign(look_, 1.f);
        dpos_ = wpos_ = hpos_ = 0; h_ = 1.f; hsum_ = look_;
        release_ = std::exp(-1.0 / (0.150 * fs));
    }

    // Devuelve la ganancia mínima aplicada en el bloque (1 = sin reducción).
    float process(float* L, float* R, int n) {
        float minGain = 1.f;
        for (int i = 0; i < n; ++i) {
            const float pk = std::max(std::fabs(L[i]), std::fabs(R[i]));
            want_[wpos_] = pk > thr_ ? thr_ / pk : 1.f;
            wpos_ = (wpos_ + 1) % (look_ + 1);
            float wmin = 1.f;
            for (float w : want_) wmin = std::min(wmin, w);
            // Envolvente en double: en float la recuperación se quedaba atascada a 0,9998 (−0,002 dB para siempre tras limitar).
            if (wmin < h_) h_ = wmin;
            else h_ = wmin + (h_ - wmin) * release_;
            const float hf = (float)h_;
            hsum_ += (double)hf - (double)hist_[hpos_];   // suma exacta en double
            hist_[hpos_] = hf;
            hpos_ = (hpos_ + 1) % look_;
            const float g = std::min(1.f, (float)(hsum_ / look_));

            const float dl = delayL_[dpos_], dr = delayR_[dpos_];
            delayL_[dpos_] = L[i]; delayR_[dpos_] = R[i];
            dpos_ = (dpos_ + 1) % look_;
            L[i] = std::clamp(dl * g, -thr_, thr_);
            R[i] = std::clamp(dr * g, -thr_, thr_);
            minGain = std::min(minGain, g);
        }
        return minGain;
    }
    int latencyFrames() const { return look_; }

private:
    float thr_ = 1.f;
    double h_ = 1.0, release_ = 0.999;
    double hsum_ = 0;
    int look_ = 64, dpos_ = 0, wpos_ = 0, hpos_ = 0;
    std::vector<float> delayL_, delayR_, want_, hist_;
};

// ---------------------------------------------------------------- Curvas
enum class XfCurve : int { Suave = 0, Completa = 1, Corte = 2 };

// x en [0,1]: 0 = todo plato A, 1 = todo plato B.
inline void crossfaderGains(float x, XfCurve c, float& gA, float& gB) {
    x = std::clamp(x, 0.f, 1.f);
    switch (c) {
    case XfCurve::Suave:  // potencia constante
        gA = (float)std::cos(x * kPi * 0.5);
        gB = (float)std::sin(x * kPi * 0.5);
        break;
    case XfCurve::Completa:  // ambos al 100 % en el centro
        gA = std::min(1.f, 2.f * (1.f - x));
        gB = std::min(1.f, 2.f * x);
        break;
    default:  // Corte (scratch): entra entera en el primer 5 %
        gA = std::clamp((1.f - x) / 0.05f, 0.f, 1.f);
        gB = std::clamp(x / 0.05f, 0.f, 1.f);
        break;
    }
}

// Mando de EQ en [0,1], 0,5 = 0 dB. Abajo hasta corte total, arriba hasta +6 dB.
inline float eqKnobToGain(float v) {
    if (v <= 0.5f) { const float t = v / 0.5f; return t * t * t; }
    return dbToLin((v - 0.5f) * 2.f * 6.f);
}
// Fader de canal [0,1] -> lineal (curva de audio).
inline float faderToGain(float v) { v = std::clamp(v, 0.f, 1.f); return v * v; }

// ---------------------------------------------------------------- Medidor
struct MeterState {
    float peakL = 0, peakR = 0;  // pico del último bloque
    float rmsL = 0, rmsR = 0;
};
inline void measure(const float* L, const float* R, int n, MeterState& m) {
    float pl = 0, pr = 0; double sl = 0, sr = 0;
    for (int i = 0; i < n; ++i) {
        pl = std::max(pl, std::fabs(L[i])); pr = std::max(pr, std::fabs(R[i]));
        sl += (double)L[i] * L[i]; sr += (double)R[i] * R[i];
    }
    m.peakL = pl; m.peakR = pr;
    m.rmsL = n ? (float)std::sqrt(sl / n) : 0; m.rmsR = n ? (float)std::sqrt(sr / n) : 0;
}

} // namespace rdj

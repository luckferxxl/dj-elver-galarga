// Ruleta de color por canal (0.11), como los «Sound Color FX» de las mesas Pioneer: un solo mando
// por canal (el de FILTER de siempre). Centro = nada; a izquierda y derecha cambia el carácter según
// el color elegido para la mesa: FILTER (el filtro de siempre), SPACE (reverb), NOISE (ruido) o
// CRUSH (sonido roto). Como el resto del motor: process() no reserva memoria ni bloquea.
#pragma once
#include "dsp.h"
#include "effects.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace rdj {

enum class ColorFxType : int { Filtro = 0, Space, Noise, Crush, Count };

inline const char* colorFxLabel(int t) {   // rótulo bajo la ruleta
    switch ((ColorFxType)t) {
    case ColorFxType::Space: return "SPACE";
    case ColorFxType::Noise: return "NOISE";
    case ColorFxType::Crush: return "CRUSH";
    default: return "FILTER";
    }
}
inline const char* colorFxName(int t) {
    switch ((ColorFxType)t) {
    case ColorFxType::Space: return "Space (reverb)";
    case ColorFxType::Noise: return "Noise (ruido)";
    case ColorFxType::Crush: return "Crush (sonido roto)";
    default: return "Filter (filtro)";
    }
}
inline const char* colorFxHelp(int t) {
    switch ((ColorFxType)t) {
    case ColorFxType::Space: return "Reverb: a la derecha sin graves, a la izquierda sin agudos. Al volver al centro la cola se apaga sola.";
    case ColorFxType::Noise: return "Ruido blanco: a la derecha más fino y agudo, a la izquierda más oscuro. Para subidas.";
    case ColorFxType::Crush: return "Sonido roto (menos bits): a la izquierda con distorsión, a la derecha quitando graves.";
    default: return "El filtro de siempre: a la izquierda paso bajo, a la derecha paso alto.";
    }
}
inline std::string colorFxTip(int t, float v) {
    char b[96];
    const float a = std::fabs(v) * 100.f;
    if (std::fabs(v) < 0.02f) std::snprintf(b, sizeof b, "%s: en el centro (sin efecto)", colorFxName(t));
    else if ((ColorFxType)t == ColorFxType::Filtro) std::snprintf(b, sizeof b, v < 0 ? "Filtro paso bajo %.0f %%" : "Filtro paso alto %.0f %%", a);
    else std::snprintf(b, sizeof b, "%s %.0f %% %s", colorFxLabel(t), a, v < 0 ? "(izquierda)" : "(derecha)");
    return b;
}

class ColorFx {
public:
    void prepare(double fs, uint32_t seed) {
        fs_ = fs;
        k_ = (float)(1.0 - std::exp(-1.0 / (0.015 * fs)));
        seed_ = seed ? seed : 1u;
        filter_.prepare(fs);
        reverb_.prepare(fs);
        cur_ = 0; xg_ = 1.f;
        reset();
    }
    void reset() {
        filter_.reset();
        reverb_.reset();
        for (auto& f : svf_) f.reset();
        sm_ = 0.f; tail_ = 0; quiet_ = 0; lastSgn_ = 1.f; lastA_ = 0.15f;
        crushL_ = crushR_ = 0.f; crushCount_ = 0.f;
        rng_ = seed_;
    }

    // type: color de la mesa; knob: -1..1 (el mando FILTER del canal).
    void process(float* L, float* R, int n, int type, float knob) {
        if (n <= 0) return;
        type = std::clamp(type, 0, (int)ColorFxType::Count - 1);
        knob = std::isfinite(knob) ? std::clamp(knob, -1.f, 1.f) : 0.f;
        // Lo de siempre: con FILTER y sin cambio en curso, exactamente el mismo filtro que antes.
        if (type == cur_ && xg_ >= 1.f && cur_ == (int)ColorFxType::Filtro) { filter_.process(L, R, n, knob); return; }
        n = std::min(n, kMax);
        std::memcpy(dL_, L, sizeof(float) * n);
        std::memcpy(dR_, R, sizeof(float) * n);
        // Al cambiar de color: el actual se va en 10 ms, se cambia y el nuevo entra en otros 10 ms.
        const bool switching = type != cur_;
        const float step = (float)(1.0 / (0.010 * fs_));
        for (int i = 0; i < n; ++i) {
            xg_ = switching ? std::max(0.f, xg_ - step) : std::min(1.f, xg_ + step);
            xArr_[i] = xg_;
        }
        run(L, R, n, knob);
        for (int i = 0; i < n; ++i) {
            L[i] = dL_[i] + (L[i] - dL_[i]) * xArr_[i];
            R[i] = dR_[i] + (R[i] - dR_[i]) * xArr_[i];
        }
        if (switching && xg_ <= 0.f) { cur_ = type; reset(); }
    }
    int current() const { return cur_; }

private:
    static constexpr int kMax = 256;
    inline float noise() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return (float)(int32_t)rng_ * (1.f / 2147483648.f); }

    void run(float* L, float* R, int n, float knob) {
        switch ((ColorFxType)cur_) {
        case ColorFxType::Filtro: filter_.process(L, R, n, knob); return;
        case ColorFxType::Space: space(L, R, n, knob); return;
        case ColorFxType::Noise: noiseFx(L, R, n, knob); return;
        case ColorFxType::Crush: crush(L, R, n, knob); return;
        default: return;
        }
    }
    // Suavizado de la ruleta (≈ 15 ms), muestra a muestra.
    inline float smooth(float target) {
        sm_ += (target - sm_) * k_;
        if (target == 0.f && std::fabs(sm_) < 1e-5f) sm_ = 0.f;
        return sm_;
    }

    void space(float* L, float* R, int n, float knob) {
        // Envío a la reverb proporcional a la ruleta; la cola sigue al volver al centro.
        float send[kMax], sgn[kMax];
        bool feeding = false;
        for (int i = 0; i < n; ++i) {
            const float v = smooth(knob);
            const float a = std::fabs(v) < 0.02f ? 0.f : (std::fabs(v) - 0.02f) / 0.98f;
            if (a > 0.f) { feeding = true; lastSgn_ = v < 0 ? -1.f : 1.f; lastA_ = std::max(a, 0.15f); }
            send[i] = a; sgn[i] = lastSgn_;
        }
        if (!feeding && tail_ <= 0) return;
        float tl[kMax], tr[kMax];
        for (int i = 0; i < n; ++i) { tl[i] = L[i] * send[i]; tr[i] = R[i] * send[i]; }
        float bl[kMax], br[kMax];
        std::memcpy(bl, tl, sizeof(float) * n); std::memcpy(br, tr, sizeof(float) * n);
        const float a1 = n > 0 ? send[n - 1] : 0.f;
        reverb_.process(tl, tr, n, 0.5f + 0.35f * a1, 1.f, true);
        float e = 0.f;
        for (int i = 0; i < n; ++i) {
            float rl = tl[i] - bl[i], rr = tr[i] - br[i];
            const float a = send[i] > 0.f ? std::max(send[i], 0.15f) : lastA_;   // la cola conserva el último filtro
            // Derecha: reverb sin graves (corte que sube); izquierda: sin agudos (corte que baja).
            if (sgn[i] >= 0.f) {
                const float g = svfG(120.0 * std::pow(25.0, (double)a), fs_);
                rl = svf_[0].hp(rl, g, 1.2f); rr = svf_[1].hp(rr, g, 1.2f);
            } else {
                const float g = svfG(9000.0 * std::pow(0.07, (double)a), fs_);
                rl = svf_[2].lp(rl, g, 1.2f); rr = svf_[3].lp(rr, g, 1.2f);
            }
            L[i] += rl * 0.5f; R[i] += rr * 0.5f;
            e = std::max(e, std::max(std::fabs(rl), std::fabs(rr)));
        }
        // Cola: termina tras 0,3 s casi en silencio (o 10 s como mucho).
        if (feeding) { tail_ = (int)(fs_ * 10); quiet_ = 0; }
        else {
            tail_ -= n;
            quiet_ = e < 1e-5f ? quiet_ + n : 0;
            if (quiet_ > (int)(0.3 * fs_) || tail_ <= 0) { tail_ = 0; reverb_.reset(); for (auto& f : svf_) f.reset(); }
        }
    }

    void noiseFx(float* L, float* R, int n, float knob) {
        for (int i = 0; i < n; ++i) {
            const float v = smooth(knob);
            const float a = std::fabs(v) < 0.02f ? 0.f : (std::fabs(v) - 0.02f) / 0.98f;
            if (a <= 0.f) continue;
            float nl = noise(), nr = noise();
            if (v > 0) {   // más fino: el corte del paso alto sube
                const float g = svfG(200.0 * std::pow(40.0, (double)a), fs_);
                nl = svf_[0].hp(nl, g, 1.1f); nr = svf_[1].hp(nr, g, 1.1f);
            } else {       // más oscuro: el corte del paso bajo baja
                const float g = svfG(14000.0 * std::pow(0.02, (double)a), fs_);
                nl = svf_[2].lp(nl, g, 1.1f); nr = svf_[3].lp(nr, g, 1.1f);
            }
            const float lvl = (v > 0 ? 0.3f : 0.5f) * a * std::sqrt(a);   // el ruido oscuro necesita más nivel para oírse
            L[i] += nl * lvl; R[i] += nr * lvl;
        }
    }

    void crush(float* L, float* R, int n, float knob) {
        for (int i = 0; i < n; ++i) {
            const float v = smooth(knob);
            const float a = std::fabs(v) < 0.02f ? 0.f : (std::fabs(v) - 0.02f) / 0.98f;
            if (a <= 0.f) { crushL_ = L[i]; crushR_ = R[i]; continue; }
            const float levels = std::pow(2.f, 13.f - 9.f * a);   // 13 → 4 bits
            const float hold = 1.f + 9.f * a * a;                 // 1 → 10 muestras
            if ((crushCount_ += 1.f) >= hold) {
                crushCount_ -= hold;
                crushL_ = std::round(L[i] * levels) / levels;
                crushR_ = std::round(R[i] * levels) / levels;
            }
            float l = crushL_, r = crushR_;
            if (v < 0) {   // izquierda: además, distorsión
                const float d = 1.f + 5.f * a;
                l = std::tanh(l * d) / std::sqrt(d); r = std::tanh(r * d) / std::sqrt(d);
            } else {       // derecha: quitando graves
                const float g = svfG(40.0 * std::pow(30.0, (double)a), fs_);
                l = svf_[0].hp(l, g, 1.f); r = svf_[1].hp(r, g, 1.f);
            }
            const float w = std::min(1.f, a * 4.f);   // entra rápido desde el centro, sin salto
            L[i] += (l - L[i]) * w; R[i] += (r - R[i]) * w;
        }
    }

    double fs_ = 48000;
    uint32_t seed_ = 1, rng_ = 1;
    int cur_ = 0;
    float xg_ = 1.f, sm_ = 0.f, k_ = 0.001f;
    float lastSgn_ = 1.f, lastA_ = 0.15f;
    int tail_ = 0, quiet_ = 0;
    DjFilter filter_;
    Reverb reverb_;
    Svf svf_[4];
    float crushL_ = 0, crushR_ = 0, crushCount_ = 0;
    float dL_[kMax], dR_[kMax], xArr_[kMax];
};

} // namespace rdj

// Efectos por plato: eco, flanger y reverberación. Todos sincronizables al BPM
// del plato. La memoria se reserva en prepare() (fuera del hilo de audio).
#pragma once
#include "dsp.h"
#include <array>

namespace rdj {

// Los de Riser en adelante (0.11) los procesa una ranura como las del rack (StUnit, rack.h), en el mismo orden que StFx.
enum class FxType : int { Ninguno = 0, Eco = 1, Flanger = 2, Reverb = 3, EchoOut, Gate, AutoFilter, Phaser, Bitcrusher, Distortion,
                          Riser, Spiral, Sidechain, Shimmer, Vinilo, Telefono, Chorus, Tremolo, AutoPan, Freeze, Slicer, Granular, Resonador, Multibanda, Count };

inline const char* fxName(FxType t) {
    switch (t) {
    case FxType::Ninguno: return "Sin efecto";
    case FxType::Eco: return "Eco";
    case FxType::Flanger: return "Flanger";
    case FxType::Reverb: return "Reverberación";
    case FxType::EchoOut: return "Echo Out";
    case FxType::Gate: return "Gate / Trance";
    case FxType::AutoFilter: return "Auto Filter";
    case FxType::Phaser: return "Phaser";
    case FxType::Bitcrusher: return "Bitcrusher";
    case FxType::Distortion: return "Distorsión";
    case FxType::Riser: return "Riser de ruido";
    case FxType::Spiral: return "Spiral";
    case FxType::Sidechain: return "Sidechain";
    case FxType::Shimmer: return "Shimmer";
    case FxType::Vinilo: return "Vinilo lo-fi";
    case FxType::Telefono: return "Teléfono / radio";
    case FxType::Chorus: return "Chorus";
    case FxType::Tremolo: return "Trémolo";
    case FxType::AutoPan: return "Auto-pan";
    case FxType::Freeze: return "Freeze";
    case FxType::Slicer: return "Slicer";
    case FxType::Granular: return "Granular";
    case FxType::Resonador: return "Resonador cromático";
    case FxType::Multibanda: return "Saturación multibanda";
    default: return "?";
    }
}

struct FxContext {
    double fs = 48000;
    double bpm = 120;  // BPM efectivo del plato (0 si no se conoce)
};

// --------------------------------------------------------------- Eco
class Echo {
public:
    void prepare(double fs) {
        fs_ = fs;
        size_ = 1;
        while (size_ < (int)(fs * 4.5)) size_ <<= 1;
        bufL_.assign(size_, 0.f); bufR_.assign(size_, 0.f);
        w_ = 0; curDelay_ = fs * 0.5;
    }
    void reset() { std::fill(bufL_.begin(), bufL_.end(), 0.f); std::fill(bufR_.begin(), bufR_.end(), 0.f); }

    // feedInput=false deja sonar la cola sin alimentar el eco (salida del efecto).
    void process(float* L, float* R, int n, const FxContext& c, float beats, float wet, bool feedInput) {
        const double bpm = c.bpm > 1 ? c.bpm : 120.0;
        double target = 60.0 / bpm * beats * fs_;
        target = std::clamp(target, 64.0, (double)size_ - 8);
        const int mask = size_ - 1;
        const float fb = 0.55f;
        for (int i = 0; i < n; ++i) {
            curDelay_ += (target - curDelay_) * 0.0005;
            const double rp = w_ - curDelay_;
            const int i0 = (int)std::floor(rp);
            const float fr = (float)(rp - i0);
            const float dl = bufL_[i0 & mask] + (bufL_[(i0 + 1) & mask] - bufL_[i0 & mask]) * fr;
            const float dr = bufR_[i0 & mask] + (bufR_[(i0 + 1) & mask] - bufR_[i0 & mask]) * fr;
            const float inL = feedInput ? L[i] : 0.f, inR = feedInput ? R[i] : 0.f;
            // Realimentación cruzada suave y paso bajo implícito por la mezcla.
            lpL_ += (dl - lpL_) * 0.6f; lpR_ += (dr - lpR_) * 0.6f;
            bufL_[w_ & mask] = inL + lpL_ * fb;
            bufR_[w_ & mask] = inR + lpR_ * fb;
            ++w_;
            L[i] += dl * wet;
            R[i] += dr * wet;
        }
    }

private:
    double fs_ = 48000, curDelay_ = 24000;
    int size_ = 1;
    long long w_ = 0;
    float lpL_ = 0, lpR_ = 0;
    std::vector<float> bufL_, bufR_;
};

// --------------------------------------------------------------- Flanger
class Flanger {
public:
    void prepare(double fs) {
        fs_ = fs;
        size_ = 1;
        while (size_ < (int)(fs * 0.02)) size_ <<= 1;
        bufL_.assign(size_, 0.f); bufR_.assign(size_, 0.f);
        w_ = 0; phase_ = 0;
    }
    void reset() { std::fill(bufL_.begin(), bufL_.end(), 0.f); std::fill(bufR_.begin(), bufR_.end(), 0.f); }

    // depth y feedback: los usa el rack (los platos usan los valores por defecto).
    void process(float* L, float* R, int n, const FxContext& c, float beats, float wet, float depth = 1.f, float feedback = 0.6f) {
        const double bpm = c.bpm > 1 ? c.bpm : 120.0;
        const double periodSec = 60.0 / bpm * std::max(1.0f, beats);
        const double inc = 1.0 / (periodSec * fs_);
        const int mask = size_ - 1;
        for (int i = 0; i < n; ++i) {
            phase_ += inc; if (phase_ >= 1) phase_ -= 1;
            const double lfo = 0.5 - 0.5 * std::cos(2 * kPi * phase_);
            const double d = (0.0007 + 0.0050 * lfo * depth) * fs_;
            const double rp = w_ - d;
            const int i0 = (int)std::floor(rp);
            const float fr = (float)(rp - i0);
            const float dl = bufL_[i0 & mask] + (bufL_[(i0 + 1) & mask] - bufL_[i0 & mask]) * fr;
            const float dr = bufR_[i0 & mask] + (bufR_[(i0 + 1) & mask] - bufR_[i0 & mask]) * fr;
            bufL_[w_ & mask] = L[i] + dl * feedback;
            bufR_[w_ & mask] = R[i] + dr * feedback;
            ++w_;
            L[i] = L[i] * (1.f - 0.5f * wet) + dl * 0.7f * wet;
            R[i] = R[i] * (1.f - 0.5f * wet) + dr * 0.7f * wet;
        }
    }

private:
    double fs_ = 48000, phase_ = 0;
    int size_ = 1;
    long long w_ = 0;
    std::vector<float> bufL_, bufR_;
};

// --------------------------------------------------------------- Reverb
// Variante de Freeverb (dominio público, Jezar): 8 peines + 4 pasotodo por canal.
class Reverb {
public:
    void prepare(double fs) {
        static const int kComb[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        static const int kAp[4] = {556, 441, 341, 225};
        const double s = fs / 44100.0;
        for (int ch = 0; ch < 2; ++ch) {
            const int spread = ch ? 23 : 0;
            for (int i = 0; i < 8; ++i) { comb_[ch][i].buf.assign((size_t)((kComb[i] + spread) * s), 0.f); comb_[ch][i].pos = 0; comb_[ch][i].store = 0; }
            for (int i = 0; i < 4; ++i) { ap_[ch][i].buf.assign((size_t)((kAp[i] + spread) * s), 0.f); ap_[ch][i].pos = 0; }
        }
    }
    void reset() {
        for (auto& ch : comb_) for (auto& c : ch) { std::fill(c.buf.begin(), c.buf.end(), 0.f); c.store = 0; }
        for (auto& ch : ap_) for (auto& a : ch) std::fill(a.buf.begin(), a.buf.end(), 0.f);
    }

    // size en [0,1] controla la duración de la cola.
    void process(float* L, float* R, int n, float size, float wet, bool feedInput) {
        const float room = 0.70f + 0.28f * std::clamp(size, 0.f, 1.f);
        const float damp = 0.25f;
        for (int i = 0; i < n; ++i) {
            const float in = feedInput ? (L[i] + R[i]) * 0.015f : 0.f;
            float out[2] = {0, 0};
            for (int ch = 0; ch < 2; ++ch) {
                for (auto& c : comb_[ch]) {
                    float y = c.buf[c.pos];
                    c.store = y * (1 - damp) + c.store * damp;
                    c.buf[c.pos] = in + c.store * room;
                    if (++c.pos >= (int)c.buf.size()) c.pos = 0;
                    out[ch] += y;
                }
                for (auto& a : ap_[ch]) {
                    float b = a.buf[a.pos];
                    float y = -out[ch] + b;
                    a.buf[a.pos] = out[ch] + b * 0.5f;
                    if (++a.pos >= (int)a.buf.size()) a.pos = 0;
                    out[ch] = y;
                }
            }
            L[i] += out[0] * wet * 3.f;
            R[i] += out[1] * wet * 3.f;
        }
    }

private:
    struct Comb { std::vector<float> buf; int pos = 0; float store = 0; };
    struct Ap { std::vector<float> buf; int pos = 0; };
    std::array<std::array<Comb, 8>, 2> comb_;
    std::array<std::array<Ap, 4>, 2> ap_;
};

// --------------------------------------------------------------- Ranura
// Una ranura de efecto por plato. Al apagar, eco y reverb dejan sonar su cola.
class PerformanceFx {
public:
    void prepare(double fs) { fs_ = fs; filter_.prepare(fs); reset(); }
    void reset() { phase_ = 0; gate_ = 1; hold_ = 0; held_[0] = held_[1] = 0; for (auto& ch : z_) for (float& v : ch) v = 0; filter_.reset(); }
    // depth: profundidad del gate y recorrido del phaser (1 = como en los platos).
    void process(float* L, float* R, int n, FxType type, float wet, float beats, double bpm, float depth = 1.f) {
        const double inc = (bpm > 0 ? bpm : 120) / (60.0 * std::max(0.0625f, beats) * fs_);
        for (int i = 0; i < n; ++i) {
            phase_ += inc; phase_ -= std::floor(phase_);
            const float lfo = (float)std::sin(2 * kPi * phase_);
            float a = L[i], b = R[i];
            switch (type) {
            case FxType::Gate:
                gate_ += ((phase_ < 0.5 ? 1.f : 1.f - depth) - gate_) * (float)(1 - std::exp(-1.0 / (0.002 * fs_)));
                a *= gate_; b *= gate_; break;
            case FxType::AutoFilter: filter_.process(&a, &b, 1, lfo * 0.85f); break;
            case FxType::Phaser: {
                const float freq = 300.f + (lfo * depth + 1.f) * 1600.f;
                const float t = std::tan((float)(kPi * freq / fs_));
                const float coef = (1 - t) / (1 + t);
                float v[2] = {a,b};
                for (int ch = 0; ch < 2; ++ch) for (int k = 0; k < 4; ++k) {
                    const float y = -coef * v[ch] + z_[ch][k]; z_[ch][k] = v[ch] + coef * y; v[ch] = y;
                }
                a = 0.5f * (a + v[0]); b = 0.5f * (b + v[1]); break;
            }
            case FxType::Bitcrusher:
                if (--hold_ <= 0) { held_[0] = std::round(a * 64) / 64; held_[1] = std::round(b * 64) / 64; hold_ = 6; }
                a = held_[0]; b = held_[1]; break;
            case FxType::Distortion: a = std::tanh(a * 6) * 0.65f; b = std::tanh(b * 6) * 0.65f; break;
            default: break;
            }
            L[i] += (a - L[i]) * wet; R[i] += (b - R[i]) * wet;
        }
    }
private:
    double fs_ = 48000, phase_ = 0;
    float gate_ = 1, z_[2][4] = {}, held_[2] = {};
    int hold_ = 0;
    DjFilter filter_;
};

class FxSlot {
public:
    void prepare(double fs) { ctx_.fs = fs; echo_.prepare(fs); flanger_.prepare(fs); reverb_.prepare(fs); perf_.prepare(fs); tail_ = 0; wasOn_ = false; echoFade_ = 0; smoothWet_ = 0; lastType_ = FxType::Ninguno; }

    void process(float* L, float* R, int n, FxType type, bool on, float amount, float beats, double bpm) {
        ctx_.bpm = bpm;
        if (type != lastType_) { echo_.reset(); flanger_.reset(); reverb_.reset(); perf_.reset(); lastType_ = type; tail_ = 0; wasOn_ = false; echoFade_ = 0; smoothWet_ = 0; }
        if (on) tail_ = (int)(ctx_.fs * 6);  // segundos de cola tras apagar
        if (!on && tail_ <= 0) return;
        if (!on) tail_ -= n;
        const float targetWet = on ? std::clamp(amount, 0.f, 1.f) : smoothWet_;
        smoothWet_ += (targetWet - smoothWet_) * (float)(1 - std::exp(-n / (0.01 * ctx_.fs)));
        const float wet = smoothWet_;
        if (on && !wasOn_) echoFade_ = 0;
        wasOn_ = on;
        switch (type) {
        case FxType::EchoOut:
            for (int i = 0; i < n; ++i) {
                const float dryL = L[i], dryR = R[i];
                const float feed = on ? 1.f - echoFade_ : 0.f;
                L[i] *= feed; R[i] *= feed;
                echo_.process(L + i, R + i, 1, ctx_, beats, wet, on);
                L[i] += dryL * (1 - feed) * (on ? 1 - wet : 1);
                R[i] += dryR * (1 - feed) * (on ? 1 - wet : 1);
                if (on) echoFade_ = std::min(1.f, echoFade_ + (float)(1 / (0.08 * ctx_.fs)));
            }
            break;
        case FxType::Gate: case FxType::AutoFilter: case FxType::Phaser: case FxType::Bitcrusher: case FxType::Distortion:
            if (on) perf_.process(L,R,n,type,wet,beats,bpm); else tail_ = 0;
            break;
        case FxType::Eco: echo_.process(L, R, n, ctx_, beats, wet, on); break;
        case FxType::Flanger: if (on) flanger_.process(L, R, n, ctx_, beats, wet); else tail_ = 0; break;
        case FxType::Reverb: reverb_.process(L, R, n, std::clamp(beats / 8.f, 0.f, 1.f), wet, on); break;
        default: break;
        }
    }

private:
    PerformanceFx perf_;
    float echoFade_ = 0, smoothWet_ = 0;
    bool wasOn_ = false;
    FxContext ctx_;
    Echo echo_;
    Flanger flanger_;
    Reverb reverb_;
    FxType lastType_ = FxType::Ninguno;
    int tail_ = 0;
};

} // namespace rdj

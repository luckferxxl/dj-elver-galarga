#include "instruments.h"
#include "rack.h"
#include <cmath>
#include <cstring>

namespace rdj {

namespace {
constexpr double kTwoPi = 2.0 * kPi;
// Coeficiente para caer a -60 dB en "sec" segundos.
inline float decayCoef(double sec, double fs) { return (float)std::exp(std::log(0.001) / std::max(1e-4, sec * fs)); }
inline float fin01(float v, float d) { return std::isfinite(v) ? std::clamp(v, 0.f, 1.f) : d; }
}

const char* drumVoiceName(int v) {
    static const char* n[kDrumTracks] = {"Bombo", "Caja", "Palmas", "Charles cerrado", "Charles abierto", "Tom", "Rim", "Platillo"};
    return v >= 0 && v < kDrumTracks ? n[v] : "?";
}
const char* drumVoiceId(int v) {
    static const char* n[kDrumTracks] = {"bombo", "caja", "palmas", "charlesc", "charlesa", "tom", "rim", "platillo"};
    return v >= 0 && v < kDrumTracks ? n[v] : "?";
}

InstrParams::InstrParams() {
    for (int p = 0; p < kDrumPatterns; ++p) {
        drumSteps[p] = 16;
        for (int t = 0; t < kDrumTracks; ++t) for (int s = 0; s < kMaxSteps; ++s) drum[p][t][s] = 0;
    }
    for (int t = 0; t < kDrumTracks; ++t) { dLevel[t] = 0.8f; dTune[t] = 0.5f; dDecay[t] = 0.5f; dMute[t] = false; dProbability[t] = 1.f;dSolo[t]=false;
        dAttackMs[t]=.5f;dHoldMs[t]=0;dDecayMs[t]=t==3?90.f:t==7?2000.f:600.f;
        dPitchEnv[t]=0;dPitchEnvMs[t]=50;dCutHz[t]=18000;dDrive[t]=0;dPan[t]=0;dChoke[t]=(t==3||t==4)?1:0; }
    for (int p = 0; p < kBassPatterns; ++p) for (int s = 0; s < kBassSteps; ++s) bass[p][s] = 0;
    for (int i = 0; i < kPads; ++i) { padMode[i] = PM_Disparo; padVol[i] = 0.8f; padPitch[i]=0;padReverse[i]=false;padStart[i]=0;padEnd[i]=1;padAttack[i]=2;padRelease[i]=10;padChoke[i]=0; }
}

InstrStatus::InstrStatus() {
    for (int i = 0; i < kPads; ++i) { padPlaying[i] = false; padWaiting[i] = false; padPos[i] = 0.f; padGen[i] = 0; }
    for (int t = 0; t < kDrumTracks; ++t) {drumHits[t] = 0;drumSampleGen[t]=0;}
}

// =================================================================== caja de ritmos

void DrumVoices::prepare(double fs) {
    fs_ = fs;
    for (int i = 0; i < kDrumTracks; ++i) { v_[i] = V{}; v_[i].rng = 0x9E3779B9u * (i + 1); }
}

namespace {
// Frecuencias de los osciladores cuadrados de los platillos (proporciones de las cajas de ritmos analógicas clásicas).
constexpr double kMetal[6] = {205.3, 304.4, 369.6, 522.7, 540.0, 800.0};
struct BQ { float b0, b1, b2, a1, a2; };
BQ bandpass(double f, double q, double fs) {
    const double w = kTwoPi * std::min(f, fs * 0.45) / fs, al = std::sin(w) / (2 * q), a0 = 1 + al;
    return {(float)(al / a0), 0.f, (float)(-al / a0), (float)(-2 * std::cos(w) / a0), (float)((1 - al) / a0)};
}
BQ highpass(double f, double fs) {
    const BiquadCoefs k = BiquadCoefs::highpass(std::min(f, fs * 0.45), M_SQRT1_2, fs);
    return {(float)k.b0, (float)k.b1, (float)k.b2, (float)k.a1, (float)k.a2};
}
} // namespace

void DrumVoices::trigger(int v, float vel, float tune, float decay) {
    if (v < 0 || v >= kDrumTracks) return;
    V& x = v_[v];
    const uint32_t rng = x.rng;
    x = V{};
    x.rng = rng;
    x.on = true; x.vel = vel; x.tune = std::clamp(tune, 0.f, 1.f); x.decay = std::clamp(decay, 0.f, 1.f);
    x.env = 1; x.env2 = 1; x.penv = 1;
    if (v == DV_CharlesC && v_[DV_CharlesA].on) v_[DV_CharlesA].env *= 0.02f;   // el cerrado corta al abierto
}

void DrumVoices::render(float* L, float* R, int n, const float* level, const bool* mute) {
    const double fs = fs_;
    for (int vi = 0; vi < kDrumTracks; ++vi) {
        V& x = v_[vi];
        if (!x.on) continue;
        const float g = mute[vi] ? 0.f : level[vi] * level[vi] * 1.3f * x.vel;
        const float tn = x.tune, dc = x.decay;
        switch (vi) {
        case DV_Bombo: {
            const double f0 = 38 + 34 * tn;
            const float kA = decayCoef(0.15 + 0.9 * dc, fs), kP = decayCoef(0.045, fs), kC = decayCoef(0.004, fs);
            for (int i = 0; i < n; ++i) {
                const double f = f0 * (1 + 3.5 * x.penv);
                x.ph += f / fs; if (x.ph >= 1) x.ph -= 1;
                const float s = (float)std::sin(kTwoPi * x.ph) * x.env + 0.25f * noise(x) * x.env2;
                x.env *= kA; x.penv *= kP; x.env2 *= kC;
                L[i] += s * g; R[i] += s * g;
            }
            break;
        }
        case DV_Caja: {
            const double f0 = 160 + 90 * tn;
            const float kA = decayCoef(0.12, fs), kN = decayCoef(0.10 + 0.35 * dc, fs);
            const float hpK = (float)(1 - std::exp(-kTwoPi * 1200 / fs));
            for (int i = 0; i < n; ++i) {
                x.ph += f0 / fs; if (x.ph >= 1) x.ph -= 1;
                x.ph2 += f0 * 1.73 / fs; if (x.ph2 >= 1) x.ph2 -= 1;
                const float nz = noise(x);
                x.hp += (nz - x.hp) * hpK;
                const float s = 0.42f * (float)(std::sin(kTwoPi * x.ph) + 0.6 * std::sin(kTwoPi * x.ph2)) * x.env + 0.62f * (nz - x.hp) * x.env2;
                x.env *= kA; x.env2 *= kN;
                L[i] += s * g; R[i] += s * g;
            }
            break;
        }
        case DV_Palmas: {
            const BQ bq = bandpass(950 + 500 * tn, 1.8, fs);
            const double tail = 0.08 + 0.3 * dc;
            for (int i = 0; i < n; ++i) {
                const double t = x.t;
                float e = 0;
                for (double tk : {0.0, 0.0105, 0.021})
                    if (t >= tk) e += (float)std::exp(-(t - tk) / 0.0045) * (tk < 0.02 ? 0.8f : 1.f);
                if (t >= 0.021) e += 0.55f * (float)std::exp(-(t - 0.021) / (tail / 6.9));
                const float in = noise(x);
                const float y = bq.b0 * in + x.bpz1;
                x.bpz1 = bq.b1 * in - bq.a1 * y + x.bpz2;
                x.bpz2 = bq.b2 * in - bq.a2 * y;
                const float s = 2.2f * y * e;
                x.t += 1.0 / fs;
                x.env = e;
                L[i] += s * g; R[i] += s * g;
            }
            if (x.t > 0.03 + tail * 1.2) x.env = 0;
            break;
        }
        case DV_CharlesC: case DV_CharlesA: case DV_Platillo: {
            const bool crash = vi == DV_Platillo;
            const BQ bq = highpass(crash ? 4500 : 7000, fs);
            const double sc = (crash ? 1.7 : 1.0) * (0.85 + 0.3 * tn);
            const double dsec = vi == DV_CharlesC ? 0.03 + 0.1 * dc : vi == DV_CharlesA ? 0.18 + 0.7 * dc : 0.7 + 2.2 * dc;
            const float kA = decayCoef(dsec, fs);
            for (int i = 0; i < n; ++i) {
                float m = 0;
                for (int k = 0; k < 6; ++k) { x.mph[k] += kMetal[k] * sc / fs; x.mph[k] -= std::floor(x.mph[k]); m += x.mph[k] < 0.5 ? 1.f : -1.f; }
                const float in = m / 6.f * 0.7f + noise(x) * (crash ? 0.5f : 0.3f);
                const float y = bq.b0 * in + x.bpz1;
                x.bpz1 = bq.b1 * in - bq.a1 * y + x.bpz2;
                x.bpz2 = bq.b2 * in - bq.a2 * y;
                const float s = y * x.env * (crash ? 0.55f : 0.6f);
                x.env *= kA;
                L[i] += s * g * 0.9f; R[i] += s * g;
            }
            break;
        }
        case DV_Tom: {
            const double f0 = 80 + 130 * tn;
            const float kA = decayCoef(0.2 + 0.6 * dc, fs), kP = decayCoef(0.08, fs);
            for (int i = 0; i < n; ++i) {
                x.ph += f0 * (1 + 0.6 * x.penv) / fs; if (x.ph >= 1) x.ph -= 1;
                const float s = 0.8f * (float)std::sin(kTwoPi * x.ph) * x.env;
                x.env *= kA; x.penv *= kP;
                L[i] += s * g * 0.85f; R[i] += s * g;
            }
            break;
        }
        case DV_Rim: {
            const double f0 = 1700 * (0.8 + 0.4 * tn);
            const float kA = decayCoef(0.025 + 0.05 * dc, fs), kC = decayCoef(0.002, fs);
            for (int i = 0; i < n; ++i) {
                x.ph += f0 / fs; if (x.ph >= 1) x.ph -= 1;
                x.ph2 += 520 / fs; if (x.ph2 >= 1) x.ph2 -= 1;
                const float s = (float)(0.45 * std::sin(kTwoPi * x.ph) + 0.35 * std::sin(kTwoPi * x.ph2)) * x.env + 0.3f * noise(x) * x.env2;
                x.env *= kA; x.env2 *= kC;
                L[i] += s * g; R[i] += s * g * 0.85f;
            }
            break;
        }
        default: break;
        }
        if (x.env < 1e-4f && x.env2 < 1e-4f && vi != DV_Palmas) x.on = false;
        if (vi == DV_Palmas && x.env < 1e-4f && x.t > 0.03) x.on = false;
    }
}

// =================================================================== bajo

void BassVoice::prepare(double fs) { fs_ = fs; ph_ = 0; amp_ = 0; fenv_ = 0; accEnv_ = 0; gate_ = false; for (float& y : y_) y = 0; ylast_ = 0; }

void BassVoice::noteOn(double freq, bool accent, bool slide) {
    if (slide && gate_) { target_ = freq; glide_ = 1; return; }   // slide: no se vuelve a disparar
    freq_ = target_ = freq;
    glide_ = 0;
    gate_ = true;
    accent_ = accent;
    fenv_ = 1.f;
    accEnv_ = accent ? 1.f : 0.f;
}

void BassVoice::noteOff() { gate_ = false; }

void BassVoice::render(float* out, int n, const InstrParams& p) {
    const double fs = fs_;
    const float cut = fin01(p.cutoff.load(std::memory_order_relaxed), 0.3f), res = fin01(p.reso.load(std::memory_order_relaxed), 0.6f);
    const float em = fin01(p.envMod.load(std::memory_order_relaxed), 0.5f), dc = fin01(p.decay.load(std::memory_order_relaxed), 0.35f);
    const float acc = fin01(p.accent.load(std::memory_order_relaxed), 0.6f), drv = fin01(p.drive.load(std::memory_order_relaxed), 0.25f);
    const bool square = p.bassWave.load(std::memory_order_relaxed) == 1;
    const double base = 45.0 * std::pow(2.0, cut * 7.2);
    const float kF = decayCoef(0.08 + 1.4 * dc, fs), kAcc = decayCoef(0.25, fs);
    const float kAtt = (float)(1 - std::exp(-1.0 / (0.002 * fs))), kRel = (float)std::exp(-1.0 / (0.008 * fs));
    const double kGl = 1 - std::exp(-1.0 / (0.035 * fs));
    const float k = 3.95f * res;
    const float dg = 1.f + 5.f * drv;
    for (int i = 0; i < n; ++i) {
        if (glide_ > 0) { freq_ += (target_ - freq_) * kGl; if (std::fabs(freq_ - target_) < 0.01) { freq_ = target_; glide_ = 0; } }
        const double dt = freq_ / fs;
        ph_ += dt; if (ph_ >= 1) ph_ -= 1;
        // Sierra (y cuadrada como dos sierras) con polyBLEP: sin aliasing audible.
        auto blep = [dt](double t) -> double {
            if (t < dt) { t /= dt; return t + t - t * t - 1; }
            if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
            return 0;
        };
        double o = 2 * ph_ - 1 - blep(ph_);
        if (square) { double p2 = ph_ + 0.5; if (p2 >= 1) p2 -= 1; o = o - (2 * p2 - 1 - blep(p2)); o *= 0.6; }
        // Envolventes
        const float ampT = gate_ ? (accent_ ? 1.f : 0.72f) * (1 + acc * (accent_ ? 0.3f : 0.f)) : 0.f;
        if (gate_) amp_ += (ampT - amp_) * kAtt; else amp_ *= kRel;
        fenv_ *= kF; accEnv_ *= kAcc;
        double fc = base * std::pow(2.0, em * 5.0 * fenv_ + acc * 2.5 * accEnv_);
        fc = std::min(fc, fs * 0.42);
        const float g = (float)(1 - std::exp(-kTwoPi * fc / fs));
        // Filtro escalera de 4 polos con saturación en cada etapa y realimentación (resonancia).
        const float x = (float)o * (1.f + 0.5f * k) - k * ylast_;
        y_[0] += g * (std::tanh(x) - std::tanh(y_[0]));
        y_[1] += g * (std::tanh(y_[0]) - std::tanh(y_[1]));
        y_[2] += g * (std::tanh(y_[1]) - std::tanh(y_[2]));
        y_[3] += g * (std::tanh(y_[2]) - std::tanh(y_[3]));
        ylast_ = y_[3];
        out[i] = std::tanh(y_[3] * dg) / std::tanh(dg) * amp_ * 0.8f;
        if (!std::isfinite(out[i])) { for (float& y : y_) y = 0; ylast_ = 0; out[i] = 0; }
    }
}

// =================================================================== reloj y secuenciadores

void Instruments::prepare(double fs) {
    fs_ = fs;
    drums_.prepare(fs);
    sampleDrums_.prepare(fs);
    bassV_.prepare(fs);
    drumRun_ = bassRun_ = drumArm_ = bassArm_ = false;
    for (auto& p : pads_) { p.playing = p.waiting = p.held = p.fading = false; p.pos = 0; p.gain = 0; }
    lastQ_ = -1e9;
}

DrumControl Instruments::drumControl(int v)const {
    DrumControl c;c.attackMs=params.dAttackMs[v].load();c.holdMs=params.dHoldMs[v].load();
    c.decayMs=params.dDecayMs[v].load()*(.5f+safeParam(params.dDecay[v].load(),0,1,.5f));
    c.pitchSemis=(safeParam(params.dTune[v].load(),0,1,.5f)-.5f)*24;
    c.pitchDepth=params.dPitchEnv[v].load();c.pitchDecayMs=params.dPitchEnvMs[v].load();c.cutoff=params.dCutHz[v].load();
    c.drive=params.dDrive[v].load();c.pan=params.dPan[v].load();c.choke=params.dChoke[v].load();return c;
}
void Instruments::setDrumSample(int v,const Track* t,uint32_t gen) {
    if(v<0||v>=kDrumTracks)return;
    sampleDrums_.set(v,t);drums_.stop(v);status.drumSampleGen[v].store(gen,std::memory_order_release);
}
void Instruments::triggerDrum(int v,float velocity) {
    if(v<0||v>=kDrumTracks)return;
    if(sampleDrums_.has(v))sampleDrums_.trigger(v,velocity,drumControl(v));
    else drums_.trigger(v,safeParam(velocity,0,1,1),params.dTune[v].load(),params.dDecay[v].load());
    status.drumHits[v].fetch_add(1,std::memory_order_relaxed);
}

void Instruments::padSet(int pad, const Track* t, uint32_t gen) {
    if (pad < 0 || pad >= kPads) return;
    Pad& p = pads_[pad];
    p.playing = p.waiting = p.held = p.fading = false;
    p.pos = 0; p.gain = 0;
    p.s = t && t->frames >= 2 && t->frames <= t->pcm.size()/2 && t->sampleRate>0 ? PadSample{t->pcm.data(), (int64_t)t->frames, (double)t->sampleRate} : PadSample{};
    status.padGen[pad].store(gen, std::memory_order_release);
}

void Instruments::startPad(int k) {
    Pad& pd=pads_[k];
    const int group=std::clamp(params.padChoke[k].load(),0,4);
    if(group) for(int j=0;j<kPads;++j) if(j!=k && params.padChoke[j].load()==group) {
        pads_[j].waiting=false; if(pads_[j].playing)pads_[j].fading=true;
    }
    const auto region=padRegion(pd.s.frames,params.padStart[k].load(),params.padEnd[k].load());
    pd.pos=region.first(params.padReverse[k].load());
    pd.playing=region.valid();pd.waiting=false;pd.fading=false;pd.gain=0;
}

void Instruments::padPress(int pad) {
    if (pad < 0 || pad >= kPads) return;
    Pad& p = pads_[pad];
    if (!p.s.pcm) return;
    const int mode = params.padMode[pad].load(std::memory_order_relaxed);
    p.held = true;
    if (mode == PM_Loop) {
        if (p.playing || p.waiting) { p.waiting = false; p.fading = true; return; }   // segundo toque: para
        if (params.padQuant.load(std::memory_order_relaxed)) {
            const int group=params.padChoke[pad].load();
            if(group>0)for(int j=0;j<kPads;++j)if(j!=pad && params.padChoke[j].load()==group)pads_[j].waiting=false;
            p.waiting = true; return;
        }
    }
    startPad(pad);
}

void Instruments::padRelease(int pad) {
    if (pad < 0 || pad >= kPads) return;
    Pad& p = pads_[pad];
    p.held = false;
    if (params.padMode[pad].load(std::memory_order_relaxed) == PM_Mantener && p.playing) p.fading = true;
}

void Instruments::padStopAll() { for (auto& p : pads_) { if (p.playing) p.fading = true; p.waiting = false; } }

// Reloj: posición en pulsos de cada muestra del bloque. Sigue al plato elegido mientras suena
// (sus empujones, el SYNC y los cambios de pitch incluidos); si se para, sigue solo a su tempo.
void Instruments::clock(int n, const RackCtx& c, double* beat) {
    const int mode = params.sync.load(std::memory_order_relaxed);
    int d = mode == 1 ? 0 : mode == 2 ? 1 : mode == 3 ? -1 : c.lead;
    const bool follow = d >= 0 && d < 2 && c.grid[d] && c.playing[d] && c.beat1[d] >= c.beat0[d];
    if (follow) {
        const double b0 = c.beat0[d], b1 = c.beat1[d];
        if (!following_ || std::fabs(b0 - beat_) > 0.05) lastQ_ = b0 * 4.0;   // salto o cambio de reloj: sin disparar lo saltado
        for (int i = 0; i < n; ++i) beat[i] = b0 + (b1 - b0) * (i + 1) / n;
        beat_ = b1;
        bpm_ = c.bpm[d];
        status.clockDeck.store(d, std::memory_order_relaxed);
    } else {
        if (mode == 3 || d < 0 || !(c.bpm[d] > 0)) {
            const float ob = params.ownBpm.load(std::memory_order_relaxed);
            bpm_ = std::isfinite(ob) ? std::clamp((double)ob, 40.0, 220.0) : 124.0;
        } else bpm_ = c.bpm[d];   // plato parado: su tempo
        const double step = bpm_ / 60.0 / fs_;
        for (int i = 0; i < n; ++i) { beat_ += step; beat[i] = beat_; }
        status.clockDeck.store(-1, std::memory_order_relaxed);
    }
    following_ = follow;
    status.bpm.store((float)bpm_, std::memory_order_relaxed);
}

void Instruments::drumStepAt(long long k) {
    InstrParams& p = params;
    const bool want = p.drumPlay.load(std::memory_order_relaxed);
    if (!want) { drumRun_ = drumArm_ = false; return; }
    if (!drumRun_) {
        // Arranque: en el siguiente compás (o ya, alineado con el compás).
        if (p.quantStart.load(std::memory_order_relaxed) && k % 16 != 0) { drumArm_ = true; return; }
        drumRun_ = true; drumArm_ = false;
        drumPat_ = std::clamp(p.drumPattern.load(std::memory_order_relaxed), 0, kDrumPatterns - 1);
        drumBase_ = k - (((k % 16) + 16) % 16);
    }
    int steps = p.drumSteps[drumPat_].load(std::memory_order_relaxed) == 32 ? 32 : 16;
    long long pos = ((k - drumBase_) % steps + steps) % steps;
    if (pos == 0) {   // cambio de patrón al empezar el patrón
        const int want2 = std::clamp(p.drumPattern.load(std::memory_order_relaxed), 0, kDrumPatterns - 1);
        if (want2 != drumPat_) { drumPat_ = want2; drumBase_ = k; steps = p.drumSteps[drumPat_].load() == 32 ? 32 : 16; pos = 0; }
    }
    for (int t = 0; t < kDrumTracks; ++t) {
        const int v = p.drum[drumPat_][t][pos].load(std::memory_order_relaxed);
        if (!v) continue;
        const float probability=fin01(p.dProbability[t].load(),1.f);
        if(probability<=0 || (probability<1 && randomUnit()>=probability))continue;
        const float human=safeParam(p.humanizeVelocity.load(),0,.5f,0);
        const float velocity=std::clamp((v==2?1.f:.72f)*(1.f+(human>0?(randomUnit()*2-1)*human:0)),0.f,1.f);
        triggerDrum(t,velocity);
    }
    status.drumStep.store((int)pos, std::memory_order_relaxed);
    status.drumPatternNow.store(drumPat_, std::memory_order_relaxed);
}

void Instruments::bassStepAt(long long k) {
    InstrParams& p = params;
    const bool want = p.bassPlay.load(std::memory_order_relaxed);
    if (!want) { if (bassRun_) bassV_.noteOff(); bassRun_ = bassArm_ = false; bassGateBeat_ = -1; return; }
    if (!bassRun_) {
        if (p.quantStart.load(std::memory_order_relaxed) && k % 16 != 0) { bassArm_ = true; return; }
        bassRun_ = true; bassArm_ = false;
        bassPat_ = std::clamp(p.bassPattern.load(std::memory_order_relaxed), 0, kBassPatterns - 1);
        bassBase_ = k - (((k % 16) + 16) % 16);
    }
    long long pos = ((k - bassBase_) % kBassSteps + kBassSteps) % kBassSteps;
    if (pos == 0) {
        const int want2 = std::clamp(p.bassPattern.load(std::memory_order_relaxed), 0, kBassPatterns - 1);
        if (want2 != bassPat_) { bassPat_ = want2; bassBase_ = k; }
    }
    const int v = p.bass[bassPat_][pos].load(std::memory_order_relaxed);
    const int prev = p.bass[bassPat_][(pos + kBassSteps - 1) % kBassSteps].load(std::memory_order_relaxed);
    const bool slideIn = (prev & kBassGate) && (prev & kBassSlide);
    if (v & kBassGate) {
        const int midi = 24 + std::clamp(p.bassRoot.load(std::memory_order_relaxed), 0, 11) + 12 * std::clamp(p.bassOct.load(std::memory_order_relaxed), -1, 1) +
                         std::clamp(bassNote(v), 0, 36);
        bassV_.noteOn(440.0 * std::pow(2.0, (midi - 69) / 12.0), (v & kBassAccent) != 0, slideIn);
        // Se suelta a mitad del paso, salvo si este paso tiene slide (se liga con el siguiente).
        bassGateBeat_ = (v & kBassSlide) ? -1 : (double)k + 0.5;
    } else {
        bassV_.noteOff();
        bassGateBeat_ = -1;
    }
    status.bassStep.store((int)pos, std::memory_order_relaxed);
    status.bassPatternNow.store(bassPat_, std::memory_order_relaxed);
}

void Instruments::render(float* L, float* R, int n, const RackCtx& ctx) {
    std::memset(L, 0, sizeof(float) * n); std::memset(R, 0, sizeof(float) * n);
    {
        // Sin nada que sonar (ni pedido): solo avanza el reloj. Así el bus no gasta nada si no se usa.
        bool busy = params.drumPlay.load(std::memory_order_relaxed) || params.bassPlay.load(std::memory_order_relaxed) || drumRun_ || bassRun_ ||
                    drums_.any() || sampleDrums_.any() || bassV_.active();
        for (const Pad& pd : pads_) busy = busy || pd.playing || pd.waiting;
        if (!busy) {
            const double prevBeat = beat_;
            clock(n, ctx, beatArr_);
            blockB0_ = following_ ? beatArr_[0] : prevBeat;
            blockB1_ = beatArr_[n - 1];
            lastQ_ = beatArr_[n - 1] * 4.0;
            if (!silent_) {
                for (int k = 0; k < kPads; ++k) status.padPlaying[k].store(false, std::memory_order_relaxed);
                status.drumRunning.store(false, std::memory_order_relaxed); status.bassRunning.store(false, std::memory_order_relaxed);
                status.drumArmed.store(false, std::memory_order_relaxed); status.bassArmed.store(false, std::memory_order_relaxed);
                status.drumStep.store(-1, std::memory_order_relaxed); status.bassStep.store(-1, std::memory_order_relaxed);
            }
            silent_ = true;
            return;
        }
        silent_ = false;
    }
    std::memset(tmpL_, 0, sizeof(float) * n); std::memset(tmpR_, 0, sizeof(float) * n);
    std::memset(bass_, 0, sizeof(float) * n);
    const double prevBeat = beat_;
    clock(n, ctx, beatArr_);
    blockB0_ = following_ ? beatArr_[0] - (beatArr_[n > 1 ? 1 : 0] - beatArr_[0]) : prevBeat;
    blockB1_ = beatArr_[n - 1];
    InstrParams& p = params;
    // Swing: las semicorcheas pares (2.ª, 4.ª…) se retrasan hasta medio paso.
    const float swRaw = p.swing.load(std::memory_order_relaxed);
    const double sd = std::clamp(std::isfinite(swRaw) ? (double)swRaw : 0.5, 0.5, 0.75) * 2.0 - 1.0;   // 0 .. 0,5 de semicorchea
    const bool anyDrum = p.drumPlay.load(std::memory_order_relaxed) || drumRun_;
    const bool anyBass = p.bassPlay.load(std::memory_order_relaxed) || bassRun_;
    if (!p.drumPlay.load(std::memory_order_relaxed)) { drumRun_ = drumArm_ = false; }
    if (!p.bassPlay.load(std::memory_order_relaxed) && bassRun_) { bassV_.noteOff(); bassRun_ = bassArm_ = false; bassGateBeat_ = -1; }
    float level[kDrumTracks];
    bool mute[kDrumTracks];
    bool anySolo=false;for(int t=0;t<kDrumTracks;++t)anySolo=anySolo||p.dSolo[t].load();
    for (int t = 0; t < kDrumTracks; ++t) { level[t] = fin01(p.dLevel[t].load(std::memory_order_relaxed), 0.8f); mute[t] = p.dMute[t].load(std::memory_order_relaxed)||(anySolo&&!p.dSolo[t].load()); }
    int seg = 0;
    for (int i = 0; i < n; ++i) {
        const double q = beatArr_[i] * 4.0;
        const double q0 = lastQ_;
        lastQ_ = q;
        if (q <= q0 || q - q0 > 1.0) continue;   // parado, hacia atrás o salto: nada
        // Eventos de semicorchea en (q0, q]: las impares (2.ª de cada pulso…) con swing.
        const long long k1 = (long long)std::floor(q);
        for (long long k = k1 - 1; k <= k1; ++k) {
            const double at = (double)k + ((k & 1) ? sd : 0.0);
            if (!(at > q0 && at <= q)) continue;
            if (i > seg) { drums_.render(tmpL_ + seg, tmpR_ + seg, i - seg, level, mute); sampleDrums_.render(tmpL_+seg,tmpR_+seg,i-seg,level,mute); bassV_.render(bass_ + seg, i - seg, p); seg = i; }
            if (anyDrum) drumStepAt(k);
            if (anyBass) bassStepAt(k);
        }
        // Fin de la nota del bajo (a mitad de paso, sin swing).
        if (bassGateBeat_ >= 0 && q0 < bassGateBeat_ && q >= bassGateBeat_) {
            if (i > seg) { drums_.render(tmpL_ + seg, tmpR_ + seg, i - seg, level, mute); sampleDrums_.render(tmpL_+seg,tmpR_+seg,i-seg,level,mute); bassV_.render(bass_ + seg, i - seg, p); seg = i; }
            bassV_.noteOff();
            bassGateBeat_ = -1;
        }
        // Loops de los pads que esperan el pulso.
        if (std::floor(q0 / 4.0) != std::floor(q / 4.0))
            for (int k=0;k<kPads;++k) if (pads_[k].waiting) startPad(k);
    }
    if (n > seg) { drums_.render(tmpL_ + seg, tmpR_ + seg, n - seg, level, mute); sampleDrums_.render(tmpL_+seg,tmpR_+seg,n-seg,level,mute); bassV_.render(bass_ + seg, n - seg, p); }
    // Sampler
    float sL[256] = {}, sR[256] = {};
    for (int k = 0; k < kPads; ++k) {
        Pad& pd=pads_[k];
        status.padWaiting[k].store(pd.waiting,std::memory_order_relaxed);
        if(!pd.playing || !pd.s.pcm){status.padPlaying[k]=false;continue;}
        const auto region=padRegion(pd.s.frames,p.padStart[k].load(),p.padEnd[k].load());
        if(!region.valid()){pd.playing=false;status.padPlaying[k]=false;continue;}
        const float vol=fin01(p.padVol[k].load(),.8f);
        const bool loop=p.padMode[k].load()==PM_Loop, reverse=p.padReverse[k].load();
        const double rate=pd.s.rate/fs_*std::pow(2.,safeParam(p.padPitch[k].load(),-24,24,0)/12.)*(reverse?-1:1);
        const float up=float(1./(safeParam(p.padAttack[k].load(),.2f,250.f,2)*.001*fs_));
        const float down=float(1./(safeParam(p.padRelease[k].load(),1.f,1000.f,10)*.001*fs_));
        const double edge=std::max(1.,std::min(.002*pd.s.rate,(region.end-region.begin)/4));
        for(int i=0;i<n;++i) {
            if(pd.pos<region.begin || pd.pos>=region.end) {
                if(loop)pd.pos=region.wrap(pd.pos);else{pd.playing=false;break;}
            }
            if(pd.fading){pd.gain=std::max(0.f,pd.gain-down);if(pd.gain<=0){pd.playing=false;pd.fading=false;break;}}
            else pd.gain=std::min(1.f,pd.gain+up);
            const int64_t j=int64_t(pd.pos),next=std::min(j+1,int64_t(region.end)-1);
            const float fr=float(pd.pos-j);
            const float seam=float(std::clamp(std::min(pd.pos-region.begin,region.end-1-pd.pos)/edge,0.,1.));
            const float g=pd.gain*vol*vol*seam;
            sL[i]+=(pd.s.pcm[j*2]+(pd.s.pcm[next*2]-pd.s.pcm[j*2])*fr)*g;
            sR[i]+=(pd.s.pcm[j*2+1]+(pd.s.pcm[next*2+1]-pd.s.pcm[j*2+1])*fr)*g;
            pd.pos+=rate;
        }
        status.padPlaying[k].store(pd.playing,std::memory_order_relaxed);
        status.padPos[k].store(float(std::clamp(pd.pos/pd.s.frames,0.,1.)),std::memory_order_relaxed);
    }
    // Mezcla del bus con rampas de volumen.
    const float vd = fin01(p.drumVol.load(std::memory_order_relaxed), 0.8f), vb = fin01(p.bassVol.load(std::memory_order_relaxed), 0.7f);
    const float vs = fin01(p.samplerVol.load(std::memory_order_relaxed), 0.8f), vi = fin01(p.instrVol.load(std::memory_order_relaxed), 0.8f);
    const float tgt[4] = {vd * vd, vb * vb, vs * vs, vi * vi};
    const float inv = 1.f / n;
    float pk = 0;
    for (int i = 0; i < n; ++i) {
        const float t = (i + 1) * inv;
        const float gd = lastVol_[0] + (tgt[0] - lastVol_[0]) * t, gb = lastVol_[1] + (tgt[1] - lastVol_[1]) * t;
        const float gs = lastVol_[2] + (tgt[2] - lastVol_[2]) * t, gi = lastVol_[3] + (tgt[3] - lastVol_[3]) * t;
        L[i] = (tmpL_[i] * gd + bass_[i] * gb + sL[i] * gs) * gi;
        R[i] = (tmpR_[i] * gd + bass_[i] * gb + sR[i] * gs) * gi;
        pk = std::max(pk, std::max(std::fabs(L[i]), std::fabs(R[i])));
    }
    for (int k = 0; k < 4; ++k) lastVol_[k] = tgt[k];
    status.peak.store(std::max(pk, status.peak.load(std::memory_order_relaxed)), std::memory_order_relaxed);
    status.drumRunning.store(drumRun_, std::memory_order_relaxed);
    status.drumArmed.store(drumArm_ || (p.drumPlay.load(std::memory_order_relaxed) && !drumRun_), std::memory_order_relaxed);
    status.bassRunning.store(bassRun_, std::memory_order_relaxed);
    status.bassArmed.store(bassArm_ || (p.bassPlay.load(std::memory_order_relaxed) && !bassRun_), std::memory_order_relaxed);
    if (!drumRun_) status.drumStep.store(-1, std::memory_order_relaxed);
    if (!bassRun_) status.bassStep.store(-1, std::memory_order_relaxed);
}

// =================================================================== sonidos del sampler

const char* fxSampleName(int kind) {
    static const char* n[kFxSamples] = {"Bocina", "Sirena", "Láser", "Impacto", "Subida", "Bajada", "Barrido", "Sub 808"};
    return kind >= 0 && kind < kFxSamples ? n[kind] : "?";
}

std::shared_ptr<Track> makeFxSample(int kind, uint32_t sr) {
    static const double lens[kFxSamples] = {1.4, 3.0, 0.45, 2.2, 4.0, 3.0, 1.6, 2.2};
    auto t = std::make_shared<Track>();
    t->sampleRate = sr;
    t->frames = (uint64_t)(lens[std::clamp(kind, 0, kFxSamples - 1)] * sr);
    t->pcm.assign(t->frames * 2, 0.f);
    uint32_t rng = 0x1234567u + kind * 977;
    auto noise = [&]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float)(int32_t)rng * (1.f / 2147483648.f); };
    // Filtro de estado variable (para los barridos de ruido).
    float lp = 0, bp = 0;
    auto svf = [&](float in, double f, double q) {
        const float fq = (float)(2 * std::sin(kPi * std::min(f, sr * 0.2) / sr));
        lp += fq * bp; const float hp = in - lp - (float)(1.0 / q) * bp; bp += fq * hp;
        return bp;
    };
    double ph[4] = {0, 0, 0, 0};
    const double len = (double)t->frames / sr;
    for (uint64_t i = 0; i < t->frames; ++i) {
        const double x = (double)i / sr;
        float l = 0, r = 0;
        switch (kind) {
        case 0: {   // Bocina: tres toques
            const double starts[3] = {0.0, 0.25, 0.5}, durs[3] = {0.19, 0.19, 0.85};
            float e = 0;
            for (int k = 0; k < 3; ++k) if (x >= starts[k] && x < starts[k] + durs[k]) e = (float)(std::min(1.0, (x - starts[k]) / 0.01) * std::min(1.0, (starts[k] + durs[k] - x) / 0.03));
            float s = 0;
            const double fr[3] = {466.2, 554.4, 698.5};
            for (int k = 0; k < 3; ++k) { ph[k] += fr[k] * (1 + 0.004 * std::sin(kTwoPi * 5.5 * x)) / sr; ph[k] -= std::floor(ph[k]); s += (float)(2 * ph[k] - 1); }
            l = r = std::tanh(s * 0.9f) * 0.55f * e;
            break;
        }
        case 1: {   // Sirena
            const double f = 650 + 380 * std::sin(kTwoPi * 1.4 * x);
            ph[0] += f / sr; ph[0] -= std::floor(ph[0]);
            const float e = (float)(std::min(1.0, x / 0.05) * std::min(1.0, (len - x) / 0.4));
            l = r = (float)(std::sin(kTwoPi * ph[0]) + 0.3 * std::sin(2 * kTwoPi * ph[0])) * 0.4f * e;
            break;
        }
        case 2: {   // Láser
            const double f = 80 + 3200 * std::exp(-x * 14);
            ph[0] += f / sr; ph[0] -= std::floor(ph[0]);
            const float e = (float)std::exp(-x * 7);
            l = r = (ph[0] < 0.5 ? 0.45f : -0.45f) * e;
            break;
        }
        case 3: {   // Impacto: golpe de sub + ruido con cola
            const double f = 34 + 40 * std::exp(-x * 18);
            ph[0] += f / sr; ph[0] -= std::floor(ph[0]);
            const float sub = (float)std::sin(kTwoPi * ph[0]) * (float)std::exp(-x * 2.2) * 0.9f;
            const float nz = svf(noise(), 400 + 3000 * std::exp(-x * 6), 0.8) * (float)std::exp(-x * 3.5) * 0.9f;
            l = std::tanh(sub + nz); r = std::tanh(sub + nz * 0.9f);
            break;
        }
        case 4: {   // Subida: ruido que se abre y tono que sube
            const double u = x / len;
            const float nz = svf(noise(), 200 * std::pow(40.0, u), 2.5) * (float)(0.15 + 0.85 * u * u) * 0.6f;
            ph[0] += (110 * std::pow(4.0, u)) / sr; ph[0] -= std::floor(ph[0]);
            const float tone = (float)(2 * ph[0] - 1) * (float)(u * u) * 0.2f;
            const float e = (float)std::min(1.0, (len - x) / 0.02);
            l = (nz + tone) * e; r = (nz * 0.9f + tone) * e;
            break;
        }
        case 5: {   // Bajada
            const double u = x / len;
            const float nz = svf(noise(), 8000 * std::pow(1 / 40.0, u), 2.0) * (float)((1 - u) * (1 - u)) * 0.7f;
            l = nz; r = nz * 0.92f;
            break;
        }
        case 6: {   // Barrido (sube y baja)
            const double u = x / len, tri = u < 0.5 ? u * 2 : (1 - u) * 2;
            const float nz = svf(noise(), 300 * std::pow(25.0, tri), 3.0) * (float)(std::sin(kPi * u)) * 0.7f;
            l = nz; r = svf(nz, 2000, 0.7) * 0.3f + nz * 0.8f;
            break;
        }
        default: {  // Sub 808
            const double f = 45 + 20 * std::exp(-x * 25);
            ph[0] += f / sr; ph[0] -= std::floor(ph[0]);
            l = r = std::tanh((float)std::sin(kTwoPi * ph[0]) * 1.6f) * (float)std::exp(-x * 1.4) * 0.75f;
            break;
        }
        }
        t->pcm[i * 2] = l; t->pcm[i * 2 + 1] = r;
    }
    // Todos al mismo pico (0,9): los barridos con filtro resonante salen con más ganancia.
    float pk = 0;
    for (float v : t->pcm) pk = std::max(pk, std::fabs(v));
    if (pk > 0) for (float& v : t->pcm) v *= 0.9f / pk;
    t->meta.durationSec = len;
    return t;
}

} // namespace rdj

#include "engine.h"
#include <chrono>
#include <cstring>

namespace rdj {

// ============================================================== DeckEngine

void DeckEngine::prepare(double fs) {
    fs_ = fs;
    eq_.prepare(fs);
    color_.prepare(fs, 0xC0102F0Du ^ (uint32_t)(uintptr_t)this);
    fx_.prepare(fs);
    fxUnit_.prepare(fs, 0xA511E9B3u ^ (uint32_t)(uintptr_t)this);
    fxFreeBeat_ = 0;
    xfRemain_ = 0;
    stretch_.prepare(fs, kMaxBlock);
    primeL_.assign(stretch_.primeLength() + 1, 0.f);
    primeR_.assign(stretch_.primeLength() + 1, 0.f);
    hist_.assign(stretch_.primeLength() + 4 * kMaxBlock, 0.0);
    histW_ = 0;
    stretchActive_ = false;
    needPrime_ = true;
}

double DeckEngine::beatLenFrames(const DeckParams& p) const {
    const double bpm = p.bpm.load(std::memory_order_relaxed);
    if (!track_ || bpm <= 0) return 0;
    return 60.0 / bpm * track_->sampleRate;
}

double DeckEngine::beatPhase(const DeckParams& p) const {
    const double bl = beatLenFrames(p);
    if (bl <= 0) return 0;
    const double anchor = p.gridAnchorSec.load(std::memory_order_relaxed) * track_->sampleRate;
    double ph = std::fmod((pos_ - anchor) / bl, 1.0);
    if (ph < 0) ph += 1.0;
    return ph;
}

double DeckEngine::beatPosition(const DeckParams& p) const {
    const double bl = beatLenFrames(p);
    if (bl <= 0) return 0;
    return (pos_ - p.gridAnchorSec.load(std::memory_order_relaxed) * track_->sampleRate) / bl;
}

double DeckEngine::effectiveBpm(const DeckParams& p) const {
    return p.bpm.load(std::memory_order_relaxed) * (1.0 + p.pitch.load(std::memory_order_relaxed));
}

double DeckEngine::snapToBeat(double f, const DeckParams& p) const {
    if (!p.quantize.load(std::memory_order_relaxed)) return f;
    const double bl = beatLenFrames(p);
    if (bl <= 0) return f;
    const double anchor = p.gridAnchorSec.load(std::memory_order_relaxed) * track_->sampleRate;
    double s = anchor + std::round((f - anchor) / bl) * bl;
    if (s < 0) s += bl;
    return s;
}

void DeckEngine::jumpTo(double f) {
    if (!track_) return;
    f = std::clamp(f, 0.0, (double)track_->frames);
    if (playGain_ > 0.f) startTail();
    pos_ = f;
    ++jumps_;
    needPrime_ = true;  // con key lock, el estirador se vuelve a cebar desde aquí
}

bool DeckEngine::inGesture(const DeckParams& p) const {
    return rolling_ || motion_ != 0 || slipping_ || cueHeld_ || p.reverse.load(std::memory_order_relaxed) ||
           (p.scratching.load(std::memory_order_relaxed) && p.vinyl.load(std::memory_order_relaxed));
}

inline void DeckEngine::sampleAt(double pos, float& l, float& r) const {
    const int64_t n = (int64_t)track_->frames;
    const int64_t i = (int64_t)std::floor(pos);
    const float t = (float)(pos - (double)i);
    const float* d = track_->pcm.data();
    if (i >= 1 && i + 2 < n) {
        const float* p = d + (i - 1) * 2;
        // Hermite cúbica de 4 puntos
        auto h = [t](float y0, float y1, float y2, float y3) {
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            return ((c3 * t + c2) * t + c1) * t + y1;
        };
        l = h(p[0], p[2], p[4], p[6]);
        r = h(p[1], p[3], p[5], p[7]);
        return;
    }
    auto at = [&](int64_t k, int ch) { return (k >= 0 && k < n) ? d[k * 2 + ch] : 0.f; };
    l = at(i, 0) + (at(i + 1, 0) - at(i, 0)) * t;
    r = at(i, 1) + (at(i + 1, 1) - at(i, 1)) * t;
}

void DeckEngine::handle(const Command& c, DeckParams& p, DeckStatus& s) {
    switch (c.cmd) {
    case Cmd::Load:
    case Cmd::Unload:
        track_ = c.cmd == Cmd::Load ? c.track : nullptr;
        pos_ = 0; playing_ = false; cueHeld_ = false; playGain_ = 0; xfRemain_ = 0;
        loopActive_ = false; loopIn_ = loopOut_ = -1;
        hot_.fill(-1);
        ++jumps_;
        rolling_ = slipping_ = false; motion_ = 0;
        p.reverse = false; p.scratching = false; p.scratchVelocity = 0;
        wasScratching_ = lastReverse_ = false;
        cue_ = 0;
        if (track_) {
            const double sr = track_->sampleRate;
            cue_ = track_->meta.cueSec * sr;
            for (int i = 0; i < kNumHotCues; ++i)
                hot_[i] = track_->meta.hotCuesSec[i] >= 0 ? track_->meta.hotCuesSec[i] * sr : -1;
            pos_ = cue_;
        }
        eq_.reset(); color_.reset();
        stretchActive_ = false;
        needPrime_ = true;
        s.endReached.store(false);
        s.loadedGen.store(c.gen, std::memory_order_release);
        break;
    default: break;
    }
    if (!track_) return;
    const double frames = (double)track_->frames;
    // An explicit transport command wins over a held gesture. Release must not restart playback.
    if (c.cmd==Cmd::Pause || c.cmd==Cmd::TogglePlay || c.cmd==Cmd::Play || c.cmd==Cmd::Seek || c.cmd==Cmd::CueDown || c.cmd==Cmd::HotCue || c.cmd==Cmd::BeatJump) {
        if (rolling_) { rolling_=false; loopActive_=savedLoop_; loopIn_=savedIn_; loopOut_=savedOut_; restartStretch(); }
        slipping_=false; motion_=0;
    }
    switch (c.cmd) {
    case Cmd::RollStart: {
        if (rolling_ || motion_ || c.d <= 0 || c.d > 8) break;
        const double bl = beatLenFrames(p) > 0 ? beatLenFrames(p) : 0.5 * track_->sampleRate;
        const double len = bl * c.d;
        savedLoop_ = loopActive_; savedIn_ = loopIn_; savedOut_ = loopOut_;
        rollPos_ = slipping_ ? slipPos_ : pos_;
        rollWasPlaying_ = slipping_ ? slipPlaying_ : playing_;
        slipping_ = false;
        double in = pos_;
        if (p.quantize.load()) {
            const double anchor = p.gridAnchorSec.load() * track_->sampleRate;
            const double step = std::min(bl, len);
            in = std::max(0.0, anchor + std::floor((pos_ - anchor) / step) * step);
        }
        if (frames - in < 64) break;
        loopIn_ = in; loopOut_ = std::min(frames, in + len); loopActive_ = true;
        rolling_ = true; playing_ = true;
        jumpTo(in); restartStretch();
        break;
    }
    case Cmd::RollEnd:
        if (rolling_) {
            rolling_ = false; loopActive_ = savedLoop_; loopIn_ = savedIn_; loopOut_ = savedOut_;
            jumpTo(rollPos_); playing_ = rollWasPlaying_; restartStretch();
        }
        break;
    case Cmd::Backspin:
    case Cmd::Brake:
        if (!rolling_) {
            motion_ = c.cmd == Cmd::Backspin ? 1 : 2;
            motionLeft_ = motionTotal_ = std::clamp(c.d, 0.15, 4.0) * fs_;
            motionWasPlaying_ = playing_; playing_ = true;
            startTail();
        }
        break;
    case Cmd::TogglePlay:
        if (playing_ && !cueHeld_) playing_ = false;
        else { if (pos_ >= frames) jumpTo(cue_); playing_ = true; }
        cueHeld_ = false;
        break;
    case Cmd::Play: if (pos_ >= frames) jumpTo(cue_); playing_ = true; cueHeld_ = false; break;
    case Cmd::Pause: playing_ = false; cueHeld_ = false; break;
    case Cmd::CueDown:
        if (playing_ && !cueHeld_) {
            playing_ = false;
            jumpTo(cue_);
        } else if (!playing_) {
            if (std::fabs(pos_ - cue_) < 1.0) {
                cueHeld_ = true; playing_ = true;  // vista previa mientras se mantiene
            } else {
                cue_ = snapToBeat(pos_, p);
                if (cue_ != pos_) jumpTo(cue_);
            }
        }
        break;
    case Cmd::CueUp:
        if (cueHeld_) { cueHeld_ = false; playing_ = false; jumpTo(cue_); }
        break;
    case Cmd::Seek: jumpTo(c.d); s.endReached.store(false); break;
    case Cmd::SetCueHere: cue_ = snapToBeat(pos_, p); break;
    case Cmd::SetCueAt: cue_ = std::clamp(c.d, 0.0, frames); break;
    case Cmd::HotCue:
        if (c.i < 0 || c.i >= kNumHotCues) break;
        if (hot_[c.i] < 0) hot_[c.i] = snapToBeat(pos_, p);
        else { jumpTo(hot_[c.i]); playing_ = true; cueHeld_ = false; }
        break;
    case Cmd::HotCueDelete:
        if (c.i >= 0 && c.i < kNumHotCues) hot_[c.i] = -1;
        break;
    case Cmd::SetHotCueAt:
        if (c.i >= 0 && c.i < kNumHotCues) hot_[c.i] = std::clamp(snapToBeat(c.d, p), 0.0, frames);
        break;
    case Cmd::LoopIn:
        loopIn_ = snapToBeat(pos_, p); loopOut_ = -1; loopActive_ = false;
        break;
    case Cmd::LoopOut:
        if (loopIn_ >= 0 && pos_ > loopIn_ + 64) {
            double o = snapToBeat(pos_, p);
            if (o <= loopIn_ + 64) o = pos_;
            loopOut_ = o; loopActive_ = true;
            if (pos_ >= loopOut_) jumpTo(loopIn_ + std::fmod(pos_ - loopIn_, loopOut_ - loopIn_));
            restartStretch();
        }
        break;
    case Cmd::AutoLoop: {
        double bl = beatLenFrames(p);
        if (bl <= 0) bl = 0.5 * track_->sampleRate;  // sin BPM: 1 pulso = 0,5 s
        const double len = bl * c.d;
        if (loopActive_ && std::fabs((loopOut_ - loopIn_) - len) < 1.0) { loopActive_ = false; break; }
        double in = pos_;
        if (p.quantize.load() && beatLenFrames(p) > 0) {
            const double anchor = p.gridAnchorSec.load() * track_->sampleRate;
            const double step = c.d < 1.0 ? len : bl;  // loops cortos se alinean a su propia longitud
            in = anchor + std::floor((pos_ - anchor) / step + 1e-6) * step;
            if (in < 0) in = 0;
        }
        loopIn_ = in; loopOut_ = std::min(frames, in + len); loopActive_ = loopOut_ > loopIn_ + 1;
        if (pos_ >= loopOut_) jumpTo(loopIn_ + std::fmod(pos_ - loopIn_, len));
        restartStretch();
        break;
    }
    case Cmd::LoopHalve:
        if (loopIn_ >= 0 && loopOut_ > loopIn_ + 128) {
            loopOut_ = loopIn_ + (loopOut_ - loopIn_) * 0.5;
            if (loopActive_ && pos_ >= loopOut_) jumpTo(loopIn_ + std::fmod(pos_ - loopIn_, loopOut_ - loopIn_));
            restartStretch();
        }
        break;
    case Cmd::LoopDouble:
        if (loopIn_ >= 0 && loopOut_ > loopIn_) { loopOut_ = std::min(frames, loopIn_ + (loopOut_ - loopIn_) * 2.0); restartStretch(); }
        break;
    case Cmd::LoopExit: loopActive_ = false; restartStretch(); break;
    case Cmd::Reloop:
        if (loopIn_ >= 0 && loopOut_ > loopIn_) { loopActive_ = true; jumpTo(loopIn_); playing_ = true; cueHeld_ = false; }
        break;
    case Cmd::BeatJump: {
        double bl = beatLenFrames(p);
        if (bl <= 0) bl = 0.5 * track_->sampleRate;
        const double delta = bl * c.d;
        if (loopActive_) { loopIn_ += delta; loopOut_ += delta; }
        jumpTo(pos_ + delta);
        break;
    }
    default: break;
    }
}

void DeckEngine::publish(DeckStatus& s) {
    s.rolling.store(rolling_, std::memory_order_relaxed);
    s.slipActive.store(slipping_ || rolling_, std::memory_order_relaxed);
    s.slipFrames.store(rolling_ ? rollPos_ : slipPos_, std::memory_order_relaxed);
    s.posFrames.store(pos_, std::memory_order_relaxed);
    s.playing.store(playing_, std::memory_order_relaxed);
    s.hasTrack.store(track_ != nullptr, std::memory_order_relaxed);
    s.cueFrames.store(cue_, std::memory_order_relaxed);
    for (int i = 0; i < kNumHotCues; ++i) s.hotCues[i].store(hot_[i], std::memory_order_relaxed);
    s.loopActive.store(loopActive_, std::memory_order_relaxed);
    s.loopIn.store(loopIn_, std::memory_order_relaxed);
    s.loopOut.store(loopOut_, std::memory_order_relaxed);
}

bool DeckEngine::advanceWrap(double& p, double rate) const {
    p += rate;
    if (loopActive_ && rate > 0 && loopOut_ > loopIn_ && p >= loopOut_) {
        p = loopIn_ + std::fmod(p - loopIn_, loopOut_ - loopIn_);
        return true;
    }
    return false;
}

void DeckEngine::restartStretch() {
    if (!stretchActive_) return;
    if (playGain_ > 0.f) startTail();
    needPrime_ = true;
}

void DeckEngine::primeStretch(double rate) {
    // El lector arranca en la posición que se oye; la salida del estirador
    // queda alineada con ella (outputSeek) y el historial dice qué posición
    // corresponde a cada fotograma que sale.
    const int L = stretch_.primeLength();
    double fp = pos_;
    for (int k = 0; k < L; ++k) {
        sampleAt(fp, primeL_[k], primeR_[k]);
        hist_[k % hist_.size()] = fp;
        advanceWrap(fp, rate);
    }
    stretch_.prime(primeL_.data(), primeR_.data(), L);
    histW_ = (uint64_t)L;
    feedPos_ = fp;
    stretchActive_ = true;
    needPrime_ = false;
}

void DeckEngine::render(float* L, float* R, int n, DeckParams& p, DeckStatus& s) {
    const double fxBeat0 = beatPosition(p);   // reloj de los efectos nuevos del plato (pulsos al empezar el bloque)
    if (!track_) {
        std::memset(L, 0, sizeof(float) * n);
        std::memset(R, 0, sizeof(float) * n);
        stretchActive_ = false;
        needPrime_ = true;
    } else {
        const double ratio = (double)track_->sampleRate / fs_;
        const bool scratching = p.scratching.load(std::memory_order_relaxed) && p.vinyl.load();
        const bool reverse = p.reverse.load();
        if (reverse != lastReverse_) { startTail(); needPrime_ = true; lastReverse_ = reverse; }
        const bool temporary = p.slip.load() && !rolling_ && (scratching || reverse || loopActive_ || motion_ != 0);
        if (temporary && !slipping_) { slipping_ = true; slipPos_ = pos_; slipPlaying_ = motion_ ? motionWasPlaying_ : playing_; }
        if (!temporary && slipping_) {
            slipping_ = false;
            if (p.slip.load()) { jumpTo(slipPos_); playing_ = slipPlaying_; }
        }

        if (scratching && !wasScratching_) scratchVel_ = playing_ ? 1.f + p.pitch.load() : 0.f;
        wasScratching_ = scratching;
        const float svTarget = p.scratchVelocity.load(std::memory_order_relaxed);
        const double tempo = std::max(0.05, 1.0 + p.pitch.load(std::memory_order_relaxed) + p.bend.load(std::memory_order_relaxed) + syncCorr_);
        const double normalRate = ratio * tempo;
        const float gainStep = (float)(1.0 / (0.004 * fs_));
        const float svCoef = (float)(1.0 - std::exp(-1.0 / (0.008 * fs_)));
        const double frames = (double)track_->frames;

        // Key lock / transposición: el estirador corrige el cambio de tono de la
        // lectura a velocidad variable. Durante el scratch se usa la lectura directa.
        const bool keyLock = p.keyLock.load(std::memory_order_relaxed);
        const float shift = std::clamp(p.keyShift.load(std::memory_order_relaxed), -12.f, 12.f);
        const bool wantStretch = !scratching && !reverse && !motion_ && (keyLock || std::fabs(shift) > 0.01f);
        const float transpose = std::pow(2.f, shift / 12.f) / (keyLock ? (float)std::max(0.25, tempo) : 1.f);
        bool goingToPlay = scratching || playing_;
        const bool wasActive = stretchActive_;
        if (wantStretch && needPrime_ && goingToPlay) primeStretch(normalRate);
        // Paso de lectura directa a estirada: fundido de un bloque entre ambas.
        const bool activating = wantStretch && stretchActive_ && !wasActive;
        const bool deactivating = !wantStretch && stretchActive_;
        const bool runStretch = stretchActive_ && !needPrime_ && (wantStretch || deactivating);
        const bool needDirect = !runStretch || activating || deactivating;
        const size_t hs = hist_.size();
        const uint64_t Lp = (uint64_t)stretch_.primeLength();

        for (int i = 0; i < n; ++i) {
            // Arranque con rampa; la parada es inmediata en posición y el
            // fundido de salida lo hace la "cola" (xf), así el plato queda
            // exactamente donde se pausó.
            if (goingToPlay) playGain_ = std::min(1.f, playGain_ + gainStep);
            else if (playGain_ > 0.f) { startTail(); playGain_ = 0.f; }

            double rate;
            if (scratching) { scratchVel_ += (svTarget - scratchVel_) * svCoef; rate = scratchVel_ * ratio; }
            else if (motion_) {
                rate = ratio * (motion_ == 1 ? -4.0 : tempo) * motionLeft_ / motionTotal_;
                if (--motionLeft_ <= 0) {
                    motion_ = 0;
                    if (slipping_) { slipping_ = false; jumpTo(slipPos_); playing_ = slipPlaying_; }
                    else playing_ = false;
                    goingToPlay = playing_;
                }
            } else rate = reverse ? -normalRate : normalRate;
            if (rolling_ && rollWasPlaying_) {
                rollPos_ += normalRate;
                if (savedLoop_ && savedOut_ > savedIn_ && rollPos_ >= savedOut_)
                    rollPos_ = savedIn_ + std::fmod(rollPos_ - savedIn_, savedOut_ - savedIn_);
                rollPos_ = std::min(rollPos_, frames);
            }
            if (slipping_ && slipPlaying_) slipPos_ = std::min(frames, slipPos_ + normalRate);

            gArr_[i] = playGain_;
            float tl = 0.f, tr = 0.f, t = 0.f;
            if (xfRemain_ > 0) {
                t = (float)xfRemain_ / kXf;
                sampleAt(xfPos_, tl, tr);
                tl *= xfGain_ * t; tr *= xfGain_ * t;
                xfPos_ += xfRate_;
                --xfRemain_;
            }
            tArr_[i] = t; tailL_[i] = tl; tailR_[i] = tr;
            dirL_[i] = dirR_[i] = 0.f;
            inL_[i] = inR_[i] = 0.f;
            if (playGain_ <= 0.f) {
                if (runStretch) needPrime_ = true;  // al reanudar se vuelve a cebar
                continue;
            }
            if (needDirect) { sampleAt(pos_, dirL_[i], dirR_[i]); dirL_[i] *= playGain_; dirR_[i] *= playGain_; }
            lastRate_ = rate;
            if (runStretch) {
                sampleAt(feedPos_, inL_[i], inR_[i]);
                hist_[histW_ % hs] = feedPos_;
                ++histW_;
                advanceWrap(feedPos_, rate);
                // Posición del siguiente fotograma que sale del estirador.
                pos_ = hist_[(histW_ - Lp) % hs];
            } else {
                double before = pos_;
                if (advanceWrap(pos_, rate)) { xfPos_ = before + rate; xfRate_ = rate; xfGain_ = playGain_; xfRemain_ = kXf; }
            }
            if (pos_ >= frames && rate > 0) {
                pos_ = frames;
                if (playing_) { playing_ = false; cueHeld_ = false; s.endReached.store(true, std::memory_order_relaxed); }
            }
            if (pos_ < 0) pos_ = 0;
        }

        if (runStretch) {
            stretch_.process(inL_, inR_, L, R, n, transpose);
            const float inv = 1.f / n;
            for (int i = 0; i < n; ++i) {
                float sl = L[i] * gArr_[i], sr = R[i] * gArr_[i];
                if (activating) { const float w = (i + 1) * inv; sl = sl * w + dirL_[i] * (1 - w); sr = sr * w + dirR_[i] * (1 - w); }
                else if (deactivating) { const float w = (i + 1) * inv; sl = dirL_[i] * w + sl * (1 - w); sr = dirR_[i] * w + sr * (1 - w); }
                L[i] = sl * (1.f - tArr_[i]) + tailL_[i];
                R[i] = sr * (1.f - tArr_[i]) + tailR_[i];
            }
            if (deactivating) { stretchActive_ = false; needPrime_ = true; }
        } else {
            for (int i = 0; i < n; ++i) {
                L[i] = dirL_[i] * (1.f - tArr_[i]) + tailL_[i];
                R[i] = dirR_[i] * (1.f - tArr_[i]) + tailR_[i];
            }
            if (!wantStretch) { stretchActive_ = false; needPrime_ = true; }
        }
    }

    // Ganancia de entrada con rampa.
    const float trim = dbToLin(std::clamp(p.gainDb.load(std::memory_order_relaxed), -24.f, 12.f));
    const float inv = 1.f / n;
    for (int i = 0; i < n; ++i) {
        const float g = lastTrim_ + (trim - lastTrim_) * (i + 1) * inv;
        L[i] *= g; R[i] *= g;
    }
    lastTrim_ = trim;

    // 0.14.0 Pro: Stems en tiempo real por plato (Voz, Batería, Bajo, Instrumentos)
    {
        float stG[4], stFx[4], stPk[4] = {};
        for (int k = 0; k < 4; ++k) {
            const bool muted = p.stemMute[k].load(std::memory_order_relaxed);
            stG[k] = muted ? 0.f : std::clamp(p.stemVol[k].load(std::memory_order_relaxed), 0.f, 1.5f);
            stFx[k] = std::clamp(p.stemFx[k].load(std::memory_order_relaxed), 0.f, 1.f);
        }
        stemProc_.process(L, R, n, stG, stFx, stPk);
        for (int k = 0; k < 4; ++k) {
            s.stemPeak[k].store(stPk[k], std::memory_order_relaxed);
        }
    }

    const float gl = eqKnobToGain(p.eqLow.load(std::memory_order_relaxed));
    const float gm = eqKnobToGain(p.eqMid.load(std::memory_order_relaxed));
    const float gh = eqKnobToGain(p.eqHigh.load(std::memory_order_relaxed));
    eq_.process(L, R, n, lastGl_, gl, lastGm_, gm, lastGh_, gh);
    lastGl_ = gl; lastGm_ = gm; lastGh_ = gh;

    color_.process(L, R, n, p.colorFx.load(std::memory_order_relaxed), std::clamp(p.filter.load(std::memory_order_relaxed), -1.f, 1.f));

    const int fxt = p.fxType.load(std::memory_order_relaxed);
    if (fxt >= 0 && fxt < (int)FxType::Count) {
        double bpm = effectiveBpm(p);
        const int st = deckFxToStation(fxt);   // 0 = efecto de siempre (FxSlot)
        const bool on = p.fxOn.load(std::memory_order_relaxed);
        const float beats = p.fxBeats.load(std::memory_order_relaxed);
        fx_.process(L, R, n, st ? FxType::Ninguno : (FxType)fxt, on, p.fxAmount.load(std::memory_order_relaxed), beats, bpm > 0 ? bpm : 120.0);
        // Los nuevos: una ranura como las del rack. Se llama siempre (con tipo 0 si no toca) para que
        // al cambiar de efecto se apague con su fundido y deje sonar su cola.
        double b0 = fxBeat0, b1 = beatPosition(p);
        if (bpm > 0 && b1 != b0 && std::isfinite(b0) && std::isfinite(b1)) fxFreeBeat_ = b1;
        else { b0 = fxFreeBeat_; fxFreeBeat_ += n * (bpm > 0 ? bpm : 120.0) / 60.0 / fs_; b1 = fxFreeBeat_; }
        if (fxFreeBeat_ > 1e6) fxFreeBeat_ = std::fmod(fxFreeBeat_, 64.0);
        float amount = 0.5f, mix = 1.f;
        deckFxUnitParams(st, std::clamp(p.fxAmount.load(std::memory_order_relaxed), 0.f, 1.f), amount, mix);
        fxUnit_.processInsert(L, R, n, st, st != 0 && on, amount, mix, beats, bpm > 0 ? bpm : 120.0, b0, b1);
    }

    float pl = 0, pr = 0;
    for (int i = 0; i < n; ++i) { pl = std::max(pl, std::fabs(L[i])); pr = std::max(pr, std::fabs(R[i])); }
    s.peakL.store(std::max(pl, s.peakL.load(std::memory_order_relaxed)), std::memory_order_relaxed);
    s.peakR.store(std::max(pr, s.peakR.load(std::memory_order_relaxed)), std::memory_order_relaxed);
    publish(s);
}

// ============================================================== Engine

Engine::Engine() {
    recRing.allocate(1 << 21);  // ~21 s estéreo a 48 kHz
    hpRing.allocate(1 << 15);
    prepare(48000);
}

void Engine::prepare(double fs) {
    fs_ = fs;
    for (auto& d : decks_) d.prepare(fs);
    limiter_.prepare(fs, -0.3f);
    hpLimiter_.prepare(fs, -1.0f);
    rack.prepare(fs);
}

double Engine::effectiveBpm(int d) const {
    return deck[d].bpm.load() * (1.0 + deck[d].pitch.load());
}

void Engine::process(float* out, int frames, int outChannels) {
    const auto t0 = std::chrono::steady_clock::now();
    Command c;
    while (queue_.pop(c)) {
        if (c.cmd == Cmd::StemLoad) {
            stemSet_ = c.stems ? *c.stems : StemSet{};
            stemPos_ = 0; stemPlaying_ = false; stemGain_ = 0.f;
            stemStatus.loadedGen.store(c.gen, std::memory_order_release);
            continue;
        }
        if (c.cmd == Cmd::StemPlay) { if (stemSet_.count > 0) stemPlaying_ = true; continue; }
        if (c.cmd == Cmd::StemPause) { stemPlaying_ = false; continue; }
        if (c.cmd == Cmd::StemSeek) { stemPos_ = std::max(0.0, c.d); continue; }
        if (c.cmd == Cmd::RackLoop) { rack.command(c.i, (int)c.d); continue; }
        if (c.cmd == Cmd::RackPad) { rack.padCommand(c.i, (int)c.d, c.track, c.gen); continue; }
        if (c.deck >= kNumDecks) continue;
        if (c.cmd == Cmd::SyncPhase) {
            // Alinear ya (al pulsar SYNC o al arrancar un plato sincronizado). c.d > 0
            // fija la relación de tempo (para alinear el plato que manda con el que le sigue).
            const int me = c.deck, other = c.i;
            if (other < 0 || other >= kNumDecks || other == me) continue;
            double err = 0, unit = 0;
            const double k = c.d > 0 ? c.d : deck[me].syncFactor.load();
            // Un plato que suena no salta para alinearse con otro parado: lo hará el SYNC cuando ambos suenen.
            const bool audibleJump = decks_[me].playing() && !decks_[other].playing();
            if (!audibleJump && phaseError(me, other, k, err, unit)) decks_[me].jumpTo(alignedPosition(decks_[me].position(), err, unit));
            sync_[me].offset = 0;
            sync_[me].nudging = false;
            sync_[me].ownJumps = decks_[me].jumpCount();
            sync_[me].otherJumps = decks_[other].jumpCount();
            continue;
        }
        decks_[c.deck].handle(c, deck[c.deck], deckStatus[c.deck]);
    }

    int done = 0;
    while (done < frames) {
        const int n = std::min(kMaxBlock, frames - done);
        processBlock(out + (size_t)done * outChannels, n, outChannels);
        done += n;
    }
    status.framesRendered.fetch_add((uint64_t)frames, std::memory_order_relaxed);

    const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const float load = (float)(el / (frames / fs_));
    const float prev = status.cpuLoad.load(std::memory_order_relaxed);
    status.cpuLoad.store(prev + (load - prev) * 0.1f, std::memory_order_relaxed);
}

void Engine::renderStems(int n) {
    std::memset(sL_, 0, sizeof(float) * n); std::memset(sR_, 0, sizeof(float) * n);
    if (stemSet_.count <= 0 || !stemSet_.tracks[0]) { stemStatus.playing.store(false); return; }
    const Track* t0 = stemSet_.tracks[0];
    const double ratio = (double)t0->sampleRate / fs_;
    const double frames = (double)t0->frames;
    bool anySolo = false;
    for (int k = 0; k < stemSet_.count; ++k) if (stem.solo[k].load(std::memory_order_relaxed)) anySolo = true;
    const float step = (float)(1.0 / (0.004 * fs_));
    const float vol = faderToGain(std::clamp(stem.volume.load(std::memory_order_relaxed), 0.f, 1.f));
    float target[kStemSlots];
    for (int k = 0; k < stemSet_.count; ++k) {
        const bool audible = !stem.mute[k].load(std::memory_order_relaxed) && (!anySolo || stem.solo[k].load(std::memory_order_relaxed));
        target[k] = audible ? std::clamp(stem.gain[k].load(std::memory_order_relaxed), 0.f, 1.5f) : 0.f;
    }
    float peaks[kStemSlots] = {};
    const float inv = 1.f / n;
    for (int i = 0; i < n; ++i) {
        if (stemPlaying_) stemGain_ = std::min(1.f, stemGain_ + step);
        else stemGain_ = std::max(0.f, stemGain_ - step);
        if (stemGain_ <= 0.f) continue;
        const int64_t idx = (int64_t)stemPos_;
        const float fr = (float)(stemPos_ - (double)idx);
        float l = 0, r = 0;
        for (int k = 0; k < stemSet_.count; ++k) {
            const Track* t = stemSet_.tracks[k];
            if (!t || idx + 1 >= (int64_t)t->frames) continue;
            const float* p = t->pcm.data() + idx * 2;
            const float g = stemLastGain_[k] + (target[k] - stemLastGain_[k]) * (i + 1) * inv;
            const float sl = p[0] + (p[2] - p[0]) * fr, sr = p[1] + (p[3] - p[1]) * fr;
            l += sl * g; r += sr * g;
            peaks[k] = std::max(peaks[k], std::max(std::fabs(sl * g), std::fabs(sr * g)));
        }
        sL_[i] = l * stemGain_ * vol; sR_[i] = r * stemGain_ * vol;
        if (stemPlaying_) {
            stemPos_ += ratio;
            if (stemPos_ >= frames) { stemPos_ = frames; stemPlaying_ = false; }
        }
    }
    for (int k = 0; k < stemSet_.count; ++k) {
        stemLastGain_[k] = target[k];
        stemStatus.peak[k].store(std::max(peaks[k], stemStatus.peak[k].load(std::memory_order_relaxed)), std::memory_order_relaxed);
    }
    const bool pfl = stem.pfl.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i) {
        mL_[i] += sL_[i]; mR_[i] += sR_[i];
        if (pfl) { cL_[i] += sL_[i]; cR_[i] += sR_[i]; }
    }
    stemStatus.posFrames.store(stemPos_, std::memory_order_relaxed);
    stemStatus.playing.store(stemPlaying_, std::memory_order_relaxed);
}

// Posición alineada más cercana; si queda antes del inicio de la pista, se avanza una unidad.
double Engine::alignedPosition(double pos, double err, double unit) {
    double t = pos - err * unit;
    if (t < 0 && unit > 0) t += unit * std::ceil(-t / unit);
    return t;
}

bool Engine::phaseError(int me, int other, double factor, double& err, double& unitFrames) const {
    const DeckEngine& a = decks_[me];
    const DeckEngine& b = decks_[other];
    if (!a.track() || !b.track()) return false;
    const double blA = a.beatLenFrames(deck[me]), blB = b.beatLenFrames(deck[other]);
    if (blA <= 0 || blB <= 0) return false;
    const double k = std::clamp(factor, 0.25, 4.0);
    const double beats = (deck[me].syncBar.load(std::memory_order_relaxed) || deck[other].syncBar.load(std::memory_order_relaxed)) &&
                         (deck[me].syncTo.load(std::memory_order_relaxed) == other || deck[other].syncTo.load(std::memory_order_relaxed) == me) ? 4.0 : 1.0;
    // Unidad: un pulso (o compás) del otro plato, medido en fotogramas de cada pista.
    unitFrames = blA / k * beats;
    const double unitB = blB * beats;
    const double ancA = deck[me].gridAnchorSec.load(std::memory_order_relaxed) * a.track()->sampleRate;
    const double ancB = deck[other].gridAnchorSec.load(std::memory_order_relaxed) * b.track()->sampleRate;
    double ph = (a.position() - ancA) / unitFrames - (b.position() - ancB) / unitB;
    ph -= std::floor(ph + 0.5);  // [-0,5, 0,5)
    err = ph;
    return true;
}

void Engine::updateSync(int n) {
    for (int d = 0; d < kNumDecks; ++d) {
        DeckEngine& a = decks_[d];
        SyncState& ss = sync_[d];
        a.setSyncCorrection(0);
        const int o = deck[d].syncTo.load(std::memory_order_relaxed);
        if (o < 0 || o >= kNumDecks || o == d) { ss.target = -1; deckStatus[d].syncLocked.store(false, std::memory_order_relaxed); continue; }
        const DeckEngine& b = decks_[o];
        const double bpmA = deck[d].bpm.load(std::memory_order_relaxed), bpmB = deck[o].bpm.load(std::memory_order_relaxed);
        if (!a.track() || !b.track() || bpmA <= 0 || bpmB <= 0) continue;
        if (ss.target != o) { ss = SyncState{}; ss.target = o; ss.ownJumps = a.jumpCount(); ss.otherJumps = b.jumpCount(); }
        // 1) Tempo: el del pitch del otro plato. Su empujón (jog, botones) NO se copia: empujar
        //    cualquiera de los dos es la forma de cuadrar a mano, y el desfase que deje se respeta.
        const double k = std::clamp(deck[d].syncFactor.load(std::memory_order_relaxed), 0.25, 4.0);
        const double tempoB = 1.0 + deck[o].pitch.load(std::memory_order_relaxed);
        const double pitchA = std::clamp(bpmB * tempoB / (bpmA * k) - 1.0, -0.5, 0.5);
        deck[d].pitch.store((float)pitchA, std::memory_order_relaxed);
        // 2) Fase.
        double err = 0, unit = 0;
        if (!phaseError(d, o, k, err, unit)) continue;
        bool ownJump = a.jumpCount() != ss.ownJumps, otherJump = b.jumpCount() != ss.otherJumps;
        ss.ownJumps = a.jumpCount(); ss.otherJumps = b.jumpCount();
        // Al acabar un scratch, reverse o freno (en cualquiera de los dos) se realinea como tras un salto.
        const bool gA = a.inGesture(deck[d]), gB = b.inGesture(deck[o]);
        if (ss.ownGesture && !gA) ownJump = true;
        if (ss.otherGesture && !gB) otherJump = true;
        ss.ownGesture = gA; ss.otherGesture = gB;
        const bool bendA = std::fabs(deck[d].bend.load(std::memory_order_relaxed)) > 1e-6f ||
                           (b.playing() && std::fabs(deck[o].bend.load(std::memory_order_relaxed)) > 1e-6f);
        const bool active = a.playing() && b.playing() && !a.inGesture(deck[d]) && !b.inGesture(deck[o]);
        double e = err - ss.offset;
        e -= std::floor(e + 0.5);
        deckStatus[d].syncError.store((float)e, std::memory_order_relaxed);
        // SYNC solo de tempo: la fase la pone el DJ a mano (el medidor sigue informando).
        if (!deck[d].syncPhase.load(std::memory_order_relaxed)) { ss.offset = 0; ss.nudging = false; deckStatus[d].syncLocked.store(std::fabs(e) < 0.02, std::memory_order_relaxed); continue; }
        if (bendA) { ss.nudging = true; deckStatus[d].syncLocked.store(false, std::memory_order_relaxed); continue; }
        if (ss.nudging) { ss.nudging = false; ss.offset = err; continue; }  // el empujón manual se respeta
        if (!active) { ss.bigErrBlocks = 0; deckStatus[d].syncLocked.store(std::fabs(e) < 0.02, std::memory_order_relaxed); continue; }
        const double beatsPerUnit = deck[d].syncBar.load(std::memory_order_relaxed) ? 4.0 : 1.0;
        const double eBeats = e * beatsPerUnit;
        // Un salto (hot cue, salto de pulsos, cue) que deja los pulsos desplazados se corrige en el acto, con otro salto.
        if ((ownJump || otherJump) && std::fabs(eBeats) > 0.03) {
            a.jumpTo(alignedPosition(a.position(), e, unit));
            ss.bigErrBlocks = 0;
            continue;
        }
        // El resto, con una corrección de tempo pequeña (inaudible): la fase converge en ~1 s.
        const double beatSec = 60.0 / (bpmB * std::max(0.1, tempoB));
        double corr = -eBeats * beatSec / 1.0;
        const double maxCorr = std::fabs(eBeats) > 0.12 ? 0.02 : 0.006;
        corr = std::clamp(corr, -maxCorr, maxCorr);
        if (std::fabs(eBeats) < 0.001) corr = 0;
        a.setSyncCorrection(corr);
        deckStatus[d].syncLocked.store(std::fabs(eBeats) < 0.02, std::memory_order_relaxed);
        (void)n;
    }
}

void Engine::processBlock(float* out, int n, int outChannels) {
    updateSync(n);
    RackCtx rc;
    for (int d = 0; d < kNumDecks; ++d) rc.beat0[d] = decks_[d].beatPosition(deck[d]);
    for (int d = 0; d < kNumDecks; ++d) decks_[d].render(dL_[d], dR_[d], n, deck[d], deckStatus[d]);
    // Rack: tempo de cada plato y plato que manda en la mezcla (el que suena más alto; si empatan, el que no sigue a otro).
    float audible[kNumDecks];
    for (int d = 0; d < kNumDecks; ++d) {
        rc.beat1[d] = decks_[d].beatPosition(deck[d]);
        rc.grid[d] = decks_[d].track() && deck[d].bpm.load(std::memory_order_relaxed) > 0;
        rc.bpm[d] = rc.grid[d] ? decks_[d].effectiveBpm(deck[d]) : 0.0;
        rc.playing[d] = decks_[d].playing();
        audible[d] = rc.playing[d] && rc.grid[d] ? lastFader_[d] * (d == 0 ? lastXfA_ : lastXfB_) + 1e-3f : 0.f;
    }
    if (audible[0] > 0 || audible[1] > 0) {
        rc.lead = audible[1] > audible[0] + 1e-4f ? 1 : audible[0] > audible[1] + 1e-4f ? 0 : (deck[0].syncTo.load(std::memory_order_relaxed) == 1 ? 1 : 0);
    } else rc.lead = rc.grid[0] ? 0 : rc.grid[1] ? 1 : -1;
    rack.beginBlock(rc, n);
    for (int d = 0; d < kNumDecks; ++d) {
        rack.processInsert(d, dL_[d], dR_[d], n);   // aislador y estación en inserción (antes del fader)
        rack.tapLoop(d, dL_[d], dR_[d], n);
        if (rack.touches(d)) {   // el medidor del canal muestra el nivel con el rack
            float pl = 0, pr = 0;
            for (int i = 0; i < n; ++i) { pl = std::max(pl, std::fabs(dL_[d][i])); pr = std::max(pr, std::fabs(dR_[d][i])); }
            deckStatus[d].peakL.store(std::max(pl, deckStatus[d].peakL.load(std::memory_order_relaxed)), std::memory_order_relaxed);
            deckStatus[d].peakR.store(std::max(pr, deckStatus[d].peakR.load(std::memory_order_relaxed)), std::memory_order_relaxed);
        }
    }

    float xa = 1.f, xb = 1.f;
    crossfaderGains(mixer.crossfader.load(std::memory_order_relaxed), (XfCurve)mixer.xfCurve.load(std::memory_order_relaxed), xa, xb);
    const float xg[kNumDecks] = {xa, xb};
    const float xlast[kNumDecks] = {lastXfA_, lastXfB_};
    const float master = faderToGain(mixer.masterVol.load(std::memory_order_relaxed));
    const float inv = 1.f / n;

    std::memset(mL_, 0, sizeof(float) * n); std::memset(mR_, 0, sizeof(float) * n);
    std::memset(cL_, 0, sizeof(float) * n); std::memset(cR_, 0, sizeof(float) * n);
    renderStems(n);  // escribe en sL_/sR_ y los suma al master (y a la preescucha si se pide)
    for (int d = 0; d < kNumDecks; ++d) {
        const float f = faderToGain(deck[d].fader.load(std::memory_order_relaxed));
        const float g1 = f * xg[d], g0 = lastFader_[d] * xlast[d];
        for (int i = 0; i < n; ++i) {
            const float g = g0 + (g1 - g0) * (i + 1) * inv;
            mL_[i] += dL_[d][i] * g;
            mR_[i] += dR_[d][i] * g;
        }
        if (deck[d].pfl.load(std::memory_order_relaxed)) {
            for (int i = 0; i < n; ++i) { cL_[i] += dL_[d][i]; cR_[i] += dR_[d][i]; }
        }
        rack.tapSend(d, dL_[d], dR_[d], n, g0, g1);   // envío del rack: después del fader y del crossfader
        lastFader_[d] = f;
    }
    lastXfA_ = xa; lastXfB_ = xb;
    // Instrumentos del rack (caja de ritmos, bajo y sampler) a la mezcla.
    rack.mixInstruments(mL_, mR_, cL_, cR_, n);
    // Rack sobre la mezcla, retornos de efectos y loop station (antes del volumen master y del limitador).
    rack.processInsert((int)RackSrc::Mezcla, mL_, mR_, n);
    rack.tapSend((int)RackSrc::Mezcla, mL_, mR_, n, 1.f, 1.f);
    rack.tapLoop((int)RackSrc::Mezcla, mL_, mR_, n);
    rack.finishBlock(mL_, mR_, cL_, cR_, n);

    float pre = 0;
    for (int i = 0; i < n; ++i) {
        const float g = lastMaster_ + (master - lastMaster_) * (i + 1) * inv;
        mL_[i] *= g; mR_[i] *= g;
        pre = std::max(pre, std::max(std::fabs(mL_[i]), std::fabs(mR_[i])));
    }
    lastMaster_ = master;
    status.preLimiterPeak.store(std::max(pre, status.preLimiterPeak.load(std::memory_order_relaxed)), std::memory_order_relaxed);

    const float lg = limiter_.process(mL_, mR_, n);
    if (lg < status.limiterGain.load(std::memory_order_relaxed)) status.limiterGain.store(lg, std::memory_order_relaxed);
    if (lg < 0.891f) status.limiterHits.fetch_add(1, std::memory_order_relaxed);

    MeterState ms;
    measure(mL_, mR_, n, ms);
    status.masterPeakL.store(std::max(ms.peakL, status.masterPeakL.load(std::memory_order_relaxed)), std::memory_order_relaxed);
    status.masterPeakR.store(std::max(ms.peakR, status.masterPeakR.load(std::memory_order_relaxed)), std::memory_order_relaxed);
    status.masterRmsL.store(ms.rmsL, std::memory_order_relaxed);
    status.masterRmsR.store(ms.rmsR, std::memory_order_relaxed);

    if (mixer.recording.load(std::memory_order_relaxed)) {
        for (int i = 0; i < n; ++i) { inter_[i * 2] = mL_[i]; inter_[i * 2 + 1] = mR_[i]; }
        if (!recRing.write(inter_, (size_t)n * 2)) status.recDropped.fetch_add((uint64_t)n * 2, std::memory_order_relaxed);
    }

    const HpRouting routing = (HpRouting)mixer.hpRouting.load(std::memory_order_relaxed);
    const int spkPair = std::clamp(mixer.spkChanPair.load(std::memory_order_relaxed), 0, 5);
    const int hpPair  = std::clamp(mixer.hpChanPair.load(std::memory_order_relaxed), 0, 5);
    const int hwCh    = mixer.hwChannels.load(std::memory_order_relaxed);
    bool anyPfl = deck[0].pfl.load(std::memory_order_relaxed) || deck[1].pfl.load(std::memory_order_relaxed) || stem.pfl.load(std::memory_order_relaxed);

    // Tono de prueba si el usuario pulsa "Probar" en Configuración de Audio
    int toneRem = mixer.testToneFrames.load(std::memory_order_relaxed);
    const int toneTgt = mixer.testToneTarget.load(std::memory_order_relaxed);
    static double tonePhase = 0.0;
    if (toneRem > 0) {
        const double step1 = 2.0 * kPi * 440.0 / std::max(8000.0, fs_);
        const double step2 = 2.0 * kPi * 554.37 / std::max(8000.0, fs_);
        for (int i = 0; i < n; ++i) {
            const float sTone = 0.22f * (float)(std::sin(tonePhase) + 0.7 * std::sin(tonePhase * (step2 / step1)));
            tonePhase += step1;
            if (tonePhase > 2.0 * kPi * 1000.0) tonePhase -= 2.0 * kPi * 1000.0;
            if (toneTgt == 1) { mL_[i] += sTone; mR_[i] += sTone; }
            else if (toneTgt == 2) { cL_[i] += sTone; cR_[i] += sTone; anyPfl = true; }
        }
        mixer.testToneFrames.store(std::max(0, toneRem - n), std::memory_order_relaxed);
    }

    if (routing == HpRouting::DecksSeparados) {
        const float gA = faderToGain(deck[0].fader.load(std::memory_order_relaxed)) * master;
        const float gB = faderToGain(deck[1].fader.load(std::memory_order_relaxed)) * master;
        for (int i = 0; i < n; ++i) {
            mL_[i] = dL_[0][i] * gA; mR_[i] = dR_[0][i] * gA;
            hL_[i] = dL_[1][i] * gB; hR_[i] = dR_[1][i] * gB;
        }
        hpLimiter_.process(hL_, hR_, n);
    } else if (routing != HpRouting::Ninguna) {
        const float mix = std::clamp(mixer.hpMix.load(std::memory_order_relaxed), 0.f, 1.f);
        const float hv = faderToGain(mixer.hpVol.load(std::memory_order_relaxed));
        for (int i = 0; i < n; ++i) {
            hL_[i] = (cL_[i] * (1.f - mix) + mL_[i] * mix) * hv;
            hR_[i] = (cR_[i] * (1.f - mix) + mR_[i] * mix) * hv;
        }
        hpLimiter_.process(hL_, hR_, n);
    }

    auto writePair = [](float* o, int chCount, int pairIdx, float l, float r) {
        if (chCount <= 0) return;
        if (pairIdx == 4) { // chan 1 (Mono L)
            o[0] += 0.5f * (l + r);
            return;
        }
        if (pairIdx == 5) { // chan 2 (Mono R)
            if (chCount >= 2) o[1] += 0.5f * (l + r);
            else o[0] += 0.5f * (l + r);
            return;
        }
        const int c0 = pairIdx * 2;
        if (c0 + 1 < chCount) {
            o[c0] += l;
            o[c0 + 1] += r;
        } else if (chCount >= 2) {
            o[0] += l;
            o[1] += r;
        } else {
            o[0] += 0.5f * (l + r);
        }
    };

    for (int i = 0; i < n; ++i) {
        float* o = out + (size_t)i * outChannels;
        for (int c = 0; c < outChannels; ++c) o[c] = 0.f;
        if (routing == HpRouting::DivisionMono && outChannels >= 2) {
            o[0] = 0.5f * (hL_[i] + hR_[i]);
            o[1] = 0.5f * (mL_[i] + mR_[i]);
            continue;
        }
        if (routing == HpRouting::Canales34) {
            // Si el dispositivo físico solo tiene 2 canales reales (p. ej. cascos WH201A estéreo) y el usuario tiene
            // configurado Altavoces (chan 1&2) + Audífonos (chan 3&4) en ese mismo dispositivo:
            // cuando pulsa CUE (anyPfl) escuchará directamente la preescucha en sus cascos (y en reposo el Master).
            // Y si la tarjeta tiene >=4 canales físicos reales, chan 1&2 lleva Master y chan 3&4 lleva Preescucha por separado.
            if (hwCh < 4 && spkPair == 0 && hpPair == 1 && anyPfl) {
                writePair(o, outChannels, 0, hL_[i], hR_[i]);
            } else {
                writePair(o, outChannels, spkPair, mL_[i], mR_[i]);
            }
            if (outChannels >= 4) {
                writePair(o, outChannels, hpPair, hL_[i], hR_[i]);
            }
            continue;
        }
        if (routing == HpRouting::DecksSeparados) {
            writePair(o, outChannels, spkPair, mL_[i], mR_[i]);
            if (outChannels >= 4) writePair(o, outChannels, hpPair, hL_[i], hR_[i]);
            continue;
        }
        writePair(o, outChannels, spkPair, mL_[i], mR_[i]);
    }
    if (routing == HpRouting::DispositivoAparte || routing == HpRouting::DecksSeparados) {
        for (int i = 0; i < n; ++i) { inter_[i * 2] = hL_[i]; inter_[i * 2 + 1] = hR_[i]; }
        if (!hpRing.write(inter_, (size_t)n * 2)) status.hpDropped.fetch_add((uint64_t)n, std::memory_order_relaxed);
    }
}

} // namespace rdj

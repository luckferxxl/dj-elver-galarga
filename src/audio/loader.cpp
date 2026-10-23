#include "loader.h"
#include "analysis.h"
#include "ma.h"
#include "../core/tags.h"
#include <cmath>

extern "C" unsigned int rdj_mp3_delay(const wchar_t* path);

namespace rdj {

namespace {
constexpr uint64_t kMaxSeconds = 45 * 60;  // límite de memoria: ~1 GB a 48 kHz
}

TrackPtr decodeFile(const std::wstring& path, std::string& err, std::atomic<float>* progress, const std::atomic<bool>* cancel) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, 0);
    ma_decoder dec;
    ma_result r = ma_decoder_init_file_w(path.c_str(), &cfg, &dec);
    if (r != MA_SUCCESS) {
        err = (r == MA_DOES_NOT_EXIST) ? "El archivo no existe o no se puede abrir."
                                       : "Formato no compatible o archivo dañado. Formatos admitidos: MP3, WAV, FLAC y OGG.";
        return nullptr;
    }
    auto t = std::make_shared<Track>();
    t->path = path;
    ma_format fmt; ma_uint32 ch, sr;
    ma_decoder_get_data_format(&dec, &fmt, &ch, &sr, nullptr, 0);
    if (sr < 8000 || sr > 384000) {
        ma_decoder_uninit(&dec);
        err = "Frecuencia de muestreo no admitida.";
        return nullptr;
    }
    t->sampleRate = sr;
    ma_uint64 total = 0;
    if (ma_decoder_get_length_in_pcm_frames(&dec, &total) != MA_SUCCESS) total = 0;
    if (total > kMaxSeconds * sr) {
        ma_decoder_uninit(&dec);
        err = "La pista es demasiado larga (máximo 45 minutos).";
        return nullptr;
    }
    try {
        if (total) t->pcm.reserve((size_t)total * 2 + 8192);
        const ma_uint64 chunk = 16384;
        std::vector<float> tmp((size_t)chunk * 2);
        for (;;) {
            if (cancel && cancel->load()) { ma_decoder_uninit(&dec); err = "Cancelado."; return nullptr; }
            ma_uint64 got = 0;
            r = ma_decoder_read_pcm_frames(&dec, tmp.data(), chunk, &got);
            if (got) t->pcm.insert(t->pcm.end(), tmp.begin(), tmp.begin() + (size_t)got * 2);
            if (progress && total) progress->store(std::min(1.f, (float)(t->pcm.size() / 2) / (float)total));
            if (t->pcm.size() / 2 > kMaxSeconds * sr) { ma_decoder_uninit(&dec); err = "La pista es demasiado larga (máximo 45 minutos)."; return nullptr; }
            if (r != MA_SUCCESS || got < chunk) break;
        }
    } catch (const std::bad_alloc&) {
        ma_decoder_uninit(&dec);
        err = "No hay memoria suficiente para cargar la pista.";
        return nullptr;
    }
    ma_decoder_uninit(&dec);
    t->frames = t->pcm.size() / 2;
    if (t->frames < sr / 10) {
        err = "El archivo no contiene audio utilizable.";
        return nullptr;
    }
    // Sustituye NaN/Inf (archivos corruptos) por silencio.
    for (float& v : t->pcm) if (!std::isfinite(v)) v = 0.f;
    return t;
}

std::vector<double> decoderOffsetCandidates(const std::wstring& path, double sr) {
    std::vector<double> c{0.0};
    std::wstring ext = path.size() >= 4 ? path.substr(path.size() - 4) : std::wstring();
    for (auto& ch : ext) ch = (wchar_t)towlower(ch);
    if (ext != L".mp3" || sr <= 0) return c;
    uint32_t delay = 0;
    delay = rdj_mp3_delay(path.c_str());
    std::vector<double> d;
    if (delay > 0) d = {(double)delay};
    else d = {529.0, 1105.0};
    for (double x : d) { c.push_back(x / sr); c.push_back(-x / sr); }
    return c;
}

double chooseDecoderOffset(double measured, const std::vector<double>& candidates, double tol) {
    double best = 0, bestErr = 1e9;
    for (double c : candidates) {
        const double e = std::fabs(measured - c);
        if (e < bestErr) { bestErr = e; best = c; }
    }
    return bestErr <= tol ? best : 0.0;
}

TrackLoader::TrackLoader() {
    for (int i = 0; i < 2; ++i) { cancel_[i] = false; progress_[i] = 0.f; busy_[i] = false; }
    th_ = std::thread(&TrackLoader::run, this);
}

TrackLoader::~TrackLoader() {
    quit_ = true;
    cancel_[0] = cancel_[1] = true;
    cv_.notify_all();
    if (th_.joinable()) th_.join();
}

uint32_t TrackLoader::request(LoadJob job) {
    std::lock_guard<std::mutex> lk(m_);
    job.id = nextId_++;
    const int d = job.deck & 1;
    for (auto it = jobs_.begin(); it != jobs_.end();) it = (it->deck == d) ? jobs_.erase(it) : it + 1;
    if (busy_[d]) cancel_[d] = true;  // el trabajo en curso se aborta
    busy_[d] = true;
    progress_[d] = 0.f;
    jobs_.push_back(std::move(job));
    cv_.notify_all();
    return nextId_ - 1;
}

bool TrackLoader::poll(LoadResult& out) {
    std::lock_guard<std::mutex> lk(m_);
    if (results_.empty()) return false;
    out = std::move(results_.front());
    results_.pop_front();
    return true;
}

void TrackLoader::run() {
    for (;;) {
        LoadJob job;
        {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait(lk, [&] { return quit_ || !jobs_.empty(); });
            if (quit_) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
            cancel_[job.deck & 1] = false;
        }
        const int d = job.deck & 1;
        LoadResult res;
        res.deck = d; res.id = job.id;
        std::string err;
        TrackPtr t = decodeFile(job.path, err, &progress_[d], &cancel_[d]);
        if (t) {
            t->title = job.title; t->artist = job.artist;
            t->meta = job.meta;
            computeWaveform(t->pcm.data(), t->frames, t->sampleRate, t->wave, t->maxPeak);
            t->meta.durationSec = t->lengthSec();
            // 0.10.2: si VirtualDJ tiene la pista analizada, manda su rejilla (BPM y primer pulso),
            // salvo que la hayas ajustado a mano aquí. Solo se corrige el desfase conocido entre
            // decodificadores de MP3 (unos 12 o 25 ms) y solo si la medida lo confirma claramente.
            ExternalGrid eg;
            if (job.useExternalGrid && externalGrid && t->meta.gridSrc != 3 && !cancel_[d] && externalGrid(job.path, eg) && eg.bpm > 30 && eg.gridSec >= 0) {
                const double beat = 60.0 / eg.bpm, bar = 4 * beat;
                double raw = std::fmod(eg.gridSec, bar);
                if (raw < 0) raw += bar;
                const std::vector<double> cand = decoderOffsetCandidates(job.path, t->sampleRate);
                auto wrapBeat = [&](double x) { x = std::fmod(x, beat); if (x > beat / 2) x -= beat; if (x < -beat / 2) x += beat; return x; };
                // ¿Ya aplicada en una carga anterior (mismo BPM y misma rejilla con alguno de los desfases)?
                bool applied = false;
                if (t->meta.gridSrc == 8 && std::fabs(t->meta.bpm - eg.bpm) < 1e-6)
                    for (double c : cand) if (std::fabs(wrapBeat(t->meta.gridAnchorSec - (raw + c))) < 0.0005) applied = true;
                if (!applied) {
                    const BeatInfo g = detectGridForBpm(t->pcm.data(), t->frames, t->sampleRate, eg.bpm, &cancel_[d]);
                    if (!cancel_[d]) {
                        const double off = g.bpm > 0 ? chooseDecoderOffset(wrapBeat(g.anchorSec - raw), cand) : 0.0;
                        const double oldAnchor = t->meta.gridAnchorSec, oldBeat = t->meta.bpm > 0 ? 60.0 / t->meta.bpm : 0.0;
                        const double newAnchor = std::fmod(raw + off + bar * 64, bar);
                        // Los cues que estaban encajados en la rejilla anterior (con el mismo BPM) se mueven con ella.
                        if (oldBeat > 0 && std::fabs(t->meta.bpm - eg.bpm) < 0.02) {
                            const double delta = wrapBeat(newAnchor - oldAnchor);
                            auto snapped = [&](double c) { const double k = (c - oldAnchor) / oldBeat; return c >= 0 && std::fabs(k - std::round(k)) * oldBeat < 0.002; };
                            if (std::fabs(delta) < 0.1) {
                                if (snapped(t->meta.cueSec)) t->meta.cueSec = std::max(0.0, t->meta.cueSec + delta);
                                for (int i = 0; i < kNumHotCues; ++i) if (snapped(t->meta.hotCuesSec[i])) t->meta.hotCuesSec[i] = std::max(0.0, t->meta.hotCuesSec[i] + delta);
                            }
                        }
                        t->meta.bpm = eg.bpm;
                        t->meta.gridAnchorSec = newAnchor;
                        t->meta.analyzed = true;
                        t->meta.importedCues = 0;
                        t->meta.gridSrc = 8;
                        res.analyzedNow = true;
                    }
                }
            }
            if (!t->meta.analyzed) {
                BeatInfo bi = detectBeats(t->pcm.data(), t->frames, t->sampleRate, job.minBpm, job.maxBpm, &cancel_[d]);
                if (!cancel_[d]) {
                    t->meta.bpm = bi.bpm;
                    t->meta.gridAnchorSec = bi.anchorSec;
                    t->meta.analyzed = true;
                    t->meta.gridSrc = 6;
                    res.analyzedNow = true;
                }
            } else if (t->meta.gridSrc == 1 && t->meta.bpm > 0) {
                // Rejilla analizada con una versión anterior (BPM redondeado): se afina
                // con el análisis preciso solo si coincide con ella (mismo tempo y casi
                // la misma fase), así no se pisa una rejilla corregida a mano.
                BeatInfo bi = detectBeats(t->pcm.data(), t->frames, t->sampleRate, job.minBpm, job.maxBpm, &cancel_[d]);
                if (!cancel_[d]) {
                    const double old = t->meta.bpm;
                    double nb = bi.bpm;
                    for (double f : {2.0, 0.5}) if (std::fabs(nb * f - old) < std::fabs(nb - old)) nb *= f;
                    if (bi.bpm > 0 && std::fabs(nb - old) < 0.12) {
                        const double beat = 60.0 / nb;
                        double dph = std::fmod(bi.anchorSec - t->meta.gridAnchorSec, beat);
                        if (dph > beat / 2) dph -= beat;
                        if (dph < -beat / 2) dph += beat;
                        if (std::fabs(dph) < 0.035) {
                            // Se conserva el primer tiempo de compás elegido; solo cambia la precisión.
                            t->meta.bpm = nb;
                            t->meta.gridAnchorSec = std::fmod(t->meta.gridAnchorSec + dph + 4 * beat * 64, 4 * beat);
                        }
                    }
                    t->meta.gridSrc = 2;
                    res.analyzedNow = true;
                }
            }
            if (t->meta.gridSrc == 4 && t->meta.bpm > 0 && !cancel_[d]) {
                // Rejilla importada de VirtualDJ: se conserva su tempo y su primer tiempo de
                // compás, pero se mide aquí la fase real de los pulsos. Si el programa que la
                // analizó decodifica el MP3 con otro desfase (unos ms), se corrige la rejilla y
                // los hot cues importados con esa misma diferencia.
                const double beat = 60.0 / t->meta.bpm, bar = 4 * beat;
                const BeatInfo g = detectGridForBpm(t->pcm.data(), t->frames, t->sampleRate, t->meta.bpm, &cancel_[d]);
                if (!cancel_[d]) {
                    if (g.bpm > 0) {
                        double delta = std::fmod(g.anchorSec - t->meta.gridAnchorSec, beat);
                        if (delta > beat / 2) delta -= beat;
                        if (delta < -beat / 2) delta += beat;
                        // 0.10.2: solo el desfase conocido entre decodificadores (antes, cualquier medida de
                        // hasta 80 ms, que con música real podía estropear una rejilla buena).
                        delta = chooseDecoderOffset(delta, decoderOffsetCandidates(job.path, t->sampleRate));
                        if (delta != 0.0) {
                            t->meta.gridAnchorSec = std::fmod(t->meta.gridAnchorSec + delta + bar * 64, bar);
                            for (int i = 0; i < kNumHotCues; ++i)
                                if ((t->meta.importedCues >> i) & 1) t->meta.hotCuesSec[i] = std::max(0.0, t->meta.hotCuesSec[i] + delta);
                        }
                    }
                    t->meta.importedCues = 0;
                    t->meta.gridSrc = 7;  // de VirtualDJ, ya comprobada (con la medida de la 0.10)
                    res.analyzedNow = true;
                }
            }
            if ((t->meta.gridSrc == 2 || t->meta.gridSrc == 5) && t->meta.bpm > 0 && !cancel_[d]) {
                // Rejilla de antes de la 0.10: la fase salía entre 5 y 20 ms tarde según el sonido
                // de la pista (y en las de VirtualDJ se había «corregido» con esa medida) y el «1» del
                // compás fallaba a menudo. Se vuelve a medir con el mismo BPM. Los cues que estaban
                // encajados en la rejilla se mueven con ella.
                const bool fromVdj = t->meta.gridSrc == 5;
                const double beat = 60.0 / t->meta.bpm, bar = 4 * beat;
                const BeatInfo g = detectGridForBpm(t->pcm.data(), t->frames, t->sampleRate, t->meta.bpm, &cancel_[d]);
                if (!cancel_[d]) {
                    if (g.bpm > 0) {
                        const double old = t->meta.gridAnchorSec;
                        double delta = std::fmod(g.anchorSec - old, beat);
                        if (delta > beat / 2) delta -= beat;
                        if (delta < -beat / 2) delta += beat;
                        if (std::fabs(delta) < 0.06) {
                            auto snapped = [&](double c) {
                                double k = (c - old) / beat;
                                return c >= 0 && std::fabs(k - std::round(k)) * beat < 0.002;
                            };
                            if (snapped(t->meta.cueSec)) t->meta.cueSec = std::max(0.0, t->meta.cueSec + delta);
                            for (int i = 0; i < kNumHotCues; ++i)
                                if (snapped(t->meta.hotCuesSec[i])) t->meta.hotCuesSec[i] = std::max(0.0, t->meta.hotCuesSec[i] + delta);
                            // Las de VirtualDJ conservan su «1» del compás; las automáticas toman el nuevo.
                            t->meta.gridAnchorSec = fromVdj ? std::fmod(old + delta + bar * 64, bar) : g.anchorSec;
                        }
                    }
                    t->meta.gridSrc = fromVdj ? 7 : 6;
                    res.analyzedNow = true;
                }
            }
            if (!t->meta.keyAnalyzed && !cancel_[d]) {
                // Primero la etiqueta del archivo (TKEY / INITIALKEY); si no la trae, se detecta.
                const TagInfo tg = readTags(job.path);
                if (tg.key >= 0) { t->meta.key = tg.key; t->meta.keyAnalyzed = true; t->meta.keySrc = 3; res.analyzedNow = true; }
                else {
                    KeyInfo ki = detectKey(t->pcm.data(), t->frames, t->sampleRate, &cancel_[d]);
                    if (!cancel_[d]) { t->meta.key = ki.key; t->meta.keyAnalyzed = true; t->meta.keySrc = 2; res.analyzedNow = true; }
                }
            }
            if (cancel_[d]) { t.reset(); err = "Cancelado."; }
        }
        res.track = t;
        res.error = err;
        std::lock_guard<std::mutex> lk(m_);
        // Si mientras tanto se pidió otra carga para este plato, el resultado se descarta.
        bool superseded = false;
        for (auto& j : jobs_) if (j.deck == d) superseded = true;
        if (!superseded) { busy_[d] = false; results_.push_back(std::move(res)); }
    }
}

} // namespace rdj

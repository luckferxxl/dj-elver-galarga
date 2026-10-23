#include "recorder.h"
#include "ma.h"
#include <chrono>
#include <vector>
#include <algorithm>

namespace rdj {

bool Recorder::start(Engine& eng, const std::wstring& path, RecFormat fmt, std::string& err) {
    if (running_) { err = "Ya se está grabando."; return false; }
    eng_ = &eng;
    sr_ = (uint32_t)eng.sampleRate();
    auto* enc = new ma_encoder;
    ma_encoder_config cfg = ma_encoder_config_init(ma_encoding_format_wav, fmt == RecFormat::Wav16 ? ma_format_s16 : ma_format_f32, 2, sr_);
    if (ma_encoder_init_file_w(path.c_str(), &cfg, enc) != MA_SUCCESS) {
        delete enc;
        err = "No se pudo crear el archivo de grabación (¿carpeta sin permisos o disco lleno?).";
        return false;
    }
    encoder_ = enc;
    path_ = path;
    framesWritten_ = 0;
    writeError_ = false;
    stopReq_ = false;
    // Descartar restos de una grabación anterior (somos el único consumidor).
    eng.recRing.skip(eng.recRing.available());
    eng.status.recDropped.store(0);
    running_ = true;
    th_ = std::thread(&Recorder::run, this, fmt);
    eng.mixer.recording.store(true);
    return true;
}

void Recorder::stop() {
    if (!running_) return;
    if (eng_) eng_->mixer.recording.store(false);
    stopReq_ = true;
    if (th_.joinable()) th_.join();
    auto* enc = (ma_encoder*)encoder_;
    if (enc) { ma_encoder_uninit(enc); delete enc; encoder_ = nullptr; }
    running_ = false;
}

void Recorder::run(RecFormat fmt) {
    auto* enc = (ma_encoder*)encoder_;
    std::vector<float> buf(16384);
    std::vector<int16_t> s16(16384);
    uint32_t rng = 0x12345678u;
    auto rnd = [&rng]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float)rng / 4294967296.0f; };
    auto stopAt = std::chrono::steady_clock::time_point::max();
    for (;;) {
        size_t n = eng_->recRing.read(buf.data(), buf.size());
        n &= ~(size_t)1;  // fotogramas completos
        if (n) {
            const ma_uint64 frames = n / 2;
            ma_uint64 written = 0;
            ma_result r;
            if (fmt == RecFormat::Wav16) {
                for (size_t i = 0; i < n; ++i) {
                    // Tramado TPDF antes de cuantificar a 16 bits.
                    const float d = (rnd() - rnd()) / 32768.0f;
                    float v = std::clamp(buf[i] + d, -1.0f, 1.0f);
                    s16[i] = (int16_t)std::lround(v * 32767.0f);
                }
                r = ma_encoder_write_pcm_frames(enc, s16.data(), frames, &written);
            } else {
                r = ma_encoder_write_pcm_frames(enc, buf.data(), frames, &written);
            }
            if (r != MA_SUCCESS || written != frames) writeError_ = true;
            framesWritten_ += written;
            continue;
        }
        if (stopReq_) {
            // Esperar un instante por el último bloque que el audio pudiera estar escribiendo.
            if (stopAt == std::chrono::steady_clock::time_point::max()) stopAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(60);
            else if (std::chrono::steady_clock::now() >= stopAt) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
}

bool writeWavStereoFloat(const std::wstring& path, const float* L, const float* R, uint64_t frames, uint32_t sampleRate, std::string& err) {
    ma_encoder enc;
    ma_encoder_config cfg = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 2, sampleRate);
    if (ma_encoder_init_file_w(path.c_str(), &cfg, &enc) != MA_SUCCESS) { err = "No se pudo crear el archivo (¿carpeta sin permisos o disco lleno?)."; return false; }
    std::vector<float> buf(8192 * 2);
    bool ok = true;
    for (uint64_t done = 0; done < frames && ok;) {
        const uint64_t n = std::min<uint64_t>(8192, frames - done);
        for (uint64_t i = 0; i < n; ++i) { buf[i * 2] = L[done + i]; buf[i * 2 + 1] = R[done + i]; }
        ma_uint64 w = 0;
        if (ma_encoder_write_pcm_frames(&enc, buf.data(), n, &w) != MA_SUCCESS || w != n) ok = false;
        done += n;
    }
    ma_encoder_uninit(&enc);
    if (!ok) err = "Error de escritura en disco.";
    return ok;
}

} // namespace rdj

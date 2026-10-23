// Pruebas automáticas del motor de Ramiro DJ (sin interfaz, sin hardware salvo
// --real-device). Uso: rdj_tests [--real-device] [--bpm-file ruta.mp3 bpm]
#include "../src/audio/analysis.h"
#include "../src/audio/audio_io.h"
#include "../src/audio/engine.h"
#include "../src/audio/loader.h"
#include "../src/audio/recorder.h"
#include "../src/audio/ma.h"
#include "../src/core/library.h"
#include "../src/core/midi.h"
#include "../src/core/tags.h"
#include "../src/core/util.h"
#include "../src/core/vdjimport.h"
#include "../src/core/mandos.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <thread>
#include <windows.h>
#include <shellapi.h>

using namespace rdj;
namespace fs = std::filesystem;

static int g_fail = 0, g_pass = 0;
#define CHECKV(cond, ...) ([&] { const bool ok_ = (cond); CHECK(ok_, __VA_ARGS__); return ok_; }())
#define CHECK(cond, ...) do { if (cond) { ++g_pass; } else { ++g_fail; std::printf("  FALLO %s:%d: %s -- ", __FILE__, __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static fs::path tmpDir() {
    fs::path p = fs::temp_directory_path() / "rdj_tests";
    fs::create_directories(p);
    return p;
}

static TrackPtr makeTrack(uint32_t sr, double seconds, const std::function<void(double t, float& l, float& r)>& gen) {
    auto t = std::make_shared<Track>();
    t->sampleRate = sr;
    t->frames = (uint64_t)(seconds * sr);
    t->pcm.resize(t->frames * 2);
    for (uint64_t i = 0; i < t->frames; ++i) gen((double)i / sr, t->pcm[i * 2], t->pcm[i * 2 + 1]);
    computeWaveform(t->pcm.data(), t->frames, sr, t->wave, t->maxPeak);
    return t;
}

// Pista de percusión sintética: bombo en cada pulso, charles a contratiempo.
static TrackPtr makeBeatTrack(uint32_t sr, double bpm, double firstBeat, double seconds) {
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> noise(-1.f, 1.f);
    const double beat = 60.0 / bpm;
    std::vector<float> hat(4096);
    for (auto& h : hat) h = noise(rng);
    return makeTrack(sr, seconds, [&](double t, float& l, float& r) {
        float v = 0;
        double tb = t - firstBeat;
        if (tb >= 0) {
            const double k = std::floor(tb / beat);
            const double dt = tb - k * beat;  // tiempo desde el último pulso
            const bool downbeat = ((long long)k % 4) == 0;  // acento en el 1 del compás
            if (dt < 0.25) v += (float)((downbeat ? 0.9 : 0.6) * std::sin(2 * kPi * (55 + 90 * std::exp(-dt * 40)) * dt) * std::exp(-dt * 14));
            const double dh = dt - beat / 2;
            if (dh >= 0 && dh < 0.05) v += 0.25f * hat[(size_t)(dh * sr) % hat.size()] * (float)std::exp(-dh * 80);
        }
        l = r = v;
    });
}

static double goertzel(const float* x, int n, int stride, double freq, double fs) {
    const double w = 2 * kPi * freq / fs, c = 2 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (int i = 0; i < n; ++i) { const double s = x[(size_t)i * stride] + c * s1 - s2; s2 = s1; s1 = s; }
    const double p = s1 * s1 + s2 * s2 - c * s1 * s2;
    return std::sqrt(std::max(0.0, p)) / (n / 2.0);
}

static std::vector<float> render(Engine& e, double seconds, int ch = 2) {
    const int frames = (int)(seconds * e.sampleRate());
    std::vector<float> out((size_t)frames * ch);
    int done = 0;
    while (done < frames) {
        const int n = std::min(480, frames - done);
        e.process(out.data() + (size_t)done * ch, n, ch);
        done += n;
    }
    return out;
}

static void loadDeck(Engine& e, int d, Track* t, uint32_t gen = 1) {
    Command c{Cmd::Load, (uint8_t)d}; c.track = t; c.gen = gen;
    e.send(c);
    float dummy[2 * 16];
    e.process(dummy, 16, 2);
}
static void cmd(Engine& e, int d, Cmd k, double v = 0, int i = 0) {
    Command c{k, (uint8_t)d}; c.d = v; c.i = i;
    e.send(c);
}

static bool writeWav(const fs::path& p, uint32_t sr, const std::vector<int16_t>& inter) {
    ma_encoder enc;
    ma_encoder_config cfg = ma_encoder_config_init(ma_encoding_format_wav, ma_format_s16, 2, sr);
    if (ma_encoder_init_file_w(p.wstring().c_str(), &cfg, &enc) != MA_SUCCESS) return false;
    ma_uint64 w = 0;
    ma_encoder_write_pcm_frames(&enc, inter.data(), inter.size() / 2, &w);
    ma_encoder_uninit(&enc);
    return w == inter.size() / 2;
}

// ------------------------------------------------------------------ pruebas

static void testDecode() {
    std::printf("[decodificación y archivos incompatibles]\n");
    const fs::path dir = tmpDir();
    std::vector<int16_t> s((size_t)44100 * 2 * 2);
    for (size_t i = 0; i < s.size() / 2; ++i) s[i * 2] = s[i * 2 + 1] = (int16_t)(10000 * std::sin(2 * kPi * 1000 * i / 44100.0));
    CHECK(writeWav(dir / "seno.wav", 44100, s), "no se pudo escribir WAV");
    std::string err;
    TrackPtr t = decodeFile((dir / "seno.wav").wstring(), err);
    CHECK(t != nullptr, "%s", err.c_str());
    if (t) {
        CHECK(t->sampleRate == 44100, "sr=%u", t->sampleRate);
        CHECK(t->frames == 88200, "frames=%llu", (unsigned long long)t->frames);
        CHECK(std::fabs(t->pcm[2 * 11] - 10000 / 32768.f * (float)std::sin(2 * kPi * 1000 * 11 / 44100.0)) < 1e-3f, "muestra incorrecta");
    }
    { std::ofstream(dir / "texto.mp3") << "esto no es audio, es texto plano con extensión mp3"; }
    t = decodeFile((dir / "texto.mp3").wstring(), err);
    CHECK(!t && !err.empty(), "texto aceptado como mp3");
    std::printf("  texto.mp3 -> \"%s\"\n", err.c_str());
    { std::ofstream(dir / "vacio.wav"); }
    t = decodeFile((dir / "vacio.wav").wstring(), err);
    CHECK(!t && !err.empty(), "vacío aceptado");
    { std::ofstream f(dir / "basura.flac", std::ios::binary); std::mt19937 g(1); for (int i = 0; i < 100000; ++i) f.put((char)g()); }
    t = decodeFile((dir / "basura.flac").wstring(), err);
    CHECK(!t && !err.empty(), "basura aceptada");
    // WAV truncado: solo la cabecera
    {
        std::ifstream in(dir / "seno.wav", std::ios::binary);
        std::vector<char> b(60); in.read(b.data(), 60);
        std::ofstream(dir / "truncado.wav", std::ios::binary).write(b.data(), 60);
    }
    t = decodeFile((dir / "truncado.wav").wstring(), err);
    CHECK(!t, "WAV truncado aceptado");
    std::printf("  truncado.wav -> \"%s\"\n", err.c_str());
    t = decodeFile((dir / "no_existe.mp3").wstring(), err);
    CHECK(!t && !err.empty(), "inexistente aceptado");
    // Nombre con acentos y eñe (rutas Unicode)
    CHECK(writeWav(dir / L"canción ñandú.wav", 48000, s), "no se pudo escribir WAV unicode");
    t = decodeFile((dir / L"canción ñandú.wav").wstring(), err);
    CHECK(t != nullptr, "ruta unicode: %s", err.c_str());
}

static void testEq() {
    std::printf("[ecualizador, filtro, limitador, crossfader]\n");
    const double fs = 48000;
    auto sineGain = [&](double f, float gl, float gm, float gh) {
        ThreeBandEq eq; eq.prepare(fs);
        const int n = 48000;
        std::vector<float> L(n), R(n);
        for (int i = 0; i < n; ++i) L[i] = R[i] = (float)std::sin(2 * kPi * f * i / fs);
        for (int o = 0; o < n; o += 256) eq.process(L.data() + o, R.data() + o, std::min(256, n - o), gl, gl, gm, gm, gh, gh);
        return 20 * std::log10(goertzel(L.data() + 24000, 24000, 1, f, fs) + 1e-12);
    };
    for (double f : {60.0, 400.0, 1000.0, 5000.0, 12000.0}) {
        const double g = sineGain(f, 1, 1, 1);
        CHECK(std::fabs(g) < 0.1, "EQ plano a %.0f Hz da %.2f dB", f, g);
    }
    CHECK(sineGain(60, 0, 1, 1) < -30, "corte de graves insuficiente: %.1f dB", sineGain(60, 0, 1, 1));
    CHECK(sineGain(12000, 1, 1, 0) < -30, "corte de agudos insuficiente: %.1f dB", sineGain(12000, 1, 1, 0));
    CHECK(sineGain(800, 1, 0, 1) < -20, "corte de medios insuficiente: %.1f dB", sineGain(800, 1, 0, 1));
    std::printf("  cortes: graves %.1f dB, medios %.1f dB, agudos %.1f dB\n", sineGain(60, 0, 1, 1), sineGain(800, 1, 0, 1), sineGain(12000, 1, 1, 0));

    // Filtro: paso bajo a tope atenúa 5 kHz
    {
        DjFilter flt; flt.prepare(fs);
        const int n = 48000;
        std::vector<float> L(n), R(n);
        for (int i = 0; i < n; ++i) L[i] = R[i] = (float)std::sin(2 * kPi * 5000 * i / fs);
        for (int o = 0; o < n; o += 256) flt.process(L.data() + o, R.data() + o, std::min(256, n - o), -1.f);
        const double g = 20 * std::log10(goertzel(L.data() + 24000, 24000, 1, 5000, fs) + 1e-12);
        CHECK(g < -40, "filtro paso bajo: %.1f dB", g);
    }

    // Limitador: +12 dB de entrada no supera -0,3 dBFS
    {
        Limiter lim; lim.prepare(fs, -0.3f);
        const int n = 48000;
        std::vector<float> L(n), R(n);
        for (int i = 0; i < n; ++i) { L[i] = (float)(4.0 * std::sin(2 * kPi * 100 * i / fs)); R[i] = (i % 4800 == 0) ? 8.f : L[i]; }
        for (int o = 0; o < n; o += 256) lim.process(L.data() + o, R.data() + o, std::min(256, n - o));
        float pk = 0;
        for (int i = 0; i < n; ++i) pk = std::max(pk, std::max(std::fabs(L[i]), std::fabs(R[i])));
        CHECK(pk <= dbToLin(-0.3f) + 1e-6f, "pico %.4f", pk);
        double rms = 0; for (int i = 24000; i < n; ++i) rms += L[i] * L[i];
        rms = std::sqrt(rms / 24000);
        CHECK(rms > 0.45, "el limitador aplasta demasiado: rms %.3f", rms);
    }

    float a, b;
    crossfaderGains(0, XfCurve::Suave, a, b); CHECK(a > 0.999f && b < 1e-6f, "xf 0");
    crossfaderGains(1, XfCurve::Suave, a, b); CHECK(a < 1e-6f && b > 0.999f, "xf 1");
    crossfaderGains(0.5f, XfCurve::Suave, a, b); CHECK(std::fabs(a * a + b * b - 1) < 1e-4f, "potencia constante");
    crossfaderGains(0.5f, XfCurve::Completa, a, b); CHECK(a == 1.f && b == 1.f, "completa");
    crossfaderGains(0.97f, XfCurve::Corte, a, b); CHECK(a > 0.5f && b == 1.f, "corte");
}

static void testBpm() {
    std::printf("[detección de BPM y fase sobre pistas sintéticas]\n");
    struct C { double bpm, first; uint32_t sr; };
    for (C c : {C{120, 0.123, 44100}, C{128, 0.050, 44100}, C{140.5, 0.300, 48000}, C{174, 0.010, 44100}, C{95, 0.200, 48000}}) {
        TrackPtr t = makeBeatTrack(c.sr, c.bpm, c.first, 60);
        auto t0 = std::chrono::steady_clock::now();
        BeatInfo bi = detectBeats(t->pcm.data(), t->frames, t->sampleRate);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        const double beat = 60.0 / c.bpm;
        double ph = std::fmod(bi.anchorSec - c.first, beat);
        if (ph > beat / 2) ph -= beat;
        if (ph < -beat / 2) ph += beat;
        // ¿coincide el primer tiempo de compás? (anchor ≡ first mod 4 pulsos)
        double bar = std::fmod(bi.anchorSec - c.first, 4 * beat);
        if (bar < 0) bar += 4 * beat;
        const bool downbeatOk = bar < 0.02 || bar > 4 * beat - 0.02;
        std::printf("  %.2f BPM -> %.3f (conf %.1f), error de fase %+.1f ms, compás %s, %.0f ms\n", c.bpm, bi.bpm, bi.confidence, ph * 1000,
                    downbeatOk ? "ok" : "desplazado", ms);
        CHECK(std::fabs(bi.bpm - c.bpm) < 0.02, "BPM %.3f esperado %.2f", bi.bpm, c.bpm);
        CHECK(std::fabs(ph) < 0.012, "fase %.1f ms", ph * 1000);
        CHECK(downbeatOk, "primer tiempo de compás desplazado %.3f s", bar);
    }
}

static void testEngineMix() {
    std::printf("[mezcla de dos pistas en el motor]\n");
    Engine e; e.prepare(48000);
    TrackPtr a = makeTrack(44100, 10, [](double t, float& l, float& r) { l = r = 0.3f * (float)std::sin(2 * kPi * 440 * t); });
    TrackPtr b = makeTrack(48000, 10, [](double t, float& l, float& r) { l = r = 0.3f * (float)std::sin(2 * kPi * 660 * t); });
    loadDeck(e, 0, a.get(), 1); loadDeck(e, 1, b.get(), 1);
    CHECK(e.deckStatus[0].loadedGen.load() == 1 && e.deckStatus[1].loadedGen.load() == 1, "carga no confirmada");
    for (int d = 0; d < 2; ++d) { e.deck[d].fader = 1.f; }
    e.mixer.masterVol = 1.f;
    e.mixer.crossfader = 0.5f;
    cmd(e, 0, Cmd::Play); cmd(e, 1, Cmd::Play);
    auto out = render(e, 1.0);
    const int n = 24000;
    const double g440 = goertzel(out.data() + 48000, n, 2, 440, 48000), g660 = goertzel(out.data() + 48000, n, 2, 660, 48000);
    std::printf("  centro: 440 Hz %.3f, 660 Hz %.3f\n", g440, g660);
    CHECK(g440 > 0.18 && g660 > 0.18, "faltan pistas en la mezcla");
    CHECK(std::fabs(g440 - g660) < 0.02, "desequilibrio");
    e.mixer.crossfader = 0.f;
    out = render(e, 0.5);
    const double b660 = goertzel(out.data() + 9600, 12000, 2, 660, 48000), b440 = goertzel(out.data() + 9600, 12000, 2, 440, 48000);
    CHECK(20 * std::log10(b660 + 1e-12) < -60, "crossfader a la izquierda deja pasar B: %.4f", b660);
    CHECK(b440 > 0.25, "A debería sonar entero: %.3f", b440);
    // EQ: corte de graves en A con un tono de 440 (medios) no lo elimina; corte de medios sí.
    e.deck[0].eqMid = 0.f;
    out = render(e, 0.5);
    const double m440 = goertzel(out.data() + 9600, 12000, 2, 440, 48000);
    CHECK(20 * std::log10(m440 / 0.3) < -20, "kill de medios: %.1f dB", 20 * std::log10(m440 / 0.3));
    e.deck[0].eqMid = 0.5f;
    // Pitch +6 %: 440 -> 466,4 Hz
    e.deck[0].pitch = 0.06f;
    out = render(e, 0.6);
    const double p466 = goertzel(out.data() + 9600, 19200, 2, 466.4, 48000), p440 = goertzel(out.data() + 9600, 19200, 2, 440, 48000);
    CHECK(p466 > 0.2 && p440 < 0.05, "pitch: 466 %.3f, 440 %.3f", p466, p440);
    // Sin NaN y sin superar el umbral del limitador con ambos platos sumados y ganancia +12 dB
    e.deck[0].pitch = 0.f;
    e.mixer.crossfader = 0.5f; e.mixer.xfCurve = (int)XfCurve::Completa;
    e.deck[0].gainDb = 12.f; e.deck[1].gainDb = 12.f;
    out = render(e, 1.0);
    float pk = 0; bool nan = false;
    for (float v : out) { pk = std::max(pk, std::fabs(v)); if (!std::isfinite(v)) nan = true; }
    CHECK(!nan, "NaN en la salida");
    CHECK(pk <= dbToLin(-0.3f) + 1e-6f, "saturación: pico %.3f", pk);
    CHECK(e.status.limiterGain.load() < 0.5f, "el limitador debería actuar");
    std::printf("  con +12 dB en ambos: pico de salida %.4f, reducción %.1f dB\n", pk, linToDb(e.status.limiterGain.load()));
}

static void testTransport() {
    std::printf("[transporte: cue, hot cues, loops, salto de pulsos]\n");
    Engine e; e.prepare(48000);
    TrackPtr a = makeTrack(48000, 30, [](double t, float& l, float& r) { l = r = 0.2f * (float)std::sin(2 * kPi * 300 * t); });
    loadDeck(e, 0, a.get());
    e.deck[0].quantize = false;
    auto pos = [&] { return e.deckStatus[0].posFrames.load() / 48000.0; };
    bool anyHot = false;
    for (auto& h : e.deckStatus[0].hotCues) if (h.load() >= 0) anyHot = true;
    CHECK(!anyHot, "hot cues ocupados en una pista nueva");
    // CUE en pausa sobre el punto cue: vista previa mientras se mantiene
    cmd(e, 0, Cmd::CueDown); render(e, 0.5);
    CHECK(e.deckStatus[0].playing.load() && pos() > 0.45, "vista previa de cue: pos %.3f", pos());
    cmd(e, 0, Cmd::CueUp); render(e, 0.05);
    CHECK(!e.deckStatus[0].playing.load() && pos() < 0.01, "volver al cue: pos %.3f", pos());
    // Buscar a 2 s y fijar cue
    cmd(e, 0, Cmd::Seek, 2.0 * 48000); render(e, 0.01);
    cmd(e, 0, Cmd::CueDown); cmd(e, 0, Cmd::CueUp); render(e, 0.01);
    CHECK(std::fabs(e.deckStatus[0].cueFrames.load() / 48000 - 2.0) < 1e-6, "cue en 2 s: %.3f", e.deckStatus[0].cueFrames.load() / 48000);
    cmd(e, 0, Cmd::Play); render(e, 1.0);
    CHECK(std::fabs(pos() - 3.0) < 0.01, "reproducción 1 s: pos %.3f", pos());
    cmd(e, 0, Cmd::CueDown); render(e, 0.1);
    CHECK(!e.deckStatus[0].playing.load() && std::fabs(pos() - 2.0) < 0.01, "CUE en reproducción vuelve y para: %.3f", pos());
    // Hot cue: fijar en 2 s, reproducir, saltar
    cmd(e, 0, Cmd::HotCue, 0, 3); render(e, 0.01);
    CHECK(std::fabs(e.deckStatus[0].hotCues[3].load() / 48000 - 2.0) < 1e-6, "hot cue 4 fijado");
    cmd(e, 0, Cmd::Seek, 10.0 * 48000); cmd(e, 0, Cmd::HotCue, 0, 3); render(e, 0.5);
    CHECK(e.deckStatus[0].playing.load() && std::fabs(pos() - 2.5) < 0.01, "salto a hot cue: %.3f", pos());
    cmd(e, 0, Cmd::HotCueDelete, 0, 3); render(e, 0.01);
    CHECK(e.deckStatus[0].hotCues[3].load() < 0, "hot cue borrado");
    // Loop automático de 4 pulsos con cuadrícula a 120 BPM (pulso 0,5 s)
    e.deck[0].bpm = 120.0; e.deck[0].gridAnchorSec = 0.0; e.deck[0].quantize = true;
    cmd(e, 0, Cmd::Seek, 5.3 * 48000); render(e, 0.01);
    cmd(e, 0, Cmd::AutoLoop, 4.0); render(e, 0.01);
    CHECK(std::fabs(e.deckStatus[0].loopIn.load() / 48000 - 5.0) < 1e-6 && std::fabs(e.deckStatus[0].loopOut.load() / 48000 - 7.0) < 1e-6,
          "loop [%.3f, %.3f]", e.deckStatus[0].loopIn.load() / 48000, e.deckStatus[0].loopOut.load() / 48000);
    double mn = 1e9, mx = -1e9;
    for (int k = 0; k < 100; ++k) { render(e, 0.05); mn = std::min(mn, pos()); mx = std::max(mx, pos()); }
    CHECK(mn >= 5.0 - 1e-3 && mx <= 7.0 + 1e-3, "fuera del loop: %.3f..%.3f", mn, mx);
    cmd(e, 0, Cmd::LoopHalve); render(e, 0.01);
    CHECK(std::fabs(e.deckStatus[0].loopOut.load() / 48000 - 6.0) < 1e-6, "mitad");
    cmd(e, 0, Cmd::LoopDouble); cmd(e, 0, Cmd::LoopDouble); render(e, 0.01);
    CHECK(std::fabs(e.deckStatus[0].loopOut.load() / 48000 - 9.0) < 1e-6, "doble: %.3f", e.deckStatus[0].loopOut.load() / 48000);
    cmd(e, 0, Cmd::LoopExit); render(e, 0.01);
    CHECK(!e.deckStatus[0].loopActive.load(), "salir del loop");
    const double p0 = pos();
    cmd(e, 0, Cmd::BeatJump, 4.0); render(e, 0.001);
    CHECK(std::fabs(pos() - (p0 + 2.0)) < 0.01, "salto de 4 pulsos: %.3f -> %.3f", p0, pos());
    // Fin de pista: se detiene
    cmd(e, 0, Cmd::Seek, 29.9 * 48000); render(e, 0.3);
    CHECK(!e.deckStatus[0].playing.load() && e.deckStatus[0].endReached.load(), "fin de pista");
    // Clic: un salto con la pista sonando no produce discontinuidades grandes
    TrackPtr dc = makeTrack(48000, 10, [](double t, float& l, float& r) { l = r = t < 5 ? 0.5f : -0.5f; });
    loadDeck(e, 1, dc.get(), 2);
    e.deck[1].fader = 1.f; e.mixer.crossfader = 1.f; e.mixer.masterVol = 1.f;
    cmd(e, 1, Cmd::Play); render(e, 0.2);
    cmd(e, 1, Cmd::Seek, 7.0 * 48000);
    auto out = render(e, 0.05);
    float maxStep = 0;
    for (size_t i = 2; i < out.size(); i += 2) maxStep = std::max(maxStep, std::fabs(out[i] - out[i - 2]));
    CHECK(maxStep < 0.02f, "salto con chasquido: escalón %.3f", maxStep);
}

static void testSync() {
    std::printf("[sincronización]\n");
    Engine e; e.prepare(48000);
    TrackPtr a = makeBeatTrack(44100, 120, 0.0, 30), b = makeBeatTrack(44100, 126, 0.1, 30);
    loadDeck(e, 0, a.get()); loadDeck(e, 1, b.get());
    e.deck[0].bpm = 120; e.deck[0].gridAnchorSec = 0.0;
    e.deck[1].bpm = 126; e.deck[1].gridAnchorSec = 0.1;
    cmd(e, 0, Cmd::Seek, 3.21 * 44100); cmd(e, 0, Cmd::Play);
    cmd(e, 1, Cmd::Seek, 7.77 * 44100); cmd(e, 1, Cmd::Play);
    render(e, 0.5);
    // Lo que hace la interfaz al pulsar SYNC en B: igualar tempo y alinear fase.
    e.deck[1].pitch = (float)(e.effectiveBpm(0) / 126.0 - 1.0);
    cmd(e, 1, Cmd::SyncPhase, 0, 0);
    render(e, 0.5);
    CHECK(std::fabs(e.effectiveBpm(1) - e.effectiveBpm(0)) < 1e-3, "BPM efectivo %.4f vs %.4f", e.effectiveBpm(1), e.effectiveBpm(0));
    for (int k = 0; k < 3; ++k) {
        render(e, 3.0);
        auto phase = [&](int d) {
            const double sec = e.deckStatus[d].posFrames.load() / 44100.0, beat = 60.0 / e.deck[d].bpm.load();
            double p = std::fmod((sec - e.deck[d].gridAnchorSec.load()) / beat, 1.0);
            return p < 0 ? p + 1 : p;
        };
        double dph = phase(1) - phase(0);
        if (dph > 0.5) dph -= 1; if (dph < -0.5) dph += 1;
        const double ms = dph * 500.0;  // pulso de 0,5 s
        std::printf("  t+%ds: desfase %+.2f ms\n", (k + 1) * 3, ms);
        CHECK(std::fabs(ms) < 2.0, "desfase %.2f ms", ms);
    }
}


// 0.6.1: SYNC llevado por el motor (tempo y fase bloque a bloque).
static void testSyncLock() {
    std::printf("[SYNC 0.6.1: seguimiento de tempo y fase]\n");
    auto phaseMs = [](Engine& e, int d, int o, double factor) {
        const double srA = 44100, beatA = 60.0 / e.deck[d].bpm.load() / factor, beatB = 60.0 / e.deck[o].bpm.load();
        double p = (e.deckStatus[d].posFrames.load() / srA - e.deck[d].gridAnchorSec.load()) / beatA -
                   (e.deckStatus[o].posFrames.load() / srA - e.deck[o].gridAnchorSec.load()) / beatB;
        p -= std::floor(p + 0.5);
        return p * beatB * 1000.0 / (1.0 + e.deck[o].pitch.load());
    };
    {   // B sigue a A; A cambia de pitch y recibe empujones: B no se separa.
        Engine e; e.prepare(48000);
        TrackPtr a = makeBeatTrack(44100, 124, 0.05, 60), b = makeBeatTrack(44100, 128, 0.2, 60);
        loadDeck(e, 0, a.get()); loadDeck(e, 1, b.get());
        e.deck[0].bpm = 124; e.deck[0].gridAnchorSec = 0.05; e.deck[1].bpm = 128; e.deck[1].gridAnchorSec = 0.2;
        cmd(e, 0, Cmd::Seek, 5.0 * 44100); cmd(e, 0, Cmd::Play);
        cmd(e, 1, Cmd::Seek, 9.3 * 44100);
        render(e, 0.3);
        e.deck[1].syncFactor = 1.0; e.deck[1].syncTo = 0;
        cmd(e, 1, Cmd::Play); cmd(e, 1, Cmd::SyncPhase, 0, 0);
        render(e, 1.0);
        CHECK(std::fabs(phaseMs(e, 1, 0, 1.0)) < 1.0, "fase tras SYNC %.2f ms", phaseMs(e, 1, 0, 1.0));
        e.deck[0].pitch = 0.035f;           // el plato que manda acelera
        render(e, 2.0);
        double worst = 0;
        for (int i = 0; i < 20; ++i) { render(e, 0.1); worst = std::max(worst, std::fabs(phaseMs(e, 1, 0, 1.0))); }
        CHECK(std::fabs(e.effectiveBpm(1) - e.effectiveBpm(0)) < 0.01, "tempo seguido: %.3f / %.3f", e.effectiveBpm(1), e.effectiveBpm(0));
        CHECK(worst < 2.0, "fase tras cambiar el pitch del otro: %.2f ms", worst);
        // 0.10.1: empujón en el que manda. El que sigue NO copia el empujón en su pitch (antes lo hacía y el
        // pitch saltaba mucho); el desfase que deja el empujón se respeta, como al empujar el que sigue.
        const float pitchBefore = e.deck[1].pitch.load();
        const double phBefore = phaseMs(e, 1, 0, 1.0);
        float pitchMove = 0.f;
        e.deck[0].bend = 0.04f;
        for (int i = 0; i < 4; ++i) { render(e, 0.1); pitchMove = std::max(pitchMove, std::fabs(e.deck[1].pitch.load() - pitchBefore)); }
        e.deck[0].bend = 0.f;
        render(e, 1.5);
        const double keptM = phaseMs(e, 1, 0, 1.0) - phBefore;
        render(e, 1.5);
        const double keptM2 = phaseMs(e, 1, 0, 1.0) - phBefore;
        CHECK(pitchMove < 1e-4f, "el que sigue no copia el empujón del que manda (pitch movido %.4f)", pitchMove);
        CHECK(std::fabs(std::fabs(keptM) - 16.0) < 3.0 && std::fabs(keptM2 - keptM) < 1.0, "el empujón en el que manda deja su desfase y se mantiene (%.2f → %.2f ms)", keptM, keptM2);
        std::printf("  pitch del otro +3,5 %%: peor desfase %.2f ms; empujón de 0,4 s al 4 %% en el que manda: %.2f ms\n", worst, keptM);
        cmd(e, 1, Cmd::SyncPhase, 0, 0);   // realinear (quita el desfase manual)
        render(e, 0.5);
        // Salto sin cuantizar en el que manda: el que sigue se realinea.
        e.deck[0].quantize = false;
        cmd(e, 0, Cmd::Seek, e.deckStatus[0].posFrames.load() + 0.17 * 44100);
        render(e, 0.2);
        CHECK(std::fabs(phaseMs(e, 1, 0, 1.0)) < 2.0, "fase tras salto del otro: %.2f ms", phaseMs(e, 1, 0, 1.0));
        // Scratch en el que sigue: al soltar, vuelve a estar en fase.
        e.deck[1].scratching = true; e.deck[1].scratchVelocity = -0.7f; render(e, 0.35);
        e.deck[1].scratching = false; e.deck[1].scratchVelocity = 0.f; render(e, 0.2);
        CHECK(std::fabs(phaseMs(e, 1, 0, 1.0)) < 2.0, "fase tras scratch: %.2f ms", phaseMs(e, 1, 0, 1.0));
        // Empujón manual en el que sigue: se respeta el desfase elegido.
        e.deck[1].bend = 0.04f; render(e, 0.25); e.deck[1].bend = 0.f; render(e, 2.0);
        const double kept = phaseMs(e, 1, 0, 1.0);
        CHECK(std::fabs(kept) > 3.0, "el empujón manual se mantiene (%.2f ms)", kept);
        // 0.7: SYNC solo de tempo: iguala el BPM y deja la fase como esté.
        e.deck[1].syncPhase = false;
        cmd(e, 1, Cmd::Seek, e.deckStatus[1].posFrames.load() + 0.11 * 44100);
        render(e, 0.2);
        const double before = phaseMs(e, 1, 0, 1.0);
        e.deck[0].pitch = 0.02f;
        render(e, 2.5);
        const double after = phaseMs(e, 1, 0, 1.0);
        CHECK(std::fabs(e.effectiveBpm(1) - e.effectiveBpm(0)) < 0.01 && std::fabs(after - before) < 3.0 && std::fabs(after) > 20.0,
              "SYNC solo de tempo: BPM %.3f/%.3f, fase %.1f -> %.1f ms", e.effectiveBpm(1), e.effectiveBpm(0), before, after);
        // Y al volver a tempo y fase, un SyncPhase lo realinea.
        e.deck[1].syncPhase = true;
        cmd(e, 1, Cmd::SyncPhase, 0, 0);
        render(e, 0.5);
        CHECK(std::fabs(phaseMs(e, 1, 0, 1.0)) < 2.0, "realinear tras el modo solo tempo: %.2f ms", phaseMs(e, 1, 0, 1.0));
    }
    {   // Tempo doble: una pista analizada a 64 BPM sigue a otra de 128.
        Engine e; e.prepare(48000);
        TrackPtr a = makeBeatTrack(44100, 128, 0.0, 40), b = makeBeatTrack(44100, 130, 0.1, 40);
        loadDeck(e, 0, a.get()); loadDeck(e, 1, b.get());
        e.deck[0].bpm = 128; e.deck[1].bpm = 65; e.deck[1].gridAnchorSec = 0.1;   // B detectado a la mitad
        cmd(e, 0, Cmd::Play); render(e, 0.2);
        e.deck[1].syncFactor = 2.0; e.deck[1].syncTo = 0;
        cmd(e, 1, Cmd::Play); cmd(e, 1, Cmd::SyncPhase, 0, 0);
        render(e, 3.0);
        CHECK(std::fabs(e.deck[1].pitch.load() - (128.0 / 130.0 - 1.0)) < 1e-4, "pitch con tempo doble %.4f", e.deck[1].pitch.load());
        CHECK(std::fabs(phaseMs(e, 1, 0, 2.0)) < 1.5, "fase con tempo doble %.2f ms", phaseMs(e, 1, 0, 2.0));
    }
}

// 0.6.1: BPM con decimales (vinilos, versiones aceleradas): la rejilla no debe derivar.
static void testBpmPrecision() {
    std::printf("[precisión del BPM]\n");
    for (double bpm : {127.96, 126.37, 128.0}) {
        TrackPtr t = makeBeatTrack(44100, bpm, 0.31, 150);
        BeatInfo bi = detectBeats(t->pcm.data(), t->frames, 44100, 70, 180);
        const double driftMs = std::fabs(bi.bpm - bpm) / bpm * 150.0 * 1000.0;
        std::printf("  %.2f BPM -> %.3f (deriva en 150 s: %.1f ms)\n", bpm, bi.bpm, driftMs);
        CHECK(driftMs < 12.0, "BPM %.3f para %.2f: la rejilla deriva %.1f ms", bi.bpm, bpm, driftMs);
    }
}

static void testRecording() {
    std::printf("[grabación de la mezcla y reproducción del archivo]\n");
    for (RecFormat fmt : {RecFormat::WavFloat32, RecFormat::Wav16}) {
        Engine e; e.prepare(48000);
        TrackPtr a = makeTrack(48000, 20, [](double t, float& l, float& r) { l = 0.4f * (float)std::sin(2 * kPi * 523 * t); r = 0.4f * (float)std::sin(2 * kPi * 659 * t); });
        TrackPtr b = makeBeatTrack(44100, 128, 0, 20);
        loadDeck(e, 0, a.get()); loadDeck(e, 1, b.get());
        e.deck[0].fader = 1; e.deck[1].fader = 1; e.mixer.masterVol = 1; e.mixer.crossfader = 0.5f;
        cmd(e, 0, Cmd::Play); cmd(e, 1, Cmd::Play);
        render(e, 0.2);
        const fs::path p = tmpDir() / (fmt == RecFormat::Wav16 ? "grabacion16.wav" : "grabacion32.wav");
        Recorder rec;
        std::string err;
        CHECK(rec.start(e, p.wstring(), fmt, err), "%s", err.c_str());
        // Este hilo hace de hilo de audio, a ritmo algo más rápido que el real.
        std::vector<float> ref;
        for (int k = 0; k < 300; ++k) {
            auto blk = render(e, 0.01);
            ref.insert(ref.end(), blk.begin(), blk.end());
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        rec.stop();
        CHECK(e.status.recDropped.load() == 0, "muestras perdidas: %llu", (unsigned long long)e.status.recDropped.load());
        TrackPtr t = decodeFile(p.wstring(), err);
        CHECK(t != nullptr, "no se puede leer la grabación: %s", err.c_str());
        if (!t) continue;
        CHECK(t->frames == ref.size() / 2, "longitud %llu esperada %zu", (unsigned long long)t->frames, ref.size() / 2);
        float md = 0;
        const size_t n = std::min(t->pcm.size(), ref.size());
        for (size_t i = 0; i < n; ++i) md = std::max(md, std::fabs(t->pcm[i] - ref[i]));
        const float tol = fmt == RecFormat::Wav16 ? 2.5e-4f : 1e-7f;
        std::printf("  %s: %.2f s, diferencia máxima con la salida %.2e\n", fmt == RecFormat::Wav16 ? "WAV 16 bits" : "WAV 32 float", t->lengthSec(), md);
        CHECK(md <= tol, "la grabación no coincide con la salida (%.2e)", md);
    }
}

static void testDevices(bool real) {
    std::printf("[dispositivos: %s]\n", real ? "reales (salida en silencio)" : "backend simulado");
    Engine e;
    AudioIO io;
    std::string err;
    CHECK(io.init(err, !real), "%s", err.c_str());
    TrackPtr a = makeTrack(44100, 30, [](double t, float& l, float& r) { l = r = 0.2f * (float)std::sin(2 * kPi * 440 * t); });
    e.mixer.masterVol = 0.f;  // silencio: no molestar al usuario
    AudioConfig cfg;
    CHECK(io.open(e, cfg), "%s", io.state().message.c_str());
    std::printf("  abierto: %s / %s, %u Hz, %d canales, periodo %d. %s\n", io.state().backend.c_str(), io.state().deviceName.c_str(),
                io.state().sampleRate, io.state().channels, io.state().periodFrames, io.state().message.c_str());
    Command c{Cmd::Load, 0}; c.track = a.get(); c.gen = 7; e.send(c);
    Command pl{Cmd::Play, 0}; e.send(pl);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    CHECK(e.deckStatus[0].loadedGen.load() == 7, "el callback no procesa órdenes");
    const double p1 = e.deckStatus[0].posFrames.load();
    CHECK(p1 > 0.2 * 44100, "la posición no avanza: %.0f", p1);
    // Cambio de dispositivo con la pista sonando: la posición se conserva.
    if (real) {
        for (auto& d : io.devices()) {
            AudioConfig c2; c2.deviceName = d.name;
            const bool ok = io.open(e, c2);
            std::printf("  cambio a \"%s\" (%d canales máx.): %s %s\n", d.name.c_str(), d.maxChannels, ok ? "ok" : "FALLO", io.state().message.c_str());
            CHECK(ok && !io.state().simulated, "no se pudo abrir %s", d.name.c_str());
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        AudioConfig bad; bad.deviceName = "Dispositivo que no existe";
        CHECK(io.open(e, bad) && io.state().running, "sin recuperación ante dispositivo inexistente");
        std::printf("  dispositivo inexistente -> %s\n", io.state().message.c_str());
    } else {
        CHECK(io.open(e, cfg), "reapertura");
    }
    const double p2 = e.deckStatus[0].posFrames.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const double p3 = e.deckStatus[0].posFrames.load();
    CHECK(p2 >= p1 && p3 > p2, "la reproducción no continúa tras reabrir (%.0f, %.0f, %.0f)", p1, p2, p3);
    io.close();
}

static void testLoader() {
    std::printf("[cargador en segundo plano]\n");
    const fs::path dir = tmpDir();
    TrackPtr beat = makeBeatTrack(44100, 132, 0.25, 40);
    std::vector<int16_t> s(beat->pcm.size());
    for (size_t i = 0; i < s.size(); ++i) s[i] = (int16_t)std::lround(std::clamp(beat->pcm[i], -1.f, 1.f) * 32767);
    CHECK(writeWav(dir / "beat132.wav", 44100, s), "wav");
    TrackLoader ld;
    LoadJob j; j.deck = 1; j.path = (dir / "beat132.wav").wstring();
    ld.request(j);
    LoadJob bad; bad.deck = 0; bad.path = (dir / "texto.mp3").wstring();
    ld.request(bad);
    LoadResult r; int got = 0; bool okBeat = false, okBad = false;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (got < 2 && std::chrono::steady_clock::now() < until) {
        if (ld.poll(r)) {
            ++got;
            if (r.deck == 1) { okBeat = r.track && std::fabs(r.track->meta.bpm - 132) < 0.05 && r.analyzedNow; std::printf("  beat132.wav -> %.2f BPM\n", r.track ? r.track->meta.bpm : 0.0); }
            if (r.deck == 0) { okBad = !r.track && !r.error.empty(); }
        } else std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(okBeat, "carga con análisis");
    CHECK(okBad, "error de archivo incompatible");
    // Cancelación: dos peticiones seguidas al mismo plato -> solo llega la última
    ld.request(j);
    LoadJob j2 = j; j2.path = (dir / "seno.wav").wstring();
    ld.request(j2);
    got = 0; std::wstring last;
    const auto until2 = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < until2) {
        if (ld.poll(r)) { ++got; if (r.track) last = r.track->path; }
        else if (!ld.busy(1)) break;
        else std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(got == 1 && last == j2.path, "se esperaba solo la última carga (llegaron %d)", got);
}

// Progresión de acordes sintética (notas MIDI) con armónicos y bajo.
static TrackPtr makeChordTrack(const std::vector<std::vector<int>>& chords, double secPerChord) {
    return makeTrack(44100, secPerChord * chords.size() * 4, [&](double t, float& l, float& r) {
        const auto& ch = chords[(size_t)(t / secPerChord) % chords.size()];
        double v = 0;
        for (size_t i = 0; i < ch.size(); ++i) {
            const double f = 440.0 * std::pow(2.0, (ch[i] - 69) / 12.0);
            for (int h = 1; h <= 4; ++h) v += std::sin(2 * kPi * f * h * t) / (h * 1.5);
        }
        const double fb = 440.0 * std::pow(2.0, (ch[0] - 12 - 69) / 12.0);  // bajo una octava abajo
        v += 0.8 * std::sin(2 * kPi * fb * t);
        l = r = (float)(v * 0.08);
    });
}

static void testKey() {
    std::printf("[tonalidad]\n");
    // Do mayor: C - F - G - C ; La menor: Am - Dm - E - Am ; Sol mayor: G - C - D - G
    struct K { std::vector<std::vector<int>> ch; const char* expect; };
    std::vector<K> cases = {
        {{{60, 64, 67}, {65, 69, 72}, {67, 71, 74}, {60, 64, 67}}, "C"},
        {{{57, 60, 64}, {62, 65, 69}, {64, 68, 71}, {57, 60, 64}}, "Am"},
        {{{55, 59, 62}, {60, 64, 67}, {62, 66, 69}, {55, 59, 62}}, "G"},
        {{{62, 65, 69}, {67, 70, 74}, {69, 73, 76}, {62, 65, 69}}, "Dm"},
    };
    for (auto& c : cases) {
        TrackPtr t = makeChordTrack(c.ch, 2.0);
        KeyInfo k = detectKey(t->pcm.data(), t->frames, t->sampleRate);
        std::printf("  esperado %-3s -> %s (%s), fuerza %.2f\n", c.expect, keyName(k.key), keyCamelot(k.key), k.strength);
        CHECK(std::string(keyName(k.key)) == c.expect, "tonalidad %s detectada como %s", c.expect, keyName(k.key));
    }
    CHECK(keysCompatible(21, 0) && keysCompatible(21, 16) && keysCompatible(0, 7) && !keysCompatible(0, 6), "compatibilidad Camelot");
    CHECK(std::string(keyCamelot(21)) == "8A" && std::string(keyCamelot(0)) == "8B" && std::string(keyCamelot(7)) == "9B", "códigos Camelot");
}

static void testKeyLock() {
    std::printf("[key lock y cambio de tono]\n");
    Engine e; e.prepare(48000);
    // Tono de 440 Hz con un clic en t = 5 s para medir la alineación temporal.
    TrackPtr a = makeTrack(48000, 20, [](double t, float& l, float& r) {
        l = r = 0.3f * (float)std::sin(2 * kPi * 440 * t) + ((t >= 5.0 && t < 5.0005) ? 0.9f : 0.f);
    });
    loadDeck(e, 0, a.get());
    e.deck[0].fader = 1; e.mixer.masterVol = 1; e.mixer.crossfader = 0;
    auto freqPeak = [&](const std::vector<float>& out, double f) { return goertzel(out.data() + 9600 * 2, 24000, 2, f, 48000); };
    // Pitch +8 % con key lock: el tono sigue en 440 Hz y la posición avanza un 8 % más rápido.
    e.deck[0].pitch = 0.08f;
    e.deck[0].keyLock = true;
    cmd(e, 0, Cmd::Seek, 1.0 * 48000);
    cmd(e, 0, Cmd::Play);
    const auto t0 = std::chrono::steady_clock::now();
    auto out = render(e, 1.0);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const double g440 = freqPeak(out, 440), g475 = freqPeak(out, 475.2);
    std::printf("  +8 %% con key lock: 440 Hz %.3f, 475 Hz %.3f (1 s de audio en %.0f ms)\n", g440, g475, ms);
    CHECK(g440 > 0.2 && g475 < 0.05, "el key lock no mantiene el tono");
    const double p1 = e.deckStatus[0].posFrames.load() / 48000;
    CHECK(std::fabs(p1 - (1.0 + 1.08)) < 0.01, "la posición no avanza al tempo: %.4f", p1);
    // Sin key lock el tono sube a 475 Hz
    e.deck[0].keyLock = false;
    out = render(e, 1.0);
    CHECK(freqPeak(out, 475.2) > 0.2, "sin key lock el tono debería subir");
    // +12 semitonos con tempo normal: 880 Hz
    e.deck[0].pitch = 0.f;
    e.deck[0].keyShift = 12.f;
    out = render(e, 1.0);
    CHECK(freqPeak(out, 880) > 0.15 && freqPeak(out, 440) < 0.05, "transposición +12: 880 %.3f 440 %.3f", freqPeak(out, 880), freqPeak(out, 440));
    e.deck[0].keyShift = 0.f;
    // Alineación: con key lock, el clic de t=5 s debe sonar cuando la posición publicada pasa por 5 s.
    e.deck[0].keyLock = true;
    e.deck[0].pitch = 0.04f;
    cmd(e, 0, Cmd::Seek, 4.0 * 48000);
    render(e, 0.3);   // tras el salto y el recebado
    const double posStart = e.deckStatus[0].posFrames.load() / 48000;
    out = render(e, 1.0);
    int idx = -1;
    for (size_t i = 0; i < out.size() / 2; ++i) if (std::fabs(out[i * 2]) > 0.6f) { idx = (int)i; break; }
    const double expected = (5.0 - posStart) / 1.04;  // segundos de salida hasta el clic
    const double errMs = idx < 0 ? 1e9 : (idx / 48000.0 - expected) * 1000;
    std::printf("  alineación con key lock: error %.1f ms\n", errMs);
    CHECK(std::fabs(errMs) < 6.0, "el audio con key lock no coincide con la posición (%.1f ms)", errMs);
    // Sincronía de dos platos, uno con key lock y pitch movido, sin deriva de fase.
    TrackPtr b1 = makeBeatTrack(44100, 120, 0.0, 40), b2 = makeBeatTrack(44100, 126, 0.0, 40);
    Engine s; s.prepare(48000);
    loadDeck(s, 0, b1.get()); loadDeck(s, 1, b2.get());
    s.deck[0].bpm = 120; s.deck[1].bpm = 126;
    s.deck[1].keyLock = true;
    cmd(s, 0, Cmd::Play); cmd(s, 1, Cmd::Play);
    render(s, 0.5);
    s.deck[1].pitch = (float)(120.0 / 126.0 - 1.0);
    cmd(s, 1, Cmd::SyncPhase, 0, 0);
    render(s, 4.0);
    auto phase = [&](int d) { const double sec = s.deckStatus[d].posFrames.load() / 44100.0, beat = 60.0 / s.deck[d].bpm.load(); double ph = std::fmod(sec / beat, 1.0); return ph < 0 ? ph + 1 : ph; };
    double dph = phase(1) - phase(0);
    if (dph > 0.5) dph -= 1; if (dph < -0.5) dph += 1;
    CHECK(std::fabs(dph * 500) < 2.0, "sync con key lock: desfase %.2f ms", dph * 500);
    std::printf("  sync con key lock: desfase %+.2f ms\n", dph * 500);
}

static void testStemPlayer() {
    std::printf("[reproductor de stems]\n");
    Engine e; e.prepare(48000);
    e.mixer.masterVol = 1.f;
    const double freqs[3] = {300, 500, 700};
    TrackPtr t[3];
    for (int k = 0; k < 3; ++k) { const double f = freqs[k]; t[k] = makeTrack(44100, 6, [f](double tt, float& l, float& r) { l = r = 0.2f * (float)std::sin(2 * kPi * f * tt); }); }
    StemSet set;
    for (int k = 0; k < 3; ++k) set.tracks[k] = t[k].get();
    set.count = 3;
    Command c{Cmd::StemLoad, 0}; c.stems = &set; c.gen = 5; e.send(c);
    e.send(Command{Cmd::StemPlay, 0});
    auto out = render(e, 1.0);
    CHECK(e.stemStatus.loadedGen.load() == 5 && e.stemStatus.playing.load(), "stems cargados y sonando");
    auto g = [&](double f) { return goertzel(out.data() + 9600 * 2, 24000, 2, f, 48000); };
    CHECK(g(300) > 0.1 && g(500) > 0.1 && g(700) > 0.1, "las tres pistas suenan juntas (%.3f %.3f %.3f)", g(300), g(500), g(700));
    const double p1 = e.stemStatus.posFrames.load() / 44100.0;
    CHECK(std::fabs(p1 - 1.0) < 0.01, "posición de los stems %.3f", p1);
    e.stem.mute[1] = true;
    out = render(e, 1.0);
    CHECK(20 * std::log10(g(500) + 1e-12) < -60 && g(300) > 0.1, "silenciar una pista");
    e.stem.mute[1] = false;
    e.stem.solo[2] = true;
    out = render(e, 1.0);
    CHECK(g(700) > 0.1 && 20 * std::log10(g(300) + 1e-12) < -60 && 20 * std::log10(g(500) + 1e-12) < -60, "solo de una pista");
    e.stem.solo[2] = false;
    Command sk{Cmd::StemSeek, 0}; sk.d = 5.5 * 44100; e.send(sk);
    render(e, 1.0);
    CHECK(!e.stemStatus.playing.load(), "los stems se paran al final");
}

static void testRouting() {
    std::printf("[preescucha y enrutado de salidas]\n");
    Engine e; e.prepare(48000);
    TrackPtr a = makeTrack(48000, 10, [](double t, float& l, float& r) { l = r = 0.3f * (float)std::sin(2 * kPi * 440 * t); });
    TrackPtr b = makeTrack(48000, 10, [](double t, float& l, float& r) { l = r = 0.3f * (float)std::sin(2 * kPi * 660 * t); });
    loadDeck(e, 0, a.get()); loadDeck(e, 1, b.get());
    e.deck[0].fader = 1; e.deck[1].fader = 1; e.mixer.masterVol = 1; e.mixer.crossfader = 0.5f;
    e.deck[0].pfl = true;  // solo A en auriculares
    e.mixer.hpMix = 0.f; e.mixer.hpVol = 1.f;
    cmd(e, 0, Cmd::Play); cmd(e, 1, Cmd::Play);
    auto db = [](double v) { return 20 * std::log10(v + 1e-12); };
    // 4 canales: 1-2 master, 3-4 auriculares
    e.mixer.hpRouting = (int)HpRouting::Canales34;
    auto out = render(e, 0.5, 4);
    const int n = 12000;
    const float* tail = out.data() + (size_t)9600 * 4;
    CHECK(goertzel(tail, n, 4, 440, 48000) > 0.15 && goertzel(tail, n, 4, 660, 48000) > 0.15, "master sin las dos pistas");
    CHECK(goertzel(tail + 2, n, 4, 440, 48000) > 0.2, "auriculares sin la pista en PFL");
    CHECK(db(goertzel(tail + 2, n, 4, 660, 48000)) < -60, "auriculares con la pista sin PFL");
    e.mixer.hpMix = 1.f;
    out = render(e, 0.5, 4);
    tail = out.data() + (size_t)9600 * 4;
    CHECK(goertzel(tail + 3, n, 4, 660, 48000) > 0.1, "la mezcla de auriculares no lleva el master");
    e.mixer.hpMix = 0.f;
    e.mixer.hpRouting = (int)HpRouting::Ninguna;
    out = render(e, 0.2, 4);
    float mx = 0;
    for (size_t i = 0; i < out.size(); i += 4) mx = std::max(mx, std::max(std::fabs(out[i + 2]), std::fabs(out[i + 3])));
    CHECK(mx == 0.f, "los canales 3-4 deberían estar en silencio");
    e.mixer.hpRouting = (int)HpRouting::DivisionMono;
    out = render(e, 0.5, 2);
    tail = out.data() + (size_t)9600 * 2;
    CHECK(goertzel(tail, n, 2, 440, 48000) > 0.2 && db(goertzel(tail, n, 2, 660, 48000)) < -60, "izquierda = solo preescucha");
    CHECK(goertzel(tail + 1, n, 2, 660, 48000) > 0.1, "derecha = master");
    e.mixer.hpRouting = (int)HpRouting::DispositivoAparte;
    e.hpRing.skip(e.hpRing.available());
    render(e, 0.1, 2);
    std::vector<float> hp(9600);
    const size_t got = e.hpRing.read(hp.data(), hp.size());
    CHECK(got == 9600 && goertzel(hp.data(), 4800, 2, 440, 48000) > 0.2, "anillo de auriculares (%zu muestras)", got);
}

static void testEffects() {
    std::printf("[efectos]\n");
    Engine e; e.prepare(48000);
    TrackPtr a = makeTrack(48000, 10, [](double t, float& l, float& r) { l = r = t < 0.01 ? 0.8f * (float)std::sin(2 * kPi * 1000 * t) : 0.f; });
    loadDeck(e, 0, a.get());
    e.deck[0].fader = 1; e.mixer.masterVol = 1; e.mixer.crossfader = 0;
    e.deck[0].bpm = 120;
    for (int fx = 1; fx <= 3; ++fx) {
        cmd(e, 0, Cmd::Seek, 0);
        e.deck[0].fxType = fx; e.deck[0].fxOn = true; e.deck[0].fxAmount = 0.6f; e.deck[0].fxBeats = 1.f;
        cmd(e, 0, Cmd::Play);
        auto out = render(e, 1.5);
        bool nan = false; float pk = 0; double late = 0;
        for (size_t i = 0; i < out.size(); ++i) { if (!std::isfinite(out[i])) nan = true; pk = std::max(pk, std::fabs(out[i])); }
        for (size_t i = (size_t)(0.45 * 48000) * 2; i < (size_t)(0.6 * 48000) * 2; ++i) late += out[i] * out[i];
        std::printf("  %s: energía tras el golpe %.4f, pico %.3f\n", fxName((FxType)fx), late, pk);
        CHECK(!nan && pk <= 1.f, "%s inestable", fxName((FxType)fx));
        if (fx != (int)FxType::Flanger) CHECK(late > 1e-3, "%s sin cola", fxName((FxType)fx));
        e.deck[0].fxOn = false;
        render(e, 7.0);
        out = render(e, 0.2);
        float rest = 0;
        for (float v : out) rest = std::max(rest, std::fabs(v));
        CHECK(rest < 1e-3f, "%s no se apaga (%.4f)", fxName((FxType)fx), rest);
        cmd(e, 0, Cmd::Pause);
    }
}

static void testMidi() {
    std::printf("[MIDI: aprendizaje y órdenes]\n");
    MidiManager m;
    m.startLearn(MA_Play, 1);
    m.pushEvent({0, 0x91, 0x0B, 127});
    auto c = m.poll();
    CHECK(c.empty() && !m.learning() && m.bindings().size() == 1, "aprendizaje");
    CHECK(m.bindings()[0].channel == 1 && m.bindings()[0].type == 0x9 && m.bindings()[0].data1 == 0x0B && m.bindings()[0].deck == 1, "datos aprendidos");
    m.pushEvent({0, 0x91, 0x0B, 127});
    m.pushEvent({0, 0x81, 0x0B, 0});
    m.pushEvent({0, 0x91, 0x0B, 0});  // nota con velocidad 0 = soltar
    c = m.poll();
    CHECK(c.size() == 3 && c[0].pressed && !c[1].pressed && !c[2].pressed && c[0].action == MA_Play && c[0].deck == 1, "pulsar / soltar");
    m.startLearn(MA_Crossfader, 0);
    m.pushEvent({0, 0xB0, 0x1F, 10});
    m.poll();
    m.pushEvent({0, 0xB0, 0x1F, 127});
    c = m.poll();
    CHECK(c.size() == 1 && std::fabs(c[0].value - 1.f) < 1e-6f, "control absoluto");
    m.startLearn(MA_Jog, 0);
    m.pushEvent({0, 0xB0, 0x21, 65});
    m.poll();
    m.pushEvent({0, 0xB0, 0x21, 66});
    m.pushEvent({0, 0xB0, 0x21, 61});
    c = m.poll();
    CHECK(c.size() == 2 && c[0].delta == 2 && c[1].delta == -3, "relativo centrado en 64");
    // Reasignar un mensaje ya usado lo quita de la acción anterior
    m.startLearn(MA_Cue, 1);
    m.pushEvent({0, 0x91, 0x0B, 100});
    m.poll();
    int playCount = 0;
    for (auto& b : m.bindings()) if (b.action == MA_Play) ++playCount;
    CHECK(playCount == 0, "mensaje duplicado en dos acciones");
    m.settings().jogScratchSens = 2.5f;
    m.settings().softTakeover = false;
    m.save();
    MidiManager m2;
    m2.load();
    CHECK(m2.bindings().size() == m.bindings().size(), "persistencia (%zu frente a %zu)", m2.bindings().size(), m.bindings().size());
    CHECK(m2.settings().jogScratchSens == 2.5f && !m2.settings().softTakeover, "persistencia de los ajustes del mando");

    // Aprender filtra por tipo: un botón no aprende un CC y un fader no aprende una nota.
    MidiManager f;
    f.startLearn(MA_Play, 0);
    f.pushEvent({0, 0xB0, 0x05, 40});
    f.poll();
    CHECK(f.learning() && f.bindings().empty(), "un botón no debe aprender un CC");
    f.pushEvent({0, 0x90, 0x0B, 127});
    f.poll();
    CHECK(!f.learning() && f.bindings().size() == 1, "el botón aprende la nota");
    f.startLearn(MA_JogScratch, 0);
    f.pushEvent({0, 0x90, 0x36, 127});  // tocar el plato (nota) se ignora
    f.pushEvent({0, 0xB0, 0x22, 66});   // girar el plato (CC) se aprende
    f.poll();
    const MidiBinding* js = f.findBinding(MA_JogScratch, 0);
    CHECK(js && js->type == 0xB && js->data1 == 0x22, "el plato del jog aprende el CC, no la nota de tocar");

    // 14 bits: el byte bajo (cc + 0x20) refina el valor del byte alto.
    MidiManager h;
    h.startLearn(MA_Volume, 0);
    h.pushEvent({0, 0xB0, 0x13, 64});
    h.poll();
    h.pushEvent({0, 0xB0, 0x13, 100});
    h.pushEvent({0, 0xB0, 0x33, 64});
    auto hc = h.poll();
    CHECK(hc.size() == 2 && std::fabs(hc[0].value - 100 / 127.f) < 1e-6f && std::fabs(hc[1].value - (100 * 128 + 64) / 16383.f) < 1e-6f,
          "14 bits (%zu órdenes)", hc.size());
    CHECK(h.findBinding(MA_Volume, 0)->hiRes, "detección automática de 14 bits");

    // Capa SHIFT: con SHIFT pulsado manda la asignación de la capa SHIFT.
    MidiManager sh;
    sh.startLearn(MA_Shift, 0);
    sh.pushEvent({0, 0x96, 0x4F, 127}); sh.poll();
    sh.pushEvent({0, 0x96, 0x4F, 0}); sh.poll();
    sh.startLearn(MA_Play, 0);
    sh.pushEvent({0, 0x90, 0x0B, 127}); sh.poll();
    sh.startLearn(MA_Sync, 0, true);
    sh.pushEvent({0, 0x90, 0x0B, 127}); sh.poll();
    sh.pushEvent({0, 0x90, 0x0B, 127});
    auto c1 = sh.poll();
    sh.pushEvent({0, 0x96, 0x4F, 127});
    sh.pushEvent({0, 0x90, 0x0B, 127});
    auto c2 = sh.poll();
    const bool c1ok = c1.size() == 1 && c1[0].action == MA_Play;
    bool c2ok = false;
    for (auto& c : c2) if (c.action == MA_Sync) c2ok = true;
    for (auto& c : c2) if (c.action == MA_Play) c2ok = false;
    CHECK(c1ok && c2ok && sh.shiftHeld(), "capa SHIFT");

    // Invertir pitch
    MidiManager pi;
    pi.startLearn(MA_Pitch, 0);
    pi.pushEvent({0, 0xB0, 0x00, 10}); pi.poll();
    pi.settings().pitchInvert = true;
    pi.pushEvent({0, 0xB0, 0x00, 0});
    auto pc = pi.poll();
    CHECK(pc.size() == 1 && pc[0].value == 1.f, "pitch invertido");

    // Preajuste DDJ-WeGO: valores comprobados por el usuario y patrón de la familia.
    MidiManager w;
    w.loadPresetWeGO();
    const MidiBinding* vol = w.findBinding(MA_Volume, 1);
    const MidiBinding* xf = w.findBinding(MA_Crossfader, 0);
    const MidiBinding* pit = w.findBinding(MA_Pitch, 0);
    const MidiBinding* plat = w.findBinding(MA_JogScratch, 1);
    CHECK(vol && vol->channel == 1 && vol->data1 == 0x13, "preajuste: volumen B");
    CHECK(xf && xf->channel == 6 && xf->data1 == 0x1F, "preajuste: crossfader");
    CHECK(pit && pit->channel == 0 && pit->data1 == 0x00, "preajuste: pitch A");
    CHECK(plat && plat->channel == 1 && plat->data1 == 0x22, "preajuste: plato del jog B");
}

static std::string id3Frame(const char* id, const std::string& txt) {
    std::string f(id, 4);
    const uint32_t sz = (uint32_t)txt.size() + 1;
    f += (char)(sz >> 24); f += (char)(sz >> 16); f += (char)(sz >> 8); f += (char)sz;
    f += '\0'; f += '\0'; f += (char)3;  // codificación UTF-8
    return f + txt;
}

static void testLibrary() {
    std::printf("[etiquetas y biblioteca]\n");
    const fs::path dir = tmpDir() / "musica";
    fs::remove_all(dir);
    fs::create_directories(dir / "sub");
    {
        const std::string body = id3Frame("TIT2", "Canción Ácida") + id3Frame("TPE1", "Ñandú Sound") + id3Frame("TBPM", "128");
        std::string hdr = "ID3";
        hdr += (char)3; hdr += (char)0; hdr += (char)0;
        const uint32_t n = (uint32_t)body.size();
        hdr += (char)((n >> 21) & 0x7F); hdr += (char)((n >> 14) & 0x7F); hdr += (char)((n >> 7) & 0x7F); hdr += (char)(n & 0x7F);
        std::ofstream(dir / "id3.mp3", std::ios::binary) << hdr << body << std::string(256, (char)0xFF);
    }
    TagInfo ti = readTags((dir / "id3.mp3").wstring());
    CHECK(ti.title == "Canción Ácida" && ti.artist == "Ñandú Sound" && ti.bpm == 128, "ID3: '%s' / '%s' / %.0f", ti.title.c_str(), ti.artist.c_str(), ti.bpm);
    std::vector<int16_t> s(48000 * 2, 0);
    writeWav(dir / L"Los Ríos - Noche de Verano.wav", 48000, s);
    writeWav(dir / "sub" / "pista2.wav", 48000, s);
    { std::ofstream(dir / "notas.txt") << "x"; }
    ti = readTags((dir / L"Los Ríos - Noche de Verano.wav").wstring());
    CHECK(ti.artist == "Los Ríos" && ti.title == "Noche de Verano", "nombre de archivo: '%s' / '%s'", ti.artist.c_str(), ti.title.c_str());
    CHECK(foldForSearch("Canción ÁCIDA Ñ") == "cancion acida n", "normalización: %s", foldForSearch("Canción ÁCIDA Ñ").c_str());
    {
        Library lib;
        lib.addFolder(dir.wstring());
        for (int i = 0; i < 300 && (lib.scanning() || lib.tracks().size() < 3); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CHECK(lib.tracks().size() == 3, "escaneo recursivo: %zu pistas", lib.tracks().size());
        CHECK(lib.query("acida", -1).size() == 1, "búsqueda sin tildes");
        CHECK(lib.query("rios verano", -1).size() == 1, "búsqueda por varias palabras");
        CHECK(lib.query("xyz", -1).empty(), "búsqueda sin resultados");
        const int pl = lib.createPlaylist("Sesión");
        lib.addToPlaylist(pl, {(dir / "sub" / "pista2.wav").wstring(), (dir / "id3.mp3").wstring()});
        CHECK(lib.query("", pl).size() == 2, "lista");
        TrackMeta m; m.bpm = 126.5; m.analyzed = true; m.hotCuesSec[2] = 12.25;
        lib.updateMeta((dir / "id3.mp3").wstring(), m);
        CHECK(lib.exportM3u(pl, (dir / "lista.m3u8").wstring()), "exportar M3U8");
        const int imp = lib.importM3u((dir / "lista.m3u8").wstring());
        CHECK(imp >= 0 && lib.query("", imp).size() == 2, "importar M3U8");
        lib.save();
    }
    Library lib2;
    lib2.load();
    TrackMeta m2;
    CHECK(lib2.metaFor((dir / "id3.mp3").wstring(), m2) && m2.bpm == 126.5 && m2.hotCuesSec[2] == 12.25, "persistencia de BPM y hot cues");
    CHECK(lib2.playlists().size() == 2, "persistencia de listas (%zu)", lib2.playlists().size());
}


// 0.6.2: tonalidad escrita (etiquetas, VirtualDJ, Open Key) e importación de VirtualDJ.
static void testKeysAndVdj() {
    std::printf("[tonalidad: notaciones, etiquetas e importación de VirtualDJ]\n");
    const int am = parseKey("8A"), c = parseKey("8B"), fsm = parseKey("11A");
    CHECK(am >= 0 && std::string(keyCamelot(am)) == "8A" && std::string(keyName(am)) == "Am", "8A -> %s / %s", am >= 0 ? keyCamelot(am) : "-", am >= 0 ? keyName(am) : "-");
    CHECK(c >= 0 && std::string(keyName(c)) == "C" && keyIsMajor(c) && !keyIsMajor(am), "8B = Do mayor");
    struct P { const char* txt; int want; };
    for (P t : {P{"08A", am}, P{"Am", am}, P{"A minor", am}, P{"Amin", am}, P{"a", -2}, P{"La menor", am}, P{"la m", am}, P{"1m", am},
                P{"C", c}, P{"Cmaj", c}, P{"C major", c}, P{"Do Mayor", c}, P{"1d", c}, P{"8b", c},
                P{"F#m", fsm}, P{"F♯m", fsm}, P{"Gbm", fsm}, P{"Sol bemol menor", fsm}, P{"Fa sostenido menor", fsm}, P{"4m", fsm}, P{"11A", fsm},
                P{"", -1}, P{"xyz", -1}, P{"13A", -1}, P{"0B", -1}}) {
        if (t.want == -2) continue;  // "a" a secas es ambiguo (La mayor): no se comprueba
        const int k = parseKey(t.txt);
        CHECK(k == t.want, "parseKey(\"%s\") = %d (se esperaba %d)", t.txt, k, t.want);
    }
    CHECK(keyOpenKey(am) == "1m" && keyOpenKey(c) == "1d" && keyOpenKey(parseKey("12A")) == "5m" && keyOpenKey(fsm) == "4m",
          "Open Key: %s %s %s %s", keyOpenKey(am).c_str(), keyOpenKey(c).c_str(), keyOpenKey(parseKey("12A")).c_str(), keyOpenKey(fsm).c_str());
    for (int k = 0; k < 24; ++k) {
        CHECK(parseKey(keyCamelot(k)) == k && parseKey(keyName(k)) == k && parseKey(keyOpenKey(k)) == k && parseKey(keyNameEs(k)) == k,
              "ida y vuelta de la tonalidad %d (%s / %s / %s / %s)", k, keyCamelot(k), keyName(k), keyOpenKey(k).c_str(), keyNameEs(k));
        CHECK(keyFromCamelot(keyCamelotNumber(k), keyIsMajor(k)) == k, "keyFromCamelot %d", k);
    }
    CHECK(keysClose(am, parseKey("3A")) && !keysClose(am, parseKey("9A")) && !keysCompatible(am, parseKey("3A")), "±1 semitono");

    const fs::path dir = tmpDir() / "vdj";
    fs::remove_all(dir);
    // Biblioteca propia y vacía para esta prueba (no depende de ejecuciones anteriores).
    wchar_t oldData[MAX_PATH] = {};
    GetEnvironmentVariableW(L"RDJ_DATA_DIR", oldData, MAX_PATH);
    fs::remove_all(tmpDir() / "datos_vdj");
    fs::create_directories(tmpDir() / "datos_vdj");
    SetEnvironmentVariableW(L"RDJ_DATA_DIR", (tmpDir() / "datos_vdj").wstring().c_str());
    fs::create_directories(dir / "musica");
    fs::create_directories(dir / "VirtualDJ" / "Playlists" / "Sub");
    // Etiqueta TKEY en un MP3 (Mixed In Key / rekordbox la escriben así).
    {
        const std::string body = id3Frame("TIT2", "Con tono") + id3Frame("TKEY", "11B");
        std::string hdr = "ID3";
        hdr += (char)3; hdr += (char)0; hdr += (char)0;
        const uint32_t n = (uint32_t)body.size();
        hdr += (char)((n >> 21) & 0x7F); hdr += (char)((n >> 14) & 0x7F); hdr += (char)((n >> 7) & 0x7F); hdr += (char)(n & 0x7F);
        std::ofstream(dir / "musica" / "tkey.mp3", std::ios::binary) << hdr << body << std::string(256, (char)0xFF);
        const TagInfo ti = readTags((dir / "musica" / "tkey.mp3").wstring());
        CHECK(ti.key == parseKey("11B") && ti.key >= 0, "TKEY: %d", ti.key);
    }
    std::vector<int16_t> silence(44100 * 2 * 2, 0);
    const fs::path w1 = dir / "musica" / "uno.wav", w2 = dir / "musica" / "dos.wav", w3 = dir / "musica" / "tres.wav", w4 = dir / "musica" / "cuatro.wav";
    writeWav(w1, 44100, silence); writeWav(w2, 44100, silence); writeWav(w3, 44100, silence); writeWav(w4, 44100, silence);
    auto xmlPath = [](const fs::path& p) { std::string u = toUtf8(p.wstring()); for (auto& ch : u) if (ch == '/') ch = '\\'; return u; };
    {
        std::ofstream x(dir / "VirtualDJ" / "database.xml", std::ios::binary);
        x << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<VirtualDJ_Database Version=\"8.5\">\n"
          << " <Song FilePath=\"" << xmlPath(w1) << "\" FileSize=\"176444\">\n"
          << "  <Tags Author=\"&#209;and&#250;\" Title=\"Uno &amp; Dos\" Key=\"Am\" Bpm=\"0.468750\" Flag=\"1\" />\n"
          << "  <Infos SongLength=\"2.0\" />\n"
          << "  <Scan Version=\"801\" Bpm=\"0.468750\" AltBpm=\"\" Volume=\"1.0\" Key=\"G\" Flag=\"1\" />\n"
          << "  <Poi Pos=\"2.0475\" Type=\"beatgrid\" />\n"
          << "  <Poi Name=\"Drop\" Pos=\"32.5\" Num=\"1\" Type=\"cue\" />\n"
          << "  <Poi Name=\"Break\" Pos=\"64.25\" Num=\"3\" Type=\"cue\" />\n"
          << " </Song>\n"
          << " <Song FilePath=\"" << xmlPath(w2) << "\">\n"
          << "  <Tags Author=\"X\" Title=\"Dos\" />\n"
          << "  <Scan Bpm=\"0.5\" Key=\"F#m\" />\n"
          << " </Song>\n"
          << " <Song FilePath=\"" << xmlPath(w3) << "\"><Scan Bpm=\"0.4\" Key=\"10B\" /></Song>\n"
          << " <Song FilePath=\"" << xmlPath(w4) << "\"><Scan Bpm=\"0.4\" Key=\"3B\" /></Song>\n"
          << " <Song FilePath=\"C:\\no\\existe.mp3\"><Scan Bpm=\"0.5\" Key=\"1A\" /></Song>\n"
          << " <Song FilePath=\"Musica\\relativa.mp3\" />\n"
          << "</VirtualDJ_Database>\n";
    }
    { std::ofstream(dir / "VirtualDJ" / "Playlists" / "Fiesta.m3u", std::ios::binary) << "#EXTM3U\r\n" << toUtf8(w1.wstring()) << "\r\n" << toUtf8(w2.wstring()) << "\r\n"; }
    { std::ofstream(dir / "VirtualDJ" / "Playlists" / "Sub" / "Cierre.m3u8", std::ios::binary) << "#EXTM3U\n" << toUtf8(w3.wstring()) << "\n"; }
    std::vector<VdjSong> songs;
    std::string err;
    CHECK(parseVdjDatabase((dir / "VirtualDJ" / "database.xml").wstring(), songs, err) && songs.size() == 6, "database.xml: %zu pistas, %s", songs.size(), err.c_str());
    if (songs.size() == 6) {
        CHECK(songs[0].artist == "Ñandú" && songs[0].title == "Uno & Dos", "entidades XML: '%s' / '%s'", songs[0].artist.c_str(), songs[0].title.c_str());
        CHECK(songs[0].key == am, "la tonalidad de la etiqueta manda sobre la del análisis (%d)", songs[0].key);
        CHECK(std::fabs(songs[0].bpm - 128.0) < 1e-6 && std::fabs(songs[0].gridSec - 2.0475) < 1e-9, "BPM %.3f, rejilla %.4f", songs[0].bpm, songs[0].gridSec);
        CHECK(songs[0].cues.size() == 2 && songs[0].cues[0].first == 1 && songs[0].cues[1].first == 3, "cues");
        CHECK(songs[1].key == fsm && std::fabs(songs[1].bpm - 120) < 1e-6, "Scan: %d, %.2f", songs[1].key, songs[1].bpm);
        CHECK(songs[5].path.size() > 2 && songs[5].path[1] == L':', "ruta relativa completada con la unidad: %s", toUtf8(songs[5].path).c_str());
    }
    SetEnvironmentVariableW(L"RDJ_VDJ_DIR", (dir / "VirtualDJ").wstring().c_str());
    const auto dbs = findVdjDatabases();
    CHECK(!dbs.empty() && _wcsicmp(dbs[0].file.c_str(), (dir / "VirtualDJ" / "database.xml").wstring().c_str()) == 0, "findVdjDatabases (%zu)", dbs.size());
    {   // 0.10.2: búsqueda por ruta para usar la rejilla al cargar (sin distinguir mayúsculas ni barras).
        VdjLookup lk;
        VdjSong s;
        std::wstring up = w1.wstring();
        for (auto& ch : up) { ch = (wchar_t)std::towupper(ch); if (ch == L'\\') ch = L'/'; }
        CHECK(lk.find(up, s) && s.bpm > 0 && s.gridSec >= 0, "VdjLookup encuentra la pista con su rejilla (%.2f BPM, %.3f s)", s.bpm, s.gridSec);
        CHECK(!lk.find(w3.wstring(), s), "VdjLookup: sin rejilla de VirtualDJ no se usa");
        CHECK(!lk.find((dir / "musica" / "otra.wav").wstring(), s), "VdjLookup: pista que no está");
    }
    SetEnvironmentVariableW(L"RDJ_VDJ_DIR", nullptr);
    {
        Library lib;
        lib.load();
        lib.addFiles({w1.wstring(), w2.wstring()});
        TrackMeta m;
        CHECK(lib.metaFor(w1.wstring(), m), "w1 en la colección");
        m.hotCuesSec[0] = 5.0;                       // hot cue propio: no se pisa
        lib.updateMeta(w1.wstring(), m);
        lib.metaFor(w2.wstring(), m);
        m.bpm = 100; m.analyzed = true; m.gridSrc = 3; // rejilla ajustada a mano: se respeta
        lib.updateMeta(w2.wstring(), m);
        VdjImportOptions opt;
        std::atomic<float> prog{0};
        VdjImportReport r = importVirtualDj(lib, (dir / "VirtualDJ" / "database.xml").wstring(), opt, &prog);
        std::printf("  VirtualDJ: %zu pistas, %zu coinciden, %zu tonalidades, %zu rejillas, %zu cues, %zu listas\n", r.songs, r.matched, r.keys, r.grids, r.cues, r.playlists);
        CHECK(r.error.empty() && r.songs == 6 && r.matched == 2 && r.keys == 2 && r.grids == 1 && r.cues == 1 && r.playlists == 2 && prog.load() == 1.f, "informe de importación");
        lib.metaFor(w1.wstring(), m);
        const double bar = 4 * 60.0 / 128;
        CHECK(m.key == am && m.keySrc == 4 && std::fabs(m.bpm - 128) < 1e-9 && m.gridSrc == 4 && std::fabs(m.gridAnchorSec - std::fmod(2.0475, bar)) < 1e-9,
              "w1: tono %d (%d), %.2f BPM, rejilla %d @ %.4f", m.key, m.keySrc, m.bpm, m.gridSrc, m.gridAnchorSec);
        CHECK(m.hotCuesSec[0] == 5.0 && m.hotCuesSec[2] == 64.25 && m.importedCues == 4, "hot cues: %.2f %.2f máscara %d", m.hotCuesSec[0], m.hotCuesSec[2], (int)m.importedCues);
        CHECK(m.hotCueNames[2] == "Break" && m.hotCueNames[0].empty(), "nombres de cue de VirtualDJ: '%s' / '%s'", m.hotCueNames[2].c_str(), m.hotCueNames[0].c_str());
        // 0.7: valoración, nombres de cue y género se guardan aparte (track_extra).
        lib.setRating(w1.wstring(), 4);
        lib.setHotCueName(w1.wstring(), 0, "Intro");
        lib.metaFor(w2.wstring(), m);
        CHECK(m.key == fsm && m.keySrc == 4 && m.bpm == 100 && m.gridSrc == 3, "w2: tono %d, %.1f BPM, rejilla %d", m.key, m.bpm, m.gridSrc);
        CHECK(lib.findPlaylist("VDJ · Fiesta") >= 0 && lib.findPlaylist("VDJ · Cierre") >= 0, "listas importadas");
        if (int pl = lib.findPlaylist("VDJ · Fiesta"); pl >= 0) CHECK(lib.query("", pl).size() == 2, "lista Fiesta: %zu", lib.query("", pl).size());
        // Tonalidad a mano: una segunda importación no la toca; las listas no se duplican.
        m.key = c; m.keySrc = 5;
        lib.updateMeta(w2.wstring(), m);
        const size_t nLists = lib.playlists().size();
        opt.addMissing = true;
        r = importVirtualDj(lib, (dir / "VirtualDJ" / "database.xml").wstring(), opt);
        lib.metaFor(w2.wstring(), m);
        CHECK(m.key == c && m.keySrc == 5, "la tonalidad puesta a mano se respeta (%d)", m.key);
        CHECK(r.added == 1 && r.matched == 4 && r.missingFiles >= 1, "añadir las que faltan: %zu añadidas, %zu coinciden, %zu sin archivo", r.added, r.matched, r.missingFiles);
        CHECK(lib.playlists().size() == nLists, "reimportar no duplica listas (%zu -> %zu)", nLists, lib.playlists().size());
        if (LibTrack* lt = lib.find(w1.wstring())) { lt->genre = "Tech House"; lib.markDirty(); }  // como si viniera de la etiqueta
        lib.save();
    }
    {
        Library lib;
        lib.load();
        TrackMeta m;
        CHECK(lib.metaFor(w1.wstring(), m) && m.keySrc == 4 && m.gridSrc == 4 && m.importedCues == 4 && m.hotCuesSec[2] == 64.25, "persistencia: tono %d, rejilla %d, máscara %d",
              m.keySrc, m.gridSrc, (int)m.importedCues);
        CHECK(lib.metaFor(w2.wstring(), m) && m.keySrc == 5 && m.key == c, "persistencia de la tonalidad a mano");
        const LibTrack* lt = lib.find(w1.wstring());
        CHECK(lt && lt->rating == 4 && lt->genre == "Tech House" && lt->meta.hotCueNames[0] == "Intro" && lt->meta.hotCueNames[2] == "Break",
              "persistencia de valoración, género y nombres de cue (%d, '%s', '%s')", lt ? lt->rating : -1, lt ? lt->genre.c_str() : "", lt ? lt->meta.hotCueNames[0].c_str() : "");
        CHECK(lib.query("tech house", -1).size() == 1, "buscar por género");
    }
    // Al cargar: la rejilla de VirtualDJ se comprueba con el audio (otro decodificador = otro desfase).
    {
        const double bpm = 128, first = 0.30, beat = 60.0 / bpm, bar = 4 * beat;
        TrackPtr bt = makeBeatTrack(44100, bpm, first, 40);
        std::vector<int16_t> pcm(bt->pcm.size());
        for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = (int16_t)std::lround(std::clamp(bt->pcm[i], -1.f, 1.f) * 32767);
        CHECK(writeWav(dir / "vdjbeat.wav", 44100, pcm), "wav");
        TrackLoader ld;
        LoadJob j; j.deck = 0; j.path = (dir / "vdjbeat.wav").wstring();
        j.meta.bpm = bpm; j.meta.analyzed = true; j.meta.gridSrc = 4;
        j.meta.gridAnchorSec = std::fmod(first + bar - 0.022, bar);   // VirtualDJ: 22 ms antes
        j.meta.hotCuesSec[2] = 10.0; j.meta.importedCues = 4;
        j.meta.hotCuesSec[5] = 20.0;                                   // cue propio: no se mueve
        j.meta.key = am; j.meta.keyAnalyzed = true; j.meta.keySrc = 4;
        ld.request(j);
        LoadResult r;
        bool got = false;
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!got && std::chrono::steady_clock::now() < until) { if (ld.poll(r)) got = true; else std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
        CHECK(got && r.track, "carga con rejilla de VirtualDJ");
        if (got && r.track) {
            const TrackMeta& m = r.track->meta;
            double ph = std::fmod(m.gridAnchorSec - first, bar);
            if (ph > bar / 2) ph -= bar;
            if (ph < -bar / 2) ph += bar;
            std::printf("  rejilla de VirtualDJ en un WAV: %+.1f ms respecto a la de VirtualDJ (cue 3: %.4f s)\n", (ph + 0.022) * 1000, m.hotCuesSec[2]);
            // 0.10.2: un WAV no tiene desfase de decodificador: la rejilla de VirtualDJ se respeta tal cual
            // (antes se «corregía» con nuestra medida, que con música real puede equivocarse).
            CHECK(std::fabs(ph + 0.022) < 0.0005, "WAV: la rejilla de VirtualDJ no se mueve (%.1f ms)", ph * 1000);
            CHECK(std::fabs(m.hotCuesSec[2] - 10.0) < 0.0005 && m.hotCuesSec[5] == 20.0, "cues: importado %.4f, propio %.4f", m.hotCuesSec[2], m.hotCuesSec[5]);
            CHECK(m.gridSrc == 7 && m.importedCues == 0 && r.analyzedNow && m.key == am && m.keySrc == 4, "estado tras cargar: rejilla %d, máscara %d", m.gridSrc, (int)m.importedCues);
        }
    }
    // 0.10.2: desfases de decodificador permitidos.
    {
        const std::vector<double> c{0.0, 0.02506, -0.02506};
        CHECK(chooseDecoderOffset(0.0249, c) == 0.02506 && chooseDecoderOffset(-0.0262, c) == -0.02506, "desfase de MP3 confirmado por la medida");
        CHECK(chooseDecoderOffset(0.011, c) == 0.0 && chooseDecoderOffset(0.031, c) == 0.0 && chooseDecoderOffset(-0.07, c) == 0.0, "medidas dudosas: se confía en VirtualDJ");
        const auto w = decoderOffsetCandidates((dir / "vdjbeat.wav").wstring(), 44100);
        CHECK(w.size() == 1 && w[0] == 0.0, "WAV: sin desfase de decodificador");
    }
    // 0.10.2: rejilla de VirtualDJ leída al cargar (sin importar): manda sobre la automática, no sobre la manual.
    {
        const double bpm = 128, first = 0.30, beat = 60.0 / bpm, bar = 4 * beat;
        const double vdjFirst = first + 0.006;   // VirtualDJ marca el golpe 6 ms después que nosotros: se respeta
        auto load = [&](const TrackMeta& meta, bool useExt, LoadResult& r) {
            TrackLoader ld;
            int calls = 0;
            ld.externalGrid = [&](const std::wstring& p, ExternalGrid& g) {
                ++calls;
                if (p != (dir / "vdjbeat.wav").wstring()) return false;
                g.bpm = bpm; g.gridSec = vdjFirst + 8 * bar;
                return true;
            };
            LoadJob j; j.deck = 0; j.path = (dir / "vdjbeat.wav").wstring(); j.meta = meta; j.useExternalGrid = useExt;
            ld.request(j);
            bool got = false;
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (!got && std::chrono::steady_clock::now() < until) { if (ld.poll(r)) got = true; else std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
            return got && r.track;
        };
        auto phase = [&](double a, double ref) { double x = std::fmod(a - ref, bar); if (x > bar / 2) x -= bar; if (x < -bar / 2) x += bar; return x; };
        TrackMeta own; own.bpm = 127.5; own.analyzed = true; own.gridSrc = 6; own.gridAnchorSec = first + beat;   // automática mala (BPM y compás)
        own.key = am; own.keyAnalyzed = true; own.keySrc = 2;
        LoadResult r;
        if (CHECKV(load(own, true, r), "carga con rejilla de VirtualDJ al vuelo")) {
            const TrackMeta& m = r.track->meta;
            CHECK(m.bpm == bpm && std::fabs(phase(m.gridAnchorSec, vdjFirst)) < 0.0005 && m.gridSrc == 8 && r.analyzedNow,
                  "se usan el BPM y la rejilla de VirtualDJ (%.2f BPM, %+.1f ms, origen %d)", m.bpm, phase(m.gridAnchorSec, vdjFirst) * 1000, m.gridSrc);
            LoadResult r2;
            if (CHECKV(load(m, true, r2), "segunda carga")) CHECK(!r2.analyzedNow && r2.track->meta.gridAnchorSec == m.gridAnchorSec, "la segunda carga no vuelve a medir nada");
        }
        TrackMeta manual = own; manual.gridSrc = 3;
        if (CHECKV(load(manual, true, r), "carga con rejilla a mano")) CHECK(r.track->meta.bpm == 127.5 && r.track->meta.gridSrc == 3, "la rejilla ajustada a mano no se toca");
        if (CHECKV(load(own, false, r), "carga con la opción apagada")) CHECK(r.track->meta.gridSrc != 8, "con la opción apagada no se usa VirtualDJ");
    }
    // 0.10.2: MP3 de verdad (codificado con LAME, con su retardo en la cabecera). Si VirtualDJ no quitara
    // ese retardo, su rejilla iría D muestras más tarde: se corrige exactamente D. Si lo quita, nada.
    if (const char* mp3 = std::getenv("RDJ_TEST_MP3")) {
        const std::wstring mp = fromUtf8(mp3);
        const double bpm = 128, first = 0.30, bar = 4 * 60.0 / bpm;
        const auto cand = decoderOffsetCandidates(mp, 44100);
        const double D = cand.size() > 1 ? std::fabs(cand[1]) : 0;
        std::printf("  MP3 de prueba: retardo del codificador %.0f muestras (%.1f ms)\n", D * 44100, D * 1000);
        CHECK(cand.size() == 3 && D > 0.01 && D < 0.04, "MP3: retardo LAME leído (%zu candidatos)", cand.size());
        for (double vdjShift : {D, 0.0}) {
            TrackLoader ld;
            ld.externalGrid = [&](const std::wstring&, ExternalGrid& g) { g.bpm = bpm; g.gridSec = first + vdjShift; return true; };
            LoadJob j; j.deck = 0; j.path = mp; j.meta.analyzed = false;
            ld.request(j);
            LoadResult r;
            bool got = false;
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (!got && std::chrono::steady_clock::now() < until) { if (ld.poll(r)) got = true; else std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
            if (!CHECKV(got && r.track, "carga del MP3")) continue;
            double ph = std::fmod(r.track->meta.gridAnchorSec - first, bar);
            if (ph > bar / 2) ph -= bar;
            if (ph < -bar / 2) ph += bar;
            std::printf("  MP3, rejilla de VirtualDJ desplazada %.1f ms: queda a %+.2f ms del golpe\n", vdjShift * 1000, ph * 1000);
            CHECK(std::fabs(ph) < 0.0015, "MP3: la rejilla queda en el golpe (%+.2f ms, VirtualDJ %+.1f ms)", ph * 1000, vdjShift * 1000);
        }
    }
    // 0.10: una rejilla automática de antes (fase 12 ms tarde y el «1» en el pulso 2) se vuelve a
    // medir al cargar; los cues encajados en la rejilla se mueven con ella, los demás no.
    {
        const double bpm = 128, first = 0.30, beat = 60.0 / bpm, bar = 4 * beat;
        TrackLoader ld;
        LoadJob j; j.deck = 0; j.path = (dir / "vdjbeat.wav").wstring();
        j.meta.bpm = bpm; j.meta.analyzed = true; j.meta.gridSrc = 2;
        const double oldAnchor = std::fmod(first + beat + 0.012, bar);
        j.meta.gridAnchorSec = oldAnchor;
        j.meta.cueSec = oldAnchor + 8 * beat;                 // encajado en la rejilla vieja
        j.meta.hotCuesSec[0] = oldAnchor + 32 * beat;         // encajado
        j.meta.hotCuesSec[1] = 15.1234;                       // puesto a mano fuera de la rejilla
        j.meta.key = am; j.meta.keyAnalyzed = true; j.meta.keySrc = 4;
        ld.request(j);
        LoadResult r;
        bool got = false;
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!got && std::chrono::steady_clock::now() < until) { if (ld.poll(r)) got = true; else std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
        CHECK(got && r.track, "carga con rejilla automática antigua");
        if (got && r.track) {
            const TrackMeta& m = r.track->meta;
            double ph = std::fmod(m.gridAnchorSec - first, bar);
            if (ph > bar / 2) ph -= bar;
            if (ph < -bar / 2) ph += bar;
            std::printf("  rejilla antigua afinada: compás y fase %+.1f ms; cue %.4f s, hot cue 1 %.4f s\n", ph * 1000, m.cueSec, m.hotCuesSec[0]);
            CHECK(std::fabs(ph) < 0.002, "rejilla antigua: queda a %.1f ms del primer tiempo de compás real", ph * 1000);
            CHECK(std::fabs(m.cueSec - (oldAnchor + 8 * beat - 0.012)) < 0.002 && std::fabs(m.hotCuesSec[0] - (oldAnchor + 32 * beat - 0.012)) < 0.002,
                  "los cues encajados se mueven con la rejilla");
            CHECK(m.hotCuesSec[1] == 15.1234 && m.gridSrc == 6 && r.analyzedNow, "cue a mano intacto y rejilla marcada como nueva (%d)", m.gridSrc);
        }
    }
    SetEnvironmentVariableW(L"RDJ_DATA_DIR", oldData[0] ? oldData : nullptr);
}

// Navegador tipo VirtualDJ: lectura de carpetas sueltas y carátulas incrustadas.
static std::string be32s(uint32_t v) { std::string r; r += (char)(v >> 24); r += (char)(v >> 16); r += (char)(v >> 8); r += (char)v; return r; }
static std::string le32s(uint32_t v) { std::string r; r += (char)v; r += (char)(v >> 8); r += (char)(v >> 16); r += (char)(v >> 24); return r; }
static std::string flacPictureBlock(const std::string& img) {
    return be32s(3) + be32s(10) + "image/jpeg" + be32s(0) + be32s(1) + be32s(1) + be32s(24) + be32s(0) + be32s((uint32_t)img.size()) + img;
}
static std::string b64(const std::string& in) {
    static const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const uint32_t v = ((uint8_t)in[i] << 16) | ((uint8_t)in[i + 1] << 8) | (uint8_t)in[i + 2];
        o += a[v >> 18]; o += a[(v >> 12) & 63]; o += a[(v >> 6) & 63]; o += a[v & 63];
    }
    if (i + 1 == in.size()) { const uint32_t v = (uint8_t)in[i] << 16; o += a[v >> 18]; o += a[(v >> 12) & 63]; o += "=="; }
    else if (i + 2 == in.size()) { const uint32_t v = ((uint8_t)in[i] << 16) | ((uint8_t)in[i + 1] << 8); o += a[v >> 18]; o += a[(v >> 12) & 63]; o += a[(v >> 6) & 63]; o += '='; }
    return o;
}
// Página Ogg (sin CRC: el lector no lo comprueba). segs = tamaños de los segmentos.
static std::string oggPage(const std::string& body, const std::vector<int>& segs, int seq) {
    std::string h = "OggS";
    h += (char)0; h += (char)(seq == 0 ? 2 : 0);
    h += std::string(8, '\0');
    h += le32s(1234) + le32s(seq) + le32s(0);
    h += (char)segs.size();
    for (int l : segs) h += (char)l;
    return h + body;
}

static void testBrowserAndCovers() {
    std::printf("[navegador: carpetas y carátulas]\n");
    const fs::path dir = tmpDir() / "navegador";
    fs::remove_all(dir);
    fs::create_directories(dir / "sub");
    std::vector<int16_t> sm(4800 * 2, 0);
    writeWav(dir / "a.wav", 48000, sm);
    writeWav(dir / "b.wav", 48000, sm);
    writeWav(dir / "sub" / "c.wav", 48000, sm);
    { std::ofstream(dir / "notas.txt") << "x"; }
    {
        Library lib;
        lib.browseFolder(dir.wstring(), false);
        for (int i = 0; i < 300 && lib.browsing(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CHECK(!lib.browsing(), "la lectura de la carpeta termina");
        CHECK(lib.inFolder(dir.wstring(), false).size() == 2, "carpeta sin subcarpetas: %zu pistas", lib.inFolder(dir.wstring(), false).size());
        CHECK(lib.inFolder((dir / "sub").wstring(), false).empty(), "la subcarpeta aún no se ha leído");
        lib.browseFolder(dir.wstring() + L"\\", true);
        for (int i = 0; i < 300 && lib.browsing(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CHECK(lib.inFolder(dir.wstring(), true).size() == 3, "carpeta con subcarpetas: %zu pistas", lib.inFolder(dir.wstring(), true).size());
        CHECK(lib.inFolder(dir.wstring(), false).size() == 2, "sin subcarpetas sigue en 2");
        CHECK(lib.tracks().size() == 3, "las pistas leídas entran en la colección (%zu)", lib.tracks().size());
        const int fav = lib.createPlaylist("Favoritos");
        CHECK(lib.findPlaylist("Favoritos") == fav && lib.findPlaylist("No existe") == -1, "buscar lista por nombre");
    }
    // Carátulas
    const std::string img = std::string("\xFF\xD8\xFF\xE0", 4) + "imagen-de-prueba" + std::string(40, 'x');
    {
        // ID3v2.3 con APIC (portada, descripción vacía)
        std::string apic;
        apic += (char)0;                       // latin-1
        apic += "image/jpeg"; apic += '\0';
        apic += (char)3;                       // portada
        apic += '\0';                          // descripción
        apic += img;
        std::string body = "APIC" + be32s((uint32_t)apic.size()) + std::string(2, '\0') + apic;
        std::string hdr = "ID3";
        hdr += (char)3; hdr += (char)0; hdr += (char)0;
        const uint32_t n = (uint32_t)body.size();
        hdr += (char)((n >> 21) & 0x7F); hdr += (char)((n >> 14) & 0x7F); hdr += (char)((n >> 7) & 0x7F); hdr += (char)(n & 0x7F);
        std::ofstream(dir / "portada.mp3", std::ios::binary) << hdr << body << std::string(256, (char)0xFF);
        auto c = readCoverArt((dir / "portada.mp3").wstring());
        CHECK(std::string(c.begin(), c.end()) == img, "carátula ID3 APIC (%zu bytes)", c.size());
    }
    {
        // FLAC con bloque PICTURE (último bloque)
        const std::string pic = flacPictureBlock(img);
        std::string f = "fLaC";
        f += (char)(0x80 | 6);
        f += (char)(pic.size() >> 16); f += (char)(pic.size() >> 8); f += (char)pic.size();
        std::ofstream(dir / "portada.flac", std::ios::binary) << f << pic;
        auto c = readCoverArt((dir / "portada.flac").wstring());
        CHECK(std::string(c.begin(), c.end()) == img, "carátula FLAC PICTURE (%zu bytes)", c.size());
    }
    {
        // OGG: METADATA_BLOCK_PICTURE en el paquete de comentarios, repartido en dos páginas
        const std::string ident = std::string("\x01vorbis", 7) + std::string(23, '\0');
        const std::string big = img + std::string(700, 'y');  // para que el paquete ocupe varias páginas
        const std::string kv = "METADATA_BLOCK_PICTURE=" + b64(flacPictureBlock(big));
        const std::string comments = std::string("\x03vorbis", 7) + le32s(4) + "test" + le32s(1) + le32s((uint32_t)kv.size()) + kv + std::string(1, '\x01');
        const size_t cut = 255;  // primer trozo: un segmento completo de 255 (el paquete continúa)
        std::vector<int> seg2;
        size_t rest = comments.size() - cut;
        while (rest >= 255) { seg2.push_back(255); rest -= 255; }
        seg2.push_back((int)rest);
        std::string ogg = oggPage(ident, {(int)ident.size()}, 0) + oggPage(comments.substr(0, cut), {255}, 1) + oggPage(comments.substr(cut), seg2, 2);
        std::ofstream(dir / "portada.ogg", std::ios::binary) << ogg;
        auto c = readCoverArt((dir / "portada.ogg").wstring());
        CHECK(std::string(c.begin(), c.end()) == big, "carátula OGG en varias páginas (%zu bytes)", c.size());
    }
    {
        // Sin carátula incrustada: cover.jpg de la carpeta
        std::ofstream(dir / "sub" / "cover.jpg", std::ios::binary) << img;
        auto c = readCoverArt((dir / "sub" / "c.wav").wstring());
        CHECK(std::string(c.begin(), c.end()) == img, "carátula de la carpeta (cover.jpg)");
        auto none = readCoverArt((dir / "a.wav").wstring());
        CHECK(none.empty(), "sin carátula: vacío");
    }
}

// 0.8: mapeos de mandos de Mixxx incluidos: todos se leen y los nombres de puerto reales
// se reconocen (los archivos se toman de res/mandos con RDJ_MANDOS_DIR).
static void testMandos() {
    std::printf("\n[mandos]\n");
    std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path().parent_path() / "res" / "mandos";
    std::wstring w = dir.wstring();
    if (!w.empty() && w[0] == L'/') w = L"Z:" + w;   // compilado en Linux y ejecutado con Wine
    for (auto& c : w) if (c == L'/') c = L'\\';
    SetEnvironmentVariableW(L"RDJ_MANDOS_DIR", w.c_str());
    const auto& cat = rdj::mandos::catalog();
    CHECK(cat.size() >= 140, "catálogo de mandos: %zu mapeos", cat.size());
    int bad = 0, inputs = 0, outputs = 0;
    for (auto& e : cat) {
        rdj::mandos::Mapping m;
        std::string err;
        if (!rdj::mandos::loadMapping(e.file, m, &err)) { ++bad; std::printf("  no se lee %s: %s\n", e.file.c_str(), err.c_str()); continue; }
        inputs += (int)m.inputs.size();
        outputs += (int)m.outputs.size();
        for (auto& s : m.scripts) {
            std::string code;
            if (!rdj::mandos::readFile(s.file, code)) { ++bad; std::printf("  falta %s (de %s)\n", s.file.c_str(), e.file.c_str()); }
        }
    }
    CHECK(bad == 0, "todos los mapeos y sus scripts se leen (%d entradas, %d luces)", inputs, outputs);
    const std::pair<const char*, const char*> ports[] = {
        {"DDJ-200", "Pioneer DDJ-200.midi.xml"}, {"DDJ-400", "Pioneer-DDJ-400.midi.xml"}, {"DDJ-FLX4", "Pioneer-DDJ-FLX4.midi.xml"},
        {"MIDIIN2 (DDJ-FLX4)", "Pioneer-DDJ-FLX4.midi.xml"}, {"2- DDJ-400", "Pioneer-DDJ-400.midi.xml"}, {"PIONEER DDJ-SB3", "Pioneer-DDJ-SB3.midi.xml"},
        {"DDJ-SB2", "Pioneer-DDJ-SB2.midi.xml"}, {"DJControl Inpulse 200", "Hercules_DJControl_Inpulse_200.midi.xml"},
        {"DJControl Inpulse 500", "Hercules_DJControl_Inpulse_500.midi.xml"}, {"Mixtrack Platinum FX", "Numark Mixtrack Platinum FX.midi.xml"},
        {"Mixtrack Pro 3", "Numark-Mixtrack-3.midi.xml"}, {"DENON DJ MC7000", "Denon-MC7000.midi.xml"}, {"DJ-505", "Roland_DJ-505.midi.xml"},
        {"XONE:K2", "Allen and Heath Xone K2.midi.xml"}, {"Party Mix", "Numark-Party-Mix.midi.xml"}};
    for (auto& [port, file] : ports) {
        const std::string got = rdj::mandos::bestMatch(port);
        CHECK(got == file, "puerto «%s» → %s", port, got.empty() ? "ninguno" : got.c_str());
    }
    for (const char* port : {"DDJ-WeGO", "USB MIDI Device", "loopMIDI Port", "Microsoft GS Wavetable Synth", "Keystation 49"})
        CHECK(rdj::mandos::bestMatch(port).empty(), "puerto «%s» no se confunde con otro mando", port);
}

#include "rack_tests.inc"

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    setvbuf(stdout, nullptr, _IONBF, 0);  // que el registro llegue entero aunque algo se cuelgue
    // Datos aislados: las pruebas no tocan la configuración real del usuario.
    SetEnvironmentVariableW(L"RDJ_DATA_DIR", (tmpDir() / "datos").wstring().c_str());
    bool real = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--real-device") real = true;
        if (a == "--rack") { testRack(); std::printf("\n%d correctas, %d fallos\n", g_pass, g_fail); return g_fail ? 1 : 0; }
        if (a == "--mandos") { testMandos(); std::printf("\n%d correctas, %d fallos\n", g_pass, g_fail); return g_fail ? 1 : 0; }
        if (a == "--sync") { testSync(); testSyncLock(); std::printf("\n%d correctas, %d fallos\n", g_pass, g_fail); return g_fail ? 1 : 0; }
        if (a == "--tonalidad") {  // solo las pruebas de tonalidad e importación de VirtualDJ
            testKeysAndVdj();
            std::printf("\n%d correctas, %d fallos\n", g_pass, g_fail);
            return g_fail ? 1 : 0;
        }
        if (a == "--bpm-file" && i + 1 < argc) {
            std::string err;
            ++i;
            int wargc = 0;
            wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
            const std::wstring p = (wargv && i < wargc) ? wargv[i] : std::wstring();
            TrackPtr t = decodeFile(p, err);
            if (!t) { std::printf("error: %s\n", err.c_str()); return 1; }
            BeatInfo bi = detectBeats(t->pcm.data(), t->frames, t->sampleRate);
            KeyInfo ki = detectKey(t->pcm.data(), t->frames, t->sampleRate);
            std::printf("%.3f %.4f %.2f %s\n", bi.bpm, bi.anchorSec, bi.confidence, keyName(ki.key));
            return 0;
        }
    }
    testDecode();
    testEq();
    testBpm();
    testEngineMix();
    testTransport();
    testSync();
    testSyncLock();
    testBpmPrecision();
    testRecording();
    testLoader();
    testKey();
    testKeyLock();
    testStemPlayer();
    testRouting();
    testEffects();
    testMidi();
    testLibrary();
    testKeysAndVdj();
    testBrowserAndCovers();
    testMandos();
    testRack();
    testDevices(false);
    if (real) testDevices(true);
    std::printf("\nResultado: %d comprobaciones correctas, %d fallos\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

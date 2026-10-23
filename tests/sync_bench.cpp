// Banco de pruebas del SYNC (no se instala): pistas realistas con BPM y compás conocidos.
// 1) Detección: error del BPM (deriva en la pista) y de la rejilla (ms y compás).
// 2) Motor: con la rejilla VERDADERA, desfase audible entre platos con SYNC
//    en distintas situaciones (arranque, pitch, empujones, saltos, key lock, loops…).
// Uso: rdj_sync_bench [--solo-deteccion | --solo-motor]
#include "audio/analysis.h"
#include "audio/engine.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace rdj;

namespace {

struct Spec {
    const char* name;
    double bpm, firstDownbeat;   // segundos del primer tiempo de compás
    int style;                   // 0 house, 1 techno con sub largo, 2 hip-hop con swing, 3 drum&bass (174), 4 house sin bombo al principio + corte, 5 percusión suave
    double seconds;
};

// Generador de pistas "de verdad" (con su rejilla conocida).
std::shared_ptr<Track> makeSong(const Spec& sp, uint32_t sr = 44100) {
    auto t = std::make_shared<Track>();
    t->sampleRate = sr;
    t->frames = (uint64_t)(sp.seconds * sr);
    t->pcm.assign(t->frames * 2, 0.f);
    std::mt19937 rng(1234 + sp.style);
    std::uniform_real_distribution<float> u(-1.f, 1.f);
    const double beat = 60.0 / sp.bpm;
    auto add = [&](double at, double dur, const std::function<float(double)>& f, float panL = 1.f, float panR = 1.f) {
        const int64_t s0 = (int64_t)std::llround(at * sr), n = (int64_t)(dur * sr);
        for (int64_t i = 0; i < n; ++i) {
            const int64_t k = s0 + i;
            if (k < 0 || k >= (int64_t)t->frames) continue;
            const float v = f((double)i / sr);
            t->pcm[k * 2] += v * panL; t->pcm[k * 2 + 1] += v * panR;
        }
    };
    std::vector<float> noise(sr);
    for (auto& v : noise) v = u(rng);
    auto kick = [&](double at, float amp, double tail) {
        add(at, tail, [=](double x) { return amp * (float)(std::sin(2 * kPi * (45 + 110 * std::exp(-x * 30)) * x) * std::exp(-x * (1.0 / tail) * 3)); });
    };
    auto clap = [&](double at, float amp) { add(at, 0.12, [&, amp](double x) { return amp * noise[(size_t)(x * sr * 7) % noise.size()] * (float)std::exp(-x * 30); }); };
    auto hat = [&](double at, float amp) { add(at, 0.04, [&, amp](double x) { return amp * noise[(size_t)(x * sr * 13 + 977) % noise.size()] * (float)std::exp(-x * 120); }, 0.7f, 1.f); };
    auto bass = [&](double at, double dur, double f, float amp) { add(at, dur, [=](double x) { return amp * (float)(std::sin(2 * kPi * f * x) * std::min(1.0, x * 200) * std::exp(-x * 2)); }); };
    auto crash = [&](double at, float amp) { add(at, 1.2, [&, amp](double x) { return amp * noise[(size_t)(x * sr * 3 + 4111) % noise.size()] * (float)std::exp(-x * 3); }); };
    auto pad = [&](double at, double dur, double f, float amp) {
        add(at, dur, [=](double x) { const double e = std::min(1.0, x / 0.4) * std::min(1.0, (dur - x) / 0.4); return amp * (float)(e * (std::sin(2 * kPi * f * x) + 0.5 * std::sin(2 * kPi * f * 1.5 * x))); });
    };
    const double total = sp.seconds;
    // Pulsos desde el primer compás; los anteriores (si los hay) también se tocan.
    const long long firstK = -(long long)std::floor(sp.firstDownbeat / beat);
    std::normal_distribution<double> jit(0.0, 0.002);   // humanización ±2 ms
    for (long long k = firstK; ; ++k) {
        const double tb = sp.firstDownbeat + k * beat;
        if (tb > total) break;
        const int bar = (int)std::floor((double)k / 4.0), inBar = (int)(((k % 4) + 4) % 4);
        const bool intro = sp.style == 4 && bar < 8;         // 8 compases sin bombo
        const bool brk = sp.style == 4 && bar >= 40 && bar < 48;  // corte de 8 compases
        switch (sp.style) {
        case 0: case 4: {
            if (!intro && !brk) kick(tb, 0.8f, 0.25);
            if (inBar == 0 && bar % 8 == 0) crash(tb, 0.18f);
            if (inBar == 1 || inBar == 3) clap(tb, 0.35f);
            hat(tb + beat * 0.5, 0.25f);
            if (!brk) bass(tb + beat * 0.5, beat * 0.45, 55.0, 0.35f);
            if (inBar == 0 && bar % 2 == 0) pad(tb, beat * 8, 220.0, 0.06f);
            break;
        }
        case 1:
            kick(tb, 0.85f, 0.45);   // sub largo que se monta con el siguiente
            for (int h = 0; h < 4; ++h) hat(tb + h * beat * 0.25, h == 2 ? 0.3f : 0.12f);
            if (inBar == 1 || inBar == 3) clap(tb + jit(rng), 0.2f);
            if (inBar == 0 && bar % 8 == 0) crash(tb, 0.15f);
            break;
        case 2: {   // 90 BPM con swing y caja en 2 y 4, bombo sincopado
            const double sw = beat * 0.5 * 1.32;  // corchea con swing
            if (inBar == 0 || (inBar == 2 && bar % 2 == 1)) kick(tb + jit(rng), 0.8f, 0.3);
            if (inBar == 2 && bar % 2 == 0) kick(tb + sw + jit(rng), 0.6f, 0.3);
            if (inBar == 1 || inBar == 3) clap(tb + jit(rng), 0.45f);
            hat(tb + jit(rng), 0.2f); hat(tb + sw + jit(rng), 0.14f);
            if (inBar == 0) bass(tb, beat * 1.5, 49.0, 0.3f);
            break;
        }
        case 3: {   // drum&bass 174: bombo en 1 y "y del 3", caja en 2 y 4
            if (inBar == 0) kick(tb, 0.8f, 0.2);
            if (inBar == 2) kick(tb + beat * 0.5, 0.7f, 0.2);
            if (inBar == 1 || inBar == 3) clap(tb, 0.55f);
            for (int h = 0; h < 2; ++h) hat(tb + h * beat * 0.5, 0.15f);
            if (inBar == 0) bass(tb, beat * 3.5, 41.0, 0.3f);
            break;
        }
        case 5:   // percusión suave (congas) y pad: sin bombo marcado
            add(tb, 0.15, [&](double x) { return 0.35f * (float)(std::sin(2 * kPi * 180 * x) * std::exp(-x * 25)); });
            if (inBar == 3) add(tb + beat * 0.5, 0.12, [&](double x) { return 0.25f * (float)(std::sin(2 * kPi * 260 * x) * std::exp(-x * 30)); });
            hat(tb + beat * 0.5, 0.1f);
            if (inBar == 0 && bar % 4 == 0) pad(tb, beat * 16, 196.0, 0.08f);
            break;
        }
    }
    float mx = 0;
    for (float v : t->pcm) mx = std::max(mx, std::fabs(v));
    if (mx > 0.95f) for (float& v : t->pcm) v *= 0.95f / mx;
    computeWaveform(t->pcm.data(), t->frames, sr, t->wave, t->maxPeak);
    return t;
}

const Spec kSpecs[] = {
    {"House 124,37", 124.37, 0.412, 0, 300},
    {"Techno 132 sub largo", 132.0, 0.090, 1, 300},
    {"Hip-hop 92 swing", 92.0, 0.731, 2, 240},
    {"DnB 174", 174.0, 0.2, 3, 240},
    {"House 126 intro sin bombo", 126.0, 0.25, 4, 330},
    {"Percusión suave 118,5", 118.5, 1.05, 5, 240},
    {"House 127,96", 127.96, 0.031, 0, 360},
    {"Techno 128 en 48 kHz", 128.0, 0.333, 1, 300},
    {"Hip-hop lento 86 (no doblar)", 86.0, 0.52, 2, 200},
};

double wrapBeat(double x) { x -= std::floor(x + 0.5); return x; }   // a [-0,5, 0,5)

void detection() {
    std::printf("== Detección de tempo y rejilla ==\n");
    std::printf("%-28s %9s %9s %9s %10s %9s %8s\n", "pista", "BPM real", "detect.", "deriva", "fase (ms)", "compás", "tiempo");
    int bad = 0;
    for (const Spec& sp : kSpecs) {
        const uint32_t sr = std::strstr(sp.name, "48 kHz") ? 48000 : 44100;
        auto t = makeSong(sp, sr);
        const auto t0 = std::chrono::steady_clock::now();
        // Rango de búsqueda como el de las preferencias por defecto (70–180).
        BeatInfo bi = detectBeats(t->pcm.data(), t->frames, sr, 70, 180);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const double ratio = bi.bpm / sp.bpm;
        const bool octave = std::fabs(ratio - 1) > 0.02;
        const double drift = octave ? 0 : std::fabs(bi.bpm - sp.bpm) / sp.bpm * sp.seconds * 1000.0;   // ms al final de la pista
        // Desfase de la rejilla detectada respecto a la verdadera, medido en mitad de la pista.
        const double beat = 60.0 / sp.bpm, mid = sp.seconds * 0.5;
        const double kTrue = (mid - sp.firstDownbeat) / beat;
        const double kDet = octave ? 0 : (mid - bi.anchorSec) / (60.0 / bi.bpm);
        const double phMs = octave ? 0 : wrapBeat(kDet - kTrue) * beat * 1000.0;
        // ¿El primer tiempo de compás cae en el compás verdadero?
        const double barOff = octave ? 0 : std::fmod(std::fmod((bi.anchorSec - sp.firstDownbeat) / beat, 4.0) + 4.0, 4.0);
        const bool barOk = !octave && (std::fabs(barOff) < 0.25 || std::fabs(barOff - 4) < 0.25);
        const bool ok = !octave && drift < 15 && std::fabs(phMs) < 15 && barOk;
        if (!ok) ++bad;
        std::printf("%-28s %9.3f %9.3f %7.1fms %+9.1f %9s %7.2fs %s\n", sp.name, sp.bpm, bi.bpm, drift, phMs,
                    octave ? "-" : barOk ? "bien" : (std::string("+") + std::to_string((int)std::lround(barOff)) + " pulsos").c_str(), secs, ok ? "" : "  <-- MAL");
    }
    std::printf("Pistas con problemas: %d de %zu\n\n", bad, sizeof(kSpecs) / sizeof(kSpecs[0]));
}

// ---------------------------------------------------------------- motor
struct Rig {
    Engine e;
    std::shared_ptr<Track> a, b;
    Spec sa, sb;
    void setup(const Spec& A, const Spec& B) {
        sa = A; sb = B;
        a = makeSong(A, 44100); b = makeSong(B, 44100);
        e.prepare(48000);
        Command c{Cmd::Load, 0}; c.track = a.get(); c.gen = 1; e.send(c);
        Command d{Cmd::Load, 1}; d.track = b.get(); d.gen = 1; e.send(d);
        float tmp[64]; e.process(tmp, 16, 2);
        // Rejilla VERDADERA: así se mide solo el motor.
        e.deck[0].bpm = A.bpm; e.deck[0].gridAnchorSec = std::fmod(A.firstDownbeat, 4 * 60.0 / A.bpm);
        e.deck[1].bpm = B.bpm; e.deck[1].gridAnchorSec = std::fmod(B.firstDownbeat, 4 * 60.0 / B.bpm);
        for (int k = 0; k < 2; ++k) { e.deck[k].fader = 1.f; e.deck[k].quantize = true; }
    }
    void cmd(int d, Cmd k, double v = 0, int i = 0) { Command c{k, (uint8_t)d}; c.d = v; c.i = i; e.send(c); }
    void run(double sec) {
        std::vector<float> out(480 * 2);
        for (int n = (int)(sec * 48000); n > 0; n -= 480) e.process(out.data(), std::min(480, n), 2);
    }
    // Desfase audible real (en pulsos de B y en ms) entre lo que suena en A y en B.
    double factor = 1.0;   // relación de tempo del SYNC: BPM de A × factor = BPM de B
    double phaseMs() {
        const double pa = e.deckStatus[0].posFrames.load() / a->sampleRate, pb = e.deckStatus[1].posFrames.load() / b->sampleRate;
        const double ka = (pa - sa.firstDownbeat) / (60.0 / sa.bpm) * factor, kb = (pb - sb.firstDownbeat) / (60.0 / sb.bpm);
        const double tempoB = 1.0 + e.deck[1].pitch.load();
        return wrapBeat(ka - kb) * 60.0 / (sb.bpm * tempoB) * 1000.0;
    }
};

struct Stat { double maxAbs = 0, sum = 0; int n = 0; void add(double v) { maxAbs = std::max(maxAbs, std::fabs(v)); sum += std::fabs(v); ++n; } double mean() const { return n ? sum / n : 0; } };

void engine() {
    std::printf("== Motor: desfase audible con SYNC (rejillas exactas) ==\n");
    auto scenario = [](const char* name, const Spec& A, const Spec& B, const std::function<void(Rig&, Stat&)>& body) {
        Rig r; r.setup(A, B);
        Stat st;
        body(r, st);
        std::printf("  %-58s medio %5.2f ms · máximo %6.2f ms %s\n", name, st.mean(), st.maxAbs, st.maxAbs > 5 ? " <-- se oye" : "");
    };
    auto sample = [](Rig& r, Stat& st, double secs, double every = 0.25, double settle = 0) {
        r.run(settle);
        for (double t = 0; t < secs; t += every) { r.run(every); st.add(r.phaseMs()); }
    };
    const Spec& H = kSpecs[0];   // 124,37
    const Spec& T = kSpecs[1];   // 132
    const Spec& H2 = kSpecs[6];  // 127,96
    scenario("A sigue a B (124,37 → 132), ambos sonando, 60 s", H, T, [&](Rig& r, Stat& st) {
        r.cmd(1, Cmd::Seek, 20.0 * 44100); r.cmd(0, Cmd::Seek, 31.3 * 44100);
        r.cmd(0, Cmd::Play); r.cmd(1, Cmd::Play);
        r.e.deck[0].syncTo = 1; r.cmd(0, Cmd::SyncPhase, 0, 1);
        sample(r, st, 60, 0.25, 0.5);
    });
    scenario("Con key lock en A", H, T, [&](Rig& r, Stat& st) {
        r.cmd(1, Cmd::Seek, 20.0 * 44100); r.cmd(0, Cmd::Seek, 31.3 * 44100);
        r.e.deck[0].keyLock = true;
        r.cmd(0, Cmd::Play); r.cmd(1, Cmd::Play);
        r.e.deck[0].syncTo = 1; r.cmd(0, Cmd::SyncPhase, 0, 1);
        sample(r, st, 40, 0.25, 0.5);
    });
    scenario("El maestro (B) mueve su pitch +4 % poco a poco", H2, T, [&](Rig& r, Stat& st) {
        r.cmd(1, Cmd::Seek, 20.0 * 44100); r.cmd(0, Cmd::Seek, 31.3 * 44100);
        r.cmd(0, Cmd::Play); r.cmd(1, Cmd::Play);
        r.e.deck[0].syncTo = 1; r.cmd(0, Cmd::SyncPhase, 0, 1);
        r.run(1.0);
        for (int i = 0; i <= 80; ++i) { r.e.deck[1].pitch = 0.0005f * i; r.run(0.1); st.add(r.phaseMs()); }
        sample(r, st, 10);
    });
    scenario("Empujones adelante y atrás en el maestro (±3 % 0,5 s)", H2, T, [&](Rig& r, Stat& st) {
        // Desde 0.10.1 el empujón del maestro no se copia en el otro: deja un desfase que se respeta.
        // Un empujón adelante y otro igual atrás tienen que dejar la fase como estaba.
        r.cmd(1, Cmd::Seek, 20.0 * 44100); r.cmd(0, Cmd::Seek, 31.3 * 44100);
        r.cmd(0, Cmd::Play); r.cmd(1, Cmd::Play);
        r.e.deck[0].syncTo = 1; r.cmd(0, Cmd::SyncPhase, 0, 1);
        r.run(1.0);
        for (int i = 0; i < 3; ++i) {
            r.e.deck[1].bend = 0.03f; r.run(0.5); r.e.deck[1].bend = 0; r.run(0.5);
            r.e.deck[1].bend = -0.03f; r.run(0.5); r.e.deck[1].bend = 0; sample(r, st, 2.0, 0.1);
        }
    });
    scenario("Hot cue / salto de 4 pulsos en el que sigue", H2, T, [&](Rig& r, Stat& st) {
        r.cmd(1, Cmd::Seek, 20.0 * 44100); r.cmd(0, Cmd::Seek, 31.3 * 44100);
        r.cmd(0, Cmd::Play); r.cmd(1, Cmd::Play);
        r.e.deck[0].syncTo = 1; r.cmd(0, Cmd::SyncPhase, 0, 1);
        r.run(1.0);
        for (int i = 0; i < 6; ++i) { r.cmd(0, Cmd::BeatJump, i % 2 ? -4.0 : 4.0); sample(r, st, 2.0, 0.05, 0.05); }
    });
    scenario("Salto del maestro (no cuantizado, 1,37 s)", H2, T, [&](Rig& r, Stat& st) {
        r.cmd(1, Cmd::Seek, 20.0 * 44100); r.cmd(0, Cmd::Seek, 31.3 * 44100);
        r.cmd(0, Cmd::Play); r.cmd(1, Cmd::Play);
        r.e.deck[0].syncTo = 1; r.cmd(0, Cmd::SyncPhase, 0, 1);
        r.run(1.0);
        for (int i = 0; i < 4; ++i) { r.cmd(1, Cmd::Seek, (30.0 + i * 7 + 1.37) * 44100); sample(r, st, 3.0, 0.05, 0.05); }
    });
    scenario("Loop de 1 pulso en el que sigue y salir", H2, T, [&](Rig& r, Stat& st) {
        r.cmd(1, Cmd::Seek, 20.0 * 44100); r.cmd(0, Cmd::Seek, 31.3 * 44100);
        r.cmd(0, Cmd::Play); r.cmd(1, Cmd::Play);
        r.e.deck[0].syncTo = 1; r.cmd(0, Cmd::SyncPhase, 0, 1);
        r.run(1.0);
        r.cmd(0, Cmd::AutoLoop, 1.0); sample(r, st, 4.0, 0.05, 0.05);
        r.cmd(0, Cmd::LoopExit); sample(r, st, 4.0, 0.05, 0.05);
    });
    scenario("Arrancar el que sigue con el maestro sonando (PLAY)", H2, T, [&](Rig& r, Stat& st) {
        r.cmd(1, Cmd::Seek, 20.0 * 44100); r.cmd(0, Cmd::Seek, 31.3 * 44100);
        r.cmd(1, Cmd::Play);
        r.e.deck[0].syncTo = 1; r.cmd(0, Cmd::SyncPhase, 0, 1);
        r.run(2.0);
        r.cmd(0, Cmd::SyncPhase, 0, 1); r.cmd(0, Cmd::Play);
        sample(r, st, 6.0, 0.05, 0.02);
    });
    scenario("Tempo doble: hip-hop 92 sigue a DnB 174 (×2)", kSpecs[2], kSpecs[3], [&](Rig& r, Stat& st) {
        r.cmd(1, Cmd::Seek, 20.0 * 44100); r.cmd(0, Cmd::Seek, 31.3 * 44100);
        r.cmd(0, Cmd::Play); r.cmd(1, Cmd::Play);
        r.factor = 2.0; r.e.deck[0].syncFactor = 2.0; r.e.deck[0].syncTo = 1; r.cmd(0, Cmd::SyncPhase, 2.0, 1);
        sample(r, st, 30, 0.25, 0.5);
    });
    scenario("Scratch de 1 s en el que sigue y soltar", H2, T, [&](Rig& r, Stat& st) {
        r.cmd(1, Cmd::Seek, 20.0 * 44100); r.cmd(0, Cmd::Seek, 31.3 * 44100);
        r.cmd(0, Cmd::Play); r.cmd(1, Cmd::Play);
        r.e.deck[0].syncTo = 1; r.cmd(0, Cmd::SyncPhase, 0, 1);
        r.run(1.0);
        r.e.deck[0].scratching = true; r.e.deck[0].scratchVelocity = -1.0f; r.run(0.5); r.e.deck[0].scratchVelocity = 1.6f; r.run(0.5);
        r.e.deck[0].scratching = false; r.e.deck[0].scratchVelocity = 0;
        sample(r, st, 4.0, 0.05, 0.05);
    });
    std::printf("\n");
}

} // namespace

int main(int argc, char** argv) {
    const bool onlyDet = argc > 1 && std::strcmp(argv[1], "--solo-deteccion") == 0;
    const bool onlyEng = argc > 1 && std::strcmp(argv[1], "--solo-motor") == 0;
    if (!onlyEng) detection();
    if (!onlyDet) engine();
    return 0;
}

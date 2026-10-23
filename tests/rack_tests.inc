// Pruebas del rack de hardware virtual (0.9): ruteo, cambios de parámetros, colas,
// loop station y continuidad del audio. Se incluye desde tests.cpp (usa sus utilidades).
#include <algorithm>
#include <limits>

namespace racktest {

constexpr double kFs = 48000;

// Motor con el plato A sonando solo (crossfader a la izquierda, volúmenes a 1).
static void setupA(Engine& e, Track* t, double bpm = 120) {
    e.prepare(kFs);
    loadDeck(e, 0, t);
    e.deck[0].fader = 1.f; e.deck[1].fader = 1.f;
    e.mixer.masterVol = 1.f;
    e.mixer.crossfader = 0.f;
    e.deck[0].bpm = bpm;
    e.deck[0].gridAnchorSec = 0.0;
}

static double lvl(const std::vector<float>& out, double t0, double t1, double freq) {
    const int a = (int)(t0 * kFs), b = (int)(t1 * kFs);
    return goertzel(out.data() + (size_t)a * 2, b - a, 2, freq, kFs);
}
static double dB(double v) { return 20 * std::log10(v + 1e-12); }
static float peakIn(const std::vector<float>& out, double t0, double t1) {
    float p = 0;
    for (size_t i = (size_t)(t0 * kFs) * 2; i < (size_t)(t1 * kFs) * 2 && i < out.size(); ++i) p = std::max(p, std::fabs(out[i]));
    return p;
}
static bool finite(const std::vector<float>& out) {
    for (float v : out) if (!std::isfinite(v)) return false;
    return true;
}
static float maxStep(const std::vector<float>& out, double t0, double t1) {
    float m = 0;
    for (size_t i = (size_t)(t0 * kFs) * 2 + 2; i < (size_t)(t1 * kFs) * 2 && i < out.size(); i += 2) m = std::max(m, std::fabs(out[i] - out[i - 2]));
    return m;
}
static void rackLoop(Engine& e, int c, int arg = 0) { Command q{Cmd::RackLoop, 0}; q.i = c; q.d = arg; e.send(q); }

} // namespace racktest

static void testRack() {
    using namespace racktest;
    std::printf("[rack: estación, aislador, pad XY y loop station]\n");
    auto tones = [](std::initializer_list<double> f, float amp) {
        std::vector<double> fs(f);
        return makeTrack(48000, 40, [fs, amp](double t, float& l, float& r) {
            double v = 0;
            for (double x : fs) v += std::sin(2 * kPi * x * t);
            l = r = (float)(v * amp);
        });
    };

    // 1) Sin activar nada, el rack no toca la señal (bit a bit), aunque haya efectos elegidos.
    {
        TrackPtr t = tones({220, 3000}, 0.2f);
        Engine ref, dut;
        setupA(ref, t.get()); setupA(dut, t.get());
        const int types[4] = {(int)StFx::Eco, (int)StFx::Reverb, (int)StFx::Filtro, (int)StFx::Gate};
        for (int k = 0; k < 4; ++k) { dut.rack.params.slot[k].type = types[k]; dut.rack.params.slot[k].amount = 0.9f; }
        dut.rack.params.stSrc = 0;
        dut.rack.params.isoSrc = 0;
        cmd(ref, 0, Cmd::Play); cmd(dut, 0, Cmd::Play);
        auto a = render(ref, 1.0), b = render(dut, 1.0);
        float diff = 0;
        for (size_t i = 0; i < a.size(); ++i) diff = std::max(diff, std::fabs(a[i] - b[i]));
        CHECK(diff == 0.f, "rack apagado altera la señal: %g", diff);
        CHECK(dut.rack.status.cpu.load() < 0.01f, "rack apagado gasta %.4f del tiempo", dut.rack.status.cpu.load());
    }

    // 2) Aislador: plano con todo a 0 dB; corte total de cada banda; recuperación suave; cruces en marcha.
    {
        TrackPtr t = tones({60, 1000, 8000}, 0.15f);
        Engine ref, e;
        setupA(ref, t.get()); setupA(e, t.get());
        cmd(ref, 0, Cmd::Play); cmd(e, 0, Cmd::Play);
        auto r0 = render(ref, 1.0);
        e.rack.params.isoSrc = 0;
        e.rack.params.isoOn = true;
        auto o = render(e, 1.0);
        const float basePk = peakIn(r0, 0.2, 1.0);
        CHECK(peakIn(o, 0.0, 0.05) < basePk * 1.15f, "aislador: salto al activarlo (%.3f frente a %.3f)", peakIn(o, 0.0, 0.05), basePk);
        for (double f : {60.0, 1000.0, 8000.0})
            CHECK(std::fabs(dB(lvl(o, 0.5, 1.0, f)) - dB(lvl(r0, 0.5, 1.0, f))) < 0.3, "aislador plano en %.0f Hz: %.2f dB", f, dB(lvl(o, 0.5, 1.0, f)) - dB(lvl(r0, 0.5, 1.0, f)));
        e.rack.params.isoGain[0] = 0.f;   // graves a cero con la ruleta
        o = render(e, 0.6);
        const double low = dB(lvl(o, 0.3, 0.6, 60)) - dB(lvl(r0, 0.5, 0.8, 60)), mid = dB(lvl(o, 0.3, 0.6, 1000)) - dB(lvl(r0, 0.5, 0.8, 1000));
        std::printf("  graves a cero: 60 Hz %.1f dB, 1 kHz %.2f dB\n", low, mid);
        CHECK(low < -35, "corte de graves insuficiente: %.1f dB", low);
        CHECK(std::fabs(mid) < 1.0, "el corte de graves toca los medios: %.2f dB", mid);
        e.rack.params.isoGain[0] = 0.5f;
        e.rack.params.isoKill[2] = true;  // botón de corte de agudos
        o = render(e, 0.4);
        const double hi = dB(lvl(o, 0.2, 0.4, 8000)) - dB(lvl(r0, 0.5, 0.7, 8000));
        CHECK(hi < -35, "corte de agudos insuficiente: %.1f dB", hi);
        e.rack.params.isoKill[2] = false; // recuperación
        o = render(e, 0.4);
        const double ref8 = lvl(r0, 0.5, 0.9, 8000);
        const double early = lvl(o, 0.0, 0.004, 8000) / ref8, late = lvl(o, 0.25, 0.4, 8000) / ref8;
        std::printf("  agudos al soltar el corte: %.0f %% a los 4 ms, %.0f %% a los 250 ms\n", early * 100, late * 100);
        CHECK(early < 0.5 && late > 0.97, "la banda no vuelve suave: %.2f / %.2f", early, late);
        CHECK(peakIn(o, 0, 0.4) < basePk * 1.1f, "sobresalto al recuperar: %.3f", peakIn(o, 0, 0.4));
        e.rack.params.isoLowHz = 450.f; e.rack.params.isoHighHz = 1500.f;   // cruces en marcha (dentro de rango)
        o = render(e, 0.5);
        CHECK(finite(o) && peakIn(o, 0, 0.5) < basePk * 1.15f, "cruces en marcha: pico %.3f", peakIn(o, 0, 0.5));
        e.rack.params.isoLowHz = 5.f; e.rack.params.isoHighHz = 90000.f;   // fuera de rango: se limita
        o = render(e, 0.3);
        CHECK(finite(o), "cruces fuera de rango");
        e.rack.params.isoOn = false;
        render(e, 0.1);
        CHECK(!e.rack.touches(1), "fuente del aislador");
    }

    // 3) Estación en inserción: filtro, mezcla y apagado.
    {
        TrackPtr t = tones({200, 5000}, 0.2f);
        Engine ref, e;
        setupA(ref, t.get()); setupA(e, t.get());
        cmd(ref, 0, Cmd::Play); cmd(e, 0, Cmd::Play);
        auto r0 = render(ref, 1.0);
        auto& sp = e.rack.params.slot[0];
        e.rack.params.stSrc = 0;
        sp.type = (int)StFx::Filtro; sp.amount = 0.15f; sp.mix = 1.f; sp.on = true;   // paso bajo a ≈ 400 Hz
        auto o = render(e, 0.8);
        const double hi = dB(lvl(o, 0.5, 0.8, 5000)) - dB(lvl(r0, 0.5, 0.8, 5000)), lo = dB(lvl(o, 0.5, 0.8, 200)) - dB(lvl(r0, 0.5, 0.8, 200));
        std::printf("  filtro paso bajo: 5 kHz %.1f dB, 200 Hz %.1f dB\n", hi, lo);
        CHECK(hi < -20 && lo > -3, "filtro de la estación: %.1f / %.1f dB", hi, lo);
        sp.mix = 0.f;
        o = render(e, 0.5);
        CHECK(std::fabs(dB(lvl(o, 0.2, 0.5, 5000)) - dB(lvl(r0, 0.5, 0.8, 5000))) < 0.5, "mezcla 0 = original");
        sp.mix = 1.f;
        render(e, 0.3);
        sp.on = false;
        o = render(e, 0.5);
        CHECK(std::fabs(dB(lvl(o, 0.2, 0.5, 5000)) - dB(lvl(r0, 0.5, 0.8, 5000))) < 0.3, "apagado = original");
        CHECK(!e.rack.status.slotActive[0].load(), "la ranura apagada sigue procesando");
        CHECK(maxStep(o, 0, 0.05) < 0.25f, "chasquido al apagar: %.3f", maxStep(o, 0, 0.05));
    }

    // 4) Eco sincronizado con el BPM (1/2 pulso) y su cola al apagar.
    {
        TrackPtr t = makeTrack(48000, 40, [](double tt, float& l, float& r) { l = r = (tt >= 0.1 && tt < 0.101) ? 0.5f * (float)std::sin(2 * kPi * 4000 * (tt - 0.1)) : 0.f; });
        for (double bpm : {120.0, 150.0}) {
            Engine e;
            setupA(e, t.get(), bpm);
            auto& sp = e.rack.params.slot[0];
            e.rack.params.stSrc = 0;
            sp.type = (int)StFx::Eco; sp.div = 4; sp.amount = 0.3f; sp.mix = 0.5f; sp.on = true;
            cmd(e, 0, Cmd::Play);
            auto o = render(e, 0.6);
            const double expect = 0.1 + 0.5 * 60.0 / bpm;
            size_t best = 0; float bv = 0;
            for (size_t i = (size_t)(0.15 * kFs); i < (size_t)(0.55 * kFs); ++i) if (std::fabs(o[i * 2]) > bv) { bv = std::fabs(o[i * 2]); best = i; }
            const double at = best / kFs;
            std::printf("  eco 1/2 pulso a %.0f BPM: primera repetición a %.3f s (esperada %.3f s)\n", bpm, at, expect);
            CHECK(std::fabs(at - expect) < 0.004 && bv > 0.1, "eco fuera de tiempo: %.4f s (%.3f)", at, bv);
            sp.on = false;   // apagar: la cola sigue
            o = render(e, 1.0);
            double tail = 0;
            for (size_t i = 0; i < o.size(); ++i) tail += o[i] * o[i];
            CHECK(tail > 1e-3, "el eco se corta al apagarlo (%.5f)", tail);
            render(e, 13.0);
            CHECK(!e.rack.status.slotActive[0].load(), "la cola no termina");
            o = render(e, 0.2);
            CHECK(peakIn(o, 0, 0.2) < 1e-6f, "queda señal tras la cola: %g", peakIn(o, 0, 0.2));
        }
    }

    // 5) Envío y retorno: el original no se duplica ni cambia de volumen; solo vuelve lo que añaden los efectos.
    {
        TrackPtr t = makeTrack(48000, 40, [](double tt, float& l, float& r) { l = r = (tt >= 0.1 && tt < 0.102) ? 0.5f * (float)std::sin(2 * kPi * 2000 * (tt - 0.1)) : 0.f; });
        Engine ref, e;
        setupA(ref, t.get()); setupA(e, t.get());
        e.rack.params.stSrc = 0; e.rack.params.stMode = 1; e.rack.params.stReturn = 1.f;
        auto& s0 = e.rack.params.slot[0];
        s0.type = (int)StFx::Filtro; s0.amount = 0.8f; s0.mix = 1.f; s0.on = true;   // solo color: no vuelve nada
        cmd(ref, 0, Cmd::Play); cmd(e, 0, Cmd::Play);
        auto a = render(ref, 0.5), b = render(e, 0.5);
        float diff = 0;
        for (size_t i = 0; i < a.size(); ++i) diff = std::max(diff, std::fabs(a[i] - b[i]));
        CHECK(diff < 1e-6f, "envío solo con filtro cambia la mezcla: %g", diff);
        auto& s1 = e.rack.params.slot[1];
        s1.type = (int)StFx::Reverb; s1.amount = 0.6f; s1.mix = 1.f; s1.on = true;
        cmd(ref, 0, Cmd::Seek, 0); cmd(e, 0, Cmd::Seek, 0);
        a = render(ref, 1.0); b = render(e, 1.0);
        float direct = 0; double extra = 0;
        for (size_t i = (size_t)(0.099 * kFs) * 2; i < (size_t)(0.103 * kFs) * 2; ++i) direct = std::max(direct, std::fabs(a[i] - b[i]));
        for (size_t i = (size_t)(0.2 * kFs) * 2; i < (size_t)(0.6 * kFs) * 2; ++i) extra += (b[i] - a[i]) * (b[i] - a[i]);
        std::printf("  envío con reverb: diferencia en el golpe directo %.4f, energía añadida %.4f\n", direct, extra);
        CHECK(direct < 0.02f, "el golpe directo cambia con el envío: %.4f", direct);
        CHECK(extra > 1e-4, "la reverb no vuelve por el retorno");
        CHECK(std::fabs(peakIn(b, 0.099, 0.103) - peakIn(a, 0.099, 0.103)) < 0.01f, "el volumen del original cambia con el envío");
    }

    // 6) Los nueve efectos: estables, cambian el audio, se apagan y dejan de procesar.
    {
        TrackPtr t = makeBeatTrack(48000, 124, 0.0, 40);
        for (int type = 1; type < (int)StFx::Count; ++type) {
            Engine ref, e;
            setupA(ref, t.get(), 124); setupA(e, t.get(), 124);
            auto& sp = e.rack.params.slot[0];
            e.rack.params.stSrc = 2;   // sobre la mezcla
            sp.type = type; sp.amount = 0.85f; sp.mix = 1.f; sp.div = 3; sp.on = true;
            cmd(ref, 0, Cmd::Play); cmd(e, 0, Cmd::Play);
            auto a = render(ref, 1.5), b = render(e, 1.5);
            double d = 0, en = 0;
            for (size_t i = 0; i < a.size(); ++i) { d += (a[i] - b[i]) * (a[i] - b[i]); en += a[i] * a[i]; }
            CHECK(finite(b) && e.rack.status.nonFinite.load() == 0, "%s: valores no finitos", stFxName(type));
            CHECK(d > en * 0.01, "%s no cambia el audio (%.4f de %.4f)", stFxName(type), d, en);
            sp.on = false;
            // Las colas terminan solas (12 s como mucho); el limitador del master necesita unos segundos para volver a 0 dB.
            const double settle = stFxHasTail(type) ? 14.0 : 3.0;
            b = render(e, settle); a = render(ref, settle);
            CHECK(!e.rack.status.slotActive[0].load(), "%s sigue procesando tras apagarlo", stFxName(type));
            a = render(ref, 0.3); b = render(e, 0.3);
            float diff = 0;
            for (size_t i = 0; i < a.size(); ++i) diff = std::max(diff, std::fabs(a[i] - b[i]));
            CHECK(diff < 1e-4f, "%s: tras apagarlo la señal no vuelve a ser la original (%g)", stFxName(type), diff);
            if (diff >= 1e-4f) {
                size_t at = 0; float m = 0;
                for (size_t i = 0; i < a.size(); ++i) if (std::fabs(a[i] - b[i]) > m) { m = std::fabs(a[i] - b[i]); at = i; }
                std::printf("    depuración: muestra %zu ref %.6f rack %.6f ratio %.6f limitador ref %.6f rack %.6f\n", at, a[at], b[at], b[at] / a[at],
                            ref.status.limiterGain.load(), e.status.limiterGain.load());
            }
        }
    }

    // 7) EFECTOS OFF: los efectos de color se van en milisegundos; las colas siguen sonando.
    {
        TrackPtr t = tones({5000}, 0.15f);
        Engine ref, e;
        setupA(ref, t.get()); setupA(e, t.get());
        e.rack.params.stSrc = 0;
        auto& s0 = e.rack.params.slot[0];
        s0.type = (int)StFx::Filtro; s0.amount = 0.05f; s0.mix = 1.f; s0.on = true;
        cmd(ref, 0, Cmd::Play); cmd(e, 0, Cmd::Play);
        auto r0 = render(ref, 0.6);
        auto o = render(e, 0.6);
        CHECK(dB(lvl(o, 0.4, 0.6, 5000)) - dB(lvl(r0, 0.4, 0.6, 5000)) < -15, "filtro antes de EFECTOS OFF");
        e.rack.params.fxCut = true;
        o = render(e, 0.4);
        const double after = dB(lvl(o, 0.1, 0.3, 5000)) - dB(lvl(r0, 0.4, 0.6, 5000));
        std::printf("  EFECTOS OFF: 5 kHz a %.2f dB del original tras 0,1 s\n", after);
        CHECK(std::fabs(after) < 0.5, "EFECTOS OFF no quita el filtro: %.2f dB", after);
        CHECK(maxStep(o, 0, 0.1) < 0.2f, "chasquido con EFECTOS OFF: %.3f", maxStep(o, 0, 0.1));
        CHECK(!e.rack.status.slotOnEff[0].load(), "EFECTOS OFF deja la ranura encendida");
    }
    {
        TrackPtr t = makeTrack(48000, 40, [](double tt, float& l, float& r) { l = r = (tt >= 0.1 && tt < 0.101) ? 0.5f * (float)std::sin(2 * kPi * 4000 * (tt - 0.1)) : 0.f; });
        Engine e;
        setupA(e, t.get());
        e.rack.params.stSrc = 0;
        auto& s0 = e.rack.params.slot[0];
        s0.type = (int)StFx::Eco; s0.div = 4; s0.amount = 0.6f; s0.mix = 0.5f; s0.on = true;
        cmd(e, 0, Cmd::Play);
        render(e, 0.2);
        e.rack.params.fxCut = true;   // tras el golpe: la cola del eco sigue
        auto o = render(e, 0.4);
        CHECK(peakIn(o, 0.1, 0.25) > 0.05f && e.rack.status.slotActive[0].load(), "EFECTOS OFF corta la cola del eco (%.3f)", peakIn(o, 0.1, 0.25));
        e.rack.params.fxCut = false;
    }

    // 8) Cambio de fuente en marcha: sin chasquidos.
    {
        TrackPtr t = tones({5000}, 0.2f);
        Engine e;
        setupA(e, t.get());
        e.rack.params.stSrc = 0;
        auto& s0 = e.rack.params.slot[0];
        s0.type = (int)StFx::Saturacion; s0.amount = 1.f; s0.mix = 1.f; s0.on = true;
        cmd(e, 0, Cmd::Play);
        auto o = render(e, 0.5);
        const float normal = maxStep(o, 0.2, 0.5);
        e.rack.params.stSrc = 1;
        o = render(e, 0.2);
        e.rack.params.stSrc = 2;
        auto o2 = render(e, 0.2);
        e.rack.params.stMode = 1;
        auto o3 = render(e, 0.2);
        std::printf("  cambio de fuente: salto máximo %.3f (normal %.3f)\n", std::max({maxStep(o, 0, 0.2), maxStep(o2, 0, 0.2), maxStep(o3, 0, 0.2)}), normal);
        CHECK(maxStep(o, 0, 0.2) < normal * 1.3f && maxStep(o2, 0, 0.2) < normal * 1.3f && maxStep(o3, 0, 0.2) < normal * 1.3f, "chasquido al cambiar de fuente o de modo");
    }

    // 9) Pad XY: tocar enciende y mueve el destino; al soltar vuelve suave; HOLD mantiene.
    {
        TrackPtr t = tones({200, 5000}, 0.2f);
        Engine ref, e;
        setupA(ref, t.get()); setupA(e, t.get());
        cmd(ref, 0, Cmd::Play); cmd(e, 0, Cmd::Play);
        auto r0 = render(ref, 0.5);
        const double base = dB(lvl(r0, 0.2, 0.5, 5000));
        auto& rp = e.rack.params;
        rp.stSrc = 0;
        rp.slot[0].type = (int)StFx::Filtro; rp.slot[0].amount = 0.5f; rp.slot[0].mix = 1.f; rp.slot[0].on = false;
        rp.xyTargetX = XY_Amount0; rp.xyTargetY = XY_None;
        rp.xyX = 0.05f; rp.xyY = 0.5f;
        rp.xyTouch = true;
        auto o = render(e, 0.3);
        const double touched = dB(lvl(o, 0.15, 0.3, 5000)) - base;
        CHECK(touched < -15 && e.rack.status.slotOnEff[0].load(), "pad XY: filtro al tocar %.1f dB", touched);
        rp.xyTouch = false;
        o = render(e, 0.9);
        const double soon = dB(lvl(o, 0.0, 0.03, 5000)) - base, later = dB(lvl(o, 0.6, 0.9, 5000)) - base;
        std::printf("  pad XY: al soltar %.1f dB a los 30 ms, %.2f dB a los 0,6 s\n", soon, later);
        CHECK(soon < -3 && std::fabs(later) < 0.5, "pad XY: la vuelta no es suave (%.1f / %.2f dB)", soon, later);
        rp.xyHold = true; rp.xyTouch = true;
        render(e, 0.2);
        rp.xyTouch = false;
        o = render(e, 1.0);
        CHECK(dB(lvl(o, 0.7, 1.0, 5000)) - base < -15, "HOLD no mantiene la posición");
        rp.xyHold = false;
        // Eje Y a la macro: el pad mueve la macro (y la macro, la ranura).
        rp.slot[0].macroAmt = -0.45f; rp.xyTargetY = XY_Macro; rp.xyTargetX = XY_None; rp.xyY = 1.f; rp.xyTouch = true;
        o = render(e, 0.4);
        CHECK(dB(lvl(o, 0.2, 0.4, 5000)) - base < -10, "pad XY → macro");
        rp.xyTouch = false; rp.slot[0].macroAmt = 0.f;
        render(e, 1.0);
    }

    // 10) Macro: mueve las ranuras asignadas y las enciende.
    {
        TrackPtr t = tones({200, 5000}, 0.2f);
        Engine ref, e;
        setupA(ref, t.get()); setupA(e, t.get());
        cmd(ref, 0, Cmd::Play); cmd(e, 0, Cmd::Play);
        auto r0 = render(ref, 0.5);
        auto& rp = e.rack.params;
        rp.stSrc = 0;
        rp.slot[0].type = (int)StFx::Filtro; rp.slot[0].amount = 0.5f; rp.slot[0].mix = 1.f; rp.slot[0].macroAmt = -0.45f;
        rp.macro = 1.f;
        auto o = render(e, 0.5);
        CHECK(dB(lvl(o, 0.3, 0.5, 5000)) - dB(lvl(r0, 0.2, 0.5, 5000)) < -15, "la macro no mueve el filtro");
        rp.macro = 0.f;
        o = render(e, 0.5);
        CHECK(std::fabs(dB(lvl(o, 0.3, 0.5, 5000)) - dB(lvl(r0, 0.2, 0.5, 5000))) < 0.5, "macro a cero = original");
    }

    // 11) Loop station: inicio y final cuantizados al compás, loop que sigue al cambiar de pista,
    //     sobregrabación, deshacer, seguir el BPM, borrar y límite de duración.
    {
        // Golpe de 3 kHz en cada compás (cada 2 s a 120 BPM) y de 1 kHz en los demás pulsos.
        TrackPtr t = makeTrack(48000, 60, [](double tt, float& l, float& r) {
            const double beat = 0.5;
            const double k = std::floor(tt / beat), dt = tt - k * beat;
            const bool bar = ((long long)k % 4) == 0;
            l = r = dt < 0.003 ? (float)((bar ? 0.3 : 0.15) * std::sin(2 * kPi * (bar ? 3000 : 1000) * dt)) : 0.f;
        });
        TrackPtr silent = makeTrack(48000, 20, [](double, float& l, float& r) { l = r = 0.f; });
        TrackPtr other = makeTrack(48000, 60, [](double tt, float& l, float& r) { l = r = (float)(0.1 * std::sin(2 * kPi * 700 * tt)); });
        Engine e;
        setupA(e, t.get());
        auto& rp = e.rack.params;
        auto& st = e.rack.status;
        rp.lpSrc = 0; rp.lpQuant = true; rp.lpAutoPlay = false; rp.lpVol = 1.f;
        e.rack.allocLoop();
        cmd(e, 0, Cmd::Seek, 0.3 * 48000);
        cmd(e, 0, Cmd::Play);
        render(e, 0.05);
        rackLoop(e, Rack::LoopRec, 1);
        double waited = 0;
        render(e, 0.02);
        CHECK(st.lpState.load() == (int)LoopState::Armado, "la grabación no espera al compás (%s)", loopStateName(st.lpState.load()));
        while (st.lpState.load() != (int)LoopState::Parado && waited < 8) { render(e, 0.05); waited += 0.05; }
        CHECK(st.lpState.load() == (int)LoopState::Parado, "la grabación no termina (%s)", loopStateName(st.lpState.load()));
        CHECK(e.rack.loopFrames() == 96000, "duración del loop: %lld muestras (esperadas 96000)", (long long)e.rack.loopFrames());
        CHECK(st.lpBars.load() == 1 && std::fabs(st.lpBpm.load() - 120) < 1e-6, "compases/BPM del loop");
        {
            const float* L = e.rack.loopL();
            int at = -1; float pk = 0;
            for (int i = 0; i < 4000; ++i) if (std::fabs(L[i]) > pk) { pk = std::fabs(L[i]); at = i; }
            float lateMax = 0;
            for (int i = 94000; i < 96000; ++i) lateMax = std::max(lateMax, std::fabs(L[i]));
            const double beat2 = goertzel(L + 24000, 200, 1, 1000, kFs), first = goertzel(L, 200, 1, 3000, kFs);
            std::printf("  loop: golpe del compás en la muestra %d (pico %.2f), pulso 2 a 1 kHz %.3f, final %.4f\n", at, pk, beat2, lateMax);
            CHECK(at >= 0 && at < 160 && pk > 0.2f, "el loop no empieza en el compás: muestra %d", at);
            CHECK(first > 0.05 && beat2 > 0.03, "contenido del loop desplazado");
            CHECK(lateMax < 0.01f, "el loop no termina antes del siguiente compás");
        }
        CHECK(!st.lpBusy.load(), "el loop sigue ocupado tras grabar");
        // Reproducir: entra en el siguiente compás, en fase con el plato.
        const double posCmd = e.deckStatus[0].posFrames.load() / 48000.0;
        rackLoop(e, Rack::LoopPlay);
        const double nextBar = std::ceil(posCmd / 2.0 + 1e-9) * 2.0;
        auto o = render(e, (nextBar - posCmd) + 0.5);
        CHECK(st.lpState.load() == (int)LoopState::Reproduciendo, "no suena tras ▶ (%s)", loopStateName(st.lpState.load()));
        const double barT = nextBar - posCmd;
        const float together = peakIn(o, barT - 0.002, barT + 0.006), alone = peakIn(o, barT + 0.4, barT + 0.45) ;
        std::printf("  ▶ en el compás: pico en el golpe %.3f (plato solo ≈ 0,3)\n", together);
        CHECK(together > 0.5f, "el loop no entra en fase con el compás del plato (%.3f)", together);
        (void)alone;
        // Cambiar la pista del plato: el loop sigue sonando.
        loadDeck(e, 0, silent.get(), 2);
        o = render(e, 4.0);
        CHECK(st.lpState.load() == (int)LoopState::Reproduciendo && peakIn(o, 0.5, 4.0) > 0.25f, "el loop se para al cambiar de pista");
        // Sobregrabar una capa (otra pista) y deshacerla.
        std::vector<float> before(e.rack.loopL(), e.rack.loopL() + e.rack.loopFrames());
        loadDeck(e, 0, other.get(), 3);
        e.deck[0].bpm = 120; e.deck[0].gridAnchorSec = 0.0;
        cmd(e, 0, Cmd::Play);
        render(e, 0.1);
        rackLoop(e, Rack::LoopDub);
        render(e, 2.6);
        CHECK(st.lpState.load() == (int)LoopState::Sobregrabando, "no sobregraba");
        rackLoop(e, Rack::LoopDub);
        render(e, 0.05);
        float changed = 0;
        for (size_t i = 0; i < before.size(); ++i) changed = std::max(changed, std::fabs(e.rack.loopL()[i] - before[i]));
        CHECK(changed > 0.05f && st.lpLayers.load() == 2 && st.lpCanUndo.load(), "sobregrabación: cambio %.3f, capas %d", changed, st.lpLayers.load());
        rackLoop(e, Rack::LoopUndo);
        render(e, 0.2);
        float rest = 0;
        for (size_t i = 0; i < before.size(); ++i) rest = std::max(rest, std::fabs(e.rack.loopL()[i] - before[i]));
        CHECK(rest < 1e-7f && st.lpLayers.load() == 1 && !st.lpCanUndo.load(), "deshacer no devuelve la capa anterior (%g)", rest);
        // Seguir el BPM del plato (+10 %).
        e.deck[0].pitch = 0.10f;
        render(e, 0.5);
        CHECK(std::fabs(st.lpRate.load() - 1.10f) < 0.005f, "seguir el BPM: velocidad %.4f", st.lpRate.load());
        rp.lpFollow = false;
        render(e, 0.5);
        CHECK(std::fabs(st.lpRate.load() - 1.f) < 0.005f, "sin seguir el BPM: velocidad %.4f", st.lpRate.load());
        e.deck[0].pitch = 0.f;
        // Parar y borrar.
        rackLoop(e, Rack::LoopStop);
        render(e, 0.05);
        CHECK(st.lpState.load() == (int)LoopState::Parado, "parar");
        rackLoop(e, Rack::LoopClear);
        cmd(e, 0, Cmd::Pause);
        o = render(e, 0.2);
        CHECK(st.lpState.load() == (int)LoopState::Vacio && peakIn(o, 0.05, 0.2) < 1e-6f, "borrar");
        // Demasiado largo: 8 compases a 50 BPM no caben en 30 s.
        e.deck[0].bpm = 50;
        cmd(e, 0, Cmd::Play);
        rackLoop(e, Rack::LoopRec, 8);
        render(e, 5.0);
        CHECK(st.lpTooLong.load() && st.lpState.load() == (int)LoopState::Vacio, "límite de duración (%s)", loopStateName(st.lpState.load()));
        // Sin cuantizar: empieza ya.
        e.deck[0].bpm = 120;
        rp.lpQuant = false;
        rackLoop(e, Rack::LoopRec, 1);
        render(e, 0.02);
        CHECK(st.lpState.load() == (int)LoopState::Grabando, "sin cuantizar debería grabar ya");
        rackLoop(e, Rack::LoopRec);   // pulsar otra vez mientras graba: se descarta
        render(e, 0.02);
        CHECK(st.lpState.load() == (int)LoopState::Vacio, "descartar la grabación en curso");
    }

    // 12) Continuidad con todo en marcha: cambios de parámetros aleatorios cada 10 ms durante 20 s
    //     (tipos, orden, fuentes, modo, XY, aislador, loop) y consumo.
    {
        TrackPtr a = makeBeatTrack(48000, 126, 0.0, 60), b = makeBeatTrack(48000, 126, 0.25, 60);
        Engine e;
        e.prepare(kFs);
        loadDeck(e, 0, a.get(), 1); loadDeck(e, 1, b.get(), 1);
        for (int d = 0; d < 2; ++d) { e.deck[d].bpm = 126; e.deck[d].fader = 1.f; }
        e.mixer.masterVol = 1.f;
        cmd(e, 0, Cmd::Play); cmd(e, 1, Cmd::Play);
        auto& rp = e.rack.params;
        const int types[4] = {(int)StFx::Reverb, (int)StFx::Delay, (int)StFx::Flanger, (int)StFx::Phaser};
        for (int k = 0; k < 4; ++k) { rp.slot[k].type = types[k]; rp.slot[k].on = true; rp.slot[k].amount = 0.7f; rp.slot[k].mix = 0.5f; }
        rp.isoOn = true; rp.isoSrc = 2;
        e.rack.allocLoop();
        rp.lpQuant = false; rp.lpAutoPlay = true;
        rackLoop(e, Rack::LoopRec, 2);
        // Consumo con los cuatro efectos, el aislador y el loop.
        const auto t0 = std::chrono::steady_clock::now();
        auto o = render(e, 10.0);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  todo encendido: 10 s de audio en %.3f s (%.1f %% de un núcleo); el rack usa %.1f %% del tiempo de cada bloque\n",
                    secs, secs / 10.0 * 100, e.rack.status.cpu.load() * 100);
        CHECK(finite(o), "valores no finitos con todo encendido");
        CHECK(secs < 3.0, "el rack es demasiado lento: %.2f s para 10 s", secs);
        CHECK(e.rack.status.lpState.load() == (int)LoopState::Reproduciendo, "loop en la prueba de carga");
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> u(0.f, 1.f);
        bool ok = true;
        for (int step = 0; step < 2000; ++step) {
            const int k = rng() % 4;
            switch (rng() % 12) {
            case 0: rp.slot[k].type = (int)(rng() % (int)StFx::Count); break;
            case 1: rp.slot[k].on = !rp.slot[k].on.load(); break;
            case 2: rp.slot[k].amount = u(rng); break;
            case 3: rp.slot[k].mix = u(rng); break;
            case 4: rp.slot[k].div = (int)(rng() % kStDivs); break;
            case 5: { uint32_t o2 = 0; int p[4] = {0, 1, 2, 3}; std::shuffle(p, p + 4, rng); for (int i = 0; i < 4; ++i) o2 |= p[i] << (i * 4); rp.stOrder = o2; break; }
            case 6: rp.stSrc = (int)(rng() % 3); break;
            case 7: rp.stMode = (int)(rng() % 2); break;
            case 8: rp.xyTouch = !rp.xyTouch.load(); rp.xyX = u(rng); rp.xyY = u(rng); rp.xyTargetX = (int)(rng() % XY_Count); rp.xyTargetY = (int)(rng() % XY_Count); break;
            case 9: rp.isoGain[rng() % 3] = u(rng); rp.isoKill[rng() % 3] = u(rng) < 0.3f; rp.isoLowHz = 50 + 500 * u(rng); rp.isoHighHz = 900 + 8000 * u(rng); break;
            case 10: rp.macro = u(rng); rp.slot[k].macroAmt = u(rng) * 2 - 1; rp.slot[k].macroMix = u(rng) * 2 - 1; break;
            default: rackLoop(e, (int)(rng() % 7), 1 + (int)(rng() % 4)); rp.lpSrc = (int)(rng() % 3); rp.fxCut = u(rng) < 0.1f; break;
            }
            auto chunk = render(e, 0.01);
            if (!finite(chunk)) { ok = false; break; }
        }
        CHECK(ok, "valores no finitos con cambios de parámetros aleatorios");
        std::printf("  2000 cambios aleatorios: bloques corregidos por valores no finitos o desbocados: %u\n", e.rack.status.nonFinite.load());
        // Parámetros no finitos: se sustituyen por valores seguros.
        rp.fxCut = false;
        rp.slot[0].type = (int)StFx::Saturacion; rp.slot[0].on = true;
        rp.slot[0].amount = std::numeric_limits<float>::quiet_NaN(); rp.slot[0].mix = std::numeric_limits<float>::infinity();
        rp.xyX = std::numeric_limits<float>::quiet_NaN(); rp.isoLowHz = std::numeric_limits<float>::quiet_NaN();
        o = render(e, 0.5);
        CHECK(finite(o), "parámetros no finitos llegan al audio");
    }

    // 13) MIDI: acciones del rack (tabla completa, ids únicos) y mandos con mapeo de Mixxx:
    //     lo asignado al rack se queda en el rack; lo demás sigue yendo al mapeo.
    {
        const auto& acts = midiActions();
        CHECK((int)acts.size() == MA_Count, "tabla de acciones MIDI: %zu de %d", acts.size(), (int)MA_Count);
        bool unique = true;
        for (size_t i = 0; i < acts.size(); ++i) for (size_t j = i + 1; j < acts.size(); ++j) if (std::string(acts[i].id) == acts[j].id) unique = false;
        CHECK(unique, "identificadores de acciones MIDI repetidos");
        MidiManager mm;
        mm.setIntegrated(0, true);
        MidiBinding b; b.action = MA_RackMacro; b.type = 0xB; b.channel = 0; b.data1 = 0x10;
        mm.bindings().push_back(b);
        mm.pushEvent(MidiEvent{0, 0xB0, 0x10, 64});
        mm.pushEvent(MidiEvent{0, 0xB0, 0x11, 10});
        auto cmds = mm.poll();
        auto rest = mm.takeIntegrated();
        CHECK(cmds.size() == 1 && cmds[0].action == MA_RackMacro && std::fabs(cmds[0].value - 64 / 127.f) < 1e-6f, "el control asignado al rack no llega al rack");
        CHECK(rest.size() == 1 && rest[0].data1 == 0x11, "lo no asignado debe seguir yendo al mapeo (%zu)", rest.size());
        // Aprender una acción del rack con un mando integrado.
        mm.startLearn(MA_RackXyX, 0);
        mm.pushEvent(MidiEvent{0, 0xB1, 0x22, 5});
        mm.poll();
        const MidiBinding* nb = mm.findBinding(MA_RackXyX, 0);
        CHECK(nb && nb->channel == 1 && nb->data1 == 0x22, "aprender una acción del rack desde un mando integrado");
        // Una acción de plato no se aprende de un mando integrado (va a su mapeo).
        mm.startLearn(MA_Play, 0);
        mm.pushEvent(MidiEvent{0, 0x90, 0x0B, 127});
        mm.poll();
        CHECK(mm.findBinding(MA_Play, 0) == nullptr && mm.takeIntegrated().size() == 1, "acción de plato aprendida desde un mando integrado");
        mm.stopLearn();
    }

    // 14) Caja de ritmos: arranca en el compás del plato, cada golpe en su pulso (precisión de muestra),
    //     sigue los cambios de tempo del plato, swing, cambio de patrón al acabar, sigue sola si el plato
    //     se para y se para al pedirlo.
    {
        TrackPtr silent = makeTrack(48000, 60, [](double, float& l, float& r) { l = r = 0.f; });
        Engine e;
        setupA(e, silent.get(), 120);
        e.deck[0].fader = 0.f;   // solo se oyen los instrumentos
        auto& ip = e.rack.instr.params;
        auto& is = e.rack.instr.status;
        ip.instrVol = 1.f; ip.drumVol = 1.f;
        for (int k : {0, 4, 8, 12}) ip.drum[0][DV_Bombo][k] = 1;
        ip.sync = 1;   // plato A
        cmd(e, 0, Cmd::Seek, 0.3 * 48000);
        cmd(e, 0, Cmd::Play);
        render(e, 0.05);
        ip.drumPlay = true;
        auto o = render(e, 4.0);   // de 0,35 s a 4,35 s de la pista
        // Ataques: primera muestra que supera 0,02 tras un silencio de 20 ms.
        auto onsets = [](const std::vector<float>& v, float thr) {
            std::vector<double> t;
            int quiet = 0;   // hace falta silencio antes del primer golpe (no vale una cola que ya sonaba)
            for (size_t i = 0; i < v.size() / 2; ++i) {
                const float a = std::fabs(v[i * 2]);
                if (a > thr && quiet > 960) t.push_back((double)i / kFs);
                quiet = a > thr * 0.5f ? 0 : quiet + 1;
            }
            return t;
        };
        auto on = onsets(o, 0.02f);
        // Pista: el plato empezó en 0,35 s → el primer compás llega a los 2,0 s de pista = 1,65 s de este render
        // (más los 1,5 ms de anticipación del limitador del master, que retrasa igual al plato).
        const double lat = 72.0 / kFs;
        CHECK(!on.empty() && std::fabs(on[0] - 1.65 - lat) < 0.0005, "la caja de ritmos no arranca en el compás (%.4f s)", on.empty() ? -1.0 : on[0]);
        double worst = 0;
        for (size_t i = 0; i < on.size(); ++i) worst = std::max(worst, std::fabs(on[i] - (1.65 + lat + 0.5 * i)));
        std::printf("  caja de ritmos: %zu golpes, desviación máxima del pulso %.2f ms\n", on.size(), worst * 1000);
        if (worst >= 0.001) { for (double t : on) std::printf("    golpe %.5f\n", t); }
        CHECK(on.size() >= 5 && worst < 0.001, "golpes fuera del pulso (%.3f ms)", worst * 1000);
        // Tempo del plato +5 %: los golpes siguen el nuevo pulso.
        e.deck[0].pitch = 0.05f;
        render(e, 0.2);
        o = render(e, 3.0);
        on = onsets(o, 0.02f);
        double maxDev = 0;
        for (size_t i = 1; i < on.size(); ++i) maxDev = std::max(maxDev, std::fabs((on[i] - on[i - 1]) - 0.5 / 1.05));
        if (maxDev >= 0.001) for (double t : on) std::printf("    golpe (tempo) %.5f\n", t);
        CHECK(on.size() >= 5 && maxDev < 0.001, "no sigue el tempo del plato (%.3f ms)", maxDev * 1000);
        CHECK(std::fabs(is.bpm.load() - 126.f) < 0.01f && is.clockDeck.load() == 0, "reloj: %.2f BPM del plato %d", is.bpm.load(), is.clockDeck.load());
        e.deck[0].pitch = 0.f;
        // Swing: charles en las semicorcheas pares con swing 2/3 (retraso de un tercio de semicorchea).
        for (int k : {0, 4, 8, 12}) ip.drum[0][DV_Bombo][k] = 0;
        ip.drum[0][DV_CharlesC][0] = 1; ip.drum[0][DV_CharlesC][1] = 1;
        ip.swing = 2.f / 3.f;
        render(e, 0.3);
        o = render(e, 4.5);
        on = onsets(o, 0.01f);
        bool swingOk = on.size() >= 4;
        for (size_t i = 1; i + 1 < on.size(); i += 2) {
            const double gap1 = on[i] - on[i - 1];   // de la semicorchea 1 a la 2 (con swing)
            if (std::fabs(gap1 - 0.125 * (1 + 1.0 / 3.0)) > 0.0015 && std::fabs(gap1 - (2.0 - 0.125 * (1 + 1.0 / 3.0))) > 0.0015) swingOk = false;
        }
        CHECK(swingOk, "swing: separaciones fuera de lo esperado (%zu golpes)", on.size());
        if (!swingOk) for (double t : on) std::printf("    golpe (swing) %.5f\n", t);
        ip.swing = 0.5f;
        ip.drum[0][DV_CharlesC][0] = 0; ip.drum[0][DV_CharlesC][1] = 0;
        // Cambio de patrón: entra al acabar el patrón que suena.
        ip.drum[1][DV_Caja][0] = 1;
        ip.drumPattern = 1;
        double waited = 0;
        while (is.drumPatternNow.load() != 1 && waited < 3) { render(e, 0.02); waited += 0.02; }
        CHECK(is.drumPatternNow.load() == 1 && is.drumStep.load() <= 1, "cambio de patrón (paso %d)", is.drumStep.load());
        // El plato se para: la caja sigue a su tempo.
        cmd(e, 0, Cmd::Pause);
        render(e, 0.1);
        const uint32_t hits0 = is.drumHits[DV_Caja].load();
        render(e, 4.1);
        CHECK(is.drumHits[DV_Caja].load() - hits0 == 2 && is.clockDeck.load() == -1 && std::fabs(is.bpm.load() - 120.f) < 0.01f,
              "con el plato parado sigue sola a 120 BPM (%u golpes)", is.drumHits[DV_Caja].load() - hits0);
        ip.drumPlay = false;
        render(e, 0.1);
        const uint32_t hits1 = is.drumHits[DV_Caja].load();
        o = render(e, 3.0);
        CHECK(is.drumHits[DV_Caja].load() == hits1 && peakIn(o, 1.0, 3.0) < 1e-4f && !is.drumRunning.load(), "parar la caja de ritmos");
        // Los ocho sonidos suenan y son finitos.
        Engine e2; setupA(e2, silent.get(), 120);
        auto& ip2 = e2.rack.instr.params;
        ip2.instrVol = 1.f; ip2.sync = 3; ip2.ownBpm = 120; ip2.quantStart = false;
        for (int t = 0; t < kDrumTracks; ++t) ip2.drum[0][t][t * 2] = 2;
        ip2.drumPlay = true;
        o = render(e2, 2.2);
        bool allHit = true;
        for (int t = 0; t < kDrumTracks; ++t) if (e2.rack.instr.status.drumHits[t].load() == 0) allHit = false;
        CHECK(allHit && finite(o) && peakIn(o, 0, 2.2) > 0.1f, "los ocho sonidos de la caja de ritmos");
    }

    // 15) Bajo: nota correcta (La1 = 55 Hz), slide sin redisparo, acento y finito con resonancia al máximo.
    {
        TrackPtr silent = makeTrack(48000, 30, [](double, float& l, float& r) { l = r = 0.f; });
        Engine e; setupA(e, silent.get(), 120);
        e.deck[0].fader = 0.f;
        auto& ip = e.rack.instr.params;
        ip.instrVol = 1.f; ip.bassVol = 1.f; ip.sync = 3; ip.ownBpm = 120; ip.quantStart = false;
        ip.cutoff = 0.9f; ip.envMod = 0.f; ip.reso = 0.f; ip.bassRoot = 9; ip.bassOct = 0;
        for (int k = 0; k < 16; ++k) ip.bass[0][k] = (int16_t)(kBassGate | kBassSlide);   // nota larga ligada
        ip.bassPlay = true;
        render(e, 0.5);
        auto o = render(e, 1.0);
        const double f55 = goertzel(o.data(), 24000, 2, 55, kFs), f52 = goertzel(o.data(), 24000, 2, 51.9, kFs), f58 = goertzel(o.data(), 24000, 2, 58.3, kFs);
        std::printf("  bajo: 55 Hz %.3f, vecinos %.3f / %.3f\n", f55, f52, f58);
        CHECK(f55 > 0.05 && f55 > 4 * f52 && f55 > 4 * f58, "el bajo no da La1 (55 Hz)");
        ip.reso = 1.f; ip.cutoff = 0.2f; ip.envMod = 1.f; ip.drive = 1.f; ip.accent = 1.f;
        for (int k = 0; k < 16; ++k) ip.bass[0][k] = (int16_t)(k % 3 ? (kBassGate | kBassAccent | (k * 5 % 24)) : 0);
        o = render(e, 3.0);
        CHECK(finite(o) && peakIn(o, 0, 3) > 0.05f && peakIn(o, 0, 3) < 4.f, "bajo con resonancia y drive al máximo: pico %.3f", peakIn(o, 0, 3));
        ip.bassPlay = false;
        o = render(e, 0.5);
        CHECK(peakIn(o, 0.2, 0.5) < 1e-3f, "parar el bajo");
    }

    // 16) Sampler: disparo, mantener (al soltar se apaga en 10 ms), loop que entra en el pulso, parar todo.
    {
        TrackPtr silent = makeTrack(48000, 30, [](double, float& l, float& r) { l = r = 0.f; });
        TrackPtr smp = makeTrack(48000, 1.0, [](double tt, float& l, float& r) { l = r = (float)(0.5 * std::sin(2 * kPi * 440 * tt)); });
        Engine e; setupA(e, silent.get(), 120);
        e.deck[0].fader = 0.f;
        auto& ip = e.rack.instr.params;
        auto& is = e.rack.instr.status;
        ip.instrVol = 1.f; ip.samplerVol = 1.f; ip.padVol[0] = 1.f; ip.sync = 3; ip.ownBpm = 120;
        auto pad = [&](int p, int action, Track* t = nullptr, uint32_t gen = 0) { Command c{Cmd::RackPad, 0}; c.i = p; c.d = action; c.track = t; c.gen = gen; e.send(c); };
        pad(0, 3, smp.get(), 7);
        render(e, 0.01);
        CHECK(is.padGen[0].load() == 7, "acuse de la muestra del pad");
        pad(0, 1);
        auto o = render(e, 0.5);
        CHECK(goertzel(o.data() + 4800 * 2, 9600, 2, 440, kFs) > 0.3 && is.padPlaying[0].load(), "el pad suena al pulsarlo");
        o = render(e, 0.7);
        CHECK(!is.padPlaying[0].load() && peakIn(o, 0.6, 0.7) < 1e-6f, "el disparo termina solo");
        ip.padMode[0] = PM_Mantener;
        pad(0, 1); render(e, 0.2); pad(0, 0);
        o = render(e, 0.1);
        CHECK(peakIn(o, 0.012, 0.1) < 1e-6f && !is.padPlaying[0].load(), "mantener: al soltar se apaga");
        ip.padMode[0] = PM_Loop; ip.padQuant = true;
        render(e, 0.13);
        pad(0, 1);
        render(e, 0.01);
        CHECK(is.padWaiting[0].load(), "loop: espera al pulso");
        o = render(e, 2.5);
        CHECK(is.padPlaying[0].load() && peakIn(o, 2.0, 2.5) > 0.3f, "loop: sigue sonando más allá del final de la muestra");
        pad(0, 2);
        o = render(e, 0.1);
        CHECK(!is.padPlaying[0].load() && peakIn(o, 0.05, 0.1) < 1e-6f, "parar todos los pads");
        // Sonidos incluidos: los ocho se generan, duran lo previsto y son finitos.
        bool okFx = true;
        for (int k = 0; k < kFxSamples; ++k) {
            auto t = makeFxSample(k, 48000);
            float pk = 0; bool fin2 = true;
            for (float v : t->pcm) { pk = std::max(pk, std::fabs(v)); if (!std::isfinite(v)) fin2 = false; }
            if (!fin2 || pk < 0.05f || pk > 1.f || t->frames < 48000 / 4) { okFx = false; std::printf("    sonido %d: pico %.3f, %llu muestras\n", k, pk, (unsigned long long)t->frames); }
        }
        CHECK(okFx, "sonidos incluidos del sampler");
    }

    // 17) Los instrumentos como fuente del rack: un filtro sobre «instrumentos» filtra la caja de ritmos y no el plato.
    {
        TrackPtr tone = makeTrack(48000, 30, [](double tt, float& l, float& r) { l = r = (float)(0.2 * std::sin(2 * kPi * 200 * tt)); });
        Engine e; setupA(e, tone.get(), 120);
        auto& ip = e.rack.instr.params;
        ip.instrVol = 1.f; ip.sync = 3; ip.quantStart = false;
        for (int k = 0; k < 16; ++k) ip.drum[0][DV_CharlesC][k] = 1;
        ip.drumPlay = true;
        cmd(e, 0, Cmd::Play);
        auto& rp = e.rack.params;
        rp.stSrc = (int)RackSrc::Instr;
        rp.slot[0].type = (int)StFx::Filtro; rp.slot[0].amount = 0.1f; rp.slot[0].mix = 1.f; rp.slot[0].on = true;
        render(e, 0.5);
        auto o = render(e, 1.0);
        // El tono del plato (5 kHz) pasa entero; el charles (agudo) casi desaparece.
        const double tone5k = goertzel(o.data(), 48000, 2, 200, kFs);
        double hiss = 0;
        for (size_t i = 2; i < o.size(); i += 2) { const double d = o[i] - o[i - 2]; hiss += d * d; }
        Engine ref; setupA(ref, tone.get(), 120);
        auto& ip2 = ref.rack.instr.params;
        ip2.instrVol = 1.f; ip2.sync = 3; ip2.quantStart = false;
        for (int k = 0; k < 16; ++k) ip2.drum[0][DV_CharlesC][k] = 1;
        ip2.drumPlay = true;
        cmd(ref, 0, Cmd::Play);
        render(ref, 0.5);
        auto r0 = render(ref, 1.0);
        double hiss0 = 0;
        for (size_t i = 2; i < r0.size(); i += 2) { const double d = r0[i] - r0[i - 2]; hiss0 += d * d; }
        const double tone0 = goertzel(r0.data(), 48000, 2, 200, kFs);
        std::printf("  rack sobre instrumentos: tono del plato %.2f dB, charles filtrado %.1f %% de su energía\n", dB(tone5k) - dB(tone0), 100 * (hiss - tone0 * 0) / std::max(1e-9, hiss0));
        CHECK(std::fabs(dB(tone5k) - dB(tone0)) < 0.3 && hiss < hiss0 * 0.8, "el rack con fuente INSTRUMENTOS solo toca los instrumentos");
    }

    // ================================================================ 0.11.0: efectos nuevos
    auto level = [](const std::vector<float>& o, double t0, double t1, double f) { return lvl(o, t0, t1, f); };

    // 18) Sidechain: baja en cada pulso de la rejilla y se recupera antes del siguiente (a 120 y a 150 BPM).
    for (double bpm : {120.0, 150.0}) {
        TrackPtr t = tones({440}, 0.3f);
        Engine e; setupA(e, t.get(), bpm);
        auto& sp = e.rack.params.slot[0];
        e.rack.params.stSrc = 0;
        sp.type = (int)StFx::Sidechain; sp.amount = 1.f; sp.mix = 1.f; sp.div = 6; sp.on = true;
        cmd(e, 0, Cmd::Play);
        auto o = render(e, 3.0);
        const double spb = 60.0 / bpm, ref = 0.3;
        double early = 0, late = 0; int nb = 0;
        for (int k = 1; (k + 1) * spb < 2.9; ++k, ++nb) {
            early += level(o, k * spb + 0.02 * spb, k * spb + 0.07 * spb, 440) / ref;
            late += level(o, k * spb + 0.85 * spb, k * spb + 0.97 * spb, 440) / ref;
        }
        early /= nb; late /= nb;
        std::printf("  sidechain a %.0f BPM: %.0f %% al empezar el pulso, %.0f %% al acabarlo\n", bpm, early * 100, late * 100);
        CHECK(early < 0.3 && late > 0.9, "sidechain fuera de la rejilla: %.2f / %.2f", early, late);
        CHECK(maxStep(o, 0.5, 2.5) < 0.06f, "sidechain con chasquidos: %.3f", maxStep(o, 0.5, 2.5));
    }

    // 19) Riser: sube durante los compases elegidos (1 compás = 2 s a 120 BPM), se queda arriba y se corta al apagarlo.
    {
        TrackPtr silent = makeTrack(48000, 30, [](double, float& l, float& r) { l = r = 0.f; });
        Engine e; setupA(e, silent.get(), 120);
        auto& sp = e.rack.params.slot[0];
        e.rack.params.stSrc = 0;
        sp.type = (int)StFx::Riser; sp.amount = 1.f; sp.mix = 1.f; sp.div = 6; sp.on = true;
        cmd(e, 0, Cmd::Play);
        auto o = render(e, 3.0);
        auto rmsIn = [&](double a, double b) { double q = 0; size_t i0 = (size_t)(a * kFs) * 2, i1 = (size_t)(b * kFs) * 2; for (size_t i = i0; i < i1; ++i) q += o[i] * o[i]; return std::sqrt(q / (i1 - i0)); };
        const double r1 = rmsIn(0.1, 0.4), r2 = rmsIn(1.6, 1.95), r3 = rmsIn(2.5, 2.95);
        std::printf("  riser: ruido %.4f al empezar, %.4f casi arriba, %.4f arriba\n", r1, r2, r3);
        CHECK(r1 < r2 * 0.3 && r3 > r2 * 0.9 && r3 < 0.5, "el riser no sube como debe (%.4f, %.4f, %.4f)", r1, r2, r3);
        sp.on = false;
        o = render(e, 0.2);
        CHECK(peakIn(o, 0.03, 0.2) < 1e-6f, "el riser no se corta al apagarlo: %g", peakIn(o, 0.03, 0.2));
    }

    // 20) Spiral: cada repetición sube el intervalo elegido (+5 semitonos con la ruleta al 50 %).
    {
        TrackPtr t = makeTrack(48000, 30, [](double tt, float& l, float& r) { l = r = (tt >= 0.1 && tt < 0.18) ? 0.4f * (float)std::sin(2 * kPi * 1000 * tt) : 0.f; });
        Engine e; setupA(e, t.get(), 120);
        auto& sp = e.rack.params.slot[0];
        e.rack.params.stSrc = 0;
        sp.type = (int)StFx::Spiral; sp.amount = 0.5f; sp.mix = 1.f; sp.div = 6; sp.on = true;
        cmd(e, 0, Cmd::Play);
        auto o = render(e, 1.4);
        const double f1 = 1000 * std::pow(2.0, 5 / 12.0), f2 = 1000 * std::pow(2.0, 10 / 12.0);
        const double a1 = level(o, 0.61, 0.67, f1), a1o = level(o, 0.61, 0.67, 1000), a2 = level(o, 1.11, 1.17, f2);
        std::printf("  spiral: 1.ª repetición %.3f a %.0f Hz (%.3f a 1 kHz), 2.ª %.3f a %.0f Hz\n", a1, f1, a1o, a2, f2);
        CHECK(a1 > 0.05 && a1 > 3 * a1o && a2 > 0.02, "el spiral no sube de tono");
        sp.on = false;
        render(e, 13.0);
        CHECK(!e.rack.status.slotActive[0].load(), "la cola del spiral no termina");
    }

    // 21) Freeze: sigue sonando lo congelado aunque la música se acabe; al apagarlo vuelve lo que suena.
    {
        TrackPtr t = makeTrack(48000, 30, [](double tt, float& l, float& r) { l = r = tt < 2.0 ? 0.2f * (float)std::sin(2 * kPi * 330 * tt) : 0.f; });
        Engine e; setupA(e, t.get(), 120);
        auto& sp = e.rack.params.slot[0];
        e.rack.params.stSrc = 0;
        sp.type = (int)StFx::Freeze; sp.amount = 0.5f; sp.mix = 1.f;
        cmd(e, 0, Cmd::Play);
        render(e, 1.0);
        sp.on = true;
        auto o = render(e, 2.0);   // la pista calla a los 2 s
        // (Nivel eficaz, no Goertzel: los granos cruzados reparten un poco la frecuencia.)
        double q = 0;
        for (size_t i = (size_t)(1.4 * kFs) * 2; i < (size_t)(1.9 * kFs) * 2; ++i) q += o[i] * o[i];
        const double held = std::sqrt(q / (0.5 * kFs * 2));
        std::printf("  freeze: nivel eficaz %.3f con la pista en silencio (el original, 0,141)\n", held);
        CHECK(held > 0.08 && held < 0.3, "el freeze no mantiene el sonido: %.3f", held);
        CHECK(maxStep(o, 0, 2.0) < 0.06f, "chasquidos en el freeze: %.3f", maxStep(o, 0, 2.0));
        sp.on = false;
        o = render(e, 0.3);
        CHECK(peakIn(o, 0.05, 0.3) < 1e-6f, "el freeze sigue sonando tras apagarlo");
    }

    // 22) Slicer: el patrón 1 (en orden) repite el último compás tal cual; el 7 (al revés) lo cambia.
    {
        // Compás que se repite exacto, muestra a muestra (bombo con acento en el 1 y charles tonal).
        TrackPtr t = makeTrack(48000, 40, [](double tt, float& l, float& r) {
            const long long idx = std::llround(tt * 48000) % 96000;
            const int k = (int)(idx / 24000);
            const double dt = (idx % 24000) / 48000.0, dh = dt - 0.25;
            float v = 0.f;
            if (dt < 0.25) v += (float)((k == 0 ? 0.9 : 0.6) * std::sin(2 * kPi * (55 + 90 * std::exp(-dt * 40)) * dt) * std::exp(-dt * 14));
            if (dh >= 0 && dh < 0.05) v += 0.25f * (float)(std::sin(2 * kPi * 7000 * dh) * std::exp(-dh * 80));
            l = r = v;
        });
        double diff[2];
        for (int pat = 0; pat < 2; ++pat) {
            Engine ref, e;
            setupA(ref, t.get(), 120); setupA(e, t.get(), 120);
            auto& sp = e.rack.params.slot[0];
            e.rack.params.stSrc = 0;
            sp.type = (int)StFx::Slicer; sp.amount = pat ? 0.8f : 0.f; sp.mix = 1.f; sp.div = 8;
            cmd(ref, 0, Cmd::Play); cmd(e, 0, Cmd::Play);
            render(ref, 2.3); render(e, 2.3);
            sp.on = true;
            auto a = render(ref, 4.0), b = render(e, 4.0);
            double d = 0, en = 0;
            for (size_t i = 0; i < a.size(); ++i) { d += (a[i] - b[i]) * (a[i] - b[i]); en += a[i] * a[i]; }
            diff[pat] = dB(std::sqrt(d / en));
            CHECK(finite(b), "slicer: valores no finitos");
        }
        std::printf("  slicer: patrón en orden a %.1f dB del original, patrón al revés a %.1f dB\n", diff[0], diff[1]);
        CHECK(diff[0] < -20 && diff[1] > -6, "slicer: %.1f / %.1f dB", diff[0], diff[1]);
        // Encender y apagar seguido (cada medio pulso): entra cada vez, sin esperar a grabar otro compás.
        Engine e; setupA(e, t.get(), 120);
        auto& sp = e.rack.params.slot[0];
        e.rack.params.stSrc = 0;
        sp.type = (int)StFx::Slicer; sp.amount = 0.8f; sp.mix = 1.f; sp.div = 8;
        cmd(e, 0, Cmd::Play);
        render(e, 4.5);
        int engaged = 0;
        for (int k = 0; k < 6; ++k) {
            sp.on = (k % 2) == 0;
            render(e, 0.25);
            if (sp.on.load() && e.rack.status.slotActive[0].load()) ++engaged;
        }
        CHECK(engaged == 3, "slicer: no entra al encenderlo seguido (%d de 3)", engaged);
    }

    // 23) Los efectos nuevos en la pestaña FX del plato: suenan, se apagan y dejan la señal como estaba.
    {
        TrackPtr t = makeBeatTrack(48000, 124, 0.0, 60);
        for (int fx = (int)FxType::Riser; fx < (int)FxType::Count; ++fx) {
            Engine ref, e;
            setupA(ref, t.get(), 124); setupA(e, t.get(), 124);
            e.deck[0].fxType = fx; e.deck[0].fxAmount = 0.8f; e.deck[0].fxBeats = 1.f; e.deck[0].fxOn = true;
            cmd(ref, 0, Cmd::Play); cmd(e, 0, Cmd::Play);
            auto a = render(ref, 2.0), b = render(e, 2.0);
            double d = 0, en = 0;
            for (size_t i = 0; i < a.size(); ++i) { d += (a[i] - b[i]) * (a[i] - b[i]); en += a[i] * a[i]; }
            CHECK(finite(b) && d > en * 0.01, "plato, %s: no cambia el audio (%.4f de %.4f)", fxName((FxType)fx), d, en);
            e.deck[0].fxOn = false;
            const double settle = stFxHasTail(deckFxToStation(fx)) ? 14.0 : 3.0;
            render(ref, settle); render(e, settle);
            a = render(ref, 0.3); b = render(e, 0.3);
            float diff = 0;
            for (size_t i = 0; i < a.size(); ++i) diff = std::max(diff, std::fabs(a[i] - b[i]));
            CHECK(diff < 1e-4f, "plato, %s: al apagarlo no vuelve al original (%g)", fxName((FxType)fx), diff);
        }
        // Cambiar de un efecto nuevo a uno de siempre con el efecto encendido: sin chasquidos.
        Engine e; setupA(e, tones({440}, 0.2f).get(), 124);
        TrackPtr tone = tones({440}, 0.2f);
        setupA(e, tone.get(), 124);
        e.deck[0].fxType = (int)FxType::Tremolo; e.deck[0].fxAmount = 0.3f; e.deck[0].fxOn = true;
        cmd(e, 0, Cmd::Play);
        auto o = render(e, 0.5);
        e.deck[0].fxType = (int)FxType::Flanger;
        auto o2 = render(e, 0.3);
        std::printf("  plato: cambio de trémolo a flanger, salto %.3f\n", maxStep(o2, 0, 0.05));
        CHECK(maxStep(o2, 0, 0.05) < 0.08f, "chasquido al cambiar de efecto nuevo a uno de siempre: %.3f", maxStep(o2, 0, 0.05));
    }

    // 24) Ruleta de color (SPACE, NOISE, CRUSH): en el centro no toca nada; girada, sí; SPACE deja cola y se apaga.
    {
        TrackPtr t = tones({220, 3000}, 0.15f);
        for (int color = 1; color < (int)ColorFxType::Count; ++color) {
            Engine ref, e;
            setupA(ref, t.get()); setupA(e, t.get());
            e.deck[0].colorFx = color;
            cmd(ref, 0, Cmd::Play); cmd(e, 0, Cmd::Play);
            auto a = render(ref, 0.5), b = render(e, 0.5);
            float diff = 0;
            for (size_t i = 0; i < a.size(); ++i) diff = std::max(diff, std::fabs(a[i] - b[i]));
            CHECK(diff == 0.f, "%s en el centro altera la señal: %g", colorFxLabel(color), diff);
            for (float knob : {-0.7f, 0.7f}) {
                e.deck[0].filter = knob; ref.deck[0].filter = 0.f;
                a = render(ref, 1.0); b = render(e, 1.0);
                double d = 0, en = 0;
                for (size_t i = 0; i < a.size(); ++i) { d += (a[i] - b[i]) * (a[i] - b[i]); en += a[i] * a[i]; }
                CHECK(finite(b) && d > en * 0.01, "%s %+.1f no cambia el audio", colorFxLabel(color), knob);
                CHECK(peakIn(b, 0, 1.0) < 1.0f, "%s %+.1f: pico %.2f", colorFxLabel(color), knob, peakIn(b, 0, 1.0));
                e.deck[0].filter = 0.f;
                a = render(ref, 0.3); b = render(e, 0.3);
                if (color == (int)ColorFxType::Space) {
                    double tail = 0;
                    for (size_t i = 0; i < a.size(); ++i) tail += (a[i] - b[i]) * (a[i] - b[i]);
                    CHECK(tail > 1e-4, "SPACE: no deja cola al volver al centro");
                }
                a = render(ref, 11.0); b = render(e, 11.0);
                a = render(ref, 0.3); b = render(e, 0.3);
                diff = 0;
                for (size_t i = 0; i < a.size(); ++i) diff = std::max(diff, std::fabs(a[i] - b[i]));
                CHECK(diff < 1e-4f, "%s: al volver al centro no queda como el original (%g)", colorFxLabel(color), diff);
            }
        }
        // Cambiar de color con la ruleta girada: sin chasquidos.
        TrackPtr tone = tones({440}, 0.25f);
        Engine e; setupA(e, tone.get());
        e.deck[0].filter = 0.6f;
        cmd(e, 0, Cmd::Play);
        render(e, 0.5);
        float worst = 0;
        for (int color : {1, 0, 1, 0}) {
            e.deck[0].colorFx = color;
            auto o = render(e, 0.4);
            worst = std::max(worst, maxStep(o, 0, 0.05));
        }
        std::printf("  ruleta de color: cambio FILTER/SPACE con la ruleta girada, salto %.3f\n", worst);
        CHECK(worst < 0.06f, "chasquido al cambiar el color: %.3f", worst);
    }
}

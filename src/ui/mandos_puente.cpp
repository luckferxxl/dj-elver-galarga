// Mandos con mapeo de Mixxx: intérprete, API de Mixxx para los scripts, entrada MIDI
// (opciones del XML, 14 bits, SysEx), conexiones, temporizadores, scratch y luces.
// La traducción de cada control a Ramiro DJ está en mandos_controles.cpp.
#include "mandos_puente.h"
#include "app.h"
#include "../core/util.h"
#include "../audio/analysis.h"
#include "quickjs.h"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <set>

namespace rdj {

namespace {

double nowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::string ckey(const std::string& g, const std::string& k) { return g + "," + k; }

// API de Mixxx escrita en JavaScript sobre las funciones nativas de __rdj.
const char* kPrelude = R"JS(
var engine = (function (n) {
    function Conn(id, g, k, cb) { this._id = id; this.id = String(id); this.group = g; this.name = k; this.callback = cb; }
    Object.defineProperty(Conn.prototype, 'isConnected', { get: function () { return n.connAlive(this._id); } });
    Conn.prototype.disconnect = function () { return n.disconnect(this._id); };
    Conn.prototype.trigger = function () { n.connTrigger(this._id); };
    Conn.prototype.connect = function (cb) {
        if (typeof cb !== 'function') return;
        var id = n.connect(this.group, this.name, cb);
        if (id >= 0) { this._id = id; this.id = String(id); this.callback = cb; }
    };
    function mk(g, k, cb) {
        if (typeof cb !== 'function') { n.log(1, 'makeConnection: ' + g + ',' + k + ' sin función'); return undefined; }
        var id = n.connect(String(g), String(k), cb);
        return id < 0 ? undefined : new Conn(id, String(g), String(k), cb);
    }
    function sig() { return { connect: function () {}, disconnect: function () {} }; }
    return {
        getValue: function (g, k) { return n.get(String(g), String(k)); },
        setValue: function (g, k, v) { n.set(String(g), String(k), Number(v)); },
        getParameter: function (g, k) { return n.getParam(String(g), String(k)); },
        setParameter: function (g, k, v) { n.setParam(String(g), String(k), Number(v)); },
        getParameterForValue: function (g, k, v) { return n.paramForValue(String(g), String(k), Number(v)); },
        reset: function (g, k) { n.reset(String(g), String(k)); },
        getDefaultValue: function (g, k) { return n.defValue(String(g), String(k)); },
        getDefaultParameter: function (g, k) { return n.paramForValue(String(g), String(k), n.defValue(String(g), String(k))); },
        makeConnection: mk,
        makeUnbufferedConnection: mk,
        connectControl: function (g, k, cb, disc) {
            if (typeof cb === 'string') { try { cb = (0, eval)(cb); } catch (e) { n.log(1, 'connectControl: ' + e); return false; } }
            if (typeof cb !== 'function') return false;
            if (disc) return n.disconnectFn(String(g), String(k), cb);
            return mk(g, k, cb);
        },
        trigger: function (g, k) { n.trigger(String(g), String(k)); },
        log: function (m) { n.log(0, String(m)); },
        beginTimer: function (ms, f, one) {
            if (typeof f === 'string') { var code = f; f = function () { (0, eval)(code); }; }
            if (typeof f !== 'function') return 0;
            return n.timer(Number(ms), f, !!one);
        },
        stopTimer: function (id) { n.stopTimer(Number(id)); },
        scratchEnable: function (d, ipr, rpm, a, b, ramp) { n.scratchEnable(Number(d), Number(ipr), Number(rpm), Number(a || 0), Number(b || 0), ramp === undefined ? true : !!ramp); },
        scratchTick: function (d, i) { n.scratchTick(Number(d), Number(i)); },
        scratchDisable: function (d, ramp) { n.scratchDisable(Number(d), ramp === undefined ? true : !!ramp); },
        isScratching: function (d) { return n.isScratching(Number(d)); },
        softTakeover: function (g, k, on) { n.softTakeover(String(g), String(k), !!on); },
        softTakeoverIgnoreNextValue: function (g, k) { n.stIgnoreNext(String(g), String(k)); },
        softTakeoverWillIgnore: function (g, k, v) { return n.stWillIgnore(String(g), String(k), Number(v)); },
        brake: function (d, on, f, r) { n.brake(Number(d), !!on); },
        spinback: function (d, on, f, r) { n.spinback(Number(d), !!on); },
        softStart: function (d, on, f) { n.softStart(Number(d), !!on); },
        isBrakeActive: function () { return false; },
        isSpinbackActive: function () { return false; },
        isSoftStartActive: function () { return false; },
        getSetting: function (name) { return n.setting(String(name)); },
        getPlayer: function (g) {
            var i = n.player(String(g));
            if (!i) return undefined;
            var p = { title: i[0], artist: i[1], album: '', albumArtist: '', genre: i[2], composer: '', grouping: '', year: '', trackNumber: '', trackTotal: '', key: i[3] };
            ['trackLoaded', 'trackUnloaded', 'artistChanged', 'titleChanged', 'albumChanged', 'albumArtistChanged', 'genreChanged',
             'composerChanged', 'groupingChanged', 'yearChanged', 'trackNumberChanged', 'trackTotalChanged', 'keyChanged'].forEach(function (s) { p[s] = sig(); });
            return p;
        },
        convertCharset: function (cs, str) {
            var a = []; str = String(str);
            for (var i = 0; i < str.length; i++) { var c = str.charCodeAt(i); a.push(c < 256 ? c : 63); }
            return a;
        }
    };
})(__rdj);
var midi = {
    sendShortMsg: function (s, a, b) { __rdj.sendShort(Number(s), Number(a), Number(b)); },
    sendShortMsgBuffered: function (s, a, b) { __rdj.sendShort(Number(s), Number(a), Number(b)); },
    sendSysexMsg: function (d, l) { __rdj.sendSysex(Array.prototype.slice.call(d)); },
    send: function (d, l) { __rdj.sendSysex(Array.prototype.slice.call(d)); },
    makeInputHandler: function (st, no, cb) {
        if (typeof cb !== 'function') return undefined;
        var id = __rdj.inputHandler(Number(st), Number(no), cb);
        return { disconnect: function () { return __rdj.removeInputHandler(id); } };
    }
};
var console = (function () {
    function j(a) { return Array.prototype.map.call(a, function (x) { try { return typeof x === 'object' ? JSON.stringify(x) : String(x); } catch (e) { return String(x); } }).join(' '); }
    return {
        log: function () { __rdj.log(0, j(arguments)); }, info: function () { __rdj.log(0, j(arguments)); },
        debug: function () {}, trace: function () {},
        warn: function () { __rdj.log(1, j(arguments)); }, error: function () { __rdj.log(2, j(arguments)); },
        assert: function (c) { if (!c) __rdj.log(1, 'assert: ' + j(Array.prototype.slice.call(arguments, 1))); }
    };
})();
var print = function (m) { __rdj.log(0, String(m)); };
function ColorMapper(map) {
    this._m = [];
    for (var k in map) { var c = Number(k); this._m.push([(c >> 16) & 255, (c >> 8) & 255, c & 255, map[k]]); }
}
ColorMapper.prototype._near = function (c) {
    var r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255, best = null, bd = Infinity;
    for (var i = 0; i < this._m.length; i++) {
        var e = this._m[i], rm = (r + e[0]) / 2, dr = r - e[0], dg = g - e[1], db = b - e[2];
        var d = (2 + rm / 256) * dr * dr + 4 * dg * dg + (2 + (255 - rm) / 256) * db * db;
        if (d < bd) { bd = d; best = e; }
    }
    return best;
};
ColorMapper.prototype.getValueForNearestColor = function (c) { var e = this._near(c); return e ? e[3] : 0; };
ColorMapper.prototype.getNearestColor = function (c) { var e = this._near(c); return e ? { red: e[0], green: e[1], blue: e[2] } : { red: 0, green: 0, blue: 0 }; };
)JS";

} // namespace

// ================================================================== MandoActivo

class MandoActivo {
public:
    MandoPuente& pu;
    mandos::Mapping map;
    std::string port;
    int dev = -1;            // entrada MIDI; -1 = simulado (pruebas)
    JSRuntime* rt = nullptr;
    JSContext* ctx = nullptr;
    bool loaded = false, started = false;
    int errors = 0, sentShort = 0, sentSysex = 0, ledsOn = 0;
    std::vector<std::string> log;
    std::vector<std::vector<uint8_t>> simOut;
    double deadline = 0;
    int depth = 0;           // llamadas anidadas: solo la de fuera pone el límite de tiempo

    struct Conn { int id; std::string g, k; JSValue fn; bool alive; };
    std::vector<Conn> conns;
    int nextConn = 1;
    struct Timer { int id; double interval, next; JSValue fn; bool oneShot, alive; };
    std::vector<Timer> timers;
    int nextTimer = 1;
    struct Watch { std::string g, k; double last = 0; };
    std::map<std::string, Watch> watches;
    std::vector<std::pair<std::string, double>> notifyQ;
    std::vector<int> outLast;
    std::multimap<uint16_t, size_t> inIndex;   // (status << 8 | midino) -> entrada
    struct ST { bool enabled = false, ignoreNext = false, first = true; double prev = 0, time = -1e9; };
    std::unordered_map<std::string, ST> st;
    std::vector<std::pair<size_t, uint8_t>> q14;
    struct Scratch { bool on = false, ramp = false; double dx = 0, acc = 0, x = 0, v = 0, alpha = 0.125, beta = 0.125 / 32, rampTo = 0; };
    Scratch scr[9];
    std::unordered_map<std::string, JSValue> wrappers;
    struct Handler { int id; uint16_t key; JSValue fn; bool alive; };
    std::vector<Handler> handlers;
    int nextHandler = 1;
    std::unordered_map<std::string, double> powerPress;
    std::vector<std::string> prefixes;
    std::map<std::string, int> untranslated;

    MandoActivo(MandoPuente& p) : pu(p) {}
    ~MandoActivo() { destroy(); }

    // ------------------------------------------------------------ registro
    void addLog(int level, const std::string& m) {
        if (level >= 2) ++errors;
        std::string line = (level >= 2 ? "Error: " : level == 1 ? "Aviso: " : "") + m;
        if (line.size() > 400) line = line.substr(0, 400) + "…";
        log.push_back(line);
        if (log.size() > 300) log.erase(log.begin(), log.begin() + 100);
    }
    std::string exceptionText() {
        JSValue e = JS_GetException(ctx);
        std::string out;
        if (const char* s = JS_ToCString(ctx, e)) { out = s; JS_FreeCString(ctx, s); }
        JSValue stack = JS_GetPropertyStr(ctx, e, "stack");
        if (JS_IsString(stack)) if (const char* s = JS_ToCString(ctx, stack)) { std::string st(s); JS_FreeCString(ctx, s); if (!st.empty()) out += " | " + st.substr(0, 300); }
        JS_FreeValue(ctx, stack);
        JS_FreeValue(ctx, e);
        return out;
    }
    void runJobs() {
        JSContext* c = nullptr;
        for (int i = 0; i < 200 && JS_ExecutePendingJob(rt, &c) > 0; ++i) {}
    }
    bool evalCode(const std::string& code, const std::string& name, double budget = 3.0) {
        if (depth++ == 0) deadline = nowSec() + budget;
        JSValue r = JS_Eval(ctx, code.c_str(), code.size(), name.c_str(), JS_EVAL_TYPE_GLOBAL);
        bool ok = !JS_IsException(r);
        if (!ok) addLog(2, name + ": " + exceptionText());
        JS_FreeValue(ctx, r);
        if (depth == 1) runJobs();
        --depth;
        return ok;
    }
    bool call(JSValueConst fn, int argc, JSValueConst* argv, const std::string& what, double budget = 0.5) {
        if (depth >= 32) { addLog(2, what + ": demasiadas llamadas anidadas"); return false; }
        if (depth++ == 0) deadline = nowSec() + budget;
        JSValue r = JS_Call(ctx, fn, JS_UNDEFINED, argc, argv);
        bool ok = !JS_IsException(r);
        if (!ok) addLog(2, what + ": " + exceptionText());
        JS_FreeValue(ctx, r);
        if (depth == 1) runJobs();
        --depth;
        return ok;
    }

    // ------------------------------------------------------------ salida MIDI
    void sendShort(int s, int d1, int d2) {
        if (s < 0x80 || s > 0xFF) return;
        ++sentShort;
        if (dev < 0) { simOut.push_back({(uint8_t)s, (uint8_t)(d1 & 0x7F), (uint8_t)(d2 & 0x7F)}); if (simOut.size() > 20000) simOut.erase(simOut.begin(), simOut.begin() + 10000); return; }
        pu.midiShort(dev, (uint8_t)s, (uint8_t)d1, (uint8_t)d2);
    }
    void sendSysex(const std::vector<uint8_t>& d) {
        if (d.empty()) return;
        ++sentSysex;
        if (dev < 0) { simOut.push_back(d); return; }
        pu.midiSysex(dev, d);
    }

    // ------------------------------------------------------------ controles
    void watch(const std::string& g, const std::string& k) {
        const std::string key = ckey(g, k);
        if (watches.count(key)) return;
        Watch w; w.g = g; w.k = k; w.last = pu.get(g, k);
        watches[key] = w;
    }
    void fire(const std::string& g, const std::string& k, double v) {
        // Copia de los ids: un callback puede crear o quitar conexiones.
        std::vector<int> ids;
        for (auto& c : conns) if (c.alive && c.g == g && c.k == k) ids.push_back(c.id);
        for (int id : ids) {
            for (auto& c : conns) {
                if (c.id != id || !c.alive) continue;
                JSValue fn = JS_DupValue(ctx, c.fn);
                JSValue args[3] = {JS_NewNumber(ctx, v), JS_NewString(ctx, g.c_str()), JS_NewString(ctx, k.c_str())};
                call(fn, 3, args, "conexión " + g + "," + k);
                for (auto& a : args) JS_FreeValue(ctx, a);
                JS_FreeValue(ctx, fn);
                break;
            }
        }
    }
    bool warnedFlood = false;
    void flushNotify() {
        // Cola de callbacks (como las conexiones en cola de Mixxx). Con un tope: algún mapeo
        // se vuelve a llamar a sí mismo sin fin; así no se queda colgado el programa.
        int fired = 0;
        for (int guard = 0; guard < 8 && !notifyQ.empty(); ++guard) {
            auto q = std::move(notifyQ);
            notifyQ.clear();
            for (auto& [key, v] : q) {
                if (++fired > 3000) {
                    if (!warnedFlood) { warnedFlood = true; addLog(1, "El script del mando se llama a sí mismo sin parar; se cortan los avisos sobrantes."); }
                    notifyQ.clear();
                    return;
                }
                auto it = watches.find(key);
                if (it == watches.end()) continue;
                it->second.last = v;
                fire(it->second.g, it->second.k, v);
            }
        }
        if (notifyQ.size() > 3000) notifyQ.resize(3000);
    }
    void scriptSet(const std::string& g, const std::string& k, double v, bool param) {
        const MxBehavior b = pu.behavior(g, k);
        const double value = param ? b.paramToValue(v) : v;
        auto it = st.find(ckey(g, k));
        if (it != st.end() && it->second.enabled && stIgnore(it->second, g, k, b.valueToParam(value))) return;
        const bool real = pu.real(g, k);
        if (!pu.known(g, k)) ++untranslated[ckey(g, k)];
        const double before = real ? 0 : pu.get(g, k);
        pu.set(g, k, value);
        if (!real && before != value) {   // como en Mixxx: poner el mismo valor no avisa
            const std::string key = ckey(g, k);
            if (watches.count(key)) notifyQ.push_back({key, value});
        }
    }
    // Soft takeover de Mixxx: no salta si el control físico está lejos del valor actual.
    bool stWillIgnore(const ST& s, const std::string& g, const std::string& k, double newParam) {
        const double now = nowSec();
        if (now < s.time + 0.05) return false;
        const double cur = pu.behavior(g, k).valueToParam(pu.get(g, k));
        const double diff = cur - newParam, prevDiff = cur - s.prev;
        if (std::signbit(prevDiff) != std::signbit(diff)) return false;
        const double thr = 3.0 / 128.0;
        if (std::fabs(diff) <= thr || std::fabs(prevDiff) <= thr) return false;
        return true;
    }
    bool stIgnore(ST& s, const std::string& g, const std::string& k, double newParam) {
        bool ign;
        if (s.ignoreNext) { s.ignoreNext = false; ign = true; }
        else if (s.first) { s.first = false; ign = std::fabs(pu.behavior(g, k).valueToParam(pu.get(g, k)) - newParam) > 3.0 / 128.0; s.time = -1e9; }
        else ign = stWillIgnore(s, g, k, newParam);
        if (!ign) s.time = nowSec();
        s.prev = newParam;
        return ign;
    }

    // ------------------------------------------------------------ entrada MIDI
    static double computeValue(uint32_t o, double prev, double v) {
        using namespace mandos;
        if (!o) return v;
        if (o & OptInvert) v = 127.0 - v;
        if (o & (OptRot64 | OptRot64Inv)) {
            double diff = v - 64.0;
            if (diff == -1 || diff == 1) diff /= 16;
            else diff += (diff > 0 ? -1 : +1);
            const double t = (o & OptRot64) ? prev + diff : prev - diff;
            return std::clamp(t, 0.0, 127.0);
        }
        if (o & OptRot64Fast) return std::clamp(prev + (v - 64.0) * 1.5, 0.0, 127.0);
        if (o & OptDiff) { if (v >= 64.0) v -= 128.0; return prev + v; }
        if (o & OptSelectKnob) { if (v >= 64.0) v -= 128.0; return v; }
        if (o & OptButton) return v != 0 ? 1 : 0;
        if (o & OptSwitch) return 1;
        if (o & OptSpread64) { const double d = v - 64.0; return d * std::fabs(d) / 64.0; }
        if (o & (OptHercJog | OptHercJogFast)) {
            if (v > 64.0) v -= 128.0;
            return (o & OptHercJogFast) ? prev + v * 3 : prev + v;
        }
        return v;
    }
    void setFromMidi(const std::string& g, const std::string& k, const MxBehavior& b, int opcode, double v) {
        const bool pressed = !(opcode == 0x80 || v == 0);
        const bool real = pu.real(g, k);
        const double before = real ? 0 : pu.get(g, k);
        switch (b.kind) {
        case MxBehavior::Push: pu.set(g, k, pressed ? 1.0 : 0.0); break;
        case MxBehavior::Toggle: if (pressed) pu.set(g, k, pu.get(g, k) > 0 ? 0.0 : 1.0); break;
        case MxBehavior::PowerWin: {
            const std::string key = ckey(g, k);
            if (pressed) { pu.set(g, k, pu.get(g, k) > 0 ? 0.0 : 1.0); powerPress[key] = nowSec(); }
            else if (nowSec() - powerPress[key] > 0.3) pu.set(g, k, 0.0);   // pulsación larga: momentáneo
            break;
        }
        case MxBehavior::Encoder: pu.set(g, k, v); break;
        case MxBehavior::Num: pu.set(g, k, v / 127.0); break;
        default: pu.set(g, k, b.paramToValue(b.midiToParam(v))); break;
        }
        if (!real) {
            if (!pu.known(g, k)) ++untranslated[ckey(g, k)];
            const std::string key = ckey(g, k);
            const double after = pu.get(g, k);
            if (after != before && watches.count(key)) notifyQ.push_back({key, after});
        }
    }
    std::unordered_map<std::string, JSValue> checkers;
    std::set<std::string> warnedMissing;
    // ¿Existe la función a la que apunta el XML? (algunos mapeos nombran funciones que su script no tiene;
    // Mixxx solo avisa). Se comprueba cada vez: puede crearse más tarde (capas, modos).
    bool bindingExists(const std::string& key) {
        JSValue fn;
        auto it = checkers.find(key);
        if (it != checkers.end()) fn = it->second;
        else {
            const std::string code = "(function () { try { return typeof (" + key + ") === 'function'; } catch (e) { return false; } })";
            fn = JS_Eval(ctx, code.c_str(), code.size(), "<comprobar>", JS_EVAL_TYPE_GLOBAL);
            if (JS_IsException(fn)) { JS_FreeValue(ctx, JS_GetException(ctx)); fn = JS_UNDEFINED; }
            checkers[key] = fn;
        }
        if (!JS_IsFunction(ctx, fn)) return false;
        JSValue r = JS_Call(ctx, fn, JS_UNDEFINED, 0, nullptr);
        const bool ok = JS_ToBool(ctx, r) > 0;
        JS_FreeValue(ctx, r);
        return ok;
    }
    void scriptBinding(const mandos::InputMap& in, int ch, int d1, int d2, int status) {
        if (!bindingExists(in.key)) {
            if (warnedMissing.insert(in.key).second) addLog(1, "El mapeo usa " + in.key + ", que su script no define (se ignora, como en Mixxx).");
            return;
        }
        JSValue fn;
        auto it = wrappers.find(in.key);
        if (it != wrappers.end()) fn = it->second;
        else {
            const std::string code = "(function (a, b, c, d, e) { return " + in.key + "(a, b, c, d, e); })";
            if (depth == 0) deadline = nowSec() + 1.0;
            fn = JS_Eval(ctx, code.c_str(), code.size(), "<entrada>", JS_EVAL_TYPE_GLOBAL);
            if (JS_IsException(fn)) { addLog(2, "función " + in.key + ": " + exceptionText()); fn = JS_UNDEFINED; }
            wrappers[in.key] = fn;
        }
        if (!JS_IsFunction(ctx, fn)) return;
        JSValue args[5] = {JS_NewInt32(ctx, ch), JS_NewInt32(ctx, d1), JS_NewInt32(ctx, d2), JS_NewInt32(ctx, status), JS_NewString(ctx, in.group.c_str())};
        call(fn, 5, args, in.key);
        for (auto& a : args) JS_FreeValue(ctx, a);
    }
    void onShort(uint8_t status, uint8_t d1, uint8_t d2) {
        using namespace mandos;
        if (status >= 0xF8) return;   // reloj y tiempo real
        const int opcode = status & 0xF0, ch = status & 0x0F;
        // Manejadores de midi.makeInputHandler
        const uint16_t hk = (uint16_t)(status << 8 | d1);
        for (size_t i = 0; i < handlers.size(); ++i) {
            if (!handlers[i].alive || handlers[i].key != hk) continue;
            JSValue fn = JS_DupValue(ctx, handlers[i].fn);
            JSValue args[4] = {JS_NewInt32(ctx, ch), JS_NewInt32(ctx, d1), JS_NewInt32(ctx, d2), JS_NewInt32(ctx, status)};
            call(fn, 4, args, "makeInputHandler");
            for (auto& a : args) JS_FreeValue(ctx, a);
            JS_FreeValue(ctx, fn);
        }
        auto range = opcode == 0xE0 ? inIndex.equal_range((uint16_t)(status << 8 | 0)) : inIndex.equal_range(hk);
        std::vector<size_t> idx;
        for (auto it = range.first; it != range.second; ++it) idx.push_back(it->second);
        if (opcode == 0xE0 && idx.empty()) {
            // Pitch bend: algunos XML ponen el byte bajo como midino.
            for (auto it = inIndex.lower_bound((uint16_t)(status << 8)); it != inIndex.end() && (it->first >> 8) == status; ++it) idx.push_back(it->second);
        }
        for (size_t i : idx) {
            const InputMap& in = map.inputs[i];
            if (in.opts & OptScript) { scriptBinding(in, ch, d1, d2, status); continue; }
            const MxBehavior b = pu.behavior(in.group, in.key);
            double v;
            int op = opcode;
            if (in.opts & (Opt14Msb | Opt14Lsb)) {
                bool found = false;
                for (size_t q = 0; q < q14.size(); ++q) {
                    const InputMap& other = map.inputs[q14[q].first];
                    if (other.group != in.group || other.key != in.key) continue;
                    if ((other.opts & in.opts) & (Opt14Msb | Opt14Lsb)) { q14.erase(q14.begin() + (long)q); found = true; v = -1; break; }
                    const int iv = (in.opts & Opt14Msb) ? (d2 << 7) | q14[q].second : (q14[q].second << 7) | d2;
                    v = std::min(iv / 128.0, 127.0);
                    q14.erase(q14.begin() + (long)q);
                    found = true;
                    break;
                }
                if (!found) { q14.push_back({i, d2}); continue; }
                if (v < 0) continue;
            } else if (opcode == 0xE0) {
                v = std::min(((d2 << 7) | d1) / 128.0, 127.0);
            } else {
                if (!q14.empty()) q14.clear();
                v = computeValue(in.opts, b.valueToMidi(pu.get(in.group, in.key)), d2);
            }
            if (in.opts & (OptButton | OptSwitch)) op = 0x90;
            if (in.opts & OptSoftTakeover) {
                ST& s = st[ckey(in.group, in.key)];
                s.enabled = true;
                if (b.kind == MxBehavior::Pot || b.kind == MxBehavior::Audio)
                    if (stIgnore(s, in.group, in.key, b.midiToParam(v))) continue;
            }
            setFromMidi(in.group, in.key, b, op, v);
        }
        flushNotify();
    }
    void onSysex(const std::vector<uint8_t>& data) {
        JSValue arr = JS_NewArray(ctx);
        for (size_t i = 0; i < data.size(); ++i) JS_SetPropertyUint32(ctx, arr, (uint32_t)i, JS_NewInt32(ctx, data[i]));
        JSValue global = JS_GetGlobalObject(ctx);
        for (auto& pf : prefixes) {
            JSValue obj = JS_GetPropertyStr(ctx, global, pf.c_str());
            if (JS_IsObject(obj)) {
                JSValue fn = JS_GetPropertyStr(ctx, obj, "incomingData");
                if (JS_IsFunction(ctx, fn)) {
                    JSValue args[2] = {JS_DupValue(ctx, arr), JS_NewInt32(ctx, (int)data.size())};
                    if (depth++ == 0) deadline = nowSec() + 0.5;
                    JSValue r = JS_Call(ctx, fn, obj, 2, args);
                    --depth;
                    if (JS_IsException(r)) addLog(2, pf + ".incomingData: " + exceptionText());
                    JS_FreeValue(ctx, r);
                    for (auto& a : args) JS_FreeValue(ctx, a);
                }
                JS_FreeValue(ctx, fn);
            }
            JS_FreeValue(ctx, obj);
        }
        JS_FreeValue(ctx, global);
        JS_FreeValue(ctx, arr);
        runJobs();
        flushNotify();
    }

    // ------------------------------------------------------------ ciclo
    void sendOutputs(bool force) {
        for (size_t i = 0; i < map.outputs.size(); ++i) {
            const auto& o = map.outputs[i];
            const double v = pu.get(o.group, o.key);
            const int byte = (v >= o.minimum && v <= o.maximum) ? o.on : o.off;
            if (!force && byte == outLast[i]) continue;
            outLast[i] = byte;
            if (byte == 0xFF) continue;
            sendShort(o.status, o.midino, byte);
            if (byte == o.on) ++ledsOn;
        }
    }
    void tick(double now, float dt) {
        // Temporizadores (como mucho una vez por fotograma cada uno).
        for (size_t i = 0; i < timers.size(); ++i) {
            if (!timers[i].alive || timers[i].next > now) continue;
            JSValue fn = JS_DupValue(ctx, timers[i].fn);
            const bool one = timers[i].oneShot;
            if (one) timers[i].alive = false;
            else { timers[i].next += timers[i].interval; if (timers[i].next < now) timers[i].next = now + timers[i].interval; }
            call(fn, 0, nullptr, "temporizador");
            JS_FreeValue(ctx, fn);
        }
        flushNotify();
        scratchTick(dt);
        // Conexiones: valores que han cambiado (por la interfaz, el motor o el script).
        for (auto& [key, w] : watches) {
            const double v = pu.get(w.g, w.k);
            if (v == w.last) continue;
            w.last = v;
            fire(w.g, w.k, v);
        }
        flushNotify();
        sendOutputs(false);
        // Limpieza
        conns.erase(std::remove_if(conns.begin(), conns.end(), [&](Conn& c) { if (!c.alive) { JS_FreeValue(ctx, c.fn); return true; } return false; }), conns.end());
        timers.erase(std::remove_if(timers.begin(), timers.end(), [&](Timer& t) { if (!t.alive) { JS_FreeValue(ctx, t.fn); return true; } return false; }), timers.end());
    }

    // Scratch con el filtro alfa-beta de Mixxx, en pasos de 1 ms dentro del fotograma.
    void scratchTick(float dt) {
        for (int d = 1; d <= 8; ++d) {
            Scratch& s = scr[d];
            if (!s.on) continue;
            const std::string g = "[Channel" + std::to_string(d) + "]";
            const int steps = std::clamp((int)std::lround(dt / 0.001), 1, 200);
            const double sdt = 0.001;
            const double perStep = s.acc / steps;
            for (int i = 0; i < steps; ++i) {
                const double obs = s.ramp ? s.rampTo * sdt : s.dx * perStep;
                const double px = s.x + s.v * sdt, pv = s.v;
                const double rx = obs - px;
                s.x = px + rx * s.alpha;
                s.v = pv + rx * s.beta / sdt;
                s.x -= obs;
            }
            s.acc = 0;
            pu.set(g, "scratch2", s.v);
            if (s.ramp && std::fabs(s.rampTo - s.v) <= 0.01) {
                s.on = s.ramp = false;
                pu.set(g, "scratch2", 0.0);
                pu.set(g, "scratch2_enable", 0.0);
            }
        }
    }

    // ------------------------------------------------------------ arranque y parada
    bool start(const std::string& file, const std::string& portName, int device);
    void stop() {
        if (!ctx) return;
        if (started) {
            for (auto& pf : prefixes)
                evalCode("if (typeof " + pf + " !== 'undefined' && " + pf + " && typeof " + pf + ".shutdown === 'function') " + pf + ".shutdown();", pf + ".shutdown", 1.0);
            flushNotify();
        }
        started = false;
        destroy();
    }
    void destroy() {
        if (!ctx) return;
        for (auto& c : conns) JS_FreeValue(ctx, c.fn);
        for (auto& t : timers) JS_FreeValue(ctx, t.fn);
        for (auto& h : handlers) JS_FreeValue(ctx, h.fn);
        for (auto& kv : wrappers) JS_FreeValue(ctx, kv.second);
        for (auto& kv : checkers) JS_FreeValue(ctx, kv.second);
        conns.clear(); timers.clear(); handlers.clear(); wrappers.clear(); checkers.clear();
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        ctx = nullptr; rt = nullptr;
        // Los scratch de este mando se sueltan.
        for (int d = 1; d <= 8; ++d) if (scr[d].on) { const std::string g = "[Channel" + std::to_string(d) + "]"; pu.set(g, "scratch2_enable", 0.0); scr[d] = Scratch{}; }
    }
};

// ------------------------------------------------------------------ funciones nativas

namespace {

MandoActivo* M(JSContext* ctx) { return static_cast<MandoActivo*>(JS_GetContextOpaque(ctx)); }
std::string S(JSContext* ctx, JSValueConst v) {
    const char* s = JS_ToCString(ctx, v);
    std::string r = s ? s : "";
    if (s) JS_FreeCString(ctx, s);
    return r;
}
double D(JSContext* ctx, JSValueConst v) { double d = 0; if (JS_ToFloat64(ctx, &d, v) < 0) { JS_FreeValue(ctx, JS_GetException(ctx)); d = 0; } return std::isfinite(d) ? d : 0; }
bool B(JSContext* ctx, JSValueConst v) { return JS_ToBool(ctx, v) > 0; }
#define ARG(i) (argc > (i) ? argv[i] : JS_UNDEFINED)
#define NATIVE(name) JSValue name(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)

NATIVE(n_get) {
    MandoActivo* m = M(ctx);
    const std::string g = S(ctx, ARG(0)), k = S(ctx, ARG(1));
    return JS_NewNumber(ctx, m->pu.get(g, k));
}
NATIVE(n_set) {
    MandoActivo* m = M(ctx);
    m->scriptSet(S(ctx, ARG(0)), S(ctx, ARG(1)), D(ctx, ARG(2)), false);
    return JS_UNDEFINED;
}
NATIVE(n_getParam) {
    MandoActivo* m = M(ctx);
    const std::string g = S(ctx, ARG(0)), k = S(ctx, ARG(1));
    return JS_NewNumber(ctx, m->pu.behavior(g, k).valueToParam(m->pu.get(g, k)));
}
NATIVE(n_setParam) {
    MandoActivo* m = M(ctx);
    m->scriptSet(S(ctx, ARG(0)), S(ctx, ARG(1)), D(ctx, ARG(2)), true);
    return JS_UNDEFINED;
}
NATIVE(n_paramForValue) {
    MandoActivo* m = M(ctx);
    return JS_NewNumber(ctx, m->pu.behavior(S(ctx, ARG(0)), S(ctx, ARG(1))).valueToParam(D(ctx, ARG(2))));
}
NATIVE(n_defValue) {
    MandoActivo* m = M(ctx);
    return JS_NewNumber(ctx, m->pu.behavior(S(ctx, ARG(0)), S(ctx, ARG(1))).def);
}
NATIVE(n_reset) {
    MandoActivo* m = M(ctx);
    const std::string g = S(ctx, ARG(0)), k = S(ctx, ARG(1));
    m->scriptSet(g, k, m->pu.behavior(g, k).def, false);
    return JS_UNDEFINED;
}
NATIVE(n_connect) {
    MandoActivo* m = M(ctx);
    if (!JS_IsFunction(ctx, ARG(2))) return JS_NewInt32(ctx, -1);
    MandoActivo::Conn c{m->nextConn++, S(ctx, ARG(0)), S(ctx, ARG(1)), JS_DupValue(ctx, ARG(2)), true};
    m->watch(c.g, c.k);
    m->conns.push_back(c);
    return JS_NewInt32(ctx, c.id);
}
NATIVE(n_disconnect) {
    MandoActivo* m = M(ctx);
    const int id = (int)D(ctx, ARG(0));
    for (auto& c : m->conns) if (c.id == id && c.alive) { c.alive = false; return JS_NewBool(ctx, true); }
    return JS_NewBool(ctx, false);
}
NATIVE(n_connAlive) {
    MandoActivo* m = M(ctx);
    const int id = (int)D(ctx, ARG(0));
    for (auto& c : m->conns) if (c.id == id) return JS_NewBool(ctx, c.alive);
    return JS_NewBool(ctx, false);
}
NATIVE(n_connTrigger) {
    MandoActivo* m = M(ctx);
    const int id = (int)D(ctx, ARG(0));
    for (auto& c : m->conns) {
        if (c.id != id || !c.alive) continue;
        const double v = m->pu.get(c.g, c.k);
        JSValue fn = JS_DupValue(ctx, c.fn);
        const std::string g = c.g, k = c.k;
        JSValue args[3] = {JS_NewNumber(ctx, v), JS_NewString(ctx, g.c_str()), JS_NewString(ctx, k.c_str())};
        m->call(fn, 3, args, "trigger " + g + "," + k);
        for (auto& a : args) JS_FreeValue(ctx, a);
        JS_FreeValue(ctx, fn);
        break;
    }
    return JS_UNDEFINED;
}
NATIVE(n_disconnectFn) {
    MandoActivo* m = M(ctx);
    const std::string g = S(ctx, ARG(0)), k = S(ctx, ARG(1));
    bool any = false;
    for (auto& c : m->conns) if (c.alive && c.g == g && c.k == k && JS_IsStrictEqual(ctx, c.fn, ARG(2))) { c.alive = false; any = true; }
    return JS_NewBool(ctx, any);
}
NATIVE(n_trigger) {
    // Como en Mixxx: los callbacks se ejecutan después (cola), no dentro de la llamada.
    MandoActivo* m = M(ctx);
    const std::string g = S(ctx, ARG(0)), k = S(ctx, ARG(1));
    m->watch(g, k);
    if (m->notifyQ.size() < 5000) m->notifyQ.push_back({ckey(g, k), m->pu.get(g, k)});
    return JS_UNDEFINED;
}
NATIVE(n_timer) {
    MandoActivo* m = M(ctx);
    if (!JS_IsFunction(ctx, ARG(1))) return JS_NewInt32(ctx, 0);
    const double ms = std::max(1.0, D(ctx, ARG(0)));
    MandoActivo::Timer t{m->nextTimer++, ms / 1000.0, nowSec() + ms / 1000.0, JS_DupValue(ctx, ARG(1)), B(ctx, ARG(2)), true};
    m->timers.push_back(t);
    return JS_NewInt32(ctx, t.id);
}
NATIVE(n_stopTimer) {
    MandoActivo* m = M(ctx);
    const int id = (int)D(ctx, ARG(0));
    for (auto& t : m->timers) if (t.id == id) t.alive = false;
    return JS_UNDEFINED;
}
NATIVE(n_scratchEnable) {
    MandoActivo* m = M(ctx);
    const int d = (int)D(ctx, ARG(0));
    if (d < 1 || d > 8) return JS_UNDEFINED;
    const double ipr = D(ctx, ARG(1)), rpm = D(ctx, ARG(2)), a = D(ctx, ARG(3)), b = D(ctx, ARG(4));
    const bool ramp = B(ctx, ARG(5));
    const double ips = rpm * ipr / 60.0;
    if (ips == 0) return JS_UNDEFINED;
    MandoActivo::Scratch& s = m->scr[d];
    const std::string g = "[Channel" + std::to_string(d) + "]";
    double v0 = 0;
    if (ramp) {
        if (m->pu.get(g, "scratch2_enable") > 0) v0 = m->pu.get(g, "scratch2");
        else if (m->pu.get(g, "play") > 0) v0 = 1.0 + m->pu.get(g, "rate") * m->pu.get(g, "rateRange");
    }
    s = MandoActivo::Scratch{};
    s.on = true;
    s.dx = 1.0 / ips;
    s.v = v0;
    if (a != 0 && b != 0) { s.alpha = a; s.beta = b; }
    m->pu.set(g, "scratch2", v0);
    m->pu.set(g, "scratch2_enable", 1.0);
    return JS_UNDEFINED;
}
NATIVE(n_scratchTick) {
    MandoActivo* m = M(ctx);
    const int d = (int)D(ctx, ARG(0));
    if (d >= 1 && d <= 8) m->scr[d].acc += D(ctx, ARG(1));
    return JS_UNDEFINED;
}
NATIVE(n_scratchDisable) {
    MandoActivo* m = M(ctx);
    const int d = (int)D(ctx, ARG(0));
    if (d < 1 || d > 8) return JS_UNDEFINED;
    MandoActivo::Scratch& s = m->scr[d];
    const std::string g = "[Channel" + std::to_string(d) + "]";
    if (!B(ctx, ARG(1)) || !s.on) {
        s = MandoActivo::Scratch{};
        m->pu.set(g, "scratch2", 0.0);
        m->pu.set(g, "scratch2_enable", 0.0);
        return JS_UNDEFINED;
    }
    s.ramp = true;
    s.rampTo = m->pu.get(g, "play") > 0 ? 1.0 + m->pu.get(g, "rate") * m->pu.get(g, "rateRange") : 0.0;
    return JS_UNDEFINED;
}
NATIVE(n_isScratching) {
    MandoActivo* m = M(ctx);
    const int d = (int)D(ctx, ARG(0));
    return JS_NewBool(ctx, m->pu.get("[Channel" + std::to_string(d) + "]", "scratch2_enable") > 0);
}
NATIVE(n_softTakeover) {
    MandoActivo* m = M(ctx);
    auto& s = m->st[ckey(S(ctx, ARG(0)), S(ctx, ARG(1)))];
    s.enabled = B(ctx, ARG(2));
    if (!s.enabled) s = MandoActivo::ST{};
    return JS_UNDEFINED;
}
NATIVE(n_stIgnoreNext) {
    MandoActivo* m = M(ctx);
    m->st[ckey(S(ctx, ARG(0)), S(ctx, ARG(1)))].ignoreNext = true;
    return JS_UNDEFINED;
}
NATIVE(n_stWillIgnore) {
    MandoActivo* m = M(ctx);
    const std::string g = S(ctx, ARG(0)), k = S(ctx, ARG(1));
    auto it = m->st.find(ckey(g, k));
    if (it == m->st.end() || !it->second.enabled) return JS_NewBool(ctx, false);
    return JS_NewBool(ctx, m->stWillIgnore(it->second, g, k, m->pu.behavior(g, k).valueToParam(D(ctx, ARG(2)))));
}
NATIVE(n_brake) {
    MandoActivo* m = M(ctx);
    const int d = (int)D(ctx, ARG(0));
    if (B(ctx, ARG(1))) m->pu.set("[Channel" + std::to_string(d) + "]", "__brake", 1.0);
    return JS_UNDEFINED;
}
NATIVE(n_spinback) {
    MandoActivo* m = M(ctx);
    const int d = (int)D(ctx, ARG(0));
    if (B(ctx, ARG(1))) m->pu.set("[Channel" + std::to_string(d) + "]", "__spinback", 1.0);
    return JS_UNDEFINED;
}
NATIVE(n_softStart) {
    MandoActivo* m = M(ctx);
    const int d = (int)D(ctx, ARG(0));
    if (B(ctx, ARG(1))) m->pu.set("[Channel" + std::to_string(d) + "]", "play", 1.0);
    return JS_UNDEFINED;
}
NATIVE(n_setting) {
    MandoActivo* m = M(ctx);
    const std::string name = S(ctx, ARG(0));
    for (auto& s : m->map.settings) {
        if (s.variable != name) continue;
        if (s.type == "boolean") return JS_NewBool(ctx, s.value == "true" || s.value == "1");
        if (s.type == "integer" || s.type == "real" || s.type == "number") return JS_NewNumber(ctx, std::atof(s.value.c_str()));
        if (s.type == "color") return JS_NewNumber(ctx, (double)std::strtol(s.value.c_str(), nullptr, 0));
        return JS_NewString(ctx, s.value.c_str());
    }
    return JS_UNDEFINED;
}
NATIVE(n_log) {
    MandoActivo* m = M(ctx);
    m->addLog((int)D(ctx, ARG(0)), S(ctx, ARG(1)));
    return JS_UNDEFINED;
}
NATIVE(n_sendShort) {
    M(ctx)->sendShort((int)D(ctx, ARG(0)), (int)D(ctx, ARG(1)), (int)D(ctx, ARG(2)));
    return JS_UNDEFINED;
}
NATIVE(n_sendSysex) {
    MandoActivo* m = M(ctx);
    std::vector<uint8_t> d;
    int64_t n = 0;
    if (JS_GetLength(ctx, ARG(0), &n) == 0)
        for (int64_t i = 0; i < n && i < 65536; ++i) {
            JSValue v = JS_GetPropertyUint32(ctx, ARG(0), (uint32_t)i);
            d.push_back((uint8_t)((int)D(ctx, v) & 0xFF));
            JS_FreeValue(ctx, v);
        }
    m->sendSysex(d);
    return JS_UNDEFINED;
}
NATIVE(n_inputHandler) {
    MandoActivo* m = M(ctx);
    if (!JS_IsFunction(ctx, ARG(2))) return JS_NewInt32(ctx, -1);
    MandoActivo::Handler h{m->nextHandler++, (uint16_t)(((int)D(ctx, ARG(0)) & 0xFF) << 8 | ((int)D(ctx, ARG(1)) & 0x7F)), JS_DupValue(ctx, ARG(2)), true};
    m->handlers.push_back(h);
    return JS_NewInt32(ctx, h.id);
}
NATIVE(n_removeInputHandler) {
    MandoActivo* m = M(ctx);
    const int id = (int)D(ctx, ARG(0));
    for (auto& h : m->handlers) if (h.id == id && h.alive) { h.alive = false; return JS_NewBool(ctx, true); }
    return JS_NewBool(ctx, false);
}
NATIVE(n_player) {
    MandoActivo* m = M(ctx);
    const std::string g = S(ctx, ARG(0));
    if (g.rfind("[Channel", 0) != 0) return JS_UNDEFINED;
    const int d = std::atoi(g.c_str() + 8) - 1;
    if (d < 0 || d > 1) return JS_UNDEFINED;
    std::string title, artist;
    int key = -1;
    if (!m->pu.playerInfo(d, title, artist, key)) return JS_UNDEFINED;
    JSValue a = JS_NewArray(ctx);
    JS_SetPropertyUint32(ctx, a, 0, JS_NewString(ctx, title.c_str()));
    JS_SetPropertyUint32(ctx, a, 1, JS_NewString(ctx, artist.c_str()));
    JS_SetPropertyUint32(ctx, a, 2, JS_NewString(ctx, ""));
    JS_SetPropertyUint32(ctx, a, 3, JS_NewString(ctx, key >= 0 ? keyName(key) : ""));
    return a;
}

int interruptHandler(JSRuntime*, void* opaque) {
    MandoActivo* m = static_cast<MandoActivo*>(opaque);
    return m && m->depth > 0 && nowSec() > m->deadline ? 1 : 0;   // script colgado: se corta
}

} // namespace

bool MandoActivo::start(const std::string& file, const std::string& portName, int device) {
    port = portName;
    dev = device;
    std::string err;
    if (!mandos::loadMapping(file, map, &err)) { addLog(2, "No se pudo leer el mapeo: " + err); return false; }
    loaded = true;
    for (size_t i = 0; i < map.inputs.size(); ++i) {
        const auto& in = map.inputs[i];
        if (in.status == 0xF0) continue;
        const uint16_t k = (uint16_t)(in.status << 8 | ((in.status & 0xF0) == 0xE0 ? 0 : in.midino));
        inIndex.insert({k, i});
    }
    outLast.assign(map.outputs.size(), -1);
    rt = JS_NewRuntime();
    JS_SetMemoryLimit(rt, 96u << 20);
    JS_SetMaxStackSize(rt, 1u << 20);
    JS_SetInterruptHandler(rt, interruptHandler, this);
    ctx = JS_NewContext(rt);
    JS_SetContextOpaque(ctx, this);
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue n = JS_NewObject(ctx);
    struct F { const char* name; JSCFunction* fn; int args; };
    static const F fns[] = {
        {"get", n_get, 2}, {"set", n_set, 3}, {"getParam", n_getParam, 2}, {"setParam", n_setParam, 3}, {"paramForValue", n_paramForValue, 3},
        {"defValue", n_defValue, 2}, {"reset", n_reset, 2}, {"connect", n_connect, 3}, {"disconnect", n_disconnect, 1}, {"connAlive", n_connAlive, 1},
        {"connTrigger", n_connTrigger, 1}, {"disconnectFn", n_disconnectFn, 3}, {"trigger", n_trigger, 2}, {"timer", n_timer, 3},
        {"stopTimer", n_stopTimer, 1}, {"scratchEnable", n_scratchEnable, 6}, {"scratchTick", n_scratchTick, 2}, {"scratchDisable", n_scratchDisable, 2},
        {"isScratching", n_isScratching, 1}, {"softTakeover", n_softTakeover, 3}, {"stIgnoreNext", n_stIgnoreNext, 2}, {"stWillIgnore", n_stWillIgnore, 3},
        {"brake", n_brake, 2}, {"spinback", n_spinback, 2}, {"softStart", n_softStart, 2}, {"setting", n_setting, 1}, {"log", n_log, 2},
        {"sendShort", n_sendShort, 3}, {"sendSysex", n_sendSysex, 1}, {"inputHandler", n_inputHandler, 3}, {"removeInputHandler", n_removeInputHandler, 1},
        {"player", n_player, 1},
    };
    for (const F& f : fns) JS_SetPropertyStr(ctx, n, f.name, JS_NewCFunction(ctx, f.fn, f.name, f.args));
    JS_SetPropertyStr(ctx, global, "__rdj", n);
    JS_FreeValue(ctx, global);
    if (!evalCode(kPrelude, "api-mixxx.js")) return false;
    // Scripts: common-controller-scripts.js siempre primero, después los del XML en orden.
    std::vector<std::string> files = {"common-controller-scripts.js"};
    for (auto& s : map.scripts) if (std::find(files.begin(), files.end(), s.file) == files.end()) files.push_back(s.file);
    bool ok = true;
    for (auto& f : files) {
        std::string code;
        if (!mandos::readFile(f, code)) { addLog(2, "Falta el script " + f); ok = false; continue; }
        ok &= evalCode(code, f);
    }
    for (auto& s : map.scripts)
        if (!s.prefix.empty() && std::find(prefixes.begin(), prefixes.end(), s.prefix) == prefixes.end()) prefixes.push_back(s.prefix);
    // init(nombre del mando, depuración)
    std::string quoted;
    for (char c : portName) { if (c == '\\' || c == '\'') quoted += '\\'; if ((unsigned char)c >= 32) quoted += c; }
    for (auto& pf : prefixes)
        ok &= evalCode("if (typeof " + pf + " !== 'undefined' && " + pf + " && typeof " + pf + ".init === 'function') " + pf + ".init('" + quoted + "', false);",
                       pf + ".init", 5.0);
    flushNotify();
    started = true;
    for (auto& o : map.outputs) watch(o.group, o.key);
    sendOutputs(true);
    return ok;
}

// ================================================================== MandoPuente

MandoPuente::MandoPuente(App& app) : app_(app) {}
MandoPuente::~MandoPuente() {
    terminarSimulacion();
    parar();
}

std::string MandoPuente::mapeoAuto(const std::string& puerto) const { return mandos::bestMatch(puerto); }

// Solo para pruebas y capturas sin mando: RDJ_MANDO_PRUEBA="DDJ-400" simula ese puerto.
static std::string mandoPrueba() {
    wchar_t b[256];
    const DWORD n = GetEnvironmentVariableW(L"RDJ_MANDO_PRUEBA", b, 256);
    return n > 0 && n < 256 ? toUtf8(std::wstring(b, n)) : std::string();
}

void MandoPuente::arrancar(bool avisar) {
    parar();
    const auto& names = app_.midi_.devices();
    std::set<std::string> started;
    for (size_t i = 0; i < names.size(); ++i) {
        const std::string& port = names[i];
        const std::string choice = app_.midi_.mappingChoice(port);
        std::string file = choice == "auto" ? mandos::bestMatch(port) : choice == "ninguno" ? std::string() : choice;
        if (file.empty()) continue;
        // Varios puertos del mismo mando (p. ej. "DDJ-400" y "MIDIIN2 (DDJ-400)"): un solo mapeo.
        if (started.count(file)) continue;
        auto m = std::make_unique<MandoActivo>(*this);
        const bool ok = m->start(file, port, (int)i);
        if (!m->loaded) continue;
        started.insert(file);
        app_.midi_.setIntegrated((int)i, true);
        if (avisar)
            app_.notify(ok ? "Mando reconocido: " + m->map.name + ". Listo para usar (mapeo de Mixxx" + (app_.midi_.hasOutput((int)i) ? ", con luces)." : ").")
                           : "Mando reconocido: " + m->map.name + ", con avisos en su mapeo (mira ⚙ → Mando MIDI).", !ok);
        activos_.push_back(std::move(m));
    }
    const std::string fake = mandoPrueba();
    if (!fake.empty()) {
        const std::string file = mandos::bestMatch(fake);
        if (!file.empty()) {
            auto m = std::make_unique<MandoActivo>(*this);
            m->start(file, fake, -1);
            if (avisar) app_.notify("Mando reconocido: " + m->map.name + ". Listo para usar (mapeo de Mixxx, con luces).");
            activos_.push_back(std::move(m));
        }
    }
}

void MandoPuente::parar() {
    for (auto& m : activos_) {
        m->stop();
        app_.midi_.setIntegrated(m->dev, false);
    }
    activos_.clear();
}

void MandoPuente::tick() {
    const double now = nowSec();
    const float dt = lastTick_ > 0 ? (float)std::clamp(now - lastTick_, 0.0, 0.1) : 0.016f;
    lastTick_ = now;
    for (const MidiEvent& e : app_.midi_.takeIntegrated()) {
        for (auto& m : activos_) {
            if (m->dev != e.device || !m->ctx) continue;
            if (e.status == 0xF0) m->onSysex(e.sysex);
            else m->onShort(e.status, e.data1, e.data2);
        }
    }
    for (auto& m : activos_) if (m->ctx) m->tick(now, dt);
    if (simulado_ && simulado_->ctx) simulado_->tick(now, dt);
    deckTick(dt);
}

std::vector<MandoInfo> MandoPuente::info() const {
    std::vector<MandoInfo> v;
    const auto& names = app_.midi_.devices();
    for (size_t i = 0; i < names.size(); ++i) {
        MandoInfo inf;
        inf.puerto = names[i];
        inf.dispositivo = (int)i;
        inf.eleccion = app_.midi_.mappingChoice(names[i]);
        inf.luces = app_.midi_.hasOutput((int)i);
        for (auto& m : activos_) {
            if (m->dev != (int)i) continue;
            inf.activo = m->started;
            inf.archivo = m->map.file;
            inf.nombre = m->map.name;
            inf.autor = m->map.author;
            inf.descripcion = m->map.description;
            inf.errores = m->errors;
            inf.registro = m->log;
        }
        v.push_back(inf);
    }
    for (auto& m : activos_) {
        if (m->dev >= 0) continue;   // simulado (RDJ_MANDO_PRUEBA)
        MandoInfo inf;
        inf.puerto = m->port;
        inf.eleccion = "auto";
        inf.luces = true;
        inf.activo = m->started;
        inf.archivo = m->map.file;
        inf.nombre = m->map.name;
        inf.autor = m->map.author;
        inf.descripcion = m->map.description;
        inf.errores = m->errors;
        inf.registro = m->log;
        v.push_back(inf);
    }
    return v;
}

void MandoPuente::midiShort(int dev, uint8_t s, uint8_t d1, uint8_t d2) { app_.midi_.sendShortTo(dev, s, d1, d2); }
void MandoPuente::midiSysex(int dev, const std::vector<uint8_t>& d) { app_.midi_.sendSysexTo(dev, d); }
bool MandoPuente::playerInfo(int d, std::string& title, std::string& artist, int& key) const {
    if (d < 0 || d > 1 || !app_.deck_[d].track) return false;
    title = app_.deck_[d].title;
    artist = app_.deck_[d].artist;
    key = app_.deck_[d].meta.key;
    return true;
}

// ------------------------------------------------------------------ pruebas

MandoActivo* MandoPuente::simular(const std::string& file) {
    terminarSimulacion();
    simulado_ = std::make_unique<MandoActivo>(*this);
    simulado_->start(file, "Prueba", -1);
    return simulado_.get();
}

void MandoPuente::simularMidi(MandoActivo* m, uint8_t status, uint8_t d1, uint8_t d2) {
    if (m && m->ctx) m->onShort(status, d1, d2);
}

void MandoPuente::limpiarSalidas(MandoActivo* m) { if (m) m->simOut.clear(); }

void MandoPuente::terminarSimulacion() {
    if (simulado_) simulado_->stop();
    simulado_.reset();
}

std::vector<std::vector<uint8_t>> MandoPuente::salidasSimuladas(MandoActivo* m) const {
    return m ? m->simOut : std::vector<std::vector<uint8_t>>{};
}

MandoPrueba MandoPuente::probar(const std::string& file, bool pulsar) {
    MandoPrueba r;
    r.archivo = file;
    MandoActivo m(*this);
    const bool ok = m.start(file, "Prueba", -1);
    r.nombre = m.map.name;
    r.cargado = m.loaded;
    r.iniciado = ok;
    r.entradas = (int)m.map.inputs.size();
    if (m.ctx && pulsar) {
        // Todos los controles del XML: botones pulsados y soltados, faders a 0, centro y arriba.
        double t = nowSec();
        for (size_t i = 0; i < m.map.inputs.size(); ++i) {
            const auto& in = m.map.inputs[i];
            if (in.status == 0xF0) continue;
            const int op = in.status & 0xF0;
            if (op == 0x90 || op == 0x80) { m.onShort(in.status, in.midino, 0x7F); m.onShort(in.status, in.midino, 0x00); }
            else if (op == 0xB0) {
                for (int v : {0x00, 0x40, 0x41, 0x3F, 0x7F, 0x40}) m.onShort(in.status, in.midino, (uint8_t)v);
            } else if (op == 0xE0) { m.onShort(in.status, 0, 0x40); m.onShort(in.status, 0x7F, 0x7F); m.onShort(in.status, 0, 0); }
            if (i % 16 == 0) { const double now = nowSec(); m.tick(now, (float)std::clamp(now - t, 0.001, 0.05)); t = now; }
        }
        for (int k = 0; k < 20; ++k) { const double now = nowSec(); m.tick(now, 0.016f); }
    }
    for (auto& kv : m.untranslated) r.noTraducidos.push_back(kv.first + " ×" + std::to_string(kv.second));
    r.sinEquivalente = (int)m.untranslated.size();
    std::set<std::string> used;
    for (auto& in : m.map.inputs) if (!(in.opts & mandos::OptScript) && in.status != 0xF0) used.insert(ckey(in.group, in.key));
    for (auto& o : m.map.outputs) used.insert(ckey(o.group, o.key));
    for (auto& kv : m.watches) used.insert(kv.first);
    r.controles = (int)used.size();
    r.salidas = m.sentShort;
    r.sysex = m.sentSysex;
    r.luces = m.ledsOn;
    m.stop();
    r.errores = m.errors;
    r.registro = m.log;
    return r;
}

// ------------------------------------------------------------------ prueba de todos los mapeos

int App::mandosPrueba(const std::wstring& out) {
    if (!mandos_) return 1;
    std::string rep = "== Prueba de los mapeos de mandos incluidos (Mixxx) ==\r\n";
    int ok = 0, conErrores = 0, total = 0, luces = 0;
    int controles = 0, sinEq = 0;
    std::map<std::string, int> faltan;   // controles sin equivalente, en cuántos mapeos
    std::string detalle;
    FILE* prog = _wfopen((out + L".progreso").c_str(), L"wb");
    for (const auto& e : mandos::catalog()) {
        ++total;
        if (prog) { std::fprintf(prog, "%s ...", e.file.c_str()); std::fflush(prog); }
        const double t0 = nowSec();
        const MandoPrueba r = mandos_->probar(e.file, true);
        if (prog) { std::fprintf(prog, " %.2f s\n", nowSec() - t0); std::fflush(prog); }
        // Controles que usa (XML y conexiones del script) sin equivalente en Ramiro DJ.
        int sinEquivalente = 0;
        std::vector<std::string> nt;
        for (auto& x : r.noTraducidos) {
            const std::string key = x.substr(0, x.find(" ×"));
            const size_t c = key.find(',');
            const std::string g = key.substr(0, c), k = key.substr(c + 1);
            // Platos 3 y 4, samplers, micrófono… no existen en Ramiro DJ: no cuentan como fallo de traducción.
            const bool extra = g.rfind("[Channel3", 0) == 0 || g.rfind("[Channel4", 0) == 0 || g.rfind("[Sampler", 0) == 0 || g.rfind("[Preview", 0) == 0 ||
                               g.rfind("[Microphone", 0) == 0 || g.rfind("[Auxiliary", 0) == 0 || g.find("Channel3") != std::string::npos ||
                               g.find("Channel4") != std::string::npos || g.find("EffectUnit3") != std::string::npos || g.find("EffectUnit4") != std::string::npos;
            if (extra) continue;
            ++sinEquivalente;
            ++faltan[key];
            nt.push_back(x);
        }
        controles += r.controles;
        sinEq += sinEquivalente;
        if (r.salidas > 0) ++luces;
        const bool good = r.cargado && r.iniciado && r.errores == 0;
        if (good) ++ok; else ++conErrores;
        char line[512];
        std::snprintf(line, sizeof line, "%s %-45s errores %d · entradas %d · mensajes a luces %d · SysEx %d · sin equivalente %d\r\n", good ? "OK   " : "FALLO",
                      r.nombre.c_str(), r.errores, r.entradas, r.salidas, r.sysex, sinEquivalente);
        detalle += line;
        int shown = 0;
        for (auto& l : r.registro) if (l.rfind("Error", 0) == 0 && shown++ < 4) detalle += "        " + l + "\r\n";
        if (!nt.empty()) {
            std::string s2;
            for (size_t i = 0; i < nt.size() && i < 8; ++i) s2 += (i ? ", " : "") + nt[i];
            detalle += "        sin equivalente: " + s2 + "\r\n";
        }
    }
    char sum[512];
    std::snprintf(sum, sizeof sum, "Mapeos: %d · arrancan sin errores: %d · con errores: %d · mandan luces: %d\r\nControles usados: %d · sin equivalente en DJ Elver Galarga: %d\r\n\r\n",
                  total, ok, conErrores, luces, controles, sinEq);
    rep += sum;
    rep += detalle;
    rep += "\r\n== Controles sin equivalente (en cuántos mapeos) ==\r\n";
    std::vector<std::pair<int, std::string>> fv;
    for (auto& kv : faltan) fv.push_back({kv.second, kv.first});
    std::sort(fv.rbegin(), fv.rend());
    for (auto& [n, k] : fv) rep += std::to_string(n) + "  " + k + "\r\n";
    if (prog) std::fclose(prog);
    if (FILE* f = _wfopen(out.c_str(), L"wb")) { fwrite(rep.data(), 1, rep.size(), f); fclose(f); }
    return conErrores;
}

} // namespace rdj

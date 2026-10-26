// Controles de Mixxx traducidos a Ramiro DJ, para los mapeos de mandos incluidos.
// Cada control tiene un comportamiento (rango y curva, como en Mixxx) y se lee o escribe
// sobre el estado real de Ramiro DJ. Los que no tienen equivalente (samplers, platos 3 y 4,
// micrófono…) se guardan como valores "virtuales" para que los scripts sigan funcionando.
#include "mandos_puente.h"
#include "app.h"
#include "../audio/effects.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace rdj {

namespace {

double db2ratio(double db) { return std::pow(10.0, db / 20.0); }
double ratio2db(double r) { return 20.0 * std::log10(std::max(r, 1e-9)); }

// Colores de los hot cues de Ramiro DJ (los mismos que en ui_frame.cpp), en 0xRRGGBB.
const int kHotRgb[8] = {0x2BE3C0, 0x4691FF, 0x9B6EFF, 0xFF5AAA, 0xFF5C58, 0xFFA64D, 0xFAD750, 0x6EDC6E};

struct GroupRef {
    enum Type { Unknown, Deck, Master, App, Library, Playlist, Eq, QuickFx, FxUnit, FxSlot, AutoDJ, Recording, Skin, Other } type = Unknown;
    int deck = -1;   // 0.. (solo 0 y 1 existen)
    int unit = 0, slot = 0;
};

int numAfter(const std::string& s, size_t pos) { return std::atoi(s.c_str() + pos); }

GroupRef parseGroup(const std::string& g) {
    static std::unordered_map<std::string, GroupRef> cache;
    auto it = cache.find(g);
    if (it != cache.end()) return it->second;
    GroupRef r;
    auto starts = [&](const char* p) { return g.rfind(p, 0) == 0; };
    if (starts("[Channel")) {
        const int n = numAfter(g, 8);
        r.deck = n - 1;
        r.type = g.find("_Stem") == std::string::npos ? GroupRef::Deck : GroupRef::Other;
    } else if (g == "[Master]" || g == "[Main]" || g == "[Mixer]") r.type = GroupRef::Master;
    else if (g == "[App]") r.type = GroupRef::App;
    else if (g == "[Library]") r.type = GroupRef::Library;
    else if (g == "[Playlist]") r.type = GroupRef::Playlist;
    else if (g == "[AutoDJ]") r.type = GroupRef::AutoDJ;
    else if (g == "[Recording]") r.type = GroupRef::Recording;
    else if (g == "[Skin]") r.type = GroupRef::Skin;
    else if (starts("[EqualizerRack1_[Channel")) {
        r.deck = numAfter(g, 24) - 1;
        r.type = g.find("]_Effect1]") != std::string::npos ? GroupRef::Eq : GroupRef::Other;
    } else if (starts("[QuickEffectRack1_[Channel")) {
        r.deck = numAfter(g, 26) - 1;
        r.type = g.find("_Stem") == std::string::npos ? GroupRef::QuickFx : GroupRef::Other;
        if (g.find("]_Effect") != std::string::npos) r.slot = 1;
    } else if (starts("[EffectRack1_EffectUnit")) {
        r.unit = numAfter(g, 23);
        const size_t e = g.find("_Effect", 23);
        if (e != std::string::npos) { r.slot = numAfter(g, e + 7); r.type = GroupRef::FxSlot; }
        else r.type = GroupRef::FxUnit;
        r.deck = (r.unit - 1) % 2;
    } else r.type = GroupRef::Other;
    if (cache.size() < 4096) cache[g] = r;
    return r;
}

bool endsWith(const std::string& s, const char* suf) {
    const size_t n = std::strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}
bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

// "hotcue_12_activate" -> 12 y "activate"
bool splitNumbered(const std::string& k, const char* prefix, double& num, std::string& rest) {
    if (!startsWith(k, prefix)) return false;
    const size_t a = std::strlen(prefix);
    const size_t u = k.find('_', a);
    if (u == std::string::npos) { rest.clear(); num = std::atof(k.c_str() + a); return a < k.size(); }
    num = std::atof(k.substr(a, u - a).c_str());
    rest = k.substr(u + 1);
    return u > a;
}

bool isPushKey(const std::string& k) {
    static const char* const ends[] = {"_activate", "_set", "_clear", "_goto", "_gotoandplay", "_gotoandstop", "_gotoandloop", "_forward",
                                       "_backward", "_up", "_down", "_up_small", "_down_small", "_set_default", "_set_zero", "_set_one",
                                       "_set_minus_one", "_toggle", "_double", "_halve", "_activatecue", "_activateloop", "_setcue", "_setloop",
                                       "_cueloop", "_curpos", "_earlier", "_later"};
    for (const char* e : ends) if (endsWith(k, e)) return true;
    static const char* const starts[] = {"cue_", "beats_", "beatsync", "rate_temp", "rate_perm", "LoadSelected", "LoadTrack", "Select", "Move",
                                         "GoTo", "next_", "prev_", "stars_", "sort_", "reloop", "loop_in", "loop_out", "loop_remove",
                                         "beatloop_activate", "beatlooproll", "beatjump_forward", "beatjump_backward", "intro_", "outro_",
                                         "crossfader_up", "crossfader_down", "AutoDj", "shift_cues", "waveform_zoom_", "hotcue_focus_color"};
    for (const char* s : starts) if (startsWith(k, s)) return true;
    static const char* const exact[] = {"start", "end", "stop", "back", "fwd", "eject", "start_play", "start_stop", "play_stutter", "reverse",
                                        "reverseroll", "bpm_tap", "tempo_tap", "sync_key", "reset_key", "pitch_up", "pitch_down",
                                        "toggle_recording", "fade_now", "skip_next", "add_random_track", "shuffle_playlist", "clear",
                                        "ToggleSelectedSidebarItem", "LoadSelectedIntoFirstStopped", "chain_selector", "loop_double",
                                        "loop_halve", "loop_in_goto", "loop_out_goto", "reloop_exit", "reloop_toggle", "reloop_andstop",
                                        "bpmlock", "update_replaygain_from_pregain", "beats_undo_adjustment", "hotcue_focus"};
    for (const char* e : exact) if (k == e) return true;
    return false;
}

bool isToggleKey(const std::string& k) {
    static const char* const t[] = {"play", "pfl", "keylock", "quantize", "slip_enabled", "sync_enabled", "sync_leader", "sync_master", "repeat",
                                    "mute", "passthrough", "vinylcontrol_enabled", "enabled", "show_maximized_library", "maximize_library",
                                    "show_samplers", "show_effectrack", "show_mixer", "show_parameters", "show_focus", "talkover", "headSplit",
                                    "filter", "flanger", "talkoverDucking", "AutoHotcueColors", "ShowDurationRemaining"};
    for (const char* e : t) if (k == e) return true;
    return startsWith(k, "group_") && endsWith(k, "_enable");
}

} // namespace

// ================================================================== comportamiento

double MxBehavior::valueToParam(double v) const {
    switch (kind) {
    case Pot: return max == min ? 0 : (std::clamp(v, min, max) - min) / (max - min);
    case Audio: {
        if (v <= 0) return 0;
        const double off = db2ratio(minDb);
        if (v < 1.0) return minDb != 0 ? (ratio2db(v + off * (1 - v)) - minDb) / minDb * neutral * -1 : v * neutral;
        if (v == 1.0) return neutral;
        if (v < max) return ratio2db(v) / maxDb * (1 - neutral) + neutral;
        return 1.0;
    }
    case Push: case Toggle: case PowerWin: return v > 0 ? 1 : 0;
    default: return v;
    }
}

double MxBehavior::paramToValue(double p) const {
    switch (kind) {
    case Pot: return min + std::clamp(p, 0.0, 1.0) * (max - min);
    case Audio: {
        if (p <= 0) return 0;
        const double off = db2ratio(minDb);
        if (p < neutral) {
            if (minDb != 0) { const double db = (p * minDb / (neutral * -1)) + minDb; return (db2ratio(db) - off) / (1 - off); }
            return p / neutral;
        }
        if (p == neutral) return 1.0;
        if (p < 1.0) return db2ratio((p - neutral) * maxDb / (1 - neutral));
        return db2ratio(maxDb);
    }
    case Push: case Toggle: case PowerWin: return p > 0 ? 1 : 0;
    default: return p;
    }
}

double MxBehavior::midiToParam(double m) const {
    if (kind == Encoder) return m;
    if (kind == Pot || kind == Audio) return m > 64 ? (m - 1) / 126.0 : m / 128.0;   // 64 = centro exacto
    return m / 127.0;
}

double MxBehavior::valueToMidi(double v) const {
    if (kind == Encoder) return v;
    if (kind == Pot || kind == Audio) { const double n = valueToParam(v); return n > 0.5 ? n * 126 + 1 : n * 128.0; }
    return valueToParam(v) * 127.0;
}

MxBehavior MandoPuente::behavior(const std::string& g, const std::string& k) const {
    MxBehavior b;
    const GroupRef r = parseGroup(g);
    auto pot = [&](double mn, double mx, double def) { b.kind = MxBehavior::Pot; b.min = mn; b.max = mx; b.def = def; return b; };
    auto audio = [&](double minDb, double maxDb, double neutral) {
        b.kind = MxBehavior::Audio; b.minDb = minDb; b.maxDb = maxDb; b.neutral = neutral; b.min = 0; b.max = db2ratio(maxDb); b.def = 1; return b;
    };
    auto enc = [&] { b.kind = MxBehavior::Encoder; b.min = -1e9; b.max = 1e9; return b; };
    switch (r.type) {
    case GroupRef::Deck:
        if (k == "volume") return audio(-20, 0, 1);
        if (k == "pregain") return audio(-12, 12, 0.5);
        if (k == "filterLow" || k == "filterMid" || k == "filterHigh") return audio(-12, ratio2db(4), 0.5);
        if (k == "rate") return pot(-1, 1, 0);
        if (k == "rateRange") return pot(0.01, 4, 0.08);
        if (k == "pitch") return pot(-6, 6, 0);
        if (k == "pitch_adjust") return pot(-3, 3, 0);
        if (k == "playposition") return pot(-0.14, 1.14, 0);
        if (k == "bpm") return pot(0, 300, 0);
        if (k == "rateSearch") return pot(-300, 300, 0);
        if (k == "vu_meter" || k == "vu_meter_left" || k == "vu_meter_right" || startsWith(k, "VuMeter")) return pot(0, 1, 0);
        if (k == "beatloop_size" || k == "beatjump_size") { b.def = 4; return b; }
        if (k == "orientation") { b.def = r.deck == 0 ? 0 : r.deck == 1 ? 2 : 1; return b; }
        if (k == "rate_dir") { b.def = 1; return b; }
        if (k == "loop_move" || k == "beatjump") return enc();
        if (endsWith(k, "Kill")) { b.kind = MxBehavior::PowerWin; b.max = 1; return b; }
        if (startsWith(k, "hotcue_") && (endsWith(k, "_position") || endsWith(k, "_color"))) { b.def = -1; return b; }
        if (k == "cue_point" || k == "loop_start_position" || k == "loop_end_position") { b.def = -1; return b; }
        break;
    case GroupRef::Master:
        if (k == "crossfader" || k == "balance") return pot(-1, 1, 0);
        if (k == "headMix") return pot(-1, 1, -1);
        if (k == "gain" || k == "volume" || k == "headGain" || k == "headVolume" || k == "booth_gain") return audio(-14, 14, 0.5);
        if (k == "duckStrength") return pot(0, 1, 0.5);
        if (startsWith(k, "vu_meter") || startsWith(k, "VuMeter")) return pot(0, 1, 0);
        if (k == "num_decks") { b.def = 2; return b; }
        if (k == "num_samplers") { b.def = 4; return b; }
        break;
    case GroupRef::App:
        if (k == "num_decks") { b.def = 2; return b; }
        if (k == "num_samplers") { b.def = 4; return b; }
        if (k == "num_preview_decks" || k == "num_microphones" || k == "num_auxiliaries") { b.def = 1; return b; }
        if (k == "samplerate") { b.def = 48000; return b; }
        break;
    case GroupRef::Library:
        if (k == "MoveVertical" || k == "MoveHorizontal" || k == "ScrollVertical" || k == "ScrollHorizontal" || k == "Move" || k == "Scroll" ||
            k == "MoveFocus" || k == "font_size_knob")
            return enc();
        break;
    case GroupRef::Playlist:
        if (k == "SelectTrackKnob" || k == "SelectPlaylist") return enc();
        break;
    case GroupRef::Eq:
        if (startsWith(k, "parameter") && k.size() <= 10) return audio(-12, ratio2db(4), 0.5);
        if (startsWith(k, "button_parameter") && k.find('_', 16) == std::string::npos) { b.kind = MxBehavior::PowerWin; b.max = 1; return b; }
        break;
    case GroupRef::QuickFx:
        if (k == "super1" || k == "meta" || startsWith(k, "parameter")) return pot(0, 1, 0.5);
        if (k == "chain_preset_selector") return enc();
        break;
    case GroupRef::FxUnit:
        if (k == "super1") return pot(0, 1, 0);
        if (k == "mix") return pot(0, 1, 1);
        if (k == "chain_preset_selector" || k == "focused_effect") return enc();
        break;
    case GroupRef::FxSlot:
        if (k == "meta" || (startsWith(k, "parameter") && k.find('_') == std::string::npos)) return pot(0, 1, 0.5);
        if (k == "effect_selector") return enc();
        break;
    default: break;
    }
    if (isToggleKey(k)) { b.kind = MxBehavior::Toggle; b.max = 1; b.def = (k == "enabled" && (r.type == GroupRef::Eq || r.type == GroupRef::QuickFx)) ? 1 : 0; return b; }
    if (isPushKey(k)) { b.kind = MxBehavior::Push; b.max = 1; return b; }
    if (startsWith(k, "beatloop_") || startsWith(k, "beatjump_") || startsWith(k, "hotcue_") || startsWith(k, "loop_move_")) {
        if (endsWith(k, "_enabled") || endsWith(k, "_status") || endsWith(k, "_type")) return b;
        b.kind = MxBehavior::Push; b.max = 1; return b;
    }
    return b;
}

// ================================================================== lectura y escritura

void MandoPuente::setPending(const std::string& g, const std::string& k, double v) {
    pending_[g + "," + k] = Pending{v, app_.now_ + 0.6};
}

double MandoPuente::get(const std::string& g, const std::string& k) {
    double v;
    if (readReal(g, k, v)) {
        if (!pending_.empty()) {
            auto it = pending_.find(g + "," + k);
            if (it != pending_.end()) {
                if (it->second.value == v || app_.now_ > it->second.until) pending_.erase(it);   // ya aplicado (o caducado)
                else return it->second.value;
            }
        }
        return v;
    }
    auto it = virtual_.find(g + "," + k);
    return it != virtual_.end() ? it->second : behavior(g, k).def;
}

void MandoPuente::set(const std::string& g, const std::string& k, double v) {
    if (!std::isfinite(v)) return;
    if (writeReal(g, k, v)) return;
    virtual_[g + "," + k] = v;
}

bool MandoPuente::real(const std::string& g, const std::string& k) const {
    double v;
    return const_cast<MandoPuente*>(this)->readReal(g, k, v);
}

// Controles de Mixxx que Ramiro DJ no tiene pero que no hacen falta para mezclar: se guardan
// (los scripts los leen y los vuelven a poner) y no cuentan como "sin equivalente".
bool MandoPuente::known(const std::string& g, const std::string& k) const {
    if (real(g, k)) return true;
    const GroupRef r = parseGroup(g);
    static const char* const anyGroup[] = {"show_focus", "focused_effect", "show_parameters", "controller_input_active", "chain_selector",
                                           "mix_mode", "show", "waveform_zoom", "waveform_zoom_up", "waveform_zoom_down", "waveform_zoom_set_default",
                                           "sort_column_toggle", "sort_column", "orientation", "passthrough", "vinylcontrol_enabled", "vinylcontrol_mode",
                                           "vinylcontrol_cueing", "vinylcontrol_status", "repeat", "mute", "headSplit", "balance", "talkoverDucking",
                                           "booth_gain", "duckStrength", "show_samplers", "show_mixer", "show_effectrack", "maximize_library",
                                           "parameter2", "parameter3", "parameter4", "button_parameter1", "button_parameter2", "button_parameter3",
                                           "button_parameter4", "ShowDurationRemaining", "touch_shift", "AutoHotcueColors", "xFaderCurve",
                                           "xFaderCalibration", "xFaderMode", "xFaderReverse", "lfoPeriod", "lfoDelay", "num_parameters",
                                           "MoveFocus", "MoveFocusForward", "MoveFocusBackward", "font_size_knob", "slip_enabled", "hotcue_focus"};
    for (const char* x : anyGroup) if (k == x) return true;
    if (r.type == GroupRef::FxUnit && startsWith(k, "group_[")) return true;   // asignar a máster, auriculares, platos 3 y 4
    if (r.type == GroupRef::Skin && startsWith(k, "show_")) return true;
    if (g.find("_Stem") != std::string::npos) return true;                     // Ramiro DJ no carga stems en los platos
    if (r.type == GroupRef::QuickFx && (k == "next_chain_preset" || k == "prev_chain_preset" || k == "loaded_chain_preset")) return true;
    return false;
}

bool MandoPuente::readReal(const std::string& g, const std::string& k, double& v) {
    App& a = app_;
    const GroupRef r = parseGroup(g);
    double num = 0;
    std::string rest;
    switch (r.type) {
    case GroupRef::Deck: {
        if (r.deck < 0 || r.deck > 1) return false;
        const int d = r.deck;
        const DeckUi& dk = a.deck_[d];
        const DeckParams& p = a.engine_.deck[d];
        const DeckStatus& st = a.engine_.deckStatus[d];
        const bool has = dk.track != nullptr;
        const double sr = has ? dk.track->sampleRate : 48000.0;
        const double len = a.deckLenSec(d), pos = a.deckPosSec(d);
        const bool playing = st.playing.load();
        if (k == "play" || k == "play_latched") { v = playing ? 1 : 0; return true; }
        if (k == "play_indicator") { v = playing ? 1 : 0; return true; }
        if (k == "track_loaded" || k == "loaded") { v = has ? 1 : 0; return true; }
        if (k == "track_samples") { v = has ? (double)dk.track->frames * 2.0 : 0; return true; }
        if (k == "track_samplerate") { v = has ? sr : 0; return true; }
        if (k == "duration") { v = len; return true; }
        if (k == "playposition") { v = len > 0 ? pos / len : 0; return true; }
        if (k == "cue_point") { v = has ? st.cueFrames.load() * 2.0 : -1; return true; }
        if (k == "cue_indicator") {
            if (!has || playing) { v = 0; return true; }
            const bool atCue = std::fabs(st.posFrames.load() - st.cueFrames.load()) < sr * 0.02;
            v = atCue ? 1 : (std::fmod(a.now_, 1.0) < 0.5 ? 1 : 0);
            return true;
        }
        if (k == "cue_default" || k == "cue_cdj" || k == "cue_play" || k == "cue_preview" || k == "cue_simple") { v = dk.mxCueHeld ? 1 : 0; return true; }
        if (startsWith(k, "hotcue_") && splitNumbered(k, "hotcue_", num, rest)) {
            const int i = (int)num - 1;
            const double h = (i >= 0 && i < kNumHotCues) ? st.hotCues[i].load() : -1;
            if (rest == "enabled" || rest == "status") { v = h >= 0 ? 1 : 0; return true; }
            if (rest == "position") { v = h >= 0 ? h * 2.0 : -1; return true; }
            if (rest == "color") { v = h >= 0 && i < 8 ? kHotRgb[i] : -1; return true; }
            if (rest == "type") { v = h >= 0 ? 1 : 0; return true; }
            if (rest == "activate" || rest == "set" || rest == "clear" || rest == "goto" || rest == "gotoandplay" || rest == "gotoandstop" ||
                rest == "activatecue" || rest == "setcue" || rest == "gotoandloop") { v = 0; return true; }
            return false;
        }
        if (k == "hotcue_focus") { v = -1; return true; }
        if (k == "loop_enabled") { v = st.loopActive.load() ? 1 : 0; return true; }
        if (k == "loop_start_position") { v = st.loopIn.load() >= 0 ? st.loopIn.load() * 2.0 : -1; return true; }
        if (k == "loop_end_position") { v = st.loopOut.load() >= 0 ? st.loopOut.load() * 2.0 : -1; return true; }
        if (k == "beatloop_size") { v = dk.mxBeatloopSize; return true; }
        if (k == "beatjump_size") { v = dk.mxBeatjumpSize; return true; }
        if (startsWith(k, "beatloop_") && endsWith(k, "_enabled")) {
            const double x = std::atof(k.c_str() + 9);
            const double bl = a.beatSec(d) * sr;
            const double curLen = st.loopOut.load() - st.loopIn.load();
            v = (st.loopActive.load() && bl > 0 && std::fabs(curLen - bl * x) < 2.0) ? 1 : 0;
            return true;
        }
        if (k == "bpm" || k == "visual_bpm") { v = a.engine_.effectiveBpm(d); return true; }
        if (k == "file_bpm" || k == "local_bpm") { v = dk.meta.bpm; return true; }
        if (k == "rate") { const float rr = a.pitchRange(d); v = rr > 0 ? std::clamp(dk.pitch / rr, -1.f, 1.f) : 0; return true; }
        if (k == "rateRange") { v = a.pitchRange(d); return true; }
        if (k == "rate_dir") { v = 1; return true; }
        if (k == "rateEngine") { v = playing ? 1.0 + dk.pitch : 0; return true; }
        if (k == "keylock") { v = p.keyLock.load() ? 1 : 0; return true; }
        if (k == "stem_vocal_enabled" || k == "stem_1_enabled") { v = !p.stemMute[0].load() ? 1 : 0; return true; }
        if (k == "stem_drums_enabled" || k == "stem_2_enabled") { v = !p.stemMute[1].load() ? 1 : 0; return true; }
        if (k == "stem_bass_enabled"  || k == "stem_3_enabled") { v = !p.stemMute[2].load() ? 1 : 0; return true; }
        if (k == "stem_inst_enabled"  || k == "stem_4_enabled") { v = !p.stemMute[3].load() ? 1 : 0; return true; }
        if (k == "pitch" || k == "pitch_adjust") { v = p.keyShift.load(); return true; }
        if (k == "key" || k == "visual_key" || k == "file_key") { v = dk.meta.key >= 0 ? dk.meta.key + 1 : 0; return true; }
        if (k == "volume") { v = MxBehavior{MxBehavior::Audio, 0, 1, 1, -20, 0, 1}.paramToValue(p.fader.load()); return true; }
        if (k == "pregain") { v = db2ratio(p.gainDb.load()); return true; }
        if (k == "pfl") { v = p.pfl.load() ? 1 : 0; return true; }
        if (k == "quantize") { v = p.quantize.load() ? 1 : 0; return true; }
        if (k == "slip_enabled") { v = p.slip.load() ? 1 : 0; return true; }
        if (k == "reverse") { v = p.reverse.load() ? 1 : 0; return true; }
        if (k == "sync_enabled") { v = dk.sync ? 1 : 0; return true; }
        if (k == "sync_leader" || k == "sync_master") { v = dk.sync && a.syncMaster_ == d ? 1 : 0; return true; }
        if (k == "sync_mode") { v = !dk.sync ? 0 : a.syncMaster_ == d ? 2 : 1; return true; }
        if (k == "end_of_track") { v = a.endWarning(d) ? 1 : 0; return true; }
        if (k == "scratch2_enable") { v = p.scratching.load() ? 1 : 0; return true; }
        if (k == "scratch2") { v = p.scratchVelocity.load(); return true; }
        if (k == "vu_meter" || k == "VuMeter" || k == "vu_meter_left" || k == "VuMeterL" || k == "vu_meter_right" || k == "VuMeterR") {
            const float x = (k == "vu_meter_left" || k == "VuMeterL") ? dk.meterL : (k == "vu_meter_right" || k == "VuMeterR") ? dk.meterR : std::max(dk.meterL, dk.meterR);
            v = std::log10(32767.0 * x + 1.0) / std::log10(32768.0);
            return true;
        }
        if (k == "peak_indicator" || k == "PeakIndicator" || k == "peak_indicator_left" || k == "PeakIndicatorL" || k == "peak_indicator_right" || k == "PeakIndicatorR") {
            v = std::max(dk.meterL, dk.meterR) >= 0.99f ? 1 : 0;
            return true;
        }
        if (k == "beat_active" || k == "beat_distance") {
            const double bs = a.beatSec(d);
            if (bs <= 0 || !has) { v = 0; return true; }
            double ph = std::fmod(pos - dk.meta.gridAnchorSec, bs) / bs;
            if (ph < 0) ph += 1;
            v = k == "beat_distance" ? ph : (playing && (ph < 0.2 || ph > 0.95) ? 1 : 0);
            return true;
        }
        if (k == "filterLow" || k == "filterMid" || k == "filterHigh") {
            const float e = k == "filterLow" ? p.eqLow.load() : k == "filterMid" ? p.eqMid.load() : p.eqHigh.load();
            v = MxBehavior{MxBehavior::Audio, 0, 4, 1, -12, ratio2db(4), 0.5}.paramToValue(e);
            return true;
        }
        if (k == "filterLowKill" || k == "filterMidKill" || k == "filterHighKill") {
            v = dk.mxKill[k == "filterLowKill" ? 0 : k == "filterMidKill" ? 1 : 2] ? 1 : 0;
            return true;
        }
        if (k == "flanger") { v = p.fxOn.load() && p.fxType.load() == (int)FxType::Flanger ? 1 : 0; return true; }
        if (k == "stem_count") { v = 0; return true; }
        if (k == "track_color") { v = -1; return true; }
        if (k == "jog" || k == "wheel") { v = 0; return true; }
        if (isPushKey(k)) { v = (k == "back" ? dk.mxSeekDir < 0 : k == "fwd" ? dk.mxSeekDir > 0 : false) ? 1 : 0; return true; }
        if (startsWith(k, "beatloop_") || startsWith(k, "beatjump_") || startsWith(k, "beatlooproll_") || startsWith(k, "loop_move")) { v = 0; return true; }
        if (k == "beatjump" || k == "loop_scale" || k == "beatloop" || k == "NextTrack" || k == "PrevTrack") { v = 0; return true; }
        return false;
    }
    case GroupRef::Master: {
        if (k == "crossfader") { v = a.engine_.mixer.crossfader.load() * 2.0 - 1.0; return true; }
        if (k == "headMix") { v = a.engine_.mixer.hpMix.load() * 2.0 - 1.0; return true; }
        if (k == "gain" || k == "volume") { v = MxBehavior{MxBehavior::Audio, 0, 5, 1, -14, 14, 0.5}.paramToValue(a.engine_.mixer.masterVol.load()); return true; }
        if (k == "headGain" || k == "headVolume") { v = MxBehavior{MxBehavior::Audio, 0, 5, 1, -14, 14, 0.5}.paramToValue(a.engine_.mixer.hpVol.load()); return true; }
        if (startsWith(k, "vu_meter") || startsWith(k, "VuMeter")) {
            const float x = (endsWith(k, "left") || endsWith(k, "L")) ? a.masterMeter_[0] : (endsWith(k, "right") || endsWith(k, "R")) ? a.masterMeter_[1]
                                                                                                                            : std::max(a.masterMeter_[0], a.masterMeter_[1]);
            v = std::log10(32767.0 * x + 1.0) / std::log10(32768.0);
            return true;
        }
        if (startsWith(k, "peak_indicator") || startsWith(k, "PeakIndicator")) { v = a.now_ < a.clipFlashUntil_ ? 1 : 0; return true; }
        if (k == "num_decks") { v = 2; return true; }
        if (k == "num_samplers") { v = 4; return true; }
        if (k == "maximize_library") { v = a.mode_ == 2 ? 1 : 0; return true; }
        if (k == "enabled" || k == "headEnabled") { v = 1; return true; }
        if (k == "samplerate") { v = 48000; return true; }
        if (k == "crossfader_up" || k == "crossfader_down" || k == "crossfader_up_small" || k == "crossfader_down_small" || k == "crossfader_set_default" ||
            k == "gain_set_default" || k == "headGain_set_default" || k == "headMix_set_default") { v = 0; return true; }
        return false;
    }
    case GroupRef::App:
        if (k == "num_decks") { v = 2; return true; }
        if (k == "num_samplers") { v = 4; return true; }
        if (k == "num_preview_decks" || k == "num_microphones" || k == "num_auxiliaries") { v = 1; return true; }
        if (k == "samplerate") { v = 48000; return true; }
        if (k == "indicator_250ms") { v = std::fmod(a.now_, 0.5) < 0.25 ? 1 : 0; return true; }
        if (k == "indicator_500ms") { v = std::fmod(a.now_, 1.0) < 0.5 ? 1 : 0; return true; }
        if (k == "gui_tick_50ms_period_s" || k == "gui_tick_full_period_s") { v = a.now_; return true; }
        return false;
    case GroupRef::Library:
        if (k == "MoveVertical" || k == "MoveUp" || k == "MoveDown" || k == "ScrollVertical" || k == "ScrollUp" || k == "ScrollDown" || k == "GoToItem" ||
            k == "MoveFocusForward" || k == "MoveFocusBackward" || k == "MoveFocus" || k == "MoveLeft" || k == "MoveRight" || k == "MoveHorizontal" ||
            k == "AutoDjAddBottom" || k == "AutoDjAddTop")
            { v = 0; return true; }
        return false;
    case GroupRef::Playlist:
        if (k == "SelectNextTrack" || k == "SelectPrevTrack" || k == "SelectTrackKnob" || k == "LoadSelectedIntoFirstStopped" || k == "AutoDjAddBottom" ||
            k == "AutoDjAddTop" || k == "SelectNextPlaylist" || k == "SelectPrevPlaylist" || k == "SelectPlaylist" || k == "ToggleSelectedSidebarItem")
            { v = 0; return true; }
        return false;
    case GroupRef::Eq: {
        if (r.deck < 0 || r.deck > 1) return false;
        const DeckParams& p = a.engine_.deck[r.deck];
        const DeckUi& dk = a.deck_[r.deck];
        if (k == "parameter1" || k == "parameter2" || k == "parameter3") {
            const float e = k == "parameter1" ? p.eqLow.load() : k == "parameter2" ? p.eqMid.load() : p.eqHigh.load();
            v = MxBehavior{MxBehavior::Audio, 0, 4, 1, -12, ratio2db(4), 0.5}.paramToValue(e);
            return true;
        }
        if (k == "button_parameter1" || k == "button_parameter2" || k == "button_parameter3") { v = dk.mxKill[k.back() - '1'] ? 1 : 0; return true; }
        if (k == "enabled" || k == "loaded") { v = 1; return true; }
        // Ecualizador de tres bandas con un botón de corte por banda (como el de Mixxx por defecto).
        if (k == "num_parameters" || k == "num_button_parameters") { v = 3; return true; }
        return false;
    }
    case GroupRef::QuickFx: {
        if (r.deck < 0 || r.deck > 1) return false;
        const DeckParams& p = a.engine_.deck[r.deck];
        if (k == "super1" || (r.slot && (k == "meta" || k == "parameter1"))) { v = (p.filter.load() + 1.0) * 0.5; return true; }
        if (k == "enabled") { v = 1; return true; }
        if (k == "super1_set_default" || k == "chain_preset_selector" || k == "next_chain" || k == "prev_chain") { v = 0; return true; }
        if (r.slot && k == "loaded") { v = 1; return true; }
        if (r.slot && k == "num_parameters") { v = 1; return true; }          // el filtro: una rueda
        if (r.slot && k == "num_button_parameters") { v = 0; return true; }
        return false;
    }
    case GroupRef::FxUnit: {
        if (r.deck < 0 || r.deck > 1) return false;
        const DeckParams& p = a.engine_.deck[r.deck];
        if (k == "enabled") { v = p.fxOn.load() ? 1 : 0; return true; }
        if (k == "mix" || k == "super1") { v = p.fxAmount.load(); return true; }
        if (startsWith(k, "group_[Channel") && endsWith(k, "]_enable")) {
            const int dd = std::atoi(k.c_str() + 14) - 1;
            if (dd < 0 || dd > 1) return false;
            v = a.engine_.deck[dd].fxOn.load() ? 1 : 0;
            return true;
        }
        if (k == "loaded_chain_preset") { v = p.fxType.load(); return true; }
        if (k == "num_effectslots") { v = 3; return true; }
        if (k == "next_chain" || k == "prev_chain" || k == "next_chain_preset" || k == "prev_chain_preset" || k == "chain_preset_selector" || k == "clear") { v = 0; return true; }
        return false;
    }
    case GroupRef::FxSlot: {
        if (r.deck < 0 || r.deck > 1) return false;
        const DeckParams& p = a.engine_.deck[r.deck];
        const bool first = r.slot == 1;
        if (k == "enabled") { v = first && p.fxOn.load() ? 1 : 0; return true; }
        if (k == "loaded") { v = first && p.fxType.load() != 0 ? 1 : 0; return true; }
        if (k == "meta" || k == "parameter1") { v = first ? p.fxAmount.load() : 0.5; return true; }
        if (k == "loaded_effect") { v = first ? p.fxType.load() : 0; return true; }
        if (k == "num_parameters") { v = first ? 1 : 0; return true; }
        if (k == "num_button_parameters") { v = 0; return true; }
        if (k == "effect_selector" || k == "next_effect" || k == "prev_effect" || k == "clear") { v = 0; return true; }
        return false;
    }
    case GroupRef::AutoDJ:
        if (k == "enabled") { v = a.automixOn_ ? 1 : 0; return true; }
        return false;
    case GroupRef::Recording:
        if (k == "status") { v = a.recorder_.active() ? 2 : 0; return true; }
        if (k == "toggle_recording") { v = 0; return true; }
        return false;
    case GroupRef::Skin:
        if (k == "show_maximized_library") { v = a.mode_ == 2 ? 1 : 0; return true; }
        return false;
    default:
        if (g == "[Flanger]" && k == "lfoDepth") { v = a.engine_.deck[0].fxAmount.load(); return true; }
        return false;
    }
}

bool MandoPuente::writeReal(const std::string& g, const std::string& k, double v) {
    App& a = app_;
    const GroupRef r = parseGroup(g);
    const bool on = v > 0;
    double num = 0;
    std::string rest;
    switch (r.type) {
    case GroupRef::Deck: {
        if (r.deck < 0 || r.deck > 1) return false;
        const int d = r.deck;
        DeckUi& dk = a.deck_[d];
        DeckParams& p = a.engine_.deck[d];
        DeckStatus& st = a.engine_.deckStatus[d];
        const bool has = dk.track != nullptr;
        const double sr = has ? dk.track->sampleRate : 48000.0;
        auto seekSec = [&](double s) { if (has) a.send(d, Cmd::Seek, std::max(0.0, s) * sr); };
        // play idempotente: no se repite la orden mientras el motor aún no la ha aplicado.
        auto play = [&](bool want) {
            if (!has) return;
            const double eff = get(g, "play");
            if (want && eff == 0) { if (st.playing.load()) a.send(d, Cmd::Play); else a.togglePlay(d); setPending(g, "play", 1); }
            else if (!want && (eff != 0 || st.playing.load())) { a.send(d, Cmd::Pause); setPending(g, "play", 0); }
        };
        auto bpmSet = [&](double nb) { if (nb >= 30 && nb <= 300) { dk.meta.bpm = nb; dk.meta.gridSrc = 3; a.pushGrid(d); a.library_.updateMeta(dk.path, dk.meta); } };
        if (k == "play") { play(on); return true; }
        if (k == "play_latched" || k == "play_indicator" || k == "track_loaded" || k == "track_samples" || k == "track_samplerate" || k == "duration" ||
            k == "file_bpm" || k == "local_bpm" || k == "rateEngine" || k == "end_of_track" || k == "beat_active" || k == "beat_distance" || k == "cue_indicator" ||
            k == "rate_dir" || k == "stem_count" || k == "track_color" || k == "visual_bpm" || k == "sync_mode" || k == "file_key")
            return true;   // solo lectura
        if (k == "stop") { if (on) play(false); return true; }
        if (k == "play_stutter") { if (on && has) { a.send(d, Cmd::Seek, st.cueFrames.load()); play(true); } return true; }
        if (k == "cue_default" || k == "cue_cdj" || k == "cue_play" || k == "cue_simple" || k == "cue_preview") {
            if (!has) return true;
            if (on && !dk.mxCueHeld) { dk.mxCueHeld = true; a.send(d, Cmd::CueDown); }
            else if (!on && dk.mxCueHeld) { dk.mxCueHeld = false; a.send(d, Cmd::CueUp); }
            return true;
        }
        if (k == "cue_gotoandplay") { if (on && has) { a.send(d, Cmd::Seek, st.cueFrames.load()); play(true); } return true; }
        if (k == "cue_gotoandstop") { if (on && has) { play(false); a.send(d, Cmd::Seek, st.cueFrames.load()); } return true; }
        if (k == "cue_goto") { if (on && has) a.send(d, Cmd::Seek, st.cueFrames.load()); return true; }
        if (k == "cue_set") { if (on && has) a.send(d, Cmd::SetCueHere); return true; }
        if (k == "cue_clear") { if (on && has) a.send(d, Cmd::SetCueAt, 0.0); return true; }
        if (k == "cue_point") { if (has && v >= 0) a.send(d, Cmd::SetCueAt, v / 2.0); return true; }
        if (k == "start") { if (on) seekSec(0); return true; }
        if (k == "start_play") { if (on) { seekSec(0); play(true); } return true; }
        if (k == "start_stop") { if (on) { play(false); seekSec(0); } return true; }
        if (k == "end") { if (on) seekSec(a.deckLenSec(d) - 0.05); return true; }
        if (k == "back") { dk.mxSeekDir = on ? -1 : (dk.mxSeekDir < 0 ? 0 : dk.mxSeekDir); return true; }
        if (k == "fwd") { dk.mxSeekDir = on ? 1 : (dk.mxSeekDir > 0 ? 0 : dk.mxSeekDir); return true; }
        if (k == "playposition") { seekSec(std::clamp(v, 0.0, 1.0) * a.deckLenSec(d)); return true; }
        if (startsWith(k, "hotcue_") && splitNumbered(k, "hotcue_", num, rest)) {
            const int i = (int)num - 1;
            if (i < 0 || i >= kNumHotCues) return rest == "activate" || rest == "set" || rest == "clear" || rest == "goto" || rest == "gotoandplay" ||
                                                   rest == "gotoandstop" || rest == "enabled" || rest == "status" || rest == "position" || rest == "color";
            const double h = st.hotCues[i].load();
            if (!has) return true;
            const std::string en = "hotcue_" + std::to_string(i + 1) + "_enabled", stt = "hotcue_" + std::to_string(i + 1) + "_status";
            if (rest == "activate" || rest == "activatecue" || rest == "activateloop" || rest == "cueloop") {
                if (on) { if (h < 0) { setPending(g, en, 1); setPending(g, stt, 1); } a.send(d, Cmd::HotCue, 0, i); }
                return true;
            }
            if (rest == "set" || rest == "setcue" || rest == "setloop") { if (on) { a.send(d, Cmd::SetHotCueAt, st.posFrames.load(), i); setPending(g, en, 1); setPending(g, stt, 1); } return true; }
            if (rest == "clear") { if (on) { a.send(d, Cmd::HotCueDelete, 0, i); dk.meta.hotCueNames[i].clear(); setPending(g, en, 0); setPending(g, stt, 0); } return true; }
            if (rest == "goto") { if (on && h >= 0) a.send(d, Cmd::Seek, h); return true; }
            if (rest == "gotoandplay" || rest == "gotoandloop") { if (on && h >= 0) { a.send(d, Cmd::Seek, h); play(true); } return true; }
            if (rest == "gotoandstop") { if (on && h >= 0) { play(false); a.send(d, Cmd::Seek, h); } return true; }
            if (rest == "position") { if (v >= 0) a.send(d, Cmd::SetHotCueAt, v / 2.0, i); else a.send(d, Cmd::HotCueDelete, 0, i); return true; }
            return rest == "enabled" || rest == "status" || rest == "color" || rest == "type";
        }
        if (k == "loop_in") { if (on && has) a.send(d, Cmd::LoopIn); return true; }
        if (k == "loop_out") { if (on && has) a.send(d, Cmd::LoopOut); return true; }
        if (k == "reloop_exit" || k == "reloop_toggle") {
            if (on && has) { const bool act = get(g, "loop_enabled") > 0; a.send(d, act ? Cmd::LoopExit : Cmd::Reloop); setPending(g, "loop_enabled", act ? 0 : 1); }
            return true;
        }
        if (k == "reloop_andstop") { if (on && has) { a.send(d, Cmd::Reloop); setPending(g, "loop_enabled", 1); play(false); } return true; }
        if (k == "loop_enabled") { if (has && on != (get(g, "loop_enabled") > 0)) { a.send(d, on ? Cmd::Reloop : Cmd::LoopExit); setPending(g, k, on ? 1 : 0); } return true; }
        if (k == "loop_remove") { if (on && has && st.loopActive.load()) a.send(d, Cmd::LoopExit); return true; }
        if (k == "loop_halve") { if (on) { dk.mxBeatloopSize = std::max(1.0 / 32, dk.mxBeatloopSize / 2); if (has && st.loopActive.load()) a.send(d, Cmd::LoopHalve); } return true; }
        if (k == "loop_double") { if (on) { dk.mxBeatloopSize = std::min(512.0, dk.mxBeatloopSize * 2); if (has && st.loopActive.load()) a.send(d, Cmd::LoopDouble); } return true; }
        if (k == "loop_in_goto") { if (on && st.loopIn.load() >= 0) a.send(d, Cmd::Seek, st.loopIn.load()); return true; }
        if (k == "loop_out_goto") { if (on && st.loopOut.load() >= 0) a.send(d, Cmd::Seek, st.loopOut.load()); return true; }
        if (k == "loop_scale") { if (v > 0 && has && st.loopActive.load()) { if (v >= 2) a.send(d, Cmd::LoopDouble); else if (v <= 0.5) a.send(d, Cmd::LoopHalve); } return true; }
        if (k == "loop_move") { if (has && v != 0) a.send(d, Cmd::BeatJump, v); return true; }
        if (startsWith(k, "loop_move_")) {
            const double x = std::atof(k.c_str() + 10);
            if (on && has && x > 0) a.send(d, Cmd::BeatJump, endsWith(k, "_backward") ? -x : x);
            return true;
        }
        if (k == "beatloop_size") { if (v > 0) dk.mxBeatloopSize = std::clamp(v, 1.0 / 32, 512.0); return true; }
        if (k == "beatjump_size") { if (v > 0) dk.mxBeatjumpSize = std::clamp(v, 1.0 / 32, 512.0); return true; }
        if (k == "beatjump_size_double") { if (on) dk.mxBeatjumpSize = std::min(512.0, dk.mxBeatjumpSize * 2); return true; }
        if (k == "beatjump_size_halve") { if (on) dk.mxBeatjumpSize = std::max(1.0 / 32, dk.mxBeatjumpSize / 2); return true; }
        if (k == "beatloop_activate") { if (on && has) a.send(d, Cmd::AutoLoop, dk.mxBeatloopSize); return true; }
        if (k == "beatloop") { if (v > 0 && has) a.send(d, Cmd::AutoLoop, v); return true; }
        if (k == "beatlooproll_activate") { if (has) a.send(d, on ? Cmd::RollStart : Cmd::RollEnd, dk.mxBeatloopSize); return true; }
        if (startsWith(k, "beatlooproll_")) {
            const char* x0 = k.c_str() + 13;
            const double x = std::atof(*x0 == 'r' ? x0 + 1 : x0);
            if (has && x > 0) a.send(d, on ? Cmd::RollStart : Cmd::RollEnd, x);
            return true;
        }
        if (startsWith(k, "beatloop_")) {
            const char* x0 = k.c_str() + 9;
            const double x = std::atof(*x0 == 'r' ? x0 + 1 : x0);
            if (endsWith(k, "_enabled")) return true;
            if (x <= 0) return false;
            if (!on || !has) return true;
            if (endsWith(k, "_toggle") || k.find('_', 9) == std::string::npos) {
                double cur;
                readReal(g, "beatloop_" + std::string(x0).substr(0, std::string(x0).find('_')) + "_enabled", cur);
                if (st.loopActive.load() && cur > 0) a.send(d, Cmd::LoopExit);
                else { dk.mxBeatloopSize = x; a.send(d, Cmd::AutoLoop, x); }
            } else { dk.mxBeatloopSize = x; a.send(d, Cmd::AutoLoop, x); }
            return true;
        }
        if (k == "beatjump_forward" || k == "beatjump_backward") { if (on && has) a.send(d, Cmd::BeatJump, k == "beatjump_forward" ? dk.mxBeatjumpSize : -dk.mxBeatjumpSize); return true; }
        if (k == "beatjump") { if (v != 0 && has) a.send(d, Cmd::BeatJump, v); return true; }
        if (startsWith(k, "beatjump_")) {
            const double x = std::atof(k.c_str() + 9);
            if (x <= 0) return false;
            if (on && has) a.send(d, Cmd::BeatJump, endsWith(k, "_backward") ? -x : x);
            return true;
        }
        if (k == "sync_enabled") { if (on != dk.sync) a.setSync(d, on); return true; }
        if (k == "sync_leader" || k == "sync_master") { if (on) a.setSyncMaster(d); return true; }
        if (k == "beatsync" || k == "beatsync_tempo" || k == "beatsync_phase") {
            if (!on || !has) return true;
            const int o = 1 - d;
            if (!a.deck_[o].track || dk.meta.bpm <= 0 || a.deck_[o].meta.bpm <= 0) return true;
            if (k != "beatsync_phase") {
                const double target = a.engine_.effectiveBpm(o);
                const double k2 = a.syncFactorFor(d, o);
                const float np = (float)(target / (dk.meta.bpm * k2) - 1.0);
                dk.pitch = std::clamp(np, -a.pitchRange(d), a.pitchRange(d));
                p.pitch = dk.pitch;
            }
            if (k != "beatsync_tempo") a.send(d, Cmd::SyncPhase, 0, o);
            return true;
        }
        if (k == "rate") {
            const float rr = a.pitchRange(d);
            double vAdj = a.settings_.pitchInvertUi ? v : -v;
            if (a.midi_.settings().pitchInvert) vAdj = -vAdj;
            const float target01 = (float)std::clamp((vAdj + 1.0) * 0.5, 0.0, 1.0);
            const float cur01 = std::clamp((dk.pitch / std::max(rr, 0.01f) + 1.f) * 0.5f, 0.f, 1.f);
            auto& pk = a.pickup_[MA_Pitch * 2 + (d & 1)];
            if (a.midi_.settings().softTakeover) {
                if (pk.engaged && std::fabs(cur01 - pk.lastApplied) > 0.025f) pk.engaged = false;
                if (!pk.engaged) {
                    const bool isClose = std::fabs(target01 - cur01) < 0.045f;
                    const bool crossed = pk.hasPrev && (pk.prev - cur01) * (target01 - cur01) <= 0.f;
                    if (isClose || crossed) pk.engaged = true;
                }
                pk.prev = target01; pk.hasPrev = true;
                if (!pk.engaged) { a.midiPickupHint_ = a.now_; return true; }
            }
            pk.engaged = true; pk.lastApplied = target01;
            if (p.syncTo.load() >= 0) dk.sync = false;
            dk.pitch = (float)std::clamp(vAdj, -1.0, 1.0) * rr;
            p.pitch = dk.pitch;
            dk.hasPendingPitchPrompt = false;
            return true;
        }
        if (k == "rateRange") { if (v > 0) a.settings_.pitchRange[d] = (float)std::clamp(v * 100.0, 2.0, 100.0); return true; }
        if (k == "rate_perm_up" || k == "rate_perm_down" || k == "rate_perm_up_small" || k == "rate_perm_down_small") {
            if (!on) return true;
            const float step = endsWith(k, "_small") ? 0.001f : 0.01f;
            if (p.syncTo.load() >= 0) dk.sync = false;
            dk.pitch = std::clamp(dk.pitch + (k.find("_up") != std::string::npos ? step : -step), -a.pitchRange(d), a.pitchRange(d));
            p.pitch = dk.pitch;
            return true;
        }
        if (k == "rate_temp_up" || k == "rate_temp_down" || k == "rate_temp_up_small" || k == "rate_temp_down_small") {
            const float step = endsWith(k, "_small") ? 0.01f : 0.04f;
            dk.mxTempBend = on ? (k.find("_up") != std::string::npos ? step : -step) : 0.f;
            return true;
        }
        if (k == "rate_set_default" || k == "rate_set_zero") { if (on) { dk.pitch = 0; p.pitch = 0; } return true; }
        if (k == "bpm") { if (v > 0 && dk.meta.bpm > 0) { dk.pitch = std::clamp((float)(v / dk.meta.bpm - 1.0), -a.pitchRange(d), a.pitchRange(d)); p.pitch = dk.pitch; } return true; }
        if (k == "keylock") { p.keyLock = on; return true; }
        if (k == "stem_vocal_toggle" || k == "stem_1_mute") { if (on) p.stemMute[0] = !p.stemMute[0].load(); return true; }
        if (k == "stem_drums_toggle" || k == "stem_2_mute") { if (on) p.stemMute[1] = !p.stemMute[1].load(); return true; }
        if (k == "stem_bass_toggle"  || k == "stem_3_mute") { if (on) p.stemMute[2] = !p.stemMute[2].load(); return true; }
        if (k == "stem_inst_toggle"  || k == "stem_4_mute") { if (on) p.stemMute[3] = !p.stemMute[3].load(); return true; }
        if (k == "stem_vocal_vol" || k == "stem_1_volume") { p.stemVol[0] = (float)std::clamp(v * 1.5, 0.0, 1.5); return true; }
        if (k == "stem_drums_vol" || k == "stem_2_volume") { p.stemVol[1] = (float)std::clamp(v * 1.5, 0.0, 1.5); return true; }
        if (k == "stem_bass_vol"  || k == "stem_3_volume") { p.stemVol[2] = (float)std::clamp(v * 1.5, 0.0, 1.5); return true; }
        if (k == "stem_inst_vol"  || k == "stem_4_volume") { p.stemVol[3] = (float)std::clamp(v * 1.5, 0.0, 1.5); return true; }
        if (k == "pitch" || k == "pitch_adjust") { p.keyShift = (float)std::clamp(v, -6.0, 6.0); return true; }
        if (k == "pitch_up" || k == "pitch_down") { if (on) p.keyShift = std::clamp(p.keyShift.load() + (k == "pitch_up" ? 1.f : -1.f), -6.f, 6.f); return true; }
        if (k == "pitch_up_small" || k == "pitch_down_small") { if (on) p.keyShift = std::clamp(p.keyShift.load() + (k == "pitch_up_small" ? 0.1f : -0.1f), -6.f, 6.f); return true; }
        if (k == "reset_key" || k == "pitch_set_zero" || k == "pitch_adjust_set_default" || k == "pitch_set_default") { if (on) p.keyShift = 0.f; return true; }
        if (k == "sync_key") { if (on && has) a.keyMatch(d); return true; }
        if (k == "volume") { p.fader = (float)MxBehavior{MxBehavior::Audio, 0, 1, 1, -20, 0, 1}.valueToParam(v); return true; }
        if (k == "volume_set_default" || k == "volume_set_one") { if (on) p.fader = 1.f; return true; }
        if (k == "volume_set_zero") { if (on) p.fader = 0.f; return true; }
        if (k == "pregain") { p.gainDb = (float)std::clamp(ratio2db(std::max(v, 0.0001)), -12.0, 12.0); return true; }
        if (k == "pregain_set_default" || k == "pregain_set_one") { if (on) p.gainDb = 0.f; return true; }
        if (k == "pfl") { p.pfl = on; return true; }
        if (k == "quantize") { p.quantize = on; return true; }
        if (k == "slip_enabled") { p.slip = on; return true; }
        if (k == "reverse") { p.reverse = on; return true; }
        if (k == "reverseroll") { p.reverse = on; p.slip = on; return true; }
        if (k == "LoadSelectedTrack") { if (on) a.loadSelectedInto(d, false); return true; }
        if (k == "NextTrack" || k == "PrevTrack") { if (on) { a.browseMove(k == "NextTrack" ? 1 : -1); a.loadSelectedInto(d, false); } return true; }
        if (k == "LoadSelectedTrackAndPlay") { if (on) a.loadSelectedInto(d, true); return true; }
        if (k == "eject") { if (on && has) a.ejectDeck(d); return true; }
        if (k == "jog") {
            if (!has || v == 0) return true;
            // Empuje: los scripts mandan pasos del jog (p. ej. DDJ-400: pasos × 0,8). Calibrado para que un giro
            // suave (un cuarto de vuelta por segundo en un jog de 720 pasos) dé ≈ 2 %, como el resto de jogs.
            if (st.playing.load()) dk.nudgeAcc += (float)v * 0.00015f * a.midi_.settings().jogBendSens;
            else seekSec(a.deckPosSec(d) + v * 0.02);
            return true;
        }
        if (k == "wheel") { dk.mxWheel = (float)std::clamp(v, -3.0, 3.0); return true; }
        if (k == "scratch2_enable") {
            if (!has) return true;
            if (on != p.scratching.load()) { p.scratching = on; if (!on) p.scratchVelocity = 0.f; }
            return true;
        }
        if (k == "scratch2") { p.scratchVelocity = (float)std::clamp(v, -8.0, 8.0); return true; }
        if (k == "filterLow" || k == "filterMid" || k == "filterHigh") {
            const int band = k == "filterLow" ? 0 : k == "filterMid" ? 1 : 2;
            const float e = (float)MxBehavior{MxBehavior::Audio, 0, 4, 1, -12, ratio2db(4), 0.5}.valueToParam(v);
            if (dk.mxKill[band]) { dk.mxKillSaved[band] = e; return true; }
            (band == 0 ? p.eqLow : band == 1 ? p.eqMid : p.eqHigh) = e;
            return true;
        }
        if (k == "filterLowKill" || k == "filterMidKill" || k == "filterHighKill") {
            const int band = k == "filterLowKill" ? 0 : k == "filterMidKill" ? 1 : 2;
            std::atomic<float>& eq = band == 0 ? p.eqLow : band == 1 ? p.eqMid : p.eqHigh;
            if (on && !dk.mxKill[band]) { dk.mxKillSaved[band] = eq.load(); eq = 0.f; dk.mxKill[band] = true; }
            else if (!on && dk.mxKill[band]) { eq = dk.mxKillSaved[band]; dk.mxKill[band] = false; }
            return true;
        }
        if (k == "flanger") {
            if (on) { if (p.fxType.load() == 0) p.fxType = (int)FxType::Flanger; p.fxOn = true; }
            else p.fxOn = false;
            return true;
        }
        if (k == "bpm_tap" || k == "tempo_tap") { if (on && has) a.tapTempo(d); return true; }
        if (k == "beats_translate_curpos") {
            if (!on || !has || dk.meta.bpm <= 0) return true;
            const double beat = 60.0 / dk.meta.bpm, pos = a.deckPosSec(d);
            const double kk = std::round((pos - dk.meta.gridAnchorSec) / beat);
            dk.meta.gridAnchorSec += pos - (dk.meta.gridAnchorSec + kk * beat);
            dk.meta.gridSrc = 3;
            a.pushGrid(d);
            a.library_.updateMeta(dk.path, dk.meta);
            return true;
        }
        if (k == "beats_translate_earlier" || k == "beats_translate_later") {
            if (!on || !has) return true;
            dk.meta.gridAnchorSec += k == "beats_translate_later" ? 0.01 : -0.01;
            dk.meta.gridSrc = 3;
            a.pushGrid(d);
            a.library_.updateMeta(dk.path, dk.meta);
            return true;
        }
        if (k == "beats_adjust_faster") { if (on) bpmSet(dk.meta.bpm + 0.01); return true; }
        if (k == "beats_adjust_slower") { if (on) bpmSet(dk.meta.bpm - 0.01); return true; }
        if (k == "beats_set_double") { if (on) bpmSet(dk.meta.bpm * 2); return true; }
        if (k == "beats_set_halve") { if (on) bpmSet(dk.meta.bpm / 2); return true; }
        if (k == "beats_set_twothirds") { if (on) bpmSet(dk.meta.bpm * 2 / 3); return true; }
        if (k == "beats_set_threefourths") { if (on) bpmSet(dk.meta.bpm * 3 / 4); return true; }
        if (k == "beats_set_fourthirds") { if (on) bpmSet(dk.meta.bpm * 4 / 3); return true; }
        if (k == "beats_set_threehalves") { if (on) bpmSet(dk.meta.bpm * 3 / 2); return true; }
        if (startsWith(k, "stars_")) {
            if (!on || !has) return true;
            int cur = 0;
            {
                std::lock_guard<std::recursive_mutex> lk(a.library_.mutex());
                if (const LibTrack* t = a.library_.find(dk.path)) cur = t->rating;
            }
            static const char* const names[] = {"stars_one", "stars_two", "stars_three", "stars_four", "stars_five"};
            int nv = cur;
            for (int i = 0; i < 5; ++i) if (k == names[i]) nv = i + 1;
            if (k == "stars_up") nv = std::min(5, cur + 1);
            if (k == "stars_down") nv = std::max(0, cur - 1);
            a.library_.setRating(dk.path, nv);
            return true;
        }
        if (k == "__brake") { if (has) a.send(d, Cmd::Brake, 1.0); return true; }
        if (k == "__spinback") { if (has) a.send(d, Cmd::Backspin, 1.0); return true; }
        if (k == "key" || k == "visual_key") return true;
        return false;
    }
    case GroupRef::Master: {
        if (k == "crossfader") { a.engine_.mixer.crossfader = (float)std::clamp((v + 1.0) * 0.5, 0.0, 1.0); return true; }
        if (k == "crossfader_up" || k == "crossfader_down" || k == "crossfader_up_small" || k == "crossfader_down_small") {
            if (!on) return true;
            const float step = endsWith(k, "_small") ? 0.005f : 0.05f;
            a.engine_.mixer.crossfader = std::clamp(a.engine_.mixer.crossfader.load() + (k.find("_up") != std::string::npos ? step : -step), 0.f, 1.f);
            return true;
        }
        if (k == "crossfader_set_default") { if (on) a.engine_.mixer.crossfader = 0.5f; return true; }
        if (k == "headMix") { a.engine_.mixer.hpMix = (float)std::clamp((v + 1.0) * 0.5, 0.0, 1.0); return true; }
        if (k == "gain" || k == "volume") { a.engine_.mixer.masterVol = (float)MxBehavior{MxBehavior::Audio, 0, 5, 1, -14, 14, 0.5}.valueToParam(v); return true; }
        if (k == "headGain" || k == "headVolume") { a.engine_.mixer.hpVol = (float)MxBehavior{MxBehavior::Audio, 0, 5, 1, -14, 14, 0.5}.valueToParam(v); return true; }
        if (k == "maximize_library") { a.mode_ = on ? 2 : 0; return true; }
        if (k == "gain_set_default") { if (on) a.engine_.mixer.masterVol = 0.8f; return true; }
        if (k == "headGain_set_default") { if (on) a.engine_.mixer.hpVol = 0.7f; return true; }
        if (k == "headMix_set_default") { if (on) a.engine_.mixer.hpMix = 0.5f; return true; }
        if (startsWith(k, "vu_meter") || startsWith(k, "VuMeter") || startsWith(k, "peak_indicator") || startsWith(k, "PeakIndicator") || k == "num_decks" ||
            k == "num_samplers" || k == "enabled" || k == "headEnabled" || k == "samplerate")
            return true;
        return false;
    }
    case GroupRef::App:
        return k == "num_decks" || k == "num_samplers" || k == "num_preview_decks" || k == "num_microphones" || k == "num_auxiliaries" || k == "samplerate" ||
               startsWith(k, "indicator_") || startsWith(k, "gui_tick");
    case GroupRef::Library: {
        if (k == "MoveVertical" || k == "ScrollVertical") { if (v != 0) a.browseMove((int)std::lround(v)); return true; }
        if (k == "MoveUp" || k == "ScrollUp") { if (on) a.browseMove(-1); return true; }
        if (k == "MoveDown" || k == "ScrollDown") { if (on) a.browseMove(1); return true; }
        if (k == "GoToItem") { if (on) a.loadSelectedInto(-1, false); return true; }
        if (k == "AutoDjAddBottom" || k == "AutoDjAddTop") return writeReal("[Playlist]", k, v);
        if (k == "MoveFocusForward" || k == "MoveFocusBackward" || k == "MoveFocus" || k == "MoveLeft" || k == "MoveRight" || k == "MoveHorizontal") return true;
        return false;
    }
    case GroupRef::Playlist: {
        if (k == "SelectNextTrack") { if (on) a.browseMove(1); return true; }
        if (k == "SelectPrevTrack") { if (on) a.browseMove(-1); return true; }
        if (k == "SelectTrackKnob") { if (v != 0) a.browseMove((int)std::lround(v)); return true; }
        if (k == "LoadSelectedIntoFirstStopped") { if (on) a.loadSelectedInto(-1, false); return true; }
        if (k == "SelectNextPlaylist") { if (on) a.browseSourceStep(1); return true; }
        if (k == "SelectPrevPlaylist") { if (on) a.browseSourceStep(-1); return true; }
        if (k == "SelectPlaylist") { if (v != 0) a.browseSourceStep((int)std::lround(v)); return true; }
        if (k == "ToggleSelectedSidebarItem") return true;
        if (k == "AutoDjAddBottom" || k == "AutoDjAddTop") {
            if (!on) return true;
            const std::wstring path = a.selectedLibraryPath();
            if (path.empty()) return true;
            const int list = a.ensurePlaylist("Automix");
            std::lock_guard<std::recursive_mutex> lk(a.library_.mutex());
            auto& paths = a.library_.playlists()[list].paths;
            if (k == "AutoDjAddTop") paths.insert(paths.begin(), path); else paths.push_back(path);
            a.library_.markDirty();
            return true;
        }
        return false;
    }
    case GroupRef::Eq: {
        if (r.deck < 0 || r.deck > 1) return false;
        DeckParams& p = a.engine_.deck[r.deck];
        DeckUi& dk = a.deck_[r.deck];
        if (k == "parameter1" || k == "parameter2" || k == "parameter3") {
            const int band = k.back() - '1';
            const float e = (float)MxBehavior{MxBehavior::Audio, 0, 4, 1, -12, ratio2db(4), 0.5}.valueToParam(v);
            if (dk.mxKill[band]) { dk.mxKillSaved[band] = e; return true; }
            (band == 0 ? p.eqLow : band == 1 ? p.eqMid : p.eqHigh) = e;
            return true;
        }
        if (k == "button_parameter1" || k == "button_parameter2" || k == "button_parameter3") {
            const int band = k.back() - '1';
            std::atomic<float>& eq = band == 0 ? p.eqLow : band == 1 ? p.eqMid : p.eqHigh;
            if (on && !dk.mxKill[band]) { dk.mxKillSaved[band] = eq.load(); eq = 0.f; dk.mxKill[band] = true; }
            else if (!on && dk.mxKill[band]) { eq = dk.mxKillSaved[band]; dk.mxKill[band] = false; }
            return true;
        }
        if (k == "enabled" || k == "loaded" || k == "num_parameters" || k == "num_button_parameters") return true;
        return false;
    }
    case GroupRef::QuickFx: {
        if (r.deck < 0 || r.deck > 1) return false;
        DeckParams& p = a.engine_.deck[r.deck];
        if (k == "super1" || (r.slot && (k == "meta" || k == "parameter1"))) {
            const float f = (float)std::clamp(v * 2.0 - 1.0, -1.0, 1.0);
            p.filter = std::fabs(f) < 0.02f ? 0.f : f;
            return true;
        }
        if (k == "super1_set_default") { if (on) p.filter = 0.f; return true; }
        if (k == "enabled") { if (!on) p.filter = 0.f; return true; }
        if (r.slot && (k == "loaded" || k == "num_parameters" || k == "num_button_parameters")) return true;
        return false;
    }
    case GroupRef::FxUnit: {
        if (r.deck < 0 || r.deck > 1) return false;
        DeckParams& p = a.engine_.deck[r.deck];
        auto cycle = [&](int dir) {
            const int n = (int)FxType::Count;
            int t = p.fxType.load();
            t = ((t - 1 + dir) % (n - 1) + (n - 1)) % (n - 1) + 1;   // sin «Sin efecto»
            p.fxType = t;
        };
        if (k == "enabled") { p.fxOn = on && p.fxType.load() != 0; if (on && p.fxType.load() == 0) { p.fxType = (int)FxType::Eco; p.fxOn = true; } return true; }
        if (k == "mix" || k == "super1") { p.fxAmount = (float)std::clamp(v, 0.0, 1.0); return true; }
        if (startsWith(k, "group_[Channel") && endsWith(k, "]_enable")) {
            const int dd = std::atoi(k.c_str() + 14) - 1;
            if (dd < 0 || dd > 1) return false;
            DeckParams& q = a.engine_.deck[dd];
            if (on && q.fxType.load() == 0) q.fxType = (int)FxType::Eco;
            q.fxOn = on;
            return true;
        }
        if (k == "next_chain" || k == "next_chain_preset") { if (on) cycle(1); return true; }
        if (k == "prev_chain" || k == "prev_chain_preset") { if (on) cycle(-1); return true; }
        if (k == "chain_preset_selector") { if (v != 0) cycle(v > 0 ? 1 : -1); return true; }
        if (k == "loaded_chain_preset") { const int t = (int)v; if (t >= 0 && t < (int)FxType::Count) p.fxType = t; return true; }
        if (k == "clear") { if (on) { p.fxOn = false; p.fxType = 0; } return true; }
        if (k == "num_effectslots") return true;
        return false;
    }
    case GroupRef::FxSlot: {
        if (r.deck < 0 || r.deck > 1) return false;
        if (r.slot != 1) return false;   // solo hay un efecto por plato
        DeckParams& p = a.engine_.deck[r.deck];
        auto cycle = [&](int dir) {
            const int n = (int)FxType::Count;
            int t = p.fxType.load();
            t = ((t - 1 + dir) % (n - 1) + (n - 1)) % (n - 1) + 1;
            p.fxType = t;
        };
        if (k == "enabled") { if (on && p.fxType.load() == 0) p.fxType = (int)FxType::Eco; p.fxOn = on; return true; }
        if (k == "meta" || k == "parameter1") { p.fxAmount = (float)std::clamp(v, 0.0, 1.0); return true; }
        if (k == "next_effect") { if (on) cycle(1); return true; }
        if (k == "prev_effect") { if (on) cycle(-1); return true; }
        if (k == "effect_selector") { if (v != 0) cycle(v > 0 ? 1 : -1); return true; }
        if (k == "loaded_effect") { const int t = (int)v; if (t >= 0 && t < (int)FxType::Count) p.fxType = t; return true; }
        if (k == "clear") { if (on) { p.fxOn = false; p.fxType = 0; } return true; }
        if (k == "loaded" || k == "num_parameters" || k == "num_button_parameters") return true;
        return false;
    }
    case GroupRef::AutoDJ:
        if (k == "enabled") { if (on != a.automixOn_) a.setAutomix(on); return true; }
        return false;
    case GroupRef::Recording:
        if (k == "toggle_recording") { if (on) { if (a.recorder_.active()) a.stopRecording(); else a.startRecording(); } return true; }
        if (k == "status") return true;
        return false;
    case GroupRef::Skin:
        if (k == "show_maximized_library") { a.mode_ = on ? 2 : 0; return true; }
        return false;
    default:
        // [Flanger] antiguo: la profundidad es la cantidad del efecto de los platos con flanger.
        if (g == "[Flanger]" && k == "lfoDepth") {
            for (int d = 0; d < kNumDecks; ++d)
                if (a.engine_.deck[d].fxType.load() == (int)FxType::Flanger) a.engine_.deck[d].fxAmount = (float)std::clamp(v, 0.0, 1.0);
            return true;
        }
        return false;
    }
}

// Efectos continuos: REW/FF mantenidos, "wheel" y el empuje del jog.
void MandoPuente::deckTick(float dt) {
    App& a = app_;
    for (int d = 0; d < kNumDecks; ++d) {
        DeckUi& dk = a.deck_[d];
        if (!dk.track) { dk.mxSeekDir = 0; continue; }
        const DeckStatus& st = a.engine_.deckStatus[d];
        if (dk.mxSeekDir != 0)
            a.send(d, Cmd::Seek, std::max(0.0, st.posFrames.load() + dk.mxSeekDir * 4.0 * dt * dk.track->sampleRate));
        if (dk.mxWheel != 0 && !st.playing.load())
            a.send(d, Cmd::Seek, std::max(0.0, st.posFrames.load() + dk.mxWheel * 0.4 * dt * dk.track->sampleRate));
        if (dk.mxPlayOnLoad.size() && dk.path == dk.mxPlayOnLoad && !st.playing.load()) { a.togglePlay(d); dk.mxPlayOnLoad.clear(); }
    }
}

} // namespace rdj

#include "midi.h"
#include "util.h"
#include <json.hpp>
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace rdj {

using json = nlohmann::json;

const std::vector<MidiActionInfo>& midiActions() {
    static const std::vector<MidiActionInfo> a = {
        {"play", "Reproducir / pausa", MidiKind::Boton, true},
        {"cue", "CUE", MidiKind::Boton, true},
        {"sync", "SYNC", MidiKind::Boton, true},
        {"pfl", "Preescucha (auriculares)", MidiKind::Boton, true},
        {"hot1", "Hot cue 1", MidiKind::Boton, true}, {"hot2", "Hot cue 2", MidiKind::Boton, true},
        {"hot3", "Hot cue 3", MidiKind::Boton, true}, {"hot4", "Hot cue 4", MidiKind::Boton, true},
        {"hot5", "Hot cue 5", MidiKind::Boton, true}, {"hot6", "Hot cue 6", MidiKind::Boton, true},
        {"hot7", "Hot cue 7", MidiKind::Boton, true}, {"hot8", "Hot cue 8", MidiKind::Boton, true},
        {"loopin", "Loop: entrada", MidiKind::Boton, true},
        {"loopout", "Loop: salida", MidiKind::Boton, true},
        {"loop4", "Loop automático 4 pulsos", MidiKind::Boton, true},
        {"loophalf", "Loop: mitad", MidiKind::Boton, true},
        {"loopdouble", "Loop: doble", MidiKind::Boton, true},
        {"loopexit", "Loop: salir", MidiKind::Boton, true},
        {"bendup", "Empujar tempo +", MidiKind::Boton, true},
        {"benddown", "Empujar tempo -", MidiKind::Boton, true},
        {"load", "Cargar pista seleccionada", MidiKind::Boton, true},
        {"fxon", "Efecto activado", MidiKind::Boton, true},
        {"volume", "Volumen de canal", MidiKind::Absoluto, true},
        {"gain", "Ganancia", MidiKind::Absoluto, true},
        {"eqhigh", "EQ agudos", MidiKind::Absoluto, true},
        {"eqmid", "EQ medios", MidiKind::Absoluto, true},
        {"eqlow", "EQ graves", MidiKind::Absoluto, true},
        {"filter", "Filtro", MidiKind::Absoluto, true},
        {"pitch", "Pitch (fader de tempo)", MidiKind::Absoluto, true},
        {"fxamount", "Cantidad de efecto", MidiKind::Absoluto, true},
        {"jog", "Jog: aro exterior (empuje)", MidiKind::Relativo, true},
        {"crossfader", "Crossfader", MidiKind::Absoluto, false},
        {"master", "Volumen master", MidiKind::Absoluto, false},
        {"hpvol", "Volumen auriculares", MidiKind::Absoluto, false},
        {"hpmix", "Mezcla auriculares", MidiKind::Absoluto, false},
        {"browse", "Navegar biblioteca (rueda)", MidiKind::Relativo, false},
        {"browseup", "Biblioteca: subir", MidiKind::Boton, false},
        {"browsedown", "Biblioteca: bajar", MidiKind::Boton, false},
        {"jogtouch", "Jog: tocar el plato", MidiKind::Boton, true},
        {"jogscratch", "Jog: girar el plato (scratch)", MidiKind::Relativo, true},
        {"shift", "SHIFT", MidiKind::Boton, false},
        {"reloop", "Reloop", MidiKind::Boton, true},
        {"browseload", "Biblioteca: cargar en plato libre", MidiKind::Boton, false},
        {"slip", "Slip", MidiKind::Boton, true},
        {"reverse", "Reverse (fijo)", MidiKind::Boton, true},
        {"reversehold", "Reverse (mantener)", MidiKind::Boton, true},
        {"rollquarter", "Roll 1/4 (mantener)", MidiKind::Boton, true},
        {"rollone", "Roll 1 pulso (mantener)", MidiKind::Boton, true},
        {"backspin", "Backspin", MidiKind::Boton, true},
        {"brake", "Freno de vinilo", MidiKind::Boton, true},
        {"vinyl", "Modo vinilo", MidiKind::Boton, true},
        {"rackfxoff", "Rack: EFECTOS OFF", MidiKind::Boton, false},
        {"rackmacro", "Rack: macro", MidiKind::Absoluto, false},
        {"rackxyx", "Rack: pad XY eje X", MidiKind::Absoluto, false},
        {"rackxyy", "Rack: pad XY eje Y", MidiKind::Absoluto, false},
        {"rackxytouch", "Rack: pad XY tocar (mantener)", MidiKind::Boton, false},
        {"rackxyhold", "Rack: pad XY HOLD", MidiKind::Boton, false},
        {"rackslot1", "Rack: ranura 1 encender", MidiKind::Boton, false},
        {"rackslot2", "Rack: ranura 2 encender", MidiKind::Boton, false},
        {"rackslot3", "Rack: ranura 3 encender", MidiKind::Boton, false},
        {"rackslot4", "Rack: ranura 4 encender", MidiKind::Boton, false},
        {"rackamt1", "Rack: ranura 1 intensidad", MidiKind::Absoluto, false},
        {"rackamt2", "Rack: ranura 2 intensidad", MidiKind::Absoluto, false},
        {"rackamt3", "Rack: ranura 3 intensidad", MidiKind::Absoluto, false},
        {"rackamt4", "Rack: ranura 4 intensidad", MidiKind::Absoluto, false},
        {"rackmix1", "Rack: ranura 1 mezcla", MidiKind::Absoluto, false},
        {"rackmix2", "Rack: ranura 2 mezcla", MidiKind::Absoluto, false},
        {"rackmix3", "Rack: ranura 3 mezcla", MidiKind::Absoluto, false},
        {"rackmix4", "Rack: ranura 4 mezcla", MidiKind::Absoluto, false},
        {"rackreturn", "Rack: retorno de efectos", MidiKind::Absoluto, false},
        {"rackisoon", "Rack: aislador encender", MidiKind::Boton, false},
        {"rackisolow", "Rack: aislador graves", MidiKind::Absoluto, false},
        {"rackisomid", "Rack: aislador medios", MidiKind::Absoluto, false},
        {"rackisohigh", "Rack: aislador agudos", MidiKind::Absoluto, false},
        {"rackkilllow", "Rack: aislador corte de graves", MidiKind::Boton, false},
        {"rackkillmid", "Rack: aislador corte de medios", MidiKind::Boton, false},
        {"rackkillhigh", "Rack: aislador corte de agudos", MidiKind::Boton, false},
        {"rackloprec", "Rack: loop grabar", MidiKind::Boton, false},
        {"racklopplay", "Rack: loop reproducir / parar", MidiKind::Boton, false},
        {"rackloopdub", "Rack: loop sobregrabar", MidiKind::Boton, false},
        {"rackloopundo", "Rack: loop deshacer capa", MidiKind::Boton, false},
        {"rackloopclear", "Rack: loop borrar", MidiKind::Boton, false},
        {"rackloopvol", "Rack: loop volumen", MidiKind::Absoluto, false},
        {"rackpresetnext", "Rack: preset siguiente", MidiKind::Boton, false},
        {"rackpresetprev", "Rack: preset anterior", MidiKind::Boton, false},
        {"rackdrumplay", "Caja de ritmos: tocar / parar", MidiKind::Boton, false},
        {"rackbassplay", "Bajo: tocar / parar", MidiKind::Boton, false},
        {"rackdrumpat", "Caja de ritmos: patrón siguiente", MidiKind::Boton, false},
        {"rackpad1", "Sampler: pad 1", MidiKind::Boton, false},
        {"rackpad2", "Sampler: pad 2", MidiKind::Boton, false},
        {"rackpad3", "Sampler: pad 3", MidiKind::Boton, false},
        {"rackpad4", "Sampler: pad 4", MidiKind::Boton, false},
        {"rackpad5", "Sampler: pad 5", MidiKind::Boton, false},
        {"rackpad6", "Sampler: pad 6", MidiKind::Boton, false},
        {"rackpad7", "Sampler: pad 7", MidiKind::Boton, false},
        {"rackpad8", "Sampler: pad 8", MidiKind::Boton, false},
        {"rackpad9", "Sampler: pad 9", MidiKind::Boton, false},
        {"rackpad10", "Sampler: pad 10", MidiKind::Boton, false},
        {"rackpad11", "Sampler: pad 11", MidiKind::Boton, false},
        {"rackpad12", "Sampler: pad 12", MidiKind::Boton, false},
        {"rackpad13", "Sampler: pad 13", MidiKind::Boton, false},
        {"rackpad14", "Sampler: pad 14", MidiKind::Boton, false},
        {"rackpad15", "Sampler: pad 15", MidiKind::Boton, false},
        {"rackpad16", "Sampler: pad 16", MidiKind::Boton, false},
        {"quitarefectos", "Quitar todos los efectos y filtros", MidiKind::Boton, false},
        {"quitarefectosplato", "Quitar los efectos del plato", MidiKind::Boton, true},
        {"stemvocalmute", "Stems: activar/silenciar VOZ (Pad 1)", MidiKind::Boton, true},
        {"stemdrumsmute", "Stems: activar/silenciar BATERÍA (Pad 2)", MidiKind::Boton, true},
        {"stembassmute", "Stems: activar/silenciar BAJO (Pad 3)", MidiKind::Boton, true},
        {"steminstmute", "Stems: activar/silenciar INSTRUMENTOS (Pad 4)", MidiKind::Boton, true},
        {"stemvocalvol", "Stems: volumen VOZ", MidiKind::Absoluto, true},
        {"stemdrumsvol", "Stems: volumen BATERÍA", MidiKind::Absoluto, true},
        {"stembassvol", "Stems: volumen BAJO", MidiKind::Absoluto, true},
        {"steminstvol", "Stems: volumen INSTRUMENTOS", MidiKind::Absoluto, true},
        {"stemvocalfx", "Stems: efecto VOZ", MidiKind::Absoluto, true},
        {"stemdrumsfx", "Stems: efecto BATERÍA", MidiKind::Absoluto, true},
        {"stembassfx", "Stems: efecto BAJO", MidiKind::Absoluto, true},
        {"steminstfx", "Stems: efecto INSTRUMENTOS", MidiKind::Absoluto, true},
        {"keyup", "Tono: subir +1 semitono", MidiKind::Boton, true},
        {"keydown", "Tono: bajar -1 semitono", MidiKind::Boton, true},
        {"keyreset", "Tono: restablecer a 0", MidiKind::Boton, true},
        {"quantize", "Cuantizar (Q) encender/apagar", MidiKind::Boton, true},
    };
    static_assert(MA_Count == 122, "la tabla de acciones MIDI y el enum deben coincidir");
    return a;
}

static bool g_midiClosing = false;   // al cerrar, los búferes SysEx vuelven vacíos y no se reponen

static void CALLBACK midiInProc(HMIDIIN h, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR) {
    auto* ctx = reinterpret_cast<MidiInContext*>(inst);
    if (msg == MIM_LONGDATA) {
        MIDIHDR* hdr = reinterpret_cast<MIDIHDR*>(p1);
        if (!hdr || g_midiClosing) return;
        if (hdr->dwBytesRecorded > 0) ctx->mm->pushSysex(ctx, reinterpret_cast<const uint8_t*>(hdr->lpData), hdr->dwBytesRecorded);
        midiInAddBuffer(h, hdr, sizeof(MIDIHDR));   // se repone para el siguiente mensaje
        return;
    }
    if (msg != MIM_DATA) return;
    MidiEvent e;
    e.device = ctx->device;
    e.status = (uint8_t)(p1 & 0xFF);
    e.data1 = (uint8_t)((p1 >> 8) & 0x7F);
    e.data2 = (uint8_t)((p1 >> 16) & 0x7F);
    ctx->mm->pushEvent(e);
}

MidiManager::~MidiManager() {
    resetLeds();
    closeAll();
}

void MidiManager::closeAll() {
    g_midiClosing = true;
    for (size_t i = 0; i < handles_.size(); ++i) {
        void* h = handles_[i];
        if (!h) continue;
        midiInStop((HMIDIIN)h);
        midiInReset((HMIDIIN)h);   // devuelve los búferes SysEx
        if (i < contexts_.size())
            for (auto& hb : contexts_[i]->headers) midiInUnprepareHeader((HMIDIIN)h, reinterpret_cast<MIDIHDR*>(hb.data()), sizeof(MIDIHDR));
        midiInClose((HMIDIIN)h);
    }
    for (void* h : outHandles_) if (h) { midiOutReset((HMIDIOUT)h); midiOutClose((HMIDIOUT)h); }
    handles_.clear();
    outHandles_.clear();
    outDevice_.clear();
    contexts_.clear();
    ledState_.clear();
    g_midiClosing = false;
}

static std::string lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

void MidiManager::refresh() {
    closeAll();
    names_.clear(); outNames_.clear(); errors_.clear();
    integrated_.clear();
    integratedQueue_.clear();
    { std::lock_guard<std::mutex> lk(m_); queue_.clear(); }
    const UINT n = midiInGetNumDevs();
    for (UINT i = 0; i < n && i < 255; ++i) {
        MIDIINCAPSW caps{};
        midiInGetDevCapsW(i, &caps, sizeof caps);
        names_.push_back(toUtf8(caps.szPname));
        HMIDIIN h = nullptr;
        contexts_.push_back(std::make_unique<MidiInContext>(MidiInContext{this, (int)i}));
        const DWORD_PTR inst = reinterpret_cast<DWORD_PTR>(contexts_.back().get());
        if (midiInOpen(&h, i, (DWORD_PTR)midiInProc, inst, CALLBACK_FUNCTION) == MMSYSERR_NOERROR) {
            // Búferes para SysEx (algunos mandos mandan así el estado de sus controles).
            MidiInContext& ctx = *contexts_.back();
            ctx.handle = h;
            ctx.buffers.assign(4, std::vector<uint8_t>(4096));
            ctx.headers.assign(4, std::vector<uint8_t>(sizeof(MIDIHDR)));
            for (int k = 0; k < 4; ++k) {
                MIDIHDR* hdr = reinterpret_cast<MIDIHDR*>(ctx.headers[k].data());
                std::memset(hdr, 0, sizeof(MIDIHDR));
                hdr->lpData = reinterpret_cast<LPSTR>(ctx.buffers[k].data());
                hdr->dwBufferLength = (DWORD)ctx.buffers[k].size();
                if (midiInPrepareHeader(h, hdr, sizeof(MIDIHDR)) == MMSYSERR_NOERROR) midiInAddBuffer(h, hdr, sizeof(MIDIHDR));
            }
            midiInStart(h);
            handles_.push_back(h);
        } else {
            handles_.push_back(nullptr);
            errors_.push_back("No se pudo abrir \"" + names_.back() + "\" (¿lo está usando otro programa?).");
        }
    }
    // Salidas con el mismo nombre que una entrada = el mismo mando (para las luces).
    const UINT no = midiOutGetNumDevs();
    for (UINT i = 0; i < no && i < 255; ++i) {
        MIDIOUTCAPSW caps{};
        midiOutGetDevCapsW(i, &caps, sizeof caps);
        const std::string name = toUtf8(caps.szPname);
        if (caps.wTechnology == MOD_SWSYNTH || caps.wTechnology == MOD_SYNTH || caps.wTechnology == MOD_MAPPER) continue;
        int paired = -1;
        for (size_t k = 0; k < names_.size() && paired < 0; ++k) {
            const std::string a = lower(names_[k]), b = lower(name);
            if (a == b) paired = (int)k;
        }
        for (size_t k = 0; k < names_.size() && paired < 0; ++k) {
            const std::string a = lower(names_[k]), b = lower(name);
            if (a.find(b) != std::string::npos || b.find(a) != std::string::npos) paired = (int)k;
        }
        if (paired < 0) continue;
        HMIDIOUT h = nullptr;
        if (midiOutOpen(&h, i, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR) {
            outHandles_.push_back(h);
            outNames_.push_back(name);
            outDevice_.push_back(paired);
        }
    }
}

bool MidiManager::devicesChanged() const {
    const UINT n = midiInGetNumDevs();
    if (n != names_.size()) return true;
    for (UINT i = 0; i < n && i < 255; ++i) {
        MIDIINCAPSW caps{};
        midiInGetDevCapsW(i, &caps, sizeof caps);
        if (toUtf8(caps.szPname) != names_[i]) return true;
    }
    return false;
}

void MidiManager::setIntegrated(int device, bool on) {
    if (device < 0) return;
    if ((int)integrated_.size() <= device) integrated_.resize(device + 1, false);
    integrated_[device] = on;
}

std::vector<MidiEvent> MidiManager::takeIntegrated() {
    std::vector<MidiEvent> v;
    v.swap(integratedQueue_);
    return v;
}

bool MidiManager::hasOutput(int device) const {
    for (size_t i = 0; i < outHandles_.size(); ++i) if (outHandles_[i] && outDevice_[i] == device) return true;
    return false;
}

void MidiManager::sendShortTo(int device, uint8_t status, uint8_t d1, uint8_t d2) {
    const DWORD msg = (DWORD)status | ((DWORD)(d1 & 0x7F) << 8) | ((DWORD)(d2 & 0x7F) << 16);
    for (size_t i = 0; i < outHandles_.size(); ++i)
        if (outHandles_[i] && outDevice_[i] == device) midiOutShortMsg((HMIDIOUT)outHandles_[i], msg);
}

void MidiManager::sendSysexTo(int device, const std::vector<uint8_t>& data) {
    if (data.empty()) return;
    for (size_t i = 0; i < outHandles_.size(); ++i) {
        if (!outHandles_[i] || outDevice_[i] != device) continue;
        std::vector<char> buf(data.begin(), data.end());
        MIDIHDR hdr{};
        hdr.lpData = buf.data();
        hdr.dwBufferLength = hdr.dwBytesRecorded = (DWORD)buf.size();
        HMIDIOUT h = (HMIDIOUT)outHandles_[i];
        if (midiOutPrepareHeader(h, &hdr, sizeof hdr) != MMSYSERR_NOERROR) continue;
        if (midiOutLongMsg(h, &hdr, sizeof hdr) == MMSYSERR_NOERROR)
            for (int t = 0; t < 200 && !(hdr.dwFlags & MHDR_DONE); ++t) Sleep(1);   // casi siempre es inmediato
        midiOutUnprepareHeader(h, &hdr, sizeof hdr);
    }
}

std::string MidiManager::mappingChoice(const std::string& port) const {
    auto it = mappingChoice_.find(port);
    return it == mappingChoice_.end() ? std::string("auto") : it->second;
}

void MidiManager::setMappingChoice(const std::string& port, const std::string& choice) {
    if (choice == "auto") mappingChoice_.erase(port);
    else mappingChoice_[port] = choice;
    save();
}

void MidiManager::pushSysex(MidiInContext* ctx, const uint8_t* data, size_t n) {
    // Un SysEx puede llegar partido en varios búferes: se junta hasta el F7.
    std::lock_guard<std::mutex> lk(m_);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t b = data[i];
        if (b == 0xF0) ctx->partial.clear();
        if (ctx->partial.size() < 65536) ctx->partial.push_back(b);
        if (b == 0xF7 && !ctx->partial.empty() && ctx->partial[0] == 0xF0) {
            MidiEvent e;
            e.device = ctx->device;
            e.status = 0xF0;
            e.sysex = std::move(ctx->partial);
            ctx->partial.clear();
            if (queue_.size() < 4096) queue_.push_back(std::move(e));
        }
    }
}

bool MidiManager::anyControllerNamed(const char* fragment) const {
    const std::string f = lower(fragment);
    for (auto& n : names_) if (lower(n).find(f) != std::string::npos) return true;
    return false;
}

void MidiManager::pushEvent(const MidiEvent& e) {
    std::lock_guard<std::mutex> lk(m_);
    if (queue_.size() < 4096) queue_.push_back(e);
}

void MidiManager::assign(const MidiBinding& b) {
    // Un mensaje solo controla una acción, y una acción tiene un solo mensaje por capa.
    const bool perDeck = midiActions()[b.action].perDeck;
    for (size_t i = 0; i < bindings_.size();)
        if ((bindings_[i].type == b.type && bindings_[i].channel == b.channel && bindings_[i].data1 == b.data1 && bindings_[i].shift == b.shift) ||
            (bindings_[i].action == b.action && (!perDeck || bindings_[i].deck == b.deck) && bindings_[i].shift == b.shift))
            bindings_.erase(bindings_.begin() + i);
        else ++i;
    bindings_.push_back(b);
    save();
}

void MidiManager::clearBinding(int action, int deck) {
    const bool perDeck = action >= 0 && action < (int)midiActions().size() && midiActions()[action].perDeck;
    bindings_.erase(std::remove_if(bindings_.begin(), bindings_.end(),
                                   [&](const MidiBinding& b) { return b.action == action && (!perDeck || b.deck == deck); }),
                    bindings_.end());
    save();
}

std::vector<MidiManager::Conflict> MidiManager::detectConflicts() const {
    std::vector<Conflict> out;
    for (size_t i = 0; i < bindings_.size(); ++i) {
        for (size_t j = i + 1; j < bindings_.size(); ++j) {
            const auto& a = bindings_[i];
            const auto& b = bindings_[j];
            if (a.type == b.type && a.channel == b.channel && a.data1 == b.data1 && a.shift == b.shift) {
                if (a.action != b.action || a.deck != b.deck) {
                    out.push_back({a.action, a.deck, b.action, b.deck, a.type, a.channel, a.data1, a.shift});
                }
            }
        }
    }
    return out;
}

const MidiBinding* MidiManager::findBinding(int action, int deck) const {
    const bool perDeck = action >= 0 && action < (int)midiActions().size() && midiActions()[action].perDeck;
    for (auto& b : bindings_) if (b.action == action && (!perDeck || b.deck == deck)) return &b;
    return nullptr;
}

std::vector<MidiCommand> MidiManager::poll() {
    std::deque<MidiEvent> evs;
    { std::lock_guard<std::mutex> lk(m_); evs.swap(queue_); }
    std::vector<MidiCommand> out;
    const auto& acts = midiActions();
    for (const MidiEvent& e : evs) {
        bool rackOnly = false;   // mando con mapeo integrado: solo cuentan las asignaciones del rack
        if (integrated(e.device)) {
            // Va entero al mapeo (y al monitor), salvo lo que se haya asignado al rack con «Aprender».
            bool forRack = false;
            if (e.status != 0xF0) {
                const uint8_t t = e.status >> 4, c = e.status & 0x0F, bt = t == 0x8 ? 0x9 : t;
                if (learnAction_ >= 0 && isRackAction(learnAction_)) forRack = t == 0x9 || t == 0xB;
                for (auto& b : bindings_)
                    if (isRackAction(b.action) && b.type == bt && b.channel == c && (b.data1 == e.data1 || (t == 0xB && b.data1 + 0x20 == e.data1))) forRack = true;
            }
            if (!forRack) {
                if (e.status != 0xF0) { recent_.push_back(e); if (recent_.size() > 16) recent_.pop_front(); }
                integratedQueue_.push_back(e);
                continue;
            }
            rackOnly = true;
        }
        if (e.status == 0xF0) continue;   // SysEx: solo lo usan los mapeos integrados
        const uint8_t type = e.status >> 4, ch = e.status & 0x0F;
        if (type != 0x8 && type != 0x9 && type != 0xB) continue;
        const uint8_t btype = type == 0x8 ? 0x9 : type;

        // Byte bajo de un control de 14 bits (cc + 0x20) ya asignado: refina el valor.
        if (type == 0xB && e.data1 >= 0x20 && e.data1 < 0x40) {
            bool direct = false;
            for (auto& b : bindings_) if (b.type == 0xB && b.channel == ch && b.data1 == e.data1) direct = true;
            if (!direct && learnAction_ < 0) {
                bool used = false;
                for (auto& b : bindings_) {
                    if (b.type != 0xB || b.channel != ch || b.data1 != e.data1 - 0x20) continue;
                    if (acts[b.action].kind != MidiKind::Absoluto) continue;
                    b.hiRes = true;
                    used = true;
                    if (b.shift && !shiftHeld_) continue;
                    const uint8_t msb = msb_[(ch << 8) | b.data1];
                    MidiCommand c;
                    c.action = b.action; c.deck = b.deck;
                    c.value = (msb * 128 + e.data2) / 16383.f;
                    if (b.invert != (b.action == MA_Pitch && settings_.pitchInvert)) c.value = 1.f - c.value;
                    out.push_back(c);
                }
                if (used) continue;
            }
        }
        recent_.push_back(e);
        if (recent_.size() > 16) recent_.pop_front();

        if (learnAction_ >= 0) {
            // Solo se aprende un mensaje del tipo adecuado: notas para botones y
            // CC para faders, potenciómetros y jogs (así, al "girar tocando el
            // plato" no se aprende la nota de tocar).
            const bool button = acts[learnAction_].kind == MidiKind::Boton;
            const bool fits = button ? ((type == 0x9 && e.data2 > 0) || (type == 0xB && e.data2 == 127)) : type == 0xB;
            if (fits) {
                MidiBinding b;
                b.action = learnAction_; b.deck = learnDeck_; b.type = btype; b.channel = ch; b.data1 = e.data1; b.shift = learnShift_;
                assign(b);
                learnAction_ = -1;
                learnedNow_ = true;
            }
            continue;
        }
        if (type == 0xB) msb_[(ch << 8) | e.data1] = e.data2;

        // Con SHIFT pulsado tienen prioridad las asignaciones de la capa SHIFT.
        bool shiftedMatch = false;
        if (shiftHeld_)
            for (auto& b : bindings_) if (b.shift && b.type == btype && b.channel == ch && b.data1 == e.data1) shiftedMatch = true;
        for (const MidiBinding& b : bindings_) {
            if (b.type != btype || b.channel != ch || b.data1 != e.data1) continue;
            if (b.action < 0 || b.action >= (int)acts.size()) continue;
            if (rackOnly && !isRackAction(b.action)) continue;
            if (b.shift && !shiftHeld_) continue;
            if (!b.shift && shiftedMatch && b.action != MA_Shift) continue;
            MidiCommand c;
            c.action = b.action; c.deck = b.deck;
            switch (acts[b.action].kind) {
            case MidiKind::Boton:
                c.pressed = (type == 0x9 && e.data2 > 0) || (type == 0xB && e.data2 >= 64);
                if (b.action == MA_Shift) shiftHeld_ = c.pressed;
                break;
            case MidiKind::Absoluto:
                c.value = e.data2 / 127.f;
                if (b.invert != (b.action == MA_Pitch && settings_.pitchInvert)) c.value = 1.f - c.value;
                break;
            case MidiKind::Relativo:
                c.delta = b.relMode == 0 ? (int)e.data2 - 64 : (e.data2 < 64 ? (int)e.data2 : (int)e.data2 - 128);
                break;
            }
            out.push_back(c);
        }
    }
    return out;
}

void MidiManager::sendShort(uint8_t status, uint8_t d1, uint8_t d2) {
    // Luces de las asignaciones propias: no se mandan a los mandos con mapeo integrado.
    const DWORD msg = (DWORD)status | ((DWORD)d1 << 8) | ((DWORD)d2 << 16);
    for (size_t i = 0; i < outHandles_.size(); ++i)
        if (outHandles_[i] && !integrated(outDevice_[i])) midiOutShortMsg((HMIDIOUT)outHandles_[i], msg);
}

void MidiManager::setLed(int action, int deck, bool on) {
    if (outHandles_.empty()) return;
    const bool enabled = settings_.leds;
    for (const MidiBinding& b : bindings_) {
        if (b.action != action || b.type != 0x9 || b.shift) continue;
        if (action >= 0 && action < (int)midiActions().size() && midiActions()[action].perDeck && b.deck != deck) continue;
        const uint8_t status = (uint8_t)(0x90 | b.channel);
        const uint8_t v = (on && enabled) ? 0x7F : 0x00;
        const uint32_t key = (status << 8) | b.data1;
        auto it = ledState_.find(key);
        if (it != ledState_.end() && it->second == v) continue;
        ledState_[key] = v;
        sendShort(status, b.data1, v);
    }
}

void MidiManager::resetLeds() {
    for (auto& kv : ledState_)
        if (kv.second) sendShort((uint8_t)(kv.first >> 8), (uint8_t)(kv.first & 0xFF), 0);
    ledState_.clear();
}

static std::wstring midiFile() { return appDataDir() + L"\\midi.json"; }

void MidiManager::load() {
    std::ifstream f(std::filesystem::path(midiFile()), std::ios::binary);
    if (!f) return;
    try {
        json j; f >> j;
        bindings_.clear();
        const auto& acts = midiActions();
        for (auto& jb : j.value("bindings", json::array())) {
            MidiBinding b;
            const std::string id = jb.value("action", "");
            b.action = -1;
            for (size_t i = 0; i < acts.size(); ++i) if (id == acts[i].id) b.action = (int)i;
            if (b.action < 0) continue;
            b.deck = jb.value("deck", 0); b.type = jb.value("type", 9); b.channel = jb.value("channel", 0);
            b.data1 = jb.value("data1", 0); b.relMode = jb.value("relMode", 0);
            b.invert = jb.value("invert", false); b.shift = jb.value("shift", false); b.hiRes = jb.value("hiRes", false);
            bindings_.push_back(b);
        }
        mappingChoice_.clear();
        if (j.contains("mapeos") && j["mapeos"].is_object())
            for (auto it = j["mapeos"].begin(); it != j["mapeos"].end(); ++it)
                if (it.value().is_string()) mappingChoice_[it.key()] = it.value().get<std::string>();
        if (j.contains("ajustes")) {
            const json& s = j["ajustes"];
            settings_.jogBendSens = s.value("jogBendSens", 1.f);
            settings_.jogScratchSens = s.value("jogScratchSens", 1.f);
            settings_.jogSeekSens = s.value("jogSeekSens", 1.f);
            settings_.touchScratch = s.value("touchScratch", true);
            settings_.softTakeover = s.value("softTakeover", true);
            settings_.leds = s.value("leds", true);
            settings_.pitchInvert = s.value("pitchInvert", false);
        }
    } catch (...) {
    }
}

void MidiManager::save() {
    json j; json arr = json::array();
    const auto& acts = midiActions();
    for (auto& b : bindings_) {
        if (b.action < 0 || b.action >= (int)acts.size()) continue;
        arr.push_back({{"action", acts[b.action].id}, {"deck", b.deck}, {"type", b.type}, {"channel", b.channel}, {"data1", b.data1},
                       {"relMode", b.relMode}, {"invert", b.invert}, {"shift", b.shift}, {"hiRes", b.hiRes}});
    }
    j["bindings"] = arr;
    json mp = json::object();
    for (auto& kv : mappingChoice_) mp[kv.first] = kv.second;
    j["mapeos"] = mp;
    j["ajustes"] = {{"jogBendSens", settings_.jogBendSens}, {"jogScratchSens", settings_.jogScratchSens}, {"jogSeekSens", settings_.jogSeekSens},
                    {"touchScratch", settings_.touchScratch}, {"softTakeover", settings_.softTakeover}, {"leds", settings_.leds},
                    {"pitchInvert", settings_.pitchInvert}};
    std::ofstream f(std::filesystem::path(midiFile()), std::ios::binary | std::ios::trunc);
    f << j.dump(1);
}

void MidiManager::loadPresetWeGO() {
    // DDJ-WeGO (primer modelo). Fuentes:
    //  - Comprobado por el usuario con «Aprender» (19-10-2026): volumen CC 0x13 en el
    //    canal del plato, pitch CC 0x00 (+0x20 de 14 bits), crossfader canal 7 CC 0x1F,
    //    canal 7 CC 0x17/0x18 (asignado por el usuario a la ganancia).
    //  - Patrón de la familia DDJ (WeGO3, mapeo comunitario de Mixxx): PLAY 0x0B, CUE 0x0C,
    //    SYNC 0x58, aro del jog CC 0x21, plato CC 0x22, tocar el plato nota 0x36,
    //    navegador canal 7 CC 0x40 / nota 0x41, carga canal 7 notas 0x46/0x47,
    //    auriculares canal 7 notas 0x54/0x55, SHIFT canal 7 nota 0x4F.
    //  Lo que no está comprobado conviene revisarlo con el asistente.
    bindings_.clear();
    auto add = [&](int act, int deck, uint8_t type, uint8_t ch, uint8_t d1, bool hiRes = false) {
        MidiBinding b; b.action = act; b.deck = deck; b.type = type; b.channel = ch; b.data1 = d1; b.hiRes = hiRes;
        bindings_.push_back(b);
    };
    for (int d = 0; d < 2; ++d) {
        const uint8_t ch = (uint8_t)d;
        add(MA_Play, d, 0x9, ch, 0x0B); add(MA_Cue, d, 0x9, ch, 0x0C); add(MA_Sync, d, 0x9, ch, 0x58);
        add(MA_Jog, d, 0xB, ch, 0x21); add(MA_JogScratch, d, 0xB, ch, 0x22); add(MA_JogTouch, d, 0x9, ch, 0x36);
        add(MA_Pitch, d, 0xB, ch, 0x00, true);
        add(MA_Volume, d, 0xB, ch, 0x13, true);
        add(MA_EqHigh, d, 0xB, ch, 0x07, true); add(MA_EqMid, d, 0xB, ch, 0x0B, true); add(MA_EqLow, d, 0xB, ch, 0x0F, true);
        add(MA_Gain, d, 0xB, 6, (uint8_t)(0x17 + d), true);
        add(MA_Load, d, 0x9, 6, (uint8_t)(0x46 + d));
        add(MA_Pfl, d, 0x9, 6, (uint8_t)(0x54 + d));
        add(MA_Hot1, d, 0x9, ch, 0x2E); add(MA_Hot2, d, 0x9, ch, 0x2F); add(MA_Hot3, d, 0x9, ch, 0x30); add(MA_Hot4, d, 0x9, ch, 0x31);
        add(MA_LoopHalve, d, 0x9, ch, 0x12); add(MA_LoopDouble, d, 0x9, ch, 0x13); add(MA_Loop4, d, 0x9, ch, 0x14);
    }
    add(MA_Crossfader, 0, 0xB, 6, 0x1F, true);
    add(MA_BrowseScroll, 0, 0xB, 6, 0x40);
    add(MA_BrowseLoad, 0, 0x9, 6, 0x41);
    add(MA_Shift, 0, 0x9, 6, 0x4F);
    save();
}

} // namespace rdj

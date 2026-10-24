#include "app.h"
#include "../audio/analysis.h"
#include "../core/util.h"
#include "../core/filename.h"
#include "covers.h"
#include "mandos_puente.h"
#include "../core/tags.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace rdj {

bool App::init(void* hwnd, bool forceNullAudio) {
    hwnd_ = hwnd;
    settings_.load();
    library_.setUseTagKeys(settings_.useTagKeys);
    library_.load();
    library_.checkTagKeys();   // pistas analizadas antes de la 0.6.2: ¿traen la tonalidad en la etiqueta?
    midi_.load();
    midi_.refresh();
    mandos_ = new MandoPuente(*this);
    mandos_->arrancar(true);   // mandos reconocidos: su mapeo arranca solo

    std::string err;
    if (!audio_.init(err, forceNullAudio)) notify(err, true);
    engine_.mixer.masterVol = settings_.masterVol;
    engine_.mixer.hpVol = settings_.hpVol;
    engine_.mixer.hpMix = settings_.hpMix;
    engine_.mixer.xfCurve = settings_.xfCurve;
    for (int d = 0; d < kNumDecks; ++d) engine_.deck[d].colorFx = settings_.colorFx;
    for (int d = 0; d < kNumDecks; ++d) { engine_.deck[d].quantize = settings_.quantize; engine_.deck[d].syncBar = settings_.syncBar; }
    pendingAudio_ = settings_.audio;
    applyAudioConfig();
    if (!library_.folders().empty()) library_.rescan();
    {
        // La lista "Preparación" de la versión 0.2 pasa a llamarse "Sidelist".
        std::lock_guard<std::recursive_mutex> lk(library_.mutex());
        auto& pls = library_.playlists();
        bool hasSide = false;
        for (auto& p : pls) if (p.name == "Sidelist") hasSide = true;
        for (auto& p : pls) if (p.name == "Preparación" && !hasSide) { p.name = "Sidelist"; library_.markDirty(); break; }
    }
    {   // Rejillas de VirtualDJ al cargar (se consultan desde el hilo del cargador).
        auto vdj = std::make_shared<VdjLookup>();
        loader_.externalGrid = [vdj](const std::wstring& path, ExternalGrid& g) {
            VdjSong s;
            if (!vdj->find(path, s)) return false;
            g.bpm = s.bpm; g.gridSec = s.gridSec;
            return true;
        };
    }
    loadSchedule();
    loadRack();
    restoreSession();
    restoreSource();
    // Primera vez con la 0.6.2: si hay VirtualDJ en el equipo, se ofrece traer su análisis.
    if (!settings_.vdjOffered) vdjFind_ = std::async(std::launch::async, [] { return findVdjDatabases(); });
    sel_.UserData = this;
    sel_.AdapterIndexToStorageId = &App::rowStorageId;
    return true;
}

void App::shutdown() {
    if (recorder_.active()) stopRecording();
    for (int d = 0; d < kNumDecks; ++d) persistDeckMeta(d, true);
    if (!autotest_.active) saveSession(true);
    rememberSource();
    settings_.masterVol = engine_.mixer.masterVol;
    settings_.hpVol = engine_.mixer.hpVol;
    settings_.hpMix = engine_.mixer.hpMix;
    settings_.xfCurve = engine_.mixer.xfCurve;
    settings_.colorFx = engine_.deck[0].colorFx;
    if (!autotest_.active) settings_.save();  // la prueba automática no cambia las preferencias
    library_.save();
    if (!autotest_.active) saveRack();
    if (mandos_) { mandos_->parar(); delete mandos_; mandos_ = nullptr; }   // apaga las luces de los mandos
    midi_.save();
    ui::CoverFetcher::get().saveMemory();
    audio_.close();
}

void App::notify(const std::string& msg, bool error) {
    if (msg.empty()) return;
    statusMsg_ = msg;
    statusErr_ = error;
    statusTime_ = now_;
    if (autotest_.active) autotest_.lines.push_back(std::string(error ? "  [aviso] " : "  [info] ") + msg);
}

void App::send(int d, Cmd c, double v, int i) {
    Command k{c, (uint8_t)d};
    k.d = v; k.i = i;
    if (!engine_.send(k)) notify("Cola de órdenes de audio llena (¿audio detenido?).", true);
}

double App::deckLenSec(int d) const { return deck_[d].track ? deck_[d].track->lengthSec() : 0; }
double App::deckPosSec(int d) const {
    const auto& t = deck_[d].track;
    return t ? engine_.deckStatus[d].posFrames.load() / t->sampleRate : 0;
}
double App::beatSec(int d) const { return deck_[d].meta.bpm > 0 ? 60.0 / deck_[d].meta.bpm : 0; }

// ------------------------------------------------------------------ audio

void App::applyAudioConfig() {
    const bool wasRec = recorder_.active();
    if (wasRec) stopRecording();  // la frecuencia de muestreo puede cambiar
    audio_.refreshDevices();
    settings_.audio = pendingAudio_;
    audio_.open(engine_, settings_.audio);
    const AudioState& s = audio_.state();
    if (!s.running) notify("No hay salida de audio: " + s.message, true);
    else if (!s.message.empty()) notify(s.message, s.simulated);
    else notify("Audio: " + s.deviceName + " (" + s.backend + ", " + std::to_string(s.sampleRate) + " Hz)");
    lostSince_ = -1;
    if (wasRec) notify("La grabación se detuvo al cambiar de dispositivo.", true);
}

void App::pollDevice() {
    if (audio_.deviceLost()) {
        if (lostSince_ < 0) {
            lostSince_ = now_;
            notify("Se perdió el dispositivo de audio \"" + audio_.state().deviceName + "\". Reintentando...", true);
        }
        if (now_ - lostSince_ > 0.5) {
            if (recorder_.active()) stopRecording();
            audio_.refreshDevices();
            audio_.open(engine_, settings_.audio);  // vuelve al predeterminado o al simulado si hace falta
            notify(audio_.state().message.empty() ? "Audio restablecido en " + audio_.state().deviceName : audio_.state().message,
                   audio_.state().simulated);
            lostSince_ = -1;
            lastRetry_ = now_;
        }
        return;
    }
    // En modo simulado por falta de dispositivo, reintentar el real cada 3 s.
    if (audio_.state().simulated && now_ - lastRetry_ > 3.0 && !autotest_.active) {
        lastRetry_ = now_;
        audio_.refreshDevices();
        if (!audio_.devices().empty()) {
            audio_.open(engine_, settings_.audio);
            if (!audio_.state().simulated) notify("Audio disponible: " + audio_.state().deviceName);
        }
    }
}

// ------------------------------------------------------------------ carga

void App::loadToDeck(int d, const std::wstring& path, bool force) {
    if (d < 0 || d >= kNumDecks) return;
    const std::wstring ext = extensionLower(path);
    if (!isSupportedAudioExt(ext)) {
        deck_[d].error = "Formato no compatible (" + (ext.empty() ? std::string("sin extensión") : toUtf8(ext)) + "). Admitidos: MP3, WAV, FLAC y OGG.";
        deck_[d].errorTime = now_;
        notify(deck_[d].error, true);
        return;
    }
    if (!force && settings_.confirmLoadWhilePlaying && engine_.deckStatus[d].playing.load()) {
        confirmDeck_ = d;
        confirmPath_ = path;
        return;
    }
    LoadJob job;
    job.deck = d;
    job.path = path;
    job.minBpm = settings_.minBpm;
    job.maxBpm = settings_.maxBpm;
    job.useExternalGrid = settings_.useVdjGrid;
    {
        std::lock_guard<std::recursive_mutex> lk(library_.mutex());
        if (LibTrack* lt = library_.find(path)) { job.meta = lt->meta; job.title = lt->title; job.artist = lt->artist; }
    }
    if (job.title.empty()) {
        // Fuera de la colección: las etiquetas del archivo (si no, el nombre).
        const TagInfo ti = readTags(path);
        job.title = ti.title; job.artist = ti.artist;
    }
    if (job.title.empty()) job.title = toUtf8(fileNameOf(path));
    deck_[d].error.clear();
    loader_.request(job);
}

void App::demoStart(double posA, double posB, bool play, bool syncB) {
    if (posA >= 0 && deck_[0].track) send(0, Cmd::Seek, posA * deck_[0].track->sampleRate);
    if (posB >= 0 && deck_[1].track) send(1, Cmd::Seek, posB * deck_[1].track->sampleRate);
    if (play) { togglePlay(0); togglePlay(1); }
    if (syncB) setSync(1, true);
}

void App::ejectDeck(int d) {
    if (engine_.deckStatus[d].playing.load()) { notify("Para expulsar, primero pausa el plato.", true); return; }
    persistDeckMeta(d, true);
    DeckUi& dk = deck_[d];
    Command c{Cmd::Unload, (uint8_t)d};
    c.gen = ++dk.gen;
    engine_.send(c);
    dk.pending.push_back({dk.gen, nullptr});
    dk.path.clear(); dk.title.clear(); dk.artist.clear();
    dk.meta = TrackMeta{};
    dk.sync = false;
    pushGrid(d);
}

void App::pollLoader() {
    LoadResult r;
    while (loader_.poll(r)) {
        DeckUi& dk = deck_[r.deck];
        if (!r.track) {
            if (r.error != "Cancelado.") {
                dk.error = r.error;
                dk.errorTime = now_;
                notify("Plato " + std::string(r.deck ? "B" : "A") + ": " + r.error, true);
            }
            continue;
        }
        persistDeckMeta(r.deck, true);  // guarda los cues de la pista anterior
        Command c{Cmd::Load, (uint8_t)r.deck};
        c.track = r.track.get();
        c.gen = ++dk.gen;
        dk.pending.push_back({dk.gen, r.track});
        dk.path = r.track->path;
        dk.title = r.track->title;
        dk.artist = r.track->artist;
        if (auto it = stemTitles_.find(dk.path); it != stemTitles_.end()) { dk.title = it->second; dk.artist = "Separador"; }
        dk.meta = r.track->meta;
        dk.error.clear();
        dk.endHandled = false;
        dk.sync = false;
        dk.playedSec = 0;
        dk.historyLogged = false;
        pushGrid(r.deck);
        engine_.send(c);
        if (dk.restorePos >= 0 && dk.restorePath == dk.path) {
            send(r.deck, Cmd::Seek, dk.restorePos * r.track->sampleRate);
            dk.restorePos = -1;
        }
        // Solo las pistas que se han podido abrir entran en la biblioteca.
        if (!library_.find(dk.path)) library_.addFiles({dk.path});
        if (auto it = stemTitles_.find(dk.path); it != stemTitles_.end()) {
            std::lock_guard<std::recursive_mutex> lk(library_.mutex());
            if (LibTrack* lt = library_.find(dk.path)) { lt->title = it->second; lt->artist = "Separador"; lt->searchKey = foldForSearch(lt->title + " separador"); library_.markDirty(); }
        }
        if (r.analyzedNow || !dk.meta.analyzed) library_.updateMeta(dk.path, dk.meta);
        if (settings_.coverDownload >= 1) fetchCovers({dk.path}, false);  // solo busca si no tiene carátula
        if (autotest_.active) autotest_.lines.push_back("  cargada en " + std::string(r.deck ? "B" : "A") + ": " + dk.title +
                                                        " (" + std::to_string(dk.meta.bpm) + " BPM)");
    }
}

void App::pollEngineAcks() {
    for (int d = 0; d < kNumDecks; ++d) {
        DeckUi& dk = deck_[d];
        const uint32_t ack = engine_.deckStatus[d].loadedGen.load(std::memory_order_acquire);
        // Solo se libera una pista cuando el motor confirma que ya usa otra.
        for (size_t i = 0; i < dk.pending.size();) {
            if (dk.pending[i].first <= ack) {
                if (dk.pending[i].first == ack) dk.track = dk.pending[i].second;
                dk.pending.erase(dk.pending.begin() + i);
            } else ++i;
        }
    }
}

void App::persistDeckMeta(int d, bool force) {
    DeckUi& dk = deck_[d];
    if (!dk.track || dk.path.empty() || dk.track->path != dk.path) return;
    const double sr = dk.track->sampleRate;
    const DeckStatus& s = engine_.deckStatus[d];
    bool changed = force;
    const double cue = s.cueFrames.load() / sr;
    if (std::fabs(cue - dk.meta.cueSec) > 1e-6) { dk.meta.cueSec = cue; changed = true; }
    for (int i = 0; i < kNumHotCues; ++i) {
        const double h = s.hotCues[i].load();
        const double hs = h < 0 ? -1.0 : h / sr;
        if (std::fabs(hs - dk.meta.hotCuesSec[i]) > 1e-6) {
            dk.meta.hotCuesSec[i] = hs;
            if (hs < 0) dk.meta.hotCueNames[i].clear();  // hot cue borrado (también desde el mando): fuera el nombre
            changed = true;
        }
    }
    if (changed) library_.updateMeta(dk.path, dk.meta);
}

void App::pushGrid(int d) {
    engine_.deck[d].bpm = deck_[d].meta.bpm;
    engine_.deck[d].gridAnchorSec = deck_[d].meta.gridAnchorSec;
}

// ------------------------------------------------------------------ transporte

void App::togglePlay(int d) {
    if (!deck_[d].track) return;
    const bool was = engine_.deckStatus[d].playing.load();
    send(d, Cmd::TogglePlay);
    if (was || !settings_.syncPhase) return;
    const int o = 1 - d;
    // Al arrancar: el plato que sigue sale alineado con el maestro; y si arranca el
    // maestro mientras el otro ya suena sincronizado, es el que arranca el que se alinea.
    if (engine_.deck[d].syncTo.load() == o) send(d, Cmd::SyncPhase, 0, o);
    else if (engine_.deck[o].syncTo.load() == d && engine_.deckStatus[o].playing.load())
        send(d, Cmd::SyncPhase, 1.0 / std::max(0.25, engine_.deck[o].syncFactor.load()), o);
}

// Relación de tempo para que "follower" siga a "master": la que menos mueve su pitch.
// Así una pista analizada a mitad o doble de tempo (64 frente a 128, 87 frente a 174) sincroniza igual.
double App::syncFactorFor(int f, int m) const {
    const double bf = deck_[f].meta.bpm, bm = deck_[m].meta.bpm;
    if (bf <= 0 || bm <= 0) return 1.0;
    const double other = bm * (1.0 + engine_.deck[m].pitch.load());
    const double cur = engine_.deck[f].pitch.load();
    double k = 1.0, best = std::fabs(other / bf - 1.0 - cur);
    for (double c : {2.0, 0.5}) {
        const double t = std::fabs(other / (bf * c) - 1.0 - cur);
        if (t + 0.02 < best) { best = t; k = c; }
    }
    return k;
}

void App::setSync(int d, bool on) {
    DeckUi& dk = deck_[d];
    const int o = 1 - d;
    if (!on) {
        dk.sync = false;
        engine_.deck[d].syncTo = -1;
        return;
    }
    if (!dk.track || !deck_[o].track) { notify("Para sincronizar hacen falta pistas en los dos platos.", true); return; }
    if (dk.meta.bpm <= 0 || deck_[o].meta.bpm <= 0) { notify("Para sincronizar, los dos platos necesitan BPM.", true); return; }
    DeckParams& p = engine_.deck[d];
    p.syncBar = settings_.syncBar;
    p.syncPhase = settings_.syncPhase;
    if (deck_[o].sync) {
        // El otro ya sigue a este plato: los dos quedan en SYNC y este marca el tempo
        // (MASTER). No se toca su tempo ni su posición.
        dk.sync = true;
        syncMaster_ = d;
        notify(std::string("SYNC en los dos platos: el ") + (d ? "B" : "A") + " marca el tempo (MASTER). Si se para, manda el otro.");
        return;
    }
    const double k = syncFactorFor(d, o);
    const double other = deck_[o].meta.bpm * (1.0 + engine_.deck[o].pitch.load());
    const double target = other / (dk.meta.bpm * k) - 1.0;
    float range = settings_.pitchRange[d] / 100.f;
    if (std::fabs(target) > range) {
        for (float r : {8.f, 16.f, 50.f})
            if (std::fabs(target) <= r / 100.f) { settings_.pitchRange[d] = r; range = r / 100.f; break; }
        if (std::fabs(target) > range) {
            notify("Tempos demasiado distintos para sincronizar (" + std::to_string((int)std::lround(target * 100)) + " %).", true);
            return;
        }
        notify("Rango de pitch del plato " + std::string(d ? "B" : "A") + " ampliado a ±" + std::to_string((int)settings_.pitchRange[d]) + " %.");
    }
    dk.sync = true;
    syncMaster_ = o;
    syncFollower_ = d;
    dk.pitch = (float)target;
    p.pitch = dk.pitch;
    p.syncFactor = k;
    p.syncTo = o;
    if (settings_.syncPhase) send(d, Cmd::SyncPhase, 0, o);
    if (k != 1.0) notify(std::string("SYNC con tempo ") + (k > 1 ? "doble" : "mitad") + ": la pista del plato " + (d ? "B" : "A") + " está analizada a " +
                         std::to_string((int)std::lround(dk.meta.bpm)) + " BPM (se cuenta como " + std::to_string((int)std::lround(dk.meta.bpm * k)) + ").");
}

void App::setSyncMaster(int d) {
    const int o = 1 - d;
    if (!deck_[d].track || !deck_[o].track || deck_[d].meta.bpm <= 0 || deck_[o].meta.bpm <= 0) return;
    if (!deck_[o].sync) {
        // El otro pasa a seguir a este plato.
        DeckParams& po = engine_.deck[o];
        po.syncFactor = syncFactorFor(o, d);
        po.syncBar = settings_.syncBar;
        po.syncPhase = settings_.syncPhase;
        deck_[o].sync = true;
        if (settings_.syncPhase) send(o, Cmd::SyncPhase, 0, d);
    }
    deck_[d].sync = true;
    syncMaster_ = d;
    notify(std::string("El plato ") + (d ? "B" : "A") + " marca el tempo (MASTER).");
}

void App::syncRealign(int d) {
    const int o = 1 - d;
    if (engine_.deck[d].syncTo.load() == o) { send(d, Cmd::SyncPhase, 0, o); notify("Fase realineada con el MASTER."); }
    else if (engine_.deck[o].syncTo.load() == d) { send(o, Cmd::SyncPhase, 0, d); notify("Fase del otro plato realineada con este."); }
}

void App::syncFollow() {
    // El motor lleva el SYNC (tempo y fase bloque a bloque); aquí se decide quién manda.
    // Con un solo plato en SYNC, ese sigue al otro. Con los dos, uno es MASTER y el otro le
    // sigue; si el MASTER se para y el otro suena, el MASTER pasa al que suena (sin saltos:
    // conserva el tempo que ya tenía).
    for (int d = 0; d < kNumDecks; ++d) {
        DeckUi& dk = deck_[d];
        const int o = 1 - d;
        if (dk.sync && (!dk.track || !deck_[o].track || dk.meta.bpm <= 0 || deck_[o].meta.bpm <= 0)) dk.sync = false;
    }
    auto playing = [&](int d) { return engine_.deckStatus[d].playing.load(); };
    int follower = -1, master = -1;
    if (deck_[0].sync && deck_[1].sync) {
        if (syncMaster_ != 0 && syncMaster_ != 1) syncMaster_ = playing(1) && !playing(0) ? 1 : 0;
        if (settings_.syncAutoMaster && !playing(syncMaster_) && playing(1 - syncMaster_)) syncMaster_ = 1 - syncMaster_;
        master = syncMaster_;
        follower = 1 - master;
    } else if (deck_[0].sync) { follower = 0; master = 1; }
    else if (deck_[1].sync) { follower = 1; master = 0; }
    syncMaster_ = master;
    if (follower >= 0 && follower != syncFollower_) engine_.deck[follower].syncFactor = syncFactorFor(follower, master);  // cambio de papeles
    syncFollower_ = follower;
    for (int d = 0; d < kNumDecks; ++d) {
        DeckUi& dk = deck_[d];
        DeckParams& p = engine_.deck[d];
        p.syncTo = d == follower ? master : -1;
        p.syncPhase = settings_.syncPhase;
        if (d == follower) {
            dk.pitch = p.pitch.load();
            if (std::fabs(dk.pitch) > settings_.pitchRange[d] / 100.f + 1e-6f) {
                if (dk.rangeBeforeAuto <= 0.f) dk.rangeBeforeAuto = settings_.pitchRange[d];
                for (float r : {8.f, 16.f, 50.f}) if (std::fabs(dk.pitch) <= r / 100.f) { settings_.pitchRange[d] = r; break; }
            } else if (dk.rangeBeforeAuto > 0.f && std::fabs(dk.pitch) <= dk.rangeBeforeAuto / 100.f - 0.002f) {
                settings_.pitchRange[d] = dk.rangeBeforeAuto;   // ya cabe en la escala de antes: vuelve a ella
                dk.rangeBeforeAuto = 0.f;
            }
            // ¿La fase no se estabiliza? (rejilla mal puesta en alguna de las dos pistas)
            const bool active = settings_.syncPhase && playing(d) && playing(master) && std::fabs(p.bend.load()) < 1e-6f && !dk.scratching;
            if (active && std::fabs(engine_.deckStatus[d].syncError.load()) > 0.06f) { if (dk.syncBadSince < 0) dk.syncBadSince = now_; }
            else dk.syncBadSince = -1;
        } else {
            dk.syncBadSince = -1;
            if (dk.rangeBeforeAuto > 0.f && std::fabs(dk.pitch) <= dk.rangeBeforeAuto / 100.f) { settings_.pitchRange[d] = dk.rangeBeforeAuto; dk.rangeBeforeAuto = 0.f; }
            if (std::fabs(p.pitch.load() - dk.pitch) > 1e-7f) p.pitch = dk.pitch;
        }
    }
}

void App::keyMatch(int d) {
    const int o = 1 - d;
    DeckUi& dk = deck_[d];
    const int target = soundingKey(o);
    if (!dk.track || dk.meta.key < 0 || target < 0) { notify("Para igualar el tono hacen falta las tonalidades de los dos platos.", true); return; }
    // Transposición mínima (-6..+6) que deja este plato en una tonalidad compatible con lo que suena en el otro.
    int best = 0, bestAbs = 99;
    for (int st = -6; st <= 6; ++st) {
        const int k = dk.meta.key < 12 ? ((dk.meta.key + st) % 12 + 12) % 12 : 12 + (((dk.meta.key - 12) + st) % 12 + 12) % 12;
        if (keysCompatible(k, target) && std::abs(st) < bestAbs) { bestAbs = std::abs(st); best = st; }
    }
    DeckParams& p = engine_.deck[d];
    p.keyShift = (float)best;
    p.keyLock = true;
    const int now = soundingKey(d);
    notify(std::string("Plato ") + (d ? "B" : "A") + (best ? (": tono " + std::string(best > 0 ? "+" : "") + std::to_string(best) + " para encajar con el plato ") : ": ya encaja con el plato ") +
           (o ? "B" : "A") + " (" + keyCode(now) + " con " + keyCode(target) + "). Key lock activado.");
}

void App::tapTempo(int d) {
    DeckUi& dk = deck_[d];
    if (!dk.track) return;
    if (!dk.taps.empty() && now_ - dk.taps.back() > 2.0) dk.taps.clear();
    dk.taps.push_back(now_);
    if (dk.taps.size() > 8) dk.taps.erase(dk.taps.begin());
    if (dk.taps.size() >= 4) {
        const double avg = (dk.taps.back() - dk.taps.front()) / (dk.taps.size() - 1);
        // BPM de la pista sin pitch: lo que se oye es tempo * (1 + pitch).
        dk.meta.bpm = std::round(60.0 / avg / (1.0 + dk.pitch) * 100) / 100;
        const double beat = 60.0 / dk.meta.bpm;
        dk.meta.gridAnchorSec = std::fmod(deckPosSec(d), 4 * beat);
        dk.meta.analyzed = true;
        dk.meta.gridSrc = 3;
        pushGrid(d);
        library_.updateMeta(dk.path, dk.meta);
    }
}

// ------------------------------------------------------------------ grabación

void App::startRecording() {
    std::time_t t = std::time(nullptr);
    std::tm lt{};
    localtime_s(&lt, &t);
    wchar_t name[96];
    wcsftime(name, 96, L"Mezcla %Y-%m-%d %H-%M-%S.wav", &lt);
    std::error_code ec;
    const auto dir = recordingFolder();
    std::filesystem::create_directories(dir, ec);
    if (ec) { notify("No se puede crear la carpeta de grabación: " + ec.message(), true); return; }
    std::wstring base = safeFileComponent(fromUtf8(settings_.recName));
    std::wstring path = dir + L"\\" + base + L" " + std::wstring(name).substr(7);
    const auto original = std::filesystem::path(path);
    for (int n = 2; std::filesystem::exists(path); ++n)
        path = (original.parent_path() / (original.stem().wstring() + L" (" + std::to_wstring(n) + L")" + original.extension().wstring())).wstring();
    std::string err;
    if (!recorder_.start(engine_, path, (RecFormat)settings_.recFormat, err)) { notify(err, true); return; }
    recordingFormat_ = settings_.recFormat;
    recordingTracklist_ = settings_.recTracklist;
    recLog_.clear();
    for (int d = 0; d < 2; ++d) { recLastPath_[d].clear(); recLastPlaying_[d] = false; }
    recordingTick();
    notify("Grabando en " + toUtf8(path));
}

void App::stopRecording() {
    if (!recorder_.active()) return;
    recorder_.stop();
    lastRecording_ = recorder_.path();
    if (recordingTracklist_) {
        auto txt = std::filesystem::path(lastRecording_); txt.replace_extension(L".txt");
        std::ofstream out(txt, std::ios::binary);
        out << "DJ ELVER GALARGA — registro de reproducción (no detecta audibilidad del master)\n";
        for (const auto& line : recLog_) out << line << "\n";
        if (!out) notify("No se pudo guardar el listado de la sesión.", true);
    }
    const uint64_t dropped = engine_.status.recDropped.load();
    const std::string err = recorder_.error();
    library_.addFiles({lastRecording_});
    int list = -1;
    {
        std::lock_guard<std::recursive_mutex> lk(library_.mutex());
        for (size_t i = 0; i < library_.playlists().size(); ++i) if (library_.playlists()[i].name == "Grabaciones") list = (int)i;
    }
    if (list < 0) list = library_.createPlaylist("Grabaciones");
    library_.addToPlaylist(list, {lastRecording_});
    if (!err.empty()) notify(err, true);
    else if (dropped) notify("Grabación guardada, pero se perdieron " + std::to_string(dropped / 2) + " muestras (disco lento).", true);
    else notify("Grabación guardada (" + formatTime(recorder_.seconds(), false) + "): " + toUtf8(lastRecording_));
}

// ------------------------------------------------------------------ MIDI

void App::pollMidi() {
    MidiSettings& ms = midi_.settings();
    // Soft takeover: un control físico solo actúa cuando "recoge" el valor actual,
    // así no hay saltos si el valor se cambió con el ratón.
    auto absolute = [&](const MidiCommand& c, float current01, auto apply) {
        Pickup& pk = pickup_[c.action * 2 + (c.deck & 1)];
        if (!ms.softTakeover) { apply(c.value); pk.engaged = true; pk.lastApplied = c.value; return; }
        if (pk.engaged && std::fabs(current01 - pk.lastApplied) > 0.02f) pk.engaged = false;  // cambiado fuera del mando
        if (!pk.engaged) {
            const bool isClose = std::fabs(c.value - current01) < 0.04f;
            const bool crossed = pk.hasPrev && (pk.prev - current01) * (c.value - current01) <= 0.f;
            if (isClose || crossed) pk.engaged = true;
        }
        pk.prev = c.value; pk.hasPrev = true;
        if (pk.engaged) { apply(c.value); pk.lastApplied = c.value; }
        else midiPickupHint_ = now_;
    };
    for (const MidiCommand& c : midi_.poll()) {
        const int d = c.deck & 1;
        DeckParams& p = engine_.deck[d];
        DeckUi& dk = deck_[d];
        midiHit_[c.action * 2 + d] = now_;
        switch (c.action) {
        case MA_Play: if (c.pressed) togglePlay(d); break;
        case MA_Cue: send(d, c.pressed ? Cmd::CueDown : Cmd::CueUp); break;
        case MA_Sync: if (c.pressed) setSync(d, !dk.sync); break;
        case MA_Pfl: if (c.pressed) p.pfl = !p.pfl.load(); break;
        case MA_Hot1: case MA_Hot2: case MA_Hot3: case MA_Hot4: case MA_Hot5: case MA_Hot6: case MA_Hot7: case MA_Hot8:
            if (c.pressed) send(d, midi_.shiftHeld() ? Cmd::HotCueDelete : Cmd::HotCue, 0, c.action - MA_Hot1);
            break;
        case MA_LoopIn: if (c.pressed) send(d, Cmd::LoopIn); break;
        case MA_LoopOut: if (c.pressed) send(d, Cmd::LoopOut); break;
        case MA_Loop4: if (c.pressed) send(d, Cmd::AutoLoop, 4.0); break;
        case MA_LoopHalve: if (c.pressed) send(d, Cmd::LoopHalve); break;
        case MA_LoopDouble: if (c.pressed) send(d, Cmd::LoopDouble); break;
        case MA_LoopExit: if (c.pressed) send(d, Cmd::LoopExit); break;
        case MA_Reloop: if (c.pressed) send(d, engine_.deckStatus[d].loopActive.load() ? Cmd::LoopExit : Cmd::Reloop); break;
        case MA_BendUp: dk.buttonBend = c.pressed ? 0.04f : 0.f; break;
        case MA_BendDown: dk.buttonBend = c.pressed ? -0.04f : 0.f; break;
        case MA_Load:
        case MA_BrowseLoad:
            if (c.pressed) {
                const std::wstring path = selectedLibraryPath();
                if (path.empty()) { notify("Selecciona una pista en la biblioteca para cargarla.", true); break; }
                int target = d;
                if (c.action == MA_BrowseLoad)
                    target = !engine_.deckStatus[0].playing.load() ? 0 : !engine_.deckStatus[1].playing.load() ? 1 : -1;
                if (target >= 0) loadToDeck(target, path);
                else notify("Los dos platos están sonando.", true);
            }
            break;
        case MA_Slip: if (c.pressed) p.slip=!p.slip.load(); break;
        case MA_Reverse: if (c.pressed) p.reverse=!p.reverse.load(); break;
        case MA_ReverseHold: p.reverse=c.pressed; break;
        case MA_RollQuarter: case MA_RollOne: send(d,c.pressed?Cmd::RollStart:Cmd::RollEnd,c.action==MA_RollQuarter?0.25:1.0); break;
        case MA_Backspin: if(c.pressed) send(d,Cmd::Backspin,1.0); break;
        case MA_Brake: if(c.pressed) send(d,Cmd::Brake,1.0); break;
        case MA_Vinyl: if(c.pressed) p.vinyl=!p.vinyl.load(); break;
        case MA_FxOn: if (c.pressed) p.fxOn = !p.fxOn.load(); break;
        case MA_DeckFxClear: if (c.pressed) clearEffectsOf(d); break;
        case MA_Volume: absolute(c, p.fader.load(), [&](float v) { p.fader = v; }); break;
        case MA_Gain: absolute(c, (p.gainDb.load() + 12.f) / 24.f, [&](float v) { p.gainDb = -12.f + 24.f * v; }); break;
        case MA_EqHigh: absolute(c, p.eqHigh.load(), [&](float v) { p.eqHigh = v; }); break;
        case MA_EqMid: absolute(c, p.eqMid.load(), [&](float v) { p.eqMid = v; }); break;
        case MA_EqLow: absolute(c, p.eqLow.load(), [&](float v) { p.eqLow = v; }); break;
        case MA_Filter:
            absolute(c, (p.filter.load() + 1.f) * 0.5f, [&](float v) { const float f = v * 2.f - 1.f; p.filter = std::fabs(f) < 0.03f ? 0.f : f; });
            break;
        case MA_Pitch: {
            const float r = pitchRange(d);
            // Valor 0 = arriba del fader = más lento (como en la interfaz).
            absolute(c, (dk.pitch / r + 1.f) * 0.5f, [&](float v) { if (p.syncTo.load() >= 0) dk.sync = false; dk.pitch = (v * 2.f - 1.f) * r; p.pitch = dk.pitch; });
            break;
        }
        case MA_FxAmount: absolute(c, p.fxAmount.load(), [&](float v) { p.fxAmount = v; }); break;
        case MA_Crossfader: absolute(c, engine_.mixer.crossfader.load(), [&](float v) { engine_.mixer.crossfader = v; }); break;
        case MA_MasterVol: absolute(c, engine_.mixer.masterVol.load(), [&](float v) { engine_.mixer.masterVol = v; }); break;
        case MA_HpVol: absolute(c, engine_.mixer.hpVol.load(), [&](float v) { engine_.mixer.hpVol = v; }); break;
        case MA_HpMix: absolute(c, engine_.mixer.hpMix.load(), [&](float v) { engine_.mixer.hpMix = v; }); break;
        case MA_JogTouch:
            dk.jogTouched = c.pressed;
            if (ms.touchScratch && dk.track) {
                p.scratching = c.pressed;
                p.scratchVelocity = 0.f;
                dk.scratchTicks = 0;
            }
            break;
        case MA_JogScratch:
            if (!dk.track) break;
            if (dk.jogTouched && ms.touchScratch) { dk.scratchTicks += c.delta; break; }
            [[fallthrough]];  // sin tocar: igual que el aro
        case MA_Jog:
            if (!dk.track) break;
            if (engine_.deckStatus[d].playing.load()) {
                // Empujón proporcional a la velocidad de giro (como un plato de verdad): 720 pasos = 1 vuelta,
                // y una vuelta por segundo acelera un 8 %. Un toque suave da un 1–3 %.
                dk.nudgeAcc += c.delta / 720.f * kJogBendPerRevSec * ms.jogBendSens;
            } else {
                // En pausa: desplazamiento fino (720 pasos ≈ una vuelta de vinilo de 1,8 s).
                send(d, Cmd::Seek, engine_.deckStatus[d].posFrames.load() + c.delta * 0.0025 * ms.jogSeekSens * dk.track->sampleRate);
            }
            break;
        case MA_BrowseScroll:
        case MA_BrowseUp:
        case MA_BrowseDown: {
            if (c.action != MA_BrowseScroll && !c.pressed) break;
            int step = c.action == MA_BrowseScroll ? c.delta : (c.action == MA_BrowseUp ? -1 : 1);
            if (midi_.shiftHeld()) step *= 10;
            browseMove(step);
            break;
        }
        default:
            if (isRackAction(c.action))
                rackMidi(c, [&](const MidiCommand& cc, float cur, const std::function<void(float)>& ap) { absolute(cc, cur, ap); });
            break;
        }
    }
    // Mandos con mapeo de Mixxx: sus mensajes, temporizadores, scratch y luces.
    if (mandos_) mandos_->tick();
    if (now_ >= midiScanAt_) {
        midiScanAt_ = now_ + 2.0;
        if (midi_.devicesChanged()) midiRescan(true);   // se ha enchufado o desenchufado un mando
    }
    const float dt = std::max(1e-3f, ImGui::GetIO().DeltaTime);
    for (int d = 0; d < kNumDecks; ++d) {
        DeckUi& dk = deck_[d];
        // Scratch con el plato: pasos del jog -> velocidad (720 pasos = 1,8 s de audio).
        if (dk.jogTouched && ms.touchScratch && dk.track && !dk.scratching) {
            const float vel = dk.scratchTicks * 0.0025f * ms.jogScratchSens / dt;
            engine_.deck[d].scratchVelocity = std::clamp(vel, -8.f, 8.f);
            dk.scratchTicks = 0;
        }
        // Empuje del jog: la velocidad de giro, suavizada (sube en ~40 ms, se suelta en ~120 ms),
        // igual a 30, 60 o 144 fotogramas por segundo. El de los botones dura mientras se mantienen.
        {
            const float target = engine_.deckStatus[d].playing.load() ? dk.nudgeAcc / dt : 0.f;
            dk.nudgeAcc = 0.f;
            const float tau = std::fabs(target) > std::fabs(dk.jogBend) ? 0.04f : 0.12f;
            dk.jogBend += (target - dk.jogBend) * (1.f - std::exp(-dt / tau));
            dk.jogBend = std::clamp(dk.jogBend, -kJogBendMax, kJogBendMax);
            if (std::fabs(dk.jogBend) < 0.0005f && target == 0.f) dk.jogBend = 0.f;
        }
        const float wheel = engine_.deckStatus[d].playing.load() ? std::clamp(dk.mxWheel * 0.1f, -kJogBendMax, kJogBendMax) : 0.f;
        engine_.deck[d].bend = std::clamp(dk.jogBend + dk.buttonBend + dk.mxTempBend + wheel, -0.15f, 0.15f);
    }
    // Luces del mando
    if (now_ < ledTestUntil_) {
        for (int a = 0; a < MA_Count; ++a)
            if (midiActions()[a].kind == MidiKind::Boton) for (int d = 0; d < kNumDecks; ++d) midi_.setLed(a, d, true);
        return;
    }
    for (int d = 0; d < kNumDecks; ++d) {
        const DeckStatus& st = engine_.deckStatus[d];
        const bool playing = st.playing.load();
        midi_.setLed(MA_Play, d, playing);
        midi_.setLed(MA_Cue, d, deck_[d].track && !playing && std::fmod(now_, 1.0) < 0.5);
        midi_.setLed(MA_Sync, d, deck_[d].sync);
        midi_.setLed(MA_Pfl, d, engine_.deck[d].pfl.load());
        for (int i = 0; i < 8; ++i) midi_.setLed(MA_Hot1 + i, d, st.hotCues[i].load() >= 0);
        midi_.setLed(MA_Loop4, d, st.loopActive.load());
        midi_.setLed(MA_Reloop, d, st.loopActive.load());
        midi_.setLed(MA_FxOn, d, engine_.deck[d].fxOn.load());
    }
    rackLeds();
}

void App::midiRescan(bool notifyNew) {
    if (mandos_) mandos_->parar();
    midi_.refresh();
    if (mandos_) mandos_->arrancar(notifyNew);
}

void App::browseMove(int step) {
    if (view_.empty() || step == 0) return;
    cursorRow_ = std::clamp(cursorRow_ + step, 0, (int)view_.size() - 1);
    scrollToCursor_ = true;
    libSelectedPath_ = view_[cursorRow_].path;
    sel_.UserData = this;
    sel_.Clear();
    sel_.SetItemSelected(rowStorageId(&sel_, cursorRow_), true);
}

void App::browseSourceStep(int step) {
    // Orden: Colección, Favoritos, las listas del usuario y Sugerencias.
    std::vector<BrowseSrc> order;
    order.push_back(BrowseSrc{Src::Coleccion, L"", -1});
    order.push_back(BrowseSrc{Src::Lista, L"", ensurePlaylist("Favoritos")});
    {
        std::lock_guard<std::recursive_mutex> lk(library_.mutex());
        const auto& pls = library_.playlists();
        for (size_t i = 0; i < pls.size(); ++i) {
            const std::string& n = pls[i].name;
            if (n == "Sidelist" || n == "Automix" || n == "Favoritos" || n == "Grabaciones") continue;
            if (n.rfind("Historial", 0) == 0) continue;
            order.push_back(BrowseSrc{Src::Lista, L"", (int)i});
        }
    }
    order.push_back(BrowseSrc{Src::Sugerencias, L"", -1});
    int cur = 0;
    for (size_t i = 0; i < order.size(); ++i) if (order[i] == src_) cur = (int)i;
    const int n = (int)order.size();
    const int next = ((cur + step) % n + n) % n;
    selectSource(order[next]);
}

void App::loadSelectedInto(int deck, bool play) {
    const std::wstring path = selectedLibraryPath();
    if (path.empty()) { notify("Selecciona una pista en la biblioteca para cargarla.", true); return; }
    int target = deck;
    if (target < 0) target = !engine_.deckStatus[0].playing.load() ? 0 : !engine_.deckStatus[1].playing.load() ? 1 : -1;
    if (target < 0) { notify("Los dos platos están sonando.", true); return; }
    if (engine_.deckStatus[target].playing.load()) { notify("Ese plato está sonando: páralo antes de cargar otra pista.", true); return; }
    loadToDeck(target, path);
    if (play) deck_[target].mxPlayOnLoad = path;
}

// ------------------------------------------------------------------ arrastrar y soltar

void App::onDropFiles(const std::vector<std::wstring>& files, ImVec2 pos) {
    auto inside = [&](const ImVec2* r) { return pos.x >= r[0].x && pos.x < r[1].x && pos.y >= r[0].y && pos.y < r[1].y; };
    std::vector<std::wstring> expanded;
    for (auto& f : files) {
        const DWORD attr = GetFileAttributesW(f.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) { library_.addFolder(f); notify("Carpeta añadida: " + toUtf8(f)); continue; }
        expanded.push_back(f);
    }
    if (expanded.empty()) return;
    if (mode_ == 1 && pos.x >= sepRect_[0].x && pos.x < sepRect_[1].x && pos.y >= sepRect_[0].y && pos.y < sepRect_[1].y) {
        for (auto& f : expanded) separateFile(f);
        return;
    }
    for (int d = 0; d < kNumDecks; ++d) {
        if (inside(deckRect_[d]) || inside(waveRect_[d])) { loadToDeck(d, expanded[0]); return; }
    }
    if (settings_.showSide && inside(sideRect_)) {
        auto ok = library_.addFiles(expanded);
        if (settings_.sideTab == 1) library_.addToPlaylist(ensurePlaylist("Automix"), ok);
        else addToSidelist(ok);
        const int bad = (int)(expanded.size() - ok.size());
        notify(std::to_string(ok.size()) + " pista(s) añadidas" + (bad ? ", " + std::to_string(bad) + " ignoradas (formato no compatible)" : "") + ".", bad > 0);
        return;
    }
    // Sobre el navegador (o fuera de los platos): a la colección y, si hay una lista abierta, también a ella.
    auto ok = library_.addFiles(expanded);
    if (src_.kind == Src::Lista && src_.list >= 0 && inside(listRect_)) library_.addToPlaylist(src_.list, ok);
    const int bad = (int)(expanded.size() - ok.size());
    notify(std::to_string(ok.size()) + " archivo(s) añadidos" + (bad ? ", " + std::to_string(bad) + " ignorados (formato no compatible)" : "") + ".", bad > 0);
}

// ------------------------------------------------------------------ prueba automática

void App::startAutotest(const std::wstring& a, const std::wstring& b, const std::wstring& logPath) {
    autotest_ = Autotest{};
    autotest_.active = true;
    autotest_.a = a; autotest_.b = b; autotest_.log = logPath;
    settings_.confirmLoadWhilePlaying = false;
}

void App::updateAutotest() {
    Autotest& t = autotest_;
    if (!t.active || t.done) return;
    auto log = [&](const std::string& s) { t.lines.push_back(s); };
    auto check = [&](bool ok, const std::string& what) { log(std::string(ok ? "OK    " : "FALLO ") + what); if (!ok) ++t.failures; };
    const double el = now_ - t.stepT;
    auto next = [&] { ++t.step; t.stepT = now_; };
    if (t.jogSimUntil > 0 && t.mxSim && mandos_) {   // aro del jog girando a velocidad constante
        if (now_ < t.jogSimUntil) {
            t.jogSimAcc += t.jogSimRate * std::max(0.001, (double)ImGui::GetIO().DeltaTime);
            for (; t.jogSimAcc >= 1.0; t.jogSimAcc -= 1.0) mandos_->simularMidi(t.mxSim, 0xB0, t.jogSimCc, 0x41);
            t.jogSimPeak = std::max(t.jogSimPeak, std::fabs(engine_.deck[0].bend.load()));
        } else t.jogSimUntil = -1;
    }
    switch (t.step) {
    case 0:
        log("== Prueba automática de DJ Elver Galarga ==");
        log("Audio: " + audio_.state().backend + " / " + audio_.state().deviceName + " a " + std::to_string(audio_.state().sampleRate) +
            " Hz, periodo " + std::to_string(audio_.state().periodFrames) + " fotogramas");
        t.t0 = now_;
        engine_.mixer.masterVol = 0.35f;
        loadToDeck(0, t.a, true);
        loadToDeck(1, t.b, true);
        next();
        break;
    case 1:
        if (deck_[0].track && deck_[1].track) {
            check(true, "carga de dos pistas en " + std::to_string((int)(el * 1000)) + " ms (decodificación + análisis en segundo plano)");
            for (int d = 0; d < 2; ++d) { engine_.deck[d].fader = 0.8f; engine_.deck[d].eqLow = engine_.deck[d].eqMid = engine_.deck[d].eqHigh = 0.5f; }
            engine_.mixer.crossfader = 0.5f;
            send(0, Cmd::Seek, 30.0 * deck_[0].track->sampleRate);
            send(1, Cmd::Seek, 30.0 * deck_[1].track->sampleRate);
            togglePlay(0);
            togglePlay(1);
            setSync(1, true);
            startRecording();
            t.frames0 = engine_.status.framesRendered.load();
            next();
        } else if (el > 40) { check(false, "las pistas no se cargaron en 40 s"); t.step = 99; }
        break;
    case 2:
        if (el > 6.0) {
            const uint64_t fr = engine_.status.framesRendered.load() - t.frames0;
            check(fr > 5.5 * audio_.state().sampleRate, "el dispositivo procesó " + std::to_string(fr) + " fotogramas en 6 s");
            check(deckPosSec(0) > 35.5 && deckPosSec(1) > 35.5, "ambos platos avanzan (A " + formatTime(deckPosSec(0)) + ", B " + formatTime(deckPosSec(1)) + ")");
            check(engine_.deckStatus[0].playing && engine_.deckStatus[1].playing, "reproducción simultánea");
            check(masterHold_[0] > 0.01f || engine_.status.masterRmsL.load() > 0.001f, "hay señal en el master");
            check(engine_.status.cpuLoad.load() < 0.5f, "carga del hilo de audio " + std::to_string((int)(engine_.status.cpuLoad.load() * 100)) + " %");
            if (deck_[0].meta.bpm > 0 && deck_[1].meta.bpm > 0)
                check(std::fabs(engine_.effectiveBpm(1) - engine_.effectiveBpm(0)) < 0.01, "sync: B a " + std::to_string(engine_.effectiveBpm(1)) + " BPM");
            t.step = 40; t.stepT = now_;   // SYNC con MASTER (0.7) y después los controles
        }
        break;
    case 40:
        // SYNC en los dos platos: A ya es la referencia de B, así que A pasa a ser el MASTER.
        check(syncMaster_ == 0 && syncFollower_ == 1, "SYNC: con SYNC solo en B, A marca el tempo");
        setSync(0, true);
        check(deck_[0].sync && deck_[1].sync && syncMaster_ == 0, "SYNC en los dos platos: A sigue siendo el MASTER");
        deck_[0].pitch = 0.03f; engine_.deck[0].pitch = 0.03f;   // el MASTER mueve su pitch
        t.step = 41; t.stepT = now_;
        break;
    case 41:
        if (el > 2.0) {
            check(deck_[0].sync && std::fabs(engine_.effectiveBpm(1) - engine_.effectiveBpm(0)) < 0.01 && std::fabs(deck_[0].pitch - 0.03f) < 1e-4f,
                  "SYNC: el MASTER mueve su pitch sin perder el SYNC y B le sigue (" + std::to_string(engine_.effectiveBpm(1)) + " BPM)");
            t.syncBpm = engine_.effectiveBpm(1);
            send(0, Cmd::Pause);
            t.step = 42; t.stepT = now_;
        }
        break;
    case 42:
        if (el > 1.5) {
            check(syncMaster_ == 1 && syncFollower_ == 0, "SYNC: al parar el MASTER (A), manda el que suena (B)");
            check(std::fabs(engine_.effectiveBpm(1) - t.syncBpm) < 0.01, "SYNC: B conserva el tempo al pasar a MASTER (" + std::to_string(engine_.effectiveBpm(1)) + " BPM)");
            togglePlay(0);
            t.step = 43; t.stepT = now_;
        }
        break;
    case 43:
        if (el > 2.5) {
            const float e = engine_.deckStatus[0].syncError.load();
            check(engine_.deckStatus[0].playing.load() && std::fabs(engine_.effectiveBpm(0) - engine_.effectiveBpm(1)) < 0.01 && std::fabs(e) < 0.03,
                  "SYNC: A vuelve a sonar alineado con el nuevo MASTER (fase " + std::to_string(e) + " pulsos)");
            // Vuelta al estado de la prueba: B sigue a A, pitch 0.
            setSync(0, false);
            deck_[0].pitch = 0.f; engine_.deck[0].pitch = 0.f;
            // Controles: crossfader, EQ, filtro, loop, hot cue
            engine_.mixer.crossfader = 0.f;
            engine_.deck[0].eqLow = 0.f;
            engine_.deck[0].filter = -0.5f;
            send(0, Cmd::AutoLoop, 4.0);
            t.step = 3; t.stepT = now_;
        }
        break;
    case 3:
        if (el > 2.5) {
            check(engine_.deckStatus[0].loopActive.load(), "loop de 4 pulsos activo");
            const double in = engine_.deckStatus[0].loopIn.load(), out = engine_.deckStatus[0].loopOut.load();
            const double p = engine_.deckStatus[0].posFrames.load();
            check(p >= in - 1 && p <= out + 1, "posición dentro del loop");
            send(0, Cmd::LoopExit);
            engine_.deck[0].eqLow = 0.5f; engine_.deck[0].filter = 0.f;
            engine_.mixer.crossfader = 0.5f;
            send(0, Cmd::HotCue, 0, 0);  // fija hot cue 1
            next();
        }
        break;
    case 4:
        if (el > 1.0) {
            check(engine_.deckStatus[0].hotCues[0].load() >= 0, "hot cue 1 fijado");
            send(0, Cmd::HotCue, 0, 0);  // salta
            // Archivo incompatible: debe fallar sin afectar a lo que suena.
            const std::wstring bad = appDataDir() + L"\\prueba_incompatible.mp3";
            { std::ofstream(std::filesystem::path(bad)) << "no es audio"; }
            loadToDeck(1, bad, true);
            t.badFile = bad;
            next();
        }
        break;
    case 5:
        if (el > 2.0) {
            check(!deck_[1].error.empty(), "archivo incompatible rechazado: \"" + deck_[1].error + "\"");
            check(engine_.deckStatus[1].playing.load() && deck_[1].track != nullptr, "el plato B sigue sonando tras el error");
            check(library_.find(t.badFile) == nullptr, "el archivo incompatible no entra en la biblioteca");
            DeleteFileW(t.badFile.c_str());
            stopRecording();
            next();
        }
        break;
    case 6: {
        std::string err;
        TrackPtr rec = decodeFile(lastRecording_, err);
        check(rec != nullptr, "la grabación se puede abrir: " + toUtf8(lastRecording_) + (err.empty() ? "" : " (" + err + ")"));
        if (rec) {
            double sum = 0; float pk = 0;
            for (float v : rec->pcm) { sum += (double)v * v; pk = std::max(pk, std::fabs(v)); }
            const double rmsDb = 10 * std::log10(sum / std::max<size_t>(1, rec->pcm.size()) + 1e-20);
            // 6 s de reproducción + 6 s de pruebas de SYNC con MASTER + 5,5 s de controles.
            check(rec->lengthSec() > 16.5 && rec->lengthSec() < 19.5, "duración de la grabación " + formatTime(rec->lengthSec()));
            check(rmsDb > -45.0, "nivel RMS de la grabación " + std::to_string((int)rmsDb) + " dBFS");
            check(pk <= 0.9661f, "sin saturación (pico " + std::to_string(pk) + ")");
            check(engine_.status.recDropped.load() == 0, "sin muestras perdidas");
        }
        // Reproducir la grabación en el plato A.
        send(0, Cmd::Pause);
        send(1, Cmd::Pause);
        loadToDeck(0, lastRecording_, true);
        next();
        break;
    }
    case 7:
        if (deck_[0].track && deck_[0].path == lastRecording_) {
            engine_.mixer.crossfader = 0.f;
            send(0, Cmd::Play);
            next();
        } else if (el > 20) { check(false, "la grabación no se cargó en el plato"); t.step = 99; }
        break;
    case 8:
        if (el > 3.0) {
            check(deckPosSec(0) > 2.5, "la grabación se reproduce en el plato A (" + formatTime(deckPosSec(0)) + ")");
            // Cambio de dispositivo con la pista sonando.
            const double before = deckPosSec(0);
            audio_.refreshDevices();
            int ok = 0;
            for (auto& dv : audio_.devices()) {
                AudioConfig c = settings_.audio;
                c.deviceName = dv.name;
                audio_.open(engine_, c);
                ok += audio_.state().running && !audio_.state().simulated && audio_.state().deviceName == dv.name;
                log("  cambio a \"" + dv.name + "\": " + (audio_.state().running ? "abierto" : "fallo") + " " + audio_.state().message);
                Sleep(300);
            }
            audio_.open(engine_, settings_.audio);
            check(ok == (int)audio_.devices().size(), "cambio entre " + std::to_string(audio_.devices().size()) + " dispositivos");
            check(deckPosSec(0) >= before, "la posición se conserva al cambiar de dispositivo");
            next();
        }
        break;
    case 9:
        if (el > 1.0) {
            check(engine_.deckStatus[0].playing.load(), "sigue sonando tras los cambios de dispositivo");
            send(0, Cmd::Pause);
            // Soltar un archivo (misma ruta que WM_DROPFILES) en el centro del plato B.
            const ImVec2 c((deckRect_[1][0].x + deckRect_[1][1].x) * 0.5f, (deckRect_[1][0].y + deckRect_[1][1].y) * 0.5f);
            onDropFiles({t.a}, c);
            next();
        }
        break;
    case 10:
        if (deck_[1].track && deck_[1].path == t.a) {
            check(true, "soltar un archivo sobre el plato B lo carga ahí");
            // MIDI: se inyectan mensajes como los de un DDJ-WeGO (se restauran después).
            t.midiBackup = midi_.bindings();
            t.midiSettingsBackup = midi_.settings();
            midi_.loadPresetWeGO();
            midi_.settings().softTakeover = true;
            engine_.deck[0].fader = 0.8f;
            send(0, Cmd::Pause);
            midi_.pushEvent({0, 0xB0, 0x13, 0});  // fader A abajo: lejos de 0,8 -> no debe saltar
            next();
        } else if (el > 20) { check(false, "soltar sobre el plato B no cargó la pista"); t.step = 99; }
        break;
    case 11:
        if (el > 0.1) {
            check(std::fabs(engine_.deck[0].fader.load() - 0.8f) < 1e-4f, "MIDI soft takeover: el fader no salta al valor del mando");
            midi_.pushEvent({0, 0xB0, 0x13, 100});  // cerca de 0,8: lo recoge
            next();
        }
        break;
    case 12:
        if (el > 0.1) {
            check(std::fabs(engine_.deck[0].fader.load() - 100 / 127.f) < 1e-4f, "MIDI: fader recogido y movido por el mando");
            midi_.pushEvent({0, 0xB0, 0x33, 64});   // byte bajo (14 bits)
            midi_.pushEvent({0, 0x90, 0x0B, 127});  // PLAY A
            midi_.pushEvent({0, 0x90, 0x0B, 0});
            next();
        }
        break;
    case 13:
        if (el > 0.2) {
            check(std::fabs(engine_.deck[0].fader.load() - (100 * 128 + 64) / 16383.f) < 1e-4f, "MIDI: precisión de 14 bits");
            check(engine_.deckStatus[0].playing.load(), "MIDI: PLAY del plato A");
            midi_.pushEvent({0, 0x90, 0x36, 127});  // tocar el jog
            midi_.pushEvent({0, 0xB0, 0x22, 74});   // girar el plato +10 pasos
            next();
        }
        break;
    case 14:
        if (el > 0.1) {
            check(deck_[0].jogTouched && engine_.deck[0].scratching.load(), "MIDI: tocar el jog activa el scratch");
            midi_.pushEvent({0, 0x90, 0x36, 0});
            next();
        }
        break;
    case 15:
        if (el > 0.1) {
            check(!engine_.deck[0].scratching.load(), "MIDI: soltar el jog termina el scratch");
            send(0, Cmd::Pause);
            midi_.bindings() = t.midiBackup;
            midi_.settings() = t.midiSettingsBackup;
            midi_.save();
            // Key lock: el plato B (que tiene la pista A) suena a +6 % con el tono intacto.
            send(1, Cmd::Seek, 20.0 * deck_[1].track->sampleRate);
            engine_.deck[1].keyLock = true;
            deck_[1].sync = false;
            deck_[1].pitch = 0.06f;
            engine_.deck[1].pitch = 0.06f;
            engine_.mixer.crossfader = 1.f;
            send(1, Cmd::Play);
            t.posMark = 20.0;
            next();
        }
        break;
    case 16:
        if (el > 2.0) {
            const double adv = deckPosSec(1) - t.posMark;
            check(engine_.deckStatus[1].playing.load() && adv > 2.0 && adv < 2.3, "key lock: el plato avanza al +6 % (" + std::to_string(adv) + " s en 2 s)");
            engine_.deck[1].keyLock = false;
            engine_.deck[1].pitch = 0.f; deck_[1].pitch = 0.f;
            send(0, Cmd::Pause); send(1, Cmd::Pause);
            // Automix con las dos pistas de la prueba.
            const int am = ensurePlaylist("Automix");
            {
                std::lock_guard<std::recursive_mutex> lk(library_.mutex());
                library_.playlists()[am].paths = {t.a, t.b};
            }
            automixIndex_ = 0;
            setAutomix(true);
            next();
        }
        break;
    case 17:
        // Cuando la primera pista suena, adelantarla hasta 12 s antes del final.
        if (automixOn_ && deck_[0].track && engine_.deckStatus[0].playing.load() && deck_[0].path == t.a) {
            check(true, "Automix arranca la primera pista en el plato A");
            send(0, Cmd::Seek, (deckLenSec(0) - 12.0) * deck_[0].track->sampleRate);
            next();
        } else if (el > 20) { check(false, "Automix no arrancó"); t.step = 99; }
        break;
    case 18:
        if (automixPhase_ == 1 && t.amTransitionAt < 0) t.amTransitionAt = now_;
        // La orden de reproducir llega al hilo de audio en el siguiente bloque: comprobar un poco después.
        if (t.amTransitionAt > 0 && !t.amChecked && now_ - t.amTransitionAt > 0.3) {
            t.amChecked = true;
            check(engine_.deckStatus[1].playing.load() && deck_[1].path == t.b, "Automix: entra la siguiente pista en el plato B");
            if (deck_[0].meta.bpm > 0 && deck_[1].meta.bpm > 0)
                check(std::fabs(engine_.effectiveBpm(1) - engine_.effectiveBpm(0)) < 0.05, "Automix: tempo sincronizado durante la transición");
        }
        if (t.amChecked && automixPhase_ == 0 && t.amDoneAt < 0) t.amDoneAt = now_;
        // Las órdenes de pausa llegan al audio en el bloque siguiente: esperar antes de comprobar.
        if (t.amDoneAt > 0 && now_ - t.amDoneAt > 0.5) {
            check(!engine_.deckStatus[0].playing.load() && engine_.deckStatus[1].playing.load() && engine_.mixer.crossfader.load() > 0.99f,
                  "Automix: transición completa (A parado, B sonando, crossfader en B)");
            setAutomix(false);
            send(1, Cmd::Pause);
            // A queda casi al final: el evento debe hacerlo sonar desde su CUE.
            send(0, Cmd::Seek, std::max(0.0, deckLenSec(0) - 0.15) * deck_[0].track->sampleRate);
            // Programador: evento en este mismo minuto -> reproducir plato A.
            std::time_t tt = std::time(nullptr);
            std::tm lt{};
            localtime_s(&lt, &tt);
            t.schedBackup = schedule_;
            SchedEvent ev;
            ev.hour = lt.tm_hour; ev.minute = lt.tm_min; ev.action = 5;
            schedule_.push_back(ev);
            next();
        } else if (el > 40) { check(false, "Automix no completó la transición en 40 s"); t.step = 99; }
        break;
    case 19:
        if (engine_.deckStatus[0].playing.load() && deckLenSec(0) - deckPosSec(0) > 2.0) {
            check(true, "Programador: el evento de la hora actual reproduce el plato A (desde su CUE)");
            schedule_ = t.schedBackup;
            saveSchedule();
            send(0, Cmd::Pause);
            // Navegador (0.5): abrir la carpeta de la pista A como en VirtualDJ.
            {
                BrowseSrc bs;
                bs.kind = Src::Carpeta;
                bs.folder = t.a.substr(0, t.a.find_last_of(L"\\/"));
                selectSource(bs);
            }
            t.step = 30;
            t.stepT = now_;
        } else if (el > 5) { check(false, "el programador no ejecutó el evento"); schedule_ = t.schedBackup; t.step = 99; }
        break;
    case 30:
        if (!library_.browsing() && el > 0.3) {
            refreshView();
            bool found = false;
            for (auto& r : view_) if (r.path == t.a) found = true;
            check(found, "Navegador: la carpeta de la pista A muestra sus archivos (" + std::to_string(view_.size()) + ")");
            // Sidelist y favoritos (se deja todo como estaba)
            const int sl = ensurePlaylist("Sidelist");
            size_t before = 0, after = 0;
            { std::lock_guard<std::recursive_mutex> lk(library_.mutex()); before = library_.playlists()[sl].paths.size(); }
            addToSidelist({t.a, t.b});
            { std::lock_guard<std::recursive_mutex> lk(library_.mutex()); after = library_.playlists()[sl].paths.size(); }
            check(after == before + 2, "Navegador: añadir dos pistas a la Sidelist");
            {
                std::lock_guard<std::recursive_mutex> lk(library_.mutex());
                auto& v = library_.playlists()[sl].paths;
                if (v.size() >= 2) v.resize(v.size() - 2);
                library_.markDirty();
            }
            const bool wasFav = isFavorite(t.a);
            toggleFavorite(t.a);
            check(isFavorite(t.a) != wasFav, "Navegador: marcar y desmarcar favorito");
            toggleFavorite(t.a);
            // Sugerencias: con las dos pistas de la prueba en los platos no deben aparecer ellas mismas.
            selectSource(BrowseSrc{Src::Sugerencias});
            refreshView();
            bool self = false;
            for (auto& r : view_) if (r.path == deck_[0].path || r.path == deck_[1].path) self = true;
            check(!self, "Navegador: las sugerencias no repiten lo que hay en los platos (" + std::to_string(view_.size()) + " sugerencias)");
            selectSource(BrowseSrc{});
            t.step = 60;
            t.stepT = now_;
        } else if (el > 20) { check(false, "Navegador: no se pudo leer la carpeta de la pista A"); t.step = 60; }
        break;
    case 60: {
        // Mandos reconocidos (mapeos de Mixxx incluidos): se simulan los mensajes MIDI reales de
        // varios mandos y se comprueba qué hace Ramiro DJ y qué luces devuelve.
        if (!deck_[0].track || !deck_[1].track || !mandos_) { log("Mandos: faltan pistas en los platos; prueba omitida"); t.step = 20; break; }
        if (engine_.deckStatus[0].playing.load()) send(0, Cmd::Pause);
        if (engine_.deckStatus[1].playing.load()) send(1, Cmd::Pause);
        t.mx.clear();
        t.mxIdx = 0;
        t.mxAt = -1;
        t.mxFader = engine_.deck[0].fader.load();
        t.mxXf = engine_.mixer.crossfader.load();
        auto midi = [this](std::initializer_list<std::array<uint8_t, 3>> msgs) {
            for (auto& m : msgs) mandos_->simularMidi(autotest_.mxSim, m[0], m[1], m[2]);
        };
        auto led = [this](uint8_t st, uint8_t no, bool lit) {
            for (auto& m : mandos_->salidasSimuladas(autotest_.mxSim))
                if (m.size() == 3 && m[0] == st && m[1] == no && (m[2] != 0) == lit) return true;
            return false;
        };
        auto add = [&](const char* file, double wait, std::function<void()> act, std::function<bool()> ok, std::string desc) {
            t.mx.push_back({file, wait, std::move(act), std::move(ok), std::move(desc)});
        };
        DeckParams& pA = engine_.deck[0];
        DeckStatus& sA = engine_.deckStatus[0];
        DeckUi& dA = deck_[0];
        auto& mx = engine_.mixer;
        // --- Pioneer DDJ-200 (todo por script)
        const char* f200 = "Pioneer DDJ-200.midi.xml";
        add(f200, 0.5, [=, this] { mandos_->limpiarSalidas(autotest_.mxSim); midi({{0x90, 0x0B, 0x7F}, {0x90, 0x0B, 0x00}}); }, [&sA] { return sA.playing.load(); },
            "DDJ-200: PLAY arranca el plato A");
        add(f200, 0.05, nullptr, [=, this] { return led(0x90, 0x0B, true); }, "DDJ-200: se enciende la luz de PLAY");
        add(f200, 0.5, [=, this] { midi({{0x90, 0x0B, 0x7F}, {0x90, 0x0B, 0x00}}); }, [&sA] { return !sA.playing.load(); }, "DDJ-200: PLAY otra vez lo para");
        add(f200, 0.2, [=, this] { midi({{0xB0, 0x13, 0x00}, {0xB0, 0x33, 0x00}}); }, [&pA] { return pA.fader.load() < 0.05f; }, "DDJ-200: fader de volumen abajo");
        add(f200, 0.2, [=, this] { midi({{0xB0, 0x13, 0x7F}, {0xB0, 0x33, 0x7F}}); }, [&pA] { return pA.fader.load() > 0.95f; }, "DDJ-200: fader de volumen arriba (14 bits)");
        // Faders como en la realidad: se recorren (con soft takeover el primer salto se ignora, igual que en Mixxx).
        add(f200, 0.2, [=, this] { for (int v = 0x7F; v >= 0; v -= 8) midi({{0xB6, 0x1F, (uint8_t)v}, {0xB6, 0x3F, 0x00}}); midi({{0xB6, 0x1F, 0x00}, {0xB6, 0x3F, 0x00}}); }, [&mx] { return mx.crossfader.load() < 0.05f; },
            "DDJ-200: crossfader a la izquierda (recorriéndolo)");
        add(f200, 0.2, [=, this] { midi({{0xB6, 0x1F, 0x7F}, {0xB6, 0x3F, 0x7F}}); }, [&mx] { return mx.crossfader.load() > 0.95f; }, "DDJ-200: crossfader a la derecha");
        add(f200, 0.2, [this, &pA, midi] { autotest_.mxXf = pA.pfl.load() ? 1.f : 0.f; midi({{0x90, 0x54, 0x7F}, {0x90, 0x54, 0x00}}); },
            [this, &pA] { return pA.pfl.load() != (autotest_.mxXf > 0.5f); }, "DDJ-200: botón de auriculares (PFL) del plato A");
        add(f200, 0.3, [=, this] { midi({{0x90, 0x58, 0x7F}, {0x90, 0x58, 0x00}}); }, [&dA] { return dA.sync; }, "DDJ-200: SYNC del plato A");
        add(f200, 0.3, [=, this] { midi({{0x90, 0x58, 0x7F}, {0x90, 0x58, 0x00}}); }, [&dA] { return !dA.sync; }, "DDJ-200: SYNC otra vez lo quita");
        // --- Pioneer DDJ-400 (XML directo + scripts)
        const char* f400 = "Pioneer-DDJ-400.midi.xml";
        add(f400, 0.5, [=, this] { mandos_->limpiarSalidas(autotest_.mxSim); midi({{0x90, 0x0B, 0x7F}, {0x90, 0x0B, 0x00}}); }, [&sA] { return sA.playing.load(); },
            "DDJ-400: PLAY arranca el plato A");
        add(f400, 0.1, nullptr, [=, this] { return led(0x90, 0x0B, true); }, "DDJ-400: luz de PLAY encendida (salida del XML)");
        add(f400, 0.5, [=, this] { midi({{0x90, 0x0B, 0x7F}, {0x90, 0x0B, 0x00}}); }, [&sA] { return !sA.playing.load(); }, "DDJ-400: PLAY otra vez lo para");
        add(f400, 0.2, [=, this] { midi({{0xB0, 0x07, 0x00}, {0xB0, 0x27, 0x00}}); }, [&pA] { return pA.eqHigh.load() < 0.03f; }, "DDJ-400: EQ de agudos al mínimo (14 bits)");
        add(f400, 0.2, [=, this] { midi({{0xB0, 0x07, 0x40}, {0xB0, 0x27, 0x00}}); }, [&pA] { return std::fabs(pA.eqHigh.load() - 0.5f) < 0.02f; }, "DDJ-400: EQ de agudos al centro");
        add(f400, 0.5, [=, this] { send(0, Cmd::HotCueDelete, 0, 0); }, nullptr, "");
        add(f400, 0.4, [=, this] { midi({{0x97, 0x00, 0x7F}, {0x97, 0x00, 0x00}}); }, [&sA] { return sA.hotCues[0].load() >= 0; }, "DDJ-400: pad 1 pone el hot cue 1");
        add(f400, 0.4, [=, this] { midi({{0x98, 0x00, 0x7F}, {0x98, 0x00, 0x00}}); }, [&sA] { return sA.hotCues[0].load() < 0; }, "DDJ-400: SHIFT + pad 1 lo borra");
        add(f400, 0.2, [=, this] { midi({{0x90, 0x36, 0x7F}}); }, [&pA] { return pA.scratching.load(); }, "DDJ-400: tocar el jog activa el scratch");
        add(f400, 0.15, [=, this] { autotest_.posMark = engine_.deckStatus[0].posFrames.load(); for (int i = 0; i < 6; ++i) midi({{0xB0, 0x22, 0x44}}); },
            [this] { return engine_.deckStatus[0].posFrames.load() - autotest_.posMark > 0.002 * deck_[0].track->sampleRate; },
            "DDJ-400: girar el jog mueve la pista (scratch)");
        add(f400, 1.5, [=, this] { midi({{0x90, 0x36, 0x00}}); }, [&pA] { return !pA.scratching.load(); }, "DDJ-400: soltar el jog termina el scratch");
        // Empuje con el aro (sin tocar la parte de arriba), sonando: un giro suave de 1/4 de vuelta por segundo
        // (180 pasos/s en un jog de 720) debe acelerar poco (1–4 %), no disparar el pitch.
        add(f400, 0.5, [=, this] { midi({{0x90, 0x0B, 0x7F}, {0x90, 0x0B, 0x00}}); }, [&sA] { return sA.playing.load(); }, "DDJ-400: PLAY (para probar el empuje)");
        add(f400, 0.8, [=, this] { autotest_.jogSimPeak = 0; autotest_.jogSimAcc = 0; autotest_.jogSimRate = 180; autotest_.jogSimCc = 0x21; autotest_.jogSimUntil = now_ + 0.6; },
            [this] {
                char b[96];
                std::snprintf(b, sizeof b, "  [info] empuje máximo con el aro: %+.2f %%", autotest_.jogSimPeak * 100.f);
                autotest_.lines.push_back(b);
                return autotest_.jogSimPeak > 0.01f && autotest_.jogSimPeak < 0.04f;
            },
            "DDJ-400: empuje suave con el aro del jog (entre el 1 y el 4 %)");
        add(f400, 0.6, nullptr, [&pA, &dA] { return std::fabs(pA.bend.load()) < 1e-4f && std::fabs(dA.pitch) < 1e-6f; }, "DDJ-400: al soltar el aro el empuje vuelve a 0 y el pitch no cambia");
        add(f400, 0.5, [=, this] { midi({{0x90, 0x0B, 0x7F}, {0x90, 0x0B, 0x00}}); }, [&sA] { return !sA.playing.load(); }, "DDJ-400: PLAY otra vez lo para (tras el empuje)");
        add(f400, 0.2, [=, this] { midi({{0xB0, 0x13, 0x00}, {0xB0, 0x33, 0x00}}); }, [&pA] { return pA.fader.load() < 0.05f; }, "DDJ-400: fader de volumen abajo");
        add(f400, 0.2, [=, this] { midi({{0xB0, 0x13, 0x7F}, {0xB0, 0x33, 0x7F}}); }, [&pA] { return pA.fader.load() > 0.95f; }, "DDJ-400: fader de volumen arriba");
        // --- Pioneer DDJ-FLX4
        const char* flx = "Pioneer-DDJ-FLX4.midi.xml";
        add(flx, 0.5, [=, this] { midi({{0x90, 0x0B, 0x7F}, {0x90, 0x0B, 0x00}}); }, [&sA] { return sA.playing.load(); }, "DDJ-FLX4: PLAY arranca el plato A");
        add(flx, 0.5, [=, this] { midi({{0x90, 0x0B, 0x7F}, {0x90, 0x0B, 0x00}}); }, [&sA] { return !sA.playing.load(); }, "DDJ-FLX4: PLAY otra vez lo para");
        add(flx, 0.2, [=, this] { midi({{0xB0, 0x07, 0x00}, {0xB0, 0x27, 0x00}}); }, [&pA] { return pA.eqHigh.load() < 0.03f; }, "DDJ-FLX4: EQ de agudos al mínimo");
        add(flx, 0.2, [=, this] { midi({{0xB0, 0x07, 0x40}, {0xB0, 0x27, 0x00}}); }, [&pA] { return std::fabs(pA.eqHigh.load() - 0.5f) < 0.02f; }, "DDJ-FLX4: EQ de agudos al centro");
        // --- Hercules DJControl Inpulse 200
        const char* ci2 = "Hercules_DJControl_Inpulse_200.midi.xml";
        add(ci2, 0.5, [=, this] { mandos_->limpiarSalidas(autotest_.mxSim); midi({{0x91, 0x07, 0x7F}, {0x91, 0x07, 0x00}}); }, [&sA] { return sA.playing.load(); },
            "Inpulse 200: PLAY arranca el plato A");
        add(ci2, 0.1, nullptr, [=, this] { return led(0x91, 0x07, true); }, "Inpulse 200: luz de PLAY encendida");
        add(ci2, 0.5, [=, this] { midi({{0x91, 0x07, 0x7F}, {0x91, 0x07, 0x00}}); }, [&sA] { return !sA.playing.load(); }, "Inpulse 200: PLAY otra vez lo para");
        add(ci2, 0.2, [=, this] { midi({{0xB1, 0x00, 0x00}}); }, [&pA] { return pA.fader.load() < 0.05f; }, "Inpulse 200: fader de volumen abajo");
        add(ci2, 0.2, [=, this] { midi({{0xB1, 0x00, 0x7F}}); }, [&pA] { return pA.fader.load() > 0.95f; }, "Inpulse 200: fader de volumen arriba");
        add(ci2, 0.2, [=, this] { midi({{0xB0, 0x00, 0x40}}); }, [&mx] { return std::fabs(mx.crossfader.load() - 0.5f) < 0.02f; }, "Inpulse 200: crossfader al centro");
        add(ci2, 0.2, [=, this] { midi({{0xB1, 0x04, 0x00}}); }, [&pA] { return pA.eqHigh.load() < 0.03f; }, "Inpulse 200: EQ de agudos al mínimo");
        add(ci2, 0.2, [=, this] { midi({{0xB1, 0x04, 0x40}}); }, [&pA] { return std::fabs(pA.eqHigh.load() - 0.5f) < 0.02f; }, "Inpulse 200: EQ de agudos al centro");
        add(ci2, 0.4, [=, this] { send(0, Cmd::HotCueDelete, 0, 0); midi({{0x96, 0x00, 0x7F}, {0x96, 0x00, 0x00}}); }, [&sA] { return sA.hotCues[0].load() >= 0; },
            "Inpulse 200: pad 1 pone el hot cue 1");
        add(ci2, 0.4, [=, this] { midi({{0x96, 0x08, 0x7F}, {0x96, 0x08, 0x00}}); }, [&sA] { return sA.hotCues[0].load() < 0; }, "Inpulse 200: SHIFT + pad 1 lo borra");
        // --- Numark Mixtrack Platinum FX (componentes de Mixxx)
        const char* mpf = "Numark Mixtrack Platinum FX.midi.xml";
        add(mpf, 0.5, [=, this] { midi({{0x90, 0x00, 0x7F}, {0x80, 0x00, 0x00}}); }, [&sA] { return sA.playing.load(); }, "Mixtrack Platinum FX: PLAY arranca el plato A");
        add(mpf, 0.5, [=, this] { midi({{0x90, 0x00, 0x7F}, {0x80, 0x00, 0x00}}); }, [&sA] { return !sA.playing.load(); }, "Mixtrack Platinum FX: PLAY otra vez lo para");
        add(mpf, 0.2, [=, this] { midi({{0xB0, 0x1C, 0x00}}); }, [&pA] { return pA.fader.load() < 0.05f; }, "Mixtrack Platinum FX: fader de volumen abajo");
        add(mpf, 0.2, [=, this] { for (int v = 0; v <= 0x7F; v += 8) midi({{0xB0, 0x1C, (uint8_t)v}}); midi({{0xB0, 0x1C, 0x7F}}); }, [&pA] { return pA.fader.load() > 0.95f; },
            "Mixtrack Platinum FX: fader de volumen arriba (recorriéndolo)");
        next();
        t.step = 61;
        break;
    }
    case 61: {
        if (t.mxIdx >= t.mx.size()) {
            mandos_->terminarSimulacion();
            t.mxSim = nullptr;
            engine_.deck[0].fader = t.mxFader;
            engine_.mixer.crossfader = 0.5f;
            engine_.deck[0].eqHigh = 0.5f;
            if (engine_.deckStatus[0].playing.load()) send(0, Cmd::Pause);
            t.step = 70;
            t.stepT = now_;
            break;
        }
        auto& p = t.mx[t.mxIdx];
        if (t.mxAt < 0) {
            if (p.file != t.mxFile) {
                t.mxSim = mandos_->simular(p.file);
                t.mxFile = p.file;
                log("  [info] Mando simulado: " + p.file);
            }
            if (p.act) p.act();
            t.mxAt = now_;
        } else if (now_ - t.mxAt >= p.wait) {
            if (p.ok) {
                const bool ok = p.ok();
                check(ok, p.desc);
                if (!ok) {
                    char b[200];
                    std::snprintf(b, sizeof b, "  [info] valores: crossfader %.3f · volumen A %.3f · agudos A %.3f · sonando %d · scratch %d", engine_.mixer.crossfader.load(),
                                  engine_.deck[0].fader.load(), engine_.deck[0].eqHigh.load(), (int)engine_.deckStatus[0].playing.load(), (int)engine_.deck[0].scratching.load());
                    log(b);
                }
            }
            ++t.mxIdx;
            t.mxAt = -1;
        }
        break;
    }
    // ---------------------------------------------------------- rack de hardware virtual (0.9)
    case 70: {
        if (!deck_[0].track) { log("Rack: falta la pista A; prueba omitida"); t.step = 20; break; }
        t.rackBackup = rackToJson();
        engine_.mixer.crossfader = 0.f;
        engine_.deck[0].fader = 0.8f;
        send(0, Cmd::Seek, 40.0 * deck_[0].track->sampleRate);
        if (!engine_.deckStatus[0].playing.load()) togglePlay(0);
        int idx = -1;
        for (size_t i = 0; i < rackPresets_.size(); ++i) if (rackPresets_[i].name == "Subida techno") idx = (int)i;
        rackApplyPreset(idx);
        RackParams& rp = engine_.rack.params;
        check(idx >= 0 && rp.slot[0].type.load() == (int)StFx::Filtro && rp.slot[1].type.load() == (int)StFx::Delay && rp.slot[2].type.load() == (int)StFx::Reverb &&
                  rp.slot[3].type.load() == (int)StFx::Gate && rp.stSrc.load() == (int)RackSrc::Mezcla,
              "Rack: el preset «Subida techno» carga su cadena (filtro, delay, reverb, gate) sobre la mezcla");
        rp.macro = 1.f;
        next();
        break;
    }
    case 71:
        if (el > 0.6) {
            const RackStatus& st = engine_.rack.status;
            bool all = true;
            for (int k = 0; k < kStSlots; ++k) all = all && st.slotOnEff[k].load();
            check(all && std::fabs(st.effAmount[0].load() - 0.86f) < 0.01f, "Rack: la macro enciende las cuatro ranuras y abre el filtro (intensidad " + std::to_string(st.effAmount[0].load()) + ")");
            check(st.stPeak.load() > 0.001f && st.nonFinite.load() == 0, "Rack: la estación procesa audio real sin valores no finitos");
            // EFECTOS OFF desde un control MIDI asignado (nota 0x50, canal 1).
            MidiBinding b; b.action = MA_RackFxOff; b.type = 0x9; b.channel = 0; b.data1 = 0x50;
            midi_.assign(b);
            midi_.pushEvent(MidiEvent{0, 0x90, 0x50, 127});
            midi_.pushEvent(MidiEvent{0, 0x80, 0x50, 0});
            next();
        }
        break;
    case 72:
        if (el > 0.3) {
            RackParams& rp = engine_.rack.params;
            bool any = false;
            for (int k = 0; k < kStSlots; ++k) any = any || engine_.rack.status.slotOnEff[k].load();
            check(rp.fxCut.load() && !any, "Rack: EFECTOS OFF desde el mando MIDI apaga las ranuras");
            midi_.clearBinding(MA_RackFxOff, 0);
            rp.fxCut = false;
            rp.macro = 0.f;
            {
                // QUITAR FX (0.11.1): quita de un golpe la ruleta del canal, el efecto del plato, las ranuras y la macro.
                rp.macro = 0.8f; rp.slot[0].on = true;
                engine_.deck[0].fxType = (int)FxType::Sidechain; engine_.deck[0].fxOn = true;
                engine_.deck[1].filter = -0.5f;
                const size_t before = activeEffects().size();
                const int quitados = clearAllEffects();
                bool clean = rp.macro.load() == 0.f && !engine_.deck[0].fxOn.load() && engine_.deck[1].filter.load() == 0.f;
                for (int k = 0; k < kStSlots; ++k) clean = clean && !rp.slot[k].on.load();
                check(before >= 4 && quitados == (int)before && clean,
                      "QUITAR FX: " + std::to_string(before) + " efectos y filtros puestos, " + std::to_string(quitados) + " quitados de un clic");
                engine_.deck[0].fxType = 0;
            }
            {
                // Botón de efectos del plato (0.11.3): quita solo lo del plato A (su efecto, REV, su ruleta y el
                // rack si suena sobre A); el filtro del plato B y su efecto siguen puestos.
                const int src0 = rp.stSrc.load();
                const int t0 = rp.slot[0].type.load();
                if (t0 == 0) rp.slot[0].type = 1;
                rp.stSrc = 0; rp.slot[0].on = true;
                engine_.deck[0].fxType = (int)FxType::Chorus; engine_.deck[0].fxOn = true;
                engine_.deck[0].reverse = true; engine_.deck[0].filter = 0.4f;
                engine_.deck[1].fxType = (int)FxType::Eco; engine_.deck[1].fxOn = true; engine_.deck[1].filter = -0.5f;
                const size_t enA = activeEffects(0).size(), enB = activeEffects(1).size();
                const int quitados = clearEffectsOf(0);
                const bool aLimpio = activeEffects(0).empty() && !engine_.deck[0].reverse.load() && !rp.slot[0].on.load();
                const bool bIntacto = engine_.deck[1].fxOn.load() && engine_.deck[1].filter.load() == -0.5f && activeEffects(1).size() == 2;
                check(enA == 4 && enB == 2 && quitados == 4 && aLimpio && bIntacto,
                      "Botón FX del plato A: " + std::to_string(quitados) + " quitados (efecto, REV, ruleta y rack sobre A); el plato B sigue con sus " +
                          std::to_string(activeEffects(1).size()));
                clearEffectsOf(1);
                engine_.deck[0].fxType = 0; engine_.deck[1].fxType = 0;
                rp.stSrc = src0; rp.slot[0].type = t0;
            }
            const std::string j1 = rackToJson();
            rackFromJson(j1);
            check(rackToJson() == j1, "Rack: guardar y recuperar el estado (JSON) da lo mismo");
            // Loop station: 1 compás del plato A, cuantizado.
            rp.lpSrc = 0; rp.lpQuant = true; rp.lpAutoPlay = false; rp.lpFollow = true;
            rackLoop(Rack::LoopRec, 1);
            next();
        }
        break;
    case 73: {
        const RackStatus& st = engine_.rack.status;
        if (st.lpState.load() == (int)LoopState::Parado && !st.lpBusy.load()) {   // (unos ms tras grabar se funde la unión)
            const double bpm = engine_.effectiveBpm(0);
            const double expect = 4.0 * 60.0 / bpm * engine_.sampleRate();
            char b[160];
            std::snprintf(b, sizeof b, "Rack: loop de 1 compás del plato A en %.1f s: %lld muestras (esperadas %.0f a %.2f BPM)", el, (long long)engine_.rack.loopFrames(), expect, bpm);
            check(std::fabs((double)engine_.rack.loopFrames() - expect) < 2.0, b);
            rackLastExport_.clear();
            rackLoopExport();
            std::error_code ec;
            const auto sz = rackLastExport_.empty() ? 0 : std::filesystem::file_size(std::filesystem::path(rackLastExport_), ec);
            check(!rackLastExport_.empty() && !ec && sz >= (uintmax_t)engine_.rack.loopFrames() * 8, "Rack: exportar el loop a WAV (" + std::to_string(sz / 1024) + " KB)");
            if (rackLastExport_.empty()) log("  [info] mensaje: " + statusMsg_);
            t.rackExport = rackLastExport_;
            t.amDoneAt = -1;
            rackLoop(Rack::LoopPlay);
            next();
        } else if (el > 10) { check(false, std::string("Rack: el loop no termina de grabarse (") + loopStateName(st.lpState.load()) + ")"); t.step = 75; }
        break;
    }
    case 74:
        if (engine_.rack.status.lpState.load() == (int)LoopState::Reproduciendo && t.amDoneAt < 0) {
            loadToDeck(0, t.b, true);   // otra canción en el plato A: el loop sigue
            t.amDoneAt = now_;
        }
        if (t.amDoneAt >= 0 && deck_[0].path == t.b && now_ - t.amDoneAt > 1.0) {
            engine_.rack.status.lpPeak.store(0.f);
            next();
        } else if (el > 15) { check(false, "Rack: el loop no empieza a sonar en el compás"); t.step = 75; }
        break;
    case 75:
        if (el > 0.4) {
            check(engine_.rack.status.lpState.load() == (int)LoopState::Reproduciendo && engine_.rack.status.lpPeak.load() > 0.001f,
                  "Rack: el loop sigue sonando tras cambiar la canción del plato A");
            rackLoop(Rack::LoopClear);
            rackFromJson(t.rackBackup);
            engine_.mixer.crossfader = 0.5f;
            t.amDoneAt = -1;
            if (!t.rackExport.empty()) { std::error_code ec; std::filesystem::remove(std::filesystem::path(t.rackExport), ec); }
            // Instrumentos: caja de ritmos y bajo siguiendo al plato A.
            t.instrBackup = instrToJson();
            if (!engine_.deckStatus[0].playing.load()) send(0, Cmd::Play);
            engine_.rack.instr.params.sync = 1;
            for (int k = 0; k < kDrumTracks; ++k) t.instrHits[k] = engine_.rack.instr.status.drumHits[k].load();
            instrDemo();
            t.step = 76;
            t.stepT = now_;
        }
        break;
    case 76:
        if (el > 4.0) {
            const InstrStatus& is = engine_.rack.instr.status;
            const uint32_t kicks = is.drumHits[DV_Bombo].load() - t.instrHits[DV_Bombo];
            const double bpm = engine_.effectiveBpm(0);
            char b[200];
            std::snprintf(b, sizeof b, "Instrumentos: la caja de ritmos suena siguiendo al plato A (%u bombos en 4 s a %.1f BPM; reloj %.2f BPM, plato %d)", kicks, bpm,
                          is.bpm.load(), is.clockDeck.load());
            check(is.drumRunning.load() && kicks >= 3 && is.clockDeck.load() == 0 && std::fabs(is.bpm.load() - bpm) < 0.05, b);
            check(is.bassRunning.load() && is.peak.load() > 0.001f && std::isfinite(is.peak.load()), "Instrumentos: el bajo suena y el bus no tiene valores no finitos");
            const std::string j1 = instrToJson();
            instrFromJson(j1);
            check(instrToJson() == j1, "Instrumentos: guardar y recuperar patrones, sonidos y pads (JSON) da lo mismo");
            check(pads_[0].track && pads_[0].builtin == 0, "Instrumentos: el pad 1 tiene su sonido incluido");
            padCmd(0, 1);
            padCmd(0, 0);
            t.step = 77;
            t.stepT = now_;
        }
        break;
    case 77:
        if (el > 0.3) {
            check(engine_.rack.instr.status.padPlaying[0].load() || engine_.rack.instr.status.padPos[0].load() > 0.f, "Instrumentos: el pad 1 suena al pulsarlo");
            engine_.rack.instr.params.drumPlay = false;
            engine_.rack.instr.params.bassPlay = false;
            padCmd(0, 2);
            instrFromJson(t.instrBackup);
            // Grabación de movimientos: 1 compás de la MACRO en rampa, y después tiene que repetirse a tempo.
            motion_ = Motion{};
            motion_.bars = 1; motion_.deck = 0; motion_.state = Motion::Armado; motion_.lastBeat = -1e9;
            t.macroBefore = engine_.rack.params.macro.load();
            t.step = 78;
            t.stepT = now_;
        }
        break;
    case 78: {
        double bt;
        if (motionBeat(0, bt)) engine_.rack.params.macro = (float)(std::fmod(bt, 4.0) / 4.0);   // la «mano» mueve la macro
        if (motion_.state == Motion::Sonando) { t.step = 79; t.stepT = now_; }
        else if (el > 8) { check(false, "Movimiento: no termina de grabarse"); t.step = 80; t.stepT = now_; }
        break;
    }
    case 79:
        if (el > 1.2) {
            double bt = 0;
            const bool ok = motionBeat(0, bt);
            const float expect = (float)(std::fmod(bt, 4.0) / 4.0), got = engine_.rack.params.macro.load();
            float err = std::fabs(got - expect);
            err = std::min(err, 1.f - err);   // vuelta del compás
            char b[160];
            std::snprintf(b, sizeof b, "Movimiento: la macro grabada se repite a tempo (esperada %.2f, suena %.2f)", expect, got);
            check(ok && err < 0.08f, b);
            t.step = 80; t.stepT = now_;
        }
        break;
    case 80:
        motion_ = Motion{};
        engine_.rack.params.macro = t.macroBefore;
        if (engine_.deckStatus[0].playing.load()) send(0, Cmd::Pause);
        t.step = 20;
        t.stepT = now_;
        break;
    case 20: {
        // Separador: usa una canción ya separada (si no hay ninguna, se omite).
        const auto songs = stems_.songs();
        if (songs.empty()) { log("Separador: no hay canciones separadas en esta carpeta de datos; prueba omitida"); t.step = 99; break; }
        mode_ = 1;
        t.stemSong = songs[0];
        openStemSong(songs[0]);
        next();
        break;
    }
    case 21:
        if (stemCount_ > 0 && !stemLoadFuture_.valid() && engine_.stemStatus.loadedGen.load() == stemGen_) {
            check(stemCount_ == (int)t.stemSong.stems.size(), "Separador: abre las " + std::to_string(stemCount_) + " pistas de «" + t.stemSong.title + "»");
            engine_.stemStatus.posFrames = 0;
            Command sk{Cmd::StemSeek, 0}; sk.d = 30.0 * stemTracks_[0]->sampleRate; engine_.send(sk);
            engine_.send(Command{Cmd::StemPlay, 0});
            next();
        } else if (el > 30) { check(false, "Separador: no se abrieron las pistas"); t.step = 99; }
        break;
    case 22:
        if (el > 1.5) {
            float pk = 0;
            for (int k = 0; k < stemCount_; ++k) pk = std::max(pk, engine_.stemStatus.peak[k].load());
            check(engine_.stemStatus.playing.load() && engine_.stemStatus.posFrames.load() > 30.0 * stemTracks_[0]->sampleRate && pk > 0.001f,
                  "Separador: el reproductor de stems suena y avanza (pos " + formatTime(engine_.stemStatus.posFrames.load() / stemTracks_[0]->sampleRate) +
                      ", pico " + std::to_string(pk) + (engine_.stemStatus.playing.load() ? ", sonando)" : ", parado)"));
            engine_.send(Command{Cmd::StemPause, 0});
            t.stemExportPath = stemOpenFolder_ + L"\\instrumental.wav";
            exportStemMix(t.stemExportPath, true, 1);
            next();
        }
        break;
    case 23:
        if (!stemExport_.valid() && deck_[1].track && deck_[1].path == t.stemExportPath) {
            check(deck_[1].title.find("(Instrumental)") != std::string::npos, "Separador: «Instrumental → plato B» exporta y carga «" + deck_[1].title + "»");
            check(std::fabs(deckLenSec(1) - (double)stemTracks_[0]->frames / stemTracks_[0]->sampleRate) < 0.1, "Separador: el instrumental dura lo mismo que la canción");
            check(mode_ == 0, "Separador: vuelve al modo MEZCLA al cargar en un plato");
            mode_ = 1;
            loadStemToDeck(0, "voz");
            next();
        } else if (el > 60) { check(false, "Separador: la exportación del instrumental no terminó"); t.step = 99; }
        break;
    case 24:
        if (deck_[0].track && deck_[0].path == stemOpenFolder_ + L"\\voz.flac") {
            check(deck_[0].title.find("(Voz)") != std::string::npos && deck_[0].artist == "Separador", "Separador: la voz se carga en el plato A como «" + deck_[0].title + "»");
            ejectDeck(0); ejectDeck(1);
            std::error_code ec;
            std::filesystem::remove(std::filesystem::path(t.stemExportPath), ec);
            t.step = 99;
        } else if (el > 20) { check(false, "Separador: no se cargó la voz en el plato A"); t.step = 99; }
        break;
    default: {
        log(t.failures ? "RESULTADO: " + std::to_string(t.failures) + " fallo(s)" : "RESULTADO: todo correcto");
        std::ofstream f(std::filesystem::path(t.log), std::ios::binary | std::ios::trunc);
        f << "\xEF\xBB\xBF";
        for (auto& l : t.lines) f << l << "\r\n";
        t.done = true;
        break;
    }
    }
}

} // namespace rdj

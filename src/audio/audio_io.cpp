#include "audio_io.h"
#include "ma.h"
#include <atomic>
#include <cstring>

namespace rdj {

struct AudioIO::Impl {
    ma_context ctx{};
    bool ctxOk = false;
    ma_context nullCtx{};
    bool nullOk = false;
    ma_device_info* playInfos = nullptr;
    ma_uint32 playCount = 0;
    ma_device dev{}, hpDev{};
    bool devOk = false, hpOk = false;
    Engine* eng = nullptr;
    std::atomic<bool> closing{false};
    std::atomic<bool> lost{false};
    int hpPeriod = 480;
    std::vector<float> hpTmp;
};

namespace {

void dataCallback(ma_device* dev, void* out, const void*, ma_uint32 frames) {
    auto* d = (AudioIO::Impl*)dev->pUserData;
    d->eng->process((float*)out, (int)frames, (int)dev->playback.channels);
}

void hpCallback(ma_device* dev, void* out, const void*, ma_uint32 frames) {
    auto* d = (AudioIO::Impl*)dev->pUserData;
    float* o = (float*)out;
    const int ch = (int)dev->playback.channels;
    SpscFloatRing& ring = d->eng->hpRing;
    const size_t avail = ring.available() / 2;
    const size_t target = (size_t)d->hpPeriod * 2;
    if (avail > target + (size_t)d->hpPeriod * 4) ring.skip((avail - target) * 2);
    float tmp[512];
    ma_uint32 done = 0;
    while (done < frames) {
        const ma_uint32 n = std::min<ma_uint32>(frames - done, 256);
        const size_t got = ring.read(tmp, (size_t)n * 2) / 2;
        for (ma_uint32 i = 0; i < n; ++i) {
            float* f = o + (size_t)(done + i) * ch;
            const float l = i < got ? tmp[i * 2] : 0.f, r = i < got ? tmp[i * 2 + 1] : 0.f;
            if (ch >= 4) {
                f[0] = l; f[1] = r; f[2] = l; f[3] = r;
                for (int c = 4; c < ch; ++c) f[c] = 0.f;
            } else if (ch >= 2) {
                f[0] = l; f[1] = r;
                for (int c = 2; c < ch; ++c) f[c] = 0.f;
            } else {
                f[0] = 0.5f * (l + r);
            }
        }
        done += n;
    }
}

void notifyCallback(const ma_device_notification* n) {
    auto* d = (AudioIO::Impl*)n->pDevice->pUserData;
    if (n->type == ma_device_notification_type_stopped && !d->closing.load()) d->lost.store(true);
}

std::string backendName(ma_backend b) {
    switch (b) {
    case ma_backend_wasapi: return "WASAPI";
    case ma_backend_dsound: return "DirectSound";
    case ma_backend_winmm: return "WinMM";
    case ma_backend_null: return "Simulado";
    default: return "Otro";
    }
}

} // namespace

AudioIO::AudioIO() : d_(new Impl) {}
AudioIO::~AudioIO() {
    close();
    if (d_->ctxOk) ma_context_uninit(&d_->ctx);
    if (d_->nullOk) ma_context_uninit(&d_->nullCtx);
}

bool AudioIO::init(std::string& err, bool forceNull) {
    ma_backend nullOnly[] = {ma_backend_null};
    if (ma_context_init(nullOnly, 1, nullptr, &d_->nullCtx) == MA_SUCCESS) d_->nullOk = true;
    if (!forceNull) {
        ma_backend backends[] = {ma_backend_wasapi, ma_backend_dsound, ma_backend_winmm};
        ma_context_config cc = ma_context_config_init();
        if (ma_context_init(backends, 3, &cc, &d_->ctx) == MA_SUCCESS) d_->ctxOk = true;
        else err = "No se pudo iniciar ningún sistema de audio (WASAPI/DirectSound/WinMM).";
    }
    refreshDevices();
    return d_->ctxOk || d_->nullOk;
}

void AudioIO::refreshDevices() {
    devices_.clear();
    if (!d_->ctxOk) return;
    ma_device_info* cap; ma_uint32 capCount;
    if (ma_context_get_devices(&d_->ctx, &d_->playInfos, &d_->playCount, &cap, &capCount) != MA_SUCCESS) {
        d_->playInfos = nullptr; d_->playCount = 0;
        return;
    }
    for (ma_uint32 i = 0; i < d_->playCount; ++i) {
        OutputDevice od;
        od.name = d_->playInfos[i].name;
        od.isDefault = d_->playInfos[i].isDefault;
        od.index = (int)i;
        ma_device_info full;
        if (ma_context_get_device_info(&d_->ctx, ma_device_type_playback, &d_->playInfos[i].id, &full) == MA_SUCCESS) {
            int mx = 0;
            for (ma_uint32 k = 0; k < full.nativeDataFormatCount; ++k) mx = std::max(mx, (int)full.nativeDataFormats[k].channels);
            od.maxChannels = mx;
        }
        devices_.push_back(od);
    }
}

bool AudioIO::deviceLost() const { return d_->lost.load(); }
void AudioIO::clearLost() { d_->lost.store(false); }

void AudioIO::close() {
    d_->closing = true;
    if (d_->devOk) { ma_device_uninit(&d_->dev); d_->devOk = false; }
    if (d_->hpOk) { ma_device_uninit(&d_->hpDev); d_->hpOk = false; }
    d_->closing = false;
    state_.running = false;
}

bool AudioIO::openOn(Engine& eng, const AudioConfig& cfg, bool useNull, std::string& err) {
    ma_context* ctx = useNull ? &d_->nullCtx : &d_->ctx;
    if (useNull ? !d_->nullOk : !d_->ctxOk) { err = "Sistema de audio no disponible."; return false; }

    const ma_device_id* id = nullptr;
    int maxCh = 2;
    std::string name = "Predeterminado del sistema";
    std::string resolvedSpkDev;
    if (!useNull && !cfg.deviceName.empty()) {
        bool found = false;
        for (auto& od : devices_) if (od.name == cfg.deviceName) {
            id = &d_->playInfos[od.index].id; maxCh = std::max(2, od.maxChannels); name = od.name; resolvedSpkDev = od.name; found = true;
        }
        if (!found) { err = "El dispositivo \"" + cfg.deviceName + "\" no está conectado."; return false; }
    } else if (!useNull) {
        for (auto& od : devices_) if (od.isDefault) { maxCh = std::max(2, od.maxChannels); name = od.name; resolvedSpkDev = od.name; }
    }

    std::string resolvedHpDev = cfg.hpDeviceName.empty() ? resolvedSpkDev : cfg.hpDeviceName;
    HpRouting routing = cfg.hpRouting;
    if (cfg.outMode == 0) routing = HpRouting::Ninguna;
    else if (cfg.outMode == 2) routing = HpRouting::DecksSeparados;
    else if (cfg.hwMode == 2 || routing == HpRouting::DivisionMono) routing = HpRouting::DivisionMono;
    else {
        // outMode == 1 (ALTAVOCES + AUDIFONOS)
        if (!useNull && !resolvedHpDev.empty() && !resolvedSpkDev.empty() && resolvedHpDev != resolvedSpkDev) {
            routing = HpRouting::DispositivoAparte;
        } else {
            routing = HpRouting::Canales34;
        }
    }

    std::string msg;
    int needCh = 2;
    if (routing == HpRouting::Canales34 || (routing == HpRouting::DecksSeparados && resolvedHpDev == resolvedSpkDev)) {
        const int maxPair = std::max(cfg.spkChan < 4 ? cfg.spkChan : 0, cfg.hpChan < 4 ? cfg.hpChan : 0);
        needCh = std::max(4, (maxPair + 1) * 2);
    }

    d_->eng = &eng;
    ma_device_config dc = ma_device_config_init(ma_device_type_playback);
    dc.playback.pDeviceID = id;
    dc.playback.format = ma_format_f32;
    dc.playback.channels = (ma_uint32)needCh;
    dc.playback.channelMixMode = ma_channel_mix_mode_simple;
    dc.sampleRate = (ma_uint32)std::max(0, cfg.sampleRate);
    dc.periodSizeInMilliseconds = (ma_uint32)std::clamp(cfg.periodMs, 2, 100);
    dc.periods = 2;
    dc.performanceProfile = ma_performance_profile_low_latency;
    dc.noPreSilencedOutputBuffer = MA_TRUE;
    dc.noClip = MA_TRUE;
    dc.dataCallback = dataCallback;
    dc.notificationCallback = notifyCallback;
    dc.pUserData = d_.get();
    dc.wasapi.usage = ma_wasapi_usage_pro_audio;
    dc.playback.shareMode = cfg.exclusive && !useNull ? ma_share_mode_exclusive : ma_share_mode_shared;

    ma_result r = ma_device_init(ctx, &dc, &d_->dev);
    bool exclusive = cfg.exclusive && !useNull;
    if (r != MA_SUCCESS && exclusive) {
        dc.playback.shareMode = ma_share_mode_shared;
        exclusive = false;
        r = ma_device_init(ctx, &dc, &d_->dev);
        if (r == MA_SUCCESS) msg += (msg.empty() ? "" : " ") + std::string("El modo exclusivo no está disponible; se usa el modo compartido.");
    }
    if (r != MA_SUCCESS && needCh > 2) {
        dc.playback.channels = 2;
        r = ma_device_init(ctx, &dc, &d_->dev);
    }
    if (r != MA_SUCCESS) { err = "No se pudo abrir \"" + name + "\" (" + ma_result_description(r) + ")."; return false; }
    d_->devOk = true;

    const int internalCh = d_->dev.playback.internalChannels > 0 ? (int)d_->dev.playback.internalChannels : maxCh;
    eng.prepare(d_->dev.sampleRate);
    eng.mixer.spkChanPair.store(std::clamp(cfg.spkChan, 0, 5));
    eng.mixer.hpChanPair.store(std::clamp(cfg.hpChan, 0, 5));
    eng.mixer.hwChannels.store(internalCh);
    eng.mixer.hpRouting.store((int)routing);

    std::string hpName = resolvedHpDev;
    if (routing == HpRouting::DispositivoAparte || (routing == HpRouting::DecksSeparados && resolvedHpDev != resolvedSpkDev)) {
        const ma_device_id* hid = nullptr;
        bool found = false;
        if (!useNull) for (auto& od : devices_) if (od.name == resolvedHpDev) { hid = &d_->playInfos[od.index].id; found = true; hpName = od.name; }
        if (!found) {
            routing = HpRouting::Canales34;
            eng.mixer.hpRouting.store((int)routing);
            hpName = name;
        } else {
            ma_device_config hc = dc;
            hc.playback.pDeviceID = hid;
            hc.playback.channels = 2;
            hc.playback.shareMode = ma_share_mode_shared;
            hc.sampleRate = d_->dev.sampleRate;
            hc.dataCallback = hpCallback;
            if (ma_device_init(ctx, &hc, &d_->hpDev) == MA_SUCCESS) {
                d_->hpOk = true;
                d_->hpPeriod = (int)d_->hpDev.playback.internalPeriodSizeInFrames;
                if (d_->hpPeriod <= 0) d_->hpPeriod = 480;
                eng.hpRing.skip(eng.hpRing.available());
            } else {
                routing = HpRouting::Canales34;
                eng.mixer.hpRouting.store((int)routing);
                hpName = name;
            }
        }
    }

    if (d_->hpOk && ma_device_start(&d_->hpDev) != MA_SUCCESS) {
        ma_device_uninit(&d_->hpDev); d_->hpOk = false;
        routing = HpRouting::Canales34; eng.mixer.hpRouting.store((int)routing);
    }
    if (ma_device_start(&d_->dev) != MA_SUCCESS) {
        close();
        err = "No se pudo iniciar la reproducción en \"" + name + "\".";
        return false;
    }

    state_ = AudioState{};
    state_.running = true;
    state_.simulated = useNull;
    state_.backend = backendName(ctx->backend);
    state_.deviceName = useNull ? "Sin salida (modo simulado)" : name;
    state_.hpDeviceName = d_->hpOk ? hpName : (routing != HpRouting::Ninguna ? name : "");
    state_.sampleRate = d_->dev.sampleRate;
    state_.channels = (int)d_->dev.playback.channels;
    state_.nativeChannels = internalCh;
    state_.periodFrames = (int)d_->dev.playback.internalPeriodSizeInFrames;
    state_.exclusive = exclusive;
    state_.hpRouting = routing;
    state_.spkChan = cfg.spkChan;
    state_.hpChan = cfg.hpChan;
    state_.message = msg;
    return true;
}

bool AudioIO::open(Engine& eng, const AudioConfig& cfg) {
    close();
    d_->lost = false;
    std::string err;
    if (d_->ctxOk && openOn(eng, cfg, false, err)) return true;
    std::string first = err;
    if (d_->ctxOk && !cfg.deviceName.empty()) {
        AudioConfig def = cfg;
        def.deviceName.clear();
        err.clear();
        if (openOn(eng, def, false, err)) {
            state_.message = first + " Se usa el dispositivo predeterminado." + (state_.message.empty() ? "" : " " + state_.message);
            return true;
        }
    }
    err.clear();
    AudioConfig nul = cfg;
    nul.hpRouting = HpRouting::Ninguna;
    if (openOn(eng, nul, true, err)) {
        state_.message = (first.empty() ? std::string("No hay salida de audio.") : first) + " Funcionando en modo simulado (sin sonido).";
        return true;
    }
    state_ = AudioState{};
    state_.message = first.empty() ? err : first;
    return false;
}

} // namespace rdj

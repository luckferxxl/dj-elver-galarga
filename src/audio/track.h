// Pista decodificada en memoria y su análisis. Una vez entregada al motor es
// inmutable (los puntos cue y la cuadrícula viven en el plato, no aquí).
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rdj {

constexpr int kNumHotCues = 8;
constexpr int kWaveFramesPerBin = 128;  // resolución de la forma de onda detallada

struct WaveBin {
    uint8_t peak;              // pico absoluto (estéreo), 255 = 0 dBFS
    uint8_t rms;               // energía (RMS) relativa al máximo de la pista
    uint8_t low, mid, high;    // envolvente de cada banda (graves < 180 Hz, medios, agudos > 3 kHz),
                               // en amplitud lineal relativa a la envolvente máxima de la pista
};

// Metadatos persistentes de una pista (biblioteca / caché de análisis).
struct TrackMeta {
    double bpm = 0;            // 0 = desconocido
    double gridAnchorSec = 0;  // instante de un primer tiempo de compás
    bool analyzed = false;
    double durationSec = 0;
    double cueSec = 0;
    std::array<double, kNumHotCues> hotCuesSec;  // <0 = vacío
    int key = -1;              // 0..23 (ver keyName); -1 = desconocida
    bool keyAnalyzed = false;
    // Origen de la rejilla: 0 = sin analizar, 1 = automática (hasta la 0.6),
    // 2 = automática precisa (0.6.1: regresión de fase), 3 = ajustada a mano,
    // 4 = importada de VirtualDJ (se comprueba el desfase al cargarla), 5 = de VirtualDJ ya comprobada,
    // 6 = automática 0.10 (fase a la muestra y «1» del compás con varias pistas), 7 = de VirtualDJ comprobada con la 0.10,
    // 8 = de VirtualDJ leída al cargar (0.10.2: su BPM y su primer pulso, solo con el desfase conocido de MP3).
    // Las 1, 2 y 5 se vuelven a medir al cargar la pista (la fase salía 5-20 ms tarde).
    int gridSrc = 0;
    // Origen de la tonalidad: 0 = sin analizar, 1 = detectada (sin mirar la etiqueta),
    // 2 = detectada (la etiqueta no la trae), 3 = de la etiqueta del archivo,
    // 4 = de VirtualDJ, 5 = puesta a mano.
    int keySrc = 0;
    uint8_t importedCues = 0;  // hot cues traídos de VirtualDJ aún sin ajustar al desfase (bit por cue)
    std::array<std::string, kNumHotCues> hotCueNames;  // nombre de cada hot cue ("Drop", "Intro"…); vacío = solo el número
    TrackMeta() { hotCuesSec.fill(-1.0); }
};

struct Track {
    std::wstring path;
    std::string title, artist;  // UTF-8
    uint32_t sampleRate = 44100;
    uint64_t frames = 0;
    std::vector<float> pcm;  // estéreo intercalado
    std::vector<WaveBin> wave;
    float maxPeak = 0;  // pico máximo absoluto de la pista (lineal)
    TrackMeta meta;     // estado inicial al cargar

    double lengthSec() const { return sampleRate ? (double)frames / sampleRate : 0; }
};

using TrackPtr = std::shared_ptr<Track>;

} // namespace rdj

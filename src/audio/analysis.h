// Análisis de pistas (se ejecuta en hilos de fondo, nunca en el de audio).
#pragma once
#include "track.h"
#include <atomic>
#include <string>

namespace rdj {

struct BeatInfo {
    double bpm = 0;          // 0 si no se detecta
    double anchorSec = 0;    // un primer tiempo de compás, en [0, 4 pulsos)
    float confidence = 0;    // relación pico / media del espectro de tempo
};

// Forma de onda por bandas: un WaveBin por kWaveFramesPerBin fotogramas.
void computeWaveform(const float* pcm, uint64_t frames, uint32_t sr, std::vector<WaveBin>& out, float& maxPeak);

// Tonalidad: 0..11 = Do mayor..Si mayor, 12..23 = La menor.. (raíz 0..11 + 12); -1 = no detectada.
struct KeyInfo {
    int key = -1;
    float strength = 0;  // correlación con el perfil ganador (0..1)
};
KeyInfo detectKey(const float* pcm, uint64_t frames, uint32_t sr, const std::atomic<bool>* cancel = nullptr);
// Nombre musical ("Am", "F#") y código Camelot ("8A").
const char* keyName(int key);
const char* keyCamelot(int key);
const char* keyNameEs(int key);   // "La menor", "Reb Mayor"…
// Compatibles para mezcla armónica: misma, ±1 en la rueda Camelot o relativa.
bool keysCompatible(int a, int b);
// "Cerca": compatible si una de las dos se transporta un semitono (la marca
// tenue de VirtualDJ). Excluye las ya compatibles.
bool keysClose(int a, int b);
// Número Camelot (1..12) y si es mayor (B). -1 si la tonalidad es desconocida.
int keyCamelotNumber(int key);
bool keyIsMajor(int key);
// Tonalidad a partir del número Camelot y la letra (A = menor, B = mayor).
int keyFromCamelot(int number, bool major);
// Notación Open Key (Traktor): "1m", "1d"…
std::string keyOpenKey(int key);
// Interpreta una tonalidad escrita: "Am", "A#m", "Bbm", "A minor", "Amin", "F# maj",
// "8A", "08A", "1m"/"1d" (Open Key), "Do menor", con ♯ ♭… -1 si no se entiende.
int parseKey(const std::string& text);

// Detección de tempo y fase. minBpm/maxBpm acotan la búsqueda.
BeatInfo detectBeats(const float* pcm, uint64_t frames, uint32_t sr, double minBpm = 70.0, double maxBpm = 180.0,
                     const std::atomic<bool>* cancel = nullptr);

// Rejilla con un BPM ya conocido (por ejemplo importado de VirtualDJ): solo
// calcula la fase de los pulsos y el primer tiempo de compás. anchorSec en [0, 4 pulsos).
BeatInfo detectGridForBpm(const float* pcm, uint64_t frames, uint32_t sr, double bpm, const std::atomic<bool>* cancel = nullptr);

} // namespace rdj

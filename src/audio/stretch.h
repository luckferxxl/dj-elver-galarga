// Cambio de tono sin cambiar la duración (key lock y transposición), basado en
// Signalsmith Stretch (MIT). Toda la memoria se reserva en prepare(); prime() y
// process() son aptos para el hilo de audio.
#pragma once
#include <memory>

namespace rdj {

class PitchStretcher {
public:
    PitchStretcher();
    ~PitchStretcher();
    void prepare(double fs, int maxBlock);
    // Fotogramas de entrada necesarios para cebar (= retardo del lector adelantado).
    int primeLength() const { return primeLen_; }
    // Ceba con primeLength() fotogramas: la siguiente salida de process()
    // corresponde exactamente al primer fotograma de esta entrada.
    void prime(const float* l, const float* r, int len);
    // n fotogramas de entrada -> n de salida, con el tono multiplicado por transpose.
    void process(const float* inL, const float* inR, float* outL, float* outR, int n, float transpose);

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
    int primeLen_ = 0;
};

} // namespace rdj

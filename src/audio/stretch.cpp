#include "stretch.h"
#include "signalsmith-stretch.h"

namespace rdj {

struct PitchStretcher::Impl {
    signalsmith::stretch::SignalsmithStretch<float> st{12345};  // semilla fija: resultado reproducible
    float lastTranspose = 1.f;
};

PitchStretcher::PitchStretcher() : d_(new Impl) {}
PitchStretcher::~PitchStretcher() = default;

void PitchStretcher::prepare(double fs, int /*maxBlock*/) {
    // Preajuste "cheaper": ~100 ms de bloque, calidad suficiente para música y
    // menos latencia/CPU que el predeterminado. Sin repartir el cálculo entre
    // llamadas para que la carga sea determinista.
    d_->st.presetCheaper(2, (float)fs, false);
    d_->st.reset();
    primeLen_ = d_->st.outputSeekLength(1.0f);
    d_->lastTranspose = 1.f;
    d_->st.setTransposeFactor(1.f);
}

void PitchStretcher::prime(const float* l, const float* r, int len) {
    const float* in[2] = {l, r};
    d_->st.outputSeek(in, len);
}

void PitchStretcher::process(const float* inL, const float* inR, float* outL, float* outR, int n, float transpose) {
    if (transpose != d_->lastTranspose) {
        d_->st.setTransposeFactor(transpose);
        d_->lastTranspose = transpose;
    }
    const float* in[2] = {inL, inR};
    float* out[2] = {outL, outR};
    d_->st.process(in, n, out, n);
}

} // namespace rdj

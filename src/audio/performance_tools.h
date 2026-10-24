#pragma once
#include <cmath>
#include <algorithm>
namespace rdj {
// Reloj portable de interfaz. No promete precisión de muestra: se evalúa cada frame.
class BarLaunch {
public:
    void arm(double beat) { target_ = (std::floor(beat / 4.) + 1) * 4.; last_ = beat; armed_ = true; }
    bool tick(double beat) {
        if (!armed_ || !std::isfinite(beat)) return false;
        if (beat < last_ - .25 || beat - last_ > 2.) { arm(beat); return false; }
        last_ = beat;
        if (beat < target_) return false;
        armed_ = false; return true;
    }
    void cancel() { armed_ = false; }
    bool armed() const { return armed_; }
private:
    bool armed_ = false;
    double target_ = 0, last_ = 0;
};
inline float transitionProgress(double beat, double start, int bars) {
    return float(std::clamp((beat-start) / (std::max(1,bars)*4.), 0., 1.));
}
} // namespace rdj

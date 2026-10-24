#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace rdj {
// 0 = granular, 1 = resonador cromático, 2 = saturación de medios/agudos.
// Memoria solo en prepare(); process() admite bloques de cualquier longitud.
class CreativeFx {
public:
    void prepare(double rate) {
        fs_ = rate;
        l_.assign(size_t(rate) + 8, 0.f); r_ = l_;
        reset();
    }
    void reset() {
        std::fill(l_.begin(), l_.end(), 0.f); std::fill(r_.begin(), r_.end(), 0.f);
        grains_ = {}; w_ = 0; hop_ = 0; next_ = 0; rng_ = 0x137A51u;
        lowL_ = lowR_ = highL_ = highR_ = 0;
    }
    void process(float* L, float* R, int n, int kind, float amount) {
        const double a = std::isfinite(amount) ? std::clamp(double(amount), 0., 1.) : .5;
        const double pi = 3.141592653589793;
        const int len = std::max(16, int(fs_ * (.03 + a * .06)));
        const double ratio = std::pow(2., std::round(a * 24 - 12) / 12.);
        const double delay = fs_ / (110. * std::pow(2., std::round(a * 24) / 12.));
        const float kl = float(1 - std::exp(-2 * pi * 120 / fs_));
        const float kh = float(1 - std::exp(-2 * pi * std::min(2500., fs_ * .4) / fs_));
        for (int i = 0; i < n; ++i) {
            const float il = L[i], ir = R[i];
            float ol = 0, orr = 0;
            if (kind == 0) {
                if (--hop_ <= 0) {
                    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
                    auto& g = grains_[next_++ % grains_.size()];
                    g = {double(w_) - fs_ * (.1 + .05 * (rng_ & 65535) / 65535.) - len * 2., 0, len, ratio};
                    hop_ = std::max(1, len / 4);
                }
                l_[w_ % l_.size()] = il; r_[w_ % r_.size()] = ir;
                for (auto& g : grains_) if (g.age < g.length) {
                    const float win = float(.25 * (1 - std::cos(2 * pi * g.age / g.length)));
                    ol += read(l_, g.pos) * win; orr += read(r_, g.pos) * win;
                    g.pos += g.ratio; ++g.age;
                }
            } else if (kind == 1) {
                const float dl = read(l_, double(w_) - delay), dr = read(r_, double(w_) - delay);
                // Feedback <1, soft saturation and no cross-channel bleed.
                l_[w_ % l_.size()] = std::tanh(il * .3f + dl * .94f);
                r_[w_ % r_.size()] = std::tanh(ir * .3f + dr * .94f);
                ol = dl; orr = dr;
            } else {
                lowL_ += kl * (il - lowL_); lowR_ += kl * (ir - lowR_);
                highL_ += kh * (il - highL_); highR_ += kh * (ir - highR_);
                const float drive = float(1 + a * 15), norm = 1 / std::sqrt(drive);
                ol = lowL_ + std::tanh((highL_ - lowL_) * drive) * norm + std::tanh((il - highL_) * drive) * norm;
                orr = lowR_ + std::tanh((highR_ - lowR_) * drive) * norm + std::tanh((ir - highR_) * drive) * norm;
            }
            ++w_; L[i] = ol; R[i] = orr;
        }
    }
private:
    float read(const std::vector<float>& v, double pos) const {
        if (pos < 0) return 0;
        const uint64_t at = uint64_t(pos);
        const float f = float(pos - at);
        return v[at % v.size()] * (1-f) + v[(at+1) % v.size()] * f;
    }
    struct Grain { double pos = 0; int age = 0, length = 0; double ratio = 1; };
    std::array<Grain,4> grains_{};
    std::vector<float> l_, r_;
    double fs_ = 48000;
    uint64_t w_ = 0;
    int hop_ = 0;
    unsigned next_ = 0;
    uint32_t rng_ = 1;
    float lowL_ = 0, lowR_ = 0, highL_ = 0, highR_ = 0;
};
} // namespace rdj

#include "analysis.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>

namespace rdj {

namespace {
constexpr double kPi2 = 6.28318530717958647692;

// FFT radix-2 iterativa in situ (n potencia de 2).
void fft(std::vector<std::complex<float>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -kPi2 / (double)len;
        const std::complex<float> wl((float)std::cos(ang), (float)std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.f, 0.f);
            for (size_t j = 0; j < len / 2; ++j) {
                const std::complex<float> u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// Transformada de Fourier de la curva de ataques a una frecuencia concreta.
std::complex<double> tempoBin(const std::vector<float>& o, double bpm, double fps) {
    const double w = kPi2 * (bpm / 60.0) / fps;
    std::complex<double> acc(0, 0);
    const std::complex<double> rot(std::cos(w), -std::sin(w));
    std::complex<double> ph(1, 0);
    for (size_t n = 0; n < o.size(); ++n) {
        acc += (double)o[n] * ph;
        ph *= rot;
        if ((n & 1023) == 1023) ph /= std::abs(ph);  // evita deriva numérica
    }
    return acc;
}

bool cancelled(const std::atomic<bool>* c) { return c && c->load(std::memory_order_relaxed); }
} // namespace

namespace {
// Biquad (RBJ, forma directa transpuesta II) para separar bandas.
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void set(bool highpass, double fc, double sr) {
        const double w = kPi2 * fc / sr, c = std::cos(w), al = std::sin(w) / (2 * 0.70710678);
        const double a0 = 1 + al;
        if (highpass) { b0 = (float)((1 + c) / 2 / a0); b1 = (float)(-(1 + c) / a0); b2 = b0; }
        else { b0 = (float)((1 - c) / 2 / a0); b1 = (float)((1 - c) / a0); b2 = b0; }
        a1 = (float)(-2 * c / a0); a2 = (float)((1 - al) / a0);
    }
    inline float run(float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};
} // namespace

void computeWaveform(const float* pcm, uint64_t frames, uint32_t sr, std::vector<WaveBin>& out, float& maxPeak) {
    // Tres bandas con filtros de 4.º orden (Linkwitz-Riley: dos Butterworth en
    // cascada): graves < 180 Hz (bombo y bajo), medios y agudos > 3 kHz
    // (charles, platos). De cada banda se guarda su envolvente (ataque
    // instantáneo y caída corta): así cada golpe de bombo se ve como un pico
    // separado del siguiente, aunque la pista esté muy comprimida.
    const uint64_t bins = (frames + kWaveFramesPerBin - 1) / kWaveFramesPerBin;
    out.assign((size_t)bins, WaveBin{0, 0, 0, 0, 0});
    std::vector<float> eLow((size_t)bins), eMid((size_t)bins), eHigh((size_t)bins), eRms((size_t)bins);
    Biquad lp1, lp2, hp1, hp2, mh1, mh2, ml1, ml2;
    lp1.set(false, 180, sr); lp2.set(false, 180, sr);
    hp1.set(true, 3000, sr); hp2.set(true, 3000, sr);
    mh1.set(true, 180, sr); mh2.set(true, 180, sr);
    ml1.set(false, 3000, sr); ml2.set(false, 3000, sr);
    const float relLow = (float)std::exp(-1.0 / (0.014 * sr)), relMid = (float)std::exp(-1.0 / (0.007 * sr)), relHigh = (float)std::exp(-1.0 / (0.004 * sr));
    float envL = 0, envM = 0, envH = 0;
    maxPeak = 0;
    float bandMax = 1e-9f, rmsMax = 1e-9f;
    for (uint64_t b = 0; b < bins; ++b) {
        const uint64_t s = b * kWaveFramesPerBin, e = std::min(frames, s + kWaveFramesPerBin);
        float pk = 0, ml = 0, mm = 0, mhh = 0;
        double sq = 0;
        for (uint64_t i = s; i < e; ++i) {
            const float L = pcm[i * 2], R = pcm[i * 2 + 1];
            pk = std::max(pk, std::max(std::fabs(L), std::fabs(R)));
            const float m = 0.5f * (L + R);
            sq += (double)m * m;
            const float low = lp2.run(lp1.run(m));
            const float high = hp2.run(hp1.run(m));
            const float mid = ml2.run(ml1.run(mh2.run(mh1.run(m))));
            envL = std::max(std::fabs(low), envL * relLow);
            envM = std::max(std::fabs(mid), envM * relMid);
            envH = std::max(std::fabs(high), envH * relHigh);
            ml = std::max(ml, envL); mm = std::max(mm, envM); mhh = std::max(mhh, envH);
        }
        const double cnt = (double)std::max<uint64_t>(1, e - s);
        eLow[b] = ml; eMid[b] = mm; eHigh[b] = mhh;
        eRms[b] = (float)std::sqrt(sq / cnt);
        bandMax = std::max({bandMax, ml, mm, mhh});
        rmsMax = std::max(rmsMax, eRms[b]);
        maxPeak = std::max(maxPeak, pk);
        out[b].peak = (uint8_t)std::lround(std::min(1.f, pk) * 255.f);
    }
    auto q = [](float v, float mx) { return (uint8_t)std::lround(std::clamp(v / mx, 0.f, 1.f) * 255.f); };
    for (uint64_t b = 0; b < bins; ++b) {
        out[b].low = q(eLow[b], bandMax); out[b].mid = q(eMid[b], bandMax); out[b].high = q(eHigh[b], bandMax);
        out[b].rms = q(eRms[b], rmsMax);
    }
}

// ----------------------------------------------------------------- tonalidad

namespace {
const char* kKeyNames[24] = {"C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B",
                             "Cm", "Dbm", "Dm", "Ebm", "Em", "Fm", "F#m", "Gm", "Abm", "Am", "Bbm", "Bm"};
// Nombres en español, según la tabla Camelot de referencia (1A = Lab menor ... 12B = Mi Mayor).
const char* kKeyNamesEs[24] = {"Do Mayor", "Reb Mayor", "Re Mayor", "Mib Mayor", "Mi Mayor", "Fa Mayor", "Fa# Mayor", "Sol Mayor",
                               "Lab Mayor", "La Mayor", "Sib Mayor", "Si Mayor",
                               "Do menor", "Reb menor", "Re menor", "Mib menor", "Mi menor", "Fa menor", "Fa# menor", "Sol menor",
                               "Lab menor", "La menor", "Sib menor", "Si menor"};
const char* kCamelot[24] = {"8B", "3B", "10B", "5B", "12B", "7B", "2B", "9B", "4B", "11B", "6B", "1B",
                            "5A", "12A", "7A", "2A", "9A", "4A", "11A", "6A", "1A", "8A", "3A", "10A"};
// Perfiles de tonalidad (peso de cada grado respecto a la tónica).
const double kProfiles[3][2][12] = {
    // Krumhansl-Kessler (1982)
    {{6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88},
     {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17}},
    // Temperley (Kostka-Payne, 2007)
    {{0.748, 0.060, 0.488, 0.082, 0.670, 0.460, 0.096, 0.715, 0.104, 0.366, 0.057, 0.400},
     {0.712, 0.084, 0.474, 0.618, 0.049, 0.460, 0.105, 0.747, 0.404, 0.067, 0.133, 0.330}},
    // EDMA (Faraldo et al., 2016: música electrónica de baile)
    {{0.1652, 0.0475, 0.0829, 0.0669, 0.0999, 0.0927, 0.0529, 0.1316, 0.0522, 0.0744, 0.0694, 0.0643},
     {0.1724, 0.0400, 0.0761, 0.1239, 0.0562, 0.0898, 0.0442, 0.1342, 0.0830, 0.0470, 0.0894, 0.0471}},
};
int g_keyProfile = 1;  // Temperley (mejor en la validación); ajustable para validar (RDJ_KEY_PROFILE)

double pearson(const double* a, const double* b) {
    double ma = 0, mb = 0;
    for (int i = 0; i < 12; ++i) { ma += a[i]; mb += b[i]; }
    ma /= 12; mb /= 12;
    double num = 0, da = 0, db = 0;
    for (int i = 0; i < 12; ++i) { num += (a[i] - ma) * (b[i] - mb); da += (a[i] - ma) * (a[i] - ma); db += (b[i] - mb) * (b[i] - mb); }
    return (da > 0 && db > 0) ? num / std::sqrt(da * db) : 0;
}
} // namespace

const char* keyName(int key) { return key >= 0 && key < 24 ? kKeyNames[key] : "—"; }
const char* keyCamelot(int key) { return key >= 0 && key < 24 ? kCamelot[key] : "—"; }
const char* keyNameEs(int key) { return key >= 0 && key < 24 ? kKeyNamesEs[key] : "—"; }

int keyCamelotNumber(int key) { return key >= 0 && key < 24 ? std::atoi(kCamelot[key]) : -1; }
bool keyIsMajor(int key) { return key >= 0 && key < 12; }
int keyFromCamelot(int number, bool major) {
    if (number < 1 || number > 12) return -1;
    for (int k = major ? 0 : 12; k < (major ? 12 : 24); ++k) if (std::atoi(kCamelot[k]) == number) return k;
    return -1;
}
std::string keyOpenKey(int key) {
    if (key < 0 || key >= 24) return "—";
    // Open Key: 1d = Do mayor (8B), 1m = La menor (8A); misma rueda, desplazada 7 posiciones.
    const int n = (keyCamelotNumber(key) + 4) % 12 + 1;
    return std::to_string(n) + (keyIsMajor(key) ? "d" : "m");
}

bool keysClose(int a, int b) {
    if (a < 0 || b < 0 || keysCompatible(a, b)) return false;
    auto shift = [](int k, int st) { return k < 12 ? ((k + st) % 12 + 12) % 12 : 12 + (((k - 12) + st) % 12 + 12) % 12; };
    return keysCompatible(a, shift(b, 1)) || keysCompatible(a, shift(b, -1));
}

int parseKey(const std::string& text) {
    std::string s;
    // Normalizar: sin espacios, minúsculas, ♯/♭ a #/b.
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = (unsigned char)text[i];
        if (c == 0xE2 && i + 2 < text.size() && (unsigned char)text[i + 1] == 0x99) {
            const unsigned char d = (unsigned char)text[i + 2];
            if (d == 0xAF) s.push_back('#'); else if (d == 0xAD) s.push_back('b');
            i += 2; continue;
        }
        if (c == ' ' || c == '\t' || c == '-' || c == '_') continue;
        s.push_back((char)std::tolower(c));
    }
    if (s.empty()) return -1;
    // Camelot ("8a", "08a") u Open Key ("1m", "1d").
    if (std::isdigit((unsigned char)s[0])) {
        size_t i = 0; int n = 0;
        while (i < s.size() && std::isdigit((unsigned char)s[i])) n = n * 10 + (s[i++] - '0');
        if (i + 1 != s.size() || n < 1 || n > 12) return -1;
        const char l = s[i];
        if (l == 'a' || l == 'b') return keyFromCamelot(n, l == 'b');
        if (l == 'm' || l == 'd') return keyFromCamelot((n + 6) % 12 + 1, l == 'd');  // Open Key -> Camelot
        return -1;
    }
    // Nombres en español: do, re, mi, fa, sol, la, si (+ #/b) + mayor/menor.
    static const struct { const char* n; int pc; } es[] = {{"sol", 7}, {"do", 0}, {"re", 2}, {"mi", 4}, {"fa", 5}, {"la", 9}, {"si", 11}};
    int pc = -1;
    size_t i = 0;
    for (auto& e : es) {
        const size_t L = std::strlen(e.n);
        if (s.compare(0, L, e.n) == 0 && (s.size() == L || s[L] == '#' || s[L] == 'b' || s.compare(L, 2, "ma") == 0 || s.compare(L, 2, "me") == 0 || s[L] == 'm' || s.compare(L, 9, "sostenido") == 0)) { pc = e.pc; i = L; break; }
    }
    if (pc < 0) {
        static const int letters[7] = {9, 11, 0, 2, 4, 5, 7};  // a..g
        if (s[0] < 'a' || s[0] > 'g') return -1;
        pc = letters[s[0] - 'a'];
        i = 1;
    }
    if (s.compare(i, 5, "bemol") == 0) { pc -= 1; i += 5; }
    else if (s.compare(i, 9, "sostenido") == 0) { pc += 1; i += 9; }
    else if (i < s.size() && (s[i] == '#' || s[i] == 'b')) { pc += s[i] == '#' ? 1 : -1; ++i; }
    pc = (pc % 12 + 12) % 12;
    const std::string rest = s.substr(i);
    bool minor = false;
    if (rest.empty() || rest == "maj" || rest == "major" || rest == "mayor" || rest == "ma" || rest == "dur") minor = false;
    else if (rest == "m" || rest == "min" || rest == "minor" || rest == "menor" || rest == "moll" || rest == "mi") minor = true;
    else return -1;
    return minor ? 12 + pc : pc;
}

bool keysCompatible(int a, int b) {
    if (a < 0 || b < 0) return false;
    const int na = std::atoi(kCamelot[a]), nb = std::atoi(kCamelot[b]);
    const bool sameMode = (a < 12) == (b < 12);
    if (na == nb) return true;  // misma o relativa (8A/8B)
    if (!sameMode) return false;
    const int d = std::abs(na - nb);
    return d == 1 || d == 11;
}

// Cromagrama armónico (tipo HPCP, Gómez 2006): 36 subdivisiones por octava,
// picos espectrales interpolados, cada pico aporta también a sus subarmónicos
// con peso decreciente y ventana coseno² de 4/3 de semitono; normalización por
// trama; afinación global por el desplazamiento de subdivisión dominante.
static KeyInfo detectKeyHpcp(const float* pcm, uint64_t frames, uint32_t sr, const std::atomic<bool>* cancel) {
    KeyInfo info;
    if (!pcm || frames < sr * 4ull) return info;
    if (const char* e = std::getenv("RDJ_KEY_PROFILE")) g_keyProfile = std::clamp(std::atoi(e), 0, 2);
    int harmonics = 4;
    if (const char* e = std::getenv("RDJ_KEY_HARM")) harmonics = std::clamp(std::atoi(e), 1, 8);
    double fLo = 100, fHi = 3500;
    if (const char* e = std::getenv("RDJ_KEY_FLO")) fLo = std::atof(e);
    const int D = std::max(1, (int)std::lround(sr / 11025.0));
    const double dsr = (double)sr / D;
    constexpr int N = 8192, hop = 2048;
    const uint64_t maxFrames = std::min<uint64_t>(frames, (uint64_t)sr * 60 * 8);
    const size_t len = (size_t)(maxFrames / D);
    if (len < (size_t)N * 2) return info;
    std::vector<float> x(len);
    for (size_t i = 0; i < len; ++i) {
        float acc = 0;
        for (int k = 0; k < D; ++k) { const uint64_t f = (uint64_t)i * D + k; acc += pcm[f * 2] + pcm[f * 2 + 1]; }
        x[i] = acc / (2.f * D);
    }
    std::vector<float> win(N);
    for (int i = 0; i < N; ++i) {  // Blackman-Harris: lóbulos laterales bajos para picos limpios
        const double t = kPi2 * i / (N - 1);
        win[i] = (float)(0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t));
    }
    std::vector<std::complex<float>> buf(N);
    std::vector<float> db(N / 2 + 1);
    double hpcp[36] = {0};
    struct Peak { double f, m; };
    std::vector<Peak> peaks;
    for (size_t start = 0; start + N <= len; start += hop) {
        if (cancelled(cancel)) return KeyInfo{};
        for (int i = 0; i < N; ++i) buf[i] = std::complex<float>(x[start + i] * win[i], 0.f);
        fft(buf);
        float maxDb = -200;
        for (int k = 1; k <= N / 2; ++k) { db[k] = 20.f * std::log10(std::abs(buf[k]) + 1e-9f); maxDb = std::max(maxDb, db[k]); }
        peaks.clear();
        const int k0 = std::max(2, (int)(fLo * N / dsr)), k1 = std::min(N / 2 - 1, (int)(fHi * N / dsr));
        for (int k = k0; k <= k1; ++k) {
            if (db[k] <= db[k - 1] || db[k] < db[k + 1] || db[k] < maxDb - 60) continue;
            const double a = db[k - 1], b2 = db[k], c = db[k + 1];
            const double den = a - 2 * b2 + c;
            const double delta = den != 0 ? 0.5 * (a - c) / den : 0;
            const double magDb = b2 - 0.25 * (a - c) * delta;
            peaks.push_back({(k + delta) * dsr / N, std::pow(10.0, magDb / 20.0)});
        }
        std::sort(peaks.begin(), peaks.end(), [](const Peak& p, const Peak& q) { return p.m > q.m; });
        if (peaks.size() > 60) peaks.resize(60);
        double frame[36] = {0};
        for (const Peak& pk : peaks) {
            for (int h = 1; h <= harmonics; ++h) {
                const double f0 = pk.f / h;
                if (f0 < 40) break;
                const double w = std::pow(0.6, h - 1);  // los armónicos altos pesan menos
                double bin = 36.0 * std::log2(f0 / 440.0);
                bin = std::fmod(bin, 36.0);
                if (bin < 0) bin += 36.0;
                for (int k = (int)std::floor(bin) - 2; k <= (int)std::floor(bin) + 3; ++k) {
                    const double d = std::fabs(bin - k);
                    if (d >= 2.0) continue;  // ventana de 4 subdivisiones = 4/3 de semitono
                    const double cw = std::cos(kPi2 * 0.5 * d / 4.0);
                    frame[((k % 36) + 36) % 36] += cw * cw * w * pk.m * pk.m;
                }
            }
        }
        double mx = 0;
        for (double v : frame) mx = std::max(mx, v);
        if (mx <= 0) continue;
        for (int i = 0; i < 36; ++i) hpcp[i] += frame[i] / mx;
    }
    // Afinación: subdivisión (de 3) con más energía; plegado a 12 clases.
    double sub[3] = {0};
    for (int i = 0; i < 36; ++i) sub[i % 3] += hpcp[i];
    int off = 0;
    if (sub[1] > sub[off]) off = 1;
    if (sub[2] > sub[off]) off = 2;
    const int shift = off == 2 ? -1 : off;  // -1, 0 o +1 subdivisión respecto a 440 Hz
    double chroma[12] = {0};
    for (int i = 0; i < 36; ++i) {
        const int rel = ((i - shift) % 36 + 36) % 36;
        // La subdivisión central de cada semitono pesa el doble. Bin 0 = La (clase 9 con Do = 0).
        const int semi = ((rel + 1) / 3) % 12;
        chroma[(semi + 9) % 12] += hpcp[i] * (rel % 3 == 0 ? 1.0 : 0.5);
    }
    double best = -2;
    int bestKey = -1;
    for (int mode = 0; mode < 2; ++mode) {
        for (int root = 0; root < 12; ++root) {
            double prof[12];
            for (int i = 0; i < 12; ++i) prof[(i + root) % 12] = kProfiles[g_keyProfile][mode][i];
            const double r = pearson(chroma, prof);
            if (r > best) { best = r; bestKey = mode * 12 + root; }
        }
    }
    info.key = bestKey;
    info.strength = (float)std::max(0.0, best);
    return info;
}

KeyInfo detectKey(const float* pcm, uint64_t frames, uint32_t sr, const std::atomic<bool>* cancel) {
    const char* algo = std::getenv("RDJ_KEY_ALGO");
    if (!algo || std::string(algo) != "simple") return detectKeyHpcp(pcm, frames, sr, cancel);
    KeyInfo info;
    if (!pcm || frames < sr * 4ull) return info;
    if (const char* e = std::getenv("RDJ_KEY_PROFILE")) g_keyProfile = std::clamp(std::atoi(e), 0, 2);
    const int D = std::max(1, (int)std::lround(sr / 11025.0));
    const double dsr = (double)sr / D;
    constexpr int N = 8192, hop = 4096;
    const uint64_t maxFrames = std::min<uint64_t>(frames, (uint64_t)sr * 60 * 8);
    const size_t len = (size_t)(maxFrames / D);
    if (len < (size_t)N * 2) return info;
    std::vector<float> x(len);
    for (size_t i = 0; i < len; ++i) {
        float acc = 0;
        for (int k = 0; k < D; ++k) { const uint64_t f = (uint64_t)i * D + k; acc += pcm[f * 2] + pcm[f * 2 + 1]; }
        x[i] = acc / (2.f * D);
    }
    // Variantes de ajuste (RDJ_KEY_MODE, bits): 1 = blanqueo espectral,
    // 2 = compresión logarítmica, 4 = empezar en 100 Hz (sin bombo), 8 = semitonos (no picos).
    int mode = 52;  // elegida por validación: sin bombo (<100 Hz) + energía + tramas ponderadas
    if (const char* e = std::getenv("RDJ_KEY_MODE")) mode = std::atoi(e);
    const double fmin = (mode & 4) ? 100.0 : 55.0;
    const double fmaxKey = (mode & 128) ? 3500.0 : 1800.0;
    // Semitono (nota MIDI) de cada contenedor de la FFT, o -1 fuera de rango.
    std::vector<int> noteOf(N / 2 + 1, -1);
    for (int k = 1; k <= N / 2; ++k) {
        const double f = k * dsr / N;
        if (f < fmin || f > fmaxKey) continue;
        noteOf[k] = (int)std::lround(69 + 12 * std::log2(f / 440.0));
    }
    std::vector<float> win(N);
    for (int i = 0; i < N; ++i) win[i] = 0.5f - 0.5f * (float)std::cos(kPi2 * i / (N - 1));
    std::vector<std::complex<float>> buf(N);
    std::vector<float> mag(N / 2 + 1), smooth(N / 2 + 1);
    double chroma[12] = {0};

    // Afinación (bit 64): desviación media, en cents, de los picos respecto a
    // La=440 Hz (media circular ponderada, picos refinados con interpolación parabólica).
    double tuneCents = 0;
    if (mode & 64) {
        double sx = 0, sy = 0;
        for (size_t start = 0; start + N <= len; start += hop * 4) {
            if (cancelled(cancel)) return KeyInfo{};
            for (int i = 0; i < N; ++i) buf[i] = std::complex<float>(x[start + i] * win[i], 0.f);
            fft(buf);
            for (int k = 1; k <= N / 2; ++k) mag[k] = std::abs(buf[k]);
            for (int k = 2; k < N / 2; ++k) {
                if (noteOf[k] < 0 || mag[k] <= mag[k - 1] || mag[k] < mag[k + 1]) continue;
                const double a = std::log(mag[k - 1] + 1e-9), b2 = std::log(mag[k] + 1e-9), c = std::log(mag[k + 1] + 1e-9);
                const double den = a - 2 * b2 + c;
                const double delta = den != 0 ? 0.5 * (a - c) / den : 0;
                const double f = (k + delta) * dsr / N;
                const double midi = 69 + 12 * std::log2(f / 440.0);
                const double dev = midi - std::round(midi);  // -0,5..0,5 semitonos
                const double w = (double)mag[k] * mag[k];
                sx += w * std::cos(kPi2 * dev);
                sy += w * std::sin(kPi2 * dev);
            }
        }
        tuneCents = std::atan2(sy, sx) / kPi2 * 100.0;
        for (int k = 1; k <= N / 2; ++k) {
            const double f = k * dsr / N;
            if (f < fmin || f > fmaxKey) { noteOf[k] = -1; continue; }
            noteOf[k] = (int)std::lround(69 + 12 * std::log2(f / 440.0) - tuneCents / 100.0);
        }
    }
    for (size_t start = 0; start + N <= len; start += hop) {
        if (cancelled(cancel)) return KeyInfo{};
        for (int i = 0; i < N; ++i) buf[i] = std::complex<float>(x[start + i] * win[i], 0.f);
        fft(buf);
        for (int k = 1; k <= N / 2; ++k) mag[k] = std::abs(buf[k]);
        if (mode & 1) {
            // Blanqueo: dividir por la media local (≈ 1/3 de octava) para quitar el timbre.
            for (int k = 1; k <= N / 2; ++k) {
                const int w = std::max(2, k / 12);
                double acc = 0; int cnt = 0;
                for (int j = std::max(1, k - w); j <= std::min(N / 2, k + w); ++j) { acc += mag[j]; ++cnt; }
                smooth[k] = (float)(acc / cnt);
            }
            for (int k = 1; k <= N / 2; ++k) mag[k] = smooth[k] > 1e-9f ? std::max(0.f, mag[k] / smooth[k] - 1.f) : 0.f;
        }
        if (mode & 2) for (int k = 1; k <= N / 2; ++k) mag[k] = std::log1p(mag[k]);
        double frameC[12] = {0}, tot = 0;
        for (int k = 2; k < N / 2; ++k) {
            if (noteOf[k] < 0) continue;
            // Por defecto solo picos espectrales (descarta el ruido de banda ancha de la percusión).
            if (!(mode & 8) && (mag[k] <= mag[k - 1] || mag[k] < mag[k + 1])) continue;
            const double v = (mode & 32) ? (double)mag[k] * mag[k] : mag[k];
            frameC[(noteOf[k] % 12 + 12) % 12] += v;
            tot += v;
        }
        if (tot <= 0) continue;
        // Por defecto cada trama pesa lo mismo; con el bit 16 pesan por su energía.
        for (int c = 0; c < 12; ++c) chroma[c] += (mode & 16) ? frameC[c] : frameC[c] / tot;
    }
    double best = -2;
    int bestKey = -1;
    for (int mode = 0; mode < 2; ++mode) {
        for (int root = 0; root < 12; ++root) {
            double prof[12];
            for (int i = 0; i < 12; ++i) prof[(i + root) % 12] = kProfiles[g_keyProfile][mode][i];
            const double r = pearson(chroma, prof);
            if (r > best) { best = r; bestKey = mode * 12 + root; }
        }
    }
    info.key = bestKey;
    info.strength = (float)std::max(0.0, best);
    return info;
}

namespace {
// Curvas de ataques (banda ancha y graves) que usan la detección de tempo y la de fase.
struct Onsets {
    std::vector<float> o, oLow, lowFlux;
    std::vector<float> hiFlux;                 // ataques de 1,5 a 5,5 kHz (caja, palmas, charles)
    std::vector<std::array<float, 5>> bandE;   // energía (log) por bandas: cambios de sonido entre compases
    double fps = 100, frameOffsetSec = 0;
    size_t nFrames = 0;
};
bool onsetCurves(const float* pcm, uint64_t frames, uint32_t sr, const std::atomic<bool>* cancel, Onsets& out) {
    if (!pcm || frames < sr * 4ull || sr < 8000) return false;

    // 1) Diezmado a ~11 kHz (media de D muestras; el aliasing es irrelevante para ataques).
    const int D = std::max(1, (int)std::lround(sr / 11025.0));
    const double dsr = (double)sr / D;
    const uint64_t maxFrames = std::min<uint64_t>(frames, (uint64_t)sr * 60 * 8);  // hasta 8 minutos
    std::vector<float> x((size_t)(maxFrames / D));
    for (size_t i = 0; i < x.size(); ++i) {
        float acc = 0;
        for (int k = 0; k < D; ++k) { const uint64_t f = (uint64_t)i * D + k; acc += pcm[f * 2] + pcm[f * 2 + 1]; }
        x[i] = acc / (2.f * D);
    }

    // 2) Flujo espectral con compresión logarítmica.
    constexpr int N = 256;
    const int hop = std::max(1, (int)std::lround(dsr / 100.0));
    const double fps = dsr / hop;
    const double frameOffsetSec = (N * 0.5) / dsr;  // instante representado por cada trama
    if (x.size() < (size_t)N * 4) return false;
    const size_t nFrames = (x.size() - N) / hop;
    std::vector<float> win(N), prev(N / 2 + 1, 0.f), cur(N / 2 + 1);
    for (int i = 0; i < N; ++i) win[i] = 0.5f - 0.5f * (float)std::cos(kPi2 * i / (N - 1));
    std::vector<float> flux(nFrames, 0.f), lowFlux(nFrames, 0.f), hiFlux(nFrames, 0.f);
    std::vector<std::array<float, 5>> bandE(nFrames);
    const double binHz = dsr / N;
    const int bandEdge[6] = {0, (int)(150 / binHz), (int)(400 / binHz), (int)(1000 / binHz), (int)(2500 / binHz), N / 2 + 1};
    const int hiLo = (int)(1500 / binHz);
    std::vector<std::complex<float>> buf(N);
    const int lowBins = std::max(2, (int)(160.0 / (dsr / N)));
    for (size_t f = 0; f < nFrames; ++f) {
        if ((f & 2047) == 0 && cancelled(cancel)) return false;
        for (int i = 0; i < N; ++i) buf[i] = std::complex<float>(x[f * hop + i] * win[i], 0.f);
        fft(buf);
        float s = 0, sl = 0, sh = 0;
        float be[5] = {0, 0, 0, 0, 0};
        int band = 0;
        for (int k = 0; k <= N / 2; ++k) {
            const float mag = std::abs(buf[k]);
            cur[k] = std::log1p(20.f * mag);
            const float d = std::max(0.f, cur[k] - prev[k]);
            s += d;
            if (k <= lowBins) sl += d;
            if (k >= hiLo) sh += d;
            while (band < 4 && k >= bandEdge[band + 1]) ++band;
            be[band] += mag * mag;
        }
        flux[f] = f ? s : 0.f;
        lowFlux[f] = f ? sl : 0.f;
        hiFlux[f] = f ? sh : 0.f;
        for (int b = 0; b < 5; ++b) bandE[f][b] = std::log(1e-6f + be[b]);
        std::swap(prev, cur);
    }

    // 3) Quitar la tendencia (media móvil de ~1 s) y rectificar.
    auto detrend = [&](const std::vector<float>& in) {
        std::vector<float> o(nFrames);
        const int half = (int)(fps * 0.5);
        double acc = 0; int cnt = 0;
        size_t lo = 0, hi = 0;
        for (size_t n = 0; n < nFrames; ++n) {
            const size_t wantHi = std::min(nFrames, n + half + 1), wantLo = n > (size_t)half ? n - half : 0;
            while (hi < wantHi) { acc += in[hi++]; ++cnt; }
            while (lo < wantLo) { acc -= in[lo++]; --cnt; }
            o[n] = std::max(0.f, in[n] - (float)(acc / std::max(1, cnt)));
        }
        return o;
    };
    out.o = detrend(flux);
    out.oLow = detrend(lowFlux);
    out.lowFlux = std::move(lowFlux);
    out.hiFlux = std::move(hiFlux);
    out.bandE = std::move(bandE);
    out.fps = fps; out.frameOffsetSec = frameOffsetSec; out.nFrames = nFrames;
    return true;
}

// Valor máximo de una curva en la trama más cercana a t (±1 trama).
float curveAt(const std::vector<float>& v, const Onsets& od, double t) {
    const long long f = std::llround((t - od.frameOffsetSec) * od.fps);
    if (f < 1 || f + 1 >= (long long)v.size()) return 0.f;
    return std::max({v[f - 1], v[f], v[f + 1]});
}

int chooseDownbeat(const Onsets& od, double bpm, double t0) {
    const double beat = 60.0 / bpm, fps = od.fps;
    const double span = (double)od.nFrames / fps;
    double kick[4] = {}, hiOn[4] = {}, change[4] = {}, accent[4] = {};
    const long long halfF = std::max(2LL, (long long)(beat * fps * 0.9));
    // Sumas acumuladas de la energía por bandas: media de cualquier tramo en O(1).
    std::vector<std::array<double, 5>> cum(od.nFrames + 1);
    cum[0] = {0, 0, 0, 0, 0};
    for (size_t f = 0; f < od.nFrames; ++f) for (int b = 0; b < 5; ++b) cum[f + 1][b] = cum[f][b] + od.bandE[f][b];
    const long long barF = (long long)std::llround(4 * beat * fps);
    auto meanBand = [&](long long f0, long long f1, double* m) {
        if (f0 < 0 || f1 > (long long)od.nFrames || f1 <= f0) return false;
        for (int b = 0; b < 5; ++b) m[b] = (cum[f1][b] - cum[f0][b]) / (double)(f1 - f0);
        return true;
    };
    int n = 0;
    for (double t = t0; t < span; t += beat, ++n) {
        const int j = n % 4;
        kick[j] += curveAt(od.lowFlux, od, t);
        hiOn[j] += curveAt(od.hiFlux, od, t);
        // Cambio de sonido: el compás que empieza en este pulso frente al compás anterior. Lo
        // que se repite en cada compás se anula; quedan los elementos que entran o cambian,
        // y eso pasa al empezar el compás (y las frases), no a mitad.
        const long long f = std::llround((t - od.frameOffsetSec) * fps);
        double before[5], after[5];
        if (meanBand(f - barF, f, before) && meanBand(f, f + barF, after)) {
            double d = 0;
            for (int b = 0; b < 5; ++b) d += std::fabs(after[b] - before[b]);
            change[j] += d;
        }
        // Acento: el pulso que empieza frente al anterior (el «1» suele sonar más lleno).
        if (meanBand(f - halfF, f - 2, before) && meanBand(f + 2, f + halfF, after)) {
            double d = 0;
            for (int b = 0; b < 5; ++b) d += std::fabs(after[b] - before[b]);
            accent[j] += d;
        }
    }
    if (n < 16) {   // pista corta: el pulso con más bombo
        int bj = 0;
        for (int j = 1; j < 4; ++j) if (kick[j] > kick[bj]) bj = j;
        return bj;
    }
    // Caja/palmas: el «1» es el pulso anterior a la caja (cuenta 2 y 4 frente a 1 y 3).
    double back[4];
    for (int j = 0; j < 4; ++j) back[j] = hiOn[(j + 1) % 4] + hiOn[(j + 3) % 4] - hiOn[j] - hiOn[(j + 2) % 4];
    // Cada pista se normaliza y pesa según su contraste respecto a su nivel real (una pista
    // plana, o una diferencia que solo es ruido, no decide nada).
    auto mean4 = [](const double* v) { return (v[0] + v[1] + v[2] + v[3]) / 4.0 + 1e-12; };
    auto score = [](const double* v, double ref, double* out, double weight) {
        double mn = v[0], mx = v[0];
        for (int j = 0; j < 4; ++j) { mn = std::min(mn, v[j]); mx = std::max(mx, v[j]); }
        if (mx - mn < 1e-12) return;
        const double contrast = std::min(1.0, (mx - mn) / ref);
        for (int j = 0; j < 4; ++j) out[j] += weight * contrast * (v[j] - mn) / (mx - mn);
    };
    double total[4] = {0, 0, 0, 0};
    score(kick, mean4(kick), total, 1.0);
    score(back, 2.0 * mean4(hiOn), total, 1.0);
    score(accent, mean4(accent), total, 1.2);
    score(change, mean4(change), total, 0.8);
    int bj = 0;
    for (int j = 1; j < 4; ++j) if (total[j] > total[bj]) bj = j;
    if (std::getenv("RDJ_BPM_DEBUG"))
        std::fprintf(stderr, "compás: bombo %.3g %.3g %.3g %.3g · caja %.3g %.3g %.3g %.3g · acento %.3g %.3g %.3g %.3g · compases %.3g %.3g %.3g %.3g -> %d\n", kick[0],
                     kick[1], kick[2], kick[3], back[0], back[1], back[2], back[3], accent[0], accent[1], accent[2], accent[3], change[0], change[1], change[2], change[3], bj);
    return bj;
}

// Fase de los pulsos (pasos 6 y 7): primer tiempo de compás en [0, 4 pulsos) para ese tempo.
double gridAnchor(const Onsets& od, double bpm) {
    const std::vector<float>& o = od.o;
    const std::vector<float>& oLow = od.oLow;
    const double fps = od.fps, frameOffsetSec = od.frameOffsetSec;

    // 6) Fase: argumento de la DFT a ese tempo. Se prefiere la curva de graves
    //    (bombo): con la de banda ancha, charles fuertes a contratiempo pueden
    //    desplazar la fase medio pulso.
    auto concentration = [](const std::vector<float>& v, std::complex<double> X) {
        double s = 0; for (float f : v) s += f;
        return s > 0 ? std::abs(X) / s : 0.0;
    };
    const std::complex<double> Xfull = tempoBin(o, bpm, fps), Xlow = tempoBin(oLow, bpm, fps);
    const bool useLow = concentration(oLow, Xlow) > 0.12 && concentration(oLow, Xlow) > 0.5 * concentration(o, Xfull);
    const std::complex<double> X = useLow ? Xlow : Xfull;
    const double w = kPi2 * (bpm / 60.0) / fps;  // rad por trama
    const double periodFrames = kPi2 / w;
    double n0 = std::fmod(-std::arg(X) / w, periodFrames);
    if (n0 < 0) n0 += periodFrames;
    const double beatSec = 60.0 / bpm;
    double t0 = n0 / fps + frameOffsetSec;

    // 7) Primer tiempo de compás: cuál de los 4 pulsos es el «1». Se combinan tres pistas,
    //    cada una con el peso de lo clara que sea en esta canción:
    //    - bombo: el «1» suele llevarlo (hip-hop, drum&bass, breaks); con bombo en todos
    //      los pulsos (house, techno) no distingue nada y pesa cero;
    //    - caja/palmas en 2 y 4: separa {1,3} de {2,4};
    //    - cambios de sonido: los elementos entran, salen y cambian de acorde al empezar
    //      el compás (sobre todo al empezar las frases de 8 compases), no en el pulso 3.
    const int bestJ = chooseDownbeat(od, bpm, t0);
    double anchor = t0 + bestJ * beatSec;
    const double bar = 4 * beatSec;
    anchor = std::fmod(anchor, bar);
    if (anchor < 0) anchor += bar;

    return anchor;
}
// 8) Fase a la muestra: la curva de ataques trabaja con tramas de 10 ms y ventanas de
//    23 ms, y su retraso depende del sonido (un bombo con mucho sub llega más tarde que
//    una palmada). Aquí, en cada pulso de la rejilla, se busca en la señal completa el
//    instante en que arranca el golpe (su envolvente cruza el 30 % de su subida)
//    y la mediana ponderada de esas diferencias corrige la rejilla. Así todas las pistas
//    quedan medidas igual y los golpes de dos canciones sincronizadas caen juntos.
double refineAnchor(const float* pcm, uint64_t frames, uint32_t sr, double bpm, double anchor) {
    if (!pcm || bpm <= 0 || sr < 8000) return anchor;
    const double beat = 60.0 / bpm;
    const uint64_t maxFrames = std::min<uint64_t>(frames, (uint64_t)sr * 60 * 8);
    const int W = (int)(sr * 0.035);          // ±35 ms alrededor del pulso
    const int pre = (int)(sr * 0.020);        // calentamiento del seguidor de picos
    // Envolvente: seguidor de picos (sube al instante, baja en ~8 ms). Con una media de 1 ms,
    // un bombo grave (ondas de 6 ms) bajaba a cero en cada paso por cero y el arranque se
    // detectaba tarde; el seguidor sube en el primer cuarto de onda y ya no baja.
    const float rel = (float)std::exp(-1.0 / (0.008 * sr));
    std::vector<float> env(2 * W);
    struct Hit { double off, w; };
    std::vector<Hit> hits;
    const double first = anchor - std::floor(anchor / beat) * beat;
    for (double t = first; t * sr + W + 2 < (double)maxFrames; t += beat) {
        const int64_t c = (int64_t)std::llround(t * sr);
        const int64_t s0 = c - W;
        if (s0 - pre < 0) continue;
        float f = 0;
        for (int64_t i = s0 - pre; i < s0; ++i) { const float* p = pcm + i * 2; f = std::max(std::fabs(0.5f * (p[0] + p[1])), f * rel); }
        int pk = 0;
        for (int i = 0; i < 2 * W; ++i) {
            const float* p = pcm + (s0 + i) * 2;
            f = std::max(std::fabs(0.5f * (p[0] + p[1])), f * rel);
            env[i] = f;
            if (env[i] > env[pk]) pk = i;
        }
        float base = env[pk];
        for (int i = 0; i <= pk; ++i) base = std::min(base, env[i]);
        const float rise = env[pk] - base;
        if (rise <= 1e-5f) continue;
        const float thr = base + 0.3f * rise;
        int on = pk;
        while (on > 0 && env[on - 1] >= thr) --on;
        if (on == 0) continue;   // el golpe empezó antes de la ventana: no sirve
        hits.push_back({(double)(on - W) / sr, rise});
    }
    if (hits.size() < 16) return anchor;
    // Solo los golpes claros (la mitad más fuerte) y mediana ponderada.
    std::vector<double> ws;
    for (auto& h : hits) ws.push_back(h.w);
    std::nth_element(ws.begin(), ws.begin() + ws.size() / 2, ws.end());
    const double wMed = ws[ws.size() / 2];
    std::vector<Hit> good;
    for (auto& h : hits) if (h.w >= wMed) good.push_back(h);
    std::sort(good.begin(), good.end(), [](const Hit& a, const Hit& b) { return a.off < b.off; });
    double tot = 0;
    for (auto& h : good) tot += h.w;
    double acc = 0, med = 0;
    for (auto& h : good) { acc += h.w; if (acc >= tot * 0.5) { med = h.off; break; } }
    // Solo se corrige si la mayoría de los golpes coinciden (si no, es que no hay golpes claros en los pulsos).
    int near = 0;
    for (auto& h : good) if (std::fabs(h.off - med) < 0.004) ++near;
    if (std::getenv("RDJ_BPM_DEBUG")) {
        std::fprintf(stderr, "fase: %zu golpes, %zu claros, mediana %+.1f ms, %d cerca\n", hits.size(), good.size(), med * 1000, near);
        std::vector<int> hist(15, 0);
        for (auto& h : good) { const int b = (int)std::floor((h.off + 0.035) / 0.005); if (b >= 0 && b < 15) ++hist[b]; }
        for (int b = 0; b < 15; ++b) std::fprintf(stderr, "%+d:%d ", b * 5 - 35, hist[b]);
        std::fprintf(stderr, "\n");
    }
    if (near < (int)good.size() * 0.4) return anchor;
    const double bar = 4 * beat;
    double a2 = std::fmod(anchor + med, bar);
    if (a2 < 0) a2 += bar;
    return a2;
}

// Tempo doble (drum&bass, jungle): con el tempo mitad, la caja (2 y 4 del tempo de verdad)
// cae a contratiempo. Si los ataques agudos a contratiempo dominan claramente a los de
// los pulsos y el tempo doble cabe en el rango, el tempo de verdad es el doble.
bool looksDoubleTime(const Onsets& od, double bpm, double t0) {
    const double beat = 60.0 / bpm, span = (double)od.nFrames / od.fps;
    double on = 0, off = 0, lowOn = 0, lowOff = 0;
    for (double t = t0; t + beat < span; t += beat) {
        on += curveAt(od.hiFlux, od, t);
        off += curveAt(od.hiFlux, od, t + beat * 0.5);
        lowOn += curveAt(od.lowFlux, od, t);
        lowOff += curveAt(od.lowFlux, od, t + beat * 0.5);
    }
    if (std::getenv("RDJ_BPM_DEBUG")) std::fprintf(stderr, "doble: agudos pulso %.3g contratiempo %.3g · graves %.3g %.3g\n", on, off, lowOn, lowOff);
    // Caja a contratiempo y bombo sobre todo en los pulsos.
    return off > 1.6 * on && lowOn > lowOff;
}

} // namespace

BeatInfo detectBeats(const float* pcm, uint64_t frames, uint32_t sr, double minBpm, double maxBpm, const std::atomic<bool>* cancel) {
    BeatInfo info;
    Onsets od;
    if (!onsetCurves(pcm, frames, sr, cancel, od)) return info;
    const std::vector<float>& o = od.o;
    const std::vector<float>& oLow = od.oLow;
    const double fps = od.fps;
    // 4) Barrido de tempo: |DFT| de la curva de ataques, con una preferencia suave
    //    por 90-170 BPM para resolver empates de octava.
    auto prior = [](double b) { const double l = std::log2(b / 128.0); return std::exp(-0.5 * l * l / (1.0 * 1.0)); };
    std::vector<double> scan;
    double bestS = -1, sumS = 0; int nS = 0;
    for (double b = minBpm; b <= maxBpm + 1e-9; b += 0.05) {
        if ((nS & 63) == 0 && cancelled(cancel)) return BeatInfo{};
        const double s = std::abs(tempoBin(o, b, fps)) * prior(b);
        scan.push_back(s);
        sumS += s; ++nS;
        bestS = std::max(bestS, s);
    }
    if (nS == 0 || bestS <= 0) return info;

    // 4b) Candidatos: los máximos locales más fuertes y, para el mejor, sus
    //     tempos emparentados (4/5, 5/4, 3/4, 4/3, 2/3, 3/2), que son las
    //     confusiones típicas con patrones sincopados.
    auto scanIndex = [&](double b) { return (long)std::lround((b - minBpm) / 0.05); };
    auto localMax = [&](double b) {  // máximo del barrido en ±0,6 BPM
        long i0 = scanIndex(b);
        long best = -1;
        for (long i = i0 - 12; i <= i0 + 12; ++i)
            if (i >= 0 && i < (long)scan.size() && (best < 0 || scan[i] > scan[best])) best = i;
        return best < 0 ? b : minBpm + 0.05 * best;
    };
    std::vector<double> cands;
    {
        std::vector<std::pair<double, double>> peaks;  // (puntuación, bpm)
        for (size_t i = 1; i + 1 < scan.size(); ++i)
            if (scan[i] >= scan[i - 1] && scan[i] > scan[i + 1] && scan[i] > 0.25 * bestS) peaks.push_back({scan[i], minBpm + 0.05 * i});
        std::sort(peaks.rbegin(), peaks.rend());
        if (peaks.size() > 8) peaks.resize(8);
        if (peaks.empty()) peaks.push_back({bestS, minBpm + 0.05 * (std::max_element(scan.begin(), scan.end()) - scan.begin())});
        for (auto& p : peaks) cands.push_back(p.second);
        for (double r : {0.8, 1.25, 0.75, 4.0 / 3.0, 2.0 / 3.0, 1.5}) {
            const double b = peaks[0].second * r;
            if (b >= minBpm && b <= maxBpm) cands.push_back(localMax(b));
        }
    }

    // 4c) Puntuación: autocorrelación de la curva de ataques a 1, 2, 4 y 8
    //     pulsos. La música de baile repite el patrón cada compás y cada dos
    //     compases, lo que solo encaja con el tempo verdadero.
    auto acAt = [&](const std::vector<float>& v, double lag) {
        const long L0 = (long)std::floor(lag);
        const double fr = lag - L0;
        if (L0 + 1 >= (long)v.size()) return 0.0;
        double a = 0, b = 0;
        for (size_t n = (size_t)L0 + 1; n < v.size(); ++n) {
            a += (double)v[n] * v[n - L0];
            b += (double)v[n] * v[n - L0 - 1];
        }
        return (a * (1 - fr) + b * fr) / (double)(v.size() - L0);
    };
    const double ac0 = acAt(o, 0) + 1e-12, ac0Low = acAt(oLow, 0) + 1e-12;
    double bestB = cands[0], bestScore = -1;
    for (double b : cands) {
        if (cancelled(cancel)) return BeatInfo{};
        const double P = fps * 60.0 / b;
        double bar = 0;
        for (int k : {1, 2, 4, 8}) bar += acAt(o, P * k) / ac0 + acAt(oLow, P * k) / ac0Low;
        const double score = bar * prior(b);
        if (std::getenv("RDJ_BPM_DEBUG")) std::fprintf(stderr, "cand %.2f compas %.4g total %.4g\n", b, bar, score);
        if (score > bestScore) { bestScore = score; bestB = b; }
    }

    // 5) Afinado: primero con autocorrelación a 4, 8 y 16 pulsos (retardos
    //    largos = más resolución), después con la DFT en un margen estrecho.
    double midB = bestB, midS = -1;
    for (double b = bestB - 0.3; b <= bestB + 0.3 + 1e-9; b += 0.01) {
        const double P = fps * 60.0 / b;
        double s = 0;
        for (int k : {4, 8, 16}) s += acAt(o, P * k) / ac0 + acAt(oLow, P * k) / ac0Low;
        if (s > midS) { midS = s; midB = b; }
    }
    double fineB = midB, fineS = -1;
    for (double b = midB - 0.03; b <= midB + 0.03; b += 0.001) {
        const double s = std::abs(tempoBin(o, b, fps));
        if (s > fineS) { fineS = s; fineB = b; }
    }
    // 5b) Afinado por regresión de fase: con el tempo exacto, la fase del pulso
    //     medida en ventanas de ~12 s es la misma en toda la pista; si el tempo
    //     está un poco desviado, la fase avanza en línea recta y su pendiente es
    //     justo el error. Así se llega a milésimas de BPM, que es lo que hace
    //     falta para que dos pistas sincronizadas no se separen al cabo de un rato.
    auto refine = [&](double b) {
        const double w = kPi2 * (b / 60.0) / fps;
        const size_t W = (size_t)(fps * 12.0), H = std::max<size_t>(1, W / 2);
        if (o.size() < W * 3) return b;
        struct Win { double x, a, m; };
        std::vector<Win> wins;
        double maxM = 0;
        for (size_t s0 = 0; s0 + W <= o.size(); s0 += H) {
            std::complex<double> acc(0, 0), ph = std::polar(1.0, -w * (double)s0);
            const std::complex<double> rot = std::polar(1.0, -w);
            for (size_t n = s0; n < s0 + W; ++n) {
                acc += (double)o[n] * ph;
                ph *= rot;
                if (((n - s0) & 1023) == 1023) ph /= std::abs(ph);
            }
            wins.push_back({s0 + W * 0.5, std::arg(acc), std::abs(acc)});
            maxM = std::max(maxM, std::abs(acc));
        }
        // Solo las ventanas con pulso claro (los cortes sin bombo no dicen nada de la fase).
        std::vector<Win> good;
        for (auto& v : wins) if (v.m >= 0.35 * maxM) good.push_back(v);
        if (good.size() < 3) return b;
        for (size_t i = 1; i < good.size(); ++i) {
            while (good[i].a - good[i - 1].a > kPi2 * 0.5) good[i].a -= kPi2;
            while (good[i].a - good[i - 1].a < -kPi2 * 0.5) good[i].a += kPi2;
        }
        double sw = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (auto& v : good) { sw += v.m; sx += v.m * v.x; sy += v.m * v.a; sxx += v.m * v.x * v.x; sxy += v.m * v.x * v.a; }
        const double den = sw * sxx - sx * sx;
        if (den <= 0) return b;
        const double slope = (sw * sxy - sx * sy) / den;  // rad por trama
        const double nb = (w + slope) * fps * 60.0 / kPi2;
        return std::fabs(nb - b) < 0.5 ? nb : b;  // nunca se aleja del candidato
    };
    double bpm = refine(refine(fineB));
    // Mucha música electrónica usa tempos enteros: se redondea solo si el
    // redondeo no desplaza la rejilla más de 20 ms a lo largo de la pista.
    {
        const double span = (double)o.size() / fps;
        auto drift = [&](double cand) { return std::fabs(cand - bpm) / bpm * span; };
        const double r1 = std::round(bpm), r2 = std::round(bpm * 2) / 2;
        if (drift(r1) < 0.020) bpm = r1;
        else if (drift(r2) < 0.020) bpm = r2;
        else bpm = std::round(bpm * 1000.0) / 1000.0;
    }

    {
        // ¿Tempo doble? (drum&bass detectado a la mitad). La fase del pulso a la mitad de tempo sirve para mirarlo.
        const double a0 = gridAnchor(od, bpm);
        if (bpm * 2 <= maxBpm + 1e-9 && bpm < 100 && looksDoubleTime(od, bpm, a0)) bpm *= 2;
    }
    info.bpm = bpm;
    info.anchorSec = refineAnchor(pcm, frames, sr, bpm, gridAnchor(od, bpm));
    info.confidence = (float)(bestS / (sumS / nS));
    return info;
}

BeatInfo detectGridForBpm(const float* pcm, uint64_t frames, uint32_t sr, double bpm, const std::atomic<bool>* cancel) {
    BeatInfo info;
    Onsets od;
    if (bpm <= 0 || !onsetCurves(pcm, frames, sr, cancel, od)) return info;
    info.bpm = bpm;
    info.anchorSec = refineAnchor(pcm, frames, sr, bpm, gridAnchor(od, bpm));
    info.confidence = 1;
    return info;
}

} // namespace rdj

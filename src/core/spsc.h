// Colas sin bloqueo de un productor / un consumidor.
// Se usan para pasar órdenes de la interfaz al hilo de audio y audio del hilo
// de audio al hilo de grabación sin mutex ni reservas de memoria en tiempo real.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstring>
#include <vector>

namespace rdj {

template <typename T>
class SpscQueue {
public:
    explicit SpscQueue(size_t capacityPow2 = 1024) : buf_(capacityPow2), mask_(capacityPow2 - 1) {}

    bool push(const T& v) {
        const size_t w = write_.load(std::memory_order_relaxed);
        const size_t r = read_.load(std::memory_order_acquire);
        if (w - r >= buf_.size()) return false;
        buf_[w & mask_] = v;
        write_.store(w + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& out) {
        const size_t r = read_.load(std::memory_order_relaxed);
        const size_t w = write_.load(std::memory_order_acquire);
        if (r == w) return false;
        out = buf_[r & mask_];
        read_.store(r + 1, std::memory_order_release);
        return true;
    }

private:
    std::vector<T> buf_;
    size_t mask_;
    alignas(64) std::atomic<size_t> write_{0};
    alignas(64) std::atomic<size_t> read_{0};
};

// Anillo de muestras float (intercaladas). Capacidad en muestras, potencia de 2.
class SpscFloatRing {
public:
    void allocate(size_t capacityPow2) {
        buf_.assign(capacityPow2, 0.0f);
        mask_ = capacityPow2 - 1;
        write_.store(0);
        read_.store(0);
    }
    size_t capacity() const { return buf_.size(); }
    size_t available() const { return write_.load(std::memory_order_acquire) - read_.load(std::memory_order_acquire); }

    // Escribe todo o nada. Devuelve false si no cabe (el llamador cuenta pérdidas).
    bool write(const float* src, size_t n) {
        if (buf_.empty()) return false;
        const size_t w = write_.load(std::memory_order_relaxed);
        const size_t r = read_.load(std::memory_order_acquire);
        if (buf_.size() - (w - r) < n) return false;
        for (size_t i = 0; i < n; ++i) buf_[(w + i) & mask_] = src[i];
        write_.store(w + n, std::memory_order_release);
        return true;
    }

    size_t read(float* dst, size_t n) {
        const size_t r = read_.load(std::memory_order_relaxed);
        const size_t w = write_.load(std::memory_order_acquire);
        const size_t avail = w - r;
        if (n > avail) n = avail;
        for (size_t i = 0; i < n; ++i) dst[i] = buf_[(r + i) & mask_];
        read_.store(r + n, std::memory_order_release);
        return n;
    }

    // Descarta muestras sin leerlas (solo consumidor).
    void skip(size_t n) {
        const size_t r = read_.load(std::memory_order_relaxed);
        const size_t w = write_.load(std::memory_order_acquire);
        if (n > w - r) n = w - r;
        read_.store(r + n, std::memory_order_release);
    }

private:
    std::vector<float> buf_;
    size_t mask_ = 0;
    alignas(64) std::atomic<size_t> write_{0};
    alignas(64) std::atomic<size_t> read_{0};
};

} // namespace rdj

// Historial estéreo post-limitador. Un productor de audio y un lector de interfaz.
// prepare() exige audio parado; push() no reserva, bloquea ni hace E/S.
#pragma once
#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdint>
#include <memory>
#include <vector>

namespace rdj {
class Retrospective {
public:
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    void prepare(unsigned rate) {
        rate_ = rate;
        capacity_ = size_t(rate) * 32 + 512;
        data_ = std::make_unique<std::atomic<uint64_t>[]>(capacity_);
        written_ = 0;
        published_.store(0);
    }
    // El motor entrega bloques de como máximo 256 fotogramas.
    void push(const float* left, const float* right, int n) {
        for (int i = 0; i < n; ++i) {
            uint64_t bits = uint64_t(std::bit_cast<uint32_t>(left[i])) |
                            (uint64_t(std::bit_cast<uint32_t>(right[i])) << 32);
            data_[written_++ % capacity_].store(bits);
        }
        published_.store(written_);
    }
    bool capture(unsigned seconds, std::vector<float>& pcm) const {
        pcm.clear();
        if (!rate_ || seconds == 0 || seconds > 30) return false;
        for (int attempt = 0; attempt < 3; ++attempt) {
            const uint64_t end = published_.load();
            const uint64_t count = std::min(end, uint64_t(seconds) * rate_);
            if (!count) return false;
            const uint64_t start = end - count;
            pcm.resize(size_t(count) * 2);
            for (uint64_t i = 0; i < count; ++i) {
                const uint64_t bits = data_[(start + i) % capacity_].load();
                pcm[size_t(i) * 2] = std::bit_cast<float>(uint32_t(bits));
                pcm[size_t(i) * 2 + 1] = std::bit_cast<float>(uint32_t(bits >> 32));
            }
            // Incluye margen para el bloque aún no publicado. Los fotogramas
            // atómicos evitan carreras incluso si el lector tarda demasiado.
            if (published_.load() - start + 256 < capacity_) return true;
        }
        pcm.clear();
        return false;
    }
private:
    unsigned rate_ = 0;
    size_t capacity_ = 0;
    uint64_t written_ = 0;
    std::atomic<uint64_t> published_{0};
    std::unique_ptr<std::atomic<uint64_t>[]> data_;
};
} // namespace rdj

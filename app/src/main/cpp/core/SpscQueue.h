#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace looper {

// Wait-free single-producer, single-consumer ring. The UI thread pushes; the audio
// thread pops. Nothing allocates after construction, so it is safe on the audio thread.
template <typename T, size_t N>
class SpscQueue {
    static_assert(N > 0 && (N & (N - 1)) == 0, "capacity must be a power of two");

public:
    bool push(const T& value) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail - head_.load(std::memory_order_acquire) == N) return false;
        items_[tail & (N - 1)] = value;
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& out) {
        const size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) return false;
        out = items_[head & (N - 1)];
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

private:
    std::array<T, N> items_{};
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};

}  // namespace looper

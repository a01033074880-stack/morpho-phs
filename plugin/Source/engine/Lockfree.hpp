// SPDX-License-Identifier: AGPL-3.0-only
// MORPHO-PHS plugin — lock-free helpers (header-only, no JUCE).  Used between the audio thread and the UI.
#pragma once
#include <array>
#include <atomic>
#include <cstddef>

namespace morpho_plugin {

// Triple buffer: one writer (audio thread), one reader (UI).  Neither side ever blocks or allocates.
// The writer always has a private back buffer; publish() swaps it with the shared middle slot and marks it fresh;
// the reader swaps its front buffer with the middle slot only if fresh.
template <class T>
class TripleBuffer {
public:
    T& write_buffer() noexcept { return buf_[back_]; }
    void publish() noexcept { back_ = mid_.exchange(back_ | kFresh, std::memory_order_acq_rel) & kIdx; }
    bool update() noexcept {   // reader: returns true if a newer snapshot was taken
        if (!(mid_.load(std::memory_order_acquire) & kFresh)) return false;
        front_ = mid_.exchange(front_, std::memory_order_acq_rel) & kIdx;
        return true;
    }
    const T& read_buffer() const noexcept { return buf_[front_]; }
    T& read_buffer_mut() noexcept { return buf_[front_]; }

private:
    static constexpr int kIdx = 3, kFresh = 4;
    std::array<T, 3> buf_{};
    int back_ = 0, front_ = 2;
    alignas(64) std::atomic<int> mid_{1};
};

// Single-producer single-consumer ring of trivially copyable items (UI -> audio events, MIDI from the editor).
template <class T, std::size_t N>
class SpscQueue {
    static_assert((N & (N - 1)) == 0, "N must be a power of two");
public:
    bool push(const T& v) noexcept {
        const auto w = w_.load(std::memory_order_relaxed);
        if (w - r_.load(std::memory_order_acquire) >= N) return false;   // full: drop (UI events are not critical)
        buf_[w & (N - 1)] = v;
        w_.store(w + 1, std::memory_order_release);
        return true;
    }
    bool pop(T& v) noexcept {
        const auto r = r_.load(std::memory_order_relaxed);
        if (r == w_.load(std::memory_order_acquire)) return false;
        v = buf_[r & (N - 1)];
        r_.store(r + 1, std::memory_order_release);
        return true;
    }

private:
    std::array<T, N> buf_{};
    alignas(64) std::atomic<std::size_t> w_{0};
    alignas(64) std::atomic<std::size_t> r_{0};
};

}  // namespace morpho_plugin

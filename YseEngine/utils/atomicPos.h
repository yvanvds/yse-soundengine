#pragma once
#include <atomic>
#include <cstdint>
#include "../headers/types.hpp"

namespace YSE {
  /// @cond INTERNAL
  class Pos;

  /**
   *  @brief Seqlock-guarded 3D vector used for the listener's spatial state.
   *
   *  The three components are published and observed as ONE consistent
   *  snapshot, so a reader can never see a mixed-generation vector (a fresh
   *  ``x`` combined with a stale ``y`` / ``z``). This replaces the previous
   *  three-independent-atomics layout, whose component-wise reads on the audio
   *  thread could tear against component-wise writes on the control thread
   *  (issue #196).
   *
   *  Threading discipline — a seqlock supports a SINGLE writer and any number
   *  of concurrent readers:
   *    - ``pos`` / ``forward`` / ``up`` are written only by the control thread
   *      (the ``YSE::listener`` setters).
   *    - ``vel`` is written only by the audio thread
   *      (``listenerImplementation::update()``).
   *  Two writers on the same instance would corrupt the snapshot, so never
   *  cross those roles.
   *
   *  Real-time safety: the writer's critical section is just three relaxed
   *  atomic stores bracketed by two sequence bumps, and the reader only retries
   *  when it collides with an in-progress write. Neither side allocates, locks,
   *  or blocks, so ``load()`` (audio thread) and the audio-thread ``vel``
   *  ``store()`` are safe on the callback path.
   */
  class API aPos {
  public:
    aPos() {
      store(0.f, 0.f, 0.f);
    }
    aPos(Flt x, Flt y, Flt z) {
      store(x, y, z);
    }
    // Pos-typed constructor / assignment are defined in vector.cpp, where Pos
    // is a complete type.
    aPos(const Pos& v);
    aPos& operator=(const Pos& v);

    /** @brief Publish all three components as one consistent snapshot. */
    void store(Flt x, Flt y, Flt z) {
      const uint32_t s = seq_.load(std::memory_order_relaxed);
      // Odd sequence marks a write in progress; a reader that observes it (or
      // sees the sequence change across its read) retries.
      seq_.store(s + 1, std::memory_order_relaxed);
      std::atomic_thread_fence(std::memory_order_release);
      x_.store(x, std::memory_order_relaxed);
      y_.store(y, std::memory_order_relaxed);
      z_.store(z, std::memory_order_relaxed);
      std::atomic_thread_fence(std::memory_order_release);
      seq_.store(s + 2, std::memory_order_release);
    }
    void store(const Pos& v);

    /** @brief Read a tear-free snapshot into three floats. */
    void loadInto(Flt& x, Flt& y, Flt& z) const {
      uint32_t s0, s1;
      do {
        s0 = seq_.load(std::memory_order_acquire);
        x = x_.load(std::memory_order_relaxed);
        y = y_.load(std::memory_order_relaxed);
        z = z_.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        s1 = seq_.load(std::memory_order_relaxed);
        // Retry if a write was in progress (odd) or one completed between the
        // two sequence reads.
      } while ((s0 & 1u) || s0 != s1);
    }

    /** @brief Read a tear-free snapshot as a Pos (defined in vector.cpp). */
    Pos load() const;

  private:
    // Payload atomics are accessed relaxed; the seqlock sequence provides both
    // the happens-before ordering and the snapshot-consistency guarantee.
    // Keeping them atomic (rather than plain floats read racily) makes the
    // reader well-defined under the C++ memory model and clean under TSan.
    std::atomic<Flt> x_{0.f};
    std::atomic<Flt> y_{0.f};
    std::atomic<Flt> z_{0.f};
    std::atomic<uint32_t> seq_{0};
  };
  /// @endcond
} // namespace YSE

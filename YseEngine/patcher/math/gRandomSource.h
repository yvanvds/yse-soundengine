#pragma once
#include <atomic>
#include "../../headers/types.hpp"
#include "../../utils/misc.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Per-object, seedable pseudo-random source shared by the patcher's
     *         random objects (``.drunk`` #453, and the ``.urn`` / ``.decide`` /
     *         ``.prob`` family that follows).
     *
     *  The engine already ships a real-time-safe generator in
     *  ``YSE::RANDOM`` (``utils/misc.hpp``), and ``.random`` uses it directly.
     *  That one keeps its state in a ``thread_local``, which is exactly right
     *  for "give me a number" but wrong for a patcher object that has to be
     *  *reproducible*: two objects on the same thread interleave draws from one
     *  shared stream, so neither can be replayed, and a unit test can assert
     *  nothing sharper than "the value was in range". A random walk needs more
     *  than that — the step distribution and the boundary behaviour are the
     *  interesting parts, and pinning them takes a fixed seed.
     *
     *  So the state lives *per object* here, and the engine generator is used
     *  only to pick an arbitrary stream when no seed was given.
     *
     *  ### The generator
     *
     *  SplitMix64 over a per-object counter: the ``stream`` word selects which
     *  sequence, the counter selects the position within it. Two consequences
     *  that matter:
     *
     *  - **Reproducible.** The n-th draw after ``Seed(s)`` is a pure function
     *    of ``s`` and ``n``. Nothing else — no thread identity, no wall clock,
     *    no global state — enters into it.
     *  - **Race-free without a lock.** The only mutable word touched per draw
     *    is a 32-bit counter advanced with ``fetch_add``, so a control thread
     *    and the audio thread hitting the same object cannot corrupt it; the
     *    worst case is that they consume each other's positions in the stream.
     *
     *  Both atomics are 32 bit, which is lock-free on every target this engine
     *  builds for (a 64-bit atomic is not universally so on 32-bit ARM). A draw
     *  is one relaxed ``fetch_add``, one relaxed load, three multiplies and a
     *  few shifts: no allocation, no lock, no I/O, no division, no unbounded
     *  loop. Safe to call from ``Calculate()`` and from inlet handlers running
     *  on the audio thread.
     *
     *  ### Bias
     *
     *  ``Bounded()`` uses Lemire's multiply-shift reduction, and deliberately
     *  omits its rejection step for the same reason ``YSE::RANDOM::Bounded``
     *  does: rejection makes the running time unbounded, which does not belong
     *  in an audio callback. The residual bias stays under ``bound / 2^32``.
     */
    class RandomSource {
    public:
      /** @brief Construct with an arbitrary stream (as if ``Seed(0)``). */
      RandomSource() {
        Seed(0);
      }

      /**
       *  @brief (Re)start the sequence.
       *
       *  A non-zero @p seed gives a deterministic stream: the same seed always
       *  replays the same draws, from the beginning. Seed 0 means "pick one for
       *  me" and takes an arbitrary stream from the engine-wide generator —
       *  which is itself reproducible across runs unless the host called
       *  ``YSE::Randomize()``, so a patch is repeatable by default and varies
       *  once the application asks it to. That is the same contract
       *  ``.random`` offers, and it matches Max's ``@seed 0``.
       *
       *  Real-time safe, so an object may reseed itself from the audio thread.
       */
      void Seed(UInt seed) {
        // The engine generator seeds its own thread state on first use; both
        // paths are allocation- and lock-free.
        const UInt picked = (seed != 0) ? seed : static_cast<UInt>(RANDOM::Next() >> 32);
        // Stored verbatim. Every 32-bit value is a legal stream — SplitMix64
        // has no bad states, zero included — so nothing may be forced here:
        // an earlier version OR-ed in the low bit and thereby handed seeds
        // 4242 and 4243 the *same* walk, which the "different seed, different
        // walk" test caught.
        stream_.store(picked, std::memory_order_relaxed);
        counter_.store(0, std::memory_order_relaxed);
      }

      /** @brief 64 well-distributed bits; advances the sequence by one. */
      U64 Next() {
        const auto n = static_cast<U64>(counter_.fetch_add(1, std::memory_order_relaxed));
        const auto s = static_cast<U64>(stream_.load(std::memory_order_relaxed));
        // The odd gamma walks the counter across the whole 64-bit space; the
        // odd stream multiplier moves the walk to a different offset per seed.
        // RANDOM::Mix (SplitMix64's finalizer) does the avalanching.
        return RANDOM::Mix((s * 0x2545F4914F6CDD1DULL) + (n * 0x9E3779B97F4A7C15ULL));
      }

      /**
       *  @brief Uniform integer in [0, @p bound). Returns 0 when @p bound is 0,
       *         so there is no division and no divide-by-zero to guard.
       */
      UInt Bounded(UInt bound) {
        const U64 product = static_cast<U64>(static_cast<UInt>(Next() >> 32)) * bound;
        return static_cast<UInt>(product >> 32);
      }

      /**
       *  @brief Uniform integer in [@p low, @p high). Returns @p low when the
       *         range is empty (``high <= low``).
       *
       *  Widened to 64 bit internally: a span such as [INT_MIN, INT_MAX)
       *  overflows ``Int``.
       */
      Int Between(Int low, Int high) {
        if (high <= low) return low;
        const I64 span = static_cast<I64>(high) - static_cast<I64>(low);
        const auto offset = static_cast<I64>(Bounded(static_cast<UInt>(span)));
        return static_cast<Int>(static_cast<I64>(low) + offset);
      }

      /** @brief Uniform float in [0, 1) — 24 bits, the full float mantissa. */
      Flt NextFloat() {
        return static_cast<Flt>(Next() >> 40) * (1.0f / 16777216.0f);
      }

      /**
       *  @brief How many draws have been taken since the last ``Seed()``.
       *
       *  Exists so tests can assert *how often* an object draws — "one draw per
       *  bang" is what makes a seeded sequence replayable, and it is the kind of
       *  thing a refactor breaks silently.
       */
      UInt Draws() const {
        return counter_.load(std::memory_order_relaxed);
      }

    private:
      // Which sequence. Written by Seed(), read on every draw.
      std::atomic<UInt> stream_{1};
      // Where in it. The only word that moves per draw.
      std::atomic<UInt> counter_{0};
    };

  } // namespace PATCHER
} // namespace YSE

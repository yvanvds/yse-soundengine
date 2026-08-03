/*
  ==============================================================================

    misc.h
    Created: 29 Jan 2014 11:11:59pm
    Author:  yvan

  ==============================================================================
*/

#ifndef MISC_H_INCLUDED
#define MISC_H_INCLUDED

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include "../headers/types.hpp"

namespace YSE {

  /**
   *  @brief Clamp ``x`` to the inclusive range [``min``, ``max``].
   *
   *  Templated so it works on any types that compare with ``<``. The result
   *  is written back into ``x``.
   */
  template <typename T0, typename T1, typename T2> inline void Clamp(T0& x, T1 min, T2 max) {
    if (x < min)
      x = min;
    else if (x > max)
      x = max;
  }

  /**
   *  @brief Internals of the engine-wide pseudo-random generator.
   *
   *  The generator is **xorshift128+** over per-thread state, replacing the C
   *  ``rand()`` / ``srand()`` pair. ``rand()`` keeps hidden *global* state, so
   *  the concurrent calls this engine makes from the audio callback (the
   *  ``gRandom`` patcher object, granulator jitter, the LFO's random shapes),
   *  from the control thread and from the manager threads are a data race on
   *  that state — and some libc implementations serialise inside ``rand()``,
   *  which is exactly what must not happen on the callback path.
   *
   *  Everything here is real-time safe: no allocation, no locks, no I/O, no
   *  division. The per-thread state is ``thread_local`` **and
   *  constant-initialised**, so no lazy-init guard variable is checked on the
   *  hot path. A thread seeds itself on its first draw, which costs one
   *  well-predicted branch, one relaxed 32-bit ``fetch_add`` and two integer
   *  mixing rounds — once per thread, ever.
   */
  namespace RANDOM {

    // The three variables below carry a bare suppression marker on their
    // declaration line. It is kept short on purpose: clang-format counts
    // trailing comments toward the 100-column limit and would otherwise wrap
    // the statement and strand the marker on the wrong line (issue #409).
    //
    // They suppress cpp:S5421, "global variables should be const". These *are*
    // the generator's mutable state, so const is not on the table, and the usual
    // way to hide them — a function-local `static` accessor — would put a
    // lazy-initialisation guard check on every draw, including on the audio
    // callback path. That is exactly what the constant initialisation below
    // exists to avoid, and CLAUDE.md rule 3 forbids trading it away. See #434.

    /**
     *  @brief Global seed base, published by ``Randomize()``.
     *
     *  Left at a fixed constant otherwise, so an application that never calls
     *  ``Randomize()`` gets a reproducible sequence — the same contract an
     *  unseeded ``rand()`` offered.
     */
    inline std::atomic<UInt> SeedBase{0x9E3779B9U}; // NOSONAR

    /** @brief Hands every thread a distinct stream index on its first draw. */
    inline std::atomic<UInt> StreamCounter{0}; // NOSONAR

    /**
     *  @brief Per-thread generator state; ``{0, 0}`` means "not seeded yet".
     *
     *  Constant-initialised on purpose: a dynamically initialised
     *  ``thread_local`` would add a guard-variable check to every single draw.
     *  ``std::array`` keeps that property — it is an aggregate, so ``{}`` is
     *  still a constant initializer and the state stays in ``.tbss``.
     */
    inline thread_local std::array<U64, 2> State{}; // NOSONAR

    /** @brief SplitMix64 finalizer — turns a counter into well-distributed bits. */
    inline U64 Mix(U64 z) {
      z += 0x9E3779B97F4A7C15ULL;
      z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
      z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
      return z ^ (z >> 31);
    }

    /**
     *  @brief Give the calling thread a fresh, non-zero state.
     *
     *  Lock-free and allocation-free, so it is safe even when it happens to run
     *  inside an audio callback — which is where the first draw on the audio
     *  thread lands unless the host calls it earlier.
     */
    inline void SeedThread() {
      const auto base = static_cast<U64>(SeedBase.load(std::memory_order_relaxed));
      const auto stream = static_cast<U64>(StreamCounter.fetch_add(1, std::memory_order_relaxed));
      State[0] = Mix((base << 32) ^ (stream + 1));
      State[1] = Mix(State[0]) | 1ULL; // the |1 keeps the pair from ever being all-zero
    }

    /** @brief xorshift128+ — 64 pseudo-random bits in a handful of instructions. */
    inline U64 Next() {
      if ((State[0] | State[1]) == 0) SeedThread();
      U64 x = State[0];
      const U64 y = State[1];
      State[0] = y;
      x ^= x << 23;
      State[1] = x ^ y ^ (x >> 17) ^ (y >> 26);
      return State[1] + y;
    }

    /**
     *  @brief Uniform integer in [0, ``bound``) via Lemire's multiply-shift
     *  reduction. Returns 0 when ``bound`` is 0, so there is no division and no
     *  divide-by-zero to guard against.
     *
     *  The rejection step of Lemire's *exactly* unbiased variant is deliberately
     *  left out: it makes the running time unbounded, which does not belong in
     *  an audio callback. Without it the bias stays below ``bound / 2^32`` —
     *  orders of magnitude under one least-significant bit for every bound this
     *  engine uses (grain offsets, comb tunings, note pitches, patcher ranges).
     */
    inline UInt Bounded(UInt bound) {
      const U64 product = static_cast<U64>(static_cast<UInt>(Next() >> 32)) * bound;
      return static_cast<UInt>(product >> 32);
    }

    /**
     *  @brief Uniform float in [0, 1).
     *
     *  Draws 24 bits — the full float mantissa — against the 15 bits
     *  ``RAND_MAX`` allowed on MSVC.
     */
    inline Flt NextFloat() {
      return static_cast<Flt>(Next() >> 40) * (1.0f / 16777216.0f);
    }

  } // namespace RANDOM

  /**
   *  @brief Seed the random generator from the current time. Call once at startup.
   *
   *  Optional — without it the engine still produces random-looking output, only
   *  reproducibly so. This publishes a clock-derived seed base and re-seeds the
   *  calling thread immediately; other threads pick the new base up on their
   *  next draw.
   *
   *  Not real-time safe (it reads the system clock): call it from the control
   *  thread at startup, never from an audio callback. Note that the generator
   *  state is header-local, so a host that links the engine as a shared library
   *  seeds its own copy, not the engine's.
   */
  inline void Randomize() {
    const auto now =
        static_cast<U64>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    RANDOM::SeedBase.store(static_cast<UInt>(RANDOM::Mix(now) >> 32), std::memory_order_relaxed);
    // Zeroing the state makes the calling thread re-seed from the new base on
    // its next draw.
    RANDOM::State[0] = 0;
    RANDOM::State[1] = 0;
  }

  /**
   *  @brief Random integer in [0, ``max``). Returns 0 when ``max <= 0``.
   *
   *  The empty range has no other sensible answer, and saying so explicitly is
   *  what retires the divide-by-zero that ``rand() % max`` carried: the patcher's
   *  ``gRandom`` object lets a patch set its range to 0 from the audio thread.
   */
  inline Int Random(Int max) {
    if (max <= 0) return 0;
    return static_cast<Int>(RANDOM::Bounded(static_cast<UInt>(max)));
  }

  /** @brief Random integer in [``min``, ``max``). Returns ``min`` when ``max <= min``. */
  inline Int Random(Int min, Int max) {
    if (max <= min) return min;
    // Widened to 64 bit: a span such as [INT_MIN, INT_MAX) overflows Int.
    const I64 span = static_cast<I64>(max) - static_cast<I64>(min);
    const auto offset = static_cast<I64>(RANDOM::Bounded(static_cast<UInt>(span)));
    return static_cast<Int>(static_cast<I64>(min) + offset);
  }

  /**
   *  @brief Heavy-tailed random integer biased toward 0; produces fewer high values than
   *  ``Random``. Returns 0 when ``max <= 0``.
   */
  inline Int BigRandom(Int max) {
    if (max <= 0) return 0;
    const auto root = static_cast<Int>(std::sqrt(static_cast<Dbl>(max)));
    return Random(root) * Random(root);
  }

  /** @brief Random float in [0, 1). */
  inline Flt RandomF() {
    return RANDOM::NextFloat();
  }

  /** @brief Random float in [0, ``max``). */
  inline Flt RandomF(Flt max) {
    return RANDOM::NextFloat() * max;
  }

  /** @brief Random float in [``min``, ``max``). */
  inline Flt RandomF(Flt min, Flt max) {
    return min + (RANDOM::NextFloat() * (max - min));
  }

  /**
   *  @brief Random pointer in [``min``, ``max``), stride one ``Flt``.
   *
   *  Both pointers must address the same array. Returns ``min`` for an empty
   *  range.
   */
  inline Flt* Random(Flt* min, Flt* max) {
    if (max <= min) return min;
    auto span = static_cast<U64>(max - min);
    if (span > 0xFFFFFFFFULL) span = 0xFFFFFFFFULL; // the reduction takes a 32-bit bound
    return min + static_cast<std::ptrdiff_t>(RANDOM::Bounded(static_cast<UInt>(span)));
  }

  const Flt Pi_6 = 0.52359878f; ///< PI/6 (30°).
  const Flt Pi_4 = 0.78539816f; ///< PI/4 (45°).
  const Flt Pi_3 = 1.04719755f; ///< PI/3 (60°).
  const Flt Pi_2 = 1.57079633f; ///< PI/2 (90°).
  const Flt Pi = 3.14159265f; ///< PI (180°).
  const Flt Pi2 = 6.28318531f; ///< PI × 2 (360°).
  const Flt ToDegrees = 57.29577951f; ///< Multiply radians by this to get degrees.
  const Flt ToRadians = 0.017453293f; ///< Multiply degrees by this to get radians.

  const Flt Sqrt2 = 1.4142135623730950f; ///< √2.
  const Flt Sqrt3 = 1.7320508075688773f; ///< √3.
  const Flt Sqrt2_2 = 0.7071067811865475f; ///< √2 / 2.
  const Flt Sqrt3_3 = 0.5773502691896257f; ///< √3 / 3.
} // namespace YSE

#endif // MISC_H_INCLUDED

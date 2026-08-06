#pragma once
#include <array>
#include <cstdint>
#include "../pObject.h"
#include "gRandomSource.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Random numbers without repetition — ``.urn`` (issue #454).
     *
     *  Draws from ``[0, limit)`` *without replacement*: every value comes out
     *  exactly once before any of them comes out a second time. That is the
     *  difference between a shuffled deck and a die, and it is what makes the
     *  object usable for sample selection and melodic material — an immediate
     *  repeat from ``.random`` reads as a bug to the ear, and over a range of
     *  12 it happens roughly one bang in twelve.
     *
     *  Two inlets and two outlets, in Max's shape. A bang on inlet 0 emits the
     *  next undrawn value on outlet 0. Once every value has been handed out the
     *  urn is *empty*: further bangs emit nothing on outlet 0 and bang outlet 1
     *  instead, so a patch can hear the cycle end and decide what to do — refill
     *  with ``clear``, stop, or move on to the next material.
     *
     *  ### How the sequence is built
     *
     *  The bag holds a **whole shuffled permutation**, built by Fisher-Yates at
     *  refill time, and a bang is a cursor step through it. The obvious
     *  alternative — draw at random and retry when the value was already taken —
     *  is what makes ``.urn`` implementations unusable in a real-time context:
     *  with one value left in a range of 4096 the expected number of retries is
     *  4096, and the worst case is unbounded. Here every bang costs one atomic
     *  compare-exchange and one array read, whatever is left in the urn.
     *
     *  The permutation is immutable between refills, and the cursor moves by
     *  compare-exchange, so two threads banging the same object concurrently
     *  still get two *different* values — the no-repetition guarantee survives
     *  contention rather than merely being unlikely to break under it.
     *
     *  ### Capacity
     *
     *  ``limit`` is clamped into ``[1, CAPACITY]``, and ``CAPACITY`` is 4096 —
     *  Max's documented maximum, adopted here because it lets the bag be a
     *  fixed member array. Nothing on any path allocates, takes a lock or
     *  blocks; the two O(limit) paths (a refill, a reseed) are bounded by that
     *  capacity and touch only memory the object already owns, so they are safe
     *  from the audio thread as well.
     *
     *  ### Messages
     *
     *  - ``clear`` on inlet 0 — refills the urn: every value is undrawn again,
     *    reshuffled, so the next cycle is a different order rather than a replay.
     *  - ``seed <n>`` on inlet 0 — restarts the random sequence and reshuffles
     *    the part of the bag that has *not* been drawn yet. Already-drawn values
     *    stay drawn, matching Max, where ``seed`` does not empty the urn.
     *  - int / float on inlet 1 — sets the limit **and** refills, as in Max: a
     *    range change invalidates the drawn set, so keeping it would let a value
     *    inside the new range never appear.
     *
     *  ### Seeding
     *
     *  The second creation parameter seeds the object. A non-zero seed makes the
     *  order reproducible — the same patch replays the same cycle on every run,
     *  which is what turns a generative patch into a composition. Seed 0 (the
     *  default) takes an arbitrary stream from the engine generator, matching
     *  ``.drunk`` and ``.random``. State lives per object (see RandomSource), so
     *  two ``.urn`` objects never consume each other's draws.
     *
     *  Note that a refill *continues* the stream rather than restarting it, so
     *  successive cycles of a seeded ``.urn`` are different orders of the same
     *  values — Max needs an explicit ``seed 0`` after each ``clear`` to avoid
     *  repeating the same cycle forever.
     */
    PATCHER_CLASS(gUrn, YSE::OBJ::G_URN)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(Bang)
    _INT_IN(SetInt)
    _FLOAT_IN(SetFloat)
    _LIST_IN(SetList)

    _PARM_PARSE

    _HAS_GUI

    /** @brief Largest honoured limit, and the fixed size of the bag. */
    static constexpr int CAPACITY = 4096;

  private:
    // Clamps `value` into [1, CAPACITY], stores it, and refills.
    void SetLimit(int value);
    // Rewrites the bag as a fresh shuffled permutation of [0, limit) and marks
    // every value undrawn. O(limit), no allocation.
    void Refill();
    // Fisher-Yates over bag[first, limit), leaving bag[0, first) alone — the
    // already-drawn prefix. O(limit - first), no allocation.
    void ShuffleFrom(int first);

    // Exclusive upper bound of the output; always inside [1, CAPACITY].
    aInt limit{1};
    // Creation seed. 0 means "pick a stream for me" — see the class docs.
    aInt seed{0};
    // How many values have been handed out since the last refill, and equally
    // the read cursor into `bag`. The urn is empty once it reaches `limit`.
    aInt drawn{0};
    // Last value emitted, for GetGuiValue().
    aInt lastValue{0};

    // The shuffled permutation. bag[0, drawn) is what has already come out,
    // bag[drawn, limit) is what has not. Written only by Refill()/ShuffleFrom().
    std::array<std::uint16_t, CAPACITY> bag{};

    RandomSource rng;
  };
}
}

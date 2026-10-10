#pragma once
#include "../pObject.h"
#include "gRandomSource.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Random 0 or 1 on every bang — ``.decide`` (issue #455).
     *
     *  The cheapest branch there is: a fair coin flip, emitted as an int. It is
     *  ``.random`` with a range of 2, given a name because that is how it is
     *  actually used — gating a drum-machine fill, choosing between two
     *  destinations through ``.gate`` or ``.if``, deciding whether a note
     *  happens at all. A patch full of ``.random 2`` boxes reads as arithmetic;
     *  a patch full of ``.decide`` boxes reads as intent.
     *
     *  Two inlets, in Max's shape. Inlet 0 is the hot one: a bang flips the
     *  coin, and an int or a float does the same thing (Max: "In left inlet:
     *  same as bang"), so anything already sending a value can drive it without
     *  a ``.b`` in between. Inlet 1 takes a seed, which is Max's right inlet.
     *
     *  ### Fairness
     *
     *  The draw is ``RandomSource::Bounded(2)``. Bounded's multiply-shift
     *  reduction carries a residual bias below ``bound / 2^32`` in general, but
     *  2 divides 2^32 exactly, so at this bound it is not merely small — it is
     *  zero. The two outcomes are equiprobable, and the bit taken is the top
     *  bit of an avalanched SplitMix64 word rather than a low bit of a counter,
     *  so successive flips are uncorrelated as well as individually fair. Both
     *  properties are pinned by the tests: an even split alone would also be
     *  satisfied by a strict 0,1,0,1 alternation.
     *
     *  ### Seeding
     *
     *  The single creation parameter seeds the object. A non-zero seed makes
     *  the whole sequence of flips reproducible — the same patch replays the
     *  same coin tosses on every run, which is what turns a generative patch
     *  into a composition rather than a one-off. Seed 0 (the default) takes an
     *  arbitrary stream from the engine generator, matching Max's documented
     *  "no argument means unpredictable", ``.drunk``, ``.urn`` and ``.random``.
     *
     *  Two ways to reseed at runtime, and they are equivalent: an int on inlet
     *  1 (Max's right inlet) or the list message ``seed <n>`` on inlet 0 (the
     *  convention the rest of this family already uses). Both restart the
     *  sequence from its beginning, so ``seed 42`` twice replays the same
     *  flips. Neither is written back to the creation parameter, so a
     *  ``DumpJSON`` keeps the seed the patch was saved with rather than one
     *  sent live.
     *
     *  State lives per object (see RandomSource), not in the engine's shared
     *  ``thread_local`` stream, so two ``.decide`` objects never consume each
     *  other's draws and each stays reproducible on its own.
     *
     *  Exactly one draw is taken per flip and nothing else in the object draws,
     *  which is what makes a seeded sequence replayable. Every path is a
     *  handful of integer operations over atomics: no allocation, no lock, no
     *  I/O, so a bang arriving on the audio thread is as safe as one from the
     *  GUI. ``Calculate()`` does nothing at all — the object is driven by its
     *  inlets, not by the DSP tick.
     */
    PATCHER_CLASS(gDecide, YSE::OBJ::G_DECIDE)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(Bang)
    _INT_IN(SetInt)
    _FLOAT_IN(SetFloat)
    _LIST_IN(SetList)

    _PARM_PARSE

    _HAS_GUI

  private:
    // Draws one coin flip, stores it for the GUI and emits it. Shared by the
    // bang, int and float paths on inlet 0.
    void Flip(YSE::THREAD thread);

    // Creation seed. 0 means "pick a stream for me" — see the class docs.
    aInt seed{0};
    // Last value emitted, for GetGuiValue().
    aInt lastValue{0};

    RandomSource rng;
  };
}
}

#pragma once

#include <atomic>
#include "../pObject.h"
#include "gRandomSource.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Uniform random integers ``.random``.
     *
     *  Two creation parameters, in Max's order: ``range`` and ``seed`` (#1236).
     *
     *  ### Seeding
     *
     *  Seed 0, the default, keeps the object on the engine-wide generator
     *  (``YSE::Random``), exactly as before #1236: its numbers follow
     *  ``YSE::RandomSeed()`` / ``YSE::Randomize()`` (#908), so a host that seeds
     *  the engine still replays every unseeded ``.random`` in a patch.
     *
     *  A non-zero seed moves the object onto its own RandomSource: the same
     *  seed replays the same numbers on every run, and no other draw on the
     *  thread can shift them. ``seed <n>`` on inlet 0 does the same at runtime
     *  (``seed 0`` returns to the engine generator); it is a live override, so a
     *  saved patch keeps the seed it was created with.
     *
     *  Exactly one draw per bang. Every path is a few atomic loads and stores
     *  and integer arithmetic: no allocation, no lock, no I/O.
     */
    PATCHER_CLASS(gRandom, YSE::OBJ::G_RANDOM)
    _NO_MESSAGES
    _NO_CALCULATE

    _INT_IN(SetIntRange)
    _FLOAT_IN(SetFloatRange)
    _BANG_IN(Bang)
    _LIST_IN(SetList)

    _PARM_PARSE

  private:
    // Selects the stream: a non-zero seed seeds the object's own source, 0
    // hands the draws back to the engine generator.
    void Reseed(int value);

    aInt range;
    // Creation seed. 0 means "use the engine generator" — see the class docs.
    aInt seed{0};
    // Whether draws come from `rng` rather than the engine generator.
    std::atomic<bool> ownStream{false};

    RandomSource rng;
  };
}
}

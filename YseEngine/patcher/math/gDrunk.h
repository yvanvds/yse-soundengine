#pragma once
#include "../pObject.h"
#include "gRandomSource.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Bounded random walk ``.drunk`` (issue #453).
     *
     *  Where ``.random`` throws a fresh number away from the last one every
     *  time, ``.drunk`` *walks*: each bang moves the stored value by a small
     *  random step and emits the new position, clipped into the range. That is
     *  the difference between noise and drift, and it is what makes the object
     *  musically usable for pitch, filter cutoff and spatial position — nearby
     *  in time means nearby in value.
     *
     *  Three inlets, in Max's order. Inlet 0 is the hot one: a bang takes a
     *  step, an int or float sets the position directly (and emits it). Inlets
     *  1 and 2 store the range and the step size.
     *
     *  ### Range
     *
     *  The output lives in ``[0, range)``, so the emitted value is between 0
     *  and ``range - 1`` inclusive — the same half-open convention ``.random``
     *  uses, and the reason both objects can drive the same lookup table. A
     *  range of 1 or less collapses the walk onto 0, which is the only value
     *  such a range contains.
     *
     *  ### Step size
     *
     *  ``stepSize`` is an *exclusive magnitude limit*, Max's reading: the step
     *  actually taken always satisfies ``|step| < |stepSize|``. So the default
     *  of 2 draws uniformly from ``{-1, 0, +1}``, a limit of 4 from ``-3..+3``,
     *  and a limit of 1 (or 0) cannot move at all. A **negative** step size
     *  means the same magnitudes with **zero excluded** — ``-2`` draws from
     *  ``{-1, +1}`` — so the walk is guaranteed to move on every bang.
     *
     *  Exactly one random draw is taken per bang, whatever the step size. That
     *  is what makes a seeded walk replayable.
     *
     *  ### Boundaries
     *
     *  The walk **clips** at the range boundaries, as Max's does: a step that
     *  would leave the range lands on the boundary instead of being reflected.
     *  A walk that reaches an edge therefore lingers there — that is the
     *  object's character, not a defect. Feed the output through ``.pong`` when
     *  a reflecting or wrapping boundary is wanted instead.
     *
     *  ### Seeding
     *
     *  The third creation parameter seeds the walk. A non-zero seed makes the
     *  whole sequence reproducible — the same patch replays the same walk on
     *  every run, which is what makes a generative patch a *composition*
     *  rather than a one-off. Seed 0 (the default) takes an arbitrary stream
     *  from the engine generator, matching Max's ``@seed 0`` and ``.random``'s
     *  behaviour. ``seed <n>`` on inlet 0 restarts the sequence at runtime.
     *
     *  The state is per object (see RandomSource), not the engine's shared
     *  ``thread_local`` stream, so two ``.drunk`` objects never consume each
     *  other's draws and each stays reproducible on its own.
     *
     *  Every path is a handful of integer operations over atomics: no
     *  allocation, no lock, no I/O, so a bang arriving on the audio thread is
     *  as safe as one from the GUI.
     */
    PATCHER_CLASS(gDrunk, YSE::OBJ::G_DRUNK)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(Bang)
    _INT_IN(SetInt)
    _FLOAT_IN(SetFloat)
    _LIST_IN(SetList)

    _PARM_PARSE

    _HAS_GUI

  private:
    // Stores an inlet value on the right field; inlet 0 additionally emits.
    void Store(int value, int inlet, YSE::THREAD thread);
    // Moves the walk to `value` clipped into the range, and returns where it
    // landed. Shared by the int/float inlet and the `set` message.
    int MoveTo(int value);
    // Draws the step for one bang: uniform over the magnitudes `limit`
    // permits, zero included unless `limit` is negative. Exactly one draw.
    int DrawStep(int limit);
    // Clips `value` (widened, so `current + step` cannot overflow) into
    // [0, range - 1].
    int ClipToRange(I64 value) const;

    // Exclusive upper bound of the output; the walk lives in [0, range).
    aInt range{128};
    // Exclusive magnitude limit of one step; negative forbids a zero step.
    aInt stepSize{2};
    // Creation seed. 0 means "pick a stream for me" — see the class docs.
    aInt seed{0};
    // Where the walk currently stands. Always inside the range.
    aInt currentValue{0};

    RandomSource rng;
  };
}
}

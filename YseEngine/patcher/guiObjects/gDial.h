#pragma once
#include "../pObject.h"
#include <atomic>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Ranged rotary control ``.dial`` (issue #552).
     *
     *  Max's ``dial`` — "output numbers in a settable range", rendered by a
     *  host as a knob. Where ``.slider`` stores a normalised float in [0, 1]
     *  and needs a ``.scale`` behind it before it means anything, ``.dial``
     *  carries the range with it: minimum, maximum and curve are the object's
     *  own parameters, so a patch can expose "cutoff, 20-20000 Hz,
     *  exponential" as a single object.
     *
     *  **The contract, and why it is this way round.** The one inlet takes the
     *  *knob position*, a float in [0, 1] clamped on the way in — exactly what
     *  ``.slider`` takes, so a host that already drives a slider drives a dial
     *  with the same code and ``GetGuiValue()`` keeps meaning what it means for
     *  ``.slider``: where the control sits. The outlet carries the position
     *  *mapped onto the range*, which is what makes the object slider-plus-scale
     *  in one. With the default parameters (0, 1, linear, float) the mapping is
     *  the identity, so a bare ``.dial`` behaves exactly like a bare
     *  ``.slider``.
     *
     *  Note that the GUI value is therefore the position and not the mapped
     *  value: a host that wants to *display* "440 Hz" needs the range as well
     *  as the position, and the single-string GUI protocol has room for one
     *  scalar. That is issue #551's subject, not this object's.
     *
     *  The mapping is ``MapRange`` — the same code ``.scale`` and ``.zmap``
     *  run, including its sign-symmetric exponent, its degenerate-range answer
     *  (minimum equal to maximum emits the minimum) and its non-finite
     *  substitution. Clamping is on: the position is already inside [0, 1], so
     *  the clamp only ever catches a mapping a hostile parameter set bent out
     *  of the range.
     *
     *  ``intMode`` rounds the mapped value to the nearest whole number and
     *  sends it as an int rather than a float, which is why the outlet is
     *  declared ``ANY`` — the type depends on a creation argument, and an
     *  outlet's declared type is fixed when the object is built. Halfway values
     *  round away from zero, ``.round``'s convention.
     *
     *  All four parameters are scalars and the object registers no
     *  clear/parse callbacks, so ``Parameters::NeedsRebuild()`` is false and a
     *  live re-range rides the wait-free scalar plan (issue #234) instead of
     *  replacing the object.
     *
     *  Calculate() is one atomic load, one MapRange (a divide and at most one
     *  ``std::pow``), at most one ``std::round`` and one Send: no allocation,
     *  no lock, no I/O.
     *
     *  The host-thread/audio-thread window on the stored position is
     *  ``.slider``'s: the position is a plain ``std::atomic<float>``, so a GUI
     *  write and an audio-thread read never tear, but nothing orders a write
     *  against the block that is already rendering. That is the contract every
     *  scalar GUI control in the patcher has (see the notes in
     *  Tests/patcher/test_patcher_object_races.cpp) and this object does not
     *  invent a different one.
     */
    PATCHER_CLASS(gDial, YSE::OBJ::G_DIAL)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)
    _BANG_IN(SetBang)
    _LIST_IN(SetList)

    // Settable since #846: inlet 0 takes the exact string GetGuiValue()
    // produces back as a list — the knob position — plus "set 0 <value>" for
    // the one cell, which is what lets `.preset` capture and restore the
    // dial. Both clamp exactly as a float does.
    _HAS_GUI_SETTABLE

  private:
    // Shared by the float and int inlet handlers: store clamped to [0, 1].
    void Store(float value);

    // Knob position in [0, 1]. Written by the host thread, read by the audio
    // thread — see the class comment.
    std::atomic<float> position;

    // Written by the parameter system (the scalar plan applies them on the
    // audio thread at the top of a block), read by Calculate. Plain fields,
    // exactly as .scale holds its mapping bounds.
    float minimum;
    float maximum;
    float exponent;
    // 0 = emit a float, non-zero = round and emit an int. Named for what
    // switching it on does, so the default (0) is the float output .slider
    // gives and turning it on is the opt-in.
    int intMode;
  };
}
}

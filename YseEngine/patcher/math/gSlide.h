#pragma once
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Logarithmic value smoothing ``.slide`` (issue #459).
     *
     *  A one-pole smoother on control values, and the standard treatment for
     *  anything a hand or a follower produced: MIDI controller data, an
     *  envelope follower's output, a value scrubbed by a slider. Each incoming
     *  number moves the stored value a *fraction* of the way towards itself
     *  rather than replacing it, so a stepped stream leaves the object as a
     *  curve and the parameter it drives never zippers.
     *
     *  Max's difference equation, exactly:
     *
     *  @code
     *    y[n] = y[n-1] + (x[n] - y[n-1]) / slide
     *  @endcode
     *
     *  which is the same shape the engine's DSP smoothers use — ``ladderFilter``,
     *  ``chorus``, ``feedbackDelay`` and ``compressor`` all run
     *  ``y += (x - y) * coef``. The difference is where the coefficient comes
     *  from, and it is why this object does not reuse them: those derive
     *  ``coef = 1 - exp(-1/tau)`` from a time constant *in seconds* against the
     *  sample rate, because they smooth once per sample. ``.slide`` is clocked
     *  by *events*, not by time — a value arrives when a value arrives — so its
     *  parameter counts steps and the coefficient is simply ``1/slide``. Ten
     *  incoming values is ten steps whether they took a millisecond or an hour.
     *  Reach for ``~line`` when the wanted shape is a linear ramp over a stated
     *  duration; reach for this when the wanted shape is exponential approach
     *  over a stated number of updates.
     *
     *  There is no audio-rate ``~slide`` in the patcher yet. When one is added
     *  it must use this same equation and these same defaults, since Max's
     *  ``slide~`` documents both identically to ``slide``.
     *
     *  ### Inlets
     *
     *  Three, in Max's order. Inlet 0 is hot: an int or float is smoothed and
     *  the result emitted, and a bang re-runs the filter on the *last* value
     *  received — which, since the stored value has moved meanwhile, walks the
     *  output another step towards it. Inlets 1 and 2 store the slide-up and
     *  slide-down amounts.
     *
     *  ### Asymmetry
     *
     *  The two amounts are independent, and that is the object's real value:
     *  ``slideUp`` applies when the incoming value is *above* the stored one and
     *  ``slideDown`` when it is below. A fast rise and a slow fall is what makes
     *  an envelope follower usable; the reverse smooths a control without
     *  making it feel sluggish to grab.
     *
     *  ### Slide amounts below 1 (defined here, not by Max)
     *
     *  Max documents that a slide of 1 makes the output equal the input and
     *  that 10 makes it change a tenth as fast, but says nothing about 0, a
     *  negative, or a fraction. Those are not merely undocumented, they are
     *  degenerate: 0 divides by zero, and anything in ``(0, 1)`` multiplies the
     *  error by more than one and makes the recursion overshoot and then
     *  oscillate — a "smoother" that rings. So **any amount below 1, and a NaN,
     *  is treated as 1**, the pass-through case. That keeps the object
     *  monotone: the output always moves towards the input and never past it.
     *
     *  ### Convergence — what "reached" means
     *
     *  The recursion approaches the target geometrically and, in exact
     *  arithmetic, never arrives. In floating point that is worse than a
     *  curiosity: once the step ``(x - y) / slide`` falls below half an ulp of
     *  ``y`` the addition rounds back to ``y`` and the value *stalls* a hair
     *  short of the target, forever. A control value that settles at 0.9999994
     *  instead of 1 is a bug waiting to be found by whatever compares against
     *  the endpoint.
     *
     *  So convergence is defined rather than left to rounding: the object
     *  snaps to the target once the remaining distance is within one part in a
     *  million of it (never coarser than 1e-6 absolute, so a target of 0 still
     *  terminates). "Reached" therefore means *within 1e-6 relative* — about
     *  eight ulps of the float the outlet carries — and once reached the output
     *  equals the input exactly, so repeating the same input is idempotent.
     *
     *  The running value is kept in ``double`` for the same reason. In float it
     *  would stall before that tolerance for any slide amount above roughly 33,
     *  which covers most useful settings; in double the stall point sits below
     *  the tolerance for every amount up to about 1e9.
     *
     *  ### Messages
     *
     *  ``set <n>`` stages @em n as the last input without emitting, so a
     *  following bang is exactly the float that was not sent — Max's wording is
     *  "set the current input value ... without causing output (bang can be
     *  used to cause successive output)". ``reset`` returns the *running* value
     *  to 0, also without emitting, which is the other half of the pair: one
     *  message loads what the filter slides towards, the other loads where it
     *  slides from.
     *
     *  The filter step therefore lives in Slide() and the object declares no
     *  Calculate(). A hot inlet fires CalculateIfReady() after *every* message
     *  type it accepts, lists included, so a Calculate() that emitted would
     *  make ``set`` and ``reset`` emit too — the one thing Max documents they
     *  must not do. Slide() is two compares, a subtract, a divide and one Send:
     *  no allocation, no lock, no I/O, so it is safe on whichever thread the
     *  message arrived on.
     *
     *  The slide amounts are plain scalars and the object registers no parse
     *  callback, so a live ``SetParams`` takes the in-place scalar route (issue
     *  #234) and the running value survives the edit — a re-parse must not
     *  audibly restart the smoother.
     */
    PATCHER_CLASS(gSlide, YSE::OBJ::G_SLIDE)
    _NO_MESSAGES
    _NO_CALCULATE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)
    _BANG_IN(Bang)
    _LIST_IN(SetList)

    _HAS_GUI

  private:
    // Shared by both numeric inlet handlers: inlet 0 stores the value and
    // fires, 1 and 2 only store. Keeping the dispatch in one place means the
    // int path is exactly the float path with a widening cast.
    void Store(float value, int inlet, YSE::THREAD thread);

    // One filter step against the stored input, and the Send that follows it.
    void Slide(YSE::THREAD thread);

    // The slide amount actually used: the requested one, floored at 1, with a
    // NaN also reading as 1. See the class docs.
    static double Effective(float slide);

    // Last value received on inlet 0 — what a bang re-runs the filter on.
    float input;

    // The two creation parameters, in Max's order.
    float slideUp;
    float slideDown;

    // The running (smoothed) value. Double on purpose — see "Convergence".
    double current;
  };
}
}

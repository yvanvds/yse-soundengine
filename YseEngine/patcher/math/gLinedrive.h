#pragma once
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Exponential scaling for control values ``.linedrive``
     *         (issue #447).
     *
     *  Maps a linear controller — a MIDI CC, a slider, a fader — onto an
     *  exponential response curve, so that the perceived change is even across
     *  the range instead of bunching up at one end. The standard treatment for
     *  anything driving frequency or amplitude from a linear source.
     *
     *  The formula is Max's, written there as
     *
     *  \code
     *    y = b * e^(-a * log c) * e^(x * log c)
     *  \endcode
     *
     *  with *a* the input maximum, *b* the output maximum and *c* the curve.
     *  That collapses to the equivalent and more accurate
     *
     *  \code
     *    out = outputMax * pow(curve, input - inputMax)
     *  \endcode
     *
     *  so the input maximum is the *anchor*: feed it in and the output is
     *  exactly ``outputMax``, whatever the curve. Below it the output falls
     *  away geometrically — one step of input always multiplies the output by
     *  ``curve``, which is what makes the response feel even.
     *
     *  Four inlets: inlet 0 is the hot one and carries the value, inlets 1-3
     *  store the input maximum, the output maximum and the curve, in Max's
     *  typed-in argument order. Float in, float out — an int on any inlet is
     *  widened, the convention the ``.+`` family already uses. Defaults are
     *  ``.scale``'s MIDI range mapped onto 0-1, with the 1.06 curve Max's own
     *  documentation recommends for a 0-127 controller.
     *
     *  Three behaviours are choices rather than ports of Max:
     *
     *  - Max's right inlet carries a ramp time and its outlet emits a
     *    ``[value, ramp]`` list for a ``line`` object to consume. ``.linedrive``
     *    emits the scaled value alone: the ramp belongs to whatever is being
     *    driven, and ``~line`` is reached directly.
     *  - Max requires a curve above 1. Any positive curve is accepted here: a
     *    curve below 1 simply mirrors the response (front-loaded rather than
     *    back-loaded) and 1 flattens it to the constant ``outputMax``. Only a
     *    curve at or below 0 is rejected, because a negative base has no real
     *    power for most exponents.
     *  - Anything that would not be finite — an overflow far above the input
     *    maximum, a non-positive curve, a NaN arriving from a neighbour —
     *    emits 0, the convention ``./``, ``.sqrt``, ``.scale`` and ``.zmap``
     *    already use, and the safest possible value for the amplitude case.
     *
     *  Calculate() is a subtract, one ``std::pow``, a multiply and a couple of
     *  branches: no allocation, no lock, no I/O.
     */
    PATCHER_CLASS(gLinedrive, YSE::OBJ::G_LINEDRIVE)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)

  private:
    // Shared by both inlet handlers: inlet 0 stores the value, 1-3 the shape of
    // the curve. Keeping the dispatch in one place means the int path is
    // exactly the float path with a widening cast.
    void Store(float value, int inlet);

    float input;
    float inputMax;
    float outputMax;
    float curve;
  };
}
}

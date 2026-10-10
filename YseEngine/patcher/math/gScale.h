#pragma once
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Control-rate range mapping ``.scale`` (issue #443).
     *
     *  Maps a value from an input range onto an output range, optionally
     *  bending the mapping with an exponential curve. Six inlets, in Max's
     *  order: inlet 0 is the hot one and carries the value, inlets 1-5 store
     *  the input low/high, the output low/high and the exponent. Float in,
     *  float out — an int on any inlet is widened, the convention the ``.+``
     *  family already uses.
     *
     *  The formula is Max's, including its sign-symmetric treatment of the
     *  exponent, so a value below the input range curves the same way one
     *  above it does:
     *
     *  \code
     *    n = (in - inLow) / (inHigh - inLow)
     *    out = outLow + (outHigh - outLow) * (n >= 0 ?  pow( n, exponent)
     *                                                : -pow(-n, exponent))
     *  \endcode
     *
     *  Like Max's default (``@classic 1``) the mapping **extrapolates**: an
     *  input outside the input range produces an output outside the output
     *  range. Set the ``clip`` parameter to 1 for the clamped behaviour Max
     *  gives with ``@classic 0`` — that is the difference between ``.scale``
     *  and Max's ``zmap``, which always clips.
     *
     *  Calculate() is a divide, at most one ``std::pow``, a couple of branches
     *  and one Send: no allocation, no lock, no I/O.
     */
    PATCHER_CLASS(gScale, YSE::OBJ::G_SCALE)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)

  private:
    // Shared by both inlet handlers: inlet 0 stores the value, 1-5 the
    // mapping bounds. Keeping the dispatch in one place means the int path
    // is exactly the float path with a widening cast.
    void Store(float value, int inlet);

    float input;
    float inLow;
    float inHigh;
    float outLow;
    float outHigh;
    float exponent;
    // 0 = extrapolate (Max's default), non-zero = clamp to the output range.
    // Parameter only, no inlet — it mirrors a Max attribute, not a signal.
    int clip;
  };
}
}

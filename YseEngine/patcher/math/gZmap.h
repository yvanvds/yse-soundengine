#pragma once
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Control-rate clipped range mapping ``.zmap`` (issue #444).
     *
     *  The always-clipping sibling of ``.scale``: maps a value from an input
     *  range onto an output range and pins anything outside the input range to
     *  the corresponding output limit instead of extrapolating. The safe choice
     *  when driving a parameter that must never leave its legal range.
     *
     *  Five inlets, in Max's order: inlet 0 is the hot one and carries the
     *  value, inlets 1-4 store the input low/high and the output low/high.
     *  Float in, float out — an int on any inlet is widened, the convention the
     *  ``.+`` family already uses. There is no exponent and no ``clip``
     *  parameter: the mapping is always linear and always clipped, which is
     *  exactly what distinguishes ``.zmap`` from ``.scale``.
     *
     *  The mapping itself is MapRange() in gRangeMap.h, shared with ``.scale``,
     *  so the two objects agree on the degenerate input range, on the mirrored
     *  clamp for a descending output range and on never letting a non-finite
     *  value escape. ``.zmap`` additionally clamps the *incoming* value to the
     *  input range first, so an infinity on the hot inlet lands on the matching
     *  output limit rather than on the substituted 0.
     *
     *  Calculate() is a couple of compares, a divide and one Send: no
     *  allocation, no lock, no I/O.
     */
    PATCHER_CLASS(gZmap, YSE::OBJ::G_ZMAP)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)

  private:
    // Shared by both inlet handlers: inlet 0 stores the value, 1-4 the mapping
    // bounds. Keeping the dispatch in one place means the int path is exactly
    // the float path with a widening cast.
    void Store(float value, int inlet);

    float input;
    float inLow;
    float inHigh;
    float outLow;
    float outHigh;
  };
}
}

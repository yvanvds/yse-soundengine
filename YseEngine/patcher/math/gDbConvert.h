#pragma once
#include "gUnaryMath.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief The amplitude <-> decibel conversions ``.atodb`` and ``.dbtoa``
     *         (issue #442).
     *
     *  Both are one ``dsp/math_functions.h`` call behind the shape
     *  gUnaryMathBase already provides for ``.abs`` / ``.sqrt``: one inlet, one
     *  outlet, no parameters, float in / float out.
     *
     *  They deliberately call the engine's own YSE::DSP::rmsToDb /
     *  YSE::DSP::dbToRms rather than a fresh ``20 * log10`` — the patcher must
     *  agree with the ``dbToRms`` / ``rmsToDb`` DSP modules and with every
     *  volume the engine already reports. That means the engine's decibel
     *  reference, not Max's: **amplitude 1.0 is 100 dB**, amplitude 0 is 0 dB,
     *  and the scale never goes negative. Subtract 100 to read the result as
     *  conventional dBFS.
     */
    UNARY_MATH_CLASS(gAToDb, YSE::OBJ::G_ATODB)
    UNARY_MATH_CLASS(gDbToA, YSE::OBJ::G_DBTOA)

  } // namespace PATCHER
} // namespace YSE

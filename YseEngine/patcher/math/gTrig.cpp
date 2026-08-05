#include "gTrig.h"
#include "../pObjectList.hpp"
#include <cmath>

using namespace YSE::PATCHER;

namespace {

  // Every one of these functions has an input a patch can reach that has no
  // finite answer: asin/acos outside [-1, 1], acosh below 1, atanh at or beyond
  // ±1, sinh/cosh past the float overflow point, and any of them fed a value
  // that is already infinite or a NaN. Emitting 0 instead keeps the convention
  // ./ and .sqrt established — a NaN escaping into the patch would poison every
  // object downstream of it. One branch on a register: no allocation, no lock,
  // no I/O.
  inline float finiteOr0(float value) {
    return std::isfinite(value) ? value : 0.f;
  }

  // Shared wording for the outputs that are an angle, so the twelve
  // constructors below stay one line of intent each.
  const char* const RADIANS = "any float (radians)";

} // namespace

// ─── circular functions ───────────────────────────────────────────────────────
// Each captureless lambda decays to the plain float(*)(float) gUnaryMathBase
// stores, so Calculate() stays a single indirect call.

gSin::gSin() : gUnaryMathBase([](float value) { return finiteOr0(std::sin(value)); }) {
  Document("Control-rate sine. Emits sin(in) as a float whenever inlet 0 fires. The input is an "
           "angle in radians, as in Max.",
           "Angle in radians — fires the evaluation.", RADIANS, "The sine of the input.",
           "-1.0 to 1.0");
}

gCos::gCos() : gUnaryMathBase([](float value) { return finiteOr0(std::cos(value)); }) {
  Document("Control-rate cosine. Emits cos(in) as a float whenever inlet 0 fires. The input is an "
           "angle in radians, as in Max.",
           "Angle in radians — fires the evaluation.", RADIANS, "The cosine of the input.",
           "-1.0 to 1.0");
}

gTan::gTan() : gUnaryMathBase([](float value) { return finiteOr0(std::tan(value)); }) {
  Document("Control-rate tangent. Emits tan(in) as a float whenever inlet 0 fires. The input is an "
           "angle in radians, as in Max. Near an odd multiple of pi/2 the result grows without "
           "bound; if it stops being finite, 0 is emitted rather than an infinity.",
           "Angle in radians — fires the evaluation.", RADIANS, "The tangent of the input.",
           "any float");
}

gAsin::gAsin() : gUnaryMathBase([](float value) { return finiteOr0(std::asin(value)); }) {
  Document("Control-rate arc-sine. Emits asin(in) in radians as a float whenever inlet 0 fires. "
           "Inputs outside -1 to 1 have no real arc-sine and emit 0 rather than a NaN.",
           "Value to take the arc-sine of — fires the evaluation. Values outside -1 to 1 force the "
           "output to 0.",
           "-1.0 to 1.0", "The arc-sine of the input, in radians.", "-pi/2 to pi/2");
}

gAcos::gAcos() : gUnaryMathBase([](float value) { return finiteOr0(std::acos(value)); }) {
  Document("Control-rate arc-cosine. Emits acos(in) in radians as a float whenever inlet 0 fires. "
           "Inputs outside -1 to 1 have no real arc-cosine and emit 0 rather than a NaN.",
           "Value to take the arc-cosine of — fires the evaluation. Values outside -1 to 1 force "
           "the output to 0.",
           "-1.0 to 1.0", "The arc-cosine of the input, in radians.", "0 to pi");
}

gAtan::gAtan() : gUnaryMathBase([](float value) { return finiteOr0(std::atan(value)); }) {
  Document("Control-rate arc-tangent. Emits atan(in) in radians as a float whenever inlet 0 fires. "
           "Use .atan2 when the quadrant of the angle matters.",
           "Value to take the arc-tangent of — fires the evaluation.", "any float",
           "The arc-tangent of the input, in radians.", "-pi/2 to pi/2");
}

// ─── hyperbolic functions ─────────────────────────────────────────────────────

gSinh::gSinh() : gUnaryMathBase([](float value) { return finiteOr0(std::sinh(value)); }) {
  Document("Control-rate hyperbolic sine. Emits sinh(in) as a float whenever inlet 0 fires. The "
           "result overflows for inputs beyond roughly ±89; 0 is emitted there rather than an "
           "infinity.",
           "Value to take the hyperbolic sine of — fires the evaluation.", "any float",
           "The hyperbolic sine of the input.", "any float");
}

gCosh::gCosh() : gUnaryMathBase([](float value) { return finiteOr0(std::cosh(value)); }) {
  Document("Control-rate hyperbolic cosine. Emits cosh(in) as a float whenever inlet 0 fires. The "
           "result overflows for inputs beyond roughly ±89; 0 is emitted there rather than an "
           "infinity.",
           "Value to take the hyperbolic cosine of — fires the evaluation.", "any float",
           "The hyperbolic cosine of the input.", "1.0 or higher");
}

gTanh::gTanh() : gUnaryMathBase([](float value) { return finiteOr0(std::tanh(value)); }) {
  Document("Control-rate hyperbolic tangent. Emits tanh(in) as a float whenever inlet 0 fires. The "
           "output saturates smoothly towards -1 and 1, which is what makes it the usual "
           "waveshaping curve.",
           "Value to take the hyperbolic tangent of — fires the evaluation.", "any float",
           "The hyperbolic tangent of the input.", "-1.0 to 1.0");
}

gAsinh::gAsinh() : gUnaryMathBase([](float value) { return finiteOr0(std::asinh(value)); }) {
  Document("Control-rate hyperbolic arc-sine. Emits asinh(in) as a float whenever inlet 0 fires. "
           "Defined for every input.",
           "Value to take the hyperbolic arc-sine of — fires the evaluation.", "any float",
           "The hyperbolic arc-sine of the input.", "any float");
}

gAcosh::gAcosh() : gUnaryMathBase([](float value) { return finiteOr0(std::acosh(value)); }) {
  Document(
      "Control-rate hyperbolic arc-cosine. Emits acosh(in) as a float whenever inlet 0 fires. "
      "Inputs below 1 have no real hyperbolic arc-cosine and emit 0 rather than a NaN.",
      "Value to take the hyperbolic arc-cosine of — fires the evaluation. Values below 1 force "
      "the output to 0.",
      "1.0 or higher", "The hyperbolic arc-cosine of the input.", "0 or higher");
}

gAtanh::gAtanh() : gUnaryMathBase([](float value) { return finiteOr0(std::atanh(value)); }) {
  Document("Control-rate hyperbolic arc-tangent. Emits atanh(in) as a float whenever inlet 0 "
           "fires. The result is unbounded at ±1 and undefined beyond them, so inputs at or "
           "outside -1 to 1 emit 0 rather than an infinity or a NaN.",
           "Value to take the hyperbolic arc-tangent of — fires the evaluation. Values at or "
           "outside -1 to 1 force the output to 0.",
           "greater than -1.0 and less than 1.0", "The hyperbolic arc-tangent of the input.",
           "any float");
}

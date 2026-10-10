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
  Document("Sends the sine of an angle in radians, as a float.",
           "Angle in radians; sends the result.", RADIANS, "The sine of the input.", "-1.0 to 1.0");
}

gCos::gCos() : gUnaryMathBase([](float value) { return finiteOr0(std::cos(value)); }) {
  Document("Sends the cosine of an angle in radians, as a float.",
           "Angle in radians; sends the result.", RADIANS, "The cosine of the input.",
           "-1.0 to 1.0");
}

gTan::gTan() : gUnaryMathBase([](float value) { return finiteOr0(std::tan(value)); }) {
  Document("Sends the tangent of an angle in radians, as a float. Where the result would be "
           "infinite, it sends 0.",
           "Angle in radians; sends the result.", RADIANS, "The tangent of the input.",
           "any float");
}

gAsin::gAsin() : gUnaryMathBase([](float value) { return finiteOr0(std::asin(value)); }) {
  Document("Sends the arc-sine of a number, in radians. Numbers outside -1 to 1 send 0.",
           "Number; sends the result. Numbers outside -1 to 1 send 0.", "-1.0 to 1.0",
           "The arc-sine of the input, in radians.", "-pi/2 to pi/2");
}

gAcos::gAcos() : gUnaryMathBase([](float value) { return finiteOr0(std::acos(value)); }) {
  Document("Sends the arc-cosine of a number, in radians. Numbers outside -1 to 1 send 0.",
           "Number; sends the result. Numbers outside -1 to 1 send 0.", "-1.0 to 1.0",
           "The arc-cosine of the input, in radians.", "0 to pi");
}

gAtan::gAtan() : gUnaryMathBase([](float value) { return finiteOr0(std::atan(value)); }) {
  Document("Sends the arc-tangent of a number, in radians. Use .atan2 for an angle around the "
           "full circle.",
           "Number; sends the result.", "any float", "The arc-tangent of the input, in radians.",
           "-pi/2 to pi/2");
}

// ─── hyperbolic functions ─────────────────────────────────────────────────────

gSinh::gSinh() : gUnaryMathBase([](float value) { return finiteOr0(std::sinh(value)); }) {
  Document("Sends the hyperbolic sine of a number. Beyond about -89 and 89 the result is too large "
           "for a float, and 0 is sent.",
           "Number; sends the result.", "any float", "The hyperbolic sine of the input.",
           "any float");
}

gCosh::gCosh() : gUnaryMathBase([](float value) { return finiteOr0(std::cosh(value)); }) {
  Document("Sends the hyperbolic cosine of a number. Beyond about -89 and 89 the result is too "
           "large for a float, and 0 is sent.",
           "Number; sends the result.", "any float", "The hyperbolic cosine of the input.",
           "1.0 or higher");
}

gTanh::gTanh() : gUnaryMathBase([](float value) { return finiteOr0(std::tanh(value)); }) {
  Document("Sends the hyperbolic tangent of a number: a smooth S-curve between -1 and 1, often "
           "used for soft saturation.",
           "Number; sends the result.", "any float", "The hyperbolic tangent of the input.",
           "-1.0 to 1.0");
}

gAsinh::gAsinh() : gUnaryMathBase([](float value) { return finiteOr0(std::asinh(value)); }) {
  Document("Sends the hyperbolic arc-sine of a number.", "Number; sends the result.", "any float",
           "The hyperbolic arc-sine of the input.", "any float");
}

gAcosh::gAcosh() : gUnaryMathBase([](float value) { return finiteOr0(std::acosh(value)); }) {
  Document("Sends the hyperbolic arc-cosine of a number. Numbers below 1 send 0.",
           "Number; sends the result. Numbers below 1 send 0.", "1.0 or higher",
           "The hyperbolic arc-cosine of the input.", "0 or higher");
}

gAtanh::gAtanh() : gUnaryMathBase([](float value) { return finiteOr0(std::atanh(value)); }) {
  Document("Sends the hyperbolic arc-tangent of a number. Numbers at or beyond -1 and 1 send 0.",
           "Number; sends the result. Numbers at or beyond -1 and 1 send 0.",
           "greater than -1.0 and less than 1.0", "The hyperbolic arc-tangent of the input.",
           "any float");
}

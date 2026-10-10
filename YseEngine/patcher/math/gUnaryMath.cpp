#include "gUnaryMath.h"
#include "../pObjectList.hpp"
#include <cmath>

using namespace YSE::PATCHER;

#define className gUnaryMathBase

gUnaryMathBase::gUnaryMathBase(operation op) : pObject(false), apply(op), input(0.f) {
  ADD_IN_0;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  ADD_OUT_FLOAT;

  ADD_CATEGORY(pCategory::MATH);
}

void gUnaryMathBase::Document(const char* summary, const char* inletDoc, const char* inletRange,
                              const char* outletDoc, const char* outletRange) {
  ADD_DESCRIPTION(summary);
  INLET_DOC(0, "in", inletDoc, inletRange);
  OUTLET_DOC(0, "out", outletDoc, outletRange);
}

FLOAT_IN(SetFloat) {
  input = value;
}

INT_IN(SetInt) {
  input = (float)value;
}

CALC() {
  outputs[0].SendFloat(apply(input), thread);
}

#undef className

// ─── the two functions ────────────────────────────────────────────────────────
// Each captureless lambda decays to a plain float(*)(float).

gAbs::gAbs() : gUnaryMathBase([](float value) { return std::fabs(value); }) {
  Document("Control-rate absolute value. Emits |in| as a float whenever inlet 0 fires.",
           "Value to take the absolute value of — fires the evaluation.", "any float",
           "The absolute value of the input.", "0 or higher");
}

// A negative operand has no real square root. Rather than let a NaN escape into
// the patch, emit 0 — the same "no non-finite value leaves a math object"
// convention ./ already uses for division by zero.
gSqrt::gSqrt() : gUnaryMathBase([](float value) { return value > 0.f ? std::sqrt(value) : 0.f; }) {
  Document("Control-rate square root. Emits the square root of the input as a float whenever "
           "inlet 0 fires. A negative input emits 0 rather than a NaN.",
           "Value to take the square root of — fires the evaluation. Negative values force the "
           "output to 0.",
           "any float", "The square root of the input (or 0 when the input is negative).",
           "0 or higher");
}

#include "dTrig.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

// ─── the one-inlet base ───────────────────────────────────────────────────────

#define className dTrigBase

dTrigBase::dTrigBase() : pObject(true) {
  floatIn = 0.f;

  // in 0: the input (signal, float or int)
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);

  // out 0: the function of the input
  ADD_OUT_BUFFER;

  ADD_CATEGORY(pCategory::MATH);
}

void dTrigBase::Document(const char* summary, const char* inletDoc, const char* inletRange,
                         const char* outletDoc, const char* outletRange) {
  ADD_DESCRIPTION(summary);
  INLET_DOC(0, "in", inletDoc, inletRange);
  OUTLET_DOC(0, "out", outletDoc, outletRange);
}

FLOAT_IN(SetFloat) {
  floatIn = value;
}

INT_IN(SetInt) {
  floatIn = static_cast<float>(value);
}

CALC() {
  // An unconnected inlet reads as its number; a connected signal wins (#1036).
  outputs[0].SendBuffer(&Process(inputs[0].Signal(floatIn)), thread);
}

#undef className

// ─── the thirteen one-inlet objects ───────────────────────────────────────────

namespace {
  constexpr const char* kSignalIn =
      "The input: a signal, or a number used while no signal is connected.";
  constexpr const char* kRadiansIn =
      "An angle in radians: a signal, or a number used while no signal is connected.";
} // namespace

dCos::dCos() {
  Document("Outputs the cosine of a phase: 0 to 1 is one full cycle. Turns a ~phasor ramp into "
           "a cosine wave. Use ~cosx for an angle in radians.",
           "A phase, 0 to 1 per cycle: a signal, or a number used while no signal is connected.",
           "any float, wraps every 1", "The cosine of 2 pi times the phase.", "-1 to 1");
}

dCosx::dCosx() : dTrigFnBase(DSP::trig::FN::COS) {
  Document("Outputs the cosine of the input, an angle in radians.", kRadiansIn, "any float",
           "The cosine of the input.", "-1 to 1");
}

dSinx::dSinx() : dTrigFnBase(DSP::trig::FN::SIN) {
  Document("Outputs the sine of the input, an angle in radians.", kRadiansIn, "any float",
           "The sine of the input.", "-1 to 1");
}

dTanx::dTanx() : dTrigFnBase(DSP::trig::FN::TAN) {
  Document("Outputs the tangent of the input, an angle in radians. Near odd multiples of pi/2 "
           "the result is very large.",
           kRadiansIn, "any float", "The tangent of the input.", "any float");
}

dAcos::dAcos() : dTrigFnBase(DSP::trig::FN::ACOS) {
  Document("Outputs the arc-cosine of the input in radians. Input outside -1..1 is clamped to "
           "it, so no NaN reaches the graph.",
           kSignalIn, "-1 to 1 (clamped)", "The angle whose cosine is the input.", "0 to pi");
}

dAsin::dAsin() : dTrigFnBase(DSP::trig::FN::ASIN) {
  Document("Outputs the arc-sine of the input in radians. Input outside -1..1 is clamped to it, "
           "so no NaN reaches the graph.",
           kSignalIn, "-1 to 1 (clamped)", "The angle whose sine is the input.", "-pi/2 to pi/2");
}

dAtan::dAtan() : dTrigFnBase(DSP::trig::FN::ATAN) {
  Document("Outputs the arc-tangent of the input in radians.", kSignalIn, "any float",
           "The angle whose tangent is the input.", "-pi/2 to pi/2");
}

dCosh::dCosh() : dTrigFnBase(DSP::trig::FN::COSH) {
  Document("Outputs the hyperbolic cosine of the input. The input is clamped to -88..88 so the "
           "result stays finite.",
           kSignalIn, "-88 to 88 (clamped)", "The hyperbolic cosine of the input.", ">= 1");
}

dSinh::dSinh() : dTrigFnBase(DSP::trig::FN::SINH) {
  Document("Outputs the hyperbolic sine of the input. The input is clamped to -88..88 so the "
           "result stays finite.",
           kSignalIn, "-88 to 88 (clamped)", "The hyperbolic sine of the input.", "any float");
}

dTanh::dTanh() : dTrigFnBase(DSP::trig::FN::TANH) {
  Document("Outputs the hyperbolic tangent of the input: a smooth soft clipper that bends any "
           "signal into -1..1. Scale the signal up first to drive it harder.",
           kSignalIn, "any float", "The hyperbolic tangent of the input.", "-1 to 1");
}

dAcosh::dAcosh() : dTrigFnBase(DSP::trig::FN::ACOSH) {
  Document("Outputs the hyperbolic arc-cosine of the input. Input below 1 is clamped to 1 "
           "(giving 0), so no NaN reaches the graph.",
           kSignalIn, ">= 1 (clamped)", "The hyperbolic arc-cosine of the input.", ">= 0");
}

dAsinh::dAsinh() : dTrigFnBase(DSP::trig::FN::ASINH) {
  Document("Outputs the hyperbolic arc-sine of the input.", kSignalIn, "any float",
           "The hyperbolic arc-sine of the input.", "any float");
}

dAtanh::dAtanh() : dTrigFnBase(DSP::trig::FN::ATANH) {
  Document("Outputs the hyperbolic arc-tangent of the input. Input is clamped just inside "
           "-1..1, so 1 gives about 8.66 instead of infinity.",
           kSignalIn, "-1 to 1 (clamped)", "The hyperbolic arc-tangent of the input.",
           "about -8.66 to 8.66");
}

// ─── ~atan2 ───────────────────────────────────────────────────────────────────

#define className dAtan2

CONSTRUCT_DSP() {
  yFloatIn = 0.f;
  xFloatIn = 0.f;

  // in 0: y (signal, float or int)
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetYFloat);
  REG_INT_IN(SetYInt);

  // in 1: x (signal, float or int)
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetXFloat);
  REG_INT_IN(SetXInt);

  // out 0: the angle
  ADD_OUT_BUFFER;

  ADD_PARAM(xFloatIn);

  ADD_DESCRIPTION("Outputs the angle of the point (x, y) in radians: the two-argument "
                  "arc-tangent, correct in all four quadrants. The origin gives 0.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "y", "The y coordinate: a signal, or a number used while no signal is connected.",
            "any float");
  INLET_DOC(1, "x", "The x coordinate: a signal, or a number used while no signal is connected.",
            "any float");
  OUTLET_DOC(0, "out", "The angle of (x, y), measured from the positive x axis.", "-pi to pi");
  PARAM_DOC("x", "0", "x coordinate used while no signal is connected to the right inlet.",
            "any float");
}

FLOAT_IN(SetYFloat) {
  yFloatIn = value;
}

INT_IN(SetYInt) {
  yFloatIn = static_cast<float>(value);
}

FLOAT_IN(SetXFloat) {
  xFloatIn = value;
}

INT_IN(SetXInt) {
  xFloatIn = static_cast<float>(value);
}

CALC() {
  // Unconnected inlets read as their numbers; a connected signal wins (#1036).
  DSP::buffer& y = inputs[0].Signal(yFloatIn);
  DSP::buffer& out =
      inputs[1].IsConstant() ? angle(y, xFloatIn) : angle(y, inputs[1].Signal(xFloatIn));
  outputs[0].SendBuffer(&out, thread);
}

#undef className

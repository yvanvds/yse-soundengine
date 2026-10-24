#include "dPolar.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

// ─── the two-inlet, two-outlet base ───────────────────────────────────────────

#define className dPolarBase

dPolarBase::dPolarBase() : pObject(true) {
  leftFloatIn = 0.f;
  rightFloatIn = 0.f;

  // in 0: the first coordinate (signal, float or int)
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetLeftFloat);
  REG_INT_IN(SetLeftInt);

  // in 1: the second coordinate (signal, float or int)
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetRightFloat);
  REG_INT_IN(SetRightInt);

  // out 0 / out 1: the converted pair
  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;

  ADD_PARAM(rightFloatIn);

  ADD_CATEGORY(pCategory::MATH);
}

void dPolarBase::Document(const char* summary, const char* leftLabel, const char* leftDoc,
                          const char* rightLabel, const char* rightDoc, const char* out0Label,
                          const char* out0Doc, const char* out0Range, const char* out1Label,
                          const char* out1Doc, const char* out1Range, const char* paramDoc) {
  ADD_DESCRIPTION(summary);
  INLET_DOC(0, leftLabel, leftDoc, "any float");
  INLET_DOC(1, rightLabel, rightDoc, "any float");
  OUTLET_DOC(0, out0Label, out0Doc, out0Range);
  OUTLET_DOC(1, out1Label, out1Doc, out1Range);
  PARAM_DOC(rightLabel, "0", paramDoc, "any float");
}

FLOAT_IN(SetLeftFloat) {
  leftFloatIn = value;
}

INT_IN(SetLeftInt) {
  leftFloatIn = static_cast<float>(value);
}

FLOAT_IN(SetRightFloat) {
  rightFloatIn = value;
}

INT_IN(SetRightInt) {
  rightFloatIn = static_cast<float>(value);
}

CALC() {
  // Unconnected inlets read as their numbers; a connected signal wins (#1036).
  DSP::buffer& left = inputs[0].Signal(leftFloatIn);
  if (inputs[1].IsConstant())
    Process(left, rightFloatIn);
  else
    Process(left, inputs[1].Signal(rightFloatIn));
  // Right outlet first, as Max fires outlets right to left.
  outputs[1].SendBuffer(&Right(), thread);
  outputs[0].SendBuffer(&Left(), thread);
}

#undef className

// ─── the three objects ────────────────────────────────────────────────────────

dCarToPol::dCarToPol() {
  Document("Converts a signal pair from Cartesian to polar form: the real and imaginary parts in, "
           "the magnitude and phase out. On the two outputs of a Hilbert transform it gives the "
           "envelope and the instantaneous phase.",
           "real", "The real part (x): a signal, or a number used while no signal is connected.",
           "imaginary",
           "The imaginary part (y): a signal, or a number used while no signal is connected.",
           "magnitude", "The distance of (real, imaginary) from the origin.", "0 or higher",
           "phase", "The angle of (real, imaginary) in radians, from the positive real axis.",
           "-pi to pi", "Imaginary part used while no signal is connected to the right inlet.");
}

dPolToCar::dPolToCar() {
  Document("Converts a signal pair from polar to Cartesian form: the magnitude and phase in, the "
           "real and imaginary parts out. The inverse of ~cartopol.",
           "magnitude",
           "The distance from the origin: a signal, or a number used while no signal is "
           "connected.",
           "phase",
           "The angle in radians: a signal, or a number used while no signal is connected.", "real",
           "The real part: magnitude times the cosine of the phase.", "any float", "imaginary",
           "The imaginary part: magnitude times the sine of the phase.", "any float",
           "Phase in radians used while no signal is connected to the right inlet.");
}

dPhaseWrap::dPhaseWrap() {
  Document("Wraps the input, an angle in radians, into -pi to pi by adding or removing whole "
           "turns. Use it on a phase difference or an accumulated phase.",
           "An angle in radians: a signal, or a number used while no signal is connected.",
           "any float", "The input wrapped into -pi to pi; pi itself gives -pi.", "-pi to pi");
}

#include "gPolar.h"
#include "../pObjectList.hpp"
#include <cmath>

using namespace YSE::PATCHER;

#define className gPolarBase

gPolarBase::gPolarBase(operation op) : pObject(false), apply(op), leftIn(0.f), rightIn(0.f) {
  ADD_IN_0;
  REG_FLOAT_IN(SetLeftFloat);
  REG_INT_IN(SetLeftInt);

  ADD_IN_1;
  REG_FLOAT_IN(SetRightFloat);
  REG_INT_IN(SetRightInt);

  ADD_OUT_FLOAT;
  ADD_OUT_FLOAT;

  ADD_PARAM(rightIn);

  ADD_CATEGORY(pCategory::MATH);
}

void gPolarBase::Document(const char* summary, const char* leftLabel, const char* leftDoc,
                          const char* leftRange, const char* rightLabel, const char* rightDoc,
                          const char* rightRange, const char* out0Label, const char* out0Doc,
                          const char* out0Range, const char* out1Label, const char* out1Doc,
                          const char* out1Range, const char* paramDoc) {
  ADD_DESCRIPTION(summary);
  INLET_DOC(0, leftLabel, leftDoc, leftRange);
  INLET_DOC(1, rightLabel, rightDoc, rightRange);
  OUTLET_DOC(0, out0Label, out0Doc, out0Range);
  OUTLET_DOC(1, out1Label, out1Doc, out1Range);
  PARAM_DOC(rightLabel, "0", paramDoc, rightRange);
}

FLOAT_IN(SetLeftFloat) {
  leftIn = value;
}

FLOAT_IN(SetRightFloat) {
  rightIn = value;
}

INT_IN(SetLeftInt) {
  leftIn = (float)value;
}

INT_IN(SetRightInt) {
  rightIn = (float)value;
}

CALC() {
  float out0 = 0.f;
  float out1 = 0.f;
  apply(leftIn, rightIn, out0, out1);

  // std::hypot / std::atan2 / std::cos / std::sin are all finite for finite
  // operands, so the only way to a NaN or an infinity here is a non-finite
  // value arriving from elsewhere in the patch. Substitute 0 rather than let it
  // escape and poison every object downstream — the same convention ./ , .sqrt
  // and .atan2 use. Two branches on a register: no allocation, no lock, no I/O.
  if (!std::isfinite(out0)) out0 = 0.f;
  if (!std::isfinite(out1)) out1 = 0.f;

  // Right outlet first: Max fires outlets right to left, so a downstream object
  // fed from both outlets already holds the second value when the first one
  // triggers it.
  outputs[1].SendFloat(out1, thread);
  outputs[0].SendFloat(out0, thread);
}

#undef className

// ─── the two conversions ──────────────────────────────────────────────────────
// Each captureless lambda decays to a plain gPolarBase::operation.

gCarToPol::gCarToPol()
  : gPolarBase([](float x, float y, float& amplitude, float& angle) {
      // hypot rather than sqrt(x*x + y*y): it does not overflow for operands
      // whose squares would.
      amplitude = std::hypot(x, y);
      angle = std::atan2(y, x);
    }) {
  Document("Control-rate cartesian to polar conversion. Whenever inlet 0 fires it emits the polar "
           "form of the point (x, y): its distance from the origin on outlet 0 and its angle in "
           "radians, measured from the positive x axis, on outlet 1. Both outlets fire on every "
           "evaluation, right one first.",
           "x", "x coordinate — fires the evaluation.", "any float", "y",
           "y coordinate — stored until the next evaluation.", "any float", "amplitude",
           "Distance of (x, y) from the origin.", "0 or higher", "angle",
           "Angle of (x, y) in radians, measured from the positive x axis.", "-pi to pi",
           "Initial y coordinate.");
}

gPolToCar::gPolToCar()
  : gPolarBase([](float amplitude, float angle, float& x, float& y) {
      x = amplitude * std::cos(angle);
      y = amplitude * std::sin(angle);
    }) {
  Document("Control-rate polar to cartesian conversion. Whenever inlet 0 fires it emits the "
           "cartesian form of the polar point (amplitude, angle): the x coordinate on outlet 0 and "
           "the y coordinate on outlet 1. The inverse of .cartopol, so feeding one into the other "
           "returns the original pair. Both outlets fire on every evaluation, right one first.",
           "amplitude", "Distance from the origin — fires the evaluation.", "any float", "angle",
           "Angle in radians, measured from the positive x axis — stored until the next "
           "evaluation.",
           "any float", "x", "x coordinate of the point.", "any float", "y",
           "y coordinate of the point.", "any float", "Initial angle in radians.");
}

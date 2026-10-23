#include "dCompare.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className dCompareBase

dCompareBase::dCompareBase(bool fuzzy) : pObject(true) {
  leftFloatIn = 0.f;
  rightFloatIn = 0.f;
  fuzzyIn = 0.f;

  // in 0: the signal compared (signal, float or int)
  ADD_IN_0;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetLeftFloat);
  REG_INT_IN(SetLeftInt);

  // in 1: what it is compared with (signal, float or int)
  ADD_IN_1;
  REG_SIGNAL_IN;
  REG_FLOAT_IN(SetRightFloat);
  REG_INT_IN(SetRightInt);

  // out 0: the result
  ADD_OUT_BUFFER;

  ADD_PARAM(rightFloatIn);
  if (fuzzy) ADD_PARAM(fuzzyIn);

  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "left",
            "The signal compared: a signal, or a number used while no signal is connected.",
            "any float");
  INLET_DOC(1, "right",
            "What the left signal is compared with: a signal, or a number used while no signal "
            "is connected.",
            "any float");
  PARAM_DOC("right", "0", "Right operand used while no signal is connected to the right inlet.",
            "any float");
  if (fuzzy)
    PARAM_DOC("fuzzy", "0",
              "Equality tolerance: values this close or closer count as equal. 0 compares "
              "exactly.",
              ">= 0");
}

void dCompareBase::Document(const char* summary, const char* outletDoc, const char* outletRange) {
  ADD_DESCRIPTION(summary);
  OUTLET_DOC(0, "out", outletDoc, outletRange);
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
  const Flt fuzzy = fuzzyIn;
  DSP::buffer& left = inputs[0].Signal(leftFloatIn);
  DSP::buffer& out = inputs[1].IsConstant() ? Process(left, rightFloatIn, fuzzy)
                                            : Process(left, inputs[1].Signal(rightFloatIn), fuzzy);
  outputs[0].SendBuffer(&out, thread);
}

#undef className

// ─── comparisons ──────────────────────────────────────────────────────────────

dCompareOpBase::dCompareOpBase(DSP::compare::OP op)
  : dCompareBase(op != DSP::compare::OP::GREATER && op != DSP::compare::OP::LESS), op(op) {}

YSE::DSP::buffer& dCompareOpBase::Process(DSP::buffer& left, Flt right, Flt fuzzy) {
  return op.setFuzzy(fuzzy)(left, right);
}

YSE::DSP::buffer& dCompareOpBase::Process(DSP::buffer& left, DSP::buffer& right, Flt fuzzy) {
  return op.setFuzzy(fuzzy)(left, right);
}

// ─── the eight objects ────────────────────────────────────────────────────────

dEqual::dEqual() : dCompareOpBase(DSP::compare::OP::EQUAL) {
  Document("Outputs 1 where the left signal equals the right one (within fuzzy), otherwise 0.",
           "1 where left == right, 0 elsewhere.", "0 or 1");
}

dNotEqual::dNotEqual() : dCompareOpBase(DSP::compare::OP::NOT_EQUAL) {
  Document("Outputs 1 where the left signal differs from the right one (by more than fuzzy), "
           "otherwise 0.",
           "1 where left != right, 0 elsewhere.", "0 or 1");
}

dGreater::dGreater() : dCompareOpBase(DSP::compare::OP::GREATER) {
  Document("Outputs 1 where the left signal is greater than the right one, otherwise 0. Turns "
           "a signal into a gate above a threshold.",
           "1 where left > right, 0 elsewhere.", "0 or 1");
}

dGreaterEqual::dGreaterEqual() : dCompareOpBase(DSP::compare::OP::GREATER_EQUAL) {
  Document("Outputs 1 where the left signal is greater than or equal to the right one (within "
           "fuzzy), otherwise 0.",
           "1 where left >= right, 0 elsewhere.", "0 or 1");
}

dLess::dLess() : dCompareOpBase(DSP::compare::OP::LESS) {
  Document("Outputs 1 where the left signal is less than the right one, otherwise 0. Turns a "
           "signal into a gate below a threshold.",
           "1 where left < right, 0 elsewhere.", "0 or 1");
}

dLessEqual::dLessEqual() : dCompareOpBase(DSP::compare::OP::LESS_EQUAL) {
  Document("Outputs 1 where the left signal is less than or equal to the right one (within "
           "fuzzy), otherwise 0.",
           "1 where left <= right, 0 elsewhere.", "0 or 1");
}

dMinimum::dMinimum() {
  Document("Outputs the smaller of the two inputs, sample by sample. With a number it limits "
           "the signal from above.",
           "The smaller of left and right at each sample.", "any float");
}

dMaximum::dMaximum() {
  Document("Outputs the larger of the two inputs, sample by sample. With a number it limits the "
           "signal from below; against 0 it is a half-wave rectifier.",
           "The larger of left and right at each sample.", "any float");
}

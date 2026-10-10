#include "gReverse.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gReverseBase

gReverseBase::gReverseBase(operation op) : pObject(false), apply(op), leftIn(0.f), rightIn(0.f) {
  ADD_IN_0;
  REG_FLOAT_IN(SetLeftFloat);
  REG_INT_IN(SetLeftInt);

  ADD_IN_1;
  REG_FLOAT_IN(SetRightFloat);
  REG_INT_IN(SetRightInt);

  ADD_OUT_FLOAT;

  ADD_PARAM(rightIn);

  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(1, "right", "Right operand, stored for the next result.", "any float");
  PARAM_DOC("right", "0", "Starting right operand.", "any float");
}

void gReverseBase::Document(const char* summary, const char* leftDoc, const char* outletDoc) {
  ADD_DESCRIPTION(summary);
  INLET_DOC(0, "left", leftDoc, "any float");
  OUTLET_DOC(0, "out", outletDoc, "any float");
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
  outputs[0].SendFloat(apply(leftIn, rightIn), thread);
}

#undef className

// ─── the two operators ────────────────────────────────────────────────────────
// Each captureless lambda decays to a plain float(*)(float, float).

gReverseSubstract::gReverseSubstract()
  : gReverseBase([](float left, float right) { return right - left; }) {
  Document("Subtracts the left operand from the right one and sends right - left as a float: "
           ".- with the operands the other way round.",
           "Left operand, the value subtracted; sends the result.", "right - left.");
}

// Division by zero emits 0, the convention ./ already uses.
gReverseDivide::gReverseDivide()
  : gReverseBase([](float left, float right) { return left != 0.f ? right / left : 0.f; }) {
  Document("Divides the right operand by the left one and sends right / left as a float: ./ with "
           "the operands the other way round. A left operand of 0 sends 0.",
           "Left operand, the divisor; sends the result. 0 makes the result 0.",
           "right / left, or 0 when left is 0.");
}

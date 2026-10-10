#include "gCompare.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gCompareBase

gCompareBase::gCompareBase(predicate op) : pObject(false), compare(op), leftIn(0.f), rightIn(0.f) {
  ADD_IN_0;
  REG_FLOAT_IN(SetLeftFloat);
  REG_INT_IN(SetLeftInt);

  ADD_IN_1;
  REG_FLOAT_IN(SetRightFloat);
  REG_INT_IN(SetRightInt);

  ADD_OUT_INT;

  ADD_PARAM(rightIn);

  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "left", "Left operand — fires the evaluation.", "any float");
  INLET_DOC(1, "right", "Right operand — stored until the next evaluation.", "any float");
  PARAM_DOC("right", "0", "Initial right-operand value.", "any float");
}

void gCompareBase::Document(const char* summary, const char* outletDoc) {
  ADD_DESCRIPTION(summary);
  OUTLET_DOC(0, "out", outletDoc, "0 or 1");
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
  outputs[0].SendInt(compare(leftIn, rightIn) ? 1 : 0, thread);
}

#undef className

// ─── the eight operators ──────────────────────────────────────────────────────
// Each captureless lambda decays to a plain bool(*)(float, float).

gEqual::gEqual() : gCompareBase([](float left, float right) { return left == right; }) {
  Document("Control-rate equality test. Emits 1 when left == right and 0 otherwise, whenever "
           "inlet 0 fires.",
           "1 when left == right, 0 otherwise.");
}

gNotEqual::gNotEqual() : gCompareBase([](float left, float right) { return left != right; }) {
  Document("Control-rate inequality test. Emits 1 when left != right and 0 otherwise, whenever "
           "inlet 0 fires.",
           "1 when left != right, 0 otherwise.");
}

gLess::gLess() : gCompareBase([](float left, float right) { return left < right; }) {
  Document("Control-rate less-than test. Emits 1 when left < right and 0 otherwise, whenever "
           "inlet 0 fires.",
           "1 when left < right, 0 otherwise.");
}

gLessEqual::gLessEqual() : gCompareBase([](float left, float right) { return left <= right; }) {
  Document("Control-rate less-than-or-equal test. Emits 1 when left <= right and 0 otherwise, "
           "whenever inlet 0 fires.",
           "1 when left <= right, 0 otherwise.");
}

gGreater::gGreater() : gCompareBase([](float left, float right) { return left > right; }) {
  Document("Control-rate greater-than test. Emits 1 when left > right and 0 otherwise, whenever "
           "inlet 0 fires.",
           "1 when left > right, 0 otherwise.");
}

gGreaterEqual::gGreaterEqual()
  : gCompareBase([](float left, float right) { return left >= right; }) {
  Document("Control-rate greater-than-or-equal test. Emits 1 when left >= right and 0 otherwise, "
           "whenever inlet 0 fires.",
           "1 when left >= right, 0 otherwise.");
}

gLogicalAnd::gLogicalAnd()
  : gCompareBase([](float left, float right) { return left != 0.f && right != 0.f; }) {
  Document("Control-rate logical AND. Emits 1 when both operands are non-zero and 0 otherwise, "
           "whenever inlet 0 fires.",
           "1 when both operands are non-zero, 0 otherwise.");
}

gLogicalOr::gLogicalOr()
  : gCompareBase([](float left, float right) { return left != 0.f || right != 0.f; }) {
  Document("Control-rate logical OR. Emits 1 when either operand is non-zero and 0 otherwise, "
           "whenever inlet 0 fires.",
           "1 when either operand is non-zero, 0 otherwise.");
}

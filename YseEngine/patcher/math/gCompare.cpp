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
  INLET_DOC(0, "left", "Left operand; sends the result.", "any float");
  INLET_DOC(1, "right", "Right operand, stored for the next result.", "any float");
  PARAM_DOC("right", "0", "Starting right operand.", "any float");
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
  Document("Sends 1 when the left operand equals the right one, otherwise 0.",
           "1 when left == right, 0 otherwise.");
}

gNotEqual::gNotEqual() : gCompareBase([](float left, float right) { return left != right; }) {
  Document("Sends 1 when the left operand differs from the right one, otherwise 0.",
           "1 when left != right, 0 otherwise.");
}

gLess::gLess() : gCompareBase([](float left, float right) { return left < right; }) {
  Document("Sends 1 when the left operand is less than the right one, otherwise 0.",
           "1 when left < right, 0 otherwise.");
}

gLessEqual::gLessEqual() : gCompareBase([](float left, float right) { return left <= right; }) {
  Document("Sends 1 when the left operand is less than or equal to the right one, otherwise 0.",
           "1 when left <= right, 0 otherwise.");
}

gGreater::gGreater() : gCompareBase([](float left, float right) { return left > right; }) {
  Document("Sends 1 when the left operand is greater than the right one, otherwise 0.",
           "1 when left > right, 0 otherwise.");
}

gGreaterEqual::gGreaterEqual()
  : gCompareBase([](float left, float right) { return left >= right; }) {
  Document("Sends 1 when the left operand is greater than or equal to the right one, otherwise 0.",
           "1 when left >= right, 0 otherwise.");
}

gLogicalAnd::gLogicalAnd()
  : gCompareBase([](float left, float right) { return left != 0.f && right != 0.f; }) {
  Document("Logical AND: sends 1 when both operands are non-zero, otherwise 0.",
           "1 when both operands are non-zero, 0 otherwise.");
}

gLogicalOr::gLogicalOr()
  : gCompareBase([](float left, float right) { return left != 0.f || right != 0.f; }) {
  Document("Logical OR: sends 1 when either operand is non-zero, otherwise 0.",
           "1 when either operand is non-zero, 0 otherwise.");
}

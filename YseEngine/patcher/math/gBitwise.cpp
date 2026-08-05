#include "gBitwise.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

namespace {

  // A shift count is only well defined in C++ for 0..31 on a 32-bit int, and a
  // patch can feed any integer into the right inlet. Rather than leave that as
  // undefined behaviour the counts are clamped to the mathematically obvious
  // answer: a negative count shifts by nothing, a count of 32 or more shifts
  // every bit out. Both helpers are branch-only — RT-safe.
  const int kIntBits = 32;

  int ShiftLeftOp(int left, int right) {
    if (right <= 0) {
      return left;
    }
    if (right >= kIntBits) {
      return 0;
    }
    // Shift through unsigned: shifting a bit into (or past) the sign bit of a
    // signed int is UB, while the unsigned round trip is the two's-complement
    // result every Max patch expects.
    return (int)((unsigned int)left << right);
  }

  int ShiftRightOp(int left, int right) {
    if (right <= 0) {
      return left;
    }
    // Arithmetic shift: the sign bit is replicated, so shifting a negative
    // value far enough saturates at -1 rather than 0.
    if (right >= kIntBits) {
      return left < 0 ? -1 : 0;
    }
    return left >> right;
  }

} // namespace

#define className gBitwiseBase

gBitwiseBase::gBitwiseBase(operation op) : pObject(false), apply(op), leftIn(0), rightIn(0) {
  ADD_IN_0;
  REG_FLOAT_IN(SetLeftFloat);
  REG_INT_IN(SetLeftInt);

  ADD_IN_1;
  REG_FLOAT_IN(SetRightFloat);
  REG_INT_IN(SetRightInt);

  ADD_OUT_INT;

  ADD_PARAM(rightIn);

  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "left", "Left operand — fires the evaluation. Floats truncate towards zero.",
            "any int");
  INLET_DOC(1, "right",
            "Right operand — stored until the next evaluation. Floats truncate towards zero.",
            "any int");
  PARAM_DOC("right", "0", "Initial right-operand value.", "any int");
}

void gBitwiseBase::Document(const char* summary, const char* outletDoc) {
  ADD_DESCRIPTION(summary);
  OUTLET_DOC(0, "out", outletDoc, "any int");
}

FLOAT_IN(SetLeftFloat) {
  leftIn = (int)value;
}

FLOAT_IN(SetRightFloat) {
  rightIn = (int)value;
}

INT_IN(SetLeftInt) {
  leftIn = value;
}

INT_IN(SetRightInt) {
  rightIn = value;
}

CALC() {
  outputs[0].SendInt(apply(leftIn, rightIn), thread);
}

#undef className

// ─── the four operators ───────────────────────────────────────────────────────
// Each captureless lambda decays to a plain int(*)(int, int).

gBitAnd::gBitAnd() : gBitwiseBase([](int left, int right) { return left & right; }) {
  Document("Control-rate bitwise AND. Emits left & right whenever inlet 0 fires. Floats truncate "
           "towards zero on input.",
           "The bitwise AND of both operands.");
}

gBitOr::gBitOr() : gBitwiseBase([](int left, int right) { return left | right; }) {
  Document("Control-rate bitwise OR. Emits left | right whenever inlet 0 fires. Floats truncate "
           "towards zero on input.",
           "The bitwise OR of both operands.");
}

gShiftLeft::gShiftLeft() : gBitwiseBase(&ShiftLeftOp) {
  Document("Control-rate bit shift left. Emits left << right whenever inlet 0 fires. Shift counts "
           "below 0 shift by nothing and counts of 32 or more shift every bit out.",
           "The left operand shifted left by the right operand.");
}

gShiftRight::gShiftRight() : gBitwiseBase(&ShiftRightOp) {
  Document("Control-rate arithmetic bit shift right. Emits left >> right whenever inlet 0 fires. "
           "The sign bit is replicated, so negative values saturate at -1.",
           "The left operand shifted right by the right operand.");
}

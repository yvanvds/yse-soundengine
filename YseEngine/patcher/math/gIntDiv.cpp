#include "gIntDiv.h"
#include "../pObjectList.hpp"
#include <climits>

using namespace YSE::PATCHER;

namespace {

  // Integer division has two inputs a patch can reach that the C++ operators
  // cannot answer: a zero divisor (division by zero) and INT_MIN / -1 (the one
  // quotient that does not fit back into an int). Both are undefined behaviour
  // — on x86 they trap and take the process down — so they are answered here
  // instead. Both helpers are branch-only: RT-safe.

  int ModuloOp(int left, int right) {
    if (right == 0) {
      return 0; // the ./ divide-by-zero convention
    }
    if (right == -1) {
      return 0; // mathematically exact, and dodges the INT_MIN overflow
    }
    return left % right;
  }

  int IntDivideOp(int left, int right) {
    if (right == 0) {
      return 0; // the ./ divide-by-zero convention
    }
    if (left == INT_MIN && right == -1) {
      return INT_MIN; // the true quotient overflows; wrap rather than trap
    }
    return left / right;
  }

} // namespace

#define className gIntDivBase

gIntDivBase::gIntDivBase(operation op) : pObject(false), apply(op), leftIn(0), rightIn(0) {
  ADD_IN_0;
  REG_FLOAT_IN(SetLeftFloat);
  REG_INT_IN(SetLeftInt);

  ADD_IN_1;
  REG_FLOAT_IN(SetRightFloat);
  REG_INT_IN(SetRightInt);

  ADD_OUT_INT;

  ADD_PARAM(rightIn);

  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "left",
            "Left operand — the dividend, and fires the evaluation. Floats truncate "
            "towards zero.",
            "any int");
  INLET_DOC(1, "right",
            "Right operand — the divisor, stored until the next evaluation. Floats truncate "
            "towards zero. Zero forces the output to 0.",
            "any int");
  PARAM_DOC("right", "0", "Initial divisor.", "any int");
}

void gIntDivBase::Document(const char* summary, const char* outletDoc) {
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

// ─── the two operators ────────────────────────────────────────────────────────

gModulo::gModulo() : gIntDivBase(&ModuloOp) {
  Document("Control-rate modulo. Emits the remainder of left / right whenever inlet 0 fires — the "
           "usual way to wrap a counter. The remainder takes the sign of the left operand, floats "
           "truncate towards zero on input, and a divisor of 0 emits 0.",
           "The remainder of left / right (or 0 when right == 0).");
}

gIntDivide::gIntDivide() : gIntDivBase(&IntDivideOp) {
  Document("Control-rate integer division. Emits left / right as an int whenever inlet 0 fires, "
           "truncating towards zero instead of producing the float ./ would. Floats truncate "
           "towards zero on input, and a divisor of 0 emits 0.",
           "left / right truncated towards zero (or 0 when right == 0).");
}

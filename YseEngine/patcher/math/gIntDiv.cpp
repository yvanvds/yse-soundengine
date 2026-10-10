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
  INLET_DOC(0, "left", "Dividend; sends the result. Floats are truncated toward zero.", "any int");
  INLET_DOC(1, "right",
            "Divisor, stored for the next result. Floats are truncated toward zero; 0 makes the "
            "result 0.",
            "any int");
  PARAM_DOC("right", "0", "Starting divisor.", "any int");
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
  Document("Sends the remainder of dividing the left operand by the right one, as an int. The "
           "remainder has the sign of the left operand, and a divisor of 0 sends 0.",
           "The remainder of left / right, or 0 when right is 0.");
}

gIntDivide::gIntDivide() : gIntDivBase(&IntDivideOp) {
  Document("Integer division: divides the left operand by the right one and sends the whole "
           "quotient as an int, truncated toward zero. A divisor of 0 sends 0.",
           "left / right truncated toward zero, or 0 when right is 0.");
}

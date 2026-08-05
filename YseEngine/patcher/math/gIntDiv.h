#pragma once
#include "../pObject.h"

// Declares one member of the integer-division family (issue #439). Mirrors
// BITWISE_CLASS from gBitwise.h: the whole two-inlet body lives in
// gIntDivBase, and the constructor in gIntDiv.cpp only has to pick an
// operation and fill in the operator-specific documentation.
#define INTDIV_CLASS(className, typeName)                                                          \
  class className : public gIntDivBase {                                                           \
  public:                                                                                          \
    className();                                                                                   \
    const char* Type() const override {                                                            \
      return typeName;                                                                             \
    }                                                                                              \
    CREATE(className)                                                                              \
  };

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Shared body for the integer-division operators ``.%`` and
     *         ``.div``.
     *
     *  Same two-inlet shape as the ``.+`` arithmetic family and the ``.&``
     *  bitwise family: inlet 0 is the left operand and fires the evaluation,
     *  inlet 1 stores the right operand. Both operands are integers — a float
     *  arriving on either inlet is truncated towards zero, matching Max's
     *  int-mode ``%`` and ``div``. The result leaves outlet 0 as an int, which
     *  is the whole point of these two next to ``./``: no float ever appears.
     *
     *  Division by zero emits 0, the convention ``./`` already uses.
     *
     *  The operation is a plain function pointer chosen by the derived
     *  constructor, so Calculate() is a single indirect call: no allocation, no
     *  lock, no I/O.
     */
    class gIntDivBase : public pObject {
    public:
      typedef int (*operation)(int left, int right);

      explicit gIntDivBase(operation op);

      void SetMessage(const std::string&, float) override {}
      void Calculate(YSE::THREAD thread) override;

      void SetLeftFloat(float value, int inlet, YSE::THREAD thread);
      void SetRightFloat(float value, int inlet, YSE::THREAD thread);
      void SetLeftInt(int value, int inlet, YSE::THREAD thread);
      void SetRightInt(int value, int inlet, YSE::THREAD thread);

    protected:
      // Fills in the two pieces of documentation that differ per operator; the
      // base constructor already set the category, both inlets and the
      // parameter. RT-cold — constructor use only.
      void Document(const char* summary, const char* outletDoc);

    private:
      operation apply;
      int leftIn;
      int rightIn;
    };

    INTDIV_CLASS(gModulo, YSE::OBJ::G_MODULO)
    INTDIV_CLASS(gIntDivide, YSE::OBJ::G_INTDIVIDE)

  } // namespace PATCHER
} // namespace YSE

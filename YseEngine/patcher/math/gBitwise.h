#pragma once
#include "../pObject.h"

// Declares one member of the bitwise operator family (issue #438). Mirrors
// COMPARE_CLASS from gCompare.h: the whole two-inlet body lives in
// gBitwiseBase, and the constructor in gBitwise.cpp only has to pick an
// operation and fill in the operator-specific documentation.
#define BITWISE_CLASS(className, typeName)                                                         \
  class className : public gBitwiseBase {                                                          \
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
     *  @brief Shared body for the control-rate bitwise operators
     *         ``.&`` ``.|`` ``.<<`` ``.>>``.
     *
     *  Same two-inlet shape as the ``.+`` / ``.-`` arithmetic family and the
     *  ``.==`` / ``.&&`` comparison family: inlet 0 is the left operand and
     *  fires the evaluation, inlet 1 stores the right operand. Both operands
     *  are integers — a float arriving on either inlet is truncated towards
     *  zero, matching Max's int-only bitwise objects. The result leaves outlet
     *  0 as an int.
     *
     *  The operation is a plain function pointer chosen by the derived
     *  constructor, so Calculate() is a single indirect call: no allocation, no
     *  lock, no I/O.
     */
    class gBitwiseBase : public pObject {
    public:
      typedef int (*operation)(int left, int right);

      explicit gBitwiseBase(operation op);

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

    BITWISE_CLASS(gBitAnd, YSE::OBJ::G_BITAND)
    BITWISE_CLASS(gBitOr, YSE::OBJ::G_BITOR)
    BITWISE_CLASS(gShiftLeft, YSE::OBJ::G_SHIFTLEFT)
    BITWISE_CLASS(gShiftRight, YSE::OBJ::G_SHIFTRIGHT)

  } // namespace PATCHER
} // namespace YSE

#pragma once
#include "../pObject.h"

// Declares one member of the reverse-operand arithmetic family (issue #439).
// Mirrors COMPARE_CLASS from gCompare.h and BITWISE_CLASS from gBitwise.h: the
// whole two-inlet body lives in gReverseBase, and the constructor in
// gReverse.cpp only has to pick an operation and fill in the operator-specific
// documentation.
#define REVERSE_CLASS(className, typeName)                                                         \
  class className : public gReverseBase {                                                          \
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
     *  @brief Shared body for the reverse-operand arithmetic operators
     *         ``.!-`` and ``.!/``.
     *
     *  Same two-inlet shape as the ``.+`` / ``.-`` family: inlet 0 is the left
     *  operand and fires the evaluation, inlet 1 stores the right operand. The
     *  operands are then applied in reverse — ``right - left`` and
     *  ``right / left`` — which saves putting a swap in front of a ``.-`` or
     *  ``./``. The result leaves outlet 0 as a float.
     *
     *  The operation is a plain function pointer chosen by the derived
     *  constructor, so Calculate() is a single indirect call: no allocation, no
     *  lock, no I/O.
     */
    class gReverseBase : public pObject {
    public:
      typedef float (*operation)(float left, float right);

      explicit gReverseBase(operation op);

      void SetMessage(const std::string&, float) override {}
      void Calculate(YSE::THREAD thread) override;

      void SetLeftFloat(float value, int inlet, YSE::THREAD thread);
      void SetRightFloat(float value, int inlet, YSE::THREAD thread);
      void SetLeftInt(int value, int inlet, YSE::THREAD thread);
      void SetRightInt(int value, int inlet, YSE::THREAD thread);

    protected:
      // Fills in the pieces of documentation that differ per operator; the base
      // constructor already set the category, both inlets and the parameter.
      // RT-cold — constructor use only.
      void Document(const char* summary, const char* leftDoc, const char* outletDoc);

    private:
      operation apply;
      float leftIn;
      float rightIn;
    };

    REVERSE_CLASS(gReverseSubstract, YSE::OBJ::G_REVERSESUBSTRACT)
    REVERSE_CLASS(gReverseDivide, YSE::OBJ::G_REVERSEDIVIDE)

  } // namespace PATCHER
} // namespace YSE

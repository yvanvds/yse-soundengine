#pragma once
#include "../pObject.h"

// Declares one member of the unary math family (issue #440). Mirrors
// COMPARE_CLASS / BITWISE_CLASS / INTDIV_CLASS from the sibling families: the
// whole single-inlet body lives in gUnaryMathBase, and the constructor in
// gUnaryMath.cpp only has to pick a function and fill in the
// function-specific documentation.
#define UNARY_MATH_CLASS(className, typeName)                                                      \
  class className : public gUnaryMathBase {                                                        \
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
     *  @brief Shared body for the single-operand math functions ``.abs`` and
     *         ``.sqrt``.
     *
     *  One inlet, one outlet, no parameters: a value arriving on inlet 0 is
     *  stored, the function is applied, and the result leaves outlet 0 as a
     *  float — the same float in / float out convention the ``.+`` family
     *  uses, so an int on the inlet is widened rather than kept as an int.
     *
     *  The function is a plain function pointer chosen by the derived
     *  constructor, so Calculate() is a single indirect call: no allocation, no
     *  lock, no I/O.
     */
    class gUnaryMathBase : public pObject {
    public:
      typedef float (*operation)(float value);

      explicit gUnaryMathBase(operation op);

      void SetMessage(const std::string&, float) override {}
      void Calculate(YSE::THREAD thread) override;

      void SetFloat(float value, int inlet, YSE::THREAD thread);
      void SetInt(int value, int inlet, YSE::THREAD thread);

    protected:
      // Fills in the pieces of documentation that differ per function; the base
      // constructor already set the category. RT-cold — constructor use only.
      void Document(const char* summary, const char* inletDoc, const char* inletRange,
                    const char* outletDoc, const char* outletRange);

    private:
      operation apply;
      float input;
    };

    UNARY_MATH_CLASS(gAbs, YSE::OBJ::G_ABS)
    UNARY_MATH_CLASS(gSqrt, YSE::OBJ::G_SQRT)

  } // namespace PATCHER
} // namespace YSE

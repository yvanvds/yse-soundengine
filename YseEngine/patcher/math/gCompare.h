#pragma once
#include "../pObject.h"

// Declares one member of the comparison / logic operator family (issue #437).
// Mirrors PATCHER_CLASS from pObject.h, but derives from gCompareBase instead
// of pObject so the whole two-inlet body is inherited rather than repeated
// eight times. The constructor lives in gCompare.cpp and only has to pick a
// predicate and fill in the operator-specific documentation.
#define COMPARE_CLASS(className, typeName)                                                         \
  class className : public gCompareBase {                                                          \
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
     *  @brief Shared body for the control-rate comparison and logic operators
     *         ``.==`` ``.!=`` ``.<`` ``.<=`` ``.>`` ``.>=`` ``.&&`` ``.||``.
     *
     *  Identical shape to the ``.+`` / ``.-`` / ``.*`` / ``./`` family: inlet 0
     *  is the left operand and fires the evaluation, inlet 1 stores the right
     *  operand. The result is emitted as an int — 1 for true, 0 for false.
     *
     *  The predicate is a plain function pointer chosen by the derived
     *  constructor, so Calculate() is a single indirect call: no allocation, no
     *  lock, no I/O.
     */
    class gCompareBase : public pObject {
    public:
      typedef bool (*predicate)(float left, float right);

      explicit gCompareBase(predicate op);

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
      predicate compare;
      float leftIn;
      float rightIn;
    };

    COMPARE_CLASS(gEqual, YSE::OBJ::G_EQUAL)
    COMPARE_CLASS(gNotEqual, YSE::OBJ::G_NOTEQUAL)
    COMPARE_CLASS(gLess, YSE::OBJ::G_LESS)
    COMPARE_CLASS(gLessEqual, YSE::OBJ::G_LESSEQUAL)
    COMPARE_CLASS(gGreater, YSE::OBJ::G_GREATER)
    COMPARE_CLASS(gGreaterEqual, YSE::OBJ::G_GREATEREQUAL)
    COMPARE_CLASS(gLogicalAnd, YSE::OBJ::G_LOGICALAND)
    COMPARE_CLASS(gLogicalOr, YSE::OBJ::G_LOGICALOR)

  } // namespace PATCHER
} // namespace YSE

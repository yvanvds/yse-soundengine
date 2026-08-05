#pragma once
#include "../pObject.h"

// Declares one member of the coordinate-conversion family (issue #442).
// Mirrors UNARY_MATH_CLASS from gUnaryMath.h: the whole two-inlet/two-outlet
// body lives in gPolarBase, and the constructor in gPolar.cpp only has to pick
// a conversion and fill in the conversion-specific documentation.
#define POLAR_CLASS(className, typeName)                                                           \
  class className : public gPolarBase {                                                            \
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
     *  @brief Shared body for the coordinate conversions ``.cartopol`` and
     *         ``.poltocar`` (issue #442).
     *
     *  Two inlets and two outlets. Inlet 0 is the hot one and fires the
     *  evaluation, inlet 1 stores the second operand — the same convention the
     *  ``.+`` family and ``.atan2`` use — and the stored operand is registered
     *  as the creation parameter so it survives a DumpJSON / ParseJSON round
     *  trip.
     *
     *  Both outlets fire on every evaluation, **right outlet first**, matching
     *  Max's right-to-left outlet ordering: anything downstream that consumes
     *  both values has the second one in hand before the first one triggers it.
     *
     *  The conversion is a plain function pointer chosen by the derived
     *  constructor, so Calculate() is a single indirect call: no allocation, no
     *  lock, no I/O. Angles are in radians throughout, matching Max and the
     *  rest of the patcher's trigonometry.
     */
    class gPolarBase : public pObject {
    public:
      typedef void (*operation)(float in0, float in1, float& out0, float& out1);

      explicit gPolarBase(operation op);

      void SetMessage(const std::string&, float) override {}
      void Calculate(YSE::THREAD thread) override;

      void SetLeftFloat(float value, int inlet, YSE::THREAD thread);
      void SetRightFloat(float value, int inlet, YSE::THREAD thread);
      void SetLeftInt(int value, int inlet, YSE::THREAD thread);
      void SetRightInt(int value, int inlet, YSE::THREAD thread);

    protected:
      // Fills in the pieces of documentation that differ per conversion; the
      // base constructor already set the category. RT-cold — constructor use
      // only.
      void Document(const char* summary, const char* leftLabel, const char* leftDoc,
                    const char* leftRange, const char* rightLabel, const char* rightDoc,
                    const char* rightRange, const char* out0Label, const char* out0Doc,
                    const char* out0Range, const char* out1Label, const char* out1Doc,
                    const char* out1Range, const char* paramDoc);

    private:
      operation apply;
      float leftIn;
      float rightIn;
    };

    POLAR_CLASS(gCarToPol, YSE::OBJ::G_CARTOPOL)
    POLAR_CLASS(gPolToCar, YSE::OBJ::G_POLTOCAR)

  } // namespace PATCHER
} // namespace YSE

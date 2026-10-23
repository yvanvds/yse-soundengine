#pragma once

#include "../pObject.h"
#include "../../dsp/oscillators.hpp"
#include "../../dsp/operators/trig.hpp"

// Declares one member of the signal trig family (issue #1049), as
// SIGNAL_COMPARE_CLASS does in dCompare.h: the one-inlet body lives in a base,
// and the constructor in dTrig.cpp only picks the function and fills in the
// function-specific documentation.
#define SIGNAL_TRIG_CLASS(className, base, typeName)                                               \
  class className : public base {                                                                  \
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
     *  @brief Shared body for the one-inlet signal trig objects: ``~cos``
     *         ``~cosx`` ``~sinx`` ``~tanx`` ``~acos`` ``~asin`` ``~atan``
     *         ``~cosh`` ``~sinh`` ``~tanh`` ``~acosh`` ``~asinh`` ``~atanh``
     *         (issue #1049).
     *
     *  Ported from Max's ``cos~`` … ``atanh~``, which take no arguments,
     *  messages or attributes. The inlet takes a signal or a number; the
     *  number is used while no signal is connected, and a connected signal
     *  wins (#1036). The wrapper contains no sample math; the derived class
     *  picks the DSP class that does it.
     */
    class dTrigBase : public pObject {
    public:
      void SetMessage(const std::string&, float) override {}
      void Calculate(YSE::THREAD thread) override;

      void SetFloat(float value, int inlet, YSE::THREAD thread);
      void SetInt(int value, int inlet, YSE::THREAD thread);

    protected:
      dTrigBase(); // RT-cold — constructor use.

      // Fills in the documentation that differs per function. RT-cold.
      void Document(const char* summary, const char* inletDoc, const char* inletRange,
                    const char* outletDoc, const char* outletRange);

      virtual DSP::buffer& Process(DSP::buffer& in) = 0;

    private:
      aFlt floatIn;
    };

    /** @brief ``~cos``: a 0..1 phase in, on ``DSP::cosine``. */
    class dPhaseCosineBase : public dTrigBase {
    protected:
      DSP::buffer& Process(DSP::buffer& in) override {
        return phaseCosine(in);
      }

    private:
      DSP::cosine phaseCosine;
    };

    /** @brief The radian and hyperbolic functions, on ``DSP::trig``. */
    class dTrigFnBase : public dTrigBase {
    protected:
      explicit dTrigFnBase(DSP::trig::FN fn) : fn(fn) {}
      DSP::buffer& Process(DSP::buffer& in) override {
        return fn(in);
      }

    private:
      DSP::trig fn;
    };

    SIGNAL_TRIG_CLASS(dCos, dPhaseCosineBase, YSE::OBJ::D_COS)
    SIGNAL_TRIG_CLASS(dCosx, dTrigFnBase, YSE::OBJ::D_COSX)
    SIGNAL_TRIG_CLASS(dSinx, dTrigFnBase, YSE::OBJ::D_SINX)
    SIGNAL_TRIG_CLASS(dTanx, dTrigFnBase, YSE::OBJ::D_TANX)
    SIGNAL_TRIG_CLASS(dAcos, dTrigFnBase, YSE::OBJ::D_ACOS)
    SIGNAL_TRIG_CLASS(dAsin, dTrigFnBase, YSE::OBJ::D_ASIN)
    SIGNAL_TRIG_CLASS(dAtan, dTrigFnBase, YSE::OBJ::D_ATAN)
    SIGNAL_TRIG_CLASS(dCosh, dTrigFnBase, YSE::OBJ::D_COSH)
    SIGNAL_TRIG_CLASS(dSinh, dTrigFnBase, YSE::OBJ::D_SINH)
    SIGNAL_TRIG_CLASS(dTanh, dTrigFnBase, YSE::OBJ::D_TANH)
    SIGNAL_TRIG_CLASS(dAcosh, dTrigFnBase, YSE::OBJ::D_ACOSH)
    SIGNAL_TRIG_CLASS(dAsinh, dTrigFnBase, YSE::OBJ::D_ASINH)
    SIGNAL_TRIG_CLASS(dAtanh, dTrigFnBase, YSE::OBJ::D_ATANH)

    /**
     *  @brief ``~atan2``: the angle of the point (x, y), ported from Max's
     *         ``atan2~`` (issue #1049), on ``DSP::arcTangent2``.
     *
     *  - Left inlet: y, a signal or a number.
     *  - Right inlet: x, a signal or a number. Its number is the ``x``
     *    parameter (creation argument 1, default 0), so it survives a save.
     *
     *  Numbers are used while no signal is connected; a connected signal wins
     *  (#1036).
     */
    PATCHER_CLASS(dAtan2, YSE::OBJ::D_ATAN2)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetYFloat)
    _INT_IN(SetYInt)
    _FLOAT_IN(SetXFloat)
    _INT_IN(SetXInt)

  private:
    aFlt yFloatIn;
    aFlt xFloatIn;
    DSP::arcTangent2 angle;
  };

} // namespace PATCHER
} // namespace YSE

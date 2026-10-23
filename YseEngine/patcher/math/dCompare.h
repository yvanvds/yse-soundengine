#pragma once

#include "../pObject.h"
#include "../../dsp/operators/compare.hpp"

// Declares one member of the signal comparison family (issue #1048). Mirrors
// PATCHER_CLASS from pObject.h, but derives from a dCompareBase so the
// two-inlet body is inherited rather than repeated eight times, as gCompare.h
// does for the control-rate family. The constructor lives in dCompare.cpp and
// only picks the operation and fills in the operator-specific documentation.
#define SIGNAL_COMPARE_CLASS(className, base, typeName)                                            \
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
     *  @brief Shared body for ``~==`` ``~!=`` ``~>`` ``~>=`` ``~<`` ``~<=``
     *         ``~minimum`` and ``~maximum`` (issue #1048).
     *
     *  Ported from Max's ``equals~`` … ``maximum~``. Both inlets take a signal
     *  or a number; the number is used while no signal is connected, and a
     *  connected signal wins (#1036).
     *
     *  - Left inlet: the signal compared.
     *  - Right inlet: what it is compared with. Its number is the ``right``
     *    parameter (creation argument 1, default 0).
     *  - ``~==`` ``~!=`` ``~>=`` ``~<=`` add a ``fuzzy`` parameter (creation
     *    argument 2, default 0), Max's ``@fuzzy`` attribute: the equality
     *    tolerance of ``DSP::compare::setFuzzy``.
     *
     *  The wrapper holds the numbers as atomics and contains no sample math;
     *  the derived class picks the DSP class that does it.
     */
    class dCompareBase : public pObject {
    public:
      void SetMessage(const std::string&, float) override {}
      void Calculate(YSE::THREAD thread) override;

      void SetLeftFloat(float value, int inlet, YSE::THREAD thread);
      void SetLeftInt(int value, int inlet, YSE::THREAD thread);
      void SetRightFloat(float value, int inlet, YSE::THREAD thread);
      void SetRightInt(int value, int inlet, YSE::THREAD thread);

    protected:
      // `fuzzy` registers the tolerance parameter. RT-cold — constructor use.
      explicit dCompareBase(bool fuzzy);

      // Fills in the documentation that differs per operator. RT-cold.
      void Document(const char* summary, const char* outletDoc, const char* outletRange);

      // One block of the operation. `fuzzy` is the current tolerance, 0 for
      // the operators without one.
      virtual DSP::buffer& Process(DSP::buffer& left, Flt right, Flt fuzzy) = 0;
      virtual DSP::buffer& Process(DSP::buffer& left, DSP::buffer& right, Flt fuzzy) = 0;

    private:
      aFlt leftFloatIn;
      aFlt rightFloatIn;
      aFlt fuzzyIn;
    };

    /** @brief The six comparisons, on ``DSP::compare``. */
    class dCompareOpBase : public dCompareBase {
    protected:
      explicit dCompareOpBase(DSP::compare::OP op);
      DSP::buffer& Process(DSP::buffer& left, Flt right, Flt fuzzy) override;
      DSP::buffer& Process(DSP::buffer& left, DSP::buffer& right, Flt fuzzy) override;

    private:
      DSP::compare op;
    };

    /** @brief ``~minimum`` and ``~maximum``, on ``DSP::minimum`` /
     *         ``DSP::maximum``. */
    template <class Op> class dExtremumBase : public dCompareBase {
    protected:
      dExtremumBase() : dCompareBase(false) {}
      DSP::buffer& Process(DSP::buffer& left, Flt right, Flt) override {
        return op(left, right);
      }
      DSP::buffer& Process(DSP::buffer& left, DSP::buffer& right, Flt) override {
        return op(left, right);
      }

    private:
      Op op;
    };

    SIGNAL_COMPARE_CLASS(dEqual, dCompareOpBase, YSE::OBJ::D_EQUAL)
    SIGNAL_COMPARE_CLASS(dNotEqual, dCompareOpBase, YSE::OBJ::D_NOTEQUAL)
    SIGNAL_COMPARE_CLASS(dGreater, dCompareOpBase, YSE::OBJ::D_GREATER)
    SIGNAL_COMPARE_CLASS(dGreaterEqual, dCompareOpBase, YSE::OBJ::D_GREATEREQUAL)
    SIGNAL_COMPARE_CLASS(dLess, dCompareOpBase, YSE::OBJ::D_LESS)
    SIGNAL_COMPARE_CLASS(dLessEqual, dCompareOpBase, YSE::OBJ::D_LESSEQUAL)
    SIGNAL_COMPARE_CLASS(dMinimum, dExtremumBase<DSP::minimum>, YSE::OBJ::D_MINIMUM)
    SIGNAL_COMPARE_CLASS(dMaximum, dExtremumBase<DSP::maximum>, YSE::OBJ::D_MAXIMUM)

  } // namespace PATCHER
} // namespace YSE

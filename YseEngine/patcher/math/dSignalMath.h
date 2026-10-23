#pragma once

#include <atomic>
#include "dTrig.h"
#include "../../dsp/math.hpp"
#include "../../dsp/operators/rounding.hpp"

namespace YSE {
  namespace PATCHER {

    // ─── one inlet: ~abs ~sqrt ~trunc ────────────────────────────────────────
    //
    // These reuse dTrigBase (dTrig.h): one signal-or-number inlet, one signal
    // outlet, and a Process() that hands the block to the DSP class. Ported
    // from Max's abs~, sqrt~ and trunc~, which take no arguments, messages or
    // attributes (issue #1050).

    /** @brief ``~abs``: absolute value, on ``DSP::abs``. */
    class dAbsBase : public dTrigBase {
    protected:
      DSP::buffer& Process(DSP::buffer& in) override {
        return fn(in);
      }

    private:
      DSP::abs fn;
    };

    /** @brief ``~sqrt``: square root, on ``DSP::sqrt``. */
    class dSqrtBase : public dTrigBase {
    protected:
      DSP::buffer& Process(DSP::buffer& in) override {
        return fn(in);
      }

    private:
      DSP::sqrt fn;
    };

    /** @brief ``~trunc``: drops the fractional part, on ``DSP::trunc``. */
    class dTruncBase : public dTrigBase {
    protected:
      DSP::buffer& Process(DSP::buffer& in) override {
        return fn(in);
      }

    private:
      DSP::trunc fn;
    };

    SIGNAL_TRIG_CLASS(dAbs, dAbsBase, YSE::OBJ::D_ABS)
    SIGNAL_TRIG_CLASS(dSqrt, dSqrtBase, YSE::OBJ::D_SQRT)
    SIGNAL_TRIG_CLASS(dTrunc, dTruncBase, YSE::OBJ::D_TRUNC)

    /**
     *  @brief ``~pow``: signal power, ported from Max's ``pow~`` (issue
     *         #1050), on ``DSP::pow``.
     *
     *  **The inlets follow Max's ``pow~``, not ``.pow``:** the exponent is on
     *  the left and the base on the right, so a signal into the left inlet
     *  is an exponent applied to a fixed base (``~pow 2`` is two to the power
     *  of the signal). ``DSP::pow`` itself takes ``(base, exponent)``.
     *
     *  - Left inlet: the exponent, a signal or a number.
     *  - Right inlet: the base, a signal or a number. Its number is the
     *    ``base`` parameter (creation argument 1, default 0), so it survives
     *    a save.
     *
     *  Numbers are used while no signal is connected; a connected signal
     *  wins (#1036). A base of 0 or below, or a result past the float range,
     *  gives 0.
     */
    PATCHER_CLASS(dPow, YSE::OBJ::D_POW)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetExponentFloat)
    _INT_IN(SetExponentInt)
    _FLOAT_IN(SetBaseFloat)
    _INT_IN(SetBaseInt)

  private:
    aFlt exponentIn;
    aFlt baseIn;
    DSP::pow fn;
  };

  /**
   *  @brief ``~log``: logarithm of a signal, ported from Max's ``log~``
   *         (issue #1050), on ``DSP::log``.
   *
   *  - Left inlet: the input, a signal or a number.
   *  - Right inlet: the base, a signal or a number. Its number is the
   *    ``base`` parameter (creation argument 1, default e), so it survives
   *    a save.
   *
   *  Numbers are used while no signal is connected; a connected signal wins
   *  (#1036). An input of 0 or below gives 0; a base of 0 or below, or of 1,
   *  falls back to the natural logarithm.
   */
  PATCHER_CLASS(dLog, YSE::OBJ::D_LOG)
  _NO_MESSAGES
  _DO_CALCULATE

  _FLOAT_IN(SetInputFloat)
  _INT_IN(SetInputInt)
  _FLOAT_IN(SetBaseFloat)
  _INT_IN(SetBaseInt)

private:
  aFlt inputFloat;
  aFlt baseIn;
  DSP::log fn;
};

/**
 *  @brief ``~round``: rounds a signal to a multiple of a step, ported from
 *         Max's ``round~`` (issue #1050), on ``DSP::round``.
 *
 *  - Left inlet: the input, a signal or a number. ``nearest 1`` rounds to
 *    the nearest multiple (the default), ``nearest 0`` rounds down.
 *  - Right inlet: the step, a signal or a number. Its number is the
 *    ``step`` parameter (creation argument 1, default 1). A step of 0
 *    passes the input through.
 *  - Parameter ``nearest`` (creation argument 2, default 1): Max's
 *    ``nearest`` attribute.
 *
 *  Numbers are used while no signal is connected; a connected signal wins
 *  (#1036).
 */
PATCHER_CLASS(dRound, YSE::OBJ::D_ROUND)
_DO_MESSAGES
_DO_CALCULATE

_FLOAT_IN(SetInputFloat)
_INT_IN(SetInputInt)
_FLOAT_IN(SetStepFloat)
_INT_IN(SetStepInt)

private:
aFlt inputFloat;
aFlt stepIn;
aInt nearest;
DSP::round fn;
}
;

} // namespace PATCHER
} // namespace YSE

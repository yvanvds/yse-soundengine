#pragma once

#include "dTrig.h"
#include "../../dsp/operators/bitwise.hpp"

// Declares one member of the two-inlet bitwise family (issue #1051), as
// SIGNAL_TRIG_CLASS does in dTrig.h: the body lives in dBitwiseBase and the
// constructor in dBitwise.cpp only picks the operation and documents it.
#define SIGNAL_BITWISE_CLASS(className, typeName)                                                  \
  class className : public dBitwiseBase {                                                          \
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
     *  @brief Shared body for ``~bitand`` ``~bitor`` and ``~bitxor`` (issue
     *         #1051), on ``DSP::bitwise``.
     *
     *  Ported from Max's ``bitand~``, ``bitor~`` and ``bitxor~``.
     *
     *  - Left inlet: the signal processed, or a number used while no signal
     *    is connected (#1036). ``bits`` followed by up to 32 ones and zeros
     *    (most significant first) sets the mask; ``mode <0-3>`` sets the
     *    mode (Max's ``mode`` attribute).
     *  - Right inlet: the mask or a second signal. An int is the mask's bit
     *    pattern as it is; a float becomes a mask the way the mode reads the
     *    right operand (its raw bits, or the integer it converts to). A
     *    connected signal wins over the mask.
     *  - Parameters: ``mask`` (creation argument 1, default 0) and ``mode``
     *    (creation argument 2, default 0), so both survive a save.
     *
     *  Max's ``float`` and ``int`` messages map onto the right inlet, its
     *  ``bits`` message and ``mode`` attribute onto the left-inlet messages.
     *  The wrapper contains no sample math.
     */
    class dBitwiseBase : public pObject {
    public:
      void SetMessage(const std::string& message, float value) override;
      bool HandlesMessages() const override {
        return true;
      }
      void Calculate(YSE::THREAD thread) override;

      void SetLeftFloat(float value, int inlet, YSE::THREAD thread);
      void SetLeftInt(int value, int inlet, YSE::THREAD thread);
      void SetMaskFloat(float value, int inlet, YSE::THREAD thread);
      void SetMaskInt(int value, int inlet, YSE::THREAD thread);

    protected:
      explicit dBitwiseBase(DSP::bitwise::OP op); // RT-cold — constructor use.

      // Fills in the documentation that differs per operation. RT-cold.
      void Document(const char* summary, const char* outletDoc);

    private:
      aFlt leftFloatIn;
      aInt mask;
      aInt mode;
      DSP::bitwise fn;
    };

    SIGNAL_BITWISE_CLASS(dBitAnd, YSE::OBJ::D_BITAND)
    SIGNAL_BITWISE_CLASS(dBitOr, YSE::OBJ::D_BITOR)
    SIGNAL_BITWISE_CLASS(dBitXor, YSE::OBJ::D_BITXOR)

    /** @brief ``~bitsafe``: NaN and infinity become 0, on ``DSP::bitsafe``. */
    class dBitSafeBase : public dTrigBase {
    protected:
      DSP::buffer& Process(DSP::buffer& in) override {
        return fn(in);
      }

    private:
      DSP::bitsafe fn;
    };

    SIGNAL_TRIG_CLASS(dBitSafe, dBitSafeBase, YSE::OBJ::D_BITSAFE)

    /**
     *  @brief ``~bitnot``: inverts every bit of a signal, ported from Max's
     *         ``bitnot~`` (issue #1051), on ``DSP::bitnot``.
     *
     *  - Inlet: the signal, or a number used while no signal is connected
     *    (#1036). ``mode <0|1>`` picks raw bits (0) or integer conversion (1).
     *  - Parameter ``mode`` (creation argument 1, default 0).
     *
     *  Max's ``int`` and ``float`` messages set the mode; here a number is
     *  the input, as on every YSE signal inlet, and ``mode`` sets the mode.
     */
    PATCHER_CLASS(dBitNot, YSE::OBJ::D_BITNOT)
    _DO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetInputFloat)
    _INT_IN(SetInputInt)

  private:
    aFlt inputFloat;
    aInt mode;
    DSP::bitnot fn;
  };

  /**
   *  @brief ``~bitshift``: shifts the bits of a signal, ported from Max's
   *         ``bitshift~`` (issue #1051), on ``DSP::bitshift``.
   *
   *  - Inlet: the signal, or a number used while no signal is connected
   *    (#1036). ``shift <n>`` shifts n bits, left for positive n and right
   *    for negative; ``mode <0|1>`` picks raw bits (0) or integer
   *    conversion (1).
   *  - Parameters ``shift`` (creation argument 1, default 0) and ``mode``
   *    (creation argument 2, default 0).
   *
   *  Max's ``int`` and ``float`` messages set the mode; here a number is the
   *  input, as on every YSE signal inlet, and ``mode`` sets the mode.
   */
  PATCHER_CLASS(dBitShift, YSE::OBJ::D_BITSHIFT)
  _DO_MESSAGES
  _DO_CALCULATE

  _FLOAT_IN(SetInputFloat)
  _INT_IN(SetInputInt)

private:
  aFlt inputFloat;
  aInt shift;
  aInt mode;
  DSP::bitshift fn;
};

} // namespace PATCHER
} // namespace YSE

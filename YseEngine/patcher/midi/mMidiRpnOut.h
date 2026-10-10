#pragma once
// The RPN/NRPN *sender* objects (issue #534) — `.rpnout` and `.nrpnout`.
//
// Siblings of the MIDI sender family: one hot inlet that both stores and fires,
// a cold inlet for the value that rides along with it, a `channel` creation
// argument rather than Max's cold inlet, and raw three-byte lists on the outlet
// — the shape `.midiout` reads and the reason these objects exist.
//
// **No platform guard.** Neither opens a device — they only format bytes onto
// their outlets — so both are compiled and registered on every platform, like
// the senders they belong with. Issue #746 lifted the `#if YSE_WINDOWS` the
// family used to carry; only `.midiout`, which holds an RtMidi port, is still
// conditional (on `YSE_ENABLE_MIDI_DEVICE`).
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Shared body of the two RPN/NRPN senders (issue #534).
     *
     *  ### What a parameter number is for
     *
     *  MIDI has 128 control-change numbers and a synthesiser has far more than
     *  128 parameters. Registered and non-registered parameter numbers are the
     *  escape hatch: instead of addressing a parameter by a controller number,
     *  a patch *selects* a 14-bit parameter number with one pair of controllers
     *  and then writes to whatever is selected with a second pair. That is how
     *  pitch-bend range, master tuning and every synth-specific parameter on
     *  real hardware are reached, and without it a patch simply cannot talk to
     *  most of a hardware synth.
     *
     *  **Registered** parameter numbers are the handful the MIDI specification
     *  itself assigns — 0 pitch-bend sensitivity, 1 fine tuning, 2 coarse
     *  tuning, 3 tuning program select, 4 tuning bank select — and mean the same
     *  thing on every device that implements them. **Non-registered** ones mean
     *  whatever the manufacturer's chart says, which is where the interesting
     *  parameters live. The two are the same mechanism with a different
     *  selecting pair, which is why one base serves both.
     *
     *  ### The four messages
     *
     *  One value on the hot inlet emits **four** complete control-change
     *  messages, in this order:
     *
     *    1. the parameter number's coarse byte, on the selecting MSB controller
     *       (101 for RPN, 99 for NRPN);
     *    2. its fine byte, on the selecting LSB controller (100 / 98);
     *    3. the value's coarse byte, on controller 6 (Data Entry MSB);
     *    4. its fine byte, on controller 38 (Data Entry LSB).
     *
     *  Four sends rather than one twelve-byte list: `.midiout` hands each list
     *  it receives to the device as a single MIDI message, so twelve bytes
     *  together would arrive as one malformed message rather than four good
     *  ones. This is `.xctlout`'s arrangement (#533) for the same reason.
     *
     *  The selection is re-sent on **every** value rather than only when the
     *  parameter number changes. It costs two messages and it is the only way
     *  the object can be correct: the receiving device has one selected
     *  parameter, and anything else in the patch — another `.rpnout`, a
     *  `.controlchange 99`, a second sequencer on the same cable — can move it
     *  between two values from this box. A sender that assumed its selection
     *  survived would write its value into whatever was selected last, which is
     *  the failure this mechanism is notorious for.
     *
     *  ### What is not sent
     *
     *  **No RPN Null.** The specification's closing gesture — parameter number
     *  16383, which deselects — is not appended after the data entry. Max's
     *  `rpnout` does not send it, and a patch that wants it can address 16383
     *  on a `.rpnout` explicitly. Sending it unasked would also break the common
     *  case of a slider sweeping one parameter, where the selection is
     *  immediately wanted again.
     *
     *  ### Real-time behaviour
     *
     *  `Calculate()` writes into one three-character string — short-string
     *  storage, so no allocation — and sends it four times. Nothing on the path
     *  allocates, locks or blocks.
     */
    class mRpnOutBase : public pObject {
    public:
      /** @brief Data Entry MSB: the controller the value's coarse byte rides
       *         on, the same for registered and non-registered parameters. */
      static constexpr int DATA_ENTRY_MSB = 6;
      /** @brief Data Entry LSB: the value's fine byte. */
      static constexpr int DATA_ENTRY_LSB = 38;

      /** @brief The widest a 14-bit parameter number or value can be. */
      static constexpr int MAX_14BIT = 16383;

      void Calculate(YSE::THREAD thread) override;
      void SetMessage(const std::string&, float) override {}

      /** @brief The value the next send will carry, 0-16383. Diagnostic /
       *         test surface. */
      int Value() const {
        return cvalue;
      }

      /** @brief The parameter number the next send will select, 0-16383.
       *         Diagnostic / test surface. */
      int Parameter() const {
        return parameter;
      }

    protected:
      /** @param selectMsb Controller carrying the parameter number's coarse
       *                   byte — 101 for RPN, 99 for NRPN.
       *  @param selectLsb Controller carrying its fine byte — 100 / 98. */
      mRpnOutBase(int selectMsb, int selectLsb);

      _INT_IN(SetIntValue)
      _INT_IN(SetIntParameter)

    private:
      // Which pair selects. Fixed by the subclass at construction and never
      // written again, which is what makes the two objects one body.
      const int selectMsb;
      const int selectLsb;

      int cvalue;
      int parameter;
      int channel;
    };

    /**
     *  @brief `.rpnout` — write to a registered parameter number (issue #534).
     *
     *  Selects with controllers 101 and 100 and writes with 6 and 38. The
     *  registered numbers are the specification's own, so a patch that sends
     *  parameter 0 raises the pitch-bend range on any device that implements it
     *  at all, without consulting a manufacturer's chart.
     *
     *  Inlet 0 (value, 0-16383) is hot; inlet 1 (parameter number, 0-16383) is
     *  cold and stores until the next value.
     */
    class mRpnOut : public mRpnOutBase {
    public:
      mRpnOut();
      const char* Type() const override {
        return YSE::OBJ::M_RPNOUT;
      }
      CREATE(mRpnOut)
    };

    /**
     *  @brief `.nrpnout` — write to a non-registered parameter number (issue
     *         #534).
     *
     *  The same mechanism as `.rpnout` with controllers 99 and 98 selecting
     *  instead of 101 and 100. Non-registered numbers mean whatever the device's
     *  own documentation says they mean, which is where a synthesiser's filter
     *  cutoff, envelope times and oscillator settings are actually addressed
     *  from — the parameters a patch most wants and the ones no standard
     *  controller reaches.
     */
    class mNrpnOut : public mRpnOutBase {
    public:
      mNrpnOut();
      const char* Type() const override {
        return YSE::OBJ::M_NRPNOUT;
      }
      CREATE(mNrpnOut)
    };

  } // namespace PATCHER
} // namespace YSE

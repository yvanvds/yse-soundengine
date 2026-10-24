#pragma once

#include "../pObject.h"
#include "../../dsp/generators/oscillatorBank.hpp"
#include "oscillatorBankControl.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief The shared body of ``~oscbank`` and ``~ioscbank``: a bank of
     *         sine oscillators, wrapping ``DSP::oscillatorBank`` (issue
     *         #1061).
     *
     *  Ported from Max's ``oscbank~`` and ``ioscbank~``, which differ only
     *  in reading the table between points (``ioscbank~``) or not, and in
     *  ``oscbank~``'s table size. The wrapper only maps pins, messages and
     *  parameters onto the DSP class.
     *
     *  - Inlet 0, frequency in Hz; inlet 1, magnitude; inlet 2, phase (0-1);
     *    inlet 3, the index of the oscillator they set.
     *  - Signals: when any inlet carries a signal, the block sets
     *    oscillators sample by sample, oscillator ``index`` getting that
     *    sample's frequency and magnitude (an unconnected frequency,
     *    magnitude or index inlet reads its last number, issue #1036). The
     *    phase signal is used with ``framesync 1`` only.
     *  - Numbers, while no inlet carries a signal: a number on inlet 0 or 1
     *    sets the frequency or magnitude of the oscillator the index (inlet
     *    3's last number) names, in the order sent; on inlet 2, with
     *    ``framesync 1``, its phase.
     *  - Messages: ``set f m f m ...`` (the first n oscillators, every other
     *    one fades to magnitude 0), ``clear``, ``silence``, ``size <n>``,
     *    ``freqsmooth <n>``, ``magsmooth <n>``, ``framesync <0/1>`` and, on
     *    ``~oscbank``, ``tabpoints <n>``. Applied at the top of the next
     *    block; not saved (``DumpJSON`` keeps the arguments).
     *  - Parameters: ``oscillators`` (the capacity, 1 to 4096, default 64),
     *    ``freqsmooth`` (1), ``magsmooth`` (0) and, on ``~oscbank``,
     *    ``tablesize`` (4096). Changing one rebuilds the object, so the bank
     *    is allocated on the control thread.
     *
     *  Not ported: ``copybuf`` (a waveform from a buffer). Reading a named
     *  sample buffer from a message needs the background rebind ``~cycle``
     *  does (issue #1192); the DSP class takes a waveform already
     *  (``setTable``). Tracked in #1196.
     *
     *  Real time: numbers and messages travel to the audio thread through
     *  ``oscillatorBankControl``'s ring, in order, without allocating,
     *  locking or blocking.
     */
    class oscillatorBankObject : public pObject {
    public:
      explicit oscillatorBankObject(bool interpolating);

      void SetMessage(const std::string& message, float value) override;
      bool HandlesMessages() const override {
        return true;
      }
      void Calculate(YSE::THREAD thread) override;

      /** @brief Requests refused by the control ring. Diagnostics, tests. */
      unsigned int Dropped() const {
        return control.Dropped();
      }

      /** @brief The bank. Only while no block runs (tests). */
      const DSP::oscillatorBank& Bank() const {
        return bank;
      }

    private:
      void ClearParams();
      void ParseParams();
      void SetFloat(float value, int inlet, YSE::THREAD thread);
      void SetInt(int value, int inlet, YSE::THREAD thread);

      const bool interpolating;
      DSP::oscillatorBank bank;
      oscillatorBankControl control;

      // The arguments. Control thread only: read when the bank is built.
      int oscillators = static_cast<int>(DSP::oscillatorBank::DEFAULT_OSCILLATORS);
      int freqsmooth = 1;
      int magsmooth = 0;
      int tablesize = static_cast<int>(DSP::oscillatorBank::DEFAULT_TABLE_SIZE);
    };

    /** @brief ``~oscbank``: a non-interpolating oscillator bank (Max's
     *         ``oscbank~``), see ``oscillatorBankObject``. */
    class dOscbank : public oscillatorBankObject {
    public:
      dOscbank() : oscillatorBankObject(false) {}
      const char* Type() const override {
        return YSE::OBJ::D_OSCBANK;
      }
      CREATE(dOscbank)
    };

    /** @brief ``~ioscbank``: an interpolating oscillator bank (Max's
     *         ``ioscbank~``), see ``oscillatorBankObject``. */
    class dIoscbank : public oscillatorBankObject {
    public:
      dIoscbank() : oscillatorBankObject(true) {}
      const char* Type() const override {
        return YSE::OBJ::D_IOSCBANK;
      }
      CREATE(dIoscbank)
    };

  } // namespace PATCHER
} // namespace YSE

#pragma once

#include <cstddef>
#include <memory>
#include <string>
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
     *    block; not saved (``DumpJSON`` keeps the arguments). And
     *    ``copybuf`` (below).
     *  - Parameters: ``oscillators`` (the capacity, 1 to 4096, default 64),
     *    ``freqsmooth`` (1), ``magsmooth`` (0) and, on ``~oscbank``,
     *    ``tablesize`` (4096). Changing one rebuilds the object, so the bank
     *    is allocated on the control thread.
     *
     *  ### ``copybuf <buffer> [offset]`` (issue #1196)
     *
     *  The table becomes the first channel of sample buffer ``buffer``, from
     *  sample ``offset`` (0 when not given or negative): the table keeps its
     *  size (``tabpoints``, 4096 on ``~ioscbank``), samples the buffer does
     *  not have are 0, values that are not finite numbers are 0. All the
     *  oscillators share it. A name no buffer has changes nothing. A later
     *  ``tabpoints`` goes back to the sine, as ``DSP::oscillatorBank``'s
     *  ``tableSize`` does.
     *
     *  Finding the buffer and copying from it lock and allocate, and the
     *  message may arrive on the audio thread, so it goes the way ``~cycle``'s
     *  ``set`` / ``setall`` / ``buffer`` go (issue #1192): the message packs
     *  the name into a fixed record and hands it to the patcher's
     *  ``fileScheduler`` as a ``TASK``. On the background pool the task
     *  copies the samples into a new table and leaves it in a one-slot
     *  mailbox; at the top of a block, after the queued messages,
     *  ``CALC()`` takes it with one atomic exchange and swaps it into the
     *  bank (``swapTable``: no copy, no allocation). The storage it let go
     *  goes back through the mailbox and a further task frees it. The
     *  mailbox is found by id, never through the object, so a task in
     *  flight survives the object's deletion. Of several ``copybuf``\ s
     *  before a block, the last wins. As tasks run in order, a ``~buffer``
     *  message sent before a ``copybuf`` (a ``fill``, a ``read``) has
     *  happened when it copies.
     *
     *  Limits, the scheduler's: refused when the patcher has
     *  ``fileScheduler::CAPACITY`` requests in flight, takes effect only
     *  while the patcher renders, and does nothing outside a patcher. Not
     *  saved: ``DumpJSON`` keeps the arguments, and a reload starts from the
     *  sine.
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
      void SetParent(pObject* newParent) override;

      /** @brief A table let go by a ``copybuf`` and not yet freed.
       *         Diagnostics and tests. */
      bool ReleasePending() const;

      // The handoff between a copybuf task and CALC(). Defined in
      // dOscbank.cpp.
      struct stagedTable;
      struct mailbox;

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

      struct copyTask;
      // Pack `task` and hand it to the scheduler. Any thread, no allocation.
      bool Submit(copyTask& task);
      // `copybuf <name> [offset]`: true when the message was one.
      bool CopyBuffer(const std::string& message);
      // CALC(): take a waiting table, and ask for the old one to be freed.
      void TakeTable();
      // The fileTaskFunction: background pool, never touches the object.
      static bool RunTask(char* args, std::size_t argBytes);

      const bool interpolating;
      DSP::oscillatorBank bank;
      oscillatorBankControl control;
      std::shared_ptr<mailbox> box; // made at construction, never replaced

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

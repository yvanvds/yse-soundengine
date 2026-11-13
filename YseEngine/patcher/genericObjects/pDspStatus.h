#pragma once
#include <atomic>
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~dspstate``: reports the patcher's DSP switch, sample rate and
     *         block size (issue #1172).
     *
     *  Ported from Max's ``dspstate~``. An engine query, not signal
     *  processing, so there is no ``YSE::DSP`` class behind it: the two-layer
     *  rule (#1035) is waived, as the issue says. The three DSP status objects
     *  are control objects (no signal pins), in category IO.
     *
     *  - Inlet 0: ``bang`` reports.
     *  - Outlets, sent right to left: 3 the I/O block size and 2 the signal
     *    block size, both ``STANDARD_BUFFERSIZE`` (a patcher always renders
     *    and exchanges audio in blocks of that size; the device's own buffer
     *    is ``System().getActiveBufferSize()`` on the host), 1 the sample rate
     *    in Hz (``YSE::SAMPLERATE``, read when the report is sent), 0 the
     *    patcher's DSP switch, 1 on or 0 off (patcherImplementation::DspOn,
     *    the switch ``start`` / ``stop`` on a ``~dac`` turns, issue #1043).
     *  - The same report goes out by itself, at the top of the first block
     *    after the switch changes: the object is polled every block
     *    (WantsBlockPoll) and compares the switch with what it last saw.
     *
     *  Not ported: a signal into the inlet. In Max it selects that signal's
     *  rate and block size, which are the same for every signal; a bang
     *  reports them here.
     *
     *  **Real time.** A bang and a poll read atomics and send numbers: no
     *  allocation, no lock, on any thread.
     */
    PATCHER_CLASS(pDspstate, YSE::OBJ::D_DSPSTATE)
    _NO_MESSAGES
    _DO_CALCULATE
    _BANG_IN(Bang)

    /** @brief Yes: the poll is what notices the switch change. */
    bool WantsBlockPoll() const override {
      return true;
    }

    /** @brief Control thread. Seeds the switch the poll compares with, so
     *         joining a patcher reports nothing by itself. */
    void SetParent(pObject* newParent) override;

  private:
    void Report(YSE::THREAD thread);
    bool DspOn() const;

    // The switch as the poll last saw it. Seeded by SetParent before the
    // object is published; the audio thread's poll owns it after that.
    std::atomic<bool> wasOn{true};
  };

  /**
   *  @brief ``~dsptime``: reports the milliseconds of audio processed since
   *         the patcher's DSP was last turned on (issue #1172).
   *
   *  Ported from Max's ``dsptime~``. No ``YSE::DSP`` class (an engine query).
   *
   *  - Inlet 0: ``bang`` sends patcherImplementation::DspTimeMs: every
   *    rendered block adds its length at the sample rate of that block, so
   *    the count follows the samples processed, not the wall clock, and
   *    keeps time in offline rendering too. It stops while the DSP is off and
   *    restarts from 0 when the DSP comes back on. Outside a patcher it is 0.
   *  - Outlet 0: the milliseconds, as a float.
   *
   *  Not ported: a signal into the inlet (the count of that signal's samples
   *  in Max, the same count for every signal here).
   *
   *  **Real time.** A bang is one atomic load and a send.
   */
  PATCHER_CLASS(pDsptime, YSE::OBJ::D_DSPTIME)
  _NO_MESSAGES
  _NO_CALCULATE
  _BANG_IN(Bang)
};

/**
 *  @brief ``~dspstress``: reports the patcher's render load, in percent
 *         (issue #1172).
 *
 *  Ported from Max's ``dspstress~``. No ``YSE::DSP`` class (an engine
 *  query). The load is patcherImplementation::RenderLoad: the time the
 *  patcher's whole block takes to process (its drains, polls, render and
 *  ~dac mix) over the block's duration at the sample rate. The patch holding
 *  a ``~dspstress`` is what turns the timing on (GraphState::measureLoad).
 *  For the load of the engine's whole audio callback, the host has
 *  ``System().cpuLoad()``.
 *
 *  - Argument: ``interval``, the reporting interval in ms (default 250).
 *  - Inlet 0: an int or float sets the interval; 0 or less stops reporting.
 *  - Outlet 0: the mean load over the interval, in percent: 100 means the
 *    patch takes as long to process as the audio it renders, more means it
 *    cannot keep up. Sent at the top of the first block after each interval,
 *    and only when it differs from the value sent before.
 *
 *  The interval counts blocks of audio processed (at the live sample rate),
 *  not the wall clock, and it keeps counting while the patcher's DSP is off:
 *  the drains and polls still run, and their cost is what is reported.
 *
 *  **Saving.** The argument is saved; a number sent to the inlet is not.
 *
 *  **Real time.** The poll is a few float operations and an atomic load; a
 *  message is one atomic store. No allocation, no lock.
 */
PATCHER_CLASS(pDspstress, YSE::OBJ::D_DSPSTRESS)
_NO_MESSAGES
_DO_CALCULATE
_INT_IN(SetInt)
_FLOAT_IN(SetFloat)

_PARM_CLEAR
_PARM_PARSE

/** @brief The default reporting interval, in ms. */
static constexpr float DEFAULT_INTERVAL_MS = 250.f;

/** @brief Yes: the poll measures every block and reports. */
bool WantsBlockPoll() const override {
  return true;
}

/** @brief The reporting interval in ms; 0 is off. Any thread. */
float IntervalMs() const {
  return interval.load(std::memory_order_relaxed);
}

/** @brief One block's measurement: @p load (1 is 100%) for a block of
 *         @p blockMs. Adds it to the interval's mean and sends the mean
 *         when the interval is over and the mean has changed. The poll
 *         calls this with the patcher's RenderLoad; tests call it directly
 *         with known loads. The thread that polls only. */
void Feed(float load, double blockMs, YSE::THREAD thread);

private:
void SetInterval(float ms);

float intervalArg = DEFAULT_INTERVAL_MS; // the argument
std::atomic<float> interval{DEFAULT_INTERVAL_MS}; // live, any thread

// The poll's own state.
double elapsedMs = 0.0;
double loadSum = 0.0;
unsigned int blocks = 0;
float lastSent = 0.f;
bool sentAny = false;
}
;

} // namespace PATCHER
} // namespace YSE

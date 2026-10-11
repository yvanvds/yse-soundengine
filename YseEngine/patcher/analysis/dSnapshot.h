#pragma once

#include <atomic>
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~snapshot``: reports a signal's value as a float (issue #1125).
     *
     *  Ported from Max's ``snapshot~``, and the reference object for
     *  signal-to-control outlets (issue #1037): the interval report is armed
     *  in CALC() and sent by the patcher after the block's DSP traversal.
     *  There is no DSP class, because reading one sample of the input is all
     *  the signal work there is.
     *
     *  - Left inlet: the signal. A bang reports the sample at ``offset`` in
     *    the last block at once. A non-zero number starts interval reporting,
     *    zero stops it. Also takes ``start``, ``stop``, ``interval <ms>``,
     *    ``sampleinterval <samples>`` and ``offset <index>``.
     *  - Right inlet: the interval in milliseconds. Zero stops reporting.
     *  - Outlet: the reported value.
     *  - Parameters: ``interval`` in ms (creation argument 1, default 0: only
     *    a bang reports) and ``offset`` (argument 2, default 0).
     *
     *  Reporting runs while it is active (the default) and an interval is
     *  set. An interval shorter than a block reports once per block: the
     *  outlet slot keeps the latest value (see outlet::ArmFloat).
     *
     *  Not ported: the Max time-format forms of the interval (``list`` and
     *  ``anything``), because YSE has no transport time format, and the
     *  scheduler-in-audio-interrupt sample accuracy of a bang, because a
     *  patcher message carries no sample offset.
     */
    PATCHER_CLASS(dSnapshot, YSE::OBJ::D_SNAPSHOT)
    _DO_MESSAGES
    _DO_CALCULATE

    _BANG_IN(Report)
    _FLOAT_IN(SetActive)
    _INT_IN(SetActiveInt)
    _FLOAT_IN(SetInterval)
    _INT_IN(SetIntervalInt)

  private:
    aFlt interval; // ms; used while sampleInterval is 0
    aInt offset; // sample index inside the block
    aInt sampleInterval{0}; // samples; overrides interval when > 0
    std::atomic<bool> active{true};

    // Written by the audio thread, read by a bang on any thread.
    aFlt lastValue{0.f};

    // Audio thread only: samples from the start of the next block to the next
    // interval report.
    double samplesLeft = 0.0;
  };

} // namespace PATCHER
} // namespace YSE

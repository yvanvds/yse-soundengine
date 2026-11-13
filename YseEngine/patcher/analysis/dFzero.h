#pragma once

#include <atomic>
#include <string>
#include "../pObject.h"
#include "../../dsp/analysis/pitchTracker.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~fzero``: the fundamental frequency, peak amplitude and note
     *         onsets of a monophonic signal, wrapping ``DSP::pitchTracker``
     *         (issue #1130).
     *
     *  Ported from Max's ``fzero~``. Its estimator is YIN rather than Max's
     *  wavelet method (see ``DSP::pitchTracker`` for the algorithm and its
     *  latency). The reports are made in CALC() and sent by the patcher
     *  after the block's DSP traversal (issue #1037), right outlet first.
     *
     *  - Inlet: the signal to analyse; unconnected it reads as silence.
     *    Takes Max's attributes as messages: ``size``, ``period``,
     *    ``freqmin``, ``freqmax``, ``threshold``, ``onsetamp``,
     *    ``onsetpitch``, ``onsetperiod``, ``onsetlist``, and YSE's
     *    ``tolerance``, each followed by a number. ``quiet`` is accepted and
     *    does nothing: there is no console to silence.
     *  - Left outlet: the pitch in Hz, after every analysis that found one.
     *  - Middle outlet: the peak amplitude of the analysis buffer, after
     *    every analysis above the threshold.
     *  - Right outlet: a bang on an onset, or with ``onsetlist 1`` a list of
     *    the pitch at the onset (0 when none was found yet) and its peak
     *    amplitude.
     *  - Parameters: the ten settings in that order, so a patch saves them
     *    (``~fzero 1024 256``). Max's ``fzero~`` takes no arguments; these
     *    are YSE's. A later message is not saved.
     *
     *  Several analyses in one block (a period shorter than the block) send
     *  the last of each kind once. The settings reach the audio thread
     *  through atomics, handed to the class at the start of each block. The
     *  wrapper contains no sample math.
     */
    PATCHER_CLASS(dFzero, YSE::OBJ::D_FZERO)
    _DO_MESSAGES
    _DO_CALCULATE

    void FlushArmedMessages(THREAD thread) override;

  private:
    DSP::pitchTracker fn;

    std::atomic<int> size{static_cast<int>(DSP::pitchTracker::DEFAULT_SIZE)};
    std::atomic<int> period{static_cast<int>(DSP::pitchTracker::DEFAULT_PERIOD)};
    aFlt freqmin{DSP::pitchTracker::DEFAULT_MIN_FREQUENCY};
    aFlt freqmax{DSP::pitchTracker::DEFAULT_MAX_FREQUENCY};
    aFlt threshold{DSP::pitchTracker::DEFAULT_THRESHOLD};
    aFlt onsetamp{DSP::pitchTracker::DEFAULT_ONSET_AMPLITUDE};
    aFlt onsetpitch{DSP::pitchTracker::DEFAULT_ONSET_PITCH};
    std::atomic<int> onsetperiod{static_cast<int>(DSP::pitchTracker::DEFAULT_ONSET_PERIOD)};
    std::atomic<int> onsetlist{0};
    aFlt tolerance{DSP::pitchTracker::DEFAULT_TOLERANCE};

    // What CALC() found for the flush after the traversal. Audio thread
    // only: both run on the thread rendering the patcher.
    bool sendFrequency = false;
    bool sendAmplitude = false;
    bool sendOnset = false;
    float frequency = 0.f;
    float amplitude = 0.f;
    float onsetFrequency = 0.f;
    float onsetAmplitude = 0.f;
    std::string scratch; // reserved in the constructor
  };

} // namespace PATCHER
} // namespace YSE

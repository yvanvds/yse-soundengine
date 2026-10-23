#pragma once

#include <atomic>
#include <string>
#include "../pObject.h"
#include "../../dsp/generators/cycle.hpp"
#include "../../dsp/sampling/sampleBuffer.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~cycle``: a wavetable oscillator with a phase inlet, wrapping
     *         ``DSP::cycle`` (issue #1054).
     *
     *  Ported from Max's ``cycle~``. The wrapper only maps pins and
     *  parameters onto the DSP class; the oscillator is ``DSP::cycle``.
     *
     *  - Left inlet: frequency in Hz, as a signal or a number (the number is
     *    used while no signal is connected, see #1036). Also takes
     *    ``reset [phase]`` (restart the running phase there, 0 without a
     *    number), ``frequency <hz>``, ``phase <0-1>``,
     *    ``buffer_offset <samples>`` and ``buffer_sizeinsamps <samples>``.
     *  - Right inlet: phase in cycles, added to the running phase, as a signal
     *    or a number (the ``phase`` attribute).
     *  - Parameters: ``frequency`` (argument 1), ``buffer`` (argument 2, the
     *    name of a sample buffer whose first channel is the waveform; none
     *    means the built-in cosine), ``offset`` (argument 3, the first sample
     *    of the table) and ``size`` (argument 4, how many samples: 0 is 512,
     *    -1 the whole buffer from the offset).
     *
     *  The buffer is read as the sampling objects read it (issue #1039): a
     *  ``DSP::sampleBufferReader`` bound in ``PARM_PARSE`` on the control
     *  thread and pinned once per block. A buffer that is reloaded or resized
     *  is followed from the next block on, which is Max's
     *  ``buffer_autoupdate`` always on.
     *
     *  Not ported: ``set``, ``setall`` and the ``buffer`` attribute as
     *  messages (binding another buffer would allocate and lock on whichever
     *  thread sent it; change the argument instead, which rebuilds the
     *  object; the ``size`` argument gives ``setall``'s whole-buffer table),
     *  ``buffer_autoupdate`` (always on) and ``mouse`` (host-side display).
     *
     *  Control-thread values reach the audio thread through atomics; a reset
     *  is posted as a request and applied at the top of the next block.
     */
    PATCHER_CLASS(dCycle, YSE::OBJ::D_CYCLE)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_PARSE

    _FLOAT_IN(SetFrequency)
    _INT_IN(SetFrequencyInt)
    _FLOAT_IN(SetPhase)
    _INT_IN(SetPhaseInt)

    /** @brief Whether a buffer name was given. Control thread, tests. */
    bool UsesBuffer() const {
      return useBuffer;
    }

  private:
    DSP::cycle oscillator;
    DSP::sampleBufferReader reader;

    aFlt frequency;
    std::string bufferName;
    std::atomic<int> offset{0};
    std::atomic<int> size{0};

    aFlt phase;

    // A reset waiting for the audio thread, exchanged in CALC() so it is
    // applied once.
    aFlt pendingReset;
    std::atomic<bool> resetPending{false};

    // Set in PARM_PARSE, before the object is published; read-only after.
    bool useBuffer = false;
  };

} // namespace PATCHER
} // namespace YSE

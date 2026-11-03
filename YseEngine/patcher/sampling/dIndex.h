#pragma once

#include <atomic>
#include <memory>
#include <string>
#include "../pObject.h"
#include "../../dsp/sampling/sampleAccess.hpp"
#include "../../dsp/sampling/sampleBuffer.hpp"
#include "bufferBinding.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~index``: reads a named sample buffer at sample indexes given
     *         by a signal, without interpolation, wrapping
     *         ``DSP::sampleIndexReader`` (issue #1145).
     *
     *  Ported from Max's ``index~``. The wrapper only maps pins and
     *  parameters onto the DSP class.
     *
     *  - Arguments: ``name`` (the buffer), ``channel`` (from 1, default 1).
     *  - Inlet 0: the index to read at, in samples from 0, as a signal or a
     *    number (the number is used while no signal is connected, see
     *    #1036). Truncated to a whole sample; below 0 reads the first sample
     *    and past the end the last.
     *  - Inlet 1: the channel, from 1. A float is truncated.
     *  - Outlet 0: the samples read. Silence for a channel the buffer lacks,
     *    or without a buffer.
     *
     *  ### Threads
     *
     *  ``CALC()`` reads through a ``DSP::sampleBufferReader`` pinned once
     *  per block, which allocates nothing and takes no lock. No message
     *  reads the buffer, so the reader belongs to the audio thread alone.
     *
     *  ### Rebinding: ``set <name>``
     *
     *  ``set <name>`` in the left inlet reads buffer ``name`` from then on;
     *  ``set`` with no name leaves the object unbound (silence). The reader
     *  is bound in the background through a ``bufferBinding`` (see
     *  bufferBinding.h, #1256): the message only hands the name to the
     *  patcher's ``fileScheduler``, and the top of a later ``CALC()`` takes
     *  the new reader. The swap happens only while the patcher renders, and
     *  outside a patcher ``set`` does nothing.
     *
     *  **Saving.** A ``set`` is not saved: ``DumpJSON`` writes the ``name``
     *  argument the object was created with.
     *
     *  Not ported: ``mouse`` (Max opens an editor window on a double click;
     *  the host draws, the engine has no window), and ``index~``'s right
     *  outlet, a signal of the channel whose only use is order forcing into
     *  ``poke~``. ``~poke``'s channel inlet takes numbers, not a signal, so
     *  the cord could not be made.
     */
    PATCHER_CLASS(dIndex, YSE::OBJ::D_INDEX)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_PARSE

    _FLOAT_IN(SetIndex)
    _INT_IN(SetIndexInt)
    _FLOAT_IN(SetChannelFloat)
    _INT_IN(SetChannel)

    void SetParent(pObject* newParent) override;

    /** @brief The buffer read, or null without a name. Only while no block
     *         runs (tests). */
    const std::shared_ptr<DSP::sampleBuffer>& Buffer() const {
      return binding.Current().buffer();
    }

    /** @brief A reader let go by a ``set`` and not yet freed. Tests. */
    bool ReleasePending() const {
      return binding.ReleasePending();
    }

    /** @brief The channel, from 1. */
    int CurrentChannel() const {
      return channel.load(std::memory_order_relaxed);
    }

  private:
    void StoreChannel(int value);

    // Taken and pinned by CALC() only.
    bufferBinding<DSP::sampleBufferReader> binding;
    DSP::sampleIndexReader reader;

    std::string bufferName;
    std::atomic<int> channel{1};

    // The index used while no signal is connected.
    aFlt constIndex;
  };

} // namespace PATCHER
} // namespace YSE

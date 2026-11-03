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
     *  @brief ``~poke``: writes a signal into a named sample buffer at
     *         signal-rate indexes, wrapping ``DSP::sampleIndexWriter``
     *         (issue #1144).
     *
     *  Ported from Max's ``poke~``. The wrapper only maps pins and
     *  parameters onto the DSP class.
     *
     *  - Arguments: ``name`` (the buffer), ``channel`` (from 1, default 1).
     *  - Inlet 0: the values to write, as a signal or a number (the number is
     *    used while no signal is connected, see #1036). A list
     *    ``value index [channel]`` writes one value at once; a channel given
     *    there becomes the channel from then on.
     *  - Inlet 1: the index to write each value at, in samples from 0, as a
     *    signal or a number. Truncated to a whole sample. Below 0 (``-1``,
     *    the default) or past the end of the buffer writes nothing.
     *  - Inlet 2: the channel, from 1.
     *  - No outlets: read the buffer back with ``~peek`` or a player.
     *
     *  Two numbers and no signal write the value at the index every block,
     *  which is a constant signal at a constant index.
     *
     *  ### Threads
     *
     *  ``CALC()`` writes through a ``DSP::sampleBufferWriter`` pinned once
     *  per block: in place, without a lock, a copy or an allocation, so
     *  every reader of the buffer sees the samples at once. The list
     *  arrives on whichever thread sends it and writes through a second
     *  writer of its own, so it never shares a claim with the audio thread;
     *  list handlers on two threads at once share that second writer
     *  through the usual ``exchange(true)`` guard, and the one that loses is
     *  dropped.
     *
     *  ### Rebinding: ``set <name>`` (issue #1256)
     *
     *  ``set <name>`` in the left inlet writes buffer ``name`` from then on;
     *  ``set`` with no name leaves the object unbound. Both writers are bound
     *  in the background through one ``bufferBinding`` (see
     *  bufferBinding.h): the message only hands the name to the patcher's
     *  ``fileScheduler``, and the top of a later ``CALC()`` takes the new
     *  pair with one exchange. The list path shares the pair, so ``CALC()``
     *  takes it under the list's guard and, while a list holds the guard,
     *  tries again next block. The writers let go are freed by a further
     *  task, never on the audio thread. The swap happens only while the
     *  patcher renders, and outside a patcher ``set`` does nothing.
     *
     *  **Saving.** A ``set`` is not saved: ``DumpJSON`` writes the ``name``
     *  argument the object was created with, and a reloaded patch (or a
     *  live ``SetParams`` rebuild) writes that buffer again.
     *
     *  Not ported: ``mouse`` (Max opens an editor window on a double click;
     *  the host draws, the engine has no window).
     */
    PATCHER_CLASS(dPoke, YSE::OBJ::D_POKE)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_PARSE

    _FLOAT_IN(SetValue)
    _INT_IN(SetValueInt)
    _LIST_IN(List)
    _FLOAT_IN(SetIndex)
    _INT_IN(SetIndexInt)
    _FLOAT_IN(SetChannelFloat)
    _INT_IN(SetChannel)

    void SetParent(pObject* newParent) override;

    /** @brief The buffer written, or null without a name. Only while no
     *         block runs (tests). */
    const std::shared_ptr<DSP::sampleBuffer>& Buffer() const {
      return binding.Current().signal.buffer();
    }

    /** @brief Writers let go by a ``set`` and not yet freed. Tests. */
    bool ReleasePending() const {
      return binding.ReleasePending();
    }

    // The audio thread's claim and the list's, bound to one buffer together.
    struct writers {
      DSP::sampleBufferWriter signal;
      DSP::sampleBufferWriter message; // pinned only by the guard's holder
      void bind(const std::shared_ptr<DSP::sampleBuffer>& buffer) {
        signal.bind(buffer);
        message.bind(buffer);
      }
    };

    /** @brief The channel, from 1. */
    int CurrentChannel() const {
      return channel.load(std::memory_order_relaxed);
    }

  private:
    void StoreChannel(int value);

    // Taken by CALC() under `busy`; the list reads it under `busy`.
    bufferBinding<writers> binding;
    DSP::sampleIndexWriter signalPath;
    DSP::sampleIndexWriter messagePath; // used only by the guard's holder
    std::atomic<bool> busy{false};

    std::string bufferName;
    std::atomic<int> channel{1};

    // The numbers used while no signal is connected.
    aFlt constValue;
    aFlt constIndex;
  };

} // namespace PATCHER
} // namespace YSE

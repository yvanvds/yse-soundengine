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
     *  @brief ``~peek``: reads and writes single samples of a named sample
     *         buffer by message, wrapping ``DSP::sampleAccess`` (issue #1144).
     *
     *  Ported from Max's ``peek~``. A control object: indexes and values
     *  arrive as numbers and the answer leaves as a number, so it works with
     *  the audio stopped too.
     *
     *  - Arguments: ``name`` (the buffer), ``channel`` (from 1, default 1),
     *    ``clip`` (1, the default, limits written values to -1 to 1; 0 writes
     *    them as they are).
     *  - Inlet 0: an int or float index (a float is truncated). The sample
     *    there goes out of the outlet; when a value arrived in inlet 1 since
     *    the last index, that value is written there instead and nothing is
     *    sent. A list ``index value [channel]`` writes at once, and a channel
     *    given there becomes the channel from then on. ``clip <0|1>`` turns
     *    clipping off or on.
     *  - Inlet 1: the value the next index writes.
     *  - Inlet 2: the channel, from 1.
     *  - Outlet 0: the sample read.
     *
     *  An index or channel outside the buffer reads and writes nothing.
     *
     *  ### Threads
     *
     *  A message may arrive on the audio thread, a timer thread or the
     *  control thread. Every read and write goes through a
     *  ``DSP::sampleBufferWriter`` bound when the object is made: pinning it
     *  allocates nothing and takes no lock, and the write lands in the
     *  buffer in place, so every reader of the buffer (a ``~poke``, a
     *  player) sees it at once. One writer is one claim, so the handlers
     *  share it through the usual ``exchange(true)`` guard: a message that
     *  arrives while another thread is inside this object is dropped, as
     *  for the other stateful objects. The value is sent after the guard is
     *  let go, so a reply that comes back into this object is not.
     *
     *  ### Rebinding: ``set <name>`` (issue #1256)
     *
     *  ``set <name>`` reads and writes buffer ``name`` from then on; ``set``
     *  with no name leaves the object unbound. The writer is bound in the
     *  background through a ``bufferBinding`` (see bufferBinding.h): the
     *  message only hands the name to the patcher's ``fileScheduler``, and
     *  the next index or list takes the new writer, under the guard, before
     *  it reads or writes. The writer let go is freed by a further task,
     *  never on the thread that sent the message. Outside a patcher ``set``
     *  does nothing.
     *
     *  **Saving.** A ``set`` is not saved: ``DumpJSON`` writes the ``name``
     *  argument the object was created with, and a reloaded patch (or a
     *  live ``SetParams`` rebuild) reads that buffer again.
     *
     *  Not ported: ``mouse`` (Max opens an editor window on a double click;
     *  the host draws, the engine has no window).
     */
    PATCHER_CLASS(dPeek, YSE::OBJ::D_PEEK)
    _DO_MESSAGES
    _NO_CALCULATE

    _PARM_PARSE

    _INT_IN(Index)
    _FLOAT_IN(IndexFloat)
    _LIST_IN(List)
    _FLOAT_IN(Value)
    _INT_IN(ValueInt)
    _FLOAT_IN(ChannelFloat)
    _INT_IN(Channel)

    void SetParent(pObject* newParent) override;

    /** @brief The buffer read and written, or null without a name. Only
     *         while no message is being handled (tests). */
    const std::shared_ptr<DSP::sampleBuffer>& Buffer() const {
      return binding.Current().buffer();
    }

    /** @brief A writer let go by a ``set`` and not yet freed. Tests. */
    bool ReleasePending() const {
      return binding.ReleasePending();
    }

    /** @brief The channel, from 1. */
    int CurrentChannel() const {
      return channel.load(std::memory_order_relaxed);
    }

    /** @brief Whether written values are clipped to -1 to 1. */
    bool Clipping() const {
      return clip.load(std::memory_order_relaxed) != 0;
    }

  private:
    // Read at `index`, or write the waiting value there. Any thread.
    void Access(int index, YSE::THREAD thread);
    // Write `value` at `index`. Any thread.
    void Write(int index, float value);
    void SetChannel(int value);

    // Taken and pinned only by the guard's holder.
    bufferBinding<DSP::sampleBufferWriter> binding;
    DSP::sampleAccess access; // used only by the guard's holder

    std::string bufferName;
    std::atomic<int> channel{1};
    std::atomic<int> clip{1};

    // A value from inlet 1, written by the next index.
    aFlt pendingValue;
    std::atomic<bool> pending{false};

    // The handlers' guard over `binding` and `access`.
    std::atomic<bool> busy{false};
  };

} // namespace PATCHER
} // namespace YSE

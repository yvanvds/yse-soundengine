#pragma once

#include <atomic>
#include <memory>
#include <string>
#include "../pObject.h"
#include "../../dsp/sampling/sampleBuffer.hpp"
#include "../../dsp/sampling/transferLookup.hpp"
#include "bufferBinding.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~lookup``: uses part of a named sample buffer as a transfer
     *         function, a signal from -1 to 1 looking up a value in it,
     *         wrapping ``DSP::transferLookup`` (issue #1147).
     *
     *  Ported from Max's ``lookup~``: waveshaping with a curve drawn into a
     *  buffer. The wrapper only maps pins and parameters onto the DSP class.
     *
     *  - Arguments: ``name`` (the buffer), ``offset`` (the first frame of
     *    the table, the one -1 reads, default 0), ``size`` (the frame the
     *    table ends before, counted from the start of the buffer, default
     *    512: the table holds ``size - offset`` entries, as in Max),
     *    ``chan`` (the buffer channel, from 1, default 1).
     *  - Inlet 0: the signal looked up, -1 to 1, or a number used while no
     *    signal is connected (#1036). Beyond -1 or 1 outputs 0. Messages:
     *    ``set <name>``, ``chan <n>``, ``offset <n>``, ``size <n>``.
     *  - Inlet 1: the offset, a number or a signal (its first sample each
     *    block). A connected signal wins over the number.
     *  - Inlet 2: the size, the same way.
     *  - Outlet 0: the table's value at each input, a straight line between
     *    two entries. Silence without a buffer or a table.
     *
     *  ### Threads
     *
     *  ``CALC()`` reads through a ``DSP::sampleBufferReader`` pinned once
     *  per block, which allocates nothing and takes no lock. Numbers and
     *  messages store atomics that ``CALC()`` hands to the class, so no
     *  message reads the buffer.
     *
     *  ### Rebinding: ``set <name>``
     *
     *  As on ``~index``: the name goes to the patcher's ``fileScheduler``
     *  through a ``bufferBinding`` (#1256), and the top of a later
     *  ``CALC()`` takes the new reader. ``set`` with no name unbinds.
     *
     *  **Saving.** Arguments are saved; messages and numbers are not (as on
     *  ``~table``): ``DumpJSON`` writes the arguments the object was created
     *  with, whatever ``set``, ``chan``, ``offset`` or ``size`` changed
     *  since.
     *
     *  Not ported: ``mouse`` (Max opens an editor window on a double click;
     *  the host draws, the engine has no window).
     */
    PATCHER_CLASS(dLookup, YSE::OBJ::D_LOOKUP)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_PARSE

    _FLOAT_IN(SetInput)
    _INT_IN(SetInputInt)
    _FLOAT_IN(SetOffsetFloat)
    _INT_IN(SetOffset)
    _FLOAT_IN(SetSizeFloat)
    _INT_IN(SetSize)

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
    /** @brief The offset and size, as the last argument, number or
     *         message set them (a connected signal is not stored). */
    int CurrentOffset() const {
      return tableOffset.load(std::memory_order_relaxed);
    }
    int CurrentSize() const {
      return tableSize.load(std::memory_order_relaxed);
    }

  private:
    // Taken and pinned by CALC() only.
    bufferBinding<DSP::sampleBufferReader> binding;
    DSP::transferLookup fn;

    std::string bufferName;
    std::atomic<int> tableOffset{0};
    std::atomic<int> tableSize{DSP::transferLookup::DEFAULT_SIZE};
    std::atomic<int> channel{1};

    // The input used while no signal is connected.
    aFlt constInput;
  };

} // namespace PATCHER
} // namespace YSE

#pragma once

#include <atomic>
#include <memory>
#include <string>
#include "../pObject.h"
#include "../../dsp/sampling/firFilter.hpp"
#include "../../dsp/sampling/sampleBuffer.hpp"
#include "bufferBinding.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~buffir``: a FIR filter whose taps are part of a named
     *         sample buffer, wrapping ``DSP::firFilter`` (issue #1149).
     *
     *  Ported from Max's ``buffir~``: the input convolved with up to 4096
     *  samples of a buffer, for custom filters and short impulse responses.
     *  The wrapper only maps pins and parameters onto the DSP class.
     *
     *  - Arguments: ``name`` (the buffer), ``offset`` (the frame of the
     *    first tap, default 0), ``size`` (the number of taps, default 0 as
     *    in Max, at most 4096), ``chan`` (the buffer channel, from 1,
     *    default 1).
     *  - Inlet 0: the signal filtered, or a number used while no signal is
     *    connected (#1036). Messages: ``set <name> [offset] [size]``,
     *    ``clear``, ``chan <n>``, ``offset <n>``, ``size <n>``.
     *  - Inlet 1: the offset, a number or a signal (its first sample each
     *    block). A connected signal wins over the number.
     *  - Inlet 2: the size, the same way.
     *  - Outlet 0: the filtered signal. Silence without taps.
     *
     *  ### Threads
     *
     *  ``CALC()`` reads the taps through a ``DSP::sampleBufferReader``
     *  pinned once per block, which allocates nothing and takes no lock.
     *  Numbers and messages store atomics that ``CALC()`` hands to the
     *  class; ``clear`` raises a flag the next block acts on, so the history
     *  is only touched by the audio thread.
     *
     *  ### Rebinding: ``set <name> [offset] [size]``
     *
     *  As on ``~lookup``: the name goes to the patcher's ``fileScheduler``
     *  through a ``bufferBinding`` (#1256), and the top of a later
     *  ``CALC()`` takes the new reader. Numbers after the name set the
     *  offset and then the size; one left out keeps its value. ``set`` with
     *  no name unbinds.
     *
     *  **Saving.** Arguments are saved; messages and numbers are not (as on
     *  ``~lookup``): ``DumpJSON`` writes the arguments the object was
     *  created with, whatever ``set``, ``chan``, ``offset`` or ``size``
     *  changed since.
     *
     *  Not ported: ``mouse`` (Max opens a window showing the buffer on a
     *  double click; the host draws, the engine has no window).
     */
    PATCHER_CLASS(dBuffir, YSE::OBJ::D_BUFFIR)
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
      return tapOffset.load(std::memory_order_relaxed);
    }
    int CurrentSize() const {
      return tapSize.load(std::memory_order_relaxed);
    }

  private:
    // Taken and pinned by CALC() only.
    bufferBinding<DSP::sampleBufferReader> binding;
    // Run by CALC() only: it holds the history.
    DSP::firFilter fir;

    std::string bufferName;
    std::atomic<int> tapOffset{0};
    std::atomic<int> tapSize{0};
    std::atomic<int> channel{1};
    // `clear` waiting for the next block.
    std::atomic<bool> clearPending{false};

    // The input used while no signal is connected.
    aFlt constInput;
  };

} // namespace PATCHER
} // namespace YSE

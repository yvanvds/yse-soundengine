#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/sampling/stutter.hpp"
#include "../../dsp/sampling/sampleBuffer.hpp"
#include "bufferBinding.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~stutter``: records its input all the time and, on a
     *         trigger, replays the most recent fragment at one or more
     *         phases, wrapping ``DSP::stutter`` (issue #1150).
     *
     *  Ported from Max's ``stutter~``, for stutter and glitch effects. The
     *  wrapper only maps pins, messages and parameters onto the DSP class.
     *
     *  - Parameter ``stutter``, Max's five arguments: the record buffer in
     *    samples (default 88200, at most 4194304), the samples a trigger
     *    copies (default 4410), the trigger polarity (above 0 positive,
     *    the default; 0 or below negative), the samples copied per block
     *    (default 0: all at once), the number of outputs (1-30, default 1).
     *    A list parameter, so new arguments rebuild the object and the
     *    buffers are allocated off the audio thread.
     *  - Inlet 0: the signal recorded (unconnected it records silence). A
     *    bang copies at the next block; an int or float sets the samples a
     *    trigger copies. Messages: ``clear``, ``maxsize <n>``,
     *    ``polarity <0|1>``, ``ampvar <0-1>``, ``dropout <%>``,
     *    ``repeat <%>``, ``setbuf <name> [offset] [chan]``.
     *  - Inlet 1: the trigger signal, or a number used while no signal is
     *    connected (#1036).
     *  - Inlets 2 and on: one phase per output, 0 to 1 across the fragment,
     *    a signal or a number used while no signal is connected.
     *  - Outlets: one per output, the fragment read at its phase.
     *
     *  ### Threads
     *
     *  Numbers and messages store atomics ``CALC()`` hands to the class;
     *  a bang, ``clear`` and ``setbuf`` raise flags the next block acts on,
     *  so the buffers are only touched by the audio thread. ``setbuf``
     *  binds its buffer through a ``bufferBinding`` (#1256): the name is
     *  looked up in the background and the block that takes the writer
     *  writes the fragment, as it is then, into the buffer from frame
     *  ``offset`` (default 0) of channel ``chan`` (from 1, default 1), all
     *  at once.
     *
     *  **Saving.** Arguments are saved; messages and numbers are not.
     *
     *  Not ported: ``print`` (Max prints the settings to its console; the
     *  engine has none, and the host reads the settings from the patch).
     *  ``maxsize`` is held to the record buffer the first argument
     *  allocated, so the audio thread never allocates.
     */
    PATCHER_CLASS(dStutter, YSE::OBJ::D_STUTTER)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _BANG_IN(Bang)
    _FLOAT_IN(SetSizeFloat)
    _INT_IN(SetSize)
    _FLOAT_IN(SetTrigger)
    _INT_IN(SetTriggerInt)
    _FLOAT_IN(SetPhase)
    _INT_IN(SetPhaseInt)

    void SetParent(pObject* newParent) override;

    /** @brief The DSP class. Diagnostics and tests; only while no block
     *         runs. */
    const DSP::stutter& Stutter() const {
      return fn;
    }

    /** @brief The buffer the last ``setbuf`` writes to, or null. Only
     *         while no block runs (tests). */
    const std::shared_ptr<DSP::sampleBuffer>& SetbufBuffer() const {
      return writer.Current().buffer();
    }

    /** @brief The samples a trigger copies, as the last argument, number
     *         or message set it. */
    int CurrentSize() const {
      return copySize.load(std::memory_order_relaxed);
    }
    /** @brief The largest size, as the first argument or ``maxsize`` set
     *         it. */
    int CurrentMaxSize() const {
      return maxSize.load(std::memory_order_relaxed);
    }

  private:
    // Rebuild the DSP class and the pins from the arguments. Control
    // thread, before the object is wired or published.
    void ShapePorts();

    // Run by CALC() only.
    DSP::stutter fn;
    std::array<DSP::stutter::signal, DSP::stutter::MAX_OUTPUTS> phases{};
    // Taken and pinned by CALC() only.
    bufferBinding<DSP::sampleBufferWriter> writer;

    std::vector<std::string> arguments;

    std::atomic<int> copySize{0};
    std::atomic<int> maxSize{0};
    std::atomic<int> copyRate{0};
    std::atomic<bool> positive{true};
    aFlt ampVar{0.f};
    aFlt dropout{0.f};
    aFlt repeat{0.f};
    aFlt trigger{0.f};
    std::array<aFlt, DSP::stutter::MAX_OUTPUTS> phaseConst{};

    std::atomic<bool> bangPending{false};
    std::atomic<bool> clearPending{false};
    // setbuf requests whose writer has not been taken yet.
    std::atomic<int> setbufPending{0};
    std::atomic<int> setbufOffset{0};
    std::atomic<int> setbufChannel{1};
  };

} // namespace PATCHER
} // namespace YSE

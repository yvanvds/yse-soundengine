#pragma once

#include <atomic>
#include <memory>
#include <string>
#include "../pObject.h"
#include "../../dsp/sampling/groovePlayer.hpp"
#include "../../dsp/sampling/sampleBuffer.hpp"
#include "bufferBinding.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~phasegroove``: plays the loop of a named sample buffer where
     *         a 0 to 1 ramp says, wrapping ``DSP::groovePlayer::phase`` (issue
     *         #1142).
     *
     *  Ported from Max's ``phasegroove~``, which turns a ramp into the speed
     *  signal of a ``groove~`` it finds through its patch cord. A YSE object
     *  does not look at what it is connected to, and the player already lives
     *  in ``DSP::groovePlayer``, so here the ramp drives the player itself:
     *  there is no ``~groove`` to connect, and the ramp lands on its loop
     *  points exactly instead of drifting by a speed. The wrapper only maps
     *  pins, messages and parameters onto the DSP class.
     *
     *  - Arguments: ``name channels loopinterp interptime loopstart loopend``,
     *    as on ``~groove``.
     *  - Inlet 0: the phase signal, 0 at the loop start to 1 at the loop end.
     *    Unconnected, it reads as the last number sent, else 0 (#1036). The
     *    words ``set``, ``setloop <start> <end>``, ``reset``, ``loopinterp``,
     *    ``interptime``, ``loopstart`` and ``loopend`` arrive here too.
     *  - Inlet 1: the loop start, inlet 2: the loop end, in ms (0 is the
     *    buffer's end). A number sets it; a connected signal sets it once
     *    per block, from the block's first sample.
     *  - Outlets 0 to ``channels - 1``: the buffer's channels. Outlet
     *    ``channels``: the sync signal, the phase read, kept in 0 to 1.
     *
     *  ### Threads
     *
     *  ``CALC()`` reads through a ``DSP::sampleBufferReader`` pinned once per
     *  block. No message touches the player: the phase, the attributes and
     *  the loop points are atomics the block passes to the class.
     *
     *  ### Rebinding: ``set <name>`` (issue #1256)
     *
     *  Through a ``bufferBinding`` (bufferBinding.h), as ``~groove``: the
     *  message hands the name to the patcher's ``fileScheduler``, and the top
     *  of a later ``CALC()`` takes the new reader. ``set`` alone plays
     *  silence. Outside a patcher ``set`` does nothing.
     *
     *  **Saving.** The arguments are saved; messages and numbers are not.
     *
     *  Not ported: ``conflict`` chooses, in Max, whether the object uses its
     *  non-timestretching algorithm while its ``groove~`` timestretches, for a
     *  stuttering effect. The player has no timestretching (#1152 / #1153), so
     *  the phase path here is that algorithm, always.
     */
    PATCHER_CLASS(dPhaseGroove, YSE::OBJ::D_PHASEGROOVE)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_PARSE

    _FLOAT_IN(SetPhase)
    _INT_IN(SetPhaseInt)
    _FLOAT_IN(SetStart)
    _INT_IN(SetStartInt)
    _FLOAT_IN(SetEnd)
    _INT_IN(SetEndInt)

    void SetParent(pObject* newParent) override;

    /** @brief The buffer played, or null without a name. Only while no
     *         block runs (tests). */
    const std::shared_ptr<DSP::sampleBuffer>& Buffer() const {
      return binding.Current().buffer();
    }

    /** @brief A reader let go by a ``set`` and not yet freed. Tests. */
    bool ReleasePending() const {
      return binding.ReleasePending();
    }

    /** @brief The DSP class. Only while no block runs (tests). */
    const DSP::groovePlayer& Player() const {
      return player;
    }

    /** @brief The number of sample outlets (the sync outlet follows). */
    int Channels() const {
      return channels;
    }

  private:
    void ShapePorts();

    // Taken and pinned by CALC() only.
    bufferBinding<DSP::sampleBufferReader> binding;
    DSP::groovePlayer player;

    std::string bufferName;
    // The pins follow it. Written by PARM_PARSE only, before any audio
    // thread sees the object (a change of argument rebuilds it).
    int channels = 1;
    aInt loopInterp;
    aFlt interpTime; // ms
    aFlt loopStart; // ms
    aFlt loopEnd; // ms; 0 is the end of the buffer
    aFlt constPhase; // the phase while nothing is connected
  };

} // namespace PATCHER
} // namespace YSE

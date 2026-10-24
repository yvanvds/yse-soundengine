#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/generators/techno.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~techno``: a step sequencer read by a phase signal, wrapping
     *         ``DSP::techno`` (issue #1068).
     *
     *  Ported from Max's ``techno~``. The wrapper only maps pins, messages
     *  and parameters onto the DSP class.
     *
     *  - Inlet: the phase, 0..1 over the whole sequence (a ``~phasor``); a
     *    number is a constant phase while no signal is connected (#1036).
     *  - Messages: ``pitch``, ``amplitude``, ``curve``, ``attack``,
     *    ``decay`` and ``pos``, each ``<step> <value>``; ``length <n>``;
     *    ``repeatpos <v> ...``. They map one to one onto the DSP class's
     *    setters and ``repeatPositions``. A step number is truncated; one
     *    below 0 or at or above the capacity is ignored.
     *  - Outlets: the pitch (Hz), the amplitude envelope, and the position
     *    in steps.
     *  - Parameter: ``steps`` (the capacity, 1 to ``DSP::techno::MAX_STEPS``,
     *    default 16). Changing it rebuilds the object, so the step arrays
     *    are allocated on the control thread. The length starts at 1.
     *
     *  All eight of Max's messages are ported. ``steps`` is YSE's own: Max's
     *  ``techno~`` takes no argument, and here the step arrays are
     *  preallocated to a size the patch chooses.
     *
     *  Real time: messages become events in a ring allocated when the
     *  object is built. Senders take a non-blocking flag among themselves
     *  (a message can come from the control thread and from a rendering
     *  graph alike), write their events and publish them with one store; a
     *  sender that loses the flag, or finds the ring too full for its whole
     *  request, does nothing and is counted in ``Dropped()``. The audio
     *  thread applies everything published at the top of its block, in the
     *  order sent, so a ``length`` followed by ``pos`` messages applies in
     *  that order. Messages are not saved; the argument is.
     */
    PATCHER_CLASS(dTechno, YSE::OBJ::D_TECHNO)
    _DO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetPhase)
    _INT_IN(SetPhaseInt)
    _PARM_CLEAR
    _PARM_PARSE

    /** @brief Requests refused: another sender held the ring, or it was
     *         too full. Diagnostics and tests. */
    unsigned int Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    /** @brief The sequencer. Only while no block runs (tests). */
    const DSP::techno& Sequencer() const {
      return sequencer;
    }

  private:
    enum class kind : std::uint8_t {
      PITCH,
      AMPLITUDE,
      CURVE,
      ATTACK,
      DECAY,
      POSITION,
      LENGTH,
      REPEAT_VALUE,
      REPEAT,
    };

    struct event {
      kind what = kind::LENGTH;
      UInt step = 0;
      Flt value = 0.f;
    };

    // Size the ring and the repeat staging for the capacity and forget
    // anything queued. Control thread, before the object is published.
    void Build();
    bool Room(UInt count) const;
    void Push(kind what, UInt step, Flt value);
    void PushRepeat(const char* text, std::size_t from, std::size_t length);
    // Audio thread, top of the block.
    void Apply();

    DSP::techno sequencer;

    std::vector<event> ring;
    UInt mask = 0;
    std::atomic<UInt> head{0}; // the next event the audio thread applies
    std::atomic<UInt> tail{0}; // one past the last published event
    std::atomic_flag writing = ATOMIC_FLAG_INIT;
    std::atomic<unsigned int> dropped{0};

    // Audio thread only: a repeatpos pattern as its events arrive.
    std::vector<Flt> repeatStage;

    aFlt phase;

    // The argument. Control thread only: read when the sequencer is built.
    int steps = static_cast<int>(DSP::techno::DEFAULT_STEPS);
  };

} // namespace PATCHER
} // namespace YSE

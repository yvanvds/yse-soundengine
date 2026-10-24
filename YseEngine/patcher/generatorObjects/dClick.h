#pragma once

#include <array>
#include <atomic>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/generators/click.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~click``: an impulse, or a short stored waveform, on a bang,
     *         wrapping ``DSP::click`` (issue #1058).
     *
     *  Ported from Max's ``click~``. The wrapper only maps the inlet, the
     *  messages and the parameter onto the DSP class; the signal is
     *  ``DSP::click``'s.
     *
     *  - Inlet: ``bang`` plays the waveform once; an ``int`` or a ``float``
     *    plays it scaled by the number. ``set <values...>`` stores a new
     *    waveform of up to 256 values (``set`` alone goes back to the unit
     *    impulse). A signal triggers it, unscaled, at every sample where the
     *    signal moves from 0 to a value that is not 0.
     *  - Outlet: the clicks, silence between them.
     *  - Parameter: ``waveform``, the creation arguments (Max's
     *    ``wavetable-values``): the stored waveform, a list of up to 256
     *    numbers. None is the unit impulse, the single sample 1.
     *
     *  ### Timing
     *
     *  A bang or a number lands at the first sample of the next block that
     *  renders, so it is block-accurate. A patcher message carries no sample
     *  offset, and the armed-outlet design of #1037 does not add one (it is
     *  the signal-to-control direction), so a sample-accurate trigger from a
     *  message is not possible yet. The signal inlet is sample-accurate:
     *  drive it from a signal (an edge from ``~phasor`` through a comparison,
     *  say) where the exact sample matters.
     *
     *  ### Real time
     *
     *  Nothing on any path allocates, locks or blocks. A trigger is an atomic
     *  scale and an atomic flag, exchanged in ``CALC()`` so it is applied
     *  once; several before a block are one, with the last scale. A ``set``
     *  waveform travels through a triple buffer: the sender parses the
     *  numbers straight into a slot only senders touch and publishes it with
     *  one exchange, and ``CALC()`` takes the newest with another and copies
     *  it into the DSP class. The audio thread therefore never waits for a
     *  sender and never reads a half-written waveform. Senders take a
     *  non-blocking flag among themselves, since a ``set`` can come from the
     *  control thread and from a rendering graph alike; one that loses it
     *  does nothing and is counted in ``Dropped()``, as ``.line``'s family
     *  does, rather than spin.
     *
     *  **Saving.** A ``set`` is not saved: ``DumpJSON`` writes the
     *  ``waveform`` arguments the object was created with, as ``~cycle``
     *  does for its rebinding messages.
     */
    PATCHER_CLASS(dClick, YSE::OBJ::D_CLICK)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _BANG_IN(Trigger)
    _FLOAT_IN(TriggerFloat)
    _INT_IN(TriggerInt)

    /** @brief ``set`` messages refused because another sender held the
     *         waveform slot. Diagnostics and tests. */
    unsigned int Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

  private:
    struct waveform {
      std::array<Flt, DSP::click::MAX_LENGTH> values{};
      UInt count = 0; // 0: the unit impulse
    };

    DSP::click generator;

    std::vector<std::string> arguments;

    // A trigger waiting for the audio thread.
    aFlt pendingScale;
    std::atomic<bool> triggerPending{false};

    // The triple buffer for `set`. `back` belongs to the sender holding
    // `writing`, `front` to CALC(); `middle` is the slot in between, with
    // FRESH set while CALC() has not taken it.
    static constexpr unsigned int FRESH = 4u;
    std::array<waveform, 3> slots;
    unsigned int back = 0;
    std::atomic<unsigned int> middle{1u};
    unsigned int front = 2;
    std::atomic_flag writing = ATOMIC_FLAG_INIT;
    std::atomic<unsigned int> dropped{0};
  };

} // namespace PATCHER
} // namespace YSE

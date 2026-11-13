#pragma once

#include "clockBridge.h"
#include "../../dsp/generators/beatSpan.hpp"
#include <atomic>
#include <cstddef>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief A signal object's reading of a named domain clock, one block at
     *         a time (issue #1278).
     *
     *  ``clockBridge`` (#688) gives control objects a clock's beat when a
     *  message asks for it. A ramp that follows a clock needs it every block,
     *  and a beat for every sample; this is the piece between the two that
     *  ``~phasor``'s ``transport`` and ``~sync``'s ``transport`` share, so
     *  they read a clock the same way.
     *
     *  - **Binding is control-thread work.** ``Bind`` runs from the owner's
     *    ``SetParent``: the clock is named by a string parameter, and a
     *    string parameter makes a live re-parse a rebuild, so the name only
     *    ever changes on a fresh object joining its patcher. ``clockBridge::
     *    Bind`` is wait-free anyway; the name is resolved on the background
     *    pool, and a clock created later is picked up by the bridge's own
     *    ``Poll``.
     *  - **Reading is wait-free.** ``Read`` is ``clockBridge::Beat`` and
     *    ``Tempo``, four acquire loads in all, then ``DSP::beatSpan::read``:
     *    no allocation, no lock, so it runs in ``CALC()``.
     *  - **Which end of the block the reading is.** The engine advances its
     *    domain clocks before it renders a block (``deviceManager::
     *    renderOneBlock``, and ``renderOffline`` in the same order), so the
     *    beat read in ``CALC()`` is the end of the block being rendered. The
     *    block starts where the previous reading ended; ``DSP::beatSpan``
     *    holds that rule.
     *
     *  Without a patcher, a name, a free slot in the bridge or a clock of that
     *  name, there is no reading and ``Read`` gives a span that is not
     *  ``valid()``: the ramps that follow it hold.
     */
    class signalClock {
    public:
      /** @brief Control thread. Bind @p name (@p length characters) on
       *         @p clocks, or nothing when either is empty. */
      void Bind(clockBridge* clocks, const char* name, std::size_t length);

      /** @brief Whether a clock name was bound. */
      bool Bound() const {
        return handle.load(std::memory_order_acquire) != 0;
      }

      /** @brief Audio thread. This block's span on the bound clock, read
       *         through @p clocks; not ``valid()`` while there is no clock.
       *         Wait-free and allocation-free. */
      const DSP::beatSpan& Read(const clockBridge* clocks, UInt length);

      /** @brief The span the last ``Read`` gave. */
      const DSP::beatSpan& Span() const {
        return span;
      }

    private:
      std::atomic<clockBridge::Handle> handle{0};
      DSP::beatSpan span;
    };

  } // namespace PATCHER
} // namespace YSE

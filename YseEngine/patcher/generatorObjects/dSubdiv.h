#pragma once

#include <atomic>
#include "../pObject.h"
#include "../../dsp/generators/phasorSubdiv.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~subdiv``: splits each cycle of a 0..1 ramp into whole-number
     *         sub-ramps, wrapping ``DSP::phasorSubdiv`` (issue #1074).
     *
     *  Ported from Max's ``subdiv~``. The wrapper only maps pins, messages
     *  and parameters onto the DSP class.
     *
     *  - Inlet: the ramp to subdivide, usually a ``~phasor`` (unconnected
     *    reads 0). A ``float`` / ``int`` sets the number of subdivisions (a
     *    float is truncated). Messages: ``div <n>``,
     *    ``pattern <steps...>`` (no steps clears it),
     *    ``prob <chances...>`` (none, or ``prob none``, clears it),
     *    ``lockprob <0|1>``, ``silentmode <0|1>``, ``syncupdate <0|1|2>``.
     *  - Outlet 0: the sub-ramps. Outlet 1: the index of the step playing,
     *    a signal.
     *  - Parameters: ``div`` (argument 1, default 1), ``syncupdate``
     *    (argument 2, default 0), ``silentmode`` (argument 3, default 0) and
     *    ``lockprob`` (argument 4, default 0). The two patterns are set by
     *    message only and are not saved with the patch.
     *
     *  Scalars reach the audio thread through atomics. A pattern is written
     *  into a fixed table of atomics under a sequence counter (odd while a
     *  write is in progress) and copied into the DSP class at the top of the
     *  next block that finds the counter even and unchanged. One message
     *  thread at a time is assumed, as for every object's messages.
     */
    PATCHER_CLASS(dSubdiv, YSE::OBJ::D_SUBDIV)
    _DO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetDiv)
    _INT_IN(SetDivInt)

  public:
    // A fixed-size pattern handed from the message thread to the audio
    // thread without a lock or an allocation.
    template <class T> struct mailbox {
      std::atomic<T> values[DSP::phasorSubdiv::MAX_PATTERN];
      std::atomic<unsigned int> count{0};
      std::atomic<unsigned int> version{0};
      unsigned int seen = 0; // audio thread only

      mailbox() {
        for (auto& v : values)
          v.store(T{}, std::memory_order_relaxed);
      }
      void Begin() {
        version.fetch_add(1, std::memory_order_acq_rel);
      }
      void End(unsigned int n) {
        count.store(n, std::memory_order_relaxed);
        version.fetch_add(1, std::memory_order_release);
      }
      // True with a fresh pattern in `out` / `n`; false when nothing changed
      // or a write is under way (the next block tries again).
      bool Take(T* out, unsigned int& n) {
        const unsigned int before = version.load(std::memory_order_acquire);
        if (before == seen || (before & 1u) != 0) return false;
        n = count.load(std::memory_order_relaxed);
        if (n > DSP::phasorSubdiv::MAX_PATTERN) n = DSP::phasorSubdiv::MAX_PATTERN;
        for (unsigned int i = 0; i < n; ++i)
          out[i] = values[i].load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (version.load(std::memory_order_relaxed) != before) return false;
        seen = before;
        return true;
      }
    };

  private:
    DSP::phasorSubdiv fn;

    std::atomic<int> div{1};
    std::atomic<int> syncupdate{0};
    std::atomic<int> silentmode{0};
    std::atomic<int> lockprob{0};

    mailbox<int> steps;
    mailbox<float> chances;
  };

} // namespace PATCHER
} // namespace YSE

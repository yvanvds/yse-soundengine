#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/filters/biquadCascade.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~cascade``: a series of biquad filters, each driven by five
     *         coefficients, wrapping ``DSP::biquadCascade`` (issue #1091).
     *
     *  Ported from Max's ``cascade~``. The wrapper only maps the pins,
     *  messages and parameter onto the DSP class.
     *
     *  - Inlet 0: the signal to filter (unconnected reads 0).
     *  - Inlet 1: messages only.
     *  - Either inlet takes a list of coefficients in sets of five,
     *    ``a0 a1 a2 b1 b2`` per section, ``clear``, which sets every
     *    coefficient and the state to 0, ``bypass <0|1>``, ``zero <0|1>`` and
     *    ``smooth <0|1>``. A command that arrives as a list (text through a
     *    receive) is read the same way. Max's ``dictionary`` message is not
     *    taken: there is no filter-design dictionary to name.
     *  - Outlet: the filtered signal.
     *  - Parameter: ``sections``, the creation arguments: the number of
     *    sections the object has room for, then optionally a coefficient
     *    list and ``smooth <0|1>`` (``~cascade 4 0.5 0 0 -0.5 0``). The room
     *    is allocated when the object is made; changing it rebuilds the
     *    object, so the allocation never happens on a filter the audio
     *    thread is running. They are saved with the patch; a later list or
     *    message is not, as for ``~biquad``.
     *
     *  A list reaches the audio thread through a sequence-numbered record of
     *  atomics (a seqlock the audio thread reads without waiting: a record
     *  half-written while a block starts is taken at the next block). Two
     *  lists arriving at once on different threads cannot both write it: the
     *  one that finds it held is dropped and counted (``Dropped()``), never
     *  waited for. The flags reach it through atomics, applied at the start
     *  of each block.
     */
    PATCHER_CLASS(dCascade, YSE::OBJ::D_CASCADE)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief The most coefficients one list carries: five per section of
     *         the largest capacity. */
    static constexpr unsigned int MAX_VALUES = DSP::biquadCascade::MAX_CAPACITY * 5;

    /** @brief Lists dropped because another thread was writing one at the
     *         same moment. */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    /** @brief The sections the DSP class has room for. Tests. */
    unsigned int Capacity() const {
      return fn.capacity();
    }

  private:
    // Publish `count` values (count 0: none, as after clear) in the record.
    // False when another writer held it.
    bool Publish(const float* values, unsigned int count, bool clear);

    DSP::biquadCascade fn;

    std::vector<std::string> arguments;

    // The pending list: `sequence` is odd while it is written.
    std::atomic<std::uint32_t> sequence{0};
    std::atomic<unsigned int> pendingCount{0};
    std::atomic<float> pending[MAX_VALUES] = {};
    std::atomic<bool> writing{false};
    // The sequence of the record last applied. Audio thread.
    std::uint32_t applied = 0;
    std::atomic<std::uint64_t> dropped{0};

    std::atomic<bool> clearRequested{false};
    std::atomic<bool> smoothing{true};
    std::atomic<bool> bypassed{false};
    std::atomic<bool> zeroed{false};
  };

} // namespace PATCHER
} // namespace YSE

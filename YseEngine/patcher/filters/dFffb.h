#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/filters/filterBank.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~fffb``: a bank of parallel bandpass filters with one outlet
     *         each, wrapping ``DSP::filterBank`` (issue #1096).
     *
     *  Ported from Max's ``fffb~``. The wrapper only maps the pins, messages
     *  and parameter onto the DSP class.
     *
     *  - Inlet: the signal every filter reads (unconnected reads 0), and the
     *    messages, also as text through a receive. Filters are indexed from
     *    0. ``freq <index> <hz>...``, ``q <index> <q>...`` and
     *    ``gain <index> <gain>...`` set filters from the index on, one value
     *    each; ``freqall <hz>``, ``qall <q>`` and ``gainall <gain>`` set every
     *    filter; ``freqratio <first> <ratio>`` spreads the frequencies as
     *    ``first * ratio^i``, ``freqratio <first> H`` as a harmonic series;
     *    a list ``<index> <hz> [<q>]`` sets one filter; ``clear`` sets every
     *    filter's state to 0. Words are matched without regard to case, so
     *    Max's ``freqAll`` or ``Q`` work too.
     *  - Outlets: one per filter, the first filter's leftmost.
     *  - Parameter: ``arguments``, the creation arguments: the number of
     *    filters (1-128, default 4), the first frequency, the ratio between
     *    successive frequencies (or ``H``), the Q and the gain
     *    (``~fffb 8 200 H 20 0.5``). The number of filters is the number of
     *    outlets; changing it rebuilds the object, so the bank is only ever
     *    allocated on an object no audio thread is running. The arguments
     *    are saved with the patch; later messages are not, as for
     *    ``~reson``.
     *
     *  Each filter's settings are held in atomics. A message writes the ones
     *  it changes and then advances a generation counter; at the start of a
     *  block that finds the counter moved, the audio thread copies them all
     *  into the DSP class (it only derives coefficients again for the
     *  filters whose settings changed). A message written while a block
     *  starts is complete by the next one, whose counter has moved again.
     *  ``clear`` reaches it as a flag.
     */
    PATCHER_CLASS(dFffb, YSE::OBJ::D_FFFB)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief The number of filters, and of outlets. */
    unsigned int FilterCount() const {
      return fn.size();
    }

  private:
    // Rebuild the bank and the outlets from the creation arguments. Control
    // thread, before the object is wired or published.
    void ShapePorts();
    // Mark the settings changed for the audio thread.
    void Publish();

    static constexpr unsigned int MAX = DSP::filterBank::MAX_FILTERS;

    DSP::filterBank fn;

    std::vector<std::string> arguments;

    std::array<std::atomic<float>, MAX> freq{};
    std::array<std::atomic<float>, MAX> quality{};
    std::array<std::atomic<float>, MAX> amp{};
    std::atomic<std::uint32_t> generation{0};
    // The generation last copied into the bank. Audio thread.
    std::uint32_t applied = 0;
    std::atomic<bool> clearRequested{false};
  };

} // namespace PATCHER
} // namespace YSE

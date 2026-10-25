#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/filters/filterResponse.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``.filterdetail``: the characteristics of a filter given as
     *         second-order sections, as six lists, wrapping
     *         ``DSP::filterResponse`` (issue #1101).
     *
     *  Ported from Max's ``filterdetail``. The wrapper only maps the inlet,
     *  outlets and parameter onto the DSP class.
     *
     *  - Inlet 0: a list of coefficients in sets of five, ``a0 a1 a2 b1 b2``
     *    per section (what ``.filterdesign`` sends and ``~cascade`` takes;
     *    one set is a single biquad), analyses the filter and sends the six
     *    lists. An incomplete trailing set is ignored; a list without one
     *    complete set, or with a value that is not a finite number, sends
     *    nothing. ``numpoints <n>`` sets the number of points per list,
     *    without sending.
     *  - Outlets, each a list of ``numpoints`` values, sent right to left:
     *    0 the magnitude (linear gain) and 1 the unwrapped phase (radians)
     *    at frequencies from 0 up to just below the Nyquist frequency in
     *    equal steps; 2 the phase delay and 3 the group delay, in samples;
     *    4 the impulse response and 5 the step response, one value per
     *    sample.
     *  - Parameter: ``numpoints``, the creation argument (``.filterdetail
     *    128`` or ``.filterdetail numpoints 128``), 1 .. ``MAX_POINTS``,
     *    default ``MAX_POINTS``. Saved with the patch; a later
     *    ``numpoints`` message is not.
     *
     *  ``MAX_POINTS`` is the patcher's list ceiling (``AtomList::MAX_ATOMS``),
     *  so every list object downstream takes a curve whole. The DSP class
     *  computes up to ``DSP::filterResponse::MAX_POINTS`` for a C++ caller.
     *
     *  What is not mapped: the ``dictionary`` message, because filter
     *  designs travel as coefficient lists here.
     *
     *  **Threads.** Nothing allocates after construction: the curves are
     *  sized in the constructor and the lists are written into a string
     *  reserved there, so a list may arrive on the audio thread. Messages
     *  from two threads at once do not wait for each other: the one that
     *  finds the object busy is dropped and counted (``Dropped()``).
     */
    PATCHER_CLASS(gFilterdetail, YSE::OBJ::G_FILTERDETAIL)
    _NO_MESSAGES
    _NO_CALCULATE

    _LIST_IN(List)

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief The most points per list. */
    static constexpr unsigned int MAX_POINTS = 256;

    /** @brief Messages dropped because the object was busy. */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    /** @brief The number of points per list. */
    unsigned int Points() const {
      return fn.points();
    }

  private:
    DSP::filterResponse fn{MAX_POINTS};

    std::vector<std::string> numpoints;

    // Where each list is written before it is sent; reserved in the
    // constructor so a list never allocates.
    std::string scratch;

    std::atomic<bool> busy{false};
    std::atomic<std::uint64_t> dropped{0};
  };

} // namespace PATCHER
} // namespace YSE

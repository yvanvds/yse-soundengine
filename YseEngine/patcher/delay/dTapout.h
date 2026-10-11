#pragma once

#include <array>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/delay/delayTap.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~tapout``: reads the delay line of a linked ``~tapin`` at one
     *         or more delay times, one ``DSP::delayTap`` per tap (issues
     *         #1038, #1105).
     *
     *  Ported from Max's ``tapout~``. There is one tap per creation argument
     *  (one with none), each with an inlet and an outlet:
     *
     *  - Inlet *n*: the delay time of tap *n* in ms, as a signal (read with
     *    interpolation, sample by sample) or a number (a fixed delay in whole
     *    samples, used while no signal is connected, see #1036).
     *  - Inlet 0 also takes the link from ``~tapin``'s outlet, and a list sets
     *    the fixed delays of several taps from the first one on.
     *  - Outlet *n*: the line delayed by tap *n*'s time.
     *
     *  The shortest delay is one block, as in MSP, and the longest is the
     *  ``~tapin``'s maximum. With no ``~tapin`` linked, the outlets are silent.
     *
     *  The link is not a dependency (#1038): this object does not wait for
     *  its ``~tapin``, so a loop from an outlet back into the ``~tapin`` is
     *  legal. Because a tap never reads the block being written, the output
     *  does not depend on which of the two runs first.
     *
     *  Not ported: ``tapconnect`` (connecting to a ``tapin~`` without a cord;
     *  here the cord is made with Connect like any other), and the ``unique``
     *  attribute, which only applies to ``mc.tapin~``.
     */
    PATCHER_CLASS(dTapout, YSE::OBJ::D_TAPOUT)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetDelay)
    _INT_IN(SetDelayInt)
    _LIST_IN(SetDelays)

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief Most taps an object builds; more arguments are ignored. */
    static constexpr int MAX_TAPS = 64;

    /** @brief How many taps (inlet / outlet pairs) the object has. */
    int TapCount() const {
      return static_cast<int>(taps.size());
    }

  private:
    // Rebuild the pins and taps from the creation arguments. Control thread,
    // before the object is wired or published: the constructor and the two
    // parameter callbacks (a live re-parse rebuilds the object, #234).
    void ShapePorts();

    std::vector<std::string> creationArgs;
    std::vector<DSP::delayTap> taps;
    // The fixed delay of each tap in ms, written by the control thread.
    std::array<aFlt, MAX_TAPS> delays;
    // Sent while no ~tapin is linked.
    DSP::buffer silence;
  };

} // namespace PATCHER
} // namespace YSE

#pragma once

#include <atomic>
#include "../pObject.h"
#include "../../dsp/generators/sampleCounter.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~count``: a signal counting the samples elapsed, wrapping
     *         ``DSP::sampleCounter`` (issue #1066).
     *
     *  Ported from Max's ``count~``. The wrapper only maps pins, messages
     *  and parameters onto the DSP class.
     *
     *  - Left inlet: an enable signal (counts while it is not 0, restarting
     *    from the minimum each time it turns non-zero). ``bang`` restarts
     *    from the minimum; an ``int`` sets the minimum and restarts from it;
     *    a ``float``, ``min <n>`` and ``set <n>`` set the minimum without a
     *    restart; ``stop`` stops and goes back to the minimum. A list
     *    ``<minimum> [<limit> [<enable> [<autoreset>]]]`` sets the minimum
     *    and the limit, and starts (enable not 0) or stops (0).
     *  - Right inlet: the limit, an ``int`` or a ``float`` (truncated); 0
     *    means none. Does not restart.
     *  - Outlets: the count, and the carry count (wraps since the start).
     *  - Parameters: ``initial`` (argument 1, default 0), ``limit``
     *    (argument 2, default 0) and ``enable`` (argument 3, default 0: the
     *    counter waits for a bang).
     *
     *  Not ported: ``autoreset`` (argument 4, the list's fourth number and
     *  the message are accepted and ignored). It resets the counter when
     *  audio is switched on, and a YSE patcher has no such transition: a
     *  counter starts from its initial value when it is created.
     *
     *  Control values reach the audio thread through atomics; a start or a
     *  stop is posted and applied at the top of the next block, the last
     *  one winning. Messages are not saved; the arguments are.
     */
    PATCHER_CLASS(dCount, YSE::OBJ::D_COUNT)
    _DO_MESSAGES
    _DO_CALCULATE

    _BANG_IN(Restart)
    _INT_IN(StartAt)
    _FLOAT_IN(SetMinimum)
    _FLOAT_IN(SetLimit)
    _INT_IN(SetLimitInt)

  private:
    enum Command : int { NONE = 0, START = 1, STOP = 2 };

    DSP::sampleCounter counter;

    aInt initial;
    aInt limit;
    aInt enable;

    // A start or a stop waiting for the audio thread, exchanged in CALC().
    std::atomic<int> command{NONE};
    // Audio thread only: whether the first block has run, and the enable
    // parameter it last applied (a live re-parse can change it).
    bool started = false;
    bool appliedEnable = false;
  };

} // namespace PATCHER
} // namespace YSE

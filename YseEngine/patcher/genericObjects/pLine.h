#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/generators/lineSegments.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~line``: straight ramp segments played back to back,
     *         wrapping ``DSP::lineSegments`` (issue #1042).
     *
     *  Retrofitted to Max's ``line~``. The wrapper only maps pins, messages
     *  and parameters onto the DSP class.
     *
     *  - Left inlet: a number (``float`` / ``int``) is a target, reached
     *    over the right inlet's time, or jumped to when that time is 0. A
     *    list is ``<target> <time>`` pairs played back to back; a last
     *    single number is a jump. ``<value>, <target> <time> ...`` (a first
     *    number followed by a comma) jumps to the value before the pairs.
     *    Also takes ``stop``, ``pause`` and ``resume``.
     *  - Right inlet: the time in ms for numbers at the left inlet. Kept
     *    until the next one, as ``~line`` always did (Max uses it once).
     *  - Left outlet: the ramp.
     *  - Middle outlet: a bang after the block in which the last segment of
     *    a list, or a jump, arrived. Armed in CALC() and sent after the
     *    traversal (issue #1037).
     *  - Right outlet: 1 while a list is heading for a target, 0 otherwise,
     *    sample by sample (Max's ``activeout``, always present here).
     *  - Parameters: ``initial`` (argument 1, default 0, the value before
     *    the first target), ``time`` (argument 2, default 0, the right
     *    inlet's time until one arrives) and ``maxpoints`` (argument 3,
     *    default 129, the most segments a list keeps, 1 to ``CAPACITY``). A
     *    later number or message is not saved.
     *
     *  Real time: a list is parsed on the sending thread into one of three
     *  slots allocated when the object is built (``CAPACITY`` segments
     *  each; a list longer than ``maxpoints`` is cut to it, counted in
     *  ``Truncated()`` and logged through the real-time log queue) and
     *  handed to the audio thread by one exchange. Senders take a
     *  non-blocking flag among themselves; a sender that loses it, or a
     *  malformed list, is refused and counted in ``Dropped()``. A jump
     *  (a number with time 0) travels on its own, so a jump and a list sent
     *  in one block both apply. Of several lists in one block the last
     *  wins, as do the last jump and the last of several ``stop`` /
     *  ``pause`` / ``resume``; a list, a jump and a command are applied in
     *  the order they arrived.
     */
    PATCHER_CLASS(pLine, YSE::OBJ::D_LINE)
    _DO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetTarget)
    _INT_IN(SetTargetInt)
    _LIST_IN(SetList)
    _FLOAT_IN(SetTime)
    _INT_IN(SetTimeInt)

    /** @brief Segments a list can hold, whatever ``maxpoints`` asks. */
    static constexpr UInt CAPACITY = 1024;
    /** @brief The default ``maxpoints``: Max's. */
    static constexpr int DEFAULT_MAXPOINTS = 129;

    /** @brief Lists refused. Diagnostics and tests. */
    unsigned int Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    /** @brief Lists cut to ``maxpoints``. Diagnostics and tests. */
    unsigned int Truncated() const {
      return truncated.load(std::memory_order_relaxed);
    }

  private:
    struct slot {
      std::vector<Flt> targets;
      std::vector<Flt> times;
      UInt count = 0;
      unsigned int serial = 0;
    };

    static constexpr unsigned int FRESH = 4;
    static constexpr unsigned int INDEX = 3;

    void Segments(const std::string& text);
    void Target(Flt value);
    void Jump(Flt value);
    void Command(const std::string& message);
    void Publish(slot& s);
    void ReportTruncation();
    unsigned int NextSerial();

    DSP::lineSegments fn{CAPACITY};

    slot slots[3];
    unsigned int writeSlot = 0; // the sender's, under `writing`
    std::atomic<unsigned int> published{1}; // the latest, FRESH when unread
    unsigned int readSlot = 2; // the audio thread's
    std::atomic_flag writing = ATOMIC_FLAG_INIT;
    std::atomic<unsigned int> dropped{0};
    std::atomic<unsigned int> truncated{0};
    std::atomic<std::uint64_t> loggedTick{0};

    // Lists, jumps and commands are numbered as they arrive, so a block
    // applies them in that order. `control` is serial << 2 | command;
    // `jumpTag` is the serial of the latest jump, `jumpValue` its value.
    std::atomic<unsigned int> serial{0};
    std::atomic<unsigned int> control{0};
    std::atomic<unsigned int> jumpTag{0};
    aFlt jumpValue{0.f};
    unsigned int controlSeen = 0; // the audio thread's
    unsigned int jumpSeen = 0; // the audio thread's

    aFlt initial{0.f};
    aFlt time{0.f};
    aInt maxpoints{DEFAULT_MAXPOINTS};
    bool started = false; // the audio thread's: the initial value is set
  };

} // namespace PATCHER
} // namespace YSE

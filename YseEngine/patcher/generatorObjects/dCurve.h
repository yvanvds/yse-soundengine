#pragma once

#include <atomic>
#include <vector>
#include "../pObject.h"
#include "../../dsp/generators/curveSegments.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~curve``: curved ramp segments played back to back, wrapping
     *         ``DSP::curveSegments`` (issue #1065).
     *
     *  Ported from Max's ``curve~``. The wrapper only maps pins, messages
     *  and parameters onto the DSP class.
     *
     *  - Left inlet: a number (``float`` / ``int``) is a target, reached
     *    over the time at the middle inlet with the curve at the right one;
     *    with no time it is a jump. A list is ``<target> <time> <curve>``
     *    triplets; a last group of two uses the right inlet's curve, a last
     *    single number is a jump. Also takes ``stop``, ``pause``,
     *    ``resume``, ``factor <curve>`` (the right inlet's curve) and
     *    ``shapemode <0|1>``.
     *  - Middle inlet: the time in ms for the next number at the left inlet.
     *    Used once: a target resets it to 0, as in Max.
     *  - Right inlet: the curvature, -1..1, for numbers and pairs. Kept.
     *  - Left outlet: the ramp.
     *  - Middle outlet: a bang after the block in which a list finished.
     *  - Right outlet: 1 when a list starts or resumes, 0 when it finishes,
     *    stops or pauses (Max's ``activeout``, always present here). Both
     *    are armed in CALC() and sent after the traversal (issue #1037).
     *  - Parameters: ``initial`` (argument 1, default 0, the value before
     *    the first list), ``curve`` (argument 2, default 0) and
     *    ``shapemode`` (argument 3, default 0: ``.function``'s power curve;
     *    1: exponential). A later number or message is not saved.
     *
     *  Real time: a list (a number is a list of one) is parsed on the
     *  sending thread into one of three slots allocated when the object is
     *  built (room for ``DSP::curveSegments::DEFAULT_POINTS`` segments
     *  each; a longer list is cut to that, counted in ``Truncated()``) and
     *  handed to the audio thread by one exchange. Senders take a
     *  non-blocking flag among themselves; a sender that loses it, or a
     *  malformed list, is refused and counted in ``Dropped()``. Of several
     *  lists in one block the last wins, as does the last of several
     *  ``stop`` / ``pause`` / ``resume``; a list and one of those are
     *  applied in the order they arrived.
     */
    PATCHER_CLASS(dCurve, YSE::OBJ::D_CURVE)
    _DO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetTarget)
    _INT_IN(SetTargetInt)
    _LIST_IN(SetList)
    _FLOAT_IN(SetTime)
    _INT_IN(SetTimeInt)
    _FLOAT_IN(SetCurve)
    _INT_IN(SetCurveInt)

    /** @brief Lists refused. Diagnostics and tests. */
    unsigned int Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    /** @brief Lists cut to the capacity. Diagnostics and tests. */
    unsigned int Truncated() const {
      return truncated.load(std::memory_order_relaxed);
    }

  private:
    struct slot {
      std::vector<Flt> targets;
      std::vector<Flt> times;
      std::vector<Flt> curves;
      UInt count = 0;
      unsigned int serial = 0;
    };

    static constexpr unsigned int FRESH = 4;
    static constexpr unsigned int INDEX = 3;

    void Segments(const std::string& text);
    void Target(Flt value);
    void Command(const std::string& message);
    void Publish(slot& s);

    DSP::curveSegments fn;

    slot slots[3];
    unsigned int writeSlot = 0; // the sender's, under `writing`
    std::atomic<unsigned int> published{1}; // the latest, FRESH when unread
    unsigned int readSlot = 2; // the audio thread's
    std::atomic_flag writing = ATOMIC_FLAG_INIT;
    std::atomic<unsigned int> dropped{0};
    std::atomic<unsigned int> truncated{0};

    // Lists and commands are numbered as they arrive, so a block applies a
    // list and a command in that order. `control` is serial << 2 | command.
    std::atomic<unsigned int> serial{0};
    std::atomic<unsigned int> control{0};
    unsigned int controlSeen = 0; // the audio thread's

    aFlt initial;
    aFlt curve;
    aInt shapemode;
    aFlt time; // the middle inlet's, used once
    bool started = false; // the audio thread's: the initial value is set
  };

} // namespace PATCHER
} // namespace YSE

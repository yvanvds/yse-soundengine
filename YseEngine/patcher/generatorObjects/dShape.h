#pragma once

#include <atomic>
#include <vector>
#include "../pObject.h"
#include "../../dsp/generators/shapeEnvelope.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~shape``: a breakpoint envelope played over the length of a
     *         ramp, wrapping ``DSP::shapeEnvelope`` (issue #1071).
     *
     *  Ported from Max's ``shape~``. The wrapper only maps pins, messages
     *  and parameters onto the DSP class.
     *
     *  - Inlet: the ramp (a ``~phasor``, rising or falling) as a signal; an
     *    unconnected inlet reads 0. A ``float`` (or ``int``) is Max's
     *    initial value: stored for the next list, it changes nothing by
     *    itself. A list is the envelope: ``<value> <time>`` pairs with
     *    curvemode off (what ``.function`` sends), ``<value> <time>
     *    <curve>`` triplets with curvemode on, either optionally led by the
     *    initial value (which is then stored too). Also takes
     *    ``constant <n>``, ``curvemode <0|1|2>`` and ``syncupdate <0|1>``,
     *    Max's attributes.
     *  - Outlet: the envelope.
     *  - Parameters: ``constant`` (argument 1, default 0), ``curvemode``
     *    (argument 2, default 0) and ``syncupdate`` (argument 3, default 0).
     *    Max's ``shape~`` takes no arguments; these are YSE's so a patch
     *    saves its settings. Curvemode 2 (Max's Auto, which asks a connected
     *    ``function`` for its mode) reads lists as pairs, as ``.function``
     *    sends them, and plays curves.
     *
     *  Real time: an envelope is parsed on the sending thread into one of
     *  three slots allocated when the object is built (room for
     *  ``DSP::shapeEnvelope::DEFAULT_POINTS`` breakpoints each) and handed
     *  to the audio thread by one exchange; the audio thread takes the
     *  latest at the top of its block. Senders take a non-blocking flag
     *  among themselves; a sender that loses it, or a list longer than the
     *  capacity, malformed or not a whole number of pairs or triplets, is
     *  refused whole and counted in ``Dropped()``. Of several lists sent
     *  within one block, the last one wins. The envelope and the messages
     *  are not saved; the arguments are.
     */
    PATCHER_CLASS(dShape, YSE::OBJ::D_SHAPE)
    _DO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetInitial)
    _INT_IN(SetInitialInt)
    _LIST_IN(SetList)

    /** @brief Lists refused. Diagnostics and tests. */
    unsigned int Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    /** @brief The envelope. Only while no block runs (tests). */
    const DSP::shapeEnvelope& Envelope() const {
      return fn;
    }

  private:
    struct slot {
      std::vector<Flt> values;
      std::vector<Flt> times;
      std::vector<Flt> curves;
      UInt count = 0;
      Flt initial = 0.f;
    };

    static constexpr unsigned int FRESH = 4;
    static constexpr unsigned int INDEX = 3;

    void Breakpoints(const std::string& text);
    void Attribute(const std::string& message);

    DSP::shapeEnvelope fn;

    slot slots[3];
    unsigned int writeSlot = 0; // the sender's, under `writing`
    std::atomic<unsigned int> published{1}; // the latest, FRESH when unread
    unsigned int readSlot = 2; // the audio thread's
    std::atomic_flag writing = ATOMIC_FLAG_INIT;
    std::atomic<unsigned int> dropped{0};

    aFlt initialValue;
    std::atomic<int> constant{0};
    std::atomic<int> curvemode{0};
    std::atomic<int> syncupdate{0};
  };

} // namespace PATCHER
} // namespace YSE

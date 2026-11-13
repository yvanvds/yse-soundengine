#pragma once

#include <atomic>
#include <string>
#include "../pObject.h"
#include "../../dsp/generators/clockFollower.hpp"
#include "../../utils/mpmcQueue.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~sync``: a 0..1 ramp once per beat of a tempo, tap tempo, MIDI
     *         beat clock or a click track, wrapping ``DSP::clockFollower``
     *         (issue #1131).
     *
     *  Ported from Max's ``sync~``. Everything that keeps time is in the DSP
     *  class; the wrapper maps pins and parameters onto it.
     *
     *  - Inlet: a click track as a signal; a bang is a tap; an int is a MIDI
     *    byte (248 tick, 250 start, 251 continue, 252 stop, others ignored),
     *    so ``.rtin`` connects straight in; a float sets the tempo. Messages:
     *    ``bpm <x>``, ``offset <0-1>``, ``midioffset <0-1>``, ``ppq <n>``,
     *    ``start``, ``stop``.
     *  - Left outlet: the ramp.
     *  - Middle outlet: ``midi``, ``click`` and ``tap`` when one or more of
     *    those arrived in the block, then ``bpm <x>`` when the tempo changed.
     *  - Right outlet: the MIDI beat clock that follows the ramp, one int per
     *    byte, in order (for ``.midiout``).
     *  - Parameters: ``bpm``, ``offset``, ``midioffset``, ``ppq`` (Max's
     *    ``sync~`` takes no arguments; these are YSE's) and ``rtport``, Max's
     *    attribute: a MIDI input port index whose real-time bytes the object
     *    reads itself, through ``MIDI::inHub`` as the ``.rtin`` family does,
     *    or -1 for none. A port name and ``All`` are not taken: ``.midiinfo``
     *    lists the indices. A later message is not saved.
     *
     *  Bangs, ints, floats and ``start``/``stop`` may arrive on any thread.
     *  They go through a lock-free queue, in order, and the class applies them
     *  at the start of the next block. ``offset``, ``midioffset`` and ``ppq``
     *  are atomics handed to the class every block. The reports are sent by
     *  the patcher after the block's traversal (issue #1037), right outlet
     *  first.
     *
     *  Not ported: following the patcher's transport (a named domain clock).
     *  ``sync~`` has no such mode, and a ramp locked to a domain clock is what
     *  ``~phasor``'s ``lock``/``transport`` attributes are for, which still
     *  need the clock bridge wired to signal objects (issue #1278).
     */
    PATCHER_CLASS(dSync, YSE::OBJ::D_SYNC)
    _DO_MESSAGES
    _DO_CALCULATE

    _BANG_IN(Tap)
    _INT_IN(Midi)
    _FLOAT_IN(Tempo)

    _PARM_PARSE

    ~dSync() override;
    dSync(const dSync&) = delete;
    dSync& operator=(const dSync&) = delete;
    dSync(dSync&&) = delete;
    dSync& operator=(dSync&&) = delete;

    /** @brief Control thread. Joining a patcher subscribes to ``rtport``. */
    void SetParent(pObject* newParent) override;

    void FlushArmedMessages(THREAD thread) override;

    /** @brief The MIDI input subscription, 0 for none. Test surface. */
    unsigned int Subscription() const {
      return subscription.load(std::memory_order_acquire);
    }

    /** @brief Events the queue had no room for. Test surface. */
    unsigned int Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

  private:
    enum class eventKind : unsigned char { TAP, TEMPO, MIDI, START, STOP };
    struct event {
      eventKind kind;
      float value;
    };
    static constexpr std::size_t EVENT_CAPACITY = 256;

    void Push(eventKind kind, float value);
    void Unsubscribe();

    DSP::clockFollower fn;

    aFlt bpm{DSP::clockFollower::DEFAULT_TEMPO};
    aFlt offset{0.f};
    aFlt midioffset{0.f};
    aInt ppq{static_cast<Int>(DSP::clockFollower::DEFAULT_PPQ)};
    aInt rtport{-1};

    YSE::mpmcQueue<event> events{EVENT_CAPACITY};
    std::atomic<unsigned int> dropped{0};
    // A MIDI::inHub handle (0 = none). Written on the control thread, read
    // by CALC().
    std::atomic<unsigned int> subscription{0};

    // Set by CALC() when the block has something to report; the flush reads
    // the class's report. Both run on the thread rendering the patcher.
    bool flushPending = false;
    std::string scratch; // "bpm <x>", reserved in the constructor
    const std::string tapWord{"tap"};
    const std::string clickWord{"click"};
    const std::string midiWord{"midi"};
  };

} // namespace PATCHER
} // namespace YSE

#pragma once

#include <atomic>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/generators/zigzag.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~zigzag``: an editable breakpoint function played at any
     *         speed, wrapping ``DSP::zigzag`` (issue #1067).
     *
     *  Ported from Max's ``zigzag~``. The wrapper only maps pins, messages
     *  and parameters onto the DSP class.
     *
     *  - Left inlet: a list is ``value time`` pairs (a last single value
     *    takes time 0) that replaces the list and plays it; a number is a
     *    list of one point, reached over the ``ramptime``. A bang jumps or
     *    steps as the mode says. A signal is an index into the list (mode
     *    0) or a trigger where it starts to fall (modes 1 and 2). Messages:
     *    ``append <value> <time>``, ``insert <index> <value> <time> ...``,
     *    ``delete <index> ...``, ``set`` / ``setindex <index> <value>
     *    <time>``, ``line`` / ``list <value> <time> ...``, ``setline <value>
     *    <time> ...`` (without playing), ``jump <index> [time]``,
     *    ``jumpstart [time]``, ``jumpend [time]``, ``next [time]``,
     *    ``prev [time]``, ``skip <points> [time]``, ``start <index>``,
     *    ``end <index>``, ``bound <start> <end>``, ``bangdelta <time>``,
     *    ``ramptime <time>``, ``speed <rate>``, ``stop``, ``dump``,
     *    ``loopmode <0|1|2>`` and ``mode <0|1|2>``.
     *  - Right inlet: the speed, a signal (its first sample each block, as
     *    in Max) or a number used while no signal is connected.
     *  - Outlets: the function; the index signal; ``dump``'s
     *    ``index value time`` lists, sent on the thread that asked; a bang
     *    after a block in which play reached the end of its range (stopped,
     *    looped or turned), armed in CALC() and sent after the traversal
     *    (issue #1037).
     *  - Parameters: ``initial`` (argument 1, default 0, the output before
     *    the first point), ``loopmode`` (argument 2) and ``mode`` (argument
     *    3). The list and later messages are not saved.
     *
     *  Word messages come in through the list handler, which knows the
     *  thread ``dump`` answers on.
     *
     *  Real time: every edit and move is turned on the sending thread into
     *  commands in a queue allocated when the object is built, published
     *  together and applied in order at the start of the next block, so an
     *  edit never allocates or locks. The same commands keep a copy of the
     *  list on the sending side, which ``dump`` reads. Senders take a
     *  non-blocking flag among themselves; a sender that loses it, a full
     *  queue or a malformed message is refused and counted in
     *  ``Dropped()``. A list is cut to ``DSP::zigzag::DEFAULT_POINTS``
     *  points, counted in ``Truncated()``.
     */
    PATCHER_CLASS(dZigzag, YSE::OBJ::D_ZIGZAG)
    _NO_MESSAGES
    _DO_CALCULATE

    _BANG_IN(Bang)
    _FLOAT_IN(SetValue)
    _INT_IN(SetValueInt)
    _LIST_IN(SetList)
    _FLOAT_IN(SetSpeed)
    _INT_IN(SetSpeedInt)

    /** @brief Messages refused. Diagnostics and tests. */
    unsigned int Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    /** @brief Lists cut to the capacity. Diagnostics and tests. */
    unsigned int Truncated() const {
      return truncated.load(std::memory_order_relaxed);
    }

    /** @brief The sending side's copy of the list. Diagnostics and tests;
     *         read it from the sending thread. */
    const DSP::zigzag& Shadow() const {
      return shadow;
    }

  private:
    struct command {
      int op = 0;
      Int index = 0;
      Flt a = 0.f;
      Flt b = 0.f;
    };

    void Command(const std::string& message, THREAD thread);
    void Pairs(const char* text, std::size_t length, std::size_t pos, bool play);
    void Insert(const char* text, std::size_t length, std::size_t pos);
    void Delete(const char* text, std::size_t length, std::size_t pos);
    void Dump(THREAD thread);
    void Single(int op, Int index, Flt a, Flt b);
    bool Begin(UInt commands);
    void Put(int op, Int index, Flt a, Flt b);
    void End();

    DSP::zigzag fn;
    DSP::zigzag shadow; // the sending side's copy, under `writing`

    std::vector<command> queue;
    std::atomic<UInt> head{0}; // written by the sender, under `writing`
    std::atomic<UInt> tail{0}; // written by the audio thread
    UInt cursor = 0; // the sender's, between Begin() and End()
    std::atomic_flag writing = ATOMIC_FLAG_INIT;
    std::atomic<unsigned int> dropped{0};
    std::atomic<unsigned int> truncated{0};
    std::string scratch; // dump's, reserved

    aFlt initial;
    aInt loopmode;
    aInt mode;
    aFlt speed;
    aFlt ramptime;
    bool started = false; // the audio thread's: the initial value is set
  };

} // namespace PATCHER
} // namespace YSE

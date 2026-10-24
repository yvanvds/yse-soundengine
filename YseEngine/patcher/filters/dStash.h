#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/generators/signalStash.hpp"
#include "../../utils/mpmcQueue.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~stash``: stores signal values into indexed places and
     *         recalls them, wrapping ``DSP::signalStash`` (issue #1084).
     *
     *  Ported from Max's ``stash~``. The wrapper only maps the pins, messages
     *  and parameter onto the DSP class.
     *
     *  - Inlet 0: the signal to record (unconnected reads the last number
     *    sent to it, 0 at first, #1036). A number is also written at the
     *    write position, as Max's ``float`` does. A list of numbers fills the
     *    memory and sets the size to its length. Messages: ``samplelevel``,
     *    ``advancelevel``, ``sampletriggermode <0-6>``,
     *    ``advancetriggermode <0-6>``, ``dir <0-3>``, ``mode <0-2>`` (or
     *    ``advancemode``), ``writemode <0-2>``, ``extend <0-3>``,
     *    ``interp <0-2>``, ``duration <n>`` (or ``dur``), ``size <n>``. A list
     *    that starts with a word (text through a receive) is read as a
     *    message.
     *  - Inlet 1: the sample trigger (unconnected reads 0); a number sets
     *    ``samplelevel``; a bang records the next block's first sample.
     *  - Inlet 2: the advance trigger or read index (unconnected reads 0); a
     *    number sets ``advancelevel``; a bang advances the output.
     *  - Inlet 3: the write index (unconnected reads the last number, 0 at
     *    first); a number also sets the write position; a bang moves it one
     *    place on.
     *  - Outlet: the value read from the memory.
     *  - Parameter: ``samplelevel``, the creation arguments: Max's
     *    ``sample-thresh`` and ``advance-thresh`` as up to two numbers, then
     *    attribute keywords, the patcher having no attribute syntax:
     *    ``~stash 0.5 0.5 size 8 mode 2 maxsize 64``. ``maxsize`` (default
     *    1000, up to 1048576) is set only here, as in Max. They are saved
     *    with the patch; a later message is not, and neither is the memory.
     *
     *  Scalars reach the audio thread through atomics, applied to the DSP
     *  class at the start of each block (the size only when it changed). A
     *  list travels as ``dSash``'s does: a table of atomics sized to
     *  ``maxsize`` under a sequence counter, its size posted first. Writes,
     *  write positions and the bangs are ordered actions, so they go through
     *  a fixed lock-free queue drained at the start of the next block, after
     *  the list (a full queue drops the action). One message thread at a
     *  time is assumed for the list, as for every object's messages.
     */
    PATCHER_CLASS(dStash, YSE::OBJ::D_STASH)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetInput)
    _INT_IN(SetInputInt)
    _LIST_IN(SetList)
    _FLOAT_IN(SetSampleLevel)
    _INT_IN(SetSampleLevelInt)
    _BANG_IN(Capture)
    _FLOAT_IN(SetAdvanceLevel)
    _INT_IN(SetAdvanceLevelInt)
    _BANG_IN(Advance)
    _FLOAT_IN(SetWriteIndex)
    _INT_IN(SetWriteIndexInt)
    _BANG_IN(AdvanceWrite)

  private:
    // An attribute keyword (dStash.cpp's keyword enum) and its number; a
    // number out of range is ignored.
    void SetAttribute(int which, float number);
    // Sizes the DSP class, the list table and the audio thread's copy of it.
    void Allocate(unsigned int capacity);
    // A list of numbers: the size, then the table.
    void PostList(const std::string& text);

    // An ordered action for the audio thread.
    struct action {
      int kind;
      float value;
    };
    void Post(int kind, float value);

    DSP::signalStash fn;

    std::vector<std::string> arguments;

    std::atomic<float> input{0.f};
    std::atomic<float> writeIndex{0.f};
    std::atomic<float> samplelevel{0.5f};
    std::atomic<float> advancelevel{0.5f};
    std::atomic<int> sampletriggermode{DSP::signalStash::ASCENDING};
    std::atomic<int> advancetriggermode{DSP::signalStash::ASCENDING};
    std::atomic<int> dir{DSP::signalStash::FORWARD};
    std::atomic<int> mode{DSP::signalStash::NEXT};
    std::atomic<int> writemode{DSP::signalStash::NEXT};
    std::atomic<int> extend{DSP::signalStash::WRAP};
    std::atomic<int> interp{DSP::signalStash::TRUNCATE};
    std::atomic<unsigned int> duration{0};
    std::atomic<unsigned int> size{1};
    unsigned int appliedSize = 1; // audio thread only

    // The list mailbox, sized to the capacity on the control thread.
    std::unique_ptr<std::atomic<float>[]> listValues;
    unsigned int listCapacity = 0;
    std::atomic<unsigned int> listCount{0};
    std::atomic<unsigned int> listVersion{0};
    unsigned int listSeen = 0; // audio thread only
    std::vector<float> listScratch; // audio thread only, sized with the table

    static constexpr std::size_t ACTION_CAPACITY = 256;
    YSE::mpmcQueue<action> actions{ACTION_CAPACITY};
  };

} // namespace PATCHER
} // namespace YSE

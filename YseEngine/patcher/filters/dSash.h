#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/generators/sampleHoldMemory.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~sash``: sample and hold with memory, wrapping
     *         ``DSP::sampleHoldMemory`` (issue #1083).
     *
     *  Ported from Max's ``sash~``. The wrapper only maps the pins, messages
     *  and parameter onto the DSP class.
     *
     *  - Inlet 0: the signal to sample (unconnected reads the last number
     *    sent to it, 0 at first, #1036). A list of numbers fills the memory
     *    and sets the size to its length, as Max's ``list`` does. Messages:
     *    ``samplelevel <x>``, ``advancelevel <x>``, ``dir <0-3>``,
     *    ``mode <0-2>``, ``size <n>`` (1 to ``maxsize``, clamped). A list
     *    that starts with a word (text through a receive) is read as a
     *    message.
     *  - Inlet 1: the sample trigger (unconnected reads 0); a number sets
     *    ``samplelevel``, as Max's ``float`` does.
     *  - Inlet 2: the advance trigger (unconnected reads 0); a number sets
     *    ``advancelevel``.
     *  - Outlet: the value read from the memory.
     *  - Parameter: ``attributes``, the creation arguments. Max's
     *    ``sash~`` takes no arguments, so these are its attributes as
     *    keywords, the patcher having no attribute syntax:
     *    ``~sash size 8 mode 2 maxsize 64``. ``maxsize`` (default 1000, up to
     *    1048576) is set only here, as in Max. They are saved with the
     *    patch; a later message is not, and neither is the memory.
     *
     *  Scalars reach the audio thread through atomics, applied to the DSP
     *  class at the start of each block (the size only when it changed). A
     *  list is written into a table of atomics sized to ``maxsize`` under a
     *  sequence counter (odd while a write is in progress, as ``dSubdiv``'s
     *  mailbox) and loaded at the top of the next block that finds the
     *  counter even and unchanged. The list's size is posted before the
     *  table, so a block that takes the table never sees an older size
     *  after it. One message thread at a time is assumed, as for every
     *  object's messages.
     */
    PATCHER_CLASS(dSash, YSE::OBJ::D_SASH)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetInput)
    _INT_IN(SetInputInt)
    _LIST_IN(SetList)
    _FLOAT_IN(SetSampleLevel)
    _INT_IN(SetSampleLevelInt)
    _FLOAT_IN(SetAdvanceLevel)
    _INT_IN(SetAdvanceLevelInt)

  private:
    // An attribute keyword (dSash.cpp's keyword enum) and its number; a
    // number out of range is ignored.
    void SetAttribute(int which, float number);
    // Sizes the DSP class, the list table and the audio thread's copy of it.
    void Allocate(unsigned int capacity);
    // A list of numbers: the size, then the table.
    void PostList(const std::string& text);

    DSP::sampleHoldMemory fn;

    std::vector<std::string> arguments;

    std::atomic<float> input{0.f};
    std::atomic<float> samplelevel{0.5f};
    std::atomic<float> advancelevel{0.5f};
    std::atomic<int> dir{DSP::sampleHoldMemory::FORWARD};
    std::atomic<int> mode{DSP::sampleHoldMemory::LEVEL};
    std::atomic<unsigned int> size{1};
    unsigned int appliedSize = 1; // audio thread only

    // The list mailbox, sized to the capacity on the control thread.
    std::unique_ptr<std::atomic<float>[]> listValues;
    unsigned int listCapacity = 0;
    std::atomic<unsigned int> listCount{0};
    std::atomic<unsigned int> listVersion{0};
    unsigned int listSeen = 0; // audio thread only
    std::vector<float> listScratch; // audio thread only, sized with the table
  };

} // namespace PATCHER
} // namespace YSE

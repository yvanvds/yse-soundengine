#pragma once

#include <atomic>
#include <string>
#include <vector>
#include "../../headers/types.hpp"
#include "../../dsp/routing/signalSelector.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief The control side ``~gate`` and ``~selector`` share (issue
     *         #1133): their creation arguments, the numbers and messages of
     *         their left inlet, and the hand-over of all of it to
     *         ``DSP::signalSelector`` at the start of a block.
     *
     *  The open channel a number, ``next`` or ``bang`` sets is the one the
     *  left inlet reads while no signal is connected to it (#1036): a
     *  connected signal wins, and the number is used again once the last
     *  cord is removed. The number, the ramp time and the step mode are
     *  atomics the message thread writes and the audio thread reads. One
     *  message thread at a time is assumed, as for every object's messages.
     */
    struct selectorControl {
      /** @brief The channel count and the initially open channel the
       *         creation arguments ask for, with ``ramptime`` / ``ramp`` and
       *         ``stepmode`` among them setting those. Control thread. */
      struct shape {
        UInt channels = DSP::signalSelector::DEFAULT_CHANNELS;
        Int open = 0;
      };

      /** @brief Reads @p arguments, resets the atomics to them and returns
       *         the shape. Control thread, before the object is published. */
      shape Parse(const std::vector<std::string>& arguments);

      /** @brief The channel count the object was built with. Set by the
       *         wrapper after ``Parse``. */
      UInt channels = DSP::signalSelector::DEFAULT_CHANNELS;

      /** @brief A number in the left inlet: opens that channel (truncated). */
      void Number(Flt value);

      /** @brief ``next`` or ``bang``: opens the next channel, wrapping. */
      void Next();

      /** @brief ``next``, ``ramptime <ms>`` (or ``ramp <ms>``),
       *         ``stepmode <0|1>`` or a number. False for anything else.
       *         Allocation-free. */
      bool Message(const std::string& message);

      /** @brief Audio thread, at the start of a block: hands the ramp time,
       *         the step mode and, when @p controlConnected is false, the
       *         open channel to @p fn. */
      void Apply(DSP::signalSelector& fn, bool controlConnected) const;

      std::atomic<Int> number{0};
      std::atomic<Flt> ramp{0.f};
      std::atomic<bool> step{false};
    };

  } // namespace PATCHER
} // namespace YSE

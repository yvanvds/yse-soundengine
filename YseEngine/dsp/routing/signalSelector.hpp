/*
  ==============================================================================

    signalSelector.hpp
    Signal routing: one of several inputs to one output, or one input to one
    of several outputs, switched hard or with a linear crossfade
    (issue #1133).

  ==============================================================================
*/

#ifndef YSE_DSP_ROUTING_SIGNALSELECTOR_HPP
#define YSE_DSP_ROUTING_SIGNALSELECTOR_HPP

#include <array>
#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Signal routing over a number of channels: picks one of several
     *         inputs for one output (``select``), or sends one input to one
     *         of several outputs (``route``). The other outputs carry silence.
     *
     *  Ported from Max's ``selector~`` and ``gate~``, which share it. The
     *  channels are numbered from 1. Channel 0 is *closed*: no input reaches
     *  any output and everything is silent. The open channel is set with
     *  ``open()``, stepped with ``next()``, or read per sample from a control
     *  signal:
     *
     *  - A number below 1 (or not a number) closes. A number from 1 upwards
     *    is truncated, so 1 to 1.999 opens channel 1, and a number above the
     *    channel count opens the last channel.
     *  - ``next()`` opens the channel after the open one, and channel 1 when
     *    the selector is closed or the last channel is open.
     *  - In step mode the control signal does not choose the channel by its
     *    value: every change of its whole-number part opens the next channel,
     *    as ``next()`` does, and a whole-number part of 0 or less closes.
     *
     *  By default a switch is hard: the new channel is fully open from the
     *  sample the switch happens on, as in Max. A ramp time in milliseconds
     *  turns each switch into a linear crossfade: the channel that opens
     *  fades in and every other channel fades out over that time, so a
     *  switch does not click. A switch during a fade carries on from the
     *  gains the fade has reached. The ramp time is converted to samples at
     *  the live ``SAMPLERATE`` at the start of each block.
     *
     *  Use an object either to select or to route, not both: ``select()``
     *  writes ``output(0)``, which ``route()`` uses for channel 1.
     *
     *  Each call processes at most ``STANDARD_BUFFERSIZE`` samples. Call it
     *  from one thread at a time. Processing allocates nothing: the outputs
     *  are sized to ``STANDARD_BUFFERSIZE`` in the constructor, one per
     *  channel.
     */
    class API signalSelector {
    public:
      /** @brief The most channels a selector has. */
      static constexpr UInt MAX_CHANNELS = 128;

      /** @brief The channel count when none is given. */
      static constexpr UInt DEFAULT_CHANNELS = 1;

      /** @brief @p channels channels (1 to ``MAX_CHANNELS``, clamped) with
       *         @p open open, as ``open()`` takes it. The open channel starts
       *         fully open, without a fade. */
      explicit signalSelector(UInt channels = DEFAULT_CHANNELS, Int open = 0);

      /** @brief The number of channels. */
      UInt channels() const {
        return count;
      }

      /** @brief Opens @p channel: 0 or below closes, above ``channels()``
       *         opens the last. Fades when a ramp time is set. */
      signalSelector& open(Int channel);

      /** @brief The open channel, the one the outputs are heading for; 0 when
       *         closed. */
      Int open() const {
        return target;
      }

      /** @brief Opens the next channel, wrapping, as ``Next()``. */
      signalSelector& next();

      /** @brief The crossfade time in milliseconds for each switch; 0 (the
       *         default) switches hard. Below 0 counts as 0; not finite is
       *         ignored. */
      signalSelector& rampTime(Flt ms);

      /** @brief The ramp time as set. */
      Flt rampTime() const {
        return ramp;
      }

      /** @brief Step mode: each change of the control signal's whole-number
       *         part opens the next channel, and 0 or less closes. Off by
       *         default. */
      signalSelector& stepMode(bool on);

      /** @brief True in step mode. */
      bool stepMode() const {
        return steps;
      }

      /** @brief The gain channel @p channel (from 1) is passed with now: 1
       *         when open, 0 when closed, in between during a fade. 0 for a
       *         channel the selector does not have. */
      Flt gain(Int channel) const;

      /** @brief The channel a control value opens among @p channels: 0 below
       *         1 and for a value that is not a number, the whole part from 1
       *         up, at most @p channels. */
      static Int Channel(Flt value, UInt channels);

      /** @brief The channel after @p open among @p channels: 1 when @p open is
       *         0 or less, or the last channel or beyond. */
      static Int Next(Int open, UInt channels);

      /** @brief One block of selecting: ``output(0)`` is the open input.
       *
       *  @p inputs points at ``channels()`` buffers, input 1 first; a null
       *  pointer is a silent input. As long as the shortest input. */
      buffer& select(buffer* const* inputs);

      /** @brief One block of selecting with the channel read per sample from
       *         @p control (see the class). As long as the shortest of the
       *         inputs and @p control. */
      buffer& select(buffer* const* inputs, buffer& control);

      /** @brief One block of routing: @p in goes to the output of the open
       *         channel, silence to the others. Returns ``output(0)``. */
      buffer& route(buffer& in);

      /** @brief One block of routing with the channel read per sample from
       *         @p control (see the class). As long as the shorter of the two.
       *         Returns ``output(0)``. */
      buffer& route(buffer& in, buffer& control);

      /** @brief The output of channel @p index + 1 (``output(0)`` is channel
       *         1, and the selected signal after ``select()``). An index past
       *         the last channel reads the last channel's output. */
      buffer& output(UInt index);

    private:
      // Starts heading for `channel`, already clamped.
      void Head(Int channel);
      // The control value of one sample.
      void Control(Flt value);
      // Moves every gain one sample towards its goal by `step`.
      void Advance(Flt step);
      // The per-sample gain change for this block's ramp time.
      Flt Step() const;

      UInt count;
      Int target = 0;
      Flt ramp = 0.f;
      bool steps = false;
      // The whole-number part of the last control sample, for step mode.
      Int lastWhole = 0;
      // True while a gain has not reached its goal.
      bool fading = false;
      std::array<Flt, MAX_CHANNELS> gains{};
      std::vector<buffer> outputs;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_ROUTING_SIGNALSELECTOR_HPP

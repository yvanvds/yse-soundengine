/*
  ==============================================================================

    sampleDelay.hpp
    A one-in, one-out delay counted in samples, down to no delay at all, with
    a crossfade when the delay time changes (issue #1104).

  ==============================================================================
*/

#ifndef YSE_DSP_DELAY_SAMPLEDELAY_HPP
#define YSE_DSP_DELAY_SAMPLEDELAY_HPP

#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Delays a signal by a number of samples: alignment, short
     *         delays, Karplus-Strong.
     *
     *  Ported from Max's ``delay~``. Unlike ``delay`` and ``delayLine``,
     *  whose shortest delay is one block, the delay here can be anything from
     *  0 (the input passes unchanged) to the maximum, because each sample is
     *  written before it is read. The price is that nothing can feed the
     *  output back into the input within a block; use ``comb`` or
     *  ``delayLine`` for feedback.
     *
     *  **Delay time.** ``delay()`` sets it in samples: whole samples, a
     *  fraction is dropped (``2.9`` is 2). Samples do not follow the sample
     *  rate, so the same number lasts longer at a lower rate. ``delayMs()``
     *  sets it in milliseconds instead, converted with the live
     *  ``SAMPLERATE`` at the start of every block and rounded to the nearest
     *  sample, so it lasts the same time at any rate. Either is clamped to 0
     *  to ``maximumDelay()``. A value that is not a finite number is ignored.
     *
     *  **Ramp.** With a ramp time (``ramp()``, in ms, converted with the live
     *  ``SAMPLERATE`` when a change starts), a new delay time does not jump:
     *  the output crossfades linearly from the old delay to the new one over
     *  the ramp, so a change does not click. A ramp of 0 jumps. A new time
     *  that arrives during a crossfade starts a new one, from the delay that
     *  was fading in.
     *
     *  **Signal delay.** The block call with a delay signal reads the delay
     *  per sample, in samples, with linear interpolation between whole
     *  samples, clamped to 0 .. ``maximumDelay()``; a sample that is not a
     *  finite number reads as 0. It bypasses the ramp, and leaves the number
     *  set with ``delay()`` alone for the calls without a signal.
     *
     *  **Memory.** The constructor allocates the line for @p memorySamples;
     *  nothing allocates after that. ``maximumDelay()`` sets the longest
     *  delay within that memory, and clears the line. To get more memory,
     *  construct a new ``sampleDelay`` off the audio thread.
     *
     *  An input sample that is not a finite number is written as 0.
     *  ``clear()`` silences the line. Each call processes at most
     *  ``STANDARD_BUFFERSIZE`` samples; the output is sized in the
     *  constructor. Call it from one thread at a time.
     */
    class API sampleDelay {
    public:
      /** @brief The memory when none is given, in samples (as in Max). */
      static constexpr UInt DEFAULT_MEMORY = 512;

      /** @brief The largest memory, in samples (a minute at 192 kHz); more
       *         is clamped to it. */
      static constexpr UInt MAX_MEMORY = 11520000;

      /**
       *  @brief A silent delay line holding up to @p memorySamples samples.
       *
       *  Allocates. The maximum delay is the memory, the delay and the ramp
       *  start at 0.
       *
       *  @param memorySamples The longest delay in samples, clamped to 0 to
       *                       ``MAX_MEMORY``.
       */
      explicit sampleDelay(UInt memorySamples = DEFAULT_MEMORY);

      /** @brief The memory in samples, as clamped by the constructor. */
      UInt memory() const {
        return memorySize;
      }

      /** @brief Set the longest delay in samples, clamped to 0 to
       *         ``memory()``, and clear the line. Does not allocate. */
      sampleDelay& maximumDelay(UInt samples);

      /** @brief The longest delay in samples. */
      UInt maximumDelay() const {
        return maxDelay;
      }

      /** @brief The delay in samples; a fraction is dropped. Non-finite:
       *         ignored. */
      sampleDelay& delay(Flt samples);

      /** @brief The delay in ms, converted at the live sample rate on every
       *         block. Non-finite: ignored. */
      sampleDelay& delayMs(Flt ms);

      /** @brief The delay as last set: samples, or ms after ``delayMs()``. */
      Flt delay() const {
        return timeValue;
      }

      /** @brief Whether the delay was last set in ms. */
      bool delayInMs() const {
        return timeInMs;
      }

      /** @brief The delay in whole samples the next block heads for, at the
       *         live sample rate and after clamping. */
      UInt delaySamples() const;

      /** @brief The crossfade time for a change of delay, in ms; 0 jumps.
       *         Below 0 or non-finite: ignored. */
      sampleDelay& ramp(Flt ms);

      /** @brief The ramp time in ms. */
      Flt ramp() const {
        return rampMs;
      }

      /** @brief Whether a crossfade is under way. */
      bool ramping() const {
        return fadeLength != 0;
      }

      /** @brief Silences the line. */
      sampleDelay& clear();

      /** @brief One sample at the current delay. */
      Flt process(Flt in);

      /** @brief One block at the current delay. */
      buffer& operator()(buffer& in);

      /** @brief One block with the delay in samples read per sample from
       *         @p delaySignal, interpolated. @p delaySignal is at least as
       *         long as @p in. */
      buffer& operator()(buffer& in, buffer& delaySignal);

    private:
      // Starts a crossfade, or jumps, when the delay the settings ask for
      // differs from the one playing.
      void Follow();
      Flt Step(Flt in);
      void Write(Flt in);
      Flt Tap(UInt back) const;

      std::vector<Flt> line;
      buffer output;
      std::size_t writePos = 0;
      UInt memorySize = 0;
      UInt maxDelay = 0;

      Flt timeValue = 0.f;
      bool timeInMs = false;
      Flt rampMs = 0.f;

      // The delay playing and, during a crossfade, the one fading out.
      UInt current = 0;
      UInt previous = 0;
      UInt fadeLength = 0; // 0: no crossfade
      UInt fadeDone = 0;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_DELAY_SAMPLEDELAY_HPP

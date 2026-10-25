/*
  ==============================================================================

    comb.hpp
    A comb filter with a feedforward and a feedback path on interpolated
    delay lines, each delay and each gain settable at signal rate
    (issue #1094).

  ==============================================================================
*/

#ifndef YSE_DSP_DELAY_COMB_HPP
#define YSE_DSP_DELAY_COMB_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"
#include "delayHistory.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Comb filter with a feedforward and a feedback path, each with
     *         its own delay: flanging, Karplus-Strong strings, resonators.
     *
     *  Ported from Max's ``comb~`` and ``teeth~``. Each sample is
     *
     *  ``y(n) = a * x(n) + b * x(n-F) + c * y(n-B)``
     *
     *  with ``a`` the gain, ``b`` the feedforward gain, ``c`` the feedback
     *  gain, ``F`` the feedforward delay and ``B`` the feedback delay, in
     *  samples. ``comb~`` is the case ``F = B``, which ``delay()`` sets; the
     *  separate delays are ``teeth~``. With ``c = 0`` it is a feedforward
     *  comb (notches at odd multiples of half the delay's frequency when
     *  ``a = b``); with ``b = 0`` a feedback comb, whose peaks ring at
     *  multiples of ``1 / B`` and decay by ``c`` per echo.
     *
     *  **Delays.** Set in milliseconds and converted with the live
     *  ``SAMPLERATE`` on every sample, so they follow a change of rate. A
     *  fractional delay is read with linear interpolation. Each delay is
     *  clamped to one sample at the least (``y(n-0)`` is the sample being
     *  computed) and to the maximum delay at the most.
     *
     *  **Gains.** The gain and the feedforward gain take any finite value.
     *  The feedback gain is clamped to -1 .. 1: beyond that the recursion
     *  grows without bound. At exactly 1 or -1 it does not decay and rings
     *  until ``clear()``.
     *
     *  **Memory.** The constructor allocates both delay lines for the
     *  maximum delay at the highest sample rate it supports. Nothing
     *  allocates after that. At a higher rate the longest delay is shorter
     *  than the maximum (``capacity()`` tells how long). The maximum is
     *  clamped to 0 .. ``MAX_MAXIMUM_DELAY`` ms. To change it, assign a new
     *  ``comb``, off the audio thread.
     *
     *  A setting that is not a finite number is ignored, an input sample
     *  that is not one is read as 0, and an output that overflows a float is
     *  written as 0, so none of them can latch the filter. ``clear()``
     *  silences both lines.
     *
     *  Each call processes at most ``STANDARD_BUFFERSIZE`` samples. Call it
     *  from one thread at a time. Processing allocates nothing: the output is
     *  sized to ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API comb {
    public:
      /** @brief The sample rate the default constructor sizes the lines
       *         for. */
      static constexpr UInt HIGHEST_SAMPLERATE = 192000;

      /** @brief The maximum delay when none is given, in ms (as in Max). */
      static constexpr Flt DEFAULT_MAXIMUM_DELAY = 10.f;

      /** @brief The longest maximum delay, in ms; larger ones are clamped
       *         to it. */
      static constexpr Flt MAX_MAXIMUM_DELAY = 60000.f;

      /**
       *  @brief A silent filter whose delays can reach @p maximumDelayMs.
       *
       *  Allocates. The delays and all three gains start at 0, so the
       *  output is silent until a gain is set.
       *
       *  @param maximumDelayMs     The longest delay in ms, clamped to 0 ..
       *                            ``MAX_MAXIMUM_DELAY``; not finite: the
       *                            default.
       *  @param highestSampleRate  The highest rate the memory is sized for.
       */
      explicit comb(Flt maximumDelayMs = DEFAULT_MAXIMUM_DELAY,
                    UInt highestSampleRate = HIGHEST_SAMPLERATE);

      /** @brief Both delays in ms (``comb~``). Non-finite: ignored. */
      comb& delay(Flt ms);

      /** @brief The feedforward delay in ms. Non-finite: ignored. */
      comb& feedforwardDelay(Flt ms);

      /** @brief The feedback delay in ms. Non-finite: ignored. */
      comb& feedbackDelay(Flt ms);

      /** @brief The gain ``a`` of the direct input. Non-finite: ignored. */
      comb& gain(Flt value);

      /** @brief The gain ``b`` of the delayed input. Non-finite: ignored. */
      comb& feedforward(Flt value);

      /** @brief The gain ``c`` of the delayed output, clamped to -1 .. 1.
       *         Non-finite: ignored. */
      comb& feedback(Flt value);

      /** @brief Silences both delay lines. */
      comb& clear();

      /** @brief The feedforward delay in ms as last set. */
      Flt feedforwardDelay() const {
        return ffMs;
      }

      /** @brief The feedback delay in ms as last set. */
      Flt feedbackDelay() const {
        return fbMs;
      }

      /** @brief The gain as last set. */
      Flt gain() const {
        return a;
      }

      /** @brief The feedforward gain as last set. */
      Flt feedforward() const {
        return b;
      }

      /** @brief The feedback gain as last set, clamped. */
      Flt feedback() const {
        return c;
      }

      /** @brief The maximum delay in ms, as clamped by the constructor. */
      Flt maximumDelay() const {
        return history.MaximumMs();
      }

      /** @brief The longest delay in ms at the live sample rate: the maximum
       *         delay, or less when the rate exceeds the one the memory was
       *         sized for. */
      Flt capacity() const;

      /** @brief The feedforward delay in samples at the live sample rate,
       *         after clamping. */
      Flt feedforwardSamples() const;

      /** @brief The feedback delay in samples at the live sample rate, after
       *         clamping. */
      Flt feedbackSamples() const;

      /** @brief One sample. */
      Flt process(Flt in);

      /** @brief One block at the current settings. */
      buffer& operator()(buffer& in);

      /** @brief One block with both delays (ms) read per sample from
       *         @p delaySignal and the three gains from @p gainSignal,
       *         @p feedforwardSignal and @p feedbackSignal (``comb~``). Each
       *         signal is at least as long as @p in; they also set the
       *         getters. */
      buffer& operator()(buffer& in, buffer& delaySignal, buffer& gainSignal,
                         buffer& feedforwardSignal, buffer& feedbackSignal);

      /** @brief One block with the feedforward and the feedback delay (ms)
       *         and the three gains read per sample (``teeth~``). Each signal
       *         is at least as long as @p in; they also set the getters. */
      buffer& operator()(buffer& in, buffer& feedforwardDelaySignal, buffer& feedbackDelaySignal,
                         buffer& gainSignal, buffer& feedforwardSignal, buffer& feedbackSignal);

    private:
      Flt Step(Flt in, Flt ffSamples, Flt fbSamples);

      buffer output;
      detail::delayHistory history;

      Flt ffMs = 0.f;
      Flt fbMs = 0.f;
      Flt a = 0.f;
      Flt b = 0.f;
      Flt c = 0.f;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_DELAY_COMB_HPP

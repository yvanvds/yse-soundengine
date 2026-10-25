/*
  ==============================================================================

    allpass.hpp
    A Schroeder allpass filter on an interpolated delay line, with a delay
    time and a gain settable at signal rate (issue #1093).

  ==============================================================================
*/

#ifndef YSE_DSP_FILTERS_ALLPASS_HPP
#define YSE_DSP_FILTERS_ALLPASS_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"
#include "../delay/delayHistory.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Schroeder allpass filter: a delay line with a feedforward and
     *         a feedback path of opposite gain, for reverb diffusers and
     *         phase smearing.
     *
     *  Ported from Max's ``allpass~``. Each sample is
     *
     *  ``y(n) = -g * x(n) + x(n-D) + g * y(n-D)``
     *
     *  with ``D`` the delay in samples and ``g`` the gain, so the transfer
     *  function is ``H(z) = (-g + z^-D) / (1 - g * z^-D)``: every frequency
     *  passes at gain 1 and only the phase changes. Sharp transients are
     *  smeared into a train of echoes ``D`` samples apart that decays by
     *  ``g`` per echo. A gain of 0 is a plain delay.
     *
     *  **Delay.** The delay is set in milliseconds and converted with the
     *  live ``SAMPLERATE`` on every sample, so it follows a change of rate.
     *  A fractional delay is read with linear interpolation, the same one
     *  for both paths; then the response is allpass only approximately near
     *  the Nyquist frequency (interpolation is a gentle lowpass), and exactly
     *  at whole-sample delays. The delay is clamped to one sample at the
     *  least (``y(n-0)`` is the sample being computed) and to the maximum
     *  delay at the most.
     *
     *  **Gain.** Clamped to -1 .. 1: beyond that the feedback grows without
     *  bound. At exactly 1 or -1 the poles sit on the zeros and the transfer
     *  function is the constant ``-g``, which the filter then outputs
     *  directly (``y(n) = -g * x(n)``). In the recursion the memory of an
     *  earlier setting would circulate there forever, never decaying; this
     *  way switching a running filter to +-1 leaves no residue, and switching
     *  back starts from a memory consistent with the new gain.
     *
     *  **Memory.** The constructor allocates both delay lines for the
     *  maximum delay at the highest sample rate it supports. Nothing
     *  allocates after that. At a lower rate the line uses less of its
     *  memory; at a higher one the longest delay is shorter than the maximum
     *  (``capacity()`` tells how long). The maximum is clamped to 0 ..
     *  ``MAX_MAXIMUM_DELAY`` ms. To change it, assign a new ``allpass``, off
     *  the audio thread.
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
    class API allpass {
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
       *  @brief A silent filter whose delay can reach @p maximumDelayMs.
       *
       *  Allocates. The delay and the gain start at 0.
       *
       *  @param maximumDelayMs     The longest delay in ms, clamped to 0 ..
       *                            ``MAX_MAXIMUM_DELAY``; not finite: the
       *                            default.
       *  @param highestSampleRate  The highest rate the memory is sized for.
       */
      explicit allpass(Flt maximumDelayMs = DEFAULT_MAXIMUM_DELAY,
                       UInt highestSampleRate = HIGHEST_SAMPLERATE);

      /** @brief The delay in ms, clamped when read (see the class
       *         description). Non-finite: ignored. */
      allpass& delay(Flt ms);

      /** @brief The gain, clamped to -1 .. 1. Non-finite: ignored. */
      allpass& gain(Flt value);

      /** @brief Silences both delay lines. */
      allpass& clear();

      /** @brief The delay in ms as last set. */
      Flt delay() const {
        return delayMs;
      }

      /** @brief The gain as last set, clamped. */
      Flt gain() const {
        return g;
      }

      /** @brief The maximum delay in ms, as clamped by the constructor. */
      Flt maximumDelay() const {
        return history.MaximumMs();
      }

      /** @brief The longest delay in ms at the live sample rate: the maximum
       *         delay, or less when the rate exceeds the one the memory was
       *         sized for. */
      Flt capacity() const;

      /** @brief The delay in samples the filter reads at the live sample
       *         rate, after clamping. */
      Flt delaySamples() const;

      /** @brief One sample. */
      Flt process(Flt in);

      /** @brief One block at the current settings. */
      buffer& operator()(buffer& in);

      /** @brief One block with the delay (ms) and the gain read per sample
       *         from @p delaySignal and @p gainSignal (each at least as long
       *         as @p in), which also set the getters. */
      buffer& operator()(buffer& in, buffer& delaySignal, buffer& gainSignal);

    private:
      Flt Step(Flt in, Flt samples);

      buffer output;
      detail::delayHistory history;

      Flt delayMs = 0.f;
      Flt g = 0.f;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_FILTERS_ALLPASS_HPP

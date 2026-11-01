/*
  ==============================================================================

    spikeDetector.hpp
    The interval between zero to non-zero transitions of a signal, with a
    refractory period (issue #1123).

  ==============================================================================
*/

#ifndef YSE_DSP_ANALYSIS_SPIKEDETECTOR_HPP
#define YSE_DSP_ANALYSIS_SPIKEDETECTOR_HPP

#include <cstdint>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"
#include "edgeDetector.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Measures the time between the zero to non-zero transitions
     *         (rises) of a signal, in milliseconds: onset and tempo
     *         intervals of a pulse train or a gate.
     *
     *  The rises are ``edgeDetector``'s: a sample is zero when it compares
     *  equal to 0, the sample before the first is zero, and the state at the
     *  end of a block carries into the next. Each rise that is reported
     *  gives the time since the previous reported rise, sample-accurate.
     *  The first one gives the time since construction or ``reset()``.
     *
     *  The refractory period is a time after a reported rise during which
     *  further rises are ignored: a rise is reported only when at least the
     *  refractory period has passed since the last reported one, so every
     *  interval is at least the refractory period. ``restart()`` starts the
     *  refractory period again from the next sample, without a rise. A
     *  refractory period of 0 (the default) reports every rise.
     *
     *  Time is counted in samples and turned into milliseconds with the
     *  live ``SAMPLERATE``, read on every call, as is the refractory period.
     *
     *  A block call reports every rise it reported in the block: how many,
     *  the sample index of each and its interval, in order, until the next
     *  block call. A block holds at most ``STANDARD_BUFFERSIZE`` samples; a
     *  longer input is processed up to that length.
     *
     *  Call it from one thread at a time. Nothing allocates: the report
     *  lists are fixed arrays inside the object.
     */
    class API spikeDetector {
    public:
      spikeDetector();

      /** @brief One sample: true if @p in rises and the rise is reported;
       *         its interval is then ``lastInterval()``. Leaves the block
       *         report of the last block call unchanged. */
      bool process(Flt in);

      /** @brief One block: finds the reported rises in @p in and replaces
       *         the report of the previous block call with them.
       *  @return The number of reported rises. */
      UInt operator()(buffer& in);

      /** @brief Reported rises in the last block call. */
      UInt spikes() const;

      /** @brief The sample index of reported rise @p i in the last block
       *         call, in order; @p i must be below ``spikes()``. */
      UInt spike(UInt i) const;

      /** @brief The interval of reported rise @p i in the last block call,
       *         in milliseconds; @p i must be below ``spikes()``. */
      Flt interval(UInt i) const;

      /** @brief The interval of the most recent reported rise, in
       *         milliseconds, from any call; 0 before the first. */
      Flt lastInterval() const;

      /** @brief Sets the refractory period in milliseconds. A negative
       *         value or one that is not a number is 0. */
      spikeDetector& refractory(Flt ms);

      /** @brief The refractory period in milliseconds. */
      Flt refractory() const;

      /** @brief Starts the refractory period again from the next sample: a
       *         rise is reported only once it has passed. */
      spikeDetector& restart();

      /** @brief Back to the state after construction: the signal is zero,
       *         time counts from the next sample, no refractory period is
       *         running and the reports are cleared. Keeps the refractory
       *         period setting. */
      spikeDetector& reset();

    private:
      bool accept(std::uint64_t at, Dbl holdSamples, Dbl msPerSample);

      edgeDetector edges;
      Flt refractoryMs;
      std::uint64_t now; // samples processed since construction or reset()
      std::uint64_t last; // time of the last reported rise
      std::uint64_t holdFrom; // where the running refractory period started
      bool holding;
      Flt latest;
      UInt count;
      UInt pos[STANDARD_BUFFERSIZE];
      Flt intervals[STANDARD_BUFFERSIZE];
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_ANALYSIS_SPIKEDETECTOR_HPP

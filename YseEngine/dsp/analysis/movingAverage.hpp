/*
  ==============================================================================

    movingAverage.hpp
    The running average of a signal over its last N samples, bipolar,
    absolute or RMS (issue #1118), and the average since the last report
    (issue #1119).

  ==============================================================================
*/

#ifndef YSE_DSP_ANALYSIS_MOVINGAVERAGE_HPP
#define YSE_DSP_ANALYSIS_MOVINGAVERAGE_HPP

#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief The average of a signal over its last N samples, as a signal:
     *         envelope following and level detection.
     *
     *  Every output sample is the average of the window that ends on the
     *  matching input sample, the last ``length()`` samples. Three modes:
     *
     *  - ``BIPOLAR`` (the default): the plain mean, ``sum(x) / N``. A
     *    signal centred on 0 averages near 0; DC passes.
     *  - ``ABSOLUTE``: the mean magnitude, ``sum(|x|) / N``.
     *  - ``RMS``: the root mean square, ``sqrt(sum(x * x) / N)``.
     *
     *  The window always divides by its full length: before N samples have
     *  arrived, the missing ones count as 0, so the average ramps up from
     *  silence. Samples that are NaN or infinite count as 0, so one bad
     *  sample cannot poison the output.
     *
     *  The capacity, the longest window, is fixed at construction, and the
     *  history is allocated there: processing never allocates and costs
     *  O(1) per sample, a running sum plus one sample in and one out. The
     *  running sum is rebuilt from scratch every N samples, so rounding
     *  never drifts. The object keeps the last ``capacity()`` samples, so
     *  ``length()`` can change at any time, also from the audio thread,
     *  within the capacity; the new window covers the samples already seen.
     *  Changing the length or the mode recomputes the sum over the new
     *  window, at most ``capacity()`` steps, once; nothing allocates.
     *
     *  No state depends on the sample rate: the window is in samples.
     *
     *  A block call fills ``output()``, a buffer the object owns. A block
     *  holds at most ``STANDARD_BUFFERSIZE`` samples; a longer input is
     *  processed up to that length. Call it from one thread at a time.
     *
     *  Accumulating: besides the window, every sample taken also counts
     *  toward the average since the last report, in the same mode, with
     *  NaN and infinities as 0. ``take()`` reports it and starts over;
     *  ``accumulated()`` reads it without starting over. An empty
     *  accumulation averages to 0. ``accumulate()`` feeds a block to the
     *  accumulation alone, for a caller that only polls: the window does
     *  not see those samples, and it costs one sum per sample. A new mode
     *  starts the accumulation over, so it never mixes two modes;
     *  ``reset()`` clears it, a new length does not. The sums are doubles,
     *  so hours of samples between reports lose no precision that matters.
     */
    class API movingAverage {
    public:
      /** @brief What is averaged. */
      enum MODE {
        BIPOLAR, ///< the samples themselves
        ABSOLUTE, ///< their absolute values
        RMS, ///< their squares, then the square root of the mean
      };

      /** @brief The capacity of a default-constructed object, in samples. */
      static constexpr UInt DEFAULT_CAPACITY = 100;
      /** @brief The largest capacity, in samples (about 22 s at 48 kHz). */
      static constexpr UInt MAXIMUM_CAPACITY = 1u << 20;

      /**
       *  @brief A silent average whose window can reach @p capacity samples.
       *         The window starts at the full capacity, in ``BIPOLAR`` mode.
       *  @param capacity  The longest window in samples, clamped to 1 ..
       *                   ``MAXIMUM_CAPACITY``. Allocated here.
       */
      explicit movingAverage(UInt capacity = DEFAULT_CAPACITY);

      /** @brief One sample: takes @p in and returns the average of the
       *         window that ends on it. */
      Flt process(Flt in);

      /** @brief One block: fills ``output()`` with the average at every
       *         sample of @p in. */
      buffer& operator()(buffer& in);

      /** @brief The output of the last block call. */
      buffer& output();

      /** @brief The average of the window that ends on the last sample
       *         taken; 0 before the first. */
      Flt value() const;

      /** @brief Sets the window in samples, clamped to 1 .. ``capacity()``. */
      movingAverage& length(UInt samples);

      /** @brief The window in samples. */
      UInt length() const;

      /** @brief The longest window, fixed at construction. */
      UInt capacity() const;

      /** @brief Sets what is averaged; an unknown mode is ``BIPOLAR``. */
      movingAverage& mode(MODE m);

      /** @brief What is averaged. */
      MODE mode() const;

      /** @brief One block into the accumulation only: @p in counts toward
       *         the next ``take()``, but the window, ``value()`` and
       *         ``output()`` do not see it. */
      movingAverage& accumulate(buffer& in);

      /** @brief The average, in the current mode, of every sample since
       *         the last ``take()``, without starting over; 0 when there
       *         is none. */
      Flt accumulated() const;

      /** @brief The number of samples since the last ``take()``. */
      Dbl accumulatedSamples() const;

      /** @brief Reports: returns ``accumulated()`` and starts a new
       *         accumulation. */
      Flt take();

      /** @brief Back to silence: forgets every sample, the accumulation
       *         too. Keeps the length, the mode and the capacity. */
      movingAverage& reset();

    private:
      Dbl contribution(Flt x) const;
      Flt average() const;
      Flt mean(Dbl total, Dbl samples) const;
      void recompute();

      std::vector<Flt> history; // the last capacity samples, a ring
      UInt cap;
      UInt len;
      UInt pos; // where the next sample goes
      MODE how;
      Dbl sum; // the running sum over the window
      Dbl fresh; // the sum of the samples since the last rebuild
      UInt filled; // how many samples are in fresh
      Flt latest;
      Dbl accSum; // the sum since the last take()
      Dbl accCount; // the samples since the last take()
      buffer out;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_ANALYSIS_MOVINGAVERAGE_HPP

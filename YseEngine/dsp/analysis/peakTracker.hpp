/*
  ==============================================================================

    peakTracker.hpp
    The largest absolute amplitude of a signal since the last report
    (issue #1126).

  ==============================================================================
*/

#ifndef YSE_DSP_ANALYSIS_PEAKTRACKER_HPP
#define YSE_DSP_ANALYSIS_PEAKTRACKER_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Tracks the peak of a signal, the largest absolute amplitude
     *         since the last report: level metering for control logic.
     *
     *  Every sample whose absolute value is above the peak so far becomes
     *  the peak. NaN samples are skipped; an infinity is a peak like any
     *  other value. With no samples since the last report the peak is 0.
     *
     *  A report ends the window: it gives the peak and starts a new window
     *  at 0. ``take()`` reports at once. With an interval set, a report
     *  also falls every interval, sample-accurate: the report point is the
     *  last sample of the window, and the samples after it in the same
     *  block belong to the next window. An interval shorter than one sample
     *  reports on every sample. The interval is turned into samples with
     *  the live ``SAMPLERATE``, read on every call, and a fractional period
     *  does not drift: the reports keep to the interval on average.
     *
     *  By default the peak is a magnitude, never negative. In signed mode
     *  it is the sample with the largest magnitude, with its sign: of a
     *  window holding 0.5 and -0.8 it is -0.8. Of two samples with equal
     *  magnitude the first stays the peak.
     *
     *  A block call reports every interval report in the block: how many,
     *  the sample index of each and its peak, in order, until the next
     *  block call. A block holds at most ``STANDARD_BUFFERSIZE`` samples; a
     *  longer input is processed up to that length.
     *
     *  Call it from one thread at a time. Nothing allocates: the report
     *  lists are fixed arrays inside the object.
     */
    class API peakTracker {
    public:
      peakTracker();

      /** @brief One sample: true if an interval report falls on @p in;
       *         its peak is then ``lastReport()``. Leaves the block report
       *         of the last block call unchanged. */
      bool process(Flt in);

      /** @brief One block: tracks the peak of @p in and replaces the report
       *         of the previous block call with the interval reports in it.
       *  @return The number of interval reports. */
      UInt operator()(buffer& in);

      /** @brief Interval reports in the last block call. */
      UInt reports() const;

      /** @brief The sample index of interval report @p i in the last block
       *         call, in order; @p i must be below ``reports()``. */
      UInt report(UInt i) const;

      /** @brief The peak of interval report @p i in the last block call;
       *         @p i must be below ``reports()``. */
      Flt value(UInt i) const;

      /** @brief The most recent report, from ``take()`` or an interval;
       *         0 before the first. */
      Flt lastReport() const;

      /** @brief The peak of the running window, without ending it. */
      Flt peak() const;

      /** @brief Reports now: returns the peak of the running window and
       *         starts a new one, with the interval counted from here. */
      Flt take();

      /** @brief Sets the reporting interval in milliseconds; 0 reports
       *         only on ``take()``. A negative value or one that is not a
       *         number is 0. A new value counts the interval from here. */
      peakTracker& interval(Flt ms);

      /** @brief The reporting interval in milliseconds. */
      Flt interval() const;

      /** @brief Signed mode: the peak keeps the sign of its sample. */
      peakTracker& signedPeak(bool on);

      /** @brief True in signed mode. */
      bool signedPeak() const;

      /** @brief Back to the state after construction: an empty window, the
       *         interval counted from the next sample, no report yet. Keeps
       *         the interval and the mode. */
      peakTracker& reset();

    private:
      void track(Flt in);
      bool tick(Dbl period);
      Dbl period() const;

      Flt intervalMs;
      bool keepSign;
      Flt best; // the peak of the running window, signed in signed mode
      Flt bestAbs; // its magnitude
      Dbl elapsed; // samples in the running window, toward the interval
      Flt latest;
      UInt count;
      UInt pos[STANDARD_BUFFERSIZE];
      Flt values[STANDARD_BUFFERSIZE];
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_ANALYSIS_PEAKTRACKER_HPP

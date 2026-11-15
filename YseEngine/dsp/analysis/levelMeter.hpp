/*
  ==============================================================================

    levelMeter.hpp
    The level of a signal for a meter display: peak or RMS per update
    interval, with ballistics, peak hold and a clip indicator (issue #1166).

  ==============================================================================
*/

#ifndef YSE_DSP_ANALYSIS_LEVELMETER_HPP
#define YSE_DSP_ANALYSIS_LEVELMETER_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Measures the level of a signal for a meter: the peak or the
     *         RMS of every update interval, smoothed by attack and release
     *         ballistics, with a peak hold and a clip indicator. The meter
     *         owns the numbers; whoever shows them does the drawing.
     *
     *  **Windows.** The signal is measured in windows of one update
     *  interval (20 ms by default). The detector gives each window one
     *  value: the largest absolute sample (``PEAK``, the default) or the
     *  root mean square of its samples (``RMS``). A window ends on its last
     *  sample, sample-accurate across blocks; the interval is turned into
     *  samples with the live ``SAMPLERATE``, read on every call, and a
     *  fractional period keeps its fraction so the windows do not drift. An
     *  interval below one sample is one sample. NaN samples are skipped; a
     *  window with none is 0.
     *
     *  **Ballistics.** At the end of each window the level moves toward the
     *  window's value. Rising it uses the attack time, falling the release
     *  time, each the time constant of a one-pole: after a window of ``T``
     *  ms the level covers ``1 - exp(-T / time)`` of the distance, so after
     *  ``time`` ms of a steady value it has covered 63 % of a step. A time
     *  of 0 jumps at once, so with both at 0 (the default) the level is the
     *  value of the last window. ``ballistics()`` sets the attack, the
     *  release and the dB offset of a standard meter (VU, the DIN, BBC and
     *  Nordic PPMs, EBU digital). A level below 1e-10 (-200 dB) is 0.
     *
     *  **Peak hold.** The held peak is the highest level, kept for the hold
     *  time after it was reached and then dropped to the level, which it
     *  follows down until the level rises again. A hold time of 0 (the
     *  default) makes it the level.
     *
     *  **Clip.** The clip indicator turns on when a sample, or a value given
     *  to ``set()``, is above 1 in magnitude (0 dBFS), whatever the
     *  detector, and stays on until ``clear()``.
     *
     *  **Decibels.** ``levelDb()`` and ``peakHoldDb()`` are
     *  ``20 log10(level)`` minus the offset, and ``SILENCE`` (-200) for 0 or
     *  anything below it. The offset is the reference level, the dBFS that
     *  reads 0: a VU meter's -20 reads 0 for -20 dBFS.
     *
     *  Call it from one thread at a time. Nothing allocates.
     */
    class API levelMeter {
    public:
      /** @brief What one window is worth. */
      enum DETECTOR {
        PEAK, ///< the largest absolute sample of the window
        RMS, ///< the root mean square of the window's samples
      };

      /** @brief The ballistics of standard meters, as Max's
       *         ``levelmeter~`` modes 0 to 5: attack, release and dB
       *         offset. */
      enum BALLISTICS {
        FAST, ///< 0, 0, 0 dB: no ballistics, no offset
        VU, ///< 330 ms, 330 ms, -20 dB
        DIN, ///< DIN 45406 PPM: 10 ms, 1500 ms, -14 dB
        BBC, ///< BBC PPM: 10 ms, 3000 ms, -14 dB
        NORDIC, ///< Nordic PPM: 5 ms, 2000 ms, -14 dB
        EBU, ///< EBU digital: 0 ms, 1500 ms, -14 dB
      };

      /** @brief The attack, the release and the offset of a standard. */
      struct ballisticsSettings {
        Flt attack; ///< ms
        Flt release; ///< ms
        Flt offset; ///< dB
      };

      /** @brief The decibel value of silence, and the lowest one given. */
      static constexpr Flt SILENCE = -200.f;

      levelMeter();

      /** @brief One sample: true if a window ends on @p in. */
      bool process(Flt in);

      /** @brief One block of any length.
       *  @return The number of windows that ended in it. */
      UInt operator()(buffer& in);

      /** @brief Windows that ended in the last block call. */
      UInt reports() const;

      /** @brief Shows @p amplitude now, as if a window with that value had
       *         just ended: its magnitude goes through the ballistics and
       *         the hold, and turns the clip indicator on when it is above
       *         1. The window being measured goes on. NaN is ignored. */
      levelMeter& set(Flt amplitude);

      /** @brief ``set()`` from a reading in dB, the offset included: the
       *         amplitude ``10^((dB + offset) / 20)``. NaN is ignored. */
      levelMeter& setDb(Flt dB);

      /** @brief The level after the ballistics, as an amplitude. */
      Flt level() const;

      /** @brief The level in dB, offset included. */
      Flt levelDb() const;

      /** @brief The held peak, as an amplitude. */
      Flt peakHold() const;

      /** @brief The held peak in dB, offset included. */
      Flt peakHoldDb() const;

      /** @brief True after a sample above 1 in magnitude, until
       *         ``clear()``. */
      bool clipped() const;

      /** @brief Turns the clip indicator off and drops the held peak to the
       *         level. */
      levelMeter& clear();

      /** @brief Back to silence: the level, the hold, the clip indicator and
       *         the window being measured. Keeps the settings. */
      levelMeter& reset();

      /** @brief Sets the detector. Takes effect at the next window end. */
      levelMeter& detector(DETECTOR d);

      /** @brief The detector. */
      DETECTOR detector() const;

      /** @brief Sets the update interval, the length of a window, in ms. A
       *         value below one sample is one sample; NaN is ignored. A new
       *         value counts the window from here. */
      levelMeter& interval(Flt ms);

      /** @brief The update interval in ms. */
      Flt interval() const;

      /** @brief Sets the attack time in ms; negative or NaN is 0. */
      levelMeter& attack(Flt ms);

      /** @brief The attack time in ms. */
      Flt attack() const;

      /** @brief Sets the release time in ms; negative or NaN is 0. */
      levelMeter& release(Flt ms);

      /** @brief The release time in ms. */
      Flt release() const;

      /** @brief Sets the peak hold time in ms; negative or NaN is 0. */
      levelMeter& hold(Flt ms);

      /** @brief The peak hold time in ms. */
      Flt hold() const;

      /** @brief Sets the offset, the reference level in dBFS that reads 0
       *         dB; it is subtracted from every reading. NaN is 0. */
      levelMeter& offset(Flt dB);

      /** @brief The dB offset. */
      Flt offset() const;

      /** @brief Sets the attack, the release and the offset of a standard
       *         meter. */
      levelMeter& ballistics(BALLISTICS standard);

      /** @brief The attack, the release and the offset of @p standard. */
      static ballisticsSettings settings(BALLISTICS standard);

      /** @brief Where @p dB lies on a scale from @p floorDb (0) to
       *         @p ceilingDb (1): ``(dB - floor) / (ceiling - floor)``, 0 at
       *         or below the floor and above 1 past the ceiling. 0 for an
       *         empty scale or NaN. */
      static Flt position(Flt dB, Flt floorDb, Flt ceilingDb);

    private:
      void track(Flt in);
      void endWindow(Dbl samples);
      void apply(Flt value, Flt ms);
      Flt toDb(Flt amplitude) const;
      Dbl period() const;

      DETECTOR mode;
      Flt intervalMs;
      Flt attackMs;
      Flt releaseMs;
      Flt holdMs;
      Flt offsetDb;

      Flt windowPeak;
      Dbl windowSquares;
      UInt windowCount;
      Dbl elapsed; // samples in the running window, toward the interval

      Flt current; // the level after the ballistics
      Flt held;
      Flt holdLeft; // ms
      bool clip;
      UInt count;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_ANALYSIS_LEVELMETER_HPP

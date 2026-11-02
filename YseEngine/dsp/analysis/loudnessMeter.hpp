/*
  ==============================================================================

    loudnessMeter.hpp
    Momentary, short-term and integrated loudness in LUFS after ITU-R
    BS.1770 and EBU R 128 (issue #1129), loudness range after EBU Tech 3342
    and true peak after BS.1770 Annex 2 (issue #1237).

  ==============================================================================
*/

#ifndef YSE_DSP_ANALYSIS_LOUDNESSMETER_HPP
#define YSE_DSP_ANALYSIS_LOUDNESSMETER_HPP

#include <array>
#include <cstdint>
#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A loudness meter after ITU-R BS.1770-4 and EBU R 128: the
     *         momentary, short-term and integrated loudness of one or more
     *         channels in LUFS, their loudness range in LU, and their sample
     *         peak in dBFS and true peak in dBTP.
     *
     *  **K-weighting.** Every channel runs through the two biquads of
     *  BS.1770, a high shelf (about +4 dB above 1.5 kHz) and a high-pass
     *  (about 38 Hz), in double precision. Their coefficients are derived
     *  from the analog prototype for the live ``SAMPLERATE``, so they match
     *  the standard's 48 kHz table there (``Shelf()`` and ``HighPass()``) and
     *  keep the same response at any other rate. A sample that is not a
     *  finite number enters the filters as 0.
     *
     *  **Loudness.** The weighted mean square of each channel is multiplied
     *  by the channel's weight (1 by default; BS.1770 gives 1.41 to the
     *  surround channels, see ``weight()``) and summed over the channels:
     *  ``L = -0.691 + 10 log10(sum)``. The signal is measured in steps of
     *  100 ms (rounded to whole samples), each split in 20 sub-steps of
     *  5 ms. The momentary loudness is the mean over the last 80 sub-steps
     *  (400 ms) and changes every sub-step, so that the window slides
     *  closely enough over a short tone to find its loudness, as EBU Tech
     *  3341 cases 13 and 14 ask. The short-term loudness is the mean over
     *  the last 30 steps (3 s) and changes once per step, past the 10 Hz
     *  that Tech 3341 asks of it. Until that much has been measured, both
     *  count what is missing as silence. ``maxMomentary()`` and
     *  ``maxShortTerm()`` keep the highest of each since the last
     *  ``reset()``, as Tech 3341 asks a meter to show.
     *
     *  **Integrated loudness** gates the 400 ms blocks that overlap by 75 %
     *  (one every step) as BS.1770 does: blocks at or below -70 LUFS are
     *  left out, then so are the blocks 10 LU or more below the loudness of
     *  the rest. It runs from the last ``reset()`` without an end. So that
     *  this needs no memory that grows, each block above -70 LUFS goes into
     *  a histogram of 0.01 LU bins from -70 to +30 LUFS, allocated in the
     *  constructor, that keeps both the count and the summed energy of its
     *  blocks; louder blocks share the top bin. The mean energies are then
     *  exact, and only the bin holding the relative gate is included or
     *  left out as a whole, by its mean loudness.
     *
     *  **Loudness range** (EBU Tech 3342) is the spread of the short-term
     *  loudness: from the moment 3 s have been measured, every step's
     *  short-term loudness at or above -70 LUFS goes into a second
     *  histogram of the same kind. Values more than 20 LU below the mean
     *  (in energy) of those are left out; the range is the 95th percentile
     *  of the rest minus the 10th, the percentiles picked by rank as Tech
     *  3342's reference code does (``round((n - 1) p)`` in sorted order) and
     *  read as the mean loudness of the bin the rank falls in. 0 while
     *  nothing passes the gates.
     *
     *  **Peaks.** The sample peak is the largest magnitude per channel
     *  since the last ``reset()``. The true peak oversamples each channel 4
     *  times with the 48-tap polyphase interpolator of BS.1770 Annex 2 (in
     *  floating point, so without its 12.04 dB headroom shift) and keeps the
     *  largest magnitude of the interpolated and the original samples, so it
     *  never reads below the sample peak. The same filter runs at every
     *  rate: above 48 kHz it oversamples more than the standard asks, at
     *  44.1 kHz it reaches 176.4 kHz rather than the 192 kHz the standard
     *  prefers.
     *
     *  Silence, and an integrated loudness with no block past the gates,
     *  read as ``SILENCE``, as does a peak of 0.
     *
     *  **Reports.** With an interval set, a block call returns how many
     *  interval periods ended in it; the values are then read with the
     *  getters, as of the end of the block. The interval is turned into
     *  samples with the live ``SAMPLERATE``.
     *
     *  **Sample rate.** Every block call checks ``SAMPLERATE``; when it has
     *  changed the meter derives new coefficients and step length, clears the
     *  filters' memory and starts over, as after ``reset()``.
     *
     *  A block holds at most ``STANDARD_BUFFERSIZE`` samples; a longer
     *  input is processed up to that length, and a channel shorter than the
     *  first reads as silence past its end. Call it from one thread at a
     *  time. The constructor allocates the histograms; nothing allocates,
     *  locks or blocks on the processing path.
     */
    class API loudnessMeter {
    public:
      /** @brief The most channels a meter measures. */
      static constexpr UInt MAX_CHANNELS = 64;

      /** @brief The value of silence in LUFS and in dBFS, and of an
       *         integrated loudness with no block past the gates. */
      static constexpr Flt SILENCE = -200.f;

      /** @brief The absolute gate of the integrated loudness in LUFS. */
      static constexpr Flt ABSOLUTE_GATE = -70.f;

      /** @brief The relative gate in LU below the absolutely gated
       *         loudness. */
      static constexpr Flt RELATIVE_GATE = -10.f;

      /** @brief One biquad section in double precision:
       *         ``y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2``. */
      struct section {
        Dbl b0 = 1.0;
        Dbl b1 = 0.0;
        Dbl b2 = 0.0;
        Dbl a1 = 0.0;
        Dbl a2 = 0.0;
      };

      /** @brief The first K-weighting stage, the high shelf, at
       *         @p sampleRate. At 48 kHz these are BS.1770's coefficients. */
      static section Shelf(Dbl sampleRate);

      /** @brief The second K-weighting stage, the high-pass, at
       *         @p sampleRate. At 48 kHz these are BS.1770's coefficients. */
      static section HighPass(Dbl sampleRate);

      /** @brief A meter for @p channels channels, clamped to
       *         1 .. ``MAX_CHANNELS``, every weight 1. */
      explicit loudnessMeter(UInt channels = 1);

      /** @brief The number of channels. */
      UInt channels() const;

      /** @brief The weight of channel @p channel (from 0). A value below 0
       *         or not finite, or a channel out of range, is ignored. */
      loudnessMeter& weight(UInt channel, Flt value);

      /** @brief The weight of channel @p channel; 0 out of range. */
      Flt weight(UInt channel) const;

      /** @brief One block of channel 0; any other channel reads as silence.
       *  @return The number of interval reports in the block. */
      UInt operator()(buffer& in);

      /** @brief One block of @p count channels, @p in[c] for channel c; a
       *         null pointer, or a channel past @p count, reads as silence,
       *         and channels past ``channels()`` are ignored.
       *  @return The number of interval reports in the block. */
      UInt operator()(buffer* const* in, UInt count);

      /** @brief The momentary loudness (400 ms) in LUFS. */
      Flt momentary() const;

      /** @brief The short-term loudness (3 s) in LUFS. */
      Flt shortTerm() const;

      /** @brief The highest momentary loudness since the last reset in
       *         LUFS. */
      Flt maxMomentary() const;

      /** @brief The highest short-term loudness since the last reset in
       *         LUFS. */
      Flt maxShortTerm() const;

      /** @brief The gated integrated loudness since the last reset in LUFS.
       *         Walks the histogram after a new block arrived, else returns
       *         the last value. */
      Flt integrated();

      /** @brief The sample peak in dBFS: of channel @p channel counted
       *         from 1, or with 0 (or a channel out of range) the highest of
       *         all channels. */
      Flt peak(UInt channel = 0) const;

      /** @brief The loudness range in LU since the last reset (EBU Tech
       *         3342). Walks the histogram after a new value arrived, else
       *         returns the last result. */
      Flt loudnessRange();

      /** @brief The true peak in dBTP: of channel @p channel counted from
       *         1, or with 0 (or a channel out of range) the highest of all
       *         channels. */
      Flt truePeak(UInt channel = 0) const;

      /** @brief The number of 400 ms blocks above the absolute gate since
       *         the last reset. */
      std::uint64_t gatedBlocks() const;

      /** @brief Sets the reporting interval in milliseconds; 0 (the
       *         default), a negative value or one that is not a number
       *         turns it off. A new value counts from here. */
      loudnessMeter& interval(Flt ms);

      /** @brief The reporting interval in milliseconds. */
      Flt interval() const;

      /** @brief Starts over: clears the windows, the integrated loudness,
       *         the loudness range, the maxima and the peaks. Keeps the channel weights,
       *         the interval and the memory of the K-weighting filters and
       *         the true-peak interpolator, since the signal goes on. */
      loudnessMeter& reset();

    private:
      static constexpr UInt WINDOW_STEPS = 30; // 3 s in 100 ms steps
      static constexpr UInt MOMENTARY_STEPS = 4; // 400 ms
      static constexpr UInt SUB_STEPS = 20; // 5 ms sub-steps per step
      static constexpr UInt MOMENTARY_SUBS = MOMENTARY_STEPS * SUB_STEPS;
      static constexpr int BINS = 10000; // 0.01 LU from -70 to +30 LUFS
      static constexpr UInt TAPS = 12; // per phase of the true-peak filter

      // Gated values by loudness: 0.01 LU bins from -70 LUFS, each with
      // the count and the summed energy of its values.
      struct histogram {
        histogram();
        void add(Dbl loudness, Dbl energy);
        void clear();
        // The bin of a loudness, clamped to the bins.
        static int bin(Dbl loudness);
        // The mean loudness of bin b, which must not be empty.
        Dbl mean(int b) const;
        // The first bin at or above the relative gate `gate` that counts,
        // and how many values from there on pass it; `strict` gates out a
        // value equal to the gate (BS.1770), else keeps it (Tech 3342).
        int gated(Dbl gate, bool strict, std::uint64_t& n, Dbl& energy) const;
        std::vector<std::uint64_t> count;
        std::vector<Dbl> energy;
        std::uint64_t total;
        Dbl totalEnergy;
      };

      struct state {
        Dbl x1 = 0.0;
        Dbl x2 = 0.0;
        Dbl y1 = 0.0;
        Dbl y2 = 0.0;
      };

      void configure();
      void endSub();
      void endStep();
      UInt subEnd() const; // where the running sub-step ends in its step
      Dbl filter(UInt channel, const Flt* in, UInt from, UInt to);

      UInt channelCount;
      UInt rate;
      UInt stepLength; // samples in one 100 ms step
      UInt stepFill; // samples in the running step
      Dbl stepEnergy; // weighted sum of squares in the running step
      UInt subIndex; // the running sub-step of the step
      Dbl subEnergy; // weighted sum of squares in the running sub-step
      section shelf;
      section highPass;
      std::array<Dbl, MAX_CHANNELS> weights;
      std::array<state, MAX_CHANNELS> shelfState;
      std::array<state, MAX_CHANNELS> highPassState;
      std::array<Flt, MAX_CHANNELS> peaks;
      std::array<Flt, MAX_CHANNELS> truePeaks;
      // The last TAPS input samples of each channel, twice over so the
      // filter reads them in one run.
      std::array<std::array<Flt, 2 * TAPS>, MAX_CHANNELS> history;
      std::array<UInt, MAX_CHANNELS> historyPos;
      std::array<Dbl, WINDOW_STEPS> steps; // energy sums of the last steps
      UInt stepPos;
      UInt stepsSeen; // up to WINDOW_STEPS
      std::array<Dbl, MOMENTARY_SUBS> subs; // energy sums of the last sub-steps
      UInt subPos;
      Flt momentaryLoudness;
      Flt shortTermLoudness;
      Flt maxMomentaryLoudness;
      Flt maxShortTermLoudness;

      histogram blocks; // 400 ms block loudness, for the integrated loudness
      histogram shortTerms; // short-term loudness, for the loudness range
      bool integratedDirty;
      Flt integratedLoudness;
      bool rangeDirty;
      Flt range;

      Flt intervalMs;
      Dbl elapsed;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_ANALYSIS_LOUDNESSMETER_HPP

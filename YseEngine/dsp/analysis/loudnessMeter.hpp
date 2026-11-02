/*
  ==============================================================================

    loudnessMeter.hpp
    Momentary, short-term and integrated loudness in LUFS after ITU-R
    BS.1770 and EBU R 128 (issue #1129).

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
     *         channels in LUFS, and their sample peak in dBFS.
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
     *  100 ms (rounded to whole samples). The momentary loudness is the mean
     *  over the last 4 steps (400 ms), the short-term over the last 30
     *  (3 s); both change once per step and, until that much has been
     *  measured, count the missing steps as silence.
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
     *  Silence, and an integrated loudness with no block past the gates,
     *  read as ``SILENCE``, as does a peak of 0. The sample peak is the
     *  largest magnitude per channel since the last ``reset()``.
     *
     *  **Reports.** With an interval set, a block call returns how many
     *  interval periods ended in it; the values are then read with the
     *  getters, as of the end of the block. The interval is turned into
     *  samples with the live ``SAMPLERATE``.
     *
     *  **Sample rate.** Every block call checks ``SAMPLERATE``; when it has
     *  changed the meter derives new coefficients and step length and starts
     *  over, as after ``reset()``.
     *
     *  A block holds at most ``STANDARD_BUFFERSIZE`` samples; a longer
     *  input is processed up to that length, and a channel shorter than the
     *  first reads as silence past its end. Call it from one thread at a
     *  time. The constructor allocates the histogram; nothing allocates,
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

      /** @brief The gated integrated loudness since the last reset in LUFS.
       *         Walks the histogram after a new block arrived, else returns
       *         the last value. */
      Flt integrated();

      /** @brief The sample peak in dBFS: of channel @p channel counted
       *         from 1, or with 0 (or a channel out of range) the highest of
       *         all channels. */
      Flt peak(UInt channel = 0) const;

      /** @brief The number of 400 ms blocks above the absolute gate since
       *         the last reset. */
      std::uint64_t gatedBlocks() const;

      /** @brief Sets the reporting interval in milliseconds; 0 (the
       *         default), a negative value or one that is not a number
       *         turns it off. A new value counts from here. */
      loudnessMeter& interval(Flt ms);

      /** @brief The reporting interval in milliseconds. */
      Flt interval() const;

      /** @brief Starts over: clears the filters, the windows, the
       *         integrated loudness and the peaks. Keeps the channel
       *         weights and the interval. */
      loudnessMeter& reset();

    private:
      static constexpr UInt WINDOW_STEPS = 30; // 3 s in 100 ms steps
      static constexpr UInt MOMENTARY_STEPS = 4; // 400 ms
      static constexpr int BINS = 10000; // 0.01 LU from -70 to +30 LUFS

      struct state {
        Dbl x1 = 0.0;
        Dbl x2 = 0.0;
        Dbl y1 = 0.0;
        Dbl y2 = 0.0;
      };

      void configure();
      void endStep();
      Dbl filter(UInt channel, const Flt* in, UInt from, UInt to);

      UInt channelCount;
      UInt rate;
      UInt stepLength; // samples in one 100 ms step
      UInt stepFill; // samples in the running step
      Dbl stepEnergy; // weighted sum of squares in the running step
      section shelf;
      section highPass;
      std::array<Dbl, MAX_CHANNELS> weights;
      std::array<state, MAX_CHANNELS> shelfState;
      std::array<state, MAX_CHANNELS> highPassState;
      std::array<Flt, MAX_CHANNELS> peaks;
      std::array<Dbl, WINDOW_STEPS> steps; // energy sums of the last steps
      UInt stepPos;
      UInt stepsSeen; // up to WINDOW_STEPS
      Flt momentaryLoudness;
      Flt shortTermLoudness;

      std::vector<std::uint64_t> binCount;
      std::vector<Dbl> binEnergy;
      std::uint64_t gatedCount;
      Dbl gatedEnergy;
      bool integratedDirty;
      Flt integratedLoudness;

      Flt intervalMs;
      Dbl elapsed;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_ANALYSIS_LOUDNESSMETER_HPP

/*
  ==============================================================================

    pitchTracker.hpp
    Fundamental frequency, peak amplitude and note onsets of a monophonic
    signal, after the YIN estimator (issue #1130).

  ==============================================================================
*/

#ifndef YSE_DSP_ANALYSIS_PITCHTRACKER_HPP
#define YSE_DSP_ANALYSIS_PITCHTRACKER_HPP

#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Estimates the fundamental frequency of a monophonic signal,
     *         with its peak amplitude and its note onsets.
     *
     *  **Analysis buffer.** Every input sample goes into a history of the
     *  last ``MAX_SIZE`` samples; the history starts as silence. Every
     *  ``period()`` samples the last ``size()`` of them are analysed: the
     *  analysis buffer. A sample that is not a finite number enters as 0.
     *
     *  **Estimator.** The analysis follows YIN (A. de Cheveigné and H.
     *  Kawahara, "YIN, a fundamental frequency estimator for speech and
     *  music", JASA 111(4), 2002). With ``W = size() / 2``, the difference
     *  function ``d(t) = sum (x[j] - x[j + t])^2`` over ``j < W`` is computed
     *  for every lag ``t`` up to ``W``, its correlation term by FFT, so one
     *  analysis costs ``O(size() log size())``. It is normalised by its
     *  cumulative mean, ``d'(t) = d(t) t / sum d(1..t)``, and the first lag
     *  from 2 on where ``d'`` falls below ``tolerance()`` is followed down
     *  to its local minimum. A parabola through that minimum and its two
     *  neighbours refines the lag between samples, and the frequency is
     *  ``SAMPLERATE / lag``. No lag below the tolerance means no pitch: the
     *  signal is not periodic enough (noise, a consonant, a chord).
     *
     *  **Range.** The search runs over every lag from 2 up to ``W - 1``
     *  samples and the lag for ``minFrequency()``, whichever is smaller; a
     *  pitch found outside ``minFrequency()`` to ``maxFrequency()`` is not
     *  reported. The lowest pitch a size can find is therefore
     *  ``SAMPLERATE / (size() / 2 - 1)``: 47 Hz for the default 2048 samples
     *  at 48 kHz, 378 Hz for 256. Searching from lag 2 whatever the
     *  maximum frequency means that a tone above the range is left out
     *  rather than reported an octave lower.
     *
     *  **Gate.** The peak amplitude is the largest sample magnitude in the
     *  analysis buffer. An analysis whose peak is below ``threshold()`` (or
     *  is 0) reports nothing: no pitch, no amplitude and no onset.
     *
     *  **Onsets.** An analysis above the gate counts as an onset when its
     *  peak rises to ``onsetAmplitude()`` or above from below it at the
     *  previous analysis (0 turns this off), or when its pitch lies
     *  ``onsetPitch()`` semitones or more from the pitch of the current note
     *  (0 turns this off). The current note's pitch is set at an onset, or,
     *  after an onset without a pitch or an analysis below the gate, by the
     *  next pitch found, which itself is no onset. For ``size()`` samples
     *  from then on the buffer still holds sound from before the note, so
     *  the note's pitch follows every estimate and no pitch onset is
     *  looked for; after that it holds. A pitch away from the note counts
     *  once two analyses in a row find it, within ``onsetPitch()`` of each
     *  other, so one stray estimate across a change is no onset; the onset
     *  comes at the second. An onset less than ``onsetPeriod()``
     *  samples after the previous one is dropped; a pitch that stays away
     *  from the note fires once that time has passed.
     *
     *  **Latency.** An analysis describes the last ``size()`` samples, so
     *  its centre lies ``latency()`` (``size() / 2``) samples before the
     *  input sample that completed it, and a change in the input is
     *  reported from the first analysis whose buffer is mostly the new
     *  sound: about ``size() / 2`` samples later, rounded up to the next
     *  analysis, every ``period()`` samples. While the buffer holds both
     *  silence and the start of a tone, or two notes, the estimate is
     *  unreliable and may land an octave off; it settles once the buffer
     *  holds the new sound alone. A block call ends with the values of the
     *  last analysis in it.
     *
     *  **Sample rate.** Sizes and periods are in samples; the frequencies
     *  are turned into lags with the live ``SAMPLERATE`` at every analysis,
     *  so nothing is derived from the rate once.
     *
     *  The constructor allocates everything for ``MAX_SIZE``, so every
     *  setter may run between blocks on the audio thread: nothing
     *  allocates, locks or blocks on the processing path. Call it from one
     *  thread at a time.
     */
    class API pitchTracker {
    public:
      /** @brief The smallest analysis buffer in samples. */
      static constexpr UInt MIN_SIZE = 128;
      /** @brief The largest analysis buffer in samples, and the length of
       *         the history. */
      static constexpr UInt MAX_SIZE = 4096;
      /** @brief The shortest time between analyses in samples. */
      static constexpr UInt MIN_PERIOD = 16;
      /** @brief The longest time between analyses in samples. */
      static constexpr UInt MAX_PERIOD = 4096;

      static constexpr UInt DEFAULT_SIZE = 2048;
      static constexpr UInt DEFAULT_PERIOD = 512;
      static constexpr Flt DEFAULT_MIN_FREQUENCY = 40.f;
      static constexpr Flt DEFAULT_MAX_FREQUENCY = 4000.f;
      static constexpr Flt DEFAULT_THRESHOLD = 0.01f;
      static constexpr Flt DEFAULT_TOLERANCE = 0.15f;
      static constexpr Flt DEFAULT_ONSET_AMPLITUDE = 0.1f;
      static constexpr Flt DEFAULT_ONSET_PITCH = 1.f;
      static constexpr UInt DEFAULT_ONSET_PERIOD = 2048;

      /** @brief A tracker with the defaults above. Allocates the history
       *         and the analysis memory for ``MAX_SIZE``. */
      pitchTracker();

      /** @brief The analysis buffer in samples, rounded down to a power of
       *         two and held to ``MIN_SIZE`` .. ``MAX_SIZE``. Takes effect at
       *         the next analysis, over the history already recorded. */
      pitchTracker& size(UInt samples);
      UInt size() const;

      /** @brief The time between analyses in samples, held to
       *         ``MIN_PERIOD`` .. ``MAX_PERIOD``. A shorter period than the
       *         time left to the next analysis brings that analysis
       *         forward. */
      pitchTracker& period(UInt samples);
      UInt period() const;

      /** @brief The lowest pitch reported, in Hz. A value at or below 0, or
       *         one that is not finite, is ignored. */
      pitchTracker& minFrequency(Flt hz);
      Flt minFrequency() const;

      /** @brief The highest pitch reported, in Hz. A value at or below 0, or
       *         one that is not finite, is ignored. When it is below
       *         ``minFrequency()`` the two swap roles. */
      pitchTracker& maxFrequency(Flt hz);
      Flt maxFrequency() const;

      /** @brief The gate: an analysis whose peak amplitude is below this
       *         reports nothing. A value below 0 is 0; one that is not
       *         finite is ignored. */
      pitchTracker& threshold(Flt amplitude);
      Flt threshold() const;

      /** @brief YIN's absolute threshold on the normalised difference,
       *         0 to 1: lower is stricter about what counts as periodic.
       *         Held to 0 .. 1; a value that is not finite is ignored. */
      pitchTracker& tolerance(Flt value);
      Flt tolerance() const;

      /** @brief The peak amplitude whose crossing from below is an onset;
       *         0 turns amplitude onsets off. A value below 0 is 0; one
       *         that is not finite is ignored. */
      pitchTracker& onsetAmplitude(Flt amplitude);
      Flt onsetAmplitude() const;

      /** @brief The distance in semitones from the current note's pitch
       *         that is an onset; 0 turns pitch onsets off. A value below 0
       *         is 0; one that is not finite is ignored. */
      pitchTracker& onsetPitch(Flt semitones);
      Flt onsetPitch() const;

      /** @brief The shortest time between two onsets in samples. */
      pitchTracker& onsetPeriod(UInt samples);
      UInt onsetPeriod() const;

      /** @brief One block. @return The number of analyses in it. */
      UInt operator()(buffer& in);

      /** @brief @p length samples from @p in; a null pointer is silence.
       *  @return The number of analyses in them. */
      UInt operator()(const Flt* in, UInt length);

      /** @brief How many analyses of the last block call passed the gate
       *         (each reports an amplitude). */
      UInt reports() const;

      /** @brief How many analyses of the last block call found a pitch. */
      UInt pitches() const;

      /** @brief How many onsets the last block call found. */
      UInt onsets() const;

      /** @brief The pitch of the last analysis that found one, in Hz; 0
       *         before any. */
      Flt frequency() const;

      /** @brief How periodic that pitch was: 1 minus the normalised
       *         difference at its lag, 0 to 1. */
      Flt clarity() const;

      /** @brief The peak amplitude of the last analysis that passed the
       *         gate; 0 before any. */
      Flt amplitude() const;

      /** @brief Whether the last analysis found a pitch. */
      bool voiced() const;

      /** @brief The pitch at the last onset in Hz, 0 when it had none. */
      Flt onsetFrequency() const;

      /** @brief The peak amplitude at the last onset. */
      Flt onsetPeak() const;

      /** @brief How far the centre of the analysis buffer lies behind the
       *         input sample that completes it: ``size() / 2`` samples. */
      UInt latency() const;

      /** @brief Starts over: silence in the history, the next analysis a
       *         whole period away, no values, no current note. Keeps the
       *         settings. */
      pitchTracker& reset();

    private:
      void analyse();
      bool estimate(Dbl& lag, Dbl& depth);
      // An in-place radix-2 FFT of `n` points (a power of two up to
      // MAX_SIZE) on the twiddle table; `inverse` without the 1/n scale.
      void transform(UInt n, bool inverse);

      // settings
      UInt frameSize;
      UInt hop;
      Flt lowest;
      Flt highest;
      Flt gate;
      Flt yinThreshold;
      Flt onsetAmp;
      Flt onsetSemitones;
      UInt onsetGap;

      // history
      std::vector<Flt> history; // MAX_SIZE, circular
      UInt writePos;
      UInt untilNext;
      UInt sinceOnset;
      UInt sinceNote; // samples since the current note's pitch was first set

      // analysis memory
      std::vector<Dbl> re;
      std::vector<Dbl> im;
      std::vector<Dbl> energy; // prefix sums of x^2, MAX_SIZE + 1
      std::vector<Dbl> difference; // d(t), MAX_SIZE / 2 + 1
      std::vector<Dbl> normalised; // d'(t), MAX_SIZE / 2 + 1
      std::vector<Dbl> cosTable; // MAX_SIZE / 2
      std::vector<Dbl> sinTable;

      // results
      UInt reportCount;
      UInt pitchCount;
      UInt onsetCount;
      Flt pitch;
      Flt pitchClarity;
      Flt peakAmplitude;
      bool isVoiced;
      Flt onsetPitchValue;
      Flt onsetPeakValue;
      Flt previousPeak;
      Flt notePitch; // 0: no current note
      Flt candidatePitch; // a pitch away from the note, waiting for a second
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_ANALYSIS_PITCHTRACKER_HPP

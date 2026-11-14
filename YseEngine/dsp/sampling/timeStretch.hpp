/*
  ==============================================================================

    timeStretch.hpp
    Offline time stretching and pitch shifting of a whole sample buffer: a
    phase vocoder with identity phase locking (issue #1152).

  ==============================================================================
*/

#ifndef YSE_DSP_SAMPLING_TIMESTRETCH_HPP
#define YSE_DSP_SAMPLING_TIMESTRETCH_HPP

#include <cstddef>
#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"

namespace YSE {
  namespace DSP {

    class sampleBuffer;

    /**
     *  @brief Changes the length and the pitch of recorded audio
     *         independently: a buffer made twice as long at the same pitch,
     *         or an octave higher at the same length. What the patcher's
     *         ``~stretch`` does (MSP's ``stretch~``).
     *
     *  This is an **offline** process. ``process`` reads a whole buffer,
     *  works on it and writes the result back in one change; it allocates
     *  and takes as long as the audio is long (a fraction of real time). Run
     *  it on a worker thread, never on the audio callback: ``~stretch``
     *  runs it on the engine's background pool. Readers of the target
     *  buffer (``~play``, ``~groove``) go on playing the old samples until
     *  the new ones are published, then the new ones, never a mix.
     *
     *  .. code-block:: cpp
     *
     *     auto drums = YSE::DSP::sampleBufferRegistry::acquire("drums");
     *     drums->read("loop.wav");
     *     YSE::DSP::timeStretch ts;
     *     ts.stretch(1.5f).cents(-200.f);   // longer and a tone lower
     *     ts.process(*drums, *drums);       // on a worker thread
     *
     *  ### The algorithm
     *
     *  A phase vocoder with identity phase locking (Laroche and Dolson,
     *  1999), built on ``DSP::stftAnalysis`` and ``DSP::stftSynthesis``.
     *  Chosen over WSOLA (overlap-add of waveform segments aligned by
     *  correlation) because WSOLA needs a single clear period to align on:
     *  it handles a solo voice well and smears chords, pads and noise,
     *  which a vocoder keeps. Phase locking is what removes the vocoder's
     *  usual "phasiness": each spectral peak's phase is advanced by its own
     *  measured frequency, and the bins around it keep their phase relative
     *  to the peak, as in the source, so a partial stays one coherent
     *  sinusoid.
     *
     *  - Output frames are ``hop`` samples apart; each reads the source at
     *    its own time divided by the vocoder ratio ``r = stretch * pitch``.
     *  - A bin's frequency is measured from two analyses of the source a
     *    fixed eighth of a frame apart, so it does not depend on how far
     *    apart the analysis frames are: a stretch of 16 reads frames a
     *    fraction of a sample apart and still knows every frequency.
     *  - The output frame's overlap grows for strong compressions, so the
     *    source is never read in frames further apart than half a frame.
     *  - The pitch is shifted by resampling the vocoder's output (4-point
     *    cubic interpolation, ``interpolate4::kernel``) by ``pitch``: the
     *    vocoder makes the audio ``r`` times as long, the resampling plays
     *    it ``pitch`` times as fast, which leaves it ``stretch`` times as
     *    long at ``pitch`` times the frequency. Going up, the bins that
     *    would end above Nyquist are cleared first, so nothing aliases.
     *  - In the modes that keep transients, the source is first searched
     *    for onsets (a frame whose spectrum rises sharply against the one a
     *    quarter frame before it, placed at the largest jump in level). An
     *    onset at ``t`` lands at ``r * t``, so the rhythm is stretched
     *    exactly, and the frame around it is copied at a speed of 1 rather
     *    than stretched: the frames that hold the onset are not repeated,
     *    and the bins that are rising take the source's phase, so a hit
     *    stays a hit instead of a smear and a tone under it does not jump.
     *    The rest of the source is stretched a little more (or less) to make
     *    up the length. An onset too close to the next to fit is stretched
     *    like the rest.
     *
     *  At ``stretch`` and ``pitch`` both exactly 1 the samples are copied
     *  unchanged.
     *
     *  **Quality** sets the frame and the overlap, at a 48 kHz buffer:
     *  ``BASIC`` 1024 samples at 4, ``GOOD`` 2048 at 4, ``BETTER`` (the
     *  default) 2048 at 8, ``BEST`` 4096 at 8. Longer frames separate close
     *  partials better and blur onsets more. The frame follows the buffer's
     *  own rate (the power of two nearest to the same duration), so a 96 kHz
     *  buffer sounds the same.
     *
     *  **Mode** tunes it for the material:
     *
     *  - ``MODE_BASIC`` (default): the quality's frame, onsets not treated
     *    apart. For anything.
     *  - ``MONOPHONIC``: half the frame, which follows a single voice's
     *    pitch changes more closely.
     *  - ``RHYTHMIC``: half the frame, onsets kept, at a lower threshold.
     *    For drums and percussion.
     *  - ``GENERAL``: the quality's frame, onsets kept. For mixed material.
     *  - ``POLYPHONIC``: twice the frame, which separates the partials of a
     *    chord, onsets kept. For piano or guitar.
     *  - ``PADS``: twice the frame, onsets not treated apart. For sustained,
     *    slowly changing sound.
     *  - ``EXTREME``: four times the frame, for stretches far beyond 2 as an
     *    effect.
     *  - ``EFFICIENT``: half the frame at an overlap of 4: the least work.
     *
     *  **Pitch correction** (``pitchCorrection``): after the stretch the
     *  result goes through a ``DSP::retune`` (phase vocoder quality, its
     *  latency taken out) that pulls every detected pitch onto the nearest
     *  equal-tempered semitone of an A at ``standardPitch``. As in
     *  ``retune``, it suits a monophonic source. The tracker reads the
     *  engine rate, so the A is scaled by the engine rate over the
     *  buffer's: a buffer at another rate is corrected to the same notes.
     *  ``slurTime`` is the corrector's ``slur``, in ms of the buffer's own
     *  time: how long the correction takes to reach full strength
     *  (0, the default: at once).
     *
     *  **Markers** (``markers``, issue #1285): instead of one factor, a
     *  time map. Each marker puts a frame of the source on a frame of the
     *  result, and the source between two markers is stretched evenly to fit
     *  the result between them, so a loose take can be pulled onto a grid:
     *  a marker at 500 ms to 1000 ms makes the first half second last a
     *  second, and what was at 500 ms is heard at 1000 ms. The map starts at
     *  frame 0 on frame 0; after the last marker the rest of the source is
     *  stretched by ``stretch()``, so the result is as long as where the
     *  source's end lands. A marker past the source's end shapes the last
     *  segment and is not reached. Each marker can carry its own pitch
     *  factor, which holds until the next marker (before the first, and for
     *  a marker without one, ``pitch()``): the vocoder stretches each
     *  segment by its stretch times its pitch, and the resampling follows
     *  the pitch segment by segment. Onsets are kept within a segment, not
     *  across a marker. ``~stretch`` reads them from a ``.dict``
     *  (``dictionary <name>``).
     *
     *  Formant preservation is not done (a non-goal): shifting the pitch
     *  shifts the formants with it.
     *
     *  Settings are plain values (the markers a list of them); a
     *  ``timeStretch`` is cheap to make and to copy. ``process`` is one thread's at a time per object; it reads only
     *  its own settings.
     */
    class API timeStretch {
    public:
      /** @brief How fine the analysis is (see the class notes). */
      enum QUALITY {
        BASIC, ///< 1024-sample frames, overlap 4
        GOOD, ///< 2048-sample frames, overlap 4
        BETTER, ///< 2048-sample frames, overlap 8 (default)
        BEST, ///< 4096-sample frames, overlap 8
      };

      /** @brief What the material is (see the class notes). */
      enum MODE {
        MODE_BASIC, ///< the quality's frame, no transient handling (default)
        MONOPHONIC, ///< half frames, for one voice
        RHYTHMIC, ///< half frames, transients kept, sensitive
        GENERAL, ///< the quality's frame, transients kept
        POLYPHONIC, ///< double frames, transients kept
        PADS, ///< double frames, no transient handling
        EXTREME, ///< four times the frame, for very long stretches
        EFFICIENT, ///< half frames at an overlap of 4
      };

      static constexpr Flt MIN_STRETCH = 0.0625f; ///< a sixteenth of the length
      static constexpr Flt MAX_STRETCH = 16.f; ///< sixteen times the length
      static constexpr Flt MIN_PITCH = 0.25f; ///< two octaves down
      static constexpr Flt MAX_PITCH = 4.f; ///< two octaves up
      static constexpr Flt MIN_STANDARD_PITCH = 400.f;
      static constexpr Flt MAX_STANDARD_PITCH = 500.f;

      /**
       *  @brief One point of the time map (see ``markers``): the frame
       *         ``source`` of the source lands on the frame ``destination``
       *         of the result, and from there to the next marker the pitch
       *         is ``pitch`` times the original (0: ``pitch()``). Frames at
       *         the buffer's own rate, fractions allowed.
       */
      struct marker {
        double source = 0.0;
        double destination = 0.0;
        Flt pitch = 0.f;
      };

      /** @brief Why ``markers`` refused a list (``describe`` spells it). */
      enum MARKER_ERROR {
        MARKERS_OK, ///< accepted
        MARKER_NOT_FINITE, ///< a time is not a finite number
        MARKER_NEGATIVE, ///< a time is below 0
        MARKER_SOURCE_ORDER, ///< a source time is not after the one before
        MARKER_DESTINATION_ORDER, ///< a destination time is not after the one before
        MARKER_STRETCH, ///< a segment's stretch is outside MIN_STRETCH .. MAX_STRETCH
        MARKER_PITCH, ///< a pitch is below 0 or not finite
      };

      /**
       *  @brief What ``process`` calls now and then, on its own thread,
       *         with the share of the work done (0 to 1) and the pointer it
       *         was given. Return false to stop: ``process`` then returns
       *         false and leaves the target as it was.
       */
      typedef bool (*progressFunction)(Flt done, void* user);

      timeStretch() = default;

      /** @brief The length factor: 2 doubles the length, 0.5 halves it.
       *         Held to ``MIN_STRETCH`` .. ``MAX_STRETCH``; not finite or
       *         not above 0: ignored. MSP's ``stretch``. */
      timeStretch& stretch(Flt factor);
      Flt stretch() const {
        return lengthFactor;
      }

      /** @brief The pitch factor: 2 an octave up, 0.5 an octave down. Held
       *         to ``MIN_PITCH`` .. ``MAX_PITCH``; not finite or not above
       *         0: ignored. MSP's ``pitchshift``. */
      timeStretch& pitch(Flt factor);
      Flt pitch() const {
        return pitchFactor;
      }

      /** @brief The pitch in cents (100 a semitone up): ``pitch(2^(c /
       *         1200))``. MSP's ``pitchshiftcent``. */
      timeStretch& cents(Flt value);

      /** @brief The analysis quality. MSP's ``quality``. */
      timeStretch& quality(QUALITY value);
      QUALITY quality() const {
        return frameQuality;
      }

      /** @brief The kind of material. MSP's ``mode``. */
      timeStretch& mode(MODE value);
      MODE mode() const {
        return material;
      }

      /** @brief Pull the result onto the nearest semitone (off by default).
       *         MSP's ``pitchcorrection``. */
      timeStretch& pitchCorrection(bool on);
      bool pitchCorrection() const {
        return correcting;
      }

      /** @brief The A the semitones of the correction are tuned from, in Hz.
       *         Held to 400 .. 500; not finite: ignored. 440 by default.
       *         MSP's ``basictuning``. */
      timeStretch& standardPitch(Flt hz);
      Flt standardPitch() const {
        return standardA;
      }

      /** @brief The time in ms the pitch correction takes to reach full
       *         strength: ``DSP::retune::slur``, reckoned at the buffer's
       *         rate. Below 0: 0; not finite: ignored. 0 by default. MSP's
       *         ``slurtime``. */
      timeStretch& slurTime(Flt ms);
      Flt slurTime() const {
        return slur;
      }

      /**
       *  @brief A time map instead of the one ``stretch()`` (class notes):
       *         @p list in source order, the start (frame 0 on frame 0)
       *         implied.
       *
       *  Both times rise strictly from marker to marker; a first marker at
       *  source 0 must land on destination 0 (it only gives the first
       *  segment a pitch). Each segment's stretch (destination span over
       *  source span) is within ``MIN_STRETCH`` .. ``MAX_STRETCH``. A pitch
       *  is 0 (``pitch()``) or above, held to ``MIN_PITCH`` .. ``MAX_PITCH``.
       *  A list that breaks one of these is refused whole: the markers stay
       *  as they were, the error is returned and @p at (may be null) gets
       *  the index of the marker at fault. An empty list clears them.
       *  Allocates; not on the audio thread.
       */
      MARKER_ERROR markers(const std::vector<marker>& list, std::size_t* at = nullptr);
      const std::vector<marker>& markers() const {
        return points;
      }

      /** @brief Back to the one ``stretch()`` factor. */
      timeStretch& clearMarkers();

      /** @brief @p error in words, for a log line. */
      static const char* describe(MARKER_ERROR error);

      /** @brief How many frames the result of @p frames source frames has:
       *         ``round(frames * stretch())`` (with markers: where frame
       *         @p frames lands on the map), at least 1 for a source that
       *         has any. */
      UInt outputFrames(UInt frames) const;

      /** @brief The frame size and overlap @p rate (the buffer's, in Hz)
       *         is analysed with in the current quality and mode, before
       *         a strong compression raises the overlap. */
      UInt frameSize(Flt rate) const;
      UInt overlap() const;

      /**
       *  @brief One channel: @p frames samples of @p input at @p rate Hz
       *         into @p output, which is resized to ``outputFrames(frames)``.
       *
       *  Allocates; a worker thread's call. Returns false, with @p output
       *  left as it was, when @p frames is 0, @p input is null, or
       *  @p progress asked to stop. @p progress (may be null) gets the share
       *  of this channel done.
       */
      bool render(const Flt* input, UInt frames, Flt rate, std::vector<Flt>& output,
                  progressFunction progress = nullptr, void* user = nullptr) const;

      /**
       *  @brief Every channel of @p source, stretched and shifted, into
       *         @p target in one change (``sampleBuffer::setContents``):
       *         ``outputFrames`` long, the source's channel count, the
       *         target's rate and file details kept. @p source and
       *         @p target may be the same buffer.
       *
       *  Allocates and locks; never on the audio thread. Returns false,
       *  with @p target unchanged, for an empty source or when
       *  @p progress asked to stop. @p progress (may be null) gets the
       *  share of the whole job done, 1 last, after the result is
       *  published.
       */
      bool process(const sampleBuffer& source, sampleBuffer& target,
                   progressFunction progress = nullptr, void* user = nullptr) const;

    private:
      // The share of `render`'s work one channel's stages take, for the
      // progress of a multichannel job.
      struct progressSpan {
        progressFunction function = nullptr;
        void* user = nullptr;
        Flt from = 0.f;
        Flt width = 1.f;
        bool report(Flt done) const;
      };

      struct plan;
      plan Plan(UInt frames) const;
      bool Vocode(const Flt* input, UInt frames, Flt rate, const plan& layout,
                  std::vector<Flt>& output, const progressSpan& span) const;
      bool Correct(std::vector<Flt>& samples, Flt rate, const progressSpan& span) const;

      Flt lengthFactor = 1.f;
      Flt pitchFactor = 1.f;
      QUALITY frameQuality = BETTER;
      MODE material = MODE_BASIC;
      bool correcting = false;
      Flt standardA = 440.f;
      Flt slur = 0.f; // ms
      std::vector<marker> points; // the time map; empty: stretch()
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SAMPLING_TIMESTRETCH_HPP

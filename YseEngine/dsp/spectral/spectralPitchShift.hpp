/*
  ==============================================================================

    spectralPitchShift.hpp
    A frequency-domain pitch shifter: the peaks of a short-time spectrum
    moved to their transposed bins, each with the bins around it, and their
    phases turned so the moved partials stay continuous from frame to frame
    (issue #1164).

  ==============================================================================
*/

#ifndef YSE_DSP_SPECTRAL_SPECTRALPITCHSHIFT_HPP
#define YSE_DSP_SPECTRAL_SPECTRALPITCHSHIFT_HPP

#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "stft.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Transposes a short-time spectrum by moving its peaks: the
     *         frequency-domain pitch shifter behind ``~gizmo``.
     *
     *  Ported from MSP's ``gizmo~``, after Laroche and Dolson's peak
     *  shifting ("New phase-vocoder techniques for pitch-shifting,
     *  harmonizing and other exotic effects", 1999). It takes *whole*
     *  frames in cartesian form (``real`` / ``imaginary``, as a
     *  ``DSP::stft`` callback gets them, or the frame a frame-wide
     *  ``~gizmo`` gathers from a ``~pfft``'s passes) and, frame by frame:
     *
     *  1. **Peaks.** A bin whose magnitude is above 0, above the bin below
     *     and at least the bin above is a peak. Every bin belongs to one
     *     peak's *region*: the boundary between two peaks is the quietest
     *     bin between them (the first of equals), which starts the upper
     *     region; the lowest region starts at bin 0, the highest runs to the
     *     top of the frame.
     *  2. **Frequency.** A peak at bin ``p`` has the true frequency
     *     ``w = p + dev`` bins, ``dev`` the phase vocoder's deviation: the
     *     phase change of bin ``p`` since the frame before, less the
     *     ``2 pi p hop / size`` a bin-centred partial would make, wrapped to
     *     +-pi and scaled by ``size / (2 pi hop)``, held to +-1 bin. On the
     *     first frame, after a gap in ``index`` or a change of size, hop or
     *     bins, ``dev`` is 0.
     *  3. **Target.** ``w' = ratio() * w + frequencyShift() * size /
     *     SAMPLERATE`` bins (the shift in Hz, at the live sample rate).
     *  4. **Move.** The whole region moves by ``d = round(w') - p`` bins, so
     *     the peak lands on the bin nearest its transposed frequency.
     *     Regions that land on the same bins add up; bins no region lands on
     *     are 0.
     *  5. **Phase.** Every bin of the region is turned by the peak's angle
     *     ``theta``: that of the nearest peak of the frame before (at most 2
     *     bins away; 0 when there is none) plus ``2 pi (w' - w) hop / size``.
     *     So the moved partial advances its phase from one frame to the next
     *     as a partial at ``w'`` does, and the bins of a region keep their
     *     phases relative to the peak (phase locking).
     *
     *  With ``ratio()`` 1 and no frequency shift, nothing moves and the
     *  angles stay where they are: from the start, the frame comes out as it
     *  went in, bit for bit.
     *
     *  **Out of range.** A bin moved below 0 or above Nyquist (``size / 2``)
     *  is, with ``clip()`` off (the default, MSP's ``clip 0``), folded back
     *  as its frequency would alias in the time domain: bin ``j`` is read
     *  modulo ``size``, and above Nyquist as the conjugate at ``size - j``.
     *  With ``clip()`` on it is dropped.
     *
     *  **The frame.** ``firstBin`` must be 0: the frame is whole (a run that
     *  starts later is passed through unchanged and starts the frames
     *  afresh). The bins processed are those of the lower half the frame
     *  holds, DC to Nyquist: ``count`` bins of a half-spectrum frame (a
     *  ``~pfft``'s ``size / 2``, which has no Nyquist bin, or a
     *  ``DSP::stft``'s ``size / 2 + 1``), ``size / 2 + 1`` of a full one,
     *  whose upper half is then written as the mirror (complex conjugate) of
     *  the lower and not read. A moved bin that lands on a bin the frame
     *  does not hold is dropped. Bins at or past ``capacity()`` are not
     *  read and come out 0.
     *
     *  An input that is not a finite number counts as 0. Input and output
     *  may be the same arrays. A shift needs an overlap of 4 or more to
     *  sound clean (MSP says the same): the moved partials are a fraction of
     *  a bin off inside each frame, and a larger hop makes the frames
     *  disagree more where they overlap.
     *
     *  All per-bin state (phases, peaks, angles, the frame being built) is
     *  ``capacity()`` long and allocated in the constructor: processing and
     *  the setters allocate nothing and take no lock. The frequency shift is
     *  turned into bins at the sample rate of each frame. One thread at a
     *  time.
     */
    class API spectralPitchShift {
    public:
      /** @brief Frames of up to 4096 bins: a half spectrum of an 8192-point
       *         FFT. */
      static constexpr UInt DEFAULT_CAPACITY = 4096;
      /** @brief A full spectrum of ``~pfft``'s largest FFT. */
      static constexpr UInt MAXIMUM_CAPACITY = 1u << 16;
      /** @brief The highest ratio: four octaves up. */
      static constexpr Flt MAXIMUM_RATIO = 16.f;

      /** @brief Transposes by @p ratio (see ``ratio()``), keeping state for
       *         @p capacity bins (clamped to 1 to ``MAXIMUM_CAPACITY``).
       *         Allocates. */
      explicit spectralPitchShift(Flt ratio = 1.f, UInt capacity = DEFAULT_CAPACITY);

      /** @brief How many bins the object keeps. */
      UInt capacity() const {
        return static_cast<UInt>(phase.size());
      }

      /** @brief The pitch ratio, from the next frame: 2 an octave up, 0.5 an
       *         octave down, 1 unchanged. Held to 0 to ``MAXIMUM_RATIO``; a
       *         value that is not a finite number counts as 1. MSP's
       *         default-pitch-scalar. */
      spectralPitchShift& ratio(Flt value);
      Flt ratio() const {
        return scale;
      }

      /** @brief A frequency shift in Hz added to every transposed peak,
       *         either sign, from the next frame. Not a finite number counts
       *         as 0. MSP's ``freqshift``. */
      spectralPitchShift& frequencyShift(Flt hz);
      Flt frequencyShift() const {
        return shiftHz;
      }

      /** @brief Drops bins moved out of 0 to Nyquist (on) instead of folding
       *         them back (off, the default). MSP's ``clip``. */
      spectralPitchShift& clip(bool on);
      bool clip() const {
        return clipping;
      }

      /** @brief Forgets the frames before: the next one is a first frame. */
      spectralPitchShift& reset();

      /** @brief One whole frame, in place on ``frame.real`` /
       *         ``frame.imaginary`` (a ``DSP::stft`` callback). */
      void operator()(stftFrame& frame);

      /** @brief One whole frame from @p inReal / @p inImaginary into
       *         @p outReal / @p outImaginary, ``frame.count`` values each
       *         (``frame.real`` and ``frame.imaginary`` are not used). Either
       *         input may be null (all 0); the outputs may be the inputs. */
      void operator()(const stftFrame& frame, const Flt* inReal, const Flt* inImaginary,
                      Flt* outReal, Flt* outImaginary);

    private:
      Flt scale;
      Flt shiftHz = 0.f;
      bool clipping = false;

      std::vector<Flt> magnitude;
      std::vector<double> phase; // this frame's
      std::vector<double> lastPhase; // the frame before's
      std::vector<UInt> peaks; // this frame's peak bins, ascending
      std::vector<UInt> lastPeaks; // the frame before's
      std::vector<double> angle; // per peak of this frame
      std::vector<double> lastAngle; // per peak of the frame before
      std::vector<Flt> real; // the frame being built
      std::vector<Flt> imaginary;
      UInt peakCount = 0;
      UInt lastPeakCount = 0;

      bool started = false;
      U64 lastIndex = 0;
      UInt lastSize = 0;
      UInt lastHop = 0;
      UInt lastTop = 0;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SPECTRAL_SPECTRALPITCHSHIFT_HPP

/*
  ==============================================================================

    spectralFreqShift.hpp
    A frequency-domain frequency shifter: every bin of a short-time spectrum
    moved by the same number of Hz, and the frame turned so the moved
    partials stay continuous from frame to frame (issue #1165).

  ==============================================================================
*/

#ifndef YSE_DSP_SPECTRAL_SPECTRALFREQSHIFT_HPP
#define YSE_DSP_SPECTRAL_SPECTRALFREQSHIFT_HPP

#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "stft.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Shifts every frequency of a short-time spectrum by the same
     *         number of Hz: the frequency-domain frequency shifter behind
     *         ``~fbinshift``.
     *
     *  Ported from MSP's ``fbinshift~``. Unlike a pitch shift
     *  (``spectralPitchShift``) it adds to frequencies rather than
     *  multiplying them, so the partials of a harmonic sound stop being
     *  harmonic. It takes *whole* frames in cartesian form (``real`` /
     *  ``imaginary``, as a ``DSP::stft`` callback gets them, or the frame a
     *  frame-wide ``~fbinshift`` gathers from a ``~pfft``'s passes) and,
     *  frame by frame:
     *
     *  1. **Shift.** ``s = frequencyShift() * size / SAMPLERATE`` bins (the
     *     shift in Hz, at the live sample rate).
     *  2. **Move.** Every bin moves by ``d = round(s)`` bins (half a bin
     *     rounds away from 0). Bins that land on the same bin add up; bins
     *     nothing lands on are 0.
     *  3. **Turn.** The whole frame is turned by one angle ``theta``: 0 on
     *     the first frame, then advanced by ``2 pi s hop / size`` (that is
     *     ``2 pi frequencyShift() hop / SAMPLERATE``) for every frame since
     *     the one before. So a partial at ``w`` bins comes out advancing its
     *     phase from one frame to the next as a partial at ``w + s`` does:
     *     a fractional shift is heard as such, though its energy sits on the
     *     whole bin ``d`` away. With a whole-bin shift this is an exact
     *     time-domain frequency shift of each frame's content. A new shift
     *     changes the advance, not the angle reached, so the output stays
     *     continuous.
     *
     *  With no shift nothing moves and the angle stays 0: the frame comes
     *  out as it went in, bit for bit.
     *
     *  **Out of range.** A bin moved below 0 or above Nyquist (``size / 2``)
     *  is, with ``clip()`` off (the default), folded back as its frequency
     *  would alias in the time domain: bin ``j`` is read modulo ``size``,
     *  and above Nyquist as the conjugate at ``size - j``. With ``clip()`` on
     *  it is dropped. (MSP's ``fbinshift~`` does not say; the fold is what
     *  ``gizmo~`` does by default, and ``clip`` is ``gizmo~``'s.)
     *
     *  **The frame.** ``firstBin`` must be 0: the frame is whole (a run that
     *  starts later is passed through unchanged and starts the frames
     *  afresh). The bins processed are those of the lower half the frame
     *  holds, DC to Nyquist: ``count`` bins of a half-spectrum frame (a
     *  ``~pfft``'s ``size / 2``, which has no Nyquist bin, or a
     *  ``DSP::stft``'s ``size / 2 + 1``), ``size / 2 + 1`` of a full one,
     *  whose upper half is then written as the mirror (complex conjugate) of
     *  the lower and not read. A moved bin that lands on a bin the frame
     *  does not hold is dropped. Bins at or past ``capacity()`` are not read
     *  and come out 0.
     *
     *  The angle starts again at 0 after ``reset()``, a change of hop, or a
     *  frame whose ``index`` is not past the one before; a gap in ``index``
     *  advances it by every frame missed. An input that is not a finite
     *  number counts as 0. Input and output may be the same arrays. Use an
     *  overlap of 4 or more for a fractional shift.
     *
     *  The frame being built is ``capacity()`` long and allocated in the
     *  constructor: processing and the setters allocate nothing and take no
     *  lock. The shift is turned into bins at the sample rate of each frame.
     *  One thread at a time.
     */
    class API spectralFreqShift {
    public:
      /** @brief Frames of up to 4096 bins: a half spectrum of an 8192-point
       *         FFT. */
      static constexpr UInt DEFAULT_CAPACITY = 4096;
      /** @brief A full spectrum of ``~pfft``'s largest FFT. */
      static constexpr UInt MAXIMUM_CAPACITY = 1u << 16;

      /** @brief Shifts by @p hz (see ``frequencyShift()``), for frames of up
       *         to @p capacity bins (clamped to 1 to ``MAXIMUM_CAPACITY``).
       *         Allocates. */
      explicit spectralFreqShift(Flt hz = 0.f, UInt capacity = DEFAULT_CAPACITY);

      /** @brief How many bins the object keeps. */
      UInt capacity() const {
        return static_cast<UInt>(real.size());
      }

      /** @brief The frequency shift in Hz, either sign, from the next frame:
       *         added to every frequency. Not a finite number counts as 0.
       *         MSP's frequency-shift argument. */
      spectralFreqShift& frequencyShift(Flt hz);
      Flt frequencyShift() const {
        return shiftHz;
      }

      /** @brief Drops bins moved out of 0 to Nyquist (on) instead of folding
       *         them back (off, the default). */
      spectralFreqShift& clip(bool on);
      bool clip() const {
        return clipping;
      }

      /** @brief Forgets the frames before: the next one is a first frame,
       *         its angle 0. */
      spectralFreqShift& reset();

      /** @brief The angle the last frame was turned by, in radians (-pi to
       *         pi). */
      double angle() const {
        return theta;
      }

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
      Flt shiftHz;
      bool clipping = false;

      std::vector<Flt> real; // the frame being built
      std::vector<Flt> imaginary;

      double theta = 0.0;
      bool started = false;
      U64 lastIndex = 0;
      UInt lastHop = 0;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SPECTRAL_SPECTRALFREQSHIFT_HPP

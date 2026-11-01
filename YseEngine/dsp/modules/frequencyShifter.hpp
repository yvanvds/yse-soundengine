/*
  ==============================================================================

    frequencyShifter.hpp
    A time-domain single-sideband frequency shifter (issue #1110).

  ==============================================================================
*/

#ifndef YSE_DSP_MODULES_FREQUENCYSHIFTER_HPP
#define YSE_DSP_MODULES_FREQUENCYSHIFTER_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"
#include "hilbert.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Single-sideband frequency shifter: moves every frequency of a
     *         signal up or down by the same number of Hz.
     *
     *  Behind ``~freqshift``, ported from Max's ``freqshift~`` (issue #1110).
     *  Unlike a pitch shift, the shift is a fixed distance in Hz, so
     *  harmonics no longer sit at whole multiples of the fundamental: small
     *  shifts give a slow beating or barber-pole phasing, larger ones
     *  inharmonic, metallic sounds. Also known as a single-sideband ring
     *  modulator: a ring modulator gives both sidebands, this keeps one.
     *
     *  The input goes through a ``DSP::hilbert``, which gives its real part
     *  ``re`` and its imaginary part ``im``, 90 degrees behind. A quadrature
     *  oscillator at the shift frequency runs a phase ``p`` (in cycles, kept
     *  in double precision) and every sample is
     *
     *  ``up   = re cos(2 pi p) - im sin(2 pi p)``
     *
     *  ``down = re cos(2 pi p) + im sin(2 pi p)``
     *
     *  after which ``p`` advances by ``shift / SAMPLERATE``. ``up()`` is the
     *  input shifted up by the shift frequency, ``down()`` shifted down by
     *  it; a negative shift swaps the two. A frequency shifted below 0 Hz
     *  comes back mirrored above it. The other sideband is suppressed by the
     *  quality of the 90 degree split: by at least 40 dB within the band
     *  where ``hilbert`` holds 90 degrees to 1 degree (``0.0018`` to
     *  ``0.308`` times the sample rate), less below and above it.
     *
     *  The shift defaults to 0 Hz, which passes ``re`` (the input through
     *  allpasses, at the same level) to both outputs. A shift that is not a
     *  finite number is ignored. The two-buffer call reads the shift per
     *  sample, and ``shift()`` keeps the last finite value read; a sample
     *  that is not finite uses that value instead. The phase increment is
     *  derived from ``SAMPLERATE`` on every call, so the shift stays right
     *  across a change of sample rate. ``resetPhase`` sets the oscillator
     *  phase; at phase 0 the first output sample of both outputs is ``re``.
     *  ``clear`` sets the filter state to 0 and leaves the phase.
     *
     *  An input sample that is not a finite number is skipped by the filter,
     *  whose outputs hold, while the oscillator runs on.
     *
     *  Each block call fills both outputs, returns ``up()``, and processes at
     *  most ``STANDARD_BUFFERSIZE`` samples; with two buffers the block is as
     *  long as the shorter one. Call it from one thread at a time.
     *  Processing allocates nothing: the outputs are sized to
     *  ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API frequencyShifter {
    public:
      /** @brief The shift until one is set, in Hz. */
      static constexpr Flt DEFAULT_SHIFT = 0.f;

      /** @brief Both outputs for one sample. */
      struct outputs {
        Flt up = 0.f;
        Flt down = 0.f;
      };

      frequencyShifter();

      /** @brief Sets the shift in Hz; negative swaps the outputs. Not a
       *         finite number: ignored. */
      frequencyShifter& shift(Flt hz);

      /** @brief The shift in Hz: the last one set or read. */
      Flt shift() const {
        return shiftHz;
      }

      /** @brief Sets the filter state to 0; the phase runs on. */
      frequencyShifter& clear();

      /** @brief Sets the oscillator phase, in cycles, wrapped into 0..1. */
      frequencyShifter& resetPhase(Flt phase = 0.f);

      /** @brief The oscillator phase the next sample uses, in 0..1. */
      Flt phase() const;

      /** @brief One sample at ``shift()``. */
      outputs process(Flt in);

      /** @brief One block at ``shift()``; fills both outputs and returns
       *         ``up()``. */
      buffer& operator()(buffer& in);

      /** @brief One block with the shift read per sample from @p shift;
       *         fills both outputs and returns ``up()``. */
      buffer& operator()(buffer& in, buffer& shift);

      /** @brief The input shifted up by the shift, from the last block
       *         call. */
      buffer& up() {
        return upOut;
      }

      /** @brief The input shifted down by the shift, from the last block
       *         call. */
      buffer& down() {
        return downOut;
      }

    private:
      outputs Step(Flt in, Dbl increment);

      hilbert quadrature;
      buffer upOut;
      buffer downOut;
      Dbl current;
      Flt shiftHz;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_MODULES_FREQUENCYSHIFTER_HPP

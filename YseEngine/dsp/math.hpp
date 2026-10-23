/*
  ==============================================================================

    math.h
    Created: 31 Jan 2014 2:54:26pm
    Author:  yvan

  ==============================================================================
*/

#ifndef MATH_H_INCLUDED
#define MATH_H_INCLUDED

#include "../headers/types.hpp"
#include "buffer.hpp"

namespace YSE {
  namespace DSP {

    /** @brief Convert a MIDI note number (0-127) to a frequency in Hz. Scalar form.
     *
     *  Notes above 1499 are clamped to 1499; notes below -1500 (or NaN) give 0 Hz,
     *  so the result is always finite.
     */
    Flt API MidiToFreq(Flt note);

    /** @brief Convert a frequency in Hz to a MIDI note number. Scalar form.
     *
     *  A frequency of 0 or below (or NaN) gives -1500, so the result is always finite.
     */
    Flt API FreqToMidi(Flt freq);

    /**
     *  @brief Hard-clipper.
     *
     *  Limits sample values to ``[low, high]``. Use to tame stray peaks or as
     *  a deliberate distortion stage.
     */
    class API clip {
    public:
      /** @brief Set both bounds in one call. */
      clip& set(Flt low, Flt high);

      /** @brief Set the lower clipping threshold. */
      clip& setLow(Flt low);

      /** @brief Set the upper clipping threshold. */
      clip& setHigh(Flt high);
      clip() : low(-1.0f), high(1.0f) {}

      /** @brief Process ``in`` in place. */
      YSE::DSP::buffer& operator()(YSE::DSP::buffer& in);

    private:
      aFlt low;
      aFlt high;
      YSE::DSP::buffer buffer;
    };

    /**
     *  @brief Fast reciprocal square root (~8 mantissa bits of precision).
     *
     *  Significantly cheaper than the standard library version. Use in DSP
     *  paths where speed matters more than the last few bits of accuracy.
     */
    class API rSqrt {
    public:
      /** @brief Process ``in`` in place. */
      YSE::DSP::buffer& operator()(YSE::DSP::buffer& in);
      rSqrt();

    private:
      YSE::DSP::buffer buffer;
    };

    /**
     *  @brief Fast square root (~8 mantissa bits of precision).
     *
     *  Same trade-off as ``rSqrt`` — cheaper than the standard library, less
     *  precise. Use in DSP paths where speed matters. Negative, NaN and
     *  infinite input give 0, so no NaN or infinity leaves this class.
     */
    class API sqrt {
    public:
      /** @brief Process ``in`` in place. */
      YSE::DSP::buffer& operator()(YSE::DSP::buffer& in);
      sqrt();

    private:
      YSE::DSP::buffer buffer;
    };

    /** @brief Fractional part — ``x - floor(x)`` per sample. */
    class API wrap {
    public:
      /** @brief Process ``in`` in place. */
      YSE::DSP::buffer& operator()(YSE::DSP::buffer& in);

    private:
      YSE::DSP::buffer buffer;
    };

    /**
     *  @brief Per-sample MIDI note → Hz conversion. Buffer-rate counterpart of ``MidiToFreq``.
     *
     *  Twelve-tone equal temperament tuned so that the reference note
     *  (``setRef``, default 69) sounds at the base frequency (``setBase``,
     *  default 440 Hz): ``base * 2 ** ((note - ref) / 12)``. A base of 0 or
     *  below, or not finite, counts as 440; a reference that is not finite
     *  counts as 69. Notes more than 1569 semitones below the reference
     *  (and NaN) give 0 Hz; notes more than 1430 above it are clamped
     *  there; a result past the float range gives 0. No NaN or infinity
     *  leaves this class. The setters are atomic and may be called from any
     *  thread; processing allocates nothing for blocks of up to
     *  ``STANDARD_BUFFERSIZE`` samples.
     */
    class API midiToFreq {
    public:
      /** @brief The frequency of the reference note, in Hz. */
      midiToFreq& setBase(Flt value);
      Flt getBase() const {
        return base;
      }

      /** @brief The MIDI note that sounds at the base frequency. */
      midiToFreq& setRef(Flt value);
      Flt getRef() const {
        return ref;
      }

      /** @brief Process ``in`` in place. */
      YSE::DSP::buffer& operator()(YSE::DSP::buffer& in);

    private:
      aFlt base{440.f};
      aFlt ref{69.f};
      YSE::DSP::buffer buffer;
    };

    /**
     *  @brief Per-sample Hz → MIDI note conversion. Buffer-rate counterpart of ``FreqToMidi``.
     *
     *  The inverse of ``midiToFreq``, with the same ``setBase`` / ``setRef``
     *  tuning: ``ref + 12 * log2(freq / base)``. A frequency of 0 or below
     *  (and NaN) gives -1500 and an infinite one is clamped to the largest
     *  float, so no NaN or infinity leaves this class.
     */
    class API freqToMidi {
    public:
      /** @brief The frequency of the reference note, in Hz. */
      freqToMidi& setBase(Flt value);
      Flt getBase() const {
        return base;
      }

      /** @brief The MIDI note that sounds at the base frequency. */
      freqToMidi& setRef(Flt value);
      Flt getRef() const {
        return ref;
      }

      /** @brief Process ``in`` in place. */
      YSE::DSP::buffer& operator()(YSE::DSP::buffer& in);

    private:
      aFlt base{440.f};
      aFlt ref{69.f};
      YSE::DSP::buffer buffer;
    };

    /** @brief Per-sample dB → linear (RMS amplitude) conversion. */
    class API dbToRms {
    public:
      /** @brief Process ``in`` in place. */
      YSE::DSP::buffer& operator()(YSE::DSP::buffer& in);

    private:
      YSE::DSP::buffer buffer;
    };

    /** @brief Per-sample linear (RMS amplitude) → dB conversion. */
    class API rmsToDb {
    public:
      /** @brief Process ``in`` in place. */
      YSE::DSP::buffer& operator()(YSE::DSP::buffer& in);

    private:
      YSE::DSP::buffer buffer;
    };

    /** @brief Per-sample dB → linear (power) conversion. */
    class API dbToPow {
    public:
      /** @brief Process ``in`` in place. */
      YSE::DSP::buffer& operator()(YSE::DSP::buffer& in);

    private:
      YSE::DSP::buffer buffer;
    };

    /** @brief Per-sample linear (power) → dB conversion. */
    class API powToDb {
    public:
      /** @brief Process ``in`` in place. */
      YSE::DSP::buffer& operator()(YSE::DSP::buffer& in);

    private:
      YSE::DSP::buffer buffer;
    };

    /**
     *  @brief Per-sample power: ``base[i] ** exponent[i]``.
     *
     *  The base comes first. A base of 0 or below gives 0 (there is no real
     *  power of a negative base for most exponents), and so does a result
     *  past the float range or a NaN operand: no NaN or infinity leaves this
     *  class. The patcher's ``~pow`` takes the two the other way round, as
     *  Max's ``pow~`` does; the class does not.
     */
    class API pow {
    public:
      /** @brief Compute ``base[i] ** exponent[i]``. */
      YSE::DSP::buffer& operator()(YSE::DSP::buffer& base, YSE::DSP::buffer& exponent);

    private:
      YSE::DSP::buffer buffer;
    };

    /** @brief Per-sample exponential (``e ** in[i]``). */
    class API exp {
    public:
      /** @brief Process ``in`` in place. */
      YSE::DSP::buffer& operator()(YSE::DSP::buffer& in);

    private:
      YSE::DSP::buffer buffer;
    };

    /**
     *  @brief Per-sample logarithm with arbitrary base (``log_{base}(in)``).
     *
     *  An input of 0 or below has no logarithm and gives 0. A base of 0 or
     *  below, or of 1, falls back to the natural logarithm. Anything still
     *  not finite (a NaN, an infinite input) gives 0, so no NaN or infinity
     *  leaves this class.
     */
    class API log {
    public:
      /** @brief Compute ``log_{base[i]}(in[i])``. */
      YSE::DSP::buffer& operator()(YSE::DSP::buffer& in, YSE::DSP::buffer& base);

    private:
      YSE::DSP::buffer buffer;
    };

    /** @brief Per-sample absolute value. */
    class API abs {
    public:
      /** @brief Process ``in`` in place. */
      YSE::DSP::buffer& operator()(YSE::DSP::buffer& in);

    private:
      YSE::DSP::buffer buffer;
    };

    /**
     *  @brief Per-sample inversion.
     *
     *  In ``zeroToOne`` mode the output is ``1 - in[i]`` — useful for
     *  inverting a normalised control signal. Otherwise the output is the
     *  signed negation ``-in[i]``.
     */
    class API inverter {
    public:
      /** @brief Process ``in`` in place. ``zeroToOne`` selects the inversion mode. */
      YSE::DSP::buffer& operator()(YSE::DSP::buffer& in, bool zeroToOne = false);

    private:
      YSE::DSP::buffer buffer;
    };
  } // namespace DSP
} // namespace YSE

#endif // MATH_H_INCLUDED

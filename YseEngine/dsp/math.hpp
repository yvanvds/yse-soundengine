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

    /** @brief Convert a MIDI note number (0-127) to a frequency in Hz. Scalar form. */
    Flt API MidiToFreq(Flt note);

    /** @brief Convert a frequency in Hz to a MIDI note number. Scalar form. */
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
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);

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
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
      rSqrt();
    private:
      YSE::DSP::buffer buffer;
    };

    /**
     *  @brief Fast square root (~8 mantissa bits of precision).
     *
     *  Same trade-off as ``rSqrt`` — cheaper than the standard library, less
     *  precise. Use in DSP paths where speed matters.
     */
    class API sqrt {
    public:
      /** @brief Process ``in`` in place. */
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
      sqrt();
    private:
      YSE::DSP::buffer buffer;
    };

    /** @brief Fractional part — ``x - floor(x)`` per sample. */
    class API wrap {
    public:
      /** @brief Process ``in`` in place. */
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
    private:
      YSE::DSP::buffer buffer;
    };

    /** @brief Per-sample MIDI note → Hz conversion. Buffer-rate counterpart of ``MidiToFreq``. */
    class API midiToFreq {
    public:
      /** @brief Process ``in`` in place. */
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
    private:
      YSE::DSP::buffer buffer;
    };

    /** @brief Per-sample Hz → MIDI note conversion. Buffer-rate counterpart of ``FreqToMidi``. */
    class API freqToMidi {
    public:
      /** @brief Process ``in`` in place. */
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
    private:
      YSE::DSP::buffer buffer;
    };

    /** @brief Per-sample dB → linear (RMS amplitude) conversion. */
    class API dbToRms {
    public:
      /** @brief Process ``in`` in place. */
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
    private:
      YSE::DSP::buffer buffer;
    };

    /** @brief Per-sample linear (RMS amplitude) → dB conversion. */
    class API rmsToDb {
    public:
      /** @brief Process ``in`` in place. */
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
    private:
      YSE::DSP::buffer buffer;
    };

    /** @brief Per-sample dB → linear (power) conversion. */
    class API dbToPow {
    public:
      /** @brief Process ``in`` in place. */
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
    private:
      YSE::DSP::buffer buffer;
    };

    /** @brief Per-sample linear (power) → dB conversion. */
    class API powToDb {
    public:
      /** @brief Process ``in`` in place. */
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
    private:
      YSE::DSP::buffer buffer;
    };

    /** @brief Per-sample power: ``in1[i] ** in2[i]``. */
    class API pow {
    public:
      /** @brief Compute ``in1[i] ** in2[i]`` in place. */
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in1, YSE::DSP::buffer & in2);
    private:
      YSE::DSP::buffer buffer;
    };

    /** @brief Per-sample exponential (``e ** in[i]``). */
    class API exp {
    public:
      /** @brief Process ``in`` in place. */
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
    private:
      YSE::DSP::buffer buffer;
    };

    /** @brief Per-sample logarithm with arbitrary base (``log_{in2}(in1)``). */
    class API log {
    public:
      /** @brief Compute ``log_{in2}(in1)`` in place. */
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in1, YSE::DSP::buffer & in2);
    private:
      YSE::DSP::buffer buffer;
    };

    /** @brief Per-sample absolute value. */
    class API abs {
    public:
      /** @brief Process ``in`` in place. */
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
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
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in, bool zeroToOne = false);
    private:
      YSE::DSP::buffer buffer;
    };
  }
}

#endif  // MATH_H_INCLUDED

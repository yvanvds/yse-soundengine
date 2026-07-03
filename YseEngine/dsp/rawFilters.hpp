/*
  ==============================================================================

    rawFilters.hpp
    Created: 15 Sep 2015 6:39:45pm
    Author:  yvan

  ==============================================================================
*/

#include "buffer.hpp"

#ifndef RAWFILTERS_HPP_INCLUDED
#define RAWFILTERS_HPP_INCLUDED

namespace YSE {
  namespace DSP {

    /**
     *  @brief Real-valued one-pole IIR filter.
     *
     *  Building block for low-level filter design. Takes a sample buffer and a
     *  per-sample feedback coefficient buffer.
     */
    class API realOnePole {
    public:
      realOnePole() : lastSample(0) {}

      /** @brief Apply one-pole IIR: ``out[i] = in1[i] + in2[i] * out[i-1]``. */
      buffer& operator()(buffer& in1, buffer& in2);

    private:
      Flt lastSample;
      buffer out;
    };

    /**
     *  @brief Real-valued one-zero FIR filter.
     *
     *  Building block: ``out[i] = in1[i] + in2[i] * in1[i-1]``.
     */
    class API realOneZero {
    public:
      realOneZero() : lastSample(0) {}

      /** @brief Apply one-zero FIR. */
      buffer& operator()(buffer& in1, buffer& in2);

    private:
      Flt lastSample;
      buffer out;
    };

    /** @brief Real-valued one-zero FIR with reversed feed-forward sign.
     *
     *  Building block: ``out[i] = in1[i] - in2[i] * in1[i-1]``.
     */
    class API realOneZeroReversed {
    public:
      realOneZeroReversed() : lastSample(0) {}

      /** @brief Apply reversed one-zero FIR. */
      buffer& operator()(buffer& in1, buffer& in2);

    private:
      Flt lastSample;
      buffer out;
    };

    /**
     *  @brief Complex one-pole IIR filter.
     *
     *  Operates on multichannel buffers: channel 0 carries the real part,
     *  channel 1 the imaginary part. Useful for building Hilbert transforms
     *  and other quadrature filters.
     */
    class API complexOnePole {
    public:
      complexOnePole();

      /** @brief Apply the complex one-pole filter. */
      MULTICHANNELBUFFER& operator()(MULTICHANNELBUFFER& in1, MULTICHANNELBUFFER& in2);

    private:
      Flt lastReal, lastImaginary;
      MULTICHANNELBUFFER out;
    };

    /** @brief Complex one-zero FIR filter. See ``complexOnePole`` for the channel convention. */
    class API complexOneZero {
    public:
      complexOneZero();

      /** @brief Apply the complex one-zero filter. */
      MULTICHANNELBUFFER& operator()(MULTICHANNELBUFFER& in1, MULTICHANNELBUFFER& in2);

    private:
      Flt lastReal, lastImaginary;
      MULTICHANNELBUFFER out;
    };

    /** @brief Complex one-zero FIR with reversed sign. See ``complexOnePole`` for the channel
     * convention. */
    class API complexOneZeroReversed {
    public:
      complexOneZeroReversed();

      /** @brief Apply the reversed complex one-zero filter. */
      MULTICHANNELBUFFER& operator()(MULTICHANNELBUFFER& in1, MULTICHANNELBUFFER& in2);

    private:
      Flt lastReal, lastImaginary;
      MULTICHANNELBUFFER out;
    };

  } // namespace DSP
} // namespace YSE

#endif // RAWFILTERS_HPP_INCLUDED

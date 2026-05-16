/*
  ==============================================================================

    interpolate4.hpp
    Created: 4 Sep 2015 11:53:08am
    Author:  yvan

  ==============================================================================
*/

#ifndef INTERPOLATE4_HPP_INCLUDED
#define INTERPOLATE4_HPP_INCLUDED

#include "buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief 4-point cubic interpolator.
     *
     *  Reads from a source ``buffer`` at fractional positions and produces
     *  smoothly interpolated output. Used internally for pitch-shifting and
     *  wavetable lookups; expose it directly when you need to resample or
     *  re-pitch a buffer at user-defined positions.
     */
    class API interpolate4 {
    public:
      interpolate4();

      /** @brief Set the source buffer to read from. */
      interpolate4 & source(buffer & data);

      /** @brief Currently attached source buffer. */
      buffer * source();

      /** @brief Set the read-offset relative to the start of the source. */
      interpolate4 & onset(Int value);

      /** @brief Current read-offset. */
      Int onset();

      /** @brief Resample ``in`` (treated as fractional positions) through the cubic kernel. */
      buffer & operator()(YSE::DSP::buffer & in);

    private:
      buffer * data;
      buffer out;
      aInt parmOnset;
    };

  }
}



#endif  // INTERPOLATE4_HPP_INCLUDED

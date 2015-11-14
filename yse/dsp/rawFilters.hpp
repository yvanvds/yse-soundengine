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

    class API realOnePole {
    public:
      realOnePole() : lastSample(0) {}

      buffer & operator()(buffer & in1, buffer & in2);

    private:
      Flt lastSample;
      buffer out;
    };

    /*********************************************************************/

    class API realOneZero {
    public:
      realOneZero() : lastSample(0) {}

      buffer & operator()(buffer & in1, buffer & in2);

    private:
      Flt lastSample;
      buffer out;
    };

    /*********************************************************************/

    class API realOneZeroReversed {
    public:
      realOneZeroReversed() : lastSample(0) {}

      buffer & operator()(buffer & in1, buffer & in2);

    private:
      Flt lastSample;
      buffer out;
    };

    /*********************************************************************/

    class API complexOnePole {
    public:
      complexOnePole();

      // Complex filters use multichannel buffers. The first channel should contain the
      // real part, the second channel the imaginary part.
      MULTICHANNELBUFFER & operator()(MULTICHANNELBUFFER & in1, MULTICHANNELBUFFER & in2);

    private:
      Flt lastReal, lastImaginary;
      MULTICHANNELBUFFER out;
    };

    /*********************************************************************/

    class API complexOneZero {
    public:
      complexOneZero();

      // Complex filters use multichannel buffers. The first channel should contain the
      // real part, the second channel the imaginary part.
      MULTICHANNELBUFFER & operator()(MULTICHANNELBUFFER & in1, MULTICHANNELBUFFER & in2);

    private:
      Flt lastReal, lastImaginary;
      MULTICHANNELBUFFER out;
    };

    /*********************************************************************/

    class API complexOneZeroReversed {
    public:
      complexOneZeroReversed();

      // Complex filters use multichannel buffers. The first channel should contain the
      // real part, the second channel the imaginary part.
      MULTICHANNELBUFFER & operator()(MULTICHANNELBUFFER & in1, MULTICHANNELBUFFER & in2);

    private:
      Flt lastReal, lastImaginary;
      MULTICHANNELBUFFER out;
    };

  }
}



#endif  // RAWFILTERS_HPP_INCLUDED

/*
  ==============================================================================

    lfo.hpp
    Created: 18 Sep 2015 6:10:24pm
    Author:  yvan

  ==============================================================================
*/

#ifndef LFO_HPP_INCLUDED
#define LFO_HPP_INCLUDED

#include "drawableBuffer.hpp"

namespace YSE {
  namespace DSP {

    enum LFO_TYPE {
      LFO_NONE,
      LFO_SAW,
      LFO_SAW_REVERSED,
      LFO_TRIANGLE,
      LFO_SINE,
      LFO_SQUARE,
      LFO_RANDOM,
    };

    class API lfo {
    public:
      lfo();

      // returns lfo buffer with values between 0 and 1
      buffer & operator()(LFO_TYPE type, Flt frequency);

    private:
      drawableBuffer result;
      Flt cursor;
      LFO_TYPE previousType;

      UInt lineLength;
      Flt currentLineValue, previousLineValue;

    };

  }
}



#endif  // LFO_HPP_INCLUDED

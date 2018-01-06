/*
  ==============================================================================

    math_functions.h
    Created: 31 Jan 2014 2:54:48pm
    Author:  yvan

  ==============================================================================
*/

#ifndef MATH_FUNCTIONS_H_INCLUDED
#define MATH_FUNCTIONS_H_INCLUDED

#include "../headers/types.hpp"
#include "buffer.hpp"

namespace YSE {
  namespace DSP {

    Flt* maximum(Flt *in1, Flt *in2, Flt *out, UInt length);
    Flt* maximum(Flt *in, Flt  f, Flt *out, UInt length);

    Flt* minimum(Flt *in1, Flt *in2, Flt *out, UInt length);
    Flt* minimum(Flt *in, Flt  f, Flt *out, UInt length);

    // utility functions
    Flt powToDb(Flt f);
    Flt rmsToDb(Flt f);
    Flt dbToPow(Flt f);
    Flt dbToRms(Flt f);

    Flt getMaxAmplitude(buffer & source);
    Flt getMaxAmplitude(Flt * pos, UInt windowSize);

    void sqrtFunc(Flt * in, Flt * out, UInt length);
  }
}



#endif  // MATH_FUNCTIONS_H_INCLUDED

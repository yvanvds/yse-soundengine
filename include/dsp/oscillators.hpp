/*
  ==============================================================================

    oscillators.h
    Created: 31 Jan 2014 2:54:59pm
    Author:  yvan

  ==============================================================================
*/

#ifndef OSCILLATORS_H_INCLUDED
#define OSCILLATORS_H_INCLUDED

#include "../headers/defines.hpp"
#include "../headers/types.hpp"
#include "../headers/constants.hpp"
#include "sample.hpp"

/* Constructor aside, all these objects should be used in dsp mode only */

namespace YSE {
  namespace DSP {

    class API saw {
    public:
      AUDIOBUFFER & operator()(Flt frequency, UInt length = STANDARD_BUFFERSIZE);
      AUDIOBUFFER & operator()(AUDIOBUFFER & in);
      saw();

    private:
      Dbl phase;
      Flt conv;
      Flt frequency;
      sample buffer;

      Flt *inPtr;
      void calc(Bool useFrequency);
    };

    class API cosine {
    public:
      AUDIOBUFFER & operator()(AUDIOBUFFER & in);
      cosine();

    private:
      sample buffer;
    };

    class API sine {
    public:
      AUDIOBUFFER & operator()(Flt frequency, UInt length = STANDARD_BUFFERSIZE);
      AUDIOBUFFER & operator()(AUDIOBUFFER & in);
      sine();
      void reset(); // set the phase back to zero 

    private:
      sample buffer;
      Dbl phase;
      Flt conv;
      Flt frequency;

      Flt *inPtr;
      void calc(Bool useFrequency);
    };


    class API noise {
    public:
      AUDIOBUFFER & operator()(UInt length = STANDARD_BUFFERSIZE);
      noise();

    private:
      sample buffer;
      Int value;
    };

    class API vcf {
    public:
      vcf& sharpness(Flt q);
      // TODO a bit awkward: first output is function out, second output sent to 3th argument
      AUDIOBUFFER & operator()(AUDIOBUFFER & in, AUDIOBUFFER & center, sample& out2);
      vcf();

    private:
      Flt re;
      Flt im;
      Flt q;
      Flt isr;
      sample buffer;
    };

  }
}



#endif  // OSCILLATORS_H_INCLUDED

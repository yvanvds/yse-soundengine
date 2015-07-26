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
#include "buffer.hpp"
#include "wavetable.hpp"

/* Constructor aside, all these objects should be used in dsp mode only */

namespace YSE {
  namespace DSP {

    class API saw {
    public:
      YSE::DSP::buffer & operator()(Flt frequency, UInt length = STANDARD_BUFFERSIZE);
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
      saw();

    private:
      Dbl phase;
      Flt conv;
      Flt frequency;
      YSE::DSP::buffer buffer;

      Flt *inPtr;
      void calc(Bool useFrequency);
    };

    class API cosine {
    public:
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
      cosine();

    private:
      YSE::DSP::buffer buffer;
    };

    class API sine {
    public:
      YSE::DSP::buffer & operator()(Flt frequency, UInt length = STANDARD_BUFFERSIZE);
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
      sine();
      void reset(); // set the phase back to zero 

    private:
      YSE::DSP::buffer buffer;
      Dbl phase;
      Flt conv;
      Flt frequency;

      Flt *inPtr;
      void calc(Bool useFrequency);
    };

    class API oscillator {
    public:
      oscillator();

      YSE::DSP::buffer & operator()(Flt frequency, UInt length = STANDARD_BUFFERSIZE);
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);

      void initialize(wavetable & source);
      void reset(); // set the phase back to zero 

    private:
      YSE::DSP::buffer buffer;
      Dbl phase;
      Flt conv;
      Flt frequency;
      wavetable * table;

      Flt *inPtr;
      void calc(Bool useFrequency);
    };

    class API noise {
    public:
      YSE::DSP::buffer & operator()(UInt length = STANDARD_BUFFERSIZE);
      noise();

    private:
      YSE::DSP::buffer buffer;
      Int value;
    };

    class API vcf {
    public:
      vcf& sharpness(Flt q);
      // TODO a bit awkward: first output is function out, second output sent to 3th argument
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in, YSE::DSP::buffer & center, YSE::DSP::buffer & out2);
      vcf();

    private:
      Flt re;
      Flt im;
      Flt q;
      Flt isr;
      YSE::DSP::buffer buffer;
    };

  }
}



#endif  // OSCILLATORS_H_INCLUDED

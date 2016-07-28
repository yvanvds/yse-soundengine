/*
  ==============================================================================

    filters.hpp
    Created: 31 Jan 2014 2:53:30pm
    Author:  yvan

  ==============================================================================
*/

#ifndef FILTERS_H_INCLUDED
#define FILTERS_H_INCLUDED

#include "buffer.hpp"
#include "../headers/enums.hpp"

namespace YSE {
  namespace DSP {

    // base class for all filters - don't use
    class API filterBase {
    public:
      filterBase();
      filterBase( const filterBase &);
      
      virtual buffer & operator()(buffer & in) = 0;

    protected:
      aFlt freq;
      aFlt gain;
      Flt q;
      Flt last;
      Flt previous;
      aFlt coef1, coef2;
      aFlt ff1, ff2, ff3, fb1, fb2;
      buffer samples;
    };

    /*******************************************************************************************/

    // highpass filter
    class API highPass : public filterBase {
    public:
      highPass& setFrequency(Flt f);
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
    };

    /*******************************************************************************************/

    // lowpass filter
    class API lowPass : public filterBase {
    public:
      lowPass& setFrequency(Flt f);
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
    };

    /*******************************************************************************************/

    // bandpass filter
    class API bandPass : public filterBase {
    public:
      bandPass& set(Flt freq, Flt q);
      bandPass& setFrequency(Flt freq);
      bandPass& setQ(Flt q);
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);
      bandPass();

    private:
      void calcCoef();
      static float qCos(Flt omega);
    };

    /*******************************************************************************************/

    // for visualizing biquad parameters, see: http://www.earlevel.com/main/2010/12/20/biquad-calculator/
    class API biQuad : public filterBase {
    public:
      biQuad& set(BQ_TYPE type, Flt frequency, Flt Q, Flt peakGain = 4);
      biQuad& setType(BQ_TYPE type);
      biQuad& setFreq(Flt frequency);
      biQuad& setQ(Flt Q);
      biQuad& setPeak(Flt peakGain);

      // use this for masochism
      biQuad& setRaw(Flt fb1, Flt fb2, Flt ff1, Flt ff2, Flt ff3);
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in);

    private:
      void calc();
      BQ_TYPE type;
    };

    /*******************************************************************************************/
    class API sampleHold {
    public:
      sampleHold& reset(Flt value = 1e20);
      sampleHold& set(Flt value);
      YSE::DSP::buffer & operator()(YSE::DSP::buffer & in, YSE::DSP::buffer & signal);
      
      sampleHold();

    private:
      aFlt lastIn, lastOut;
      YSE::DSP::buffer samples;
    };

    // looking for vcf? It is in oscillators because it shares a lot of that code



  }
}




#endif  // FILTERS_H_INCLUDED

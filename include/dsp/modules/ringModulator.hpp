/*
  ==============================================================================

    ringModulator.h
    Created: 31 Jan 2014 2:56:31pm
    Author:  yvan

  ==============================================================================
*/

#ifndef RINGMODULATOR_H_INCLUDED
#define RINGMODULATOR_H_INCLUDED

#include "../../headers/defines.hpp"
#include "../dspObject.hpp"
#include "../oscillators.hpp"
#include <memory>

/*	when building a new dsp module, take this as an example.
Also don't forget to:
1. add a new type to DSP_TYPE in dspElement.h
2. include your header file to dsp.h
4. add code to add and remove functions in dsp.cpp
5. add code to callback function in dsp.cpp
*/

namespace YSE {
  namespace DSP {
    class API ringModulator : public dspObject {
    public:
      // these functions are thread safe
      ringModulator();
      virtual ~ringModulator() {}

      ringModulator& frequency(Flt value);
      Flt            frequency();

      // dsp functions
      virtual void create();
      virtual void process(MULTICHANNELBUFFER & buffer);

    private:
      aFlt parmFrequency;
      aFlt parmLevel;
      std::shared_ptr<sine> sineGen;
      std::shared_ptr<buffer> result;
    };

  }
}




#endif  // RINGMODULATOR_H_INCLUDED

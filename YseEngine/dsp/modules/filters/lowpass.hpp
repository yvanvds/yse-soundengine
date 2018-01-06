/*
  ==============================================================================

    lowpass.h
    Created: 3 Aug 2014 10:08:59pm
    Author:  yvan

  ==============================================================================
*/

#ifndef LOWPASS_H_INCLUDED
#define LOWPASS_H_INCLUDED

#include "../../../headers/defines.hpp"
#include "../../dspObject.hpp"
#include "../../filters.hpp"
#include <memory>

// only works on mono buffers!

namespace YSE {
  namespace DSP {
    namespace MODULES {

      class API lowPassFilter : public dspObject {
      public:
        lowPassFilter();
        virtual ~lowPassFilter() {};

        lowPassFilter & frequency(Flt value);
        Flt             frequency();

        virtual void create();
        virtual void process(MULTICHANNELBUFFER & buffer);

      private:
        aFlt parmFrequency;
        std::shared_ptr<DSP::buffer> result;
        std::shared_ptr<lowPass> lp;
      };


    }
  }
}


#endif  // LOWPASS_H_INCLUDED

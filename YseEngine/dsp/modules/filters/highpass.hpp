/*
  ==============================================================================

    highpass.h
    Created: 3 Aug 2014 10:09:15pm
    Author:  yvan

  ==============================================================================
*/

#ifndef HIGHPASS_H_INCLUDED
#define HIGHPASS_H_INCLUDED


#include "../../../headers/defines.hpp"
#include "../../dspObject.hpp"
#include "../../filters.hpp"
#include <memory>

// only works on mono buffers!

namespace YSE {
  namespace DSP {
    namespace MODULES {

      class API highPassFilter : public dspObject {
      public:
        highPassFilter();
        virtual ~highPassFilter() {};

        highPassFilter & frequency(Flt value);
        Flt             frequency();

        virtual void create();
        virtual void process(MULTICHANNELBUFFER & buffer);

      private:
        aFlt parmFrequency;
        std::shared_ptr<DSP::buffer> result;
        std::shared_ptr<highPass> hp;
      };


    }
  }
}



#endif  // HIGHPASS_H_INCLUDED

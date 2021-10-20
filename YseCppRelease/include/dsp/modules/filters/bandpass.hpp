/*
  ==============================================================================

    bandpass.h
    Created: 3 Aug 2014 10:09:23pm
    Author:  yvan

  ==============================================================================
*/

#ifndef BANDPASS_H_INCLUDED
#define BANDPASS_H_INCLUDED


#include "../../../headers/defines.hpp"
#include "../../dspObject.hpp"
#include "../../filters.hpp"
#include <memory>

// only works on mono buffers!

namespace YSE {
  namespace DSP {
    namespace MODULES {

      class API bandPassFilter : public dspObject {
      public:
        bandPassFilter();
        virtual ~bandPassFilter() {};

        bandPassFilter & frequency(Flt value);
        Flt             frequency();

        bandPassFilter & setQ(Flt value);
        Flt              getQ();

        virtual void create();
        virtual void process(MULTICHANNELBUFFER & buffer);

      private:
        aFlt parmFrequency;
        aFlt parmQ;
        std::shared_ptr<DSP::buffer> result;
        std::shared_ptr<bandPass> bp;
      };


    }
  }
}



#endif  // BANDPASS_H_INCLUDED

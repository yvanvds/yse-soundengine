/*
  ==============================================================================

    highpassDelay.hpp
    Created: 30 Sep 2015 4:40:29pm
    Author:  yvan

  ==============================================================================
*/

#ifndef HIGHPASSDELAY_HPP_INCLUDED
#define HIGHPASSDELAY_HPP_INCLUDED

#include "basicDelay.hpp"
#include "../../filters.hpp"

namespace YSE {
  namespace DSP {
    namespace MODULES {

      class API highPassDelay : public basicDelay {
      public:
        highPassDelay();

        highPassDelay & frequency(Flt value);
        Flt frequency();

      private:
        virtual void createPreFilter();
        virtual void applyPreFilter(DSP::buffer & buffer);

        aFlt parmFrequency;
        std::shared_ptr<DSP::highPass> hp;

      };

    }
  }
}

#endif  // HIGHPASSDELAY_HPP_INCLUDED

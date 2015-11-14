/*
  ==============================================================================

    lowpassDelay.hpp
    Created: 30 Sep 2015 4:40:43pm
    Author:  yvan

  ==============================================================================
*/

#ifndef LOWPASSDELAY_HPP_INCLUDED
#define LOWPASSDELAY_HPP_INCLUDED

#include "basicDelay.hpp"
#include "../../filters.hpp"

namespace YSE {
  namespace DSP {
    namespace MODULES {

      class API lowPassDelay : public basicDelay {
      public:

        lowPassDelay();

        lowPassDelay & frequency(Flt value);
        Flt frequency();

      private:
        virtual void createPreFilter();
        virtual void applyPreFilter(DSP::buffer & buffer);

        aFlt parmFrequency;
        std::shared_ptr<DSP::lowPass> lp;

      };

    }
  }
}



#endif  // LOWPASSDELAY_HPP_INCLUDED

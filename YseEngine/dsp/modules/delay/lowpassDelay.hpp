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

      /**
       *  @brief ``basicDelay`` with a low-pass filter in front.
       *
       *  Darkens each successive echo — the classic "tape" delay character.
       *  Inherits the three-tap layout from ``basicDelay``.
       */
      class API lowPassDelay : public basicDelay {
      public:

        lowPassDelay();

        /** @brief Set the low-pass cutoff frequency in Hz. */
        lowPassDelay & frequency(Flt value);

        /** @brief Current cutoff frequency. */
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

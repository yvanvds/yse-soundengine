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

      /**
       *  @brief ``basicDelay`` with a high-pass filter in front.
       *
       *  Useful for ducking bass build-up in long delay tails. Inherits the
       *  three-tap layout from ``basicDelay``.
       */
      class API highPassDelay : public basicDelay {
      public:
        highPassDelay();

        /** @brief Set the high-pass cutoff frequency in Hz. */
        highPassDelay& frequency(Flt value);

        /** @brief Current cutoff frequency. */
        Flt frequency();

      private:
        virtual void createPreFilter();
        virtual void applyPreFilter(DSP::buffer& buffer);

        aFlt parmFrequency;
        std::shared_ptr<DSP::highPass> hp;
      };

    } // namespace MODULES
  } // namespace DSP
} // namespace YSE

#endif // HIGHPASSDELAY_HPP_INCLUDED

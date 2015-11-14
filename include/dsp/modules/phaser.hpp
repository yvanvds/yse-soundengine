/*
  ==============================================================================

    phaser.hpp
    Created: 15 Sep 2015 7:54:15pm
    Author:  yvan

  ==============================================================================
*/

#ifndef PHASER_HPP_INCLUDED
#define PHASER_HPP_INCLUDED

#include "../dspObject.hpp"
#include "../lfo.hpp"
#include "../rawFilters.hpp"
#include <memory>

#include "../math.hpp"
#include "../filters.hpp"

namespace YSE {
  namespace DSP {
    namespace MODULES {

      class API phaser : public dspObject {
      public:
        phaser();
        virtual ~phaser() {};

        // sweep frequency, typically a low (-24) frequency, default is 0.3, must be > 0
        phaser & frequency(Flt value);
        Flt      frequency();

        // range coicient, cannot be larger as 0.5, default is 0.1
        phaser & range(Flt value);
        Flt      range();

        virtual void create();
        virtual void process(MULTICHANNELBUFFER & buffer);

      private:
        aFlt parmFrequency;
        aFlt parmRange;

        std::shared_ptr<lfo> triangle;
        std::shared_ptr<realOneZeroReversed> rzero1;
        std::shared_ptr<realOnePole> rpole1;
        std::shared_ptr<realOneZeroReversed> rzero2;
        std::shared_ptr<realOnePole> rpole2;
        std::shared_ptr<realOneZeroReversed> rzero3;
        std::shared_ptr<realOnePole> rpole3;
        std::shared_ptr<realOneZeroReversed> rzero4;
        std::shared_ptr<realOnePole> rpole4;

        std::shared_ptr<buffer> result;
      };

    }
  }
}



#endif  // PHASER_HPP_INCLUDED

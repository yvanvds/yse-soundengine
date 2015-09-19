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
#include "../oscillators.hpp"
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

        virtual void create();
        virtual void process(MULTICHANNELBUFFER & buffer);

      private:
        std::shared_ptr<saw> sawTooth;
        std::shared_ptr<realOneZeroReversed> rzero1;
        std::shared_ptr<realOnePole> rpole1;
        std::shared_ptr<realOneZeroReversed> rzero2;
        std::shared_ptr<realOnePole> rpole2;
        std::shared_ptr<realOneZeroReversed> rzero3;
        std::shared_ptr<realOnePole> rpole3;
        std::shared_ptr<realOneZeroReversed> rzero4;
        std::shared_ptr<realOnePole> rpole4;

        std::shared_ptr<saw> phasor1;
        std::shared_ptr<saw> phasor2;
        std::shared_ptr<saw> phasor3;
        std::shared_ptr<saw> phasor4;

        std::shared_ptr<clip> clip1;
        std::shared_ptr<clip> clip2;
        std::shared_ptr<clip> clip3;
        std::shared_ptr<clip> clip4;

        std::shared_ptr<YSE::DSP::cosine> cos1;
        std::shared_ptr<YSE::DSP::cosine> cos2;
        std::shared_ptr<YSE::DSP::cosine> cos3;
        std::shared_ptr<YSE::DSP::cosine> cos4;

        std::shared_ptr<highPass> hp;
      };

    }
  }
}



#endif  // PHASER_HPP_INCLUDED

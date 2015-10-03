/*
  ==============================================================================

    sweep.hpp
    Created: 4 Sep 2015 10:49:34am
    Author:  yvan

  ==============================================================================
*/

#ifndef SWEEP_HPP_INCLUDED
#define SWEEP_HPP_INCLUDED

#include "../../../headers/defines.hpp"
#include "../../dspObject.hpp"
#include "../../filters.hpp"
#include "../../oscillators.hpp"
#include "../../interpolate4.hpp"
#include <memory>

namespace YSE {
  namespace DSP {
    namespace MODULES {

      class API sweepFilter : public dspObject {
      public:

        enum SHAPE {
          TRIANGLE,
          SAW,
          SQUARE,
        };

        sweepFilter(SHAPE shape = SAW);
        virtual ~sweepFilter() {};

        sweepFilter & speed(Flt value);
        Flt           speed();

        sweepFilter & depth(Int value); // 0 - 100
        Int           depth();

        sweepFilter & frequency(Int value); // 0 - 100
        Int           frequency();

        virtual void create();
        virtual void process(MULTICHANNELBUFFER & buffer);

      private:
        aFlt parmSpeed;
        aInt parmDepth;
        aInt parmFrequency;

        SHAPE shape;
        std::shared_ptr<wavetable> table;
        std::shared_ptr<oscillator> osc;
        std::shared_ptr<vcf> filter;
        std::shared_ptr<DSP::buffer> result;
        std::shared_ptr<interpolate4> interpolator;
      };

    }

  }


}

#endif  // SWEEP_HPP_INCLUDED

/*
  ==============================================================================

    difference.h
    Created: 4 Aug 2014 1:21:47pm
    Author:  yvan

  ==============================================================================
*/

#ifndef DIFFERENCE_H_INCLUDED
#define DIFFERENCE_H_INCLUDED


#include "../../../headers/defines.hpp"
#include "../../dspObject.hpp"
#include "../../filters.hpp"
#include "../../oscillators.hpp"
#include "../../math.hpp"
#include <memory>

// only works on mono buffers!

namespace YSE {
  namespace DSP {
    namespace MODULES {

      class API difference : public dspObject {
      public:
        difference();
        virtual ~difference() {};

        difference & frequency(Flt value);
        Flt frequency();

        difference & amplitude(Flt value);
        Flt amplitude();

        virtual void create();
        virtual void process(MULTICHANNELBUFFER & buffer);

      private:
        aFlt parmFrequency;
        aFlt parmAmplitude;

        std::shared_ptr<DSP::buffer> result;
        std::shared_ptr<sine> source;
        std::shared_ptr<clip> clipper;

      };

    }
  }
}

#endif  // DIFFERENCE_H_INCLUDED

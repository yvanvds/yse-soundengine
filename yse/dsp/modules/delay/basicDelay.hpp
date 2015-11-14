/*
  ==============================================================================

    basicDelay.hpp
    Created: 30 Sep 2015 4:40:17pm
    Author:  yvan

  ==============================================================================
*/

#ifndef BASICDELAY_HPP_INCLUDED
#define BASICDELAY_HPP_INCLUDED


#include "../../../headers/defines.hpp"
#include "../../dspObject.hpp"
#include "../../delay.hpp"
#include <memory>

// only works on mono buffers!

namespace YSE {
  namespace DSP {
    namespace MODULES {

      class API basicDelay : public dspObject {
      public:
          
        enum DELAY_NR {
          FIRST,
          SECOND,
          THIRD,
        };

      
        basicDelay();
        virtual ~basicDelay() {};

        basicDelay & set(DELAY_NR nr, Flt time, Flt gain); // time is delay in milliseconds
        Flt time(DELAY_NR nr);
        Flt gain(DELAY_NR nr);

        virtual void create();
        virtual void process(MULTICHANNELBUFFER & buffer);

      protected:
        // these don't do anything, but can be overwritten
        virtual void createPreFilter();
        virtual void applyPreFilter(DSP::buffer & buffer);

        aFlt time0, time1, time2;
        aFlt gain0, gain1, gain2;

        std::shared_ptr<DSP::buffer> result;
        std::shared_ptr<DSP::delay> delayBuffer;
        std::shared_ptr<DSP::buffer> reader;
      };


    }
  }
}




#endif  // BASICDELAY_HPP_INCLUDED

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

namespace YSE {
  namespace DSP {
    namespace MODULES {

      /**
       *  @brief Three-tap delay packaged as a chainable ``dspObject``.
       *
       *  Reads the same delay line at up to three offsets, each with its own
       *  time and gain. Use ``highPassDelay`` / ``lowPassDelay`` for the same
       *  effect with a filter in the feedback path.
       *
       *  @note Mono only — feeds the first channel of the multichannel buffer.
       */
      class API basicDelay : public dspObject {
      public:

        /** @brief Which of the three delay taps to address. */
        enum DELAY_NR {
          FIRST,
          SECOND,
          THIRD,
        };

        basicDelay();
        virtual ~basicDelay() {};

        /** @brief Configure one of the three taps.
         *  @param nr   Tap to configure.
         *  @param time Delay time in milliseconds.
         *  @param gain Gain for this tap.
         */
        basicDelay & set(DELAY_NR nr, Flt time, Flt gain);

        /** @brief Current delay time of tap ``nr``. */
        Flt time(DELAY_NR nr);

        /** @brief Current gain of tap ``nr``. */
        Flt gain(DELAY_NR nr);

        /** @brief dspObject lifecycle hook — allocates buffers. */
        virtual void create();

        /** @brief dspObject audio-thread entry point. */
        virtual void process(MULTICHANNELBUFFER & buffer);

      protected:
        /** @brief Hook for subclasses to construct a pre-filter (used by ``lowPassDelay`` / ``highPassDelay``). */
        virtual void createPreFilter();

        /** @brief Hook for subclasses to apply their pre-filter to ``buffer`` before reading from the delay line. */
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

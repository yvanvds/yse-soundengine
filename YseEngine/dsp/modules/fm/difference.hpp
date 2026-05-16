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

namespace YSE {
  namespace DSP {
    namespace MODULES {

      /**
       *  @brief FM difference-tone synthesis as a chainable ``dspObject``.
       *
       *  Generates a sine carrier and clips the sum of input and carrier to
       *  produce difference-frequency intermodulation tones. A simple
       *  FM/distortion hybrid for buzzy, bell-like textures.
       *
       *  @note Mono only — feeds the first channel of the multichannel buffer.
       */
      class API difference : public dspObject {
      public:
        difference();
        virtual ~difference() {};

        /** @brief Set the carrier frequency in Hz. */
        difference & frequency(Flt value);

        /** @brief Current carrier frequency. */
        Flt frequency();

        /** @brief Set the carrier amplitude. */
        difference & amplitude(Flt value);

        /** @brief Current carrier amplitude. */
        Flt amplitude();

        /** @brief dspObject lifecycle hook — allocates buffers. */
        virtual void create();

        /** @brief dspObject audio-thread entry point. */
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

/*
  ==============================================================================

    underWaterEffect.h
    Created: 1 Feb 2014 10:02:28pm
    Author:  yvan

  ==============================================================================
*/

#ifndef UNDERWATEREFFECT_H_INCLUDED
#define UNDERWATEREFFECT_H_INCLUDED

#include <atomic>
#include <forward_list>
#include "../classes.hpp"
#include "../dsp/buffer.hpp"
#include "../dsp/filters.hpp"
#include "../reverb/reverbInterface.hpp"

namespace YSE {
  namespace INTERNAL {
    class underWaterEffect {
    public:
      underWaterEffect();
      underWaterEffect & channel(CHANNEL::implementationObject * ch);
      CHANNEL::implementationObject * channel();

      underWaterEffect & setDepth(Flt value); // reverb has to be done before channel reverb processing
      underWaterEffect & apply(MULTICHANNELBUFFER & channelBuffer); // lowpass has to be done AFTER channel reverb processing

    private:
      Flt depth;
      DSP::buffer buffer;
      DSP::buffer lpBuffer;
      DSP::lowPass filter;
      reverb verb;
      std::atomic<CHANNEL::implementationObject *> activeChannel; // only one channel can get the underwatereffect
    };

    underWaterEffect & UnderWaterEffect();

  }
}



#endif  // UNDERWATEREFFECT_H_INCLUDED

/*
  ==============================================================================

    underWaterEffect.h
    Created: 1 Feb 2014 10:02:28pm
    Author:  yvan

  ==============================================================================
*/

#ifndef UNDERWATEREFFECT_H_INCLUDED
#define UNDERWATEREFFECT_H_INCLUDED

#include "../reverb/reverbInterface.hpp"
#include "../dsp/sample.hpp"
#include "../dsp/filters.hpp"
#include "../implementations/channelImplementation.h"
#include "JuceHeader.h"
#include <atomic>
#include <forward_list>

namespace YSE {
  namespace INTERNAL {
    class underWaterEffect {
    public:
      underWaterEffect();
      underWaterEffect & channel(INTERNAL::channelImplementation * ch);
      INTERNAL::channelImplementation * channel();

      underWaterEffect & setDepth(Flt value); // reverb has to be done before channel reverb processing
      underWaterEffect & apply(MULTICHANNELBUFFER & channelBuffer); // lowpass has to be done AFTER channel reverb processing

    private:
      Flt depth;
      DSP::sample buffer;
      DSP::sample lpBuffer;
      DSP::lowPass filter;
      reverb verb;
      std::atomic<INTERNAL::channelImplementation *> activeChannel; // only one channel can get the underwatereffect
    };

    underWaterEffect & UnderWaterEffect();

  }
}



#endif  // UNDERWATEREFFECT_H_INCLUDED

/*
  ==============================================================================

    reverbImplementation.h
    Created: 10 Mar 2014 8:02:05pm
    Author:  yvan

  ==============================================================================
*/

#ifndef REVERBIMPLEMENTATION_H_INCLUDED
#define REVERBIMPLEMENTATION_H_INCLUDED

#include "implementation.h"
#include "../headers/types.hpp"
#include "../reverb.hpp"

namespace YSE {

  namespace INTERNAL {

    class reverbImplementation : public implementation<reverb> {
    public:
      reverbImplementation(YSE::reverb * head);
      
      void parseMessage(const reverb::message & message);

    private:
      Vec position;
      Flt size, rolloff, roomsize, damp, dry, wet;
      Flt modFrequency, modWidth;
      Bool active;
      Int earlyPtr[4]; // early reflections
      Flt earlyGain[4];
    };

  }
}



#endif  // REVERBIMPLEMENTATION_H_INCLUDED

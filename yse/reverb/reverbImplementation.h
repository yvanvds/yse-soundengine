/*
  ==============================================================================

    reverbImplementation.h
    Created: 10 Mar 2014 8:02:05pm
    Author:  yvan

  ==============================================================================
*/

#ifndef REVERBIMPLEMENTATION_H_INCLUDED
#define REVERBIMPLEMENTATION_H_INCLUDED

#include "reverb.hpp"
#include "../templates/implementationObject.h"
#include "../utils/vector.hpp"
#include "reverbMessage.h"
#include "reverbInterface.hpp"
#include "../headers/types.hpp"

namespace YSE {
  namespace REVERB {
    
    /**
      The implementation counterpart of a reverb interface.
    */
    class implementationObject : public TEMPLATE::implementationObject<reverbSubSystem> {
    public:
      
      implementationObject(interfaceObject * head); // < Constructor needs a pointer to the interface
      
      virtual void parseMessage(const messageObject & message); // < Parse all messages, if any
      virtual void implementationSetup(){} // < no custom setup needed
      virtual Bool implementationReadyCheck() { return true; } // < no ready check needed

    private:
      // all reverb setting
      Vec position; // < world position for a localized reverb
      Flt size, rolloff, roomsize, damp, dry, wet;
      Flt modFrequency, modWidth;
      Bool active; // < won't be included in calculations if false
      Int earlyPtr[4]; // < early reflections
      Flt earlyGain[4]; // < gain of early reflections
    };

  }
}



#endif  // REVERBIMPLEMENTATION_H_INCLUDED

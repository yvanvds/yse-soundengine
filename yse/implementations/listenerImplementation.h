/*
  ==============================================================================

    listenerImplementation.h
    Created: 30 Jan 2014 4:22:09pm
    Author:  yvan

  ==============================================================================
*/

#ifndef LISTENERIMPLEMENTATION_H_INCLUDED
#define LISTENERIMPLEMENTATION_H_INCLUDED

#include "JuceHeader.h"
#include "../utils/vector.hpp"

namespace YSE {
  namespace INTERNAL {

    class listenerImplementation {
    public:
      void update();
      listenerImplementation();
      ~listenerImplementation();

      inline const Vec & getPos() { return newPos; }
      
      juce_DeclareSingleton(listenerImplementation, true)

    private:
      Vec newPos, lastPos;
      aVec pos;
      aVec up;
      aVec forward;
      aVec vel;

      friend class SOUND::implementationObject;
      friend class channelImplementation;
      friend class underWaterEffect;
      friend class listener;
      friend class reverbManager;
    };
  
  }
}



#endif  // LISTENERIMPLEMENTATION_H_INCLUDED

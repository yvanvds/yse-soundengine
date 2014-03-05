/*
  ==============================================================================

    listenerImplementation.h
    Created: 30 Jan 2014 4:22:09pm
    Author:  yvan

  ==============================================================================
*/

#ifndef LISTENERIMPLEMENTATION_H_INCLUDED
#define LISTENERIMPLEMENTATION_H_INCLUDED

#include "../utils/vector.hpp"
#include "JuceHeader.h"

namespace YSE {
  namespace INTERNAL {

    class listenerImplementation {
    public:
      void update();
      listenerImplementation();
      ~listenerImplementation();
      juce_DeclareSingleton(listenerImplementation, true)

    private:
      Vec newPos, lastPos;
      aVec pos;
      aVec up;
      aVec forward;
      aVec vel;

      friend class soundImplementation;
      friend class channelImplementation;
      friend class underWaterEffect;
      friend class listener;
      friend class reverbManager;
    };
  
  }
}



#endif  // LISTENERIMPLEMENTATION_H_INCLUDED

/*
  ==============================================================================

    listenerImplementation.h
    Created: 30 Jan 2014 4:22:09pm
    Author:  yvan

  ==============================================================================
*/

#ifndef LISTENERIMPLEMENTATION_H_INCLUDED
#define LISTENERIMPLEMENTATION_H_INCLUDED

#include "../classes.hpp"
#include "../utils/vector.hpp"

namespace YSE {
  namespace INTERNAL {

    class listenerImplementation {
    public:
      void update();
      listenerImplementation();

      inline const Vec & getPos() { return newPos; }
      

    private:
      Vec newPos, lastPos;
      aVec pos;
      aVec up;
      aVec forward;
      aVec vel;

      friend class SOUND::implementationObject;
      friend class channelImplementation;
      friend class underWaterEffect;
      friend class YSE::listener;
      friend class reverbManager;
    };
  
    listenerImplementation & ListenerImpl();
  }
}



#endif  // LISTENERIMPLEMENTATION_H_INCLUDED

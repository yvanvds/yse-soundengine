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
#include "../utils/atomicPos.h"

namespace YSE {
  namespace INTERNAL {

    class listenerImplementation {
    public:
      void update();
      listenerImplementation();

      inline const Pos& getPos() {
        return newPos;
      }

    private:
      Pos newPos, lastPos;
      aPos pos;
      aPos up;
      aPos forward;
      aPos vel;

      friend class SOUND::implementationObject;
      friend class channelImplementation;
      friend class underWaterEffect;
      friend class YSE::listener;
      friend class reverbManager;
      // The per-voice panner (issue #169) reads the same listener snapshot the
      // sound path does — position and forward — to derive each voice's pan.
      friend class YSE::DSP::panner;
    };

    listenerImplementation& ListenerImpl();
  } // namespace INTERNAL
} // namespace YSE

#endif // LISTENERIMPLEMENTATION_H_INCLUDED

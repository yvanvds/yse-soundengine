/*
  ==============================================================================

    soundMessage.h
    Created: 24 Mar 2014 3:06:51pm
    Author:  yvan

  ==============================================================================
*/

#ifndef SOUNDMESSAGE_H_INCLUDED
#define SOUNDMESSAGE_H_INCLUDED

#include "sound.hpp"
#include "../templates/messageObject.h"


namespace YSE {
  namespace SOUND {
    enum MESSAGE {
      POSITION,
      SPREAD,
      VOLUME_VALUE,
      VOLUME_TIME,
      SPEED,
      SIZE,
      LOOP,
      INTENT,
      OCCLUSION,
      DSP,
      TIME,
      RELATIVE,
      DOPPLER,
      PAN2D,
      FADE_AND_STOP,
      MOVE,
    };

    class messageObject : public TEMPLATE::messageObject<soundSubSystem> {

    };
  }
}




#endif  // SOUNDMESSAGE_H_INCLUDED

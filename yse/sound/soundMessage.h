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
    

    class messageObject : public TEMPLATE::messageTemplate<soundSubSystem> {

    };
  }
}




#endif  // SOUNDMESSAGE_H_INCLUDED

/*
  ==============================================================================

    channelMessage.h
    Created: 23 Mar 2014 12:14:47pm
    Author:  yvan

  ==============================================================================
*/

#ifndef CHANNELMESSAGE_H_INCLUDED
#define CHANNELMESSAGE_H_INCLUDED

#include "../templates/messageObject.h"
#include "channel.hpp"

namespace YSE {
  namespace CHANNEL {
    enum MESSAGE {
      VOLUME,
      MOVE,
      VIRTUAL,
      ATTACH_REVERB,
    };

    class messageObject : public TEMPLATE::messageObject<channelSubSystem> {

    };
  }
}


#endif  // CHANNELMESSAGE_H_INCLUDED

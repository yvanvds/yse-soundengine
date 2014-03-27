/*
  ==============================================================================

    reverbMessage.h
    Created: 22 Mar 2014 2:40:28pm
    Author:  yvan

  ==============================================================================
*/

#ifndef REVERBMESSAGE_H_INCLUDED
#define REVERBMESSAGE_H_INCLUDED

#include "../templates/messageObject.h"
#include "reverb.hpp"

namespace YSE {
  namespace REVERB {
    

    class messageObject : public TEMPLATE::messageTemplate<reverbSubSystem> {
      
    };
  }
}



#endif  // REVERBMESSAGE_H_INCLUDED

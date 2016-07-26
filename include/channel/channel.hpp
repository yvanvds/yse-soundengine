/*
  ==============================================================================

    channel.h
    Created: 23 Mar 2014 11:50:29am
    Author:  yvan

  ==============================================================================
*/

#ifndef CHANNEL_H_INCLUDED
#define CHANNEL_H_INCLUDED

namespace YSE {
  /** Every subSystem consists out of several class which are meant to work together.
  They all have an interface, implementation, manager, message and a message enumeration.
  */

  class channel; // interface object

  namespace CHANNEL {
    class implementationObject;
    class messageObject;
    class managerObject;
    enum MESSAGE {
      VOLUME,
      MOVE,
      VIRTUAL,
      ATTACH_REVERB,
    };
  }
}




#endif  // CHANNEL_H_INCLUDED

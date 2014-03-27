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

  For consistency they are all base on the same template classes. Because those template
  classes need to know about each other, we create a struct like below to pass it as
  a kind of lookup to very template.
  */
  namespace CHANNEL {
    class interfaceObject;
    class implementationObject;
    class messageObject;
    class managerObject;
    enum MESSAGE;
  }

  struct channelSubSystem {
    typedef CHANNEL::interfaceObject interfaceObject;
    typedef CHANNEL::implementationObject implementationObject;
    typedef CHANNEL::managerObject managerObject;
    typedef CHANNEL::messageObject messageObject;
    typedef CHANNEL::MESSAGE MESSAGE;
  };

  // the interface itself gets a more generic name, so that users can just
  // define a 'reverb' to get an interface object.
  typedef CHANNEL::interfaceObject channel;
}




#endif  // CHANNEL_H_INCLUDED

/*
  ==============================================================================

    playerMessage.h
    Created: 9 Apr 2015 1:40:20pm
    Author:  yvan

  ==============================================================================
*/



#ifndef PLAYERMESSAGE_H_INCLUDED
#define PLAYERMESSAGE_H_INCLUDED

#include "player.hpp"
#include "../headers/types.hpp"

namespace YSE {
  namespace PLAYER {

    /*
    Message objects are used to send messages from interface to implementation. In this
    case, a message will be sent from a channelInterfaceObject to a
    channelImplementationObject. They are a way to ensure threadsafe and lockfree communication
    between the two.
    */
    class messageObject {
    public:
      /** The ID of a message defines how it will be stored in the implementation
      */
      MESSAGE ID;

      /** The data is stored in a union, so to not use more data as needed. Other types
      could be added to this union if needed, but they should not exeed the current
      length (Flt[3]). (Doing so won't be unsafe, but it will affect every message
      used in by all subSystems. So don't.)
      */
      union {
        Bool boolValue;
        Int intValue;
        UInt uIntValue;
        Flt floatValue;
        Flt floatPair[2]; // linear interpolation, 0 = target value, 1 = time
        struct {
          void * ptr;
          Flt time;
        } object;
      };
    };

  }
}



#endif  // PLAYERMESSAGE_H_INCLUDED

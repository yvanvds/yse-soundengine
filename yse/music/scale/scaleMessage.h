/*
  ==============================================================================

    scaleMessage.h
    Created: 14 Apr 2015 2:55:22pm
    Author:  yvan

  ==============================================================================
*/

#ifndef SCALEMESSAGE_H_INCLUDED
#define SCALEMESSAGE_H_INCLUDED

#include "scale.hpp"
#include "../../headers/types.hpp"

namespace YSE {
  namespace SCALE {

    /*
    Message objects are used to send messages from interface to implementation. In this
    case, a message will be sent from a scaleInterfaceObject to a
    scaleImplementationObject. They are a way to ensure threadsafe and lockfree communication
    between the two.
    */
    class messageObject {
    public:
      /** The ID of a message defines how it will be stored in the implementation
      */
      MESSAGE ID;

      union {
        Flt floatPair[2]; // 0 = pitch, 1 = step
      };
    };

  }
}



#endif  // SCALEMESSAGE_H_INCLUDED

/*
  ==============================================================================

    motifMessage.h
    Created: 14 Apr 2015 6:18:09pm
    Author:  yvan

  ==============================================================================
*/

#ifndef MOTIFMESSAGE_H_INCLUDED
#define MOTIFMESSAGE_H_INCLUDED

#include "motif.hpp"
#include "../../headers/types.hpp"

namespace YSE {
  namespace MOTIF {

    /*
    Message objects are used to send messages from interface to implementation. In this
    case, a message will be sent from a motifInterfaceObject to a
    motifImplementationObject. They are a way to ensure threadsafe and lockfree communication
    between the two.
    */
    class messageObject {
    public:
      /** The ID of a message defines how it will be stored in the implementation
      */
      MESSAGE ID;

      union {
        Flt floatValue;
        void * ptr;
        struct {
          Flt position;
          Flt pitch;
          Flt volume;
          Flt length;
          Int channel;
        } note;

      };
    };

  }
}





#endif  // MOTIFMESSAGE_H_INCLUDED

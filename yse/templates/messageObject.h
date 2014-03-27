/*
  ==============================================================================

    message.h
    Created: 22 Mar 2014 1:39:29pm
    Author:  yvan

  ==============================================================================
*/

#ifndef MESSAGEOBJECT_H_INCLUDED
#define MESSAGEOBJECT_H_INCLUDED

#include "../headers/types.hpp"
#include "../headers/enums.hpp"

namespace YSE {
  namespace TEMPLATE {

    /** Every subsystem needs a messageObject based on this template.
        It is used for nonblocking & threadsafe communication between
        the implementation and the interface.
    */
    template <typename SUBSYSTEM>
    class messageTemplate {
    public:
      typedef typename SUBSYSTEM::MESSAGE MESSAGE;
      
      /** The ID of a message defines how it will be stored in the implementation
      */
      MESSAGE ID;

      /** The data is stored in a union, so to not use more data as needed. Other types
          could be added to this union if needed, but they should not exeed the current
          length (Flt[3]). (Doing so won't be unsafe, but it will affect every message
          used in by all subSystems. So don't.)
      */
      union {
        Bool   boolValue;
        Flt    vecValue[3];
        Flt    floatValue;
        UInt   uintValue;
        void * ptrValue;
        SOUND_INTENT intentValue;
      };
    };

  }
}




#endif  // MESSAGE_H_INCLUDED

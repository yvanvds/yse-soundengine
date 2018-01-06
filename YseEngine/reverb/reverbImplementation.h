/*
  ==============================================================================

    reverbImplementation.h
    Created: 10 Mar 2014 8:02:05pm
    Author:  yvan

  ==============================================================================
*/

#ifndef REVERBIMPLEMENTATION_H_INCLUDED
#define REVERBIMPLEMENTATION_H_INCLUDED

#include "reverb.hpp"
#include "../utils/vector.hpp"
#include "reverbMessage.h"
#include "reverbInterface.hpp"
#include "../headers/types.hpp"
#include "../utils/lfQueue.hpp"

namespace YSE {
  namespace REVERB {
    
    /**
      The implementation counterpart of a reverb interface.
    */
    class implementationObject  {
    public:
      
      implementationObject(reverb * head); // < Constructor needs a pointer to the interface
      ~implementationObject();
      Bool readyCheck();
      void removeInterface();
      OBJECT_IMPLEMENTATION_STATE getStatus();
      void setStatus(OBJECT_IMPLEMENTATION_STATE value);

      void sync();
      virtual void parseMessage(const messageObject & message); // < Parse all messages, if any
      inline void sendMessage(const messageObject & message) { messages.push(message); }

      /**
      This function is used by the forward_list remove_if function
      */
      static bool canBeDeleted(const implementationObject& impl) {
        return impl.objectStatus == OBJECT_DELETE;
      }

      /**
      This function is used by the forward_list remove_if function
      */
      static bool canBeRemovedFromLoading(const std::atomic<implementationObject*> & impl) {
        if (impl.load()->objectStatus == OBJECT_READY
          || impl.load()->objectStatus == OBJECT_RELEASE
          || impl.load()->objectStatus == OBJECT_DELETE) {
          return true;
        }

        return false;
      }
      
    private:
      std::atomic<reverb *> head; // < The interface connected to this object
      std::atomic<OBJECT_IMPLEMENTATION_STATE> objectStatus; // < the status of this object
      lfQueue<messageObject> messages;

      // all reverb setting
      Pos position; // < world position for a localized reverb
      Flt size, rolloff, roomsize, damp, dry, wet;
      Flt modFrequency, modWidth;
      Bool active; // < won't be included in calculations if false
      Int earlyPtr[4]; // < early reflections
      Flt earlyGain[4]; // < gain of early reflections

      friend class YSE::REVERB::managerObject;
    };

  }
}



#endif  // REVERBIMPLEMENTATION_H_INCLUDED

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
    class implementationObject {
    public:
      implementationObject(reverb* head); // < Constructor needs a pointer to the interface
      virtual ~implementationObject();
      Bool readyCheck();
      void removeInterface();
      OBJECT_IMPLEMENTATION_STATE getStatus();
      void setStatus(OBJECT_IMPLEMENTATION_STATE value);

      void sync();

      /**
      Copy only the DSP parameter fields of this (audio-thread-synced)
      implementation into the interface object `dst`, leaving `dst`'s pimpl,
      connection and global state untouched. Used by managerObject on the
      audio thread to fold the global reverb into its scratch interface
      without aliasing pimpls (issue #192). Reading from the impl — which the
      audio thread owns after sync() — also avoids the data race on the
      interface's own parameter fields, which the main thread writes.
      */
      void copyParamsInto(reverb& dst) const;

      virtual void parseMessage(const messageObject& message); // < Parse all messages, if any
      inline void sendMessage(const messageObject& message) {
        messages.push(message);
      }

      /**
      This function is used by the forward_list remove_if function
      */
      static bool canBeDeleted(const implementationObject& impl) {
        return impl.objectStatus == OBJECT_DELETE;
      }

      /**
      This function is used by the forward_list remove_if function on the
      audio-thread-owned `toLoad` list.
      */
      static bool canBeRemovedFromLoading(const implementationObject* impl) {
        if (impl->objectStatus == OBJECT_READY || impl->objectStatus == OBJECT_RELEASE ||
            impl->objectStatus == OBJECT_DELETE) {
          return true;
        }

        return false;
      }

    private:
      // Intrusive forward-list link (issue #194). Threads this impl through the
      // REVERB manager's audio-thread toLoad/inUse lists (mutually exclusive
      // membership, so one link suffices). Touched only on the audio thread,
      // replacing the std::forward_list<T*> node without per-tick heap churn.
      implementationObject* _mgrNext = nullptr;

      std::atomic<reverb*> head; // < The interface connected to this object
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

  } // namespace REVERB
} // namespace YSE

#endif // REVERBIMPLEMENTATION_H_INCLUDED

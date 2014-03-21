/*
  ==============================================================================

    implementation.h
    Created: 10 Mar 2014 9:41:02am
    Author:  yvan

  ==============================================================================
*/

#ifndef IMPLEMENTATION_H_INCLUDED
#define IMPLEMENTATION_H_INCLUDED

#include <atomic>
#include "../headers/enums.hpp"
#include "../headers/defines.hpp"
#include "../headers/types.hpp"

namespace YSE {
  namespace INTERNAL {

    /**
      Base class for all implementations
    */
    template <class INTERFACE>
    class implementation {
    public:
      implementation(INTERFACE * head) : objectStatus(OBJECT_CONSTRUCTED), head(head) {}
      ~implementation() {
        exit();
      }

      /**
        This function will be called in a threadJob after the object is constructed
        For this to happen, the derived class should set the objectStatus to OBJECT_CREATED,
        possibly in the constructor or a custom create function AND call the manager's setup
        function with a pointer to this implementation as an argument
      */
      virtual void implementSetup() {
        objectStatus = OBJECT_SETUP;
      }

      /**
        This function will be called by the threadJob when an object is in create
        status.
      */
      void setup() {
        if (objectStatus >= OBJECT_CREATED) {
          implementationSetup();
        }
        objectStatus = OBJECT_SETUP;
      }

      /**
        This function should contain all custom checks to make sure an object is ready to
        use. It will be called on the dsp thread, so it must be fast. Do not allocate 
        resources in here, but do so in implementSetup. By returning false, the object will
        be sent back to the setup queue.
      */
      virtual Bool implementReadyCheck() {
        return true;
      }

      /**
        This function will be called on new objects by the dsp thread. It ensures the object
        is ready by calling the implementReadyCheck function above and move it to the list of
        objects in use if so.
      */
      Bool readyCheck() {
        if (objectStatus == OBJECT_SETUP) {
          if (implementationDoubleCheck()) {
            objectStatus = OBJECT_READY;
            return true;
          }
        }
        objectStatus = OBJECT_CREATED;
        return false;
      }

      /**
        This function should parse messages from the interface message pipe. It is responsable
        for syncronising information between head and implementation
      */
      void parseMessage(const INTERFACE::message & message) = 0;

      /**
        This function will be called when the system is instructed to do an update. It looks
        for messages in the interface message pipe and forwards them to the parseMessage
        function above.
      */
      void sync() {
        if (head == nullptr) {
          objectStatus = OBJECT_RELEASE;
          return;
        }

        INTERFACE::message message;
        while (messages.try_pop(message)) {
          parseMessage(message);
        }
      }

      std::atomic<INTERFACE *> head; // < The interface connected to this object
      std::atomic<OBJECT_IMPLEMENTATION_STATE> objectStatus; // < the status of this object
      lfQueue<INTERFACE::messages> messages;
    };
  }
}



#endif  // IMPLEMENTATION_H_INCLUDED

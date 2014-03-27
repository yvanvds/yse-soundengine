/*
  ==============================================================================

    implementation.h
    Created: 10 Mar 2014 9:41:02am
    Author:  yvan

  ==============================================================================
*/

#ifndef IMPLEMENTATIONOBJECT_H_INCLUDED
#define IMPLEMENTATIONOBJECT_H_INCLUDED

#include "../utils/lfQueue.hpp"

namespace YSE {
  namespace TEMPLATE {
    /**
      This is a base class for all implementationObjects that are part of a subSystem 
      (Which means they have an interfaceObject, implementationObject, managerObject,
      messageObject and a MESSAGE enumaration.
    */
    template <typename SUBSYSTEM>
    class implementationObject {
      //typedef typename NAMESPACE::interfaceObject interfaceObject;
      //typedef typename NAMESPACE::messageObject messageObject;

    public:
      typedef typename implementationObject<SUBSYSTEM> super;
      typedef typename SUBSYSTEM::interfaceObject derrivedInterface;
      typedef typename SUBSYSTEM::messageObject derrivedMessage;
      typedef typename SUBSYSTEM::implementationObject derrivedImplementation;

      // An implementation object can only by created with a pointer to respective interface supplied
      implementationObject(derrivedInterface * head) : objectStatus(OBJECT_CONSTRUCTED), head(head) {
        head->setInterfacePointer(&(head));
      }
      
      ~implementationObject() {
        exit();
      }

      /**
      This must be used to send a message to this implementation. It should be called from the
      corresponding interface object when a setting changes.
      */
      void sendMessage(const derrivedMessage & message) {
        messages.push(message);
      }

      /**
        This function should parse everything in the message pipe. It is responsable
        for syncronising information between the interface and the implementation. It should
        handle all messages that are defined in the enumeration for this subSystem.
      */
      virtual void parseMessage(const derrivedMessage & message) = 0;

      /**
        If your implementation needs to do anything after parsing messages, you should
        implement this function. It runs in the DSP thread, but only when an update is
        requested (normally between 20-60 times a second)
      */
      virtual void update() {}

      /**
        This function will be called in a threadJob after the object is constructed
        For this to happen, the interfaceObject must call the manager's setup
        function with a pointer to this implementation as an argument. If you don't need
        anything to be done, just implement it as an empty function
      */
      virtual void implementationSetup() = 0;

      /**
        This function should contain all custom checks to make sure an object is ready to
        use. It will be called on the dsp thread, so it must be fast. Do not allocate
        resources in here, but do so in implementSetup. By returning false, the object will
        be sent back to the setup queue.
      */
      virtual Bool implementationReadyCheck() = 0;

      /**
        If anything should be done after an implementation is ready for use, but before it
        is added to the the list of objects that is parsed by the DSP, you should implement
        this function. (You don't have to call it yourself as this is done by the
        TEMPLATE::managerObject.)
      */
      virtual void doThisWhenReady() {}

      /**
        If anything needs to be done when deconstructing this object, you should implement 
        this function.
      */
      virtual void exit() {}

      /**
        This function will be called by the threadJob when an object is in create
        status. It will run the implementationSetup function which must be supplied by 
        derived classes. (You don't have to call it yourself as this is done by the
        TEMPLATE::managerObject.)
      */
      void setup() {
        if (objectStatus >= OBJECT_CREATED) {
          implementationSetup();
        }
        if (objectStatus != OBJECT_DELETE) {
          objectStatus = OBJECT_SETUP;
        }
      }

      /**
        This function will be called on new objects by the dsp thread. It ensures the object
        is ready by calling the implementReadyCheck function above and move it to the list of
        objects in use if so. (You don't have to call it yourself as this is done by the
        TEMPLATE::managerObject.)
      */
      Bool readyCheck() {
        if (objectStatus == OBJECT_SETUP) {
          if (implementationReadyCheck()) {
            objectStatus = OBJECT_READY;
            return true;
          }
        }
        objectStatus = OBJECT_CREATED;
        return false;
      }

      /**
        This function will be called when the system is instructed to do an update. It looks
        for messages in the interface message pipe and forwards them to the parseMessage
        function above. (You don't have to call it yourself as this is done by the
        TEMPLATE::managerObject.)
      */
      virtual void sync() {
        if (head.load() == nullptr) {
          objectStatus = OBJECT_RELEASE;
          return;
        }

        derrivedMessage message;
        while (messages.try_pop(message)) {
          parseMessage(message);
        }
      }

      /**
        This function is used in the deconstructor of the interface to unlink it from 
        this implementation. When gone, the object will be flagged for deletion.
      */
      void removeInterface() {
        this->head = nullptr;
      }

      /**
        This function is used by the managerObject to evaluate the status of this implementation.
      */
      OBJECT_IMPLEMENTATION_STATE getStatus() {
        return objectStatus.load();
      }

      /**
      This function is used by the managerObject to change the status of this implementation.
      */
      void setStatus(OBJECT_IMPLEMENTATION_STATE value) {
        objectStatus.store(value);
      }

      /**
      This function is used by the forward_list remove_if function
      */
      static bool canBeDeleted(const derrivedImplementation& impl) {
        return impl.objectStatus == OBJECT_DELETE;
      }

      /**
      This function is used by the forward_list remove_if function
      */
      static bool canBeRemovedFromLoading(const std::atomic<derrivedImplementation*> & impl) {
        if (impl.load()->objectStatus == OBJECT_READY
          || impl.load()->objectStatus == OBJECT_RELEASE
          || impl.load()->objectStatus == OBJECT_DELETE) {
          return true;
        }

        return false;
      }

      

    protected:
      std::atomic<derrivedInterface *> head; // < The interface connected to this object
      std::atomic<OBJECT_IMPLEMENTATION_STATE> objectStatus; // < the status of this object
      lfQueue<derrivedMessage> messages;
    };
  }
}



#endif  // IMPLEMENTATION_H_INCLUDED

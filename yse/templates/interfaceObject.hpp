/*
  ==============================================================================

    interface.hpp
    Created: 10 Mar 2014 9:41:24am
    Author:  yvan

  ==============================================================================
*/

#ifndef INTERFACEOBJECT_H_INCLUDED
#define INTERFACEOBJECT_H_INCLUDED

#include <atomic>

namespace YSE {
  namespace TEMPLATE {

    /**
     Base class for all interfaceObjects. You should extend it mainly with set/get
     functions which send messages to your implementationObject.
     */
    template <typename SUBSYSTEM>
    class interfaceObject {
    public:
      typedef typename interfaceObject<SUBSYSTEM> super;
      typedef typename SUBSYSTEM::implementationObject derrivedImplementation;
      typedef typename SUBSYSTEM::interfaceObject derrivedInterface;

      interfaceObject() : pimpl(nullptr) {}

      ~interfaceObject() {
        if (pimpl != nullptr) {
          *self = nullptr;
          pimpl = nullptr;
        }
      }

      /** Call this function at the start of your own create function to make sure it won't
          be called twice. In a create function, you should include the following code at the
          end to create an implementationObject and do some setup routines on it (Replace
          XXX with the mananager object you need):

          pimpl = INTERNAL::Global.getXXXManager().addImplementation(this);
          INTERNAL::Global.getXXXManager().setup(pimpl);
      */
      virtual void create() {
        assert(pimpl == nullptr); 
      }

      void isValid() {
        return pimpl != nullptr;
      }

      // get the implementation, this can be a nullptr if not connected.
      derrivedImplementation * getImplementation() {
        return pimpl;
      }

      // provide a pointer to this interface that is used by the impletation
      // This function is only intended for internal use!
      void setInterfacePointer(derrivedInterface ** ptr) {
        self = ptr;
      }

    //protected:
      derrivedImplementation * pimpl;

      // The implementation has a pointer to this interface which should be set to
      // zero when destructing this interface. But we cannot access the implementation's 
      // functions from the interface because that would create a dependency on it. Which
      // we don't want: the whole point of separating the two is to create a library which
      // can be used without knowing about the internal objects (and their dependencies).
      derrivedInterface ** self; // TODO: this is not theadsafe! How to pass a pointer to an atomic<ptr>??
    };

  }
}



#endif  // INTERFACE_H_INCLUDED

/*
  ==============================================================================

    interface.hpp
    Created: 10 Mar 2014 9:41:24am
    Author:  yvan

  ==============================================================================
*/

#ifndef INTERFACEOBJECT_H_INCLUDED
#define INTERFACEOBJECT_H_INCLUDED

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

      interfaceObject() : pimpl(nullptr) {}

      ~interfaceObject() {
        if (pimpl != nullptr) {
          pimpl->removeInterface();
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

    protected:
      derrivedImplementation * pimpl;
    };

  }
}



#endif  // INTERFACE_H_INCLUDED

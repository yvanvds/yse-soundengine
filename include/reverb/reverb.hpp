/*
  ==============================================================================

    reverb.hpp
    Created: 22 Mar 2014 3:17:07pm
    Author:  yvan

  ==============================================================================
*/

#ifndef REVERB_H_INCLUDED
#define REVERB_H_INCLUDED

namespace YSE {
  /** Every subSystem consists out of several class which are meant to work together.
      They all have an interface, implementation, manager, message and a message enumeration.

      For consistency they are all base on the same template classes. Because those template
      classes need to know about each other, we create a struct like below to pass it as
      a kind of lookup to very template.
  */
  namespace REVERB {
    class interfaceObject;
    class implementationObject;
    class messageObject;
    class managerObject;
    enum MESSAGE {
      POSITION,
      SIZE,
      ROLLOFF,
      ACTIVE,
      ROOMSIZE,
      DAMP,
      DRY_WET,
      MODULATION,
      REFLECTION,
    };
  }


  // the interface itself gets a more generic name, so that users can just
  // define a 'reverb' to get an interface object.
  typedef REVERB::interfaceObject reverb;
}



#endif  // REVERB_H_INCLUDED

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
  */

	class reverb; // interface object

  namespace REVERB {
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

}



#endif  // REVERB_H_INCLUDED

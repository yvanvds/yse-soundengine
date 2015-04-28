/*
  ==============================================================================

    scale.hpp
    Created: 14 Apr 2015 2:53:42pm
    Author:  yvan

  ==============================================================================
*/

#ifndef SCALE_HPP_INCLUDED
#define SCALE_HPP_INCLUDED

namespace YSE {
  /** Every subSystem consists out of several class which are meant to work together.
  They all have an interface, implementation, manager, message and a message enumeration.
  */

  namespace SCALE {
    class interfaceObject;
    class implementationObject;
    class messageObject;
    class managerObject;

    enum MESSAGE {
      ADD,
      REMOVE,
      CLEAR,
    };
  }

  // the interface itself gets a more generic name, so that users can just
  // define a 'scale' to get an interface object.
  typedef SCALE::interfaceObject scale;

}



#endif  // SCALE_HPP_INCLUDED

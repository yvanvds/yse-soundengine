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
  class scale;

  namespace SCALE {
    class implementationObject;
    class messageObject;
    class managerObject;

    enum MESSAGE {
      ADD,
      REMOVE,
      CLEAR,
    };
  }

}



#endif  // SCALE_HPP_INCLUDED

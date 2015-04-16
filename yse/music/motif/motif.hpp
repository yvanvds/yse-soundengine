/*
  ==============================================================================

    motif.hpp
    Created: 14 Apr 2015 6:19:33pm
    Author:  yvan

  ==============================================================================
*/

#ifndef MOTIF_HPP_INCLUDED
#define MOTIF_HPP_INCLUDED


namespace YSE {
  /** Every subSystem consists out of several class which are meant to work together.
  They all have an interface, implementation, manager, message and a message enumeration.
  */

  namespace MOTIF {
    class interfaceObject;
    class implementationObject;
    class messageObject;
    class managerObject;

    enum MESSAGE {
      ADD,
      CLEAR,
      LENGTH,
      TRANSPOSE,
      FIRST_PITCH,
    };
  }

  // the interface itself gets a more generic name, so that users can just
  // define a 'player' to get an interface object.
  typedef MOTIF::interfaceObject motif;

}



#endif  // MOTIF_HPP_INCLUDED

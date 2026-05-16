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
  class motif;

  /// @cond INTERNAL
  namespace MOTIF {
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
  /// @endcond

}



#endif  // MOTIF_HPP_INCLUDED

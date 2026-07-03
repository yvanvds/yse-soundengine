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
  class scale;

  /// @cond INTERNAL
  namespace SCALE {
    class implementationObject;
    class messageObject;
    class managerObject;

    enum MESSAGE {
      ADD,
      REMOVE,
      CLEAR,
    };
  } // namespace SCALE
  /// @endcond

} // namespace YSE

#endif // SCALE_HPP_INCLUDED

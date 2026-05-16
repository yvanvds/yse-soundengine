/*
  ==============================================================================

    player.hpp
    Created: 9 Apr 2015 1:37:30pm
    Author:  yvan

  ==============================================================================
*/

#ifndef PLAYER_HPP_INCLUDED
#define PLAYER_HPP_INCLUDED

namespace YSE {
  class player;

  /// @cond INTERNAL
  namespace PLAYER {
    class implementationObject;
    class messageObject;
    class managerObject;

    enum MESSAGE {
      PLAY,
      MIN_PITCH,
      MAX_PITCH,
      MIN_VELOCITY,
      MAX_VELOCITY,
      MIN_GAP,
      MAX_GAP,
      MIN_LENGTH,
      MAX_LENGTH,
      VOICES,
      SCALE,
      ADD_MOTIF,
      REM_MOTIF,
      ADJUST_MOTIF,
      PARTIAL_MOTIF,
      PLAY_MOTIF,
      MOTIF_FITS_SCALE,
    };
  }
  /// @endcond

}



#endif  // PLAYER_HPP_INCLUDED

/*
  ==============================================================================

    playerInterface.hpp
    Created: 9 Apr 2015 1:38:34pm
    Author:  yvan

  ==============================================================================
*/

#ifndef PLAYERINTERFACE_HPP_INCLUDED
#define PLAYERINTERFACE_HPP_INCLUDED

#include "../headers/defines.hpp"
#include "../headers/types.hpp"
#include "player.hpp"
//#include "../synth/synth.hpp"
#include "../music/motif/motif.hpp"

namespace YSE {

  class API player {
  public:
    player();
    ~player();

    //player& create(synth & s);
    player& play();
    player& stop();
    Bool isPlaying();

    // Modifiers to change player behaviour. If a time value is provided the
    // change will happen gradually (linear interpolation)
      
    // lowest and highest pitch that will be be played
    player& setMinimumPitch(Flt target, Flt time = 0); // range 0 - 126
    player& setMaximumPitch(Flt target, Flt time = 0); // range 1 - 127

    // lowest and highest velocity
    player& setMinimumVelocity(Flt target, Flt time = 0); // range 0 - 0.999999
    player& setMaximumVelocity(Flt target, Flt time = 0); // range 0.000001 - 1
      
    // space between notes or motifs
    player& setMinimumGap(Flt target, Flt time = 0); // range 0 - 
    player& setMaximumGap(Flt target, Flt time = 0); // range 0 -

    // length of notes when no motif is supplied
    player& setMinimumLength(Flt target, Flt time = 0); // range 0 -
    player& setMaximumLength(Flt target, Flt time = 0); // range 0 -

    // number of simultanious voices to be played
    player& setVoices(UInt target, Flt time = 0);

    // restrict played notes to this scale. the player makes a copy of this
    // scale, so alterations you make afterwards are not passed to the player.
    player& setScale(scale & scale, Flt time = 0);

    // Provide the player with a motif. Instead of random notes, all voices will play this
    // motif. Several motifs can be added and the player will pick a motif when needed,
    // taking weight into account.
    player& addMotif(motif & motif, UInt weight = 1);

    // remove this motif from the player
    player& removeMotif(motif & motif);

    // adjust the weight of a motif after it has been added
    player& adjustMotifWeight(motif & motif, UInt weight);

    // With target == 0, the full motif is always played, with target == 1 only parts
    // of the motif will be played. 
    player& playPartialMotifs(Flt target, Flt time = 0);

    // With target == 0 only random notes will be played, with target == 1 only motif
    // notes will be played.
    player& playMotifs(Flt target, Flt time = 0);

    // alter the notes in the motif to fit the current scale. If not, only the first note
    // will be taken from the scale.
    player& fitMotifsToScale(Flt target, Flt time = 0);



  private:
    PLAYER::implementationObject * pimpl;
    Bool _isPlaying;

    friend class PLAYER::implementationObject;
  };

  
}



#endif  // PLAYERINTERFACE_HPP_INCLUDED

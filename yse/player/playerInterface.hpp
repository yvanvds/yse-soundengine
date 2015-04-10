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
#include "../synth/synth.hpp"

namespace YSE {
  namespace PLAYER {

    class API interfaceObject {
    public:
      interfaceObject();
     ~interfaceObject();

      interfaceObject& create(synth & s);
      interfaceObject& play();
      interfaceObject& stop();

      // Modifiers to change player behaviour. If a time value is provided the
      // change will happen gradually (linear interpolation)
      
      // lowest and highest pitch that will be be played
      interfaceObject& setMinimumPitch(Flt target, Flt time = 0); // range 0 - 126
      interfaceObject& setMaximumPitch(Flt target, Flt time = 0); // range 1 - 127

      // lowest and highest velocity
      interfaceObject& setMinimumVelocity(Flt target, Flt time = 0); // range 0 - 0.999999
      interfaceObject& setMaximumVelocity(Flt target, Flt time = 0); // range 0.000001 - 1
      
      // space between notes or motifs
      interfaceObject& setMinimumGap(Flt target, Flt time = 0); // range 0 - 
      interfaceObject& setMaximumGap(Flt target, Flt time = 0); // range 0 -

      // length of notes when no motif is supplied
      interfaceObject& setMinimumLength(Flt target, Flt time = 0); // range 0 -
      interfaceObject& setMaximumLength(Flt target, Flt time = 0); // range 0 -

      // number of simultanious voices to be played
      interfaceObject& setVoices(UInt target, Flt time = 0);

      // restrict played notes to this scale. the player makes a copy of this
      // scale, so alterations you make afterwards are not passed to the player.
      interfaceObject& setScale(MUSIC::scale & scale, Flt time = 0);

    private:
      implementationObject * pimpl;

      friend class PLAYER::implementationObject;
    };

  }
}



#endif  // PLAYERINTERFACE_HPP_INCLUDED

/*
  ==============================================================================

    sound.h
    Created: 24 Mar 2014 3:06:24pm
    Author:  yvan

  ==============================================================================
*/

#ifndef SOUND_H_INCLUDED
#define SOUND_H_INCLUDED

namespace YSE {
  /** Every subSystem consists out of several class which are meant to work together.
  They all have an interface, implementation, manager, message and a message enumeration.

  */
  class sound; // interfaceObject

  namespace SOUND {
    class implementationObject;
    class messageObject;
    class managerObject;
    enum MESSAGE {
      POSITION,
      SPREAD,
      VOLUME_VALUE,
      VOLUME_TIME,
      SPEED,
      SIZE,
      LOOP,
      INTENT,
      OCCLUSION,
      DSP,
      TIME,
      RELATIVE,
      DOPPLER,
      PAN2D,
      FADE_AND_STOP,
      MOVE,
    };
  }

}



#endif  // SOUND_H_INCLUDED

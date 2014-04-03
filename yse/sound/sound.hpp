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

  For consistency they are all base on the same template classes. Because those template
  classes need to know about each other, we create a struct like below to pass it as
  a kind of lookup to very template.
  */
  namespace SOUND {
    class interfaceObject;
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

  // the interface itself gets a more generic name, so that users can just
  // define a 'sound' to get an interface object.
  typedef SOUND::interfaceObject sound;
}



#endif  // SOUND_H_INCLUDED

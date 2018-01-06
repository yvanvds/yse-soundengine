/*
  ==============================================================================

    synth.hpp
    Created: 6 Jul 2014 10:02:25pm
    Author:  yvan

  ==============================================================================
*/

#ifndef SYNTH_HPP_INCLUDED
#define SYNTH_HPP_INCLUDED

namespace YSE {
  /** Every subSystem consists out of several class which are meant to work together.
  They all have an interface, implementation, manager, message and a message enumeration.

  For consistency they are all base on the same template classes. Because those template
  classes need to know about each other, we create a struct like below to pass it as
  a kind of lookup to very template.
  */
  namespace SYNTH {
    class interfaceObject;
    class implementationObject;
    class messageObject;
    class managerObject;
    enum MESSAGE {
      NOTE_ON,
      NOTE_OFF,
      ALL_NOTES_OFF,
      PITCH_WHEEL,
      CONTROLLER,
      AFTERTOUCH,
      SUSTAIN,
      SOSTENUTO,
      SOFTPEDAL,
      CALLBACK,
    };
  }

  // the interface itself gets a more generic name, so that users can just
  // define a 'synth' to get an interface object.
  typedef SYNTH::interfaceObject synth;
}




#endif  // SYNTH_HPP_INCLUDED

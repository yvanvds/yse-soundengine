/*
  ==============================================================================

    synth.hpp
    Forward-declaration hub for the YSE::synth subsystem.

    Mirrors sound/sound.hpp: declares the four-part subsystem split (interface,
    implementation, message, manager) plus the voice base, and defines the
    MESSAGE op-set and the public `YSE::synth` typedef. Implements §2 ("Object
    model") of docs/design/synth_core.md.

  ==============================================================================
*/

#ifndef YSE_SYNTH_SYNTH_HPP
#define YSE_SYNTH_SYNTH_HPP

namespace YSE {
  /** Every subSystem consists out of several classes which are meant to work
      together: an interface, implementation, manager, message and a message
      enumeration. The synth subsystem mirrors the sound subsystem one-for-one.
  */
  namespace SYNTH {
    class interfaceObject;
    class implementationObject;
    class messageObject;
    class managerObject;
    class dspVoice;
    class positionHandler;

    /** The control events the interface can push onto an implementation's
        lock-free inbox. #153 wires up the note ops (NOTE_ON / NOTE_OFF /
        ALL_NOTES_OFF); the pedal / controller / wheel / aftertouch ops are
        declared here as the fixed contract but handled by #154 (keyboard
        state). There is deliberately no CALLBACK op — onNoteEvent is a direct
        atomic function pointer (#154), never a queued message. */
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
      // Per-note positioning (#170). HANDLER_PARAM updates the synth's shared
      // handler-param block; NOTE_POSITION imperatively places sounding notes.
      HANDLER_PARAM,
      NOTE_POSITION,
    };
  } // namespace SYNTH

  /** Users just write ``YSE::synth`` to get an interface object. */
  typedef SYNTH::interfaceObject synth;
} // namespace YSE

#endif // YSE_SYNTH_SYNTH_HPP

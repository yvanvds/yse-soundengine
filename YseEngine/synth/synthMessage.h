/*
  ==============================================================================

    synthMessage.h
    Tagged-union control message from the synth interface to its implementation.

    Implements §6 ("Message op-set") of docs/design/synth_core.md. Unlike the
    old loosely-typed synth message (which aliased a single Flt[3] across every
    op and carried a latent field-aliasing bug), each op reads and writes its
    own named struct in the union, so no field is ever read under the wrong
    name.

  ==============================================================================
*/

#ifndef YSE_SYNTH_SYNTHMESSAGE_H
#define YSE_SYNTH_SYNTHMESSAGE_H

#include "synth.hpp"
#include "../headers/types.hpp"

namespace YSE {
  namespace SYNTH {

    /*
       Message objects carry a control event from a synth interfaceObject to its
       implementationObject across the per-impl lock-free SPSC inbox. They are a
       way to ensure threadsafe and lockfree communication between the two.
    */
    // Named per-op payloads. Each op touches exactly the fields it means — no
    // positional aliasing. Named (rather than anonymous nested) struct types so
    // the tagged union below is standard C++ with no -Wnested-anon-types.
    struct NoteOnData {
      Int channel;
      Int note;
      Flt velocity;
    };
    struct NoteOffData {
      Int channel;
      Int note;
      Flt velocity;
    };
    struct AllOffData {
      Int channel; // 0 = all channels
    };
    struct WheelData {
      Int channel;
      Flt value; // [-1, 1]  (handled by #154)
    };
    struct ControllerData {
      Int channel;
      Int number;
      Flt value; // [0, 1]   (handled by #154)
    };
    struct AftertouchData {
      Int channel;
      Int note; // -1 = channel-wide  (handled by #154)
      Flt value;
    };
    struct PedalData {
      Int channel;
      Bool down; // SUSTAIN / SOSTENUTO / SOFTPEDAL  (handled by #154)
    };
    // Per-note positioning ops (#170). Pos is stored as three loose Flt (not a
    // Pos member) so the union stays trivial — no positional aliasing.
    struct HandlerParamData {
      Int index;
      Flt value;
    };
    struct NotePositionData {
      Int channel;
      Int note;
      Flt x, y, z; // the target position, unpacked
    };

    class messageObject {
    public:
      /** Selects which member of the union below is live. */
      MESSAGE ID;

      union {
        NoteOnData noteOn; // NOTE_ON
        NoteOffData noteOff; // NOTE_OFF
        AllOffData allOff; // ALL_NOTES_OFF
        WheelData wheel; // PITCH_WHEEL
        ControllerData cc; // CONTROLLER
        AftertouchData touch; // AFTERTOUCH
        PedalData pedal; // SUSTAIN / SOSTENUTO / SOFTPEDAL
        HandlerParamData handlerParam; // HANDLER_PARAM
        NotePositionData notePosition; // NOTE_POSITION
      };
    };

  } // namespace SYNTH
} // namespace YSE

#endif // YSE_SYNTH_SYNTHMESSAGE_H

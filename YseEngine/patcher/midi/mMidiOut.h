#pragma once
#include "headers/defines.hpp"
// Patcher midi-out object: the only one of the patcher's MIDI senders that
// needs the RtMidi-backed device backend, because it holds an output port
// rather than just formatting bytes. When YSE_ENABLE_MIDI_DEVICE is OFF the
// underlying YSE::midiOut type doesn't exist, so this file is empty. The extra
// `YSE_WINDOWS` this used to carry was lifted with the rest of the family
// (issue #746) — YSE_ENABLE_MIDI_DEVICE is already exactly the set of
// platforms with a port to open, and it is what the input family uses.
#if YSE_ENABLE_MIDI_DEVICE
#include "../pObject.h"
#include "../../midi/device.hpp"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(mMidiOut, YSE::OBJ::M_OUT)
    _DO_MESSAGES
    _NO_CALCULATE

    _LIST_IN(SetListValue)

  private:
    aInt port;
    YSE::midiOut out;
    bool ready;
  };
}
}
#endif
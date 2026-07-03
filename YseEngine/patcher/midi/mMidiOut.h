#pragma once
#include "headers/defines.hpp"
// Patcher midi-out object: Windows-only AND requires the RtMidi-backed MIDI
// device backend (YSE_ENABLE_MIDI_DEVICE). When the option is OFF the
// underlying YSE::midiOut type doesn't exist, so this file is empty.
#if YSE_WINDOWS && YSE_ENABLE_MIDI_DEVICE
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
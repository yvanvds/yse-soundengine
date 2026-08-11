#include "headers/defines.hpp"
// See the matching guard in mMidiOut.h.
#if YSE_WINDOWS && YSE_ENABLE_MIDI_DEVICE
#include "mMidiOut.h"
#include "../pObjectList.hpp"
#include "../patcherImplementation.h"
#include "pMidiByteList.h"

#include <cstddef>

using namespace YSE::PATCHER;
#define className mMidiOut

CONSTRUCT(), ready(false) {
  ADD_IN_0;
  REG_LIST_IN(SetListValue);

  ADD_PARAM(port);

  ADD_DESCRIPTION(
      "MIDI device output — the end of every chain that sends. A message arrives at the inlet as a "
      "list of bytes and goes straight out of the selected hardware port, whole and in order. Both "
      "of the patcher's spellings of a byte list are read (issue #748): the numeric one, '144 60 "
      "100', which is what '.midiformat', '.sxformat' and '.seq' send and what a list is "
      "everywhere "
      "else in the patcher, and the binary one, whose characters are the bytes themselves, which "
      "is "
      "what the older senders ('.noteon', '.noteoff', '.controlchange', '.polypressure', "
      "'.channelpressure', '.programchange', '.bendout' and the '.x*out' family) build. The two "
      "cannot be confused: a MIDI message begins with a status byte, 128 or above, which is never "
      "a "
      "decimal digit. The message is sent at the length it has rather than padded or cut to three "
      "bytes, so a program change goes out as the two bytes it is and a system-exclusive dump of "
      "any length up to 256 bytes goes out entire; a numeric list carrying a number outside 0-255, "
      "or longer than that, is not a MIDI message and is dropped rather than sent in part. Also "
      "understands 'allnotesoff', 'reset', 'omni on/off', 'poly on/off' and 'local control on/off' "
      "as control messages. The port is opened on the first message rather than at creation, so a "
      "patch loads on a machine whose devices are not the ones it was written on.");
  ADD_CATEGORY(pCategory::MIDI);
  INLET_DOC(0, "midi",
            "One MIDI message to send, as a list of bytes: numeric ('144 60 100', the shape "
            "'.midiformat' and '.sxformat' send) or binary (the shape the older senders build). "
            "Sent at the length it arrives with.",
            "0-255");
  PARAM_DOC("port", "0", "Index of the output MIDI port.", "device-dependent");
}

LIST_IN(SetListValue) {
  if (!ready) {
    out.create(port);
    ready = true;
  }

  // On the stack rather than in a member: this handler runs on whichever thread
  // sent the message, and two of them arriving at once must not share a buffer.
  // A fixed array, so nothing here allocates.
  unsigned char bytes[MIDI_BYTE_LIST_MAX];
  int count = 0;

  switch (ReadMidiByteList(value, bytes, MIDI_BYTE_LIST_MAX, count)) {
  case midiByteList::numeric:
    out.Raw(bytes, static_cast<std::size_t>(count));
    break;
  case midiByteList::characters:
    // The binary spelling: the characters are already the bytes.
    out.Raw(value);
    break;
  case midiByteList::refused:
    break;
  }
}

MESSAGES() {
  if (message == "allnotesoff") {
    out.AllNotesOff();
  } else if (message == "reset") {
    out.Reset();
  } else if (message == "omni on") {
    out.Omni(true);
  } else if (message == "omni off") {
    out.Omni(false);
  } else if (message == "poly on") {
    out.Poly(true);
  } else if (message == "poly off") {
    out.Poly(false);
  } else if (message == "local control on") {
    out.LocalControl(true);
  } else if (message == "local control off") {
    out.LocalControl(false);
  }
}
#endif
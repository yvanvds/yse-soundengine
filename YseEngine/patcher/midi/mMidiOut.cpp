#include "headers/defines.hpp"
// See the matching guard in mMidiOut.h.
#if YSE_ENABLE_MIDI_DEVICE
#include "mMidiOut.h"
#include "../pObjectList.hpp"
#include "../patcherImplementation.h"
#include "midiPortOpener.h"
#include "pMidiByteList.h"
#include "../../midi/midiOutSender.h"
#include "../../midi/midiSynthRouting.hpp"

#include <cstddef>

static_assert(YSE::MIDI::kRawEventMax >= static_cast<std::size_t>(YSE::PATCHER::MIDI_BYTE_LIST_MAX),
              "every byte list .midiout can read must fit the sender's immediate lane whole");

using namespace YSE::PATCHER;
#define className mMidiOut

CONSTRUCT() {
  ADD_IN_0;
  REG_LIST_IN(SetListValue);

  ADD_PARAM(port);

  // Taken for the object's whole life, here rather than on the first message: a
  // slot costs a few words until something is asked of it, and claiming it once
  // means the message handler never has to find one on a path that may be the
  // audio callback. A full table leaves `open` at 0, which the handler accepts
  // and quietly never opens a port for.
  open = MidiPortOpener().Claim();

  ADD_DESCRIPTION("Sends MIDI messages to an output port. Each list on the inlet is one message, "
                  "as byte values or as built by the MIDI sender objects; a few words send common "
                  "channel-mode messages.");
  ADD_CATEGORY(pCategory::MIDI);
  INLET_DOC(0, "midi",
            "One MIDI message: a list of byte values such as 144 60 100, or a message from a "
            "sender object such as .noteon. Messages that arrive before the port is open, "
            "including the first, are dropped.",
            "0-255");
  PARAM_DOC("port", "0", "MIDI output port, counted from 0; .midiinfo lists them.",
            "device-dependent");
}

mMidiOut::~mMidiOut() {
  // Hands the slot back without joining: the #227 epoch reclaimer frees retired
  // patcher objects on the background pool itself, so a destructor that joined
  // its own job could spin on the very worker running it. `Release` waits out an
  // open mid-flight instead, which is bounded by one `openPort` — and it is what
  // guarantees no worker is still reaching into `out` once this returns. See
  // midiPortOpener's class notes.
  MidiPortOpener().Release(open);
}

bool mMidiOut::OpenInFlight() const {
  return MidiPortOpener().Busy(open);
}

bool mMidiOut::EnsurePort() {
  // The common case, once the port has arrived: one acquire load.
  if (ready.load(std::memory_order_acquire)) return true;

  if (MidiPortOpener().Consume(open)) {
    // The acquire inside Consume pairs with the job's release store, so the
    // port `midiOut::create` wrote on the pool is visible from here on. Cached
    // before the latch is released, so a thread that sees `ready` sees it too.
    sendPort.store(out.rawPort(), std::memory_order_release);
    ready.store(true, std::memory_order_release);
    return true;
  }

  // Wait-free: one CAS and one push onto the background pool's lock-free ring.
  // Asking again on a slot that is already busy is one atomic load, which is
  // what makes asking on every message affordable here.
  MidiPortOpener().Request(open, &out, static_cast<unsigned int>(port.load()));
  return false;
}

void mMidiOut::Send(const unsigned char* data, std::size_t length) {
  if (length == 0) return;
  // The open settled without a device — a port index this machine does not
  // have. Nothing to send to, exactly as before.
  RtMidiOut* device = sendPort.load(std::memory_order_acquire);
  if (device == nullptr) return;

  // Issue #949: never RtMidi here. This runs on whichever thread dispatched
  // the message — routinely the audio callback — and a send is a driver call
  // that can block (and, for SysEx on some backends, allocate). One lock-free
  // push onto the sender thread's immediate lane instead; a full lane drops
  // the message, counted rather than logged for the same reason.
  if (!MIDI::OutSender().tryEnqueueNow(device, data, length)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
  }
}

void mMidiOut::SendControl(unsigned char channel, unsigned char controller, unsigned char value) {
  const unsigned char message[3] = {static_cast<unsigned char>(MIDI::MSG_CONTROL_CHANGE + channel),
                                    controller, value};
  Send(message, 3);
}

void mMidiOut::UsePortForTest(RtMidiOut* device) {
  sendPort.store(device, std::memory_order_release);
  ready.store(true, std::memory_order_release);
}

LIST_IN(SetListValue) {
  if (!EnsurePort()) {
    // There is no device to send to yet. Counted rather than logged: this
    // handler runs on whichever thread sent the message, and that may be the
    // audio callback.
    deferred.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  // On the stack rather than in a member: this handler runs on whichever thread
  // sent the message, and two of them arriving at once must not share a buffer.
  // A fixed array, so nothing here allocates.
  unsigned char bytes[MIDI_BYTE_LIST_MAX];
  int count = 0;

  switch (ReadMidiByteList(value, bytes, MIDI_BYTE_LIST_MAX, count)) {
  case midiByteList::numeric:
    Send(bytes, static_cast<std::size_t>(count));
    break;
  case midiByteList::characters:
    // The binary spelling: the characters are already the bytes. Held to the
    // numeric spelling's limit — longer is dropped whole, never sent in part.
    if (value.size() <= MIDI::kRawEventMax) {
      Send(reinterpret_cast<const unsigned char*>(value.data()), value.size());
    }
    break;
  case midiByteList::refused:
    break;
  }
}

MESSAGES() {
  // Through the same gate as the list handler, and not only for symmetry: these
  // reach into `out`, so the acquire load in EnsurePort is what makes the port
  // the background pool opened visible on this thread at all.
  if (!EnsurePort()) {
    deferred.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  // The same bytes YSE::midiOut's control calls put on the wire, but queued
  // through Send rather than sent from here (issue #949).
  if (message == "allnotesoff") {
    for (unsigned char channel = 0; channel < 16; channel++)
      SendControl(channel, MIDI::CC_ALL_NOTES_OFF, 0);
  } else if (message == "reset") {
    for (unsigned char channel = 0; channel < 16; channel++)
      SendControl(channel, MIDI::CC_RESET_ALL_CONTROLLERS, 0);
  } else if (message == "omni on") {
    SendControl(0, MIDI::CC_OMNI_MODE_ON, 0);
  } else if (message == "omni off") {
    SendControl(0, MIDI::CC_OMNI_MODE_OFF, 0);
  } else if (message == "poly on") {
    SendControl(0, MIDI::CC_POLY_MODE_ON, 0);
  } else if (message == "poly off") {
    SendControl(0, MIDI::CC_MONO_MODE_ON, 0);
  } else if (message == "local control on") {
    SendControl(0, MIDI::CC_LOCAL_CONTROL, 127);
  } else if (message == "local control off") {
    SendControl(0, MIDI::CC_LOCAL_CONTROL, 0);
  }
}
#endif

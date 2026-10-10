#include "headers/defines.hpp"
// See the matching guard in mMidiIn.h.
#if YSE_ENABLE_MIDI_DEVICE
#include "mMidiIn.h"
#include "../pObjectList.hpp"
#include "../patcherImplementation.h"

#include <cstddef>

using namespace YSE::PATCHER;

namespace {

  // The wire format, spelled once. Every member of the family reads its own
  // fields straight out of the bytes rather than going through a shared parser:
  // the fields differ per message type, and a three-byte read is cheaper than
  // any abstraction over it would be on the audio thread.
  constexpr unsigned char kStatusMask = 0xF0;
  constexpr unsigned char kChannelMask = 0x0F;
  constexpr unsigned char kDataMask = 0x7F;

  constexpr unsigned char kNoteOff = 0x80;
  constexpr unsigned char kNoteOn = 0x90;
  constexpr unsigned char kPolyPressure = 0xA0;
  constexpr unsigned char kControlChange = 0xB0;
  constexpr unsigned char kProgramChange = 0xC0;
  constexpr unsigned char kChannelPressure = 0xD0;
  constexpr unsigned char kPitchBend = 0xE0;

  // System real time: 0xF8..0xFF, one byte each, and never carrying a channel.
  constexpr unsigned char kFirstRealTime = 0xF8;

  // Whether `event` is a channel-voice message of `status` with at least
  // `needed` bytes. The length check is not paranoia: a chunk of a longer
  // message can land here (see inHub's header) and must not be read as a short
  // one that happens to start with the right nibble.
  bool IsVoice(const YSE::MIDI::inEvent& event, unsigned char status, unsigned char needed) {
    if (event.len < needed) return false;
    return (event.bytes[0] & kStatusMask) == status;
  }

  int Data(const YSE::MIDI::inEvent& event, std::size_t index) {
    return static_cast<int>(event.bytes[index] & kDataMask);
  }

  unsigned char Nibble(const YSE::MIDI::inEvent& event) {
    return static_cast<unsigned char>(event.bytes[0] & kChannelMask);
  }

  constexpr char kPortDoc[] =
      "MIDI input port to listen to, counted from 0; .midiinfo lists them. A port that cannot be "
      "opened leaves the object silent, and the patch still loads.";

  constexpr char kChannelDoc[] =
      "MIDI channel to listen to, 1-16, or 0 for all channels. Any other value means all "
      "channels. The channel outlet stays either way.";

  constexpr char kChannelOutletDoc[] =
      "The channel the message arrived on, 1-16. Sent before the outlets to its left.";

} // namespace

// ─── mMidiInBase ────────────────────────────────────────────────────────────

mMidiInBase::mMidiInBase() : pObject(false) {
  // No inlets at all: this object's input is the wire, not the patch. That is
  // also why it needs WantsBlockPoll() — with no inlet there is nothing for the
  // graph traversal to push it through.
  ADD_PARAM(port);
  port = 0;

  // Registering a parse callback is what makes Parameters::NeedsRebuild() true,
  // and that is load-bearing rather than incidental: a live SetParams must
  // rebuild this object so the replacement's SetParent opens the new port,
  // instead of storing an int into a field nobody re-reads.
  parms.RegisterParse([this]() { ParseParams(); });

  ADD_CATEGORY(pCategory::MIDI);
  PARAM_DOC("port", "0", kPortDoc, "0-7");
}

mMidiInBase::~mMidiInBase() {
  Unsubscribe();
}

unsigned int mMidiInBase::Port() const {
  const Int value = port.load();
  return value > 0 ? static_cast<unsigned int>(value) : 0u;
}

void mMidiInBase::ParseParams() {
  // A negative port index names no device. Clamped rather than refused, so a
  // malformed argument is a patch that listens to port 0 instead of a patch
  // that fails to load.
  if (port.load() < 0) port = 0;
}

void mMidiInBase::Unsubscribe() {
  const YSE::MIDI::inHub::Handle held =
      subscription.exchange(YSE::MIDI::inHub::kNoHandle, std::memory_order_acq_rel);
  if (held == YSE::MIDI::inHub::kNoHandle) return;
  YSE::MIDI::InHub().Unsubscribe(held);
}

void mMidiInBase::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // Whenever the parent changes — including the first time, inside
  // patcherImplementation::CreateObjectUnlocked — re-take the subscription, so
  // the arguments this object was created with decide the port. `.r` re-anchors
  // its bus address at the same moment and for the same reason.
  Unsubscribe();
  // A standalone object (a unit-test rig, or one not yet added to a patcher)
  // deliberately holds no subscription: it would open a device nothing is going
  // to drain, since nothing calls Calculate() on it.
  if (newParent == nullptr) return;
  subscription.store(YSE::MIDI::InHub().Subscribe(Port()), std::memory_order_release);
}

void mMidiInBase::Calculate(YSE::THREAD thread) {
  const YSE::MIDI::inHub::Handle held = subscription.load(std::memory_order_acquire);
  if (held == YSE::MIDI::inHub::kNoHandle) return;

  // Bounded on purpose, and not by the queue's size: the queue is sized to hold
  // a whole SysEx dump (issue #950), and draining one in a single block would
  // be thousands of sends inside one callback. Anything past the bound stays
  // queued for the next block rather than being dropped; the hub's own
  // overflow report covers the case where the producer is genuinely outrunning
  // this drain.
  std::size_t budget = YSE::MIDI::inHub::kDrainPerBlock;
  YSE::MIDI::inEvent event;
  while (budget > 0 && YSE::MIDI::InHub().TryPop(held, event)) {
    budget--;
    Receive(event, thread);
  }
}

// ─── mMidiChannelInBase ─────────────────────────────────────────────────────

mMidiChannelInBase::mMidiChannelInBase() : mMidiInBase() {
  ADD_PARAM(channel);
  channel = 0;
  PARAM_DOC("channel", "0", kChannelDoc, "0-16");
}

void mMidiChannelInBase::ParseParams() {
  mMidiInBase::ParseParams();
  // Outside 1..16 there is no such channel, and reading a stray number as one
  // would silence the object for good. Anything out of range means omni, which
  // is what 0 already means and what an object with no argument does.
  const Int value = channel.load();
  if (value < 1 || value > 16) channel = 0;
}

bool mMidiChannelInBase::Accepts(unsigned char nibble) const {
  const Int filter = channel.load();
  if (filter == 0) return true;
  return ChannelNumber(nibble) == filter;
}

// ─── .midiin — the raw bytes ────────────────────────────────────────────────

mMidiIn::mMidiIn() : mMidiInBase() {
  ADD_OUT_INT;

  ADD_DESCRIPTION("Raw MIDI input. Every byte received on a MIDI input port leaves the outlet as "
                  "an int, in arrival order and uninterpreted.");
  OUTLET_DOC(0, "byte",
             "Each received byte as an int. Status bytes keep their value on the wire (144-159 is "
             "a note-on, 240 starts a system-exclusive dump); data bytes are 0-127.",
             "0-255");
}

void mMidiIn::Receive(const YSE::MIDI::inEvent& event, YSE::THREAD thread) {
  for (std::size_t i = 0; i < event.len; i++) {
    outputs[0].SendInt(static_cast<int>(event.bytes[i]), thread);
  }
}

// ─── .rtin — the system real-time messages ──────────────────────────────────

mRtIn::mRtIn() : mMidiInBase() {
  ADD_OUT_INT;

  // Active sensing (254) never arrives: the device backend filters it, since a
  // device that sends it sends 300 a minute forever.
  ADD_DESCRIPTION("MIDI clock input. Reports the system real-time messages a sequencer or drum "
                  "machine sends: timing clock, start, continue, stop and reset.");
  OUTLET_DOC(0, "status",
             "The message: 248 timing clock (24 per quarter note), 250 start, 251 continue, 252 "
             "stop, 255 reset.",
             "248-255");
}

void mRtIn::Receive(const YSE::MIDI::inEvent& event, YSE::THREAD thread) {
  // A real-time message is exactly one byte. The length test is what keeps a
  // chunk of some longer message that happens to begin with 0xF8 out.
  if (event.len != 1) return;
  if (event.bytes[0] < kFirstRealTime) return;
  outputs[0].SendInt(static_cast<int>(event.bytes[0]), thread);
}

// ─── .notein ────────────────────────────────────────────────────────────────

mNoteIn::mNoteIn() : mMidiChannelInBase() {
  ADD_OUT_INT; // pitch
  ADD_OUT_INT; // velocity
  ADD_OUT_INT; // channel

  ADD_DESCRIPTION("MIDI note input. Every note-on and note-off received on a MIDI input port "
                  "leaves as pitch, velocity and channel; a release reports velocity 0.");
  OUTLET_DOC(0, "pitch", "The note number; middle C is 60. Sent last.", "0-127");
  OUTLET_DOC(1, "velocity",
             "How hard the key was struck, or 0 for a release: a note-off message and a note-on "
             "with velocity 0 both report 0.",
             "0-127");
  OUTLET_DOC(2, "channel", kChannelOutletDoc, "1-16");
}

void mNoteIn::Receive(const YSE::MIDI::inEvent& event, YSE::THREAD thread) {
  const bool on = IsVoice(event, kNoteOn, 3);
  const bool off = IsVoice(event, kNoteOff, 3);
  if (!on && !off) return;

  const unsigned char nibble = Nibble(event);
  if (!Accepts(nibble)) return;

  // A note-off reports velocity 0 rather than the release velocity the message
  // carries. That is Max's rule, and it is what makes the two spellings of a
  // release — 0x80, and 0x90 with velocity 0 — indistinguishable downstream.
  const int velocity = on ? Data(event, 2) : 0;

  outputs[2].SendInt(ChannelNumber(nibble), thread);
  outputs[1].SendInt(velocity, thread);
  outputs[0].SendInt(Data(event, 1), thread);
}

// ─── .ctlin ─────────────────────────────────────────────────────────────────

mCtlIn::mCtlIn() : mMidiChannelInBase() {
  ADD_OUT_INT; // value
  ADD_OUT_INT; // controller number
  ADD_OUT_INT; // channel

  ADD_PARAM(controller);
  controller = -1;

  ADD_DESCRIPTION("MIDI control-change input. Reports the value, controller number and channel of "
                  "every control change received on a MIDI input port: knobs, faders, wheels and "
                  "pedals.");
  OUTLET_DOC(0, "value", "The controller's value. Sent last.", "0-127");
  OUTLET_DOC(1, "controller",
             "The controller number: 1 is the modulation wheel, 7 volume, 64 the sustain pedal.",
             "0-127");
  OUTLET_DOC(2, "channel", kChannelOutletDoc, "1-16");
  PARAM_DOC("controller", "-1",
            "Controller number to listen to, 0-127, or -1 for all controllers. Any other value "
            "means all controllers.",
            "-1 to 127");
}

void mCtlIn::ParseParams() {
  mMidiChannelInBase::ParseParams();
  const Int value = controller.load();
  if (value < 0 || value > 127) controller = -1;
}

void mCtlIn::Receive(const YSE::MIDI::inEvent& event, YSE::THREAD thread) {
  if (!IsVoice(event, kControlChange, 3)) return;

  const unsigned char nibble = Nibble(event);
  if (!Accepts(nibble)) return;

  const int number = Data(event, 1);
  const Int filter = controller.load();
  if (filter >= 0 && filter != number) return;

  outputs[2].SendInt(ChannelNumber(nibble), thread);
  outputs[1].SendInt(number, thread);
  outputs[0].SendInt(Data(event, 2), thread);
}

// ─── .bendin ────────────────────────────────────────────────────────────────

mBendIn::mBendIn() : mMidiChannelInBase() {
  ADD_OUT_INT; // bend
  ADD_OUT_INT; // channel

  ADD_DESCRIPTION("MIDI pitch-bend input at 7-bit resolution: reports the pitch wheel as 0-127 "
                  "with 64 at rest. .xbendin reports the full 14 bits.");
  OUTLET_DOC(0, "bend", "The wheel position: 0 fully down, 64 at rest, 127 fully up.", "0-127");
  OUTLET_DOC(1, "channel", kChannelOutletDoc, "1-16");
}

void mBendIn::Receive(const YSE::MIDI::inEvent& event, YSE::THREAD thread) {
  if (!IsVoice(event, kPitchBend, 3)) return;

  const unsigned char nibble = Nibble(event);
  if (!Accepts(nibble)) return;

  // Byte 2 is the MSB — the wire sends the fine byte first. Reporting the
  // coarse one alone is what makes this the 7-bit object.
  outputs[1].SendInt(ChannelNumber(nibble), thread);
  outputs[0].SendInt(Data(event, 2), thread);
}

// ─── .pgmin ─────────────────────────────────────────────────────────────────

mPgmIn::mPgmIn() : mMidiChannelInBase() {
  ADD_OUT_INT; // program
  ADD_OUT_INT; // channel

  ADD_DESCRIPTION("MIDI program-change input. Reports each program change received on a MIDI "
                  "input port as 1-128, the numbering most devices display.");
  OUTLET_DOC(0, "program", "The program number: the value on the wire plus one.", "1-128");
  OUTLET_DOC(1, "channel", kChannelOutletDoc, "1-16");
}

void mPgmIn::Receive(const YSE::MIDI::inEvent& event, YSE::THREAD thread) {
  if (!IsVoice(event, kProgramChange, 2)) return;

  const unsigned char nibble = Nibble(event);
  if (!Accepts(nibble)) return;

  outputs[1].SendInt(ChannelNumber(nibble), thread);
  outputs[0].SendInt(Data(event, 1) + 1, thread);
}

// ─── .touchin ───────────────────────────────────────────────────────────────

mTouchIn::mTouchIn() : mMidiChannelInBase() {
  ADD_OUT_INT; // pressure
  ADD_OUT_INT; // channel

  ADD_DESCRIPTION("MIDI channel-aftertouch input. Reports the pressure on the keys after they are "
                  "down, as one value for the whole channel.");
  OUTLET_DOC(0, "pressure",
             "Channel pressure: one value however many keys are held. Per-key pressure is "
             ".polyin's.",
             "0-127");
  OUTLET_DOC(1, "channel", kChannelOutletDoc, "1-16");
}

void mTouchIn::Receive(const YSE::MIDI::inEvent& event, YSE::THREAD thread) {
  if (!IsVoice(event, kChannelPressure, 2)) return;

  const unsigned char nibble = Nibble(event);
  if (!Accepts(nibble)) return;

  outputs[1].SendInt(ChannelNumber(nibble), thread);
  outputs[0].SendInt(Data(event, 1), thread);
}

// ─── .polyin ────────────────────────────────────────────────────────────────

mPolyIn::mPolyIn() : mMidiChannelInBase() {
  ADD_OUT_INT; // pitch
  ADD_OUT_INT; // pressure
  ADD_OUT_INT; // channel

  ADD_DESCRIPTION("MIDI polyphonic key-pressure input. Reports the pressure on each held key "
                  "separately, as pitch, pressure and channel.");
  OUTLET_DOC(0, "pitch",
             "The key the pressure belongs to, numbered as .notein numbers it. Sent last.",
             "0-127");
  OUTLET_DOC(1, "pressure", "The pressure on that key.", "0-127");
  OUTLET_DOC(2, "channel", kChannelOutletDoc, "1-16");
}

void mPolyIn::Receive(const YSE::MIDI::inEvent& event, YSE::THREAD thread) {
  if (!IsVoice(event, kPolyPressure, 3)) return;

  const unsigned char nibble = Nibble(event);
  if (!Accepts(nibble)) return;

  outputs[2].SendInt(ChannelNumber(nibble), thread);
  outputs[1].SendInt(Data(event, 2), thread);
  outputs[0].SendInt(Data(event, 1), thread);
}

#endif

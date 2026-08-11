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
      "Index of the MIDI input device to listen to, as numbered by "
      "YSE::system::getMidiInDeviceName. The port is opened the first time any patcher object asks "
      "for it and closed when the last one goes, so several objects on one keyboard share a single "
      "device and each still gets a queue of its own. A port that will not open — no hardware, or "
      "a "
      "backend that allows one client per port and a host that already holds it — leaves an object "
      "that is perfectly valid and simply never receives anything, which is what lets a patch load "
      "the same way on a machine with nothing plugged in. Editing this argument on a live object "
      "rebuilds the object rather than storing into it: the subscription is taken when the object "
      "joins its patcher, and a field written on the audio thread cannot move a device port.";

  constexpr char kChannelDoc[] =
      "MIDI channel to accept, 1-16, or 0 for every channel — Max's omni, and the default. Unlike "
      "Max, giving a channel here does not remove the channel outlet: an object whose outlet count "
      "depends on its arguments has no stable outlet numbering, so adding an argument would "
      "silently re-point every cord leaving the box and a saved patch's connection indices would "
      "mean two different things. The outlet stays and carries the filtered channel back, which "
      "costs a patch nothing.";

  constexpr char kChannelOutletDoc[] =
      "The MIDI channel the message arrived on, 1-16 — the numbering Max uses, the numbering the "
      "sending objects use, and the numbering printed on the hardware, rather than the 0-15 nibble "
      "on the wire. Sent first, before the outlets to its left, so anything the leftmost outlet "
      "triggers downstream already knows which channel it is looking at.";

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

  // Bounded on purpose: at most one queue's worth per block, so a flood on the
  // wire — a controller sweeping every CC, a dump — cannot make the audio
  // callback run long. Anything past the bound stays queued for the next block
  // rather than being dropped; the hub's own overflow report covers the case
  // where the producer is genuinely outrunning this drain.
  std::size_t budget = YSE::MIDI::inHub::kQueueCapacity + 1;
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

  ADD_DESCRIPTION(
      "Raw MIDI input — Max's 'midiin' (issue #529). Every byte received on the port leaves the "
      "outlet as an int, in the order it arrived and with nothing interpreted: a note-on is three "
      "ints, a SysEx dump is however many the device sent. This is the one member of the input "
      "family that does not decode, and it is the one to reach for when a patch needs the whole "
      "protocol rather than the common cases — SysEx, song position, manufacturer-specific "
      "traffic, "
      "or anything a '.notein' / '.ctlin' pair would filter out. It is also the object that feeds "
      "a "
      "'.seq' recording, whose stored format is the raw byte stream. Bytes are drained from a "
      "bounded lock-free queue once per audio block: MIDI arrives on the device backend's own "
      "thread and this object hands it to the patch on the audio thread without allocating, "
      "locking "
      "or blocking on either side. Status bytes are reported as they are on the wire, 0-255, so a "
      "note-on on channel 1 is 144 rather than 'channel 1' — the decoding objects are where "
      "channels become 1-16.");
  OUTLET_DOC(0, "byte",
             "Every byte received on the port, one int per byte, in arrival order. Status bytes "
             "keep their wire value (144-159 note-on, 176-191 control change, 240 SysEx start, and "
             "so on) and data bytes are 0-127; nothing is filtered, split or reordered. A message "
             "longer than the transport's event size arrives as consecutive chunks, which changes "
             "nothing here, the bytes being sent one at a time either way.",
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

  ADD_DESCRIPTION(
      "MIDI system real-time input — Max's 'rtin' (issue #529). Reports the one-byte messages a "
      "master clock sends to keep everything on it in step: 248 timing clock (24 per quarter "
      "note), "
      "250 start, 251 continue, 252 stop and 255 system reset. This is how a patch follows an "
      "external sequencer or drum machine rather than leading it — count the clocks with a "
      "'.counter' for a tempo grid, and use start / stop to drive a '.transport'. Real-time bytes "
      "are the one part of MIDI that may appear *inside* another message on the wire; the device "
      "backend delivers them as separate one-byte messages, so a clock arriving mid-SysEx reaches "
      "this outlet whole and does not disturb a '.midiin' reading the dump. Active sensing (254) "
      "is "
      "deliberately absent: the backend filters it, because a device that sends it sends 300 a "
      "minute forever and no patch wants that in its queue. Drained once per audio block from a "
      "bounded lock-free queue, so nothing on the path allocates, locks or blocks.");
  OUTLET_DOC(0, "status",
             "The real-time status byte: 248 timing clock, 250 start, 251 continue, 252 stop, 255 "
             "system reset. Nothing else is ever sent here — a channel-voice message goes to the "
             "decoding objects and the raw stream to '.midiin'.",
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

  ADD_DESCRIPTION(
      "MIDI note input — Max's 'notein' (issue #529), and the object that makes a patch playable "
      "from a keyboard at all. Every note-on and note-off received on the port leaves as pitch, "
      "velocity and channel. A note-off is reported the way Max reports it, as the same pitch with "
      "velocity 0 rather than on a separate outlet, so the common case is one comparison "
      "downstream: '.sel 0' on the velocity, or a '.togedge' if what the patch wants is the gate "
      "rather than the numbers. That also folds the two spellings of a note-off — a real note-off "
      "message and a note-on with velocity 0, both of which hardware sends — into one shape, which "
      "is exactly the ambiguity a patch should not have to handle itself. The three outlets fire "
      "right to left, channel first, so whatever the pitch triggers downstream already sees the "
      "velocity and channel that came with it. A 'channel' argument filters; unlike Max it does "
      "not "
      "remove the channel outlet, so an object's shape never depends on its arguments. Events "
      "cross "
      "from the device backend's thread on a bounded lock-free queue and are drained once per "
      "audio "
      "block, so nothing allocates, locks or blocks on either side.");
  OUTLET_DOC(
      0, "pitch",
      "The note number, 0-127, sent last of the three so the velocity and channel that "
      "belong to it are already out when it triggers anything. Middle C is 60; '.mtof' turns "
      "it into a frequency.",
      "0-127");
  OUTLET_DOC(
      1, "velocity",
      "How hard the key was struck, 0-127, and 0 for a release — Max's convention, and the "
      "one that folds a note-off message and a note-on with velocity 0 into a single shape a "
      "patch can test once.",
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

  ADD_DESCRIPTION(
      "MIDI control-change input — Max's 'ctlin' (issue #529). Every control-change message "
      "received on the port leaves as value, controller number and channel: this is the object a "
      "patch reads knobs, faders, the modulation wheel, the sustain pedal and the expression pedal "
      "with. The three outlets fire right to left, so whatever the value triggers downstream "
      "already knows which controller and channel it came from — which is what lets one '.ctlin' "
      "feed a '.route' or a '.sel' over the controller-number outlet and fan a whole control "
      "surface out of a single box. A 'channel' argument filters by channel and a second argument "
      "filters by controller number, whose default is -1 rather than 0 because controller 0 is "
      "Bank "
      "Select MSB, a real controller a patch may want on its own. Unlike Max, neither filter "
      "removes an outlet: an object's shape must not depend on its arguments. Events cross from "
      "the "
      "device backend's thread on a bounded lock-free queue and are drained once per audio block, "
      "so nothing allocates, locks or blocks on either side.");
  OUTLET_DOC(
      0, "value",
      "The controller's new value, 0-127, sent last of the three so the controller number "
      "and channel that identify it are already out when it triggers anything. A switch-type "
      "controller such as the sustain pedal sends 0 and 127 rather than a sweep.",
      "0-127");
  OUTLET_DOC(1, "controller",
             "Which controller moved, 0-127: 1 modulation wheel, 7 channel volume, 11 expression, "
             "64 sustain pedal, and so on. Constant when the object was given a controller "
             "argument, which is the whole reason the outlet is kept in that case — a patch's "
             "wiring should not change shape with its arguments.",
             "0-127");
  OUTLET_DOC(2, "channel", kChannelOutletDoc, "1-16");
  PARAM_DOC(
      "controller", "-1",
      "Controller number to accept, 0-127, or -1 for every controller — the default. Not 0 "
      "for 'any': controller 0 is Bank Select MSB, a controller a patch may legitimately want "
      "on its own, so it could not double as the wildcard. Anything outside 0-127 is read as "
      "-1, so a stray argument means 'every controller' rather than silencing the object "
      "outright.",
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

  ADD_DESCRIPTION(
      "MIDI pitch-bend input — Max's 'bendin' (issue #529). Reports the pitch wheel's position as "
      "0-127 with 64 at rest, which is Max's 7-bit reading of a message that is 14 bits on the "
      "wire: only the most significant byte is used, and that is the reference behaviour rather "
      "than a shortcut. The full resolution is what '.xbendin' is for (issue #533), and the two "
      "exist separately for a reason worth knowing — 7 bits over a two-semitone bend range is "
      "about "
      "three cents a step, which is audible as a staircase on a slow bend, so a patch that bends "
      "expressively wants the extended object while one that just needs a modulation source does "
      "not. The wheel's rest position is 64 here, not 0, so a patch usually subtracts before "
      "scaling. Both outlets fire right to left, channel first. A 'channel' argument filters and, "
      "unlike Max, leaves the channel outlet in place. Events cross from the device backend's "
      "thread on a bounded lock-free queue and are drained once per audio block, so nothing "
      "allocates, locks or blocks on either side.");
  OUTLET_DOC(0, "bend",
             "The wheel position, 0-127, centred at 64: 0 is fully down, 127 fully up. This is the "
             "high byte of the 14-bit message, Max's reading; '.xbendin' reports all 14 bits.",
             "0-127");
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

  ADD_DESCRIPTION(
      "MIDI program-change input — Max's 'pgmin' (issue #529). Reports patch changes as 1-128, "
      "which is Max's numbering and one more than the 0-127 the wire carries: hardware front "
      "panels "
      "and patch sheets count from 1, and the object that *sends* program changes takes the same "
      "numbering, so a round trip through a patch gives back the number the user saw. This is how "
      "a "
      "patch follows a hardware program change — recalling a preset, switching a sampler's kit, or "
      "arming a whole scene from one button on a keyboard. Both outlets fire right to left, "
      "channel "
      "first. A 'channel' argument filters and, unlike Max, leaves the channel outlet in place. "
      "Events cross from the device backend's thread on a bounded lock-free queue and are drained "
      "once per audio block, so nothing allocates, locks or blocks on either side.");
  OUTLET_DOC(0, "program",
             "The program number, 1-128 — the wire's 0-127 plus one, which is what the hardware's "
             "own display shows and what the sending objects take.",
             "1-128");
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

  ADD_DESCRIPTION(
      "MIDI channel-aftertouch input — Max's 'touchin' (issue #529). Reports the pressure applied "
      "after a key is already down, as one value for the whole channel: a keyboard with channel "
      "aftertouch sends the hardest-pressed key's reading, not one per note, which is what "
      "separates this from '.polyin'. It is the cheapest expressive input a patch can have — a "
      "vibrato depth, a filter opening or a swell that the player controls without moving a hand "
      "off the keys — and the one most keyboards actually implement. Both outlets fire right to "
      "left, channel first. A 'channel' argument filters and, unlike Max, leaves the channel "
      "outlet "
      "in place. Events cross from the device backend's thread on a bounded lock-free queue and "
      "are "
      "drained once per audio block, so nothing allocates, locks or blocks on either side.");
  OUTLET_DOC(0, "pressure",
             "Channel aftertouch, 0-127: one value for the whole channel, however many keys are "
             "held. Per-note pressure is '.polyin'.",
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

  ADD_DESCRIPTION(
      "MIDI polyphonic key-pressure input — Max's 'polyin' (issue #529). The per-note counterpart "
      "of '.touchin': every held key reports its own pressure, so a chord can be shaped one note "
      "at "
      "a time rather than as a whole. Far fewer keyboards send it than send channel aftertouch, "
      "which is why the two are separate objects rather than one — a patch built on '.polyin' "
      "silently does nothing on hardware that has only the channel kind. The three outlets fire "
      "right to left, so whatever the pitch triggers downstream already sees the pressure and "
      "channel that came with it; pairing the pitch with the one a '.notein' reported is how a "
      "patch routes the value to the right voice. A 'channel' argument filters and, unlike Max, "
      "leaves the channel outlet in place. Events cross from the device backend's thread on a "
      "bounded lock-free queue and are drained once per audio block, so nothing allocates, locks "
      "or "
      "blocks on either side.");
  OUTLET_DOC(
      0, "pitch",
      "Which held key the pressure belongs to, 0-127 — the same numbering '.notein' reports, "
      "which is how a patch matches the value to a sounding voice. Sent last of the three.",
      "0-127");
  OUTLET_DOC(1, "pressure", "Pressure on that one key, 0-127.", "0-127");
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

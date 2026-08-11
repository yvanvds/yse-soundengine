#include "headers/defines.hpp"
// See the matching guard in mMidiRpnIn.h.
#if YSE_ENABLE_MIDI_DEVICE
#include "mMidiRpnIn.h"
#include "../pObjectList.hpp"

#include <cstddef>

using namespace YSE::PATCHER;

namespace {

  // The wire format again, spelled locally as mMidiIn.cpp and mMidiXIn.cpp both
  // do. Sharing constants that have not moved since 1983 across files with
  // different platform guards would cost more than it saves.
  constexpr unsigned char kStatusMask = 0xF0;
  constexpr unsigned char kChannelMask = 0x0F;
  constexpr unsigned char kDataMask = 0x7F;

  constexpr unsigned char kControlChange = 0xB0;

  // The four controllers that select a parameter number, and the two that write
  // a value into whichever one is selected.
  constexpr int kNrpnSelectLsb = 98;
  constexpr int kNrpnSelectMsb = 99;
  constexpr int kRpnSelectLsb = 100;
  constexpr int kRpnSelectMsb = 101;
  constexpr int kDataEntryMsb = 6;
  constexpr int kDataEntryLsb = 38;

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

  constexpr char kChannelOutletDoc[] =
      "The MIDI channel the sequence arrived on, 1-16 — the numbering Max uses, the numbering the "
      "sending objects use, and the numbering printed on the hardware, rather than the 0-15 nibble "
      "on the wire. Sent first, before the outlets to its left, so anything the leftmost outlet "
      "triggers downstream already knows which channel it is looking at.";

  constexpr char kValueOutletDoc[] =
      "The value written, 0-16383, sent last of the three so the parameter number and channel that "
      "identify it are already out when it triggers anything. A device that sends only the coarse "
      "byte covers the range in steps of 128.";

} // namespace

// ─── the shared body ────────────────────────────────────────────────────────

mRpnInBase::mRpnInBase(bool registeredKind) : mMidiChannelInBase(), registered(registeredKind) {
  ADD_OUT_INT; // value
  ADD_OUT_INT; // parameter number
  ADD_OUT_INT; // channel

  OUTLET_DOC(0, "value", kValueOutletDoc, "0-16383");
  OUTLET_DOC(2, "channel", kChannelOutletDoc, "1-16");
}

int mRpnInBase::Selected(int channel) const {
  if (channel < 1 || channel > CHANNELS) return -1;
  const ChannelState& s = state[channel - 1];
  const Selection wanted = registered ? Selection::REGISTERED : Selection::NON_REGISTERED;
  if (s.selection != wanted) return -1;
  return (static_cast<int>(s.paramMsb) << 7) | static_cast<int>(s.paramLsb);
}

void mRpnInBase::Select(ChannelState& s, bool isRegistered, bool msb, int byte) {
  if (msb) {
    s.paramMsb = static_cast<unsigned char>(byte);
  } else {
    s.paramLsb = static_cast<unsigned char>(byte);
  }
  s.selection = isRegistered ? Selection::REGISTERED : Selection::NON_REGISTERED;
  // A new selection means the stored coarse value byte belongs to a parameter
  // that is no longer the current one.
  s.dataMsb = 0;

  // RPN Null: registered parameter 16383 is the specification's "nothing is
  // selected", sent after a write so a stray Data Entry cannot land somewhere
  // unintended. Not applied to non-registered numbers — the Null is defined for
  // registered ones only, and 16383 is a legal NRPN address.
  if (isRegistered && s.paramMsb == 0x7F && s.paramLsb == 0x7F) {
    s.selection = Selection::NONE;
  }
}

void mRpnInBase::Emit(const ChannelState& s, unsigned char nibble, int value, YSE::THREAD thread) {
  outputs[2].SendInt(ChannelNumber(nibble), thread);
  outputs[1].SendInt((static_cast<int>(s.paramMsb) << 7) | static_cast<int>(s.paramLsb), thread);
  outputs[0].SendInt(value, thread);
}

void mRpnInBase::Receive(const YSE::MIDI::inEvent& event, YSE::THREAD thread) {
  if (!IsVoice(event, kControlChange, 3)) return;

  const unsigned char nibble = Nibble(event);
  if (!Accepts(nibble)) return;

  const int controller = Data(event, 1);
  const int byte = Data(event, 2);
  ChannelState& s = state[nibble];

  switch (controller) {
  case kRpnSelectMsb:
    Select(s, true, true, byte);
    return;
  case kRpnSelectLsb:
    Select(s, true, false, byte);
    return;
  case kNrpnSelectMsb:
    Select(s, false, true, byte);
    return;
  case kNrpnSelectLsb:
    Select(s, false, false, byte);
    return;
  default:
    break;
  }

  // Everything else is either a Data Entry byte or a controller this object has
  // no business with. Both selecting pairs write into the same register, so a
  // value is this object's only when the last selection was of its kind — which
  // is what keeps an interleaved RPN/NRPN stream coming out of the right box.
  const Selection wanted = registered ? Selection::REGISTERED : Selection::NON_REGISTERED;
  if (s.selection != wanted) return;

  if (controller == kDataEntryMsb) {
    // The coarse byte emits at once with the fine byte read as 0, rather than
    // waiting for a partner that most devices never send. `.xctlin`'s rule.
    s.dataMsb = static_cast<unsigned char>(byte);
    Emit(s, nibble, byte << 7, thread);
    return;
  }

  if (controller == kDataEntryLsb) {
    Emit(s, nibble, (static_cast<int>(s.dataMsb) << 7) | byte, thread);
  }
}

// ─── .rpnin ─────────────────────────────────────────────────────────────────

mRpnIn::mRpnIn() : mRpnInBase(true) {
  ADD_DESCRIPTION(
      "Registered parameter number input — Max's 'rpnin' (issue #534). MIDI has 128 controller "
      "numbers and a synthesiser has far more than 128 parameters; registered and non-registered "
      "parameter numbers are the escape hatch, and this is the registered half. A write is not one "
      "message but four control changes — controllers 101 and 100 select a 14-bit parameter "
      "number, "
      "then controllers 6 and 38 write a 14-bit value into whatever is selected — so this object "
      "is "
      "a small state machine rather than a decoder: nothing in the value bytes says which "
      "parameter "
      "they belong to, and remembering that is the whole job. The selection is tracked per "
      "channel, "
      "so two devices on one cable cannot cross-contaminate each other. Registered numbers are the "
      "specification's own and mean the same thing on every device that implements them: 0 "
      "pitch-bend sensitivity, 1 fine tuning, 2 coarse tuning, 3 tuning program select, 4 tuning "
      "bank select. Manufacturer-specific numbers are '.nrpnin''s, and because both pairs move the "
      "same selection register, an interleaved stream comes out of the right box — this object "
      "stays quiet while a non-registered parameter is selected. The RPN Null (parameter 16383) "
      "deselects, as the specification intends, so a Data Entry that follows an abandoned sequence "
      "is reported by nobody. A value is emitted on both halves, not only on the fine one: the "
      "coarse byte emits at once with the fine byte read as 0, which is what keeps the many "
      "devices "
      "that send only the coarse byte from leaving the object silent. The three outlets fire right "
      "to left. A 'channel' argument filters and, unlike Max, leaves the channel outlet in place. "
      "Events cross from the device backend's thread on a bounded lock-free queue and are drained "
      "once per audio block, so nothing allocates, locks or blocks on either side.");
  OUTLET_DOC(1, "parameter",
             "Which registered parameter was written, 0-16383: 0 pitch-bend sensitivity, 1 fine "
             "tuning, 2 coarse tuning, 3 tuning program select, 4 tuning bank select. 16383 is "
             "never reported — that is the Null, which deselects rather than selecting.",
             "0-16382");
}

// ─── .nrpnin ────────────────────────────────────────────────────────────────

mNrpnIn::mNrpnIn() : mRpnInBase(false) {
  ADD_DESCRIPTION(
      "Non-registered parameter number input — Max's 'nrpnin' (issue #534). The same mechanism "
      "'.rpnin' reports, with controllers 99 and 98 selecting the 14-bit parameter number instead "
      "of 101 and 100, and controllers 6 and 38 writing the 14-bit value as before. The difference "
      "is what the number means: a registered number is the MIDI specification's and means the "
      "same "
      "everywhere, while a non-registered one means whatever the sending device's documentation "
      "says. That is where a hardware synthesiser's filter cutoff, envelope times and oscillator "
      "settings actually live, so this is the object that lets a patch follow a synth's own front "
      "panel. A write is four control changes rather than one message and the selection is tracked "
      "per channel; both selecting pairs move the same register, so this object stays quiet while "
      "a "
      "registered parameter is selected and an interleaved stream comes out of the right box. "
      "Parameter 16383 is reported here — the Null is defined for registered numbers only, and "
      "stealing a legal non-registered address would silence a device that used it. A value is "
      "emitted on both halves, the coarse byte at once with the fine byte read as 0, so a device "
      "that sends only the coarse byte is still heard. The three outlets fire right to left. A "
      "'channel' argument filters and, unlike Max, leaves the channel outlet in place. Events "
      "cross "
      "from the device backend's thread on a bounded lock-free queue and are drained once per "
      "audio "
      "block, so nothing allocates, locks or blocks on either side.");
  OUTLET_DOC(1, "parameter",
             "Which non-registered parameter was written, 0-16383. What each number means is the "
             "sending device's business — read it off the manufacturer's MIDI implementation "
             "chart.",
             "0-16383");
}

#endif

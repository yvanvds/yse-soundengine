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
      "The channel the value arrived on, 1-16. Sent before the outlets to its left.";

  constexpr char kValueOutletDoc[] =
      "The value written. Sent last. A device that sends only the coarse byte moves in steps of "
      "128.";

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
  ADD_DESCRIPTION("Registered parameter (RPN) input. Follows the control changes that select and "
                  "write a parameter, and reports each value written with its parameter number and "
                  "channel.");
  OUTLET_DOC(1, "parameter",
             "The registered parameter written: 0 pitch-bend range, 1 fine tuning, 2 coarse "
             "tuning, 3 tuning program, 4 tuning bank.",
             "0-16382");
}

// ─── .nrpnin ────────────────────────────────────────────────────────────────

mNrpnIn::mNrpnIn() : mRpnInBase(false) {
  ADD_DESCRIPTION("Non-registered parameter (NRPN) input. Follows the control changes that select "
                  "and write a device's own parameter, and reports each value written with its "
                  "parameter number and channel.");
  OUTLET_DOC(1, "parameter",
             "The non-registered parameter written. What each number means is in the sending "
             "device's documentation.",
             "0-16383");
}

#endif

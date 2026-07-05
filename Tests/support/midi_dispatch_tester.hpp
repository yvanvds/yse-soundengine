#pragma once
// Test-only access to YSE::midiIn's private dispatch() path.
//
// RtMidi's openVirtualPort() is unavailable on Windows, so tests cannot feed a
// real device. midiIn friend-declares this struct (device.hpp) so tests can
// drive dispatch() directly with synthetic MIDI bytes, deterministically on
// every platform. Shared by test_midi_in.cpp (parser fan-out) and the
// device -> synth routing tests so the friend struct has a single definition.

#include "headers/defines.hpp"
#if YSE_ENABLE_MIDI_DEVICE

#include <cstddef>
#include "midi/device.hpp"

struct MidiInDispatchTester {
  static void dispatch(YSE::midiIn& m, double ts, const unsigned char* bytes, std::size_t len) {
    m.dispatch(ts, bytes, len);
  }
};

#endif // YSE_ENABLE_MIDI_DEVICE

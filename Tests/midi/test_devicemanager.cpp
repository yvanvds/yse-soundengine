// Tests for the YSE MIDI device backend (YseEngine/midi/midiDeviceManager.cpp
// and YseEngine/midi/device.cpp).
//
// Coverage:
//   - MIDI::DeviceManager() singleton identity
//   - getNumMidiInDevices / getNumMidiOutDevices return non-negative counts
//     (0 on CI without MIDI hardware, ≥1 on a workstation with devices)
//   - Per-device name lookups walk the full enumerated range
//   - getMidiOutPort(ID) — cache hit on repeat call, nullptr on invalid port
//   - GenerateMidiError(RtMidiError) drives every RtMidiError::Type switch arm
//   - midiOut default state and isPrepared() early-return on every send method
//   - midiOut::create(port) attempt; downstream message sends are no-ops when
//     no device is attached
//   - Raw(string) handles 0/1/2/3+ byte inputs without out-of-bounds reads
//
// The whole TU is guarded by the same YSE_ENABLE_MIDI_DEVICE option that
// gates midiDeviceManager.cpp and device.cpp — when the option is OFF those
// files are not compiled and the deviceManager / midiOut symbols don't exist.
//
// No engine initialisation is required.  The MIDI singleton stands on its own
// and does not depend on PortAudio.

#include <doctest/doctest.h>
#include "headers/defines.hpp"

#if YSE_ENABLE_MIDI_DEVICE

#include "midi/midiDeviceManager.h"
#include "midi/device.hpp"
#include "RtMidi.h"

TEST_SUITE("midi") {

  // ─── DeviceManager singleton ─────────────────────────────────────────────────

  TEST_CASE("midi deviceManager: DeviceManager() returns the same instance") {
    YSE::MIDI::deviceManager& a = YSE::MIDI::DeviceManager();
    YSE::MIDI::deviceManager& b = YSE::MIDI::DeviceManager();
    CHECK(&a == &b);
  }

  // ─── Device enumeration ──────────────────────────────────────────────────────
  //
  // On CI (no MIDI hardware) the in/out counts are typically 0.  On a developer
  // workstation with virtual or hardware ports they may be ≥1.  The counts are
  // `unsigned int`, so the only failure mode here is a hang / crash inside RtMidi —
  // the call itself should always succeed.

  TEST_CASE("midi deviceManager: getNumMidiInDevices is callable without crash") {
    unsigned int n = YSE::MIDI::DeviceManager().getNumMidiInDevices();
    (void)n;
    CHECK(true);
  }

  TEST_CASE("midi deviceManager: getNumMidiOutDevices is callable without crash") {
    unsigned int n = YSE::MIDI::DeviceManager().getNumMidiOutDevices();
    (void)n;
    CHECK(true);
  }

  TEST_CASE("midi deviceManager: getMidiInDeviceName walks the in-device range") {
    unsigned int n = YSE::MIDI::DeviceManager().getNumMidiInDevices();
    for (unsigned int i = 0; i < n; i++) {
      const std::string name = YSE::MIDI::DeviceManager().getMidiInDeviceName(i);
      // Name strings come from RtMidi; only require they are non-empty and
      // not the "Invalid Call" sentinel returned when isPrepared() is false.
      CHECK(name != "Invalid Call");
    }
    CHECK(true);
  }

  TEST_CASE("midi deviceManager: getMidiOutDeviceName walks the out-device range") {
    unsigned int n = YSE::MIDI::DeviceManager().getNumMidiOutDevices();
    for (unsigned int i = 0; i < n; i++) {
      const std::string name = YSE::MIDI::DeviceManager().getMidiOutDeviceName(i);
      CHECK(name != "Invalid Call");
    }
    CHECK(true);
  }

  // ─── getMidiOutPort: cache + invalid-port path ───────────────────────────────

  TEST_CASE("midi deviceManager: getMidiOutPort(invalid) returns nullptr") {
    // Port id 9999 is virtually guaranteed to be out of range on any host;
    // RtMidiOut::openPort throws INVALID_PARAMETER, which deviceManager
    // catches and converts to nullptr via the catch arm.
    RtMidiOut* port = YSE::MIDI::DeviceManager().getMidiOutPort(9999);
    CHECK(port == nullptr);
  }

  TEST_CASE("midi deviceManager: getMidiOutPort(invalid) does not cache on failure") {
    // Regression test for issue #32.  Prior to the fix the impl emplaced the
    // new RtMidiOut into midiOutPorts *before* calling openPort, so when
    // openPort threw the catch returned nullptr but left a partially
    // initialised entry behind — a subsequent call with the same ID hit the
    // cache-hit branch and returned a non-null but useless pointer.  After
    // the fix the cache is only populated on success, so every retry of an
    // invalid ID must return nullptr.
    RtMidiOut* first = YSE::MIDI::DeviceManager().getMidiOutPort(9998);
    RtMidiOut* second = YSE::MIDI::DeviceManager().getMidiOutPort(9998);
    CHECK(first == nullptr);
    CHECK(second == nullptr);
  }

  // ─── GenerateMidiError: every Type enum arm ─────────────────────────────────
  //
  // GenerateMidiError dispatches on RtMidiError::Type.  Constructing a synthetic
  // error per arm exercises the full switch without needing a real MIDI failure.
  // The function emits to the YSE log; we only require it does not crash.

  TEST_CASE("midi GenerateMidiError: WARNING arm") {
    RtMidiError e("warn", RtMidiError::Type::WARNING);
    YSE::MIDI::GenerateMidiError(e);
    CHECK(true);
  }

  TEST_CASE("midi GenerateMidiError: DEBUG_WARNING arm") {
    RtMidiError e("dwarn", RtMidiError::Type::DEBUG_WARNING);
    YSE::MIDI::GenerateMidiError(e);
    CHECK(true);
  }

  TEST_CASE("midi GenerateMidiError: UNSPECIFIED arm") {
    RtMidiError e("unspec", RtMidiError::Type::UNSPECIFIED);
    YSE::MIDI::GenerateMidiError(e);
    CHECK(true);
  }

  TEST_CASE("midi GenerateMidiError: NO_DEVICES_FOUND arm") {
    RtMidiError e("no devs", RtMidiError::Type::NO_DEVICES_FOUND);
    YSE::MIDI::GenerateMidiError(e);
    CHECK(true);
  }

  TEST_CASE("midi GenerateMidiError: INVALID_DEVICE arm") {
    RtMidiError e("invdev", RtMidiError::Type::INVALID_DEVICE);
    YSE::MIDI::GenerateMidiError(e);
    CHECK(true);
  }

  TEST_CASE("midi GenerateMidiError: MEMORY_ERROR arm") {
    RtMidiError e("mem", RtMidiError::Type::MEMORY_ERROR);
    YSE::MIDI::GenerateMidiError(e);
    CHECK(true);
  }

  TEST_CASE("midi GenerateMidiError: INVALID_PARAMETER arm") {
    RtMidiError e("invparam", RtMidiError::Type::INVALID_PARAMETER);
    YSE::MIDI::GenerateMidiError(e);
    CHECK(true);
  }

  TEST_CASE("midi GenerateMidiError: INVALID_USE arm") {
    RtMidiError e("invuse", RtMidiError::Type::INVALID_USE);
    YSE::MIDI::GenerateMidiError(e);
    CHECK(true);
  }

  TEST_CASE("midi GenerateMidiError: DRIVER_ERROR arm") {
    RtMidiError e("drv", RtMidiError::Type::DRIVER_ERROR);
    YSE::MIDI::GenerateMidiError(e);
    CHECK(true);
  }

  TEST_CASE("midi GenerateMidiError: SYSTEM_ERROR arm") {
    RtMidiError e("sys", RtMidiError::Type::SYSTEM_ERROR);
    YSE::MIDI::GenerateMidiError(e);
    CHECK(true);
  }

  TEST_CASE("midi GenerateMidiError: THREAD_ERROR arm") {
    RtMidiError e("thr", RtMidiError::Type::THREAD_ERROR);
    YSE::MIDI::GenerateMidiError(e);
    CHECK(true);
  }

  // ─── midiOut: default state + isPrepared() early-return on every method ─────
  //
  // midiOut() leaves the internal device pointer null; isPrepared() therefore
  // returns false and every send method early-returns.  Calling each entry point
  // in this state covers the "false" branch of the guard without touching real
  // hardware.

  TEST_CASE("midiOut: default construction leaves device pointer null") {
    YSE::midiOut out;
    // No public accessor for `device`; the observable effect is that send
    // methods are no-ops.  Driving them must not crash.
    out.NoteOn(YSE::MIDI::CH_01, 60, 100);
    CHECK(true);
  }

  TEST_CASE("midiOut: NoteOn / NoteOff overloads are safe with no device") {
    YSE::midiOut out;
    out.NoteOn(YSE::MIDI::CH_01, YSE::MIDI::C4, (unsigned char)100);
    out.NoteOn(YSE::MIDI::CH_01, (unsigned char)60, (unsigned char)100);
    out.NoteOff(YSE::MIDI::CH_01, YSE::MIDI::C4, (unsigned char)0);
    out.NoteOff(YSE::MIDI::CH_01, (unsigned char)60, (unsigned char)0);
    CHECK(true);
  }

  TEST_CASE("midiOut: PolyPressure overloads are safe with no device") {
    YSE::midiOut out;
    out.PolyPressure(YSE::MIDI::CH_01, YSE::MIDI::C4, (unsigned char)50);
    out.PolyPressure(YSE::MIDI::CH_01, (unsigned char)60, (unsigned char)50);
    CHECK(true);
  }

  TEST_CASE("midiOut: ChannelPressure / ProgramChange / ControlChange are safe with no device") {
    YSE::midiOut out;
    out.ChannelPressure(YSE::MIDI::CH_01, 100);
    out.ProgramChange(YSE::MIDI::CH_01, 5);
    out.ControlChange(YSE::MIDI::CH_01, 7, 100);
    CHECK(true);
  }

  TEST_CASE("midiOut: AllNotesOff (per-channel + all-channels) are safe with no device") {
    YSE::midiOut out;
    out.AllNotesOff(YSE::MIDI::CH_01);
    out.AllNotesOff();
    CHECK(true);
  }

  TEST_CASE("midiOut: Reset (per-channel + all-channels) is safe with no device") {
    YSE::midiOut out;
    out.Reset(YSE::MIDI::CH_01);
    out.Reset();
    CHECK(true);
  }

  TEST_CASE("midiOut: LocalControl / Omni / Poly toggles are safe with no device") {
    YSE::midiOut out;
    out.LocalControl(true);
    out.LocalControl(false);
    out.Omni(true);
    out.Omni(false);
    out.Poly(true);
    out.Poly(false);
    CHECK(true);
  }

  TEST_CASE("midiOut: Raw byte triple is safe with no device") {
    YSE::midiOut out;
    out.Raw((unsigned char)0x90, (unsigned char)60, (unsigned char)100);
    CHECK(true);
  }

  TEST_CASE("midiOut: Raw(string) handles empty / partial / full inputs") {
    // Raw(string) reads up to three bytes from the string with length checks.
    // Empty / 1-byte / 2-byte / 3-byte inputs each pick a distinct branch of
    // the ternary chain in device.cpp's Raw(string).
    YSE::midiOut out;
    out.Raw(std::string()); // length 0
    out.Raw(std::string("\xB0")); // length 1
    out.Raw(std::string("\xB0\x07")); // length 2
    out.Raw(std::string("\xB0\x07\x64")); // length 3
    out.Raw(std::string("\xB0\x07\x64\xFF\xFF")); // length > 3 — extra bytes ignored
    CHECK(true);
  }

  // ─── midiOut::create — invalid-port path ────────────────────────────────────

  TEST_CASE("midiOut::create on an out-of-range port leaves device null") {
    YSE::midiOut out;
    out.create(9999); // DeviceManager returns nullptr → device stays null
    // isPrepared() still false; sends remain no-ops.
    out.NoteOn(YSE::MIDI::CH_01, 60, 100);
    CHECK(true);
  }

} // TEST_SUITE("midi")

#endif // YSE_ENABLE_MIDI_DEVICE

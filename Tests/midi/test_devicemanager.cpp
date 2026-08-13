// Tests for the YSE MIDI device backend (YseEngine/midi/midiDeviceManager.cpp
// and YseEngine/midi/device.cpp).
//
// Coverage:
//   - MIDI::DeviceManager() singleton identity
//   - getNumMidiInDevices / getNumMidiOutDevices return non-negative counts
//     (0 on CI without MIDI hardware, ≥1 on a workstation with devices)
//   - Per-device name lookups walk the full enumerated range, and an ID the
//     manager did not count has no name at all — empty on a live backend and
//     on one that failed to initialise alike (issue #585; the latter used to
//     hand back the literal string "Invalid Call")
//   - getMidiOutPort(ID) — cache hit on repeat call, nullptr on invalid port
//   - GenerateMidiError(RtMidiError) drives every RtMidiError::Type switch arm
//   - midiOut default state and isPrepared() early-return on every send method
//   - midiOut::create(port) attempt; downstream message sends are no-ops when
//     no device is attached
//   - Raw(string) and Raw(pointer, length) handle 0/1/2/3+ byte inputs without
//     out-of-bounds reads (issue #748 made both length-honest)
//   - Concurrent calls from several threads (issue #757). Every entry point
//     takes the manager's mutex now, because it was already reachable from more
//     than one thread — system.cpp on the control thread, inHub while opening a
//     port, `.midiout` from its list handler, and `.midiinfo`'s rescan on the
//     background pool. `isPrepared` lazily constructs the RtMidi backends and
//     `getMidiOutPort` mutates a std::map, so the unsynchronised version was a
//     plain data race. The case below is written for the sanitizer job: on an
//     ordinary run it only proves the calls return, but under TSan it is what
//     fails if the lock is dropped.
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

#include <atomic>
#include <string>
#include <thread>
#include <vector>

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
      // Name strings come from RtMidi; a port the manager counted must have a
      // name, since the count and the names come from the same live backend.
      CHECK_FALSE(YSE::MIDI::DeviceManager().getMidiInDeviceName(i).empty());
    }
    CHECK(true);
  }

  TEST_CASE("midi deviceManager: getMidiOutDeviceName walks the out-device range") {
    unsigned int n = YSE::MIDI::DeviceManager().getNumMidiOutDevices();
    for (unsigned int i = 0; i < n; i++) {
      CHECK_FALSE(YSE::MIDI::DeviceManager().getMidiOutDeviceName(i).empty());
    }
    CHECK(true);
  }

  // ─── no device, no name (issue #585) ────────────────────────────────────────
  //
  // The count is the contract: a name only ever comes back for an ID the
  // manager counted. Both branches of the getter must agree on that. With a
  // live backend RtMidi's getPortName warns and returns "" past the end; on the
  // not-prepared path — a host where RtMidiIn/RtMidiOut construction failed,
  // which is every headless Linux box, since there is no ALSA sequencer to talk
  // to — the manager used to return the literal string "Invalid Call" instead,
  // for *any* ID. That is a twelve-character device name as far as a caller can
  // tell, and it contradicted getNumMidi*Devices(), which answers 0 on exactly
  // the same path.
  //
  // The assertions below are host-independent in form but only the backend-less
  // host exercises the fixed branch: on Windows WinMM always initialises, so
  // this is the empty out-of-range answer RtMidi already gave. The branch this
  // pins is what the Linux CI leg runs.

  TEST_CASE("midi deviceManager: an uncounted MIDI in device has no name (#585)") {
    const unsigned int n = YSE::MIDI::DeviceManager().getNumMidiInDevices();
    CHECK(YSE::MIDI::DeviceManager().getMidiInDeviceName(n).empty());
    CHECK(YSE::MIDI::DeviceManager().getMidiInDeviceName(n + 9999).empty());
    // Index 0 on a manager that reports no devices at all: the enumeration a
    // binding does before it has looked at the count, and the case the bug
    // report is written from.
    if (n == 0) {
      CHECK(YSE::MIDI::DeviceManager().getMidiInDeviceName(0).empty());
    }
  }

  TEST_CASE("midi deviceManager: an uncounted MIDI out device has no name (#585)") {
    const unsigned int n = YSE::MIDI::DeviceManager().getNumMidiOutDevices();
    CHECK(YSE::MIDI::DeviceManager().getMidiOutDeviceName(n).empty());
    CHECK(YSE::MIDI::DeviceManager().getMidiOutDeviceName(n + 9999).empty());
    if (n == 0) {
      CHECK(YSE::MIDI::DeviceManager().getMidiOutDeviceName(0).empty());
    }
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

  // ─── concurrent access (issue #757) ─────────────────────────────────────────

  TEST_CASE("midi deviceManager: concurrent callers are safe (#757)") {
    // Four threads hammering all five entry points at once, including the two
    // that mutate state: `isPrepared` (which lazily constructs RtMidiIn /
    // RtMidiOut and flips `initialized`) and `getMidiOutPort` (which inserts
    // into a std::map). The out-of-range port ids keep the map's failure path
    // busy without opening real hardware.
    //
    // The assertion an ordinary run can make is only that every call returns a
    // consistent answer; the one that matters runs under TSan in CI, where an
    // unsynchronised deviceManager reports a race here.
    constexpr int kThreads = 4;
    constexpr int kRounds = 64;

    const unsigned int expectedIn = YSE::MIDI::DeviceManager().getNumMidiInDevices();
    const unsigned int expectedOut = YSE::MIDI::DeviceManager().getNumMidiOutDevices();

    // Baseline names taken single-threaded, so the workers below can assert the
    // getters answer the same thing under contention as they do alone. (This
    // used to compare against the "Invalid Call" sentinel the not-prepared path
    // returned; #585 replaced it with an empty string, and the port names
    // themselves are the stronger check anyway.)
    std::vector<std::string> namesIn;
    std::vector<std::string> namesOut;
    namesIn.reserve(expectedIn);
    namesOut.reserve(expectedOut);
    for (unsigned int i = 0; i < expectedIn; i++)
      namesIn.push_back(YSE::MIDI::DeviceManager().getMidiInDeviceName(i));
    for (unsigned int i = 0; i < expectedOut; i++)
      namesOut.push_back(YSE::MIDI::DeviceManager().getMidiOutDeviceName(i));

    std::atomic<int> mismatches{0};
    std::vector<std::thread> workers;
    workers.reserve(kThreads);

    for (int t = 0; t < kThreads; t++) {
      workers.emplace_back([&, t] {
        for (int round = 0; round < kRounds; round++) {
          if (YSE::MIDI::DeviceManager().getNumMidiInDevices() != expectedIn)
            mismatches.fetch_add(1, std::memory_order_relaxed);
          if (YSE::MIDI::DeviceManager().getNumMidiOutDevices() != expectedOut)
            mismatches.fetch_add(1, std::memory_order_relaxed);
          for (unsigned int i = 0; i < expectedIn; i++) {
            if (YSE::MIDI::DeviceManager().getMidiInDeviceName(i) != namesIn[i])
              mismatches.fetch_add(1, std::memory_order_relaxed);
          }
          for (unsigned int i = 0; i < expectedOut; i++) {
            if (YSE::MIDI::DeviceManager().getMidiOutDeviceName(i) != namesOut[i])
              mismatches.fetch_add(1, std::memory_order_relaxed);
          }
          // A distinct out-of-range id per thread, so the map is written from
          // four threads rather than four times from one.
          if (YSE::MIDI::DeviceManager().getMidiOutPort(9000u + static_cast<unsigned>(t)) !=
              nullptr) {
            mismatches.fetch_add(1, std::memory_order_relaxed);
          }
        }
      });
    }

    for (auto& worker : workers)
      worker.join();

    CHECK(mismatches.load() == 0);
    // Still answering after the storm, and with the same answer.
    CHECK(YSE::MIDI::DeviceManager().getNumMidiInDevices() == expectedIn);
    CHECK(YSE::MIDI::DeviceManager().getNumMidiOutDevices() == expectedOut);
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
    // Since issue #748 Raw(string) sends the whole string rather than exactly
    // three bytes of it, so none of these lengths is a special case any more —
    // what is being driven is that each still returns without reading past the
    // end. Length 0 is the one input Raw refuses outright.
    YSE::midiOut out;
    out.Raw(std::string()); // length 0 — sends nothing
    out.Raw(std::string("\xB0")); // length 1
    out.Raw(std::string("\xB0\x07")); // length 2 — no longer zero-padded to 3
    out.Raw(std::string("\xB0\x07\x64")); // length 3
    out.Raw(std::string("\xB0\x07\x64\xFF\xFF")); // length > 3 — no longer truncated
    CHECK(true);
  }

  TEST_CASE("midiOut: Raw(pointer, length) is safe with no device (#748)") {
    // The length-honest entry point `.midiout` uses for a numeric byte list.
    // A null buffer and a zero length are refused before the port is looked at,
    // so both are safe whether or not one is open.
    YSE::midiOut out;
    const unsigned char message[3] = {0x90, 60, 100};
    out.Raw(message, 3);
    out.Raw(message, 2); // a two-byte message stays two bytes
    out.Raw(message, 0); // nothing to send
    out.Raw(nullptr, 3); // nothing to send it from
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

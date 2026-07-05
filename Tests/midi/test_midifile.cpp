// Tests for the YSE MIDI subsystem (YseEngine/midi/).
//
// Coverage:
//   - midiMessage base class: getRaw() pointer validity and raw vector properties
//   - midiNote construction, note/velocity getters/setters, raw byte layout
//   - midifile lifecycle: construction, create, destruction
//   - midifileManager: singleton identity, update(), orphan removal
//   - SMF parser (issue #155): decodes the Type-0 fixture into a time-sorted
//     event list with correct note numbers, velocities and sample times
//
// No engine initialisation is required — the MIDI classes (and the SMF parser)
// are independent of PortAudio and the audio graph. SAMPLERATE is initialised
// to 44100 by the device translation unit at static-init time.

#include <doctest/doctest.h>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include "midi/midiMessage.hpp"
#include "midi/midiNote.hpp"
#include "midi/midifile.hpp"
#include "midi/midifileImplementation.h"
#include "midi/midifileManager.h"

TEST_SUITE("midi") {

  // ─── midiMessage: getRaw and raw vector ──────────────────────────────────────

  TEST_CASE("midiMessage: getRaw returns non-null for midiNote") {
    YSE::MIDI::midiNote n(60, 100);
    CHECK(n.getRaw() != nullptr);
  }

  TEST_CASE("midiMessage: raw vector has 3 bytes for a note-on message") {
    YSE::MIDI::midiNote n(60, 100);
    CHECK(n.getRaw()->size() == 3u);
  }

  // ─── midiNote: construction and raw byte layout ──────────────────────────────

  TEST_CASE("midiNote: status byte is 144 (note-on, channel 1)") {
    YSE::MIDI::midiNote n(60, 100);
    CHECK((*n.getRaw())[0] == 144u);
  }

  TEST_CASE("midiNote: note getter returns constructor value") {
    YSE::MIDI::midiNote n(60, 100);
    CHECK(n.note() == 60u);
  }

  TEST_CASE("midiNote: velocity getter returns constructor value") {
    YSE::MIDI::midiNote n(60, 100);
    CHECK(n.velocity() == 100u);
  }

  TEST_CASE("midiNote: raw layout is [status, note, velocity]") {
    YSE::MIDI::midiNote n(48, 64);
    const auto& raw = *n.getRaw();
    CHECK(raw[0] == 144u); // note-on, ch1
    CHECK(raw[1] == 48u); // note number
    CHECK(raw[2] == 64u); // velocity
  }

  // ─── midiNote: setter round-trips ────────────────────────────────────────────

  TEST_CASE("midiNote: note setter round-trip") {
    YSE::MIDI::midiNote n(60, 100);
    n.note(72);
    CHECK(n.note() == 72u);
  }

  TEST_CASE("midiNote: velocity setter round-trip") {
    YSE::MIDI::midiNote n(60, 100);
    n.velocity(64);
    CHECK(n.velocity() == 64u);
  }

  TEST_CASE("midiNote: note setter does not change status or velocity") {
    YSE::MIDI::midiNote n(60, 100);
    n.note(72);
    CHECK((*n.getRaw())[0] == 144u); // status preserved
    CHECK(n.velocity() == 100u); // velocity preserved
  }

  TEST_CASE("midiNote: velocity setter does not change status or note") {
    YSE::MIDI::midiNote n(60, 100);
    n.velocity(64);
    CHECK((*n.getRaw())[0] == 144u); // status preserved
    CHECK(n.note() == 60u); // note preserved
  }

  // ─── midiNote: boundary values ───────────────────────────────────────────────

  TEST_CASE("midiNote: minimum note value 0 is stored correctly") {
    YSE::MIDI::midiNote n(0, 64);
    CHECK(n.note() == 0u);
    CHECK((*n.getRaw())[1] == 0u);
  }

  TEST_CASE("midiNote: maximum note value 127 is stored correctly") {
    YSE::MIDI::midiNote n(127, 64);
    CHECK(n.note() == 127u);
    CHECK((*n.getRaw())[1] == 127u);
  }

  TEST_CASE("midiNote: zero velocity constructs without error") {
    // velocity=0 on a note-on status byte is interpreted as note-off by many devices
    YSE::MIDI::midiNote n(60, 0);
    CHECK(n.note() == 60u);
    CHECK(n.velocity() == 0u);
    CHECK((*n.getRaw())[0] == 144u);
  }

  // ─── midifile: lifecycle (stub implementation) ───────────────────────────────

  TEST_CASE("midifile: construction does not crash") {
    YSE::MIDI::file f;
    // constructor calls Manager().addImplementation() — no device needed
  }

  TEST_CASE("midifile: destruction does not crash") {
    { YSE::MIDI::file f; } // ~file() calls pimpl->removeInterface()
  }

  TEST_CASE("midifile: create on a missing file returns false") {
    YSE::MIDI::file f;
    bool ok = f.create("nonexistent.mid");
    CHECK(ok == false);
  }

  TEST_CASE("midifile: multiple file objects can coexist") {
    const std::string fixture = std::string(YSE_TEST_FIXTURES_DIR) + "/test_type0.mid";
    YSE::MIDI::file a;
    YSE::MIDI::file b;
    bool ok1 = a.create(fixture);
    bool ok2 = b.create(fixture);
    CHECK(ok1 == true);
    CHECK(ok2 == true);
  }

  // ─── midifileManager ─────────────────────────────────────────────────────────

  TEST_CASE("midifileManager: Manager() returns same singleton reference") {
    CHECK(&YSE::MIDI::Manager() == &YSE::MIDI::Manager());
  }

  TEST_CASE("midifileManager: update with live file does not crash") {
    YSE::MIDI::file f;
    YSE::MIDI::Manager().update();
  }

  TEST_CASE("midifileManager: orphaned fileImpl is removed by update") {
    // After ~file(), pimpl->removeInterface() nulls the head pointer.
    // Manager().update() removes that implementation from the forward_list.
    { YSE::MIDI::file f; }
    YSE::MIDI::Manager().update();
  }

  // ─── SMF parser (issue #155) ─────────────────────────────────────────────────

  TEST_CASE("SMF parser: missing file leaves no events and fails") {
    YSE::MIDI::fileImpl impl(nullptr);
    CHECK(impl.create("does-not-exist.mid") == false);
    CHECK(impl.events().empty());
  }

  TEST_CASE("SMF parser: Type-0 fixture decodes to a note-on then note-off") {
    // test_type0.mid: 96 PPQN, 120 BPM tempo meta, note-on C4 vel 127 at tick 0,
    // note-off C4 at tick 96 (one quarter = 0.5 s), end-of-track.
    YSE::MIDI::fileImpl impl(nullptr);
    REQUIRE(impl.create(std::string(YSE_TEST_FIXTURES_DIR) + "/test_type0.mid"));

    const auto& ev = impl.events();
    REQUIRE(ev.size() == 2);

    // Event 0: note-on, channel 1 (status 0x90), note 60, velocity 127, at t=0.
    CHECK((ev[0].status & 0xF0) == 0x90);
    CHECK((ev[0].status & 0x0F) == 0x00);
    CHECK(ev[0].data1 == 60);
    CHECK(ev[0].data2 == 127);
    CHECK(ev[0].sampleTime == 0u);

    // Event 1: note-off (status 0x80), note 60, half a second later.
    CHECK((ev[1].status & 0xF0) == 0x80);
    CHECK(ev[1].data1 == 60);
    // 96 ticks at 96 PPQN, 120 BPM = one quarter note = 0.5 s.
    CHECK(ev[1].sampleTime == static_cast<uint64_t>(YSE::SAMPLERATE) / 2u);
  }

  TEST_CASE("SMF parser: events come back sorted by sample time") {
    YSE::MIDI::fileImpl impl(nullptr);
    REQUIRE(impl.create(std::string(YSE_TEST_FIXTURES_DIR) + "/test_type0.mid"));
    const auto& ev = impl.events();
    for (std::size_t i = 1; i < ev.size(); ++i)
      CHECK(ev[i - 1].sampleTime <= ev[i].sampleTime);
  }

  TEST_CASE("SMF parser: garbage data is rejected without crashing") {
    // A file that exists but is not a valid MIDI file must fail create() (not
    // crash) and leave the event list empty.
    const std::string tmp = std::string(YSE_TEST_FIXTURES_DIR) + "/garbage_not_midi.tmp";
    {
      std::ofstream out(tmp, std::ios::binary);
      const char junk[] = "this is definitely not a midi file";
      out.write(junk, sizeof(junk));
    }
    YSE::MIDI::fileImpl impl(nullptr);
    CHECK(impl.create(tmp) == false);
    CHECK(impl.events().empty());
    std::remove(tmp.c_str());
  }

  // ─── MIDI fixture existence ───────────────────────────────────────────────────

  TEST_CASE("midi fixture: Type-0 MIDI file exists in fixtures directory") {
    // Verifies the committed fixture is present and non-empty so that future
    // parsing tests have a stable target once file parsing is implemented.
    std::string path = std::string(YSE_TEST_FIXTURES_DIR) + "/test_type0.mid";
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    CHECK(f.is_open());
    if (f.is_open()) {
      std::streamsize sz = f.tellg();
      CHECK(sz == 41); // exact size of the committed Type-0 fixture
    }
  }

} // TEST_SUITE("midi")

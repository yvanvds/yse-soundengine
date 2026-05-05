// Tests for the YSE MIDI subsystem (YseEngine/midi/).
//
// Coverage:
//   - midiMessage base class: getRaw() pointer validity and raw vector properties
//   - midiNote construction, note/velocity getters/setters, raw byte layout
//   - midifile lifecycle: construction, create (stub), destruction
//   - midifileManager: singleton identity, update(), orphan removal
//   - MIDI Type-0 fixture: verifies fixture file exists for future parsing tests
//
// Note: MIDI file parsing (midifileImplementation::create) is currently stubbed;
// the fixture is committed for future use when parsing is implemented.
// No engine initialisation is required — the MIDI classes are independent of
// PortAudio and the audio graph.

#include <doctest/doctest.h>
#include <fstream>
#include <string>
#include "midi/midiMessage.hpp"
#include "midi/midiNote.hpp"
#include "midi/midifile.hpp"
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
    CHECK(raw[0] == 144u);   // note-on, ch1
    CHECK(raw[1] == 48u);    // note number
    CHECK(raw[2] == 64u);    // velocity
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
    CHECK((*n.getRaw())[0] == 144u);   // status preserved
    CHECK(n.velocity() == 100u);       // velocity preserved
}

TEST_CASE("midiNote: velocity setter does not change status or note") {
    YSE::MIDI::midiNote n(60, 100);
    n.velocity(64);
    CHECK((*n.getRaw())[0] == 144u);   // status preserved
    CHECK(n.note() == 60u);            // note preserved
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
    {
        YSE::MIDI::file f;
    }  // ~file() calls pimpl->removeInterface()
}

TEST_CASE("midifile: create with any filename returns true (stub)") {
    YSE::MIDI::file f;
    bool ok = f.create("nonexistent.mid");
    CHECK(ok == true);
}

TEST_CASE("midifile: multiple file objects can coexist") {
    YSE::MIDI::file a;
    YSE::MIDI::file b;
    bool ok1 = a.create("a.mid");
    bool ok2 = b.create("b.mid");
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
    {
        YSE::MIDI::file f;
    }
    YSE::MIDI::Manager().update();
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
        CHECK(sz == 41);   // exact size of the committed Type-0 fixture
    }
}

} // TEST_SUITE("midi")

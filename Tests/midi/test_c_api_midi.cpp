// C-API boundary tests for YseEngine/c_api/yse_midi.cpp (issue #568, the
// follow-up half of #417 / epic #420).
//
// The midiIn half of this TU is already exercised from Tests/midi/test_midi_in.cpp
// (issue #52), so it is not repeated here. What had no C-level coverage was the
// midiOut surface, the midifile transport past play(), and midiNote — that is
// what this file adds.
//
// As elsewhere in the #417 / #568 family the assertions are about the boundary
// contract, not MIDI behaviour: NULL-handle safety on every entry point,
// create/destroy ownership pairs, and setter/getter round-trips.
//
// Headless-CI notes, both deliberate:
//
//   * No MIDI port is ever opened. A midiOut that has never been create()d
//     holds a null RtMidi port and every send method early-returns through
//     midiOut::isPrepared() — so the whole send surface is reachable with no
//     hardware. The one open() call uses an index past the end of the (empty
//     on CI) device list, which the device manager answers with nullptr; that
//     path is already asserted at the C++ level in test_devicemanager.cpp.
//   * The midiOut / midiIn entry points are compiled either as the RtMidi
//     implementation or as the uniform-ABI stubs, depending on
//     YSE_ENABLE_MIDI_DEVICE. The cases below call them unconditionally and
//     only assert what holds on both sides of that gate, so the same source
//     covers whichever branch the build selected.
//
// No engine initialisation is required: the MIDI subsystem stands on its own.

#include <doctest/doctest.h>

#include <string>

#include "headers/defines.hpp"

#include "yse_c/yse_common.h"
#include "yse_c/yse_midi.h"

#ifndef YSE_TEST_FIXTURES_DIR
#define YSE_TEST_FIXTURES_DIR "../../Tests/support/fixtures"
#endif

namespace {

  std::string fixture(const char* name) {
    return std::string(YSE_TEST_FIXTURES_DIR) + "/" + name;
  }

} // namespace

TEST_SUITE("capilowcov") {

  // ─── midi file ─────────────────────────────────────────────────────────────

  TEST_CASE("c-api midi file: load failure paths are distinguishable") {
    YseMidiFile* f = yse_midi_file_create();
    REQUIRE(f != nullptr);

    // NULL handle vs NULL filename are separate status codes.
    CHECK(yse_midi_file_load(nullptr, "x.mid") == YSE_ERR_INVALID_HANDLE);
    CHECK(yse_midi_file_load(f, nullptr) == YSE_ERR_INVALID_ARGUMENT);

    yse_clear_last_error();
    CHECK(yse_midi_file_load(f, "definitely_not_here.mid") == YSE_ERR_FILE_NOT_FOUND);
    CHECK(std::string(yse_last_error()).find("definitely_not_here.mid") != std::string::npos);
    yse_clear_last_error();

    yse_midi_file_destroy(f);
  }

  TEST_CASE("c-api midi file: load the fixture and drive the transport") {
    YseMidiFile* f = yse_midi_file_create();
    REQUIRE(f != nullptr);

    const std::string mid = fixture("test_type0.mid");
    REQUIRE(yse_midi_file_load(f, mid.c_str()) == YSE_OK);

    // The transport trio is a pure state machine on the file impl — no engine
    // session and no audio device needed to drive it.
    yse_midi_file_play(f);
    yse_midi_file_pause(f);
    yse_midi_file_play(f);
    yse_midi_file_stop(f);

    // A NULL synth handle leaves the file's synth table untouched rather than
    // dereferencing it.
    yse_midi_file_connect_synth(f, nullptr);
    yse_midi_file_disconnect_synth(f, nullptr);

    yse_midi_file_destroy(f);
  }

  TEST_CASE("c-api midi file: every entry point is NULL-safe") {
    yse_midi_file_play(nullptr);
    yse_midi_file_pause(nullptr);
    yse_midi_file_stop(nullptr);
    yse_midi_file_connect_synth(nullptr, nullptr);
    yse_midi_file_disconnect_synth(nullptr, nullptr);
    yse_midi_file_destroy(nullptr);
    CHECK(true); // reached here without dereferencing a NULL handle
  }

  // ─── midi out ──────────────────────────────────────────────────────────────

  TEST_CASE("c-api midi out: the whole send surface is NULL-safe") {
    yse_midi_out_open(nullptr, 0);
    yse_midi_out_note_on(nullptr, 0, 60, 100);
    yse_midi_out_note_off(nullptr, 0, 60, 0);
    yse_midi_out_poly_pressure(nullptr, 0, 60, 64);
    yse_midi_out_channel_pressure(nullptr, 0, 64);
    yse_midi_out_program_change(nullptr, 0, 12);
    yse_midi_out_control_change(nullptr, 0, 7, 100);
    yse_midi_out_all_notes_off_channel(nullptr, 0);
    yse_midi_out_all_notes_off(nullptr);
    yse_midi_out_reset_channel(nullptr, 0);
    yse_midi_out_reset(nullptr);
    yse_midi_out_local_control(nullptr, 1);
    yse_midi_out_omni(nullptr, 1);
    yse_midi_out_poly(nullptr, 1);
    yse_midi_out_raw3(nullptr, 0x90, 60, 100);
    yse_midi_out_destroy(nullptr);
    CHECK(true); // reached here without dereferencing a NULL handle
  }

  TEST_CASE("c-api midi out: sends on an unopened port are silent no-ops") {
    YseMidiOut* m = yse_midi_out_create();
#if YSE_ENABLE_MIDI_DEVICE
    REQUIRE(m != nullptr);
#else
    // The uniform-ABI stub reports why the surface is unavailable and hands
    // back nothing to own.
    CHECK(m == nullptr);
    CHECK(std::string(yse_last_error()).find("Windows/Linux only") != std::string::npos);
    yse_clear_last_error();
#endif

    // Opening a port index past the end of the device list (always the case on
    // headless CI, where the list is empty) leaves the port unopened; every
    // send below then early-returns inside the engine.
    yse_midi_out_open(m, 9999);

    // The C API's channel argument is clamped to 0..15 before it reaches the
    // engine's M_CHANNEL enum — drive both ends and past both ends.
    for (int channel : {-5, 0, 15, 99}) {
      yse_midi_out_note_on(m, channel, 60, 100);
      yse_midi_out_note_off(m, channel, 60, 0);
      yse_midi_out_poly_pressure(m, channel, 60, 64);
      yse_midi_out_channel_pressure(m, channel, 64);
      yse_midi_out_program_change(m, channel, 12);
      yse_midi_out_control_change(m, channel, 7, 100);
      yse_midi_out_all_notes_off_channel(m, channel);
      yse_midi_out_reset_channel(m, channel);
    }

    yse_midi_out_all_notes_off(m);
    yse_midi_out_reset(m);
    yse_midi_out_local_control(m, 1);
    yse_midi_out_local_control(m, 0);
    yse_midi_out_omni(m, 1);
    yse_midi_out_omni(m, 0);
    yse_midi_out_poly(m, 1);
    yse_midi_out_poly(m, 0);
    yse_midi_out_raw3(m, 0x90, 60, 100);

    yse_midi_out_destroy(m);
    CHECK(true); // the whole surface ran without an open port
  }

  // ─── midi in ───────────────────────────────────────────────────────────────
  //
  // create / destroy / is_open / the callback setters / free_message are
  // covered from test_midi_in.cpp (#52). close() on a never-opened port is the
  // one entry point that suite does not reach through the C ABI.

  TEST_CASE("c-api midi in: close is safe on a never-opened port and on NULL") {
    yse_midi_in_close(nullptr);

    YseMidiIn* m = yse_midi_in_create();
#if YSE_ENABLE_MIDI_DEVICE
    REQUIRE(m != nullptr);
    CHECK(yse_midi_in_is_open(m) == 0);
    yse_midi_in_close(m); // never opened
    CHECK(yse_midi_in_is_open(m) == 0);
    yse_midi_in_close(m); // idempotent
    CHECK(yse_midi_in_is_open(m) == 0);
#else
    CHECK(m == nullptr);
    yse_midi_in_close(m);
#endif
    yse_midi_in_destroy(m);
  }

  // ─── midiNote ──────────────────────────────────────────────────────────────

  TEST_CASE("c-api midi note: create / accessors round-trip and destroy") {
    YseMidiNote* n = yse_midi_note_create(60, 100);
    REQUIRE(n != nullptr);

    CHECK(yse_midi_note_get_note(n) == 60);
    CHECK(yse_midi_note_get_velocity(n) == 100);

    yse_midi_note_set_note(n, 72);
    CHECK(yse_midi_note_get_note(n) == 72);
    CHECK(yse_midi_note_get_velocity(n) == 100); // unchanged

    yse_midi_note_set_velocity(n, 1);
    CHECK(yse_midi_note_get_velocity(n) == 1);
    CHECK(yse_midi_note_get_note(n) == 72); // unchanged

    // Both ends of the 7-bit range survive the trip.
    yse_midi_note_set_note(n, 0);
    yse_midi_note_set_velocity(n, 127);
    CHECK(yse_midi_note_get_note(n) == 0);
    CHECK(yse_midi_note_get_velocity(n) == 127);

    yse_midi_note_destroy(n);
  }

  TEST_CASE("c-api midi note: every entry point is NULL-safe") {
    yse_midi_note_set_note(nullptr, 60);
    yse_midi_note_set_velocity(nullptr, 100);
    CHECK(yse_midi_note_get_note(nullptr) == 0);
    CHECK(yse_midi_note_get_velocity(nullptr) == 0);
    yse_midi_note_destroy(nullptr);
    yse_midi_in_free_message(nullptr); // documented as NULL-tolerant
    CHECK(true); // reached here without dereferencing a NULL handle
  }

} // TEST_SUITE("capilowcov")

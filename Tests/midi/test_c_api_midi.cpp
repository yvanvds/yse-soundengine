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
#include <vector>

#include "headers/defines.hpp"

#include "support/capilowcov_offline.hpp"

#include "yse_c/yse_channel.h"
#include "yse_c/yse_common.h"
#include "yse_c/yse_midi.h"
#include "yse_c/yse_patcher.h"
#include "yse_c/yse_sound.h"

#if YSE_ENABLE_MIDI_DEVICE
#include <cstddef>
#include <cstring>
#include <vector>

#include "c_api/yse_c_internal.hpp"
#include "support/midi_dispatch_tester.hpp"
#endif

#ifndef YSE_TEST_FIXTURES_DIR
#define YSE_TEST_FIXTURES_DIR "../../Tests/support/fixtures"
#endif

namespace {

  std::string fixture(const char* name) {
    return std::string(YSE_TEST_FIXTURES_DIR) + "/" + name;
  }

#if YSE_ENABLE_MIDI_DEVICE
  // Keeps every raw delivery it is handed instead of freeing it in the
  // callback: the buffer is the receiver's, so it must outlive the call and be
  // released later, from another thread, with yse_midi_in_free_message (#913).
  struct KeptMessages {
    struct Kept {
      double ts;
      unsigned char* bytes;
      std::size_t len;
      void* userData;
    };
    std::vector<Kept> kept;

    static void YSE_C_CALLBACK keep(double ts, unsigned char* bytes, std::size_t len, void* ud) {
      static_cast<KeptMessages*>(ud)->kept.push_back({ts, bytes, len, ud});
    }
  };
#endif

  // A GUI value read the way a C host reads it: size query, then fill.
  std::string guiValue(YsePHandle* h) {
    const size_t need = yse_phandle_get_gui_value(h, nullptr, 0);
    std::vector<char> buf(need + 1, '\0');
    yse_phandle_get_gui_value(h, buf.data(), buf.size());
    return std::string(buf.data());
  }

} // namespace

TEST_SUITE("capilowcov") {

  // ─── midi file ─────────────────────────────────────────────────────────────

  TEST_CASE("c-api midi file: load failure paths are distinguishable") {
    YseMidiFile* f = yse_midi_file_create();
    REQUIRE(f != nullptr);

    // NULL handle vs NULL filename are separate status codes.
    CHECK(yse_midi_file_load(nullptr, "x.mid") == YSE_ERR_INVALID_HANDLE);
    yse_clear_last_error();
    CHECK(yse_midi_file_load(f, nullptr) == YSE_ERR_INVALID_ARGUMENT);
    CHECK_FALSE(std::string(yse_last_error()).empty()); // issue #910

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
    const unsigned char programChange[] = {0xC0, 12};
    yse_midi_out_raw(nullptr, programChange, sizeof(programChange));
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

  TEST_CASE("c-api midi out: raw sends any length and refuses empty input (#903)") {
    // Clear any error a previous case left behind so the check below sees only
    // what this case's calls report.
    yse_clear_last_error();
    YseMidiOut* m = yse_midi_out_create();
#if YSE_ENABLE_MIDI_DEVICE
    REQUIRE(m != nullptr);
#else
    CHECK(m == nullptr);
    yse_clear_last_error();
#endif
    yse_midi_out_open(m, 9999); // no such port: every send early-returns

    const unsigned char channelPressure[] = {0xD0, 64}; // 2 bytes
    const unsigned char noteOn[] = {0x90, 60, 100}; // 3 bytes
    const unsigned char sysex[] = {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7}; // SysEx

    yse_midi_out_raw(m, channelPressure, sizeof(channelPressure));
    yse_midi_out_raw(m, noteOn, sizeof(noteOn));
    yse_midi_out_raw(m, sysex, sizeof(sysex));
    yse_midi_out_raw(m, noteOn, 1); // a one-byte prefix is still a message

    // The documented no-op inputs: nothing to send, or nothing to send it from.
    yse_midi_out_raw(m, noteOn, 0);
    yse_midi_out_raw(m, nullptr, 3);
    yse_midi_out_raw(m, nullptr, 0);

    // None of these is a failure, so none of them reports one.
    CHECK(std::string(yse_last_error()).empty());

    yse_midi_out_destroy(m);
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

#if YSE_ENABLE_MIDI_DEVICE
  TEST_CASE("c-api midi in: a raw delivery is the receiver's to free later (#913)") {
    // A real message through the raw bridge, freed the documented way — after
    // the callback has returned, which is the ownership transfer the header
    // promises. The dispatch is synchronous on this thread (the tester stands
    // in for RtMidi's input thread), so the vector needs no lock.
    YseMidiIn* m = yse_midi_in_create();
    REQUIRE(m != nullptr);
    YSE::midiIn* port = yse_c::midi_in_from_handle(m);
    REQUIRE(port != nullptr);

    KeptMessages sink;
    yse_midi_in_set_raw_callback(m, &KeptMessages::keep, &sink);

    const unsigned char noteOn[] = {0x92, 0x3C, 0x64};
    const unsigned char sysex[] = {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};
    MidiInDispatchTester::dispatch(*port, 1.5, noteOn, sizeof(noteOn));
    MidiInDispatchTester::dispatch(*port, 2.25, sysex, sizeof(sysex));
    yse_midi_in_set_raw_callback(m, nullptr, nullptr);
    MidiInDispatchTester::dispatch(*port, 3.0, noteOn, sizeof(noteOn)); // detached

    REQUIRE(sink.kept.size() == 2u);
    const auto& a = sink.kept[0];
    const auto& b = sink.kept[1];
    CHECK(a.userData == &sink);
    CHECK(a.ts == doctest::Approx(1.5));
    REQUIRE(a.len == sizeof(noteOn));
    REQUIRE(a.bytes != nullptr);
    // A copy of its own, not a view of the dispatcher's buffer.
    CHECK(a.bytes != noteOn);
    CHECK(std::memcmp(a.bytes, noteOn, sizeof(noteOn)) == 0);
    CHECK(b.ts == doctest::Approx(2.25));
    REQUIRE(b.len == sizeof(sysex));
    REQUIRE(b.bytes != nullptr);
    CHECK(b.bytes != a.bytes);
    CHECK(std::memcmp(b.bytes, sysex, sizeof(sysex)) == 0);

    // Released after the dispatch, from the host side — under ASan a buffer the
    // bridge still owned or had already freed fails here.
    for (const auto& k : sink.kept)
      yse_midi_in_free_message(k.bytes);
    yse_midi_in_destroy(m);
  }
#endif

  // ─── MIDI input injection (#1247) ──────────────────────────────────────────

  TEST_CASE("c-api midi in inject: argument errors are reported, not ignored (#1247)") {
    const unsigned char noteOn[] = {0x90, 60, 100};
#if YSE_ENABLE_MIDI_DEVICE
    CHECK(yse_midi_in_inject(7, nullptr, 3) == YSE_ERR_INVALID_ARGUMENT);
    CHECK(yse_midi_in_inject(7, noteOn, 0) == YSE_ERR_INVALID_ARGUMENT);
    // The hub has eight input ports, 0..7.
    CHECK(yse_midi_in_inject(8, noteOn, sizeof(noteOn)) == YSE_ERR_INVALID_ARGUMENT);
    CHECK(std::string(yse_last_error()).find("port 8") != std::string::npos);
    // A port nothing listens to takes the message and drops it.
    CHECK(yse_midi_in_inject(7, noteOn, sizeof(noteOn)) == YSE_OK);
#else
    CHECK(yse_midi_in_inject(7, noteOn, sizeof(noteOn)) == YSE_ERR_MIDI);
#endif
  }

#if YSE_ENABLE_MIDI_DEVICE
  TEST_CASE("c-api midi in inject: a patch's MIDI input objects hear injected messages, with no "
            "hardware (#1247)") {
    // The host's view, end to end through the C ABI only: a patch built with
    // yse_patcher_*, played by a sound in a running (offline) engine, fed by
    // yse_midi_in_inject and read back through the GUI value of `.i` boxes.
    // Port 7, as in test_patcher_midiin.cpp: a machine with eight MIDI inputs
    // is rare enough that real traffic cannot reach the assertions.
    REQUIRE(capilowcov::ensureOffline());

    YsePatcher* patch = yse_patcher_create();
    REQUIRE(patch != nullptr);
    REQUIRE(yse_patcher_init(patch, 1) == YSE_OK);

    YsePHandle* notein = yse_patcher_create_object(patch, ".notein", "7");
    YsePHandle* ctlin = yse_patcher_create_object(patch, ".ctlin", "7");
    YsePHandle* pitch = yse_patcher_create_object(patch, ".i", nullptr);
    YsePHandle* velocity = yse_patcher_create_object(patch, ".i", nullptr);
    YsePHandle* ccValue = yse_patcher_create_object(patch, ".i", nullptr);
    REQUIRE(notein != nullptr);
    REQUIRE(ctlin != nullptr);
    REQUIRE(pitch != nullptr);
    REQUIRE(velocity != nullptr);
    REQUIRE(ccValue != nullptr);
    yse_patcher_connect(patch, notein, 0, pitch, 0);
    yse_patcher_connect(patch, notein, 1, velocity, 0);
    yse_patcher_connect(patch, ctlin, 0, ccValue, 0);

    YseSound* snd = yse_sound_create();
    REQUIRE(snd != nullptr);
    REQUIRE(yse_sound_load_patcher(snd, patch, yse_channel_master(), 0.f) == YSE_OK);
    yse_sound_play(snd);
    capilowcov::pump(10);

    CHECK(guiValue(pitch) == "0");
    CHECK(guiValue(ccValue) == "0");

    const unsigned char noteOn[] = {0x90, 64, 99};
    const unsigned char cc[] = {0xB0, 7, 42};
    REQUIRE(yse_midi_in_inject(7, noteOn, sizeof(noteOn)) == YSE_OK);
    REQUIRE(yse_midi_in_inject(7, cc, sizeof(cc)) == YSE_OK);
    capilowcov::pump(10);

    CHECK(guiValue(pitch) == "64");
    CHECK(guiValue(velocity) == "99");
    CHECK(guiValue(ccValue) == "42");

    // A message on another port does not reach port 7's listeners.
    const unsigned char otherPort[] = {0x90, 70, 10};
    REQUIRE(yse_midi_in_inject(6, otherPort, sizeof(otherPort)) == YSE_OK);
    capilowcov::pump(10);
    CHECK(guiValue(pitch) == "64");

    yse_sound_stop(snd);
    capilowcov::pump(5);
    yse_sound_destroy(snd);
    capilowcov::pump(5);
    yse_patcher_destroy(patch);
  }

  TEST_CASE("c-api midi in inject: more than eight MIDI input objects on one port all hear it, "
            "across patches and across a patch swap (#1246)") {
    // The repro from #1246 at the host's level: the hub used to hold eight
    // listeners per port, counted across every patcher, so the ninth `.ctlin`
    // loaded and then never sent anything. Here two live patches hold six
    // each (twelve on port 7), and then one is disposed and a third, also of
    // six, is built straight away — before the engine has destroyed the
    // disposed one — which is what a host swapping patches does.
    REQUIRE(capilowcov::ensureOffline());

    struct Patch {
      YsePatcher* patch = nullptr;
      YseSound* snd = nullptr;
      std::vector<YsePHandle*> boxes;
    };
    const auto build = [](int listeners) {
      Patch p;
      p.patch = yse_patcher_create();
      REQUIRE(p.patch != nullptr);
      REQUIRE(yse_patcher_init(p.patch, 1) == YSE_OK);
      for (int i = 0; i < listeners; i++) {
        YsePHandle* in = yse_patcher_create_object(p.patch, ".ctlin", "7");
        YsePHandle* box = yse_patcher_create_object(p.patch, ".i", nullptr);
        REQUIRE(in != nullptr);
        REQUIRE(box != nullptr);
        yse_patcher_connect(p.patch, in, 0, box, 0);
        p.boxes.push_back(box);
      }
      p.snd = yse_sound_create();
      REQUIRE(p.snd != nullptr);
      REQUIRE(yse_sound_load_patcher(p.snd, p.patch, yse_channel_master(), 0.f) == YSE_OK);
      yse_sound_play(p.snd);
      return p;
    };
    const auto dispose = [](Patch& p) {
      yse_sound_stop(p.snd);
      yse_sound_destroy(p.snd);
      yse_patcher_destroy(p.patch);
      p = Patch{};
    };
    const auto heard = [](const Patch& p, const std::string& value) {
      int count = 0;
      for (YsePHandle* box : p.boxes) {
        if (guiValue(box) == value) count++;
      }
      return count;
    };

    Patch a = build(6);
    Patch b = build(6);
    capilowcov::pump(10);

    const unsigned char first[] = {0xB0, 1, 11};
    REQUIRE(yse_midi_in_inject(7, first, sizeof(first)) == YSE_OK);
    capilowcov::pump(10);
    CHECK(heard(a, "11") == 6);
    CHECK(heard(b, "11") == 6);

    // Dispose one and build its replacement at once, with no blocks between.
    dispose(a);
    Patch c = build(6);
    capilowcov::pump(10);

    const unsigned char second[] = {0xB0, 1, 12};
    REQUIRE(yse_midi_in_inject(7, second, sizeof(second)) == YSE_OK);
    capilowcov::pump(10);
    CHECK(heard(b, "12") == 6);
    CHECK(heard(c, "12") == 6);

    yse_sound_stop(b.snd);
    yse_sound_stop(c.snd);
    capilowcov::pump(5);
    yse_sound_destroy(b.snd);
    yse_sound_destroy(c.snd);
    capilowcov::pump(5);
    yse_patcher_destroy(b.patch);
    yse_patcher_destroy(c.patch);
  }
#endif

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

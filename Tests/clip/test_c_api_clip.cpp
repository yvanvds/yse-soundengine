// C-API clip-transport tests (issue #417) — exercises YseEngine/c_api/yse_clip.cpp
// through the flat C ABI: create/destroy, bind, the event-list copy, loop length,
// the synth / MIDI-out connect pair, and play/stop/is_playing.
//
// Like the C++ clip cases in test_clip_transport.cpp, these drive
// CLOCK::Manager().update() and CLIP::Manager().update() directly on the test
// thread, so they live in the same TEST_SUITE("clip") and inherit its ctest
// isolation (yse_tests_clip) — those manager updates must not share a process
// with a live audio thread. No engine init and no audio hardware are needed:
// clocks and clip transports exist independently of the device layer.
//
// The focus is the boundary contract rather than the firing rules (already
// covered exhaustively against the transport in test_clip_transport.cpp):
// NULL handles, the documented "events may be NULL only when count is 0" rule,
// and the create/destroy ownership pair.

#include <doctest/doctest.h>

#include <vector>

#include "yse_c/yse_clip.h"
#include "yse_c/yse_midi.h"
#include "yse_c/yse_system.h"

#include "yse.hpp"
#include "clip/clipManager.h"
#include "clock/clockManager.h"

namespace {

  YseClipEvent cev(double start, double dur, int ch, int pitch, float vel = 0.8f,
                   float bend = 0.f) {
    YseClipEvent e;
    e.start_beat = start;
    e.duration_beats = dur;
    e.channel = ch;
    e.pitch = pitch;
    e.velocity = vel;
    e.pitch_bend = bend;
    return e;
  }

  // One "audio block" as the device manager would drive it: clocks first, then
  // the clip transports.
  void pump(int blocks, float seconds = 0.25f) {
    for (int i = 0; i < blocks; ++i) {
      YSE::CLOCK::Manager().update(seconds);
      YSE::CLIP::Manager().update();
    }
  }

} // namespace

TEST_SUITE("clip") {

  TEST_CASE("c-api clip: create -> bind -> play -> stop through the flat ABI") {
    YseSystem* sys = yse_system_get();
    REQUIRE(yse_system_create_clock(sys, "capi.clip.run", 60.f) == 1); // 1 beat/second

    YseClip* c = yse_clip_create();
    REQUIRE(c != nullptr);
    CHECK(yse_clip_bind(c, "capi.clip.run") == 1);

    const std::vector<YseClipEvent> events{cev(1.0, 1.0, 1, 60), cev(2.0, 1.0, 1, 64)};
    yse_clip_set_events(c, events.data(), events.size());
    yse_clip_set_loop_length(c, 4.0);
    CHECK(yse_clip_is_playing(c) == 0);

    yse_clip_play(c);
    pump(8);
    CHECK(yse_clip_is_playing(c) == 1);

    yse_clip_stop(c);
    pump(1);
    CHECK(yse_clip_is_playing(c) == 0);

    yse_clip_destroy(c);
    yse_system_destroy_clock(sys, "capi.clip.run");
    pump(1, 0.01f); // let the audio side retire the clock
  }

  TEST_CASE("c-api clip: bind rejects an unknown clock and NULL arguments") {
    YseSystem* sys = yse_system_get();
    REQUIRE(yse_system_create_clock(sys, "capi.clip.bind", 120.f) == 1);

    YseClip* c = yse_clip_create();
    REQUIRE(c != nullptr);
    CHECK(yse_clip_bind(c, "capi.clip.bind") == 1);
    CHECK(yse_clip_bind(c, "capi.clip.bind.nope") == 0);
    CHECK(yse_clip_bind(c, nullptr) == 0);
    CHECK(yse_clip_bind(nullptr, "capi.clip.bind") == 0);

    yse_clip_destroy(c);
    yse_system_destroy_clock(sys, "capi.clip.bind");
    pump(1, 0.01f);
  }

  TEST_CASE("c-api clip: a NULL event list is accepted only when count is 0") {
    YseClip* c = yse_clip_create();
    REQUIRE(c != nullptr);

    yse_clip_set_events(c, nullptr, 0); // documented: clears the list
    const std::vector<YseClipEvent> events{cev(0.5, 0.25, 3, 72, 0.5f, 0.25f)};
    yse_clip_set_events(c, events.data(), events.size());
    yse_clip_set_events(c, nullptr, 4); // rejected — no read through the NULL
    yse_clip_set_loop_length(c, 0.0); // <= 0 disables looping

    yse_clip_destroy(c);
  }

  TEST_CASE("c-api clip: connecting a NULL synth or MIDI-out handle is a no-op") {
    YseClip* c = yse_clip_create();
    REQUIRE(c != nullptr);

    yse_clip_connect_synth(c, nullptr);
    yse_clip_disconnect_synth(c, nullptr);
    yse_clip_connect_midi_out(c, nullptr);
    yse_clip_disconnect_midi_out(c, nullptr);

    // An unopened MIDI-out port is a silent no-op too (the handle is NULL on
    // builds without MIDI device support, which the calls above already cover).
    YseMidiOut* out = yse_midi_out_create();
    if (out != nullptr) {
      yse_clip_connect_midi_out(c, out);
      yse_clip_disconnect_midi_out(c, out);
      yse_midi_out_destroy(out);
    }

    yse_clip_destroy(c);
  }

  TEST_CASE("c-api clip: every entry point is NULL-safe") {
    const std::vector<YseClipEvent> events{cev(0.0, 1.0, 1, 60)};
    yse_clip_destroy(nullptr);
    yse_clip_set_events(nullptr, events.data(), events.size());
    yse_clip_set_loop_length(nullptr, 4.0);
    yse_clip_connect_synth(nullptr, nullptr);
    yse_clip_disconnect_synth(nullptr, nullptr);
    yse_clip_connect_midi_out(nullptr, nullptr);
    yse_clip_disconnect_midi_out(nullptr, nullptr);
    yse_clip_play(nullptr);
    yse_clip_stop(nullptr);
    CHECK(yse_clip_is_playing(nullptr) == 0);
  }

} // TEST_SUITE("clip")

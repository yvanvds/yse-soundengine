// C-API synth surface tests (issue #157) — exercises YseEngine/c_api/yse_synth.*
// entirely through the flat C ABI (no engine C++ headers), mirroring the C++
// synth coverage in test_synth.cpp / test_synth_keyboard.cpp at the C surface.
//
// The engine is driven offline (yse_system_init_offline + render_offline), so
// no audio hardware is needed and the suite runs in CI. Because it calls
// yse_system_close() it lives in its own TEST_SUITE("synthcapi") and its own
// ctest process (see Tests/CMakeLists.txt) — the same isolation the
// synthlifecycle / midisynth / playersynth suites use, keeping System::close()'s
// process-global teardown out of the crowded combined suite (#298/#304).

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

#include "yse_c/yse_synth.h"
#include "yse_c/yse_sound.h"
#include "yse_c/yse_system.h"

using namespace std::chrono_literals;

namespace {

  // Pump the engine offline until the synth has cloned `expected` voices (i.e.
  // reached OBJECT_READY on the setup pool) or the budget expires. Mirrors the
  // drain() helper the C++ playersynth/midisynth suites use, but through the C
  // surface: update() flags the managers, render_offline() runs the audio
  // callback that promotes setup and renders a block.
  bool drainUntilVoices(YseSystem* sys, YseSynth* syn, int expected,
                        std::chrono::milliseconds budget = 2000ms) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      yse_system_update(sys);
      yse_system_render_offline(sys, 1);
      if (yse_synth_get_num_voices(syn) >= expected) return true;
      std::this_thread::sleep_for(2ms);
    }
    return yse_synth_get_num_voices(syn) >= expected;
  }

  // Render `blocks` offline blocks so queued note/control events are drained on
  // the audio thread and any release tails advance.
  void render(YseSystem* sys, int blocks) {
    for (int i = 0; i < blocks; ++i) {
      yse_system_update(sys);
      yse_system_render_offline(sys, 1);
    }
  }

  // Pump the engine for `budget` so the manager delete jobs run on the setup
  // pool and released sound/synth impls are fully freed BEFORE yse_system_close()
  // tears down the thread pools. Skipping this races teardown against the
  // pending deletes (the #298/#304 lineage) and can segfault at close. Mirrors
  // the drain() the C++ playersynth/midisynth suites run before System::close().
  void drainFor(YseSystem* sys, std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      yse_system_update(sys);
      yse_system_render_offline(sys, 1);
      std::this_thread::sleep_for(2ms);
    }
  }

  // ---- note-callback probe (issue #157 §7 hook) ------------------------------
  // Captureless C function pointer, per the onNoteEvent contract. Counts the
  // events it sees and transposes every note up an octave in place — the
  // transpose proves the hook can rewrite the note before allocation, and doing
  // it on note-off as well as note-on keeps a released note matching its
  // note-on's rewritten identity (docs/design/synth_core.md §7).
  std::atomic<int> g_noteOnCalls{0};
  std::atomic<int> g_noteOffCalls{0};

  void YSE_C_CALLBACK testNoteCb(int note_on, float* note_number, float* velocity) {
    (void)velocity;
    if (note_number) *note_number += 12.0f;
    if (note_on) {
      g_noteOnCalls.fetch_add(1, std::memory_order_relaxed);
    } else {
      g_noteOffCalls.fetch_add(1, std::memory_order_relaxed);
    }
  }

} // namespace

TEST_SUITE("synthcapi") {

  // ─── null-handle safety (no engine required) ──────────────────────────────

  TEST_CASE("c-api synth: NULL handles are null-safe") {
    CHECK(yse_synth_is_valid(nullptr) == 0);
    CHECK(yse_synth_get_num_voices(nullptr) == 0);
    CHECK(yse_synth_add_voices_sine(nullptr, 4, 0, 0, 127, 0.01f, 0.1f, 0.8f, 0.2f) ==
          YSE_ERR_INVALID_HANDLE);
    CHECK(yse_synth_attach_to_sound(nullptr, nullptr, nullptr, 1.0f) == YSE_ERR_INVALID_HANDLE);

    // Void entry points must no-op, not crash, on a NULL handle.
    yse_synth_note_on(nullptr, 1, 60, 1.0f);
    yse_synth_note_off(nullptr, 1, 60, 0.0f);
    yse_synth_all_notes_off(nullptr, 0);
    yse_synth_pitch_wheel(nullptr, 1, 0.5f);
    yse_synth_controller(nullptr, 1, 74, 0.5f);
    yse_synth_aftertouch(nullptr, 1, 60, 0.5f);
    yse_synth_sustain(nullptr, 1, 1);
    yse_synth_sostenuto(nullptr, 1, 1);
    yse_synth_soft_pedal(nullptr, 1, 1);
    yse_synth_set_note_callback(nullptr, &testNoteCb);
    yse_synth_destroy(nullptr);
  }

  // ─── build, attach, and drive a polyphonic note sequence ──────────────────

  TEST_CASE("c-api synth: build, attach, and drive a polyphonic sequence") {
    YseSystem* sys = yse_system_get();
    REQUIRE(sys != nullptr);
    yse_system_close(sys);
    if (yse_system_init_offline(sys) != YSE_OK) return; // unavailable → skip

    YseSynth* syn = yse_synth_create();
    REQUIRE(syn != nullptr);
    CHECK(yse_synth_is_valid(syn) == 1);

    // Invalid voice count is rejected before any group is recorded.
    CHECK(yse_synth_add_voices_sine(syn, 0, 0, 0, 127, 0.01f, 0.1f, 0.8f, 0.2f) ==
          YSE_ERR_INVALID_ARGUMENT);

    // 8-voice omni pool, snappy envelope so releases finish quickly.
    REQUIRE(yse_synth_add_voices_sine(syn, 8, 0, 0, 127, 0.001f, 0.001f, 1.0f, 0.05f) == YSE_OK);

    YseSound* snd = yse_sound_create();
    REQUIRE(snd != nullptr);
    REQUIRE(yse_synth_attach_to_sound(syn, snd, nullptr, 0.8f) == YSE_OK);
    CHECK(yse_sound_is_valid(snd) == 1);
    yse_sound_play(snd);

    // Setup pool clones the voices; wait until the pool is playable.
    REQUIRE(drainUntilVoices(sys, syn, 8));
    CHECK(yse_synth_get_num_voices(syn) == 8);

    // Sound a C-major triad, then exercise the full control surface.
    yse_synth_note_on(syn, 1, 60, 1.0f);
    yse_synth_note_on(syn, 1, 64, 0.8f);
    yse_synth_note_on(syn, 1, 67, 0.6f);
    render(sys, 4);

    yse_synth_pitch_wheel(syn, 1, 0.25f);
    yse_synth_controller(syn, 1, 74, 0.5f); // non-pedal CC → stored
    yse_synth_aftertouch(syn, 1, 60, 0.7f); // per-note pressure
    yse_synth_aftertouch(syn, 1, -1, 0.4f); // channel-wide pressure
    render(sys, 2);

    // Sustain pedal: note-offs defer until the pedal lifts.
    yse_synth_sustain(syn, 1, 1);
    yse_synth_note_off(syn, 1, 60, 0.0f);
    yse_synth_note_off(syn, 1, 64, 0.0f);
    render(sys, 2);
    yse_synth_sustain(syn, 1, 0); // release the sustained notes now
    render(sys, 2);

    // Sostenuto + soft pedal smoke, then a hard bulk release.
    yse_synth_sostenuto(syn, 1, 1);
    yse_synth_soft_pedal(syn, 1, 1);
    yse_synth_note_on(syn, 1, 72, 0.9f);
    render(sys, 2);
    yse_synth_soft_pedal(syn, 1, 0);
    yse_synth_sostenuto(syn, 1, 0);
    yse_synth_all_notes_off(syn, 0);
    render(sys, 8);

    // The synth and its voice pool survive the whole sequence intact.
    CHECK(yse_synth_is_valid(syn) == 1);
    CHECK(yse_synth_get_num_voices(syn) == 8);

    yse_sound_destroy(snd); // sound must go before the synth it renders
    yse_synth_destroy(syn);
    drainFor(sys, 300ms); // let the delete jobs free the impls before close
    yse_system_close(sys);
  }

  // ─── onNoteEvent hook installs, fires on the audio thread, and clears ─────

  TEST_CASE("c-api synth: note callback fires and clears") {
    YseSystem* sys = yse_system_get();
    REQUIRE(sys != nullptr);
    yse_system_close(sys);
    if (yse_system_init_offline(sys) != YSE_OK) return;

    g_noteOnCalls.store(0, std::memory_order_relaxed);
    g_noteOffCalls.store(0, std::memory_order_relaxed);

    YseSynth* syn = yse_synth_create();
    REQUIRE(syn != nullptr);
    REQUIRE(yse_synth_add_voices_sine(syn, 4, 0, 0, 127, 0.001f, 0.001f, 1.0f, 0.02f) == YSE_OK);

    YseSound* snd = yse_sound_create();
    REQUIRE(snd != nullptr);
    REQUIRE(yse_synth_attach_to_sound(syn, snd, nullptr, 1.0f) == YSE_OK);
    yse_sound_play(snd);
    REQUIRE(drainUntilVoices(sys, syn, 4));

    // Install the hook, then drive note-ons/offs; the hook fires inside the
    // offline render (audio thread), before allocation.
    yse_synth_set_note_callback(syn, &testNoteCb);
    yse_synth_note_on(syn, 1, 60, 1.0f);
    yse_synth_note_on(syn, 1, 62, 1.0f);
    render(sys, 3);
    yse_synth_note_off(syn, 1, 60, 0.0f);
    yse_synth_note_off(syn, 1, 62, 0.0f);
    render(sys, 3);

    CHECK(g_noteOnCalls.load(std::memory_order_relaxed) == 2);
    CHECK(g_noteOffCalls.load(std::memory_order_relaxed) == 2);

    // Clear the hook: subsequent events must not reach it.
    yse_synth_set_note_callback(syn, nullptr);
    const int onsBefore = g_noteOnCalls.load(std::memory_order_relaxed);
    yse_synth_note_on(syn, 1, 64, 1.0f);
    render(sys, 3);
    CHECK(g_noteOnCalls.load(std::memory_order_relaxed) == onsBefore);

    yse_synth_all_notes_off(syn, 0);
    render(sys, 4);

    yse_sound_destroy(snd);
    yse_synth_destroy(syn);
    drainFor(sys, 300ms); // let the delete jobs free the impls before close
    yse_system_close(sys);
  }

  // ─── split keyboard: several groups sum into one voice count ──────────────

  TEST_CASE("c-api synth: split keyboard builds multiple groups") {
    YseSystem* sys = yse_system_get();
    REQUIRE(sys != nullptr);
    yse_system_close(sys);
    if (yse_system_init_offline(sys) != YSE_OK) return;

    YseSynth* syn = yse_synth_create();
    REQUIRE(syn != nullptr);
    // Bass below C3, lead C3 and up — both on channel 1.
    REQUIRE(yse_synth_add_voices_sine(syn, 4, 1, 0, 47, 0.005f, 0.05f, 0.8f, 0.1f) == YSE_OK);
    REQUIRE(yse_synth_add_voices_sine(syn, 6, 1, 48, 127, 0.005f, 0.05f, 0.8f, 0.1f) == YSE_OK);

    YseSound* snd = yse_sound_create();
    REQUIRE(snd != nullptr);
    REQUIRE(yse_synth_attach_to_sound(syn, snd, nullptr, 0.7f) == YSE_OK);
    yse_sound_play(snd);

    REQUIRE(drainUntilVoices(sys, syn, 10));
    CHECK(yse_synth_get_num_voices(syn) == 10);

    // A low note lands in the bass group, a high note in the lead group.
    yse_synth_note_on(syn, 1, 36, 1.0f);
    yse_synth_note_on(syn, 1, 72, 1.0f);
    render(sys, 4);
    yse_synth_all_notes_off(syn, 1);
    render(sys, 6);

    CHECK(yse_synth_get_num_voices(syn) == 10);

    yse_sound_destroy(snd);
    yse_synth_destroy(syn);
    drainFor(sys, 300ms); // let the delete jobs free the impls before close
    yse_system_close(sys);
  }

  // ─── per-note positioning: NULL-handle safety (no engine required) ─────────

  TEST_CASE("c-api synth: position-handler NULL handles are null-safe") {
    YseSynthPositionParams params{};
    // Fallible entry point rejects a NULL handle explicitly.
    CHECK(yse_synth_set_position_handler(nullptr, YSE_POSITION_HANDLER_RANDOM_SPREAD, &params) ==
          YSE_ERR_INVALID_HANDLE);
    // Void entry points must no-op, not crash, on a NULL handle.
    yse_synth_handler_param(nullptr, YSE_HANDLER_PARAM_CENTER_X, 1.0f);
    yse_synth_note_position(nullptr, 1, 60, 1.0f, 2.0f, 3.0f);
    // get_voice_position writes the origin on a NULL handle and tolerates NULL outs.
    float x = 9.f, y = 9.f, z = 9.f;
    yse_synth_get_voice_position(nullptr, 1, 60, &x, &y, &z);
    CHECK(x == 0.f);
    CHECK(y == 0.f);
    CHECK(z == 0.f);
    yse_synth_get_voice_position(nullptr, 1, 60, nullptr, nullptr, nullptr);
  }

  // ─── randomSpread: distinct per-note positions + steerable centre ──────────
  //
  // The issue's acceptance case, driven entirely through the flat C ABI: create
  // a synth, select the random-spread handler, play notes, and observe distinct
  // per-note positions via engine introspection (yse_synth_get_voice_position).
  // Then steer the shared centre with yse_synth_handler_param and confirm the
  // whole scatter tracks it.

  TEST_CASE("c-api synth: randomSpread gives distinct per-note positions") {
    YseSystem* sys = yse_system_get();
    REQUIRE(sys != nullptr);
    yse_system_close(sys);
    if (yse_system_init_offline(sys) != YSE_OK) return;

    YseSynth* syn = yse_synth_create();
    REQUIRE(syn != nullptr);
    REQUIRE(yse_synth_add_voices_sine(syn, 8, 0, 0, 127, 0.005f, 0.05f, 0.8f, 0.05f) == YSE_OK);

    // Select the random-spread handler with a generous radius and a fixed seed,
    // BEFORE attach (the engine rejects a handler swap after the pool is built).
    YseSynthPositionParams params{};
    params.spread_radius = 5.0f;
    params.spread_seed = 1234u;
    REQUIRE(yse_synth_set_position_handler(syn, YSE_POSITION_HANDLER_RANDOM_SPREAD, &params) ==
            YSE_OK);

    YseSound* snd = yse_sound_create();
    REQUIRE(snd != nullptr);
    REQUIRE(yse_synth_attach_to_sound(syn, snd, nullptr, 0.8f) == YSE_OK);
    yse_sound_play(snd);
    REQUIRE(drainUntilVoices(sys, syn, 8));

    // Sound six distinct notes; each lands on its own slot with its own draw.
    const int base = 60;
    const int n = 6;
    for (int i = 0; i < n; ++i)
      yse_synth_note_on(syn, 1, base + i, 0.7f);
    render(sys, 3);

    // Collect each note's position and require distinct scatter: at least one
    // pair differs (radius 5 across separate RNG streams -> effectively all do).
    float px[n], py[n], pz[n];
    for (int i = 0; i < n; ++i)
      yse_synth_get_voice_position(syn, 1, base + i, &px[i], &py[i], &pz[i]);
    int distinctPairs = 0;
    for (int i = 0; i < n; ++i) {
      // Draw sits inside the radius sphere around the origin centre.
      CHECK(std::sqrt(px[i] * px[i] + py[i] * py[i] + pz[i] * pz[i]) <= 5.0f * 1.7321f + 1e-3f);
      for (int j = i + 1; j < n; ++j) {
        if (px[i] != px[j] || py[i] != py[j] || pz[i] != pz[j]) ++distinctPairs;
      }
    }
    CHECK(distinctPairs > 0);

    // Steer the shared centre far along +X; the held scatter tracks it next block.
    float bx = px[0];
    yse_synth_handler_param(syn, YSE_HANDLER_PARAM_CENTER_X, 100.0f);
    render(sys, 3);
    float ax = 0.f, ay = 0.f, az = 0.f;
    yse_synth_get_voice_position(syn, 1, base, &ax, &ay, &az);
    CHECK(ax > bx + 90.0f); // moved by ~the centre shift, radius aside

    yse_synth_all_notes_off(syn, 0);
    render(sys, 6);

    yse_sound_destroy(snd);
    yse_synth_destroy(syn);
    drainFor(sys, 300ms);
    yse_system_close(sys);
  }

  // ─── orbit: swarm notes trace distinct, moving orbits ──────────────────────

  TEST_CASE("c-api synth: orbit handler moves voices over time") {
    YseSystem* sys = yse_system_get();
    REQUIRE(sys != nullptr);
    yse_system_close(sys);
    if (yse_system_init_offline(sys) != YSE_OK) return;

    YseSynth* syn = yse_synth_create();
    REQUIRE(syn != nullptr);
    REQUIRE(yse_synth_add_voices_sine(syn, 8, 0, 0, 127, 0.005f, 0.05f, 0.8f, 0.1f) == YSE_OK);

    // Fixed-radius orbit so phase alone separates notes; a brisk rate so a few
    // blocks visibly advance each orbit.
    YseSynthPositionParams params{};
    params.orbit_radius = 2.0f;
    params.orbit_velocity_radius = 0.0f;
    params.orbit_aftertouch_widen = 0.0f;
    params.orbit_rate = 4.0f;
    params.orbit_height = 0.0f;
    params.orbit_release_slow = 0.5f;
    REQUIRE(yse_synth_set_position_handler(syn, YSE_POSITION_HANDLER_ORBIT, &params) == YSE_OK);

    YseSound* snd = yse_sound_create();
    REQUIRE(snd != nullptr);
    REQUIRE(yse_synth_attach_to_sound(syn, snd, nullptr, 0.8f) == YSE_OK);
    yse_sound_play(snd);
    REQUIRE(drainUntilVoices(sys, syn, 8));

    const int base = 48;
    const int n = 6;
    for (int i = 0; i < n; ++i)
      yse_synth_note_on(syn, 1, base + i, 0.8f);
    render(sys, 2);

    // Distinct per note, and each sits on the fixed-radius ring.
    float fx[n], fy[n], fz[n];
    for (int i = 0; i < n; ++i) {
      yse_synth_get_voice_position(syn, 1, base + i, &fx[i], &fy[i], &fz[i]);
      CHECK(std::sqrt(fx[i] * fx[i] + fz[i] * fz[i]) == doctest::Approx(2.0).epsilon(0.05));
    }
    int distinctPairs = 0;
    for (int i = 0; i < n; ++i)
      for (int j = i + 1; j < n; ++j)
        if (fx[i] != fx[j] || fz[i] != fz[j]) ++distinctPairs;
    CHECK(distinctPairs > 0);

    // Advance several blocks: every note's orbit has moved.
    render(sys, 30);
    int moved = 0;
    for (int i = 0; i < n; ++i) {
      float cx = 0.f, cy = 0.f, cz = 0.f;
      yse_synth_get_voice_position(syn, 1, base + i, &cx, &cy, &cz);
      if (cx != fx[i] || cz != fz[i]) ++moved;
    }
    CHECK(moved == n);

    yse_synth_all_notes_off(syn, 0);
    render(sys, 6);

    yse_sound_destroy(snd);
    yse_synth_destroy(syn);
    drainFor(sys, 300ms);
    yse_system_close(sys);
  }

  // ─── static handler holds a fixed position; unknown kind is rejected ───────

  TEST_CASE("c-api synth: static handler fixes every voice's position") {
    YseSystem* sys = yse_system_get();
    REQUIRE(sys != nullptr);
    yse_system_close(sys);
    if (yse_system_init_offline(sys) != YSE_OK) return;

    YseSynth* syn = yse_synth_create();
    REQUIRE(syn != nullptr);
    REQUIRE(yse_synth_add_voices_sine(syn, 4, 0, 0, 127, 0.005f, 0.05f, 0.8f, 0.05f) == YSE_OK);

    // Unknown kind is rejected before any prototype is built.
    CHECK(yse_synth_set_position_handler(syn, (YseSynthPositionHandler)999, nullptr) ==
          YSE_ERR_INVALID_ARGUMENT);

    YseSynthPositionParams params{};
    params.static_x = 7.0f;
    params.static_y = -1.0f;
    params.static_z = -3.0f;
    REQUIRE(yse_synth_set_position_handler(syn, YSE_POSITION_HANDLER_STATIC, &params) == YSE_OK);

    YseSound* snd = yse_sound_create();
    REQUIRE(snd != nullptr);
    REQUIRE(yse_synth_attach_to_sound(syn, snd, nullptr, 0.8f) == YSE_OK);
    yse_sound_play(snd);
    REQUIRE(drainUntilVoices(sys, syn, 4));

    yse_synth_note_on(syn, 1, 60, 0.9f);
    render(sys, 3);
    float x = 0.f, y = 0.f, z = 0.f;
    yse_synth_get_voice_position(syn, 1, 60, &x, &y, &z);
    CHECK(x == doctest::Approx(7.0f));
    CHECK(y == doctest::Approx(-1.0f));
    CHECK(z == doctest::Approx(-3.0f));

    yse_synth_all_notes_off(syn, 0);
    render(sys, 6);

    yse_sound_destroy(snd);
    yse_synth_destroy(syn);
    drainFor(sys, 300ms);
    yse_system_close(sys);
  }

  // ─── no handler: default origin, then app-driven notePosition placement ────
  //
  // With no handler attached, notePosition is the imperative placement path (a
  // handler would re-steer the voice each block; see the header docs).

  TEST_CASE("c-api synth: notePosition places voices with no handler") {
    YseSystem* sys = yse_system_get();
    REQUIRE(sys != nullptr);
    yse_system_close(sys);
    if (yse_system_init_offline(sys) != YSE_OK) return;

    YseSynth* syn = yse_synth_create();
    REQUIRE(syn != nullptr);
    REQUIRE(yse_synth_add_voices_sine(syn, 4, 0, 0, 127, 0.005f, 0.05f, 0.8f, 0.05f) == YSE_OK);
    // No set_position_handler call — voices default to the aggregate origin.

    YseSound* snd = yse_sound_create();
    REQUIRE(snd != nullptr);
    REQUIRE(yse_synth_attach_to_sound(syn, snd, nullptr, 0.8f) == YSE_OK);
    yse_sound_play(snd);
    REQUIRE(drainUntilVoices(sys, syn, 4));

    yse_synth_note_on(syn, 1, 60, 0.9f);
    render(sys, 3);
    float x = 9.f, y = 9.f, z = 9.f;
    yse_synth_get_voice_position(syn, 1, 60, &x, &y, &z);
    CHECK(x == doctest::Approx(0.0f));
    CHECK(y == doctest::Approx(0.0f));
    CHECK(z == doctest::Approx(0.0f));

    // notePosition places the voice imperatively (bounded RT-safe message).
    yse_synth_note_position(syn, 1, 60, -4.0f, 0.0f, 2.0f);
    render(sys, 3);
    yse_synth_get_voice_position(syn, 1, 60, &x, &y, &z);
    CHECK(x == doctest::Approx(-4.0f));
    CHECK(z == doctest::Approx(2.0f));

    yse_synth_all_notes_off(syn, 0);
    render(sys, 6);

    yse_sound_destroy(snd);
    yse_synth_destroy(syn);
    drainFor(sys, 300ms);
    yse_system_close(sys);
  }

} // TEST_SUITE("synthcapi")

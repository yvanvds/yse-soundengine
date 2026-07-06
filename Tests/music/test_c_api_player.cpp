// C-API generative-player surface tests (issue #268) — exercises the player
// create -> play path in YseEngine/c_api/yse_music.* entirely through the flat
// C ABI (no engine C++ headers).
//
// Before #268 the C API had no working create path: yse_player_create() took
// no synth, so the YSE::player it returned was never bound to an implementation
// (pimpl stayed null). Driving it did nothing useful, and the original crash
// (yse_player_play() dereferencing the null pimpl) was the reachable symptom.
// #268 gives yse_player_create() a synth handle and calls player::create()
// under the hood, so the returned player is live: play() actually generates
// notes into the synth.
//
// Coverage here:
//   * NULL-handle safety (no engine): yse_player_create(NULL) fails cleanly and
//     every void entry point no-ops on a NULL handle.
//   * End-to-end: build a synth behind a sound, create a player BOUND TO THAT
//     synth through the C ABI, play it, and assert the synth actually receives
//     note-ons (proving the create path connected the player) — this is the
//     regression: an unbound player generates zero notes. stop() then halts
//     generation.
//
// ISOLATION: like the synthcapi / playersynth suites, these cases call
// yse_system_close() (process-global thread-pool teardown), so they run as a
// dedicated ctest process in TEST_SUITE("playercapi"), excluded from the
// combined run (#298/#304). initOffline() needs no audio hardware, so the
// end-to-end case runs in CI; if it is unavailable the case bails out and
// doctest counts it as a pass.

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "yse_c/yse_music.h"
#include "yse_c/yse_synth.h"
#include "yse_c/yse_sound.h"
#include "yse_c/yse_system.h"

using namespace std::chrono_literals;

namespace {

  // Captureless note-event probe (onNoteEvent contract). Counts the note-ons the
  // synth receives from the player. The offline render is single-threaded (the
  // test thread also drives the render), so a relaxed atomic is ample.
  std::atomic<int> g_playerNoteOns{0};

  void YSE_C_CALLBACK countNoteOn(int note_on, float* /*note_number*/, float* /*velocity*/) {
    if (note_on) g_playerNoteOns.fetch_add(1, std::memory_order_relaxed);
  }

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

  // Advance the engine `blocks` blocks. update() ticks the gated managers
  // (scale drain, player generation, synth lifecycle) alongside the audio
  // render that drains the player's note messages into the synth.
  void render(YseSystem* sys, int blocks) {
    for (int i = 0; i < blocks; ++i) {
      yse_system_update(sys);
      yse_system_render_offline(sys, 1);
    }
  }

  // Pump the engine so the manager delete jobs free released impls BEFORE
  // yse_system_close() tears down the pools (the #298/#304 lineage).
  void drainFor(YseSystem* sys, std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      yse_system_update(sys);
      yse_system_render_offline(sys, 1);
      std::this_thread::sleep_for(2ms);
    }
  }

} // namespace

TEST_SUITE("playercapi") {

  // ─── NULL-handle safety (no engine required) ──────────────────────────────

  TEST_CASE("c-api player: NULL handles are null-safe") {
    // create() with a NULL synth must fail cleanly, not crash or hand back an
    // unbound player.
    CHECK(yse_player_create(nullptr) == nullptr);

    // Every void entry point no-ops on a NULL handle; queries return 0.
    yse_player_play(nullptr);
    yse_player_stop(nullptr);
    CHECK(yse_player_is_playing(nullptr) == 0);
    yse_player_set_minimum_pitch(nullptr, 48.f, 0.f);
    yse_player_set_maximum_pitch(nullptr, 72.f, 0.f);
    yse_player_set_minimum_velocity(nullptr, 0.3f, 0.f);
    yse_player_set_maximum_velocity(nullptr, 0.8f, 0.f);
    yse_player_set_minimum_gap(nullptr, 0.f, 0.f);
    yse_player_set_maximum_gap(nullptr, 0.f, 0.f);
    yse_player_set_minimum_length(nullptr, 0.1f, 0.f);
    yse_player_set_maximum_length(nullptr, 0.3f, 0.f);
    yse_player_set_voices(nullptr, 4, 0.f);
    yse_player_set_scale(nullptr, nullptr, 0.f);
    yse_player_play_motifs(nullptr, 0.5f, 0.f);
    yse_player_play_partial_motifs(nullptr, 0.5f, 0.f);
    yse_player_fit_motifs_to_scale(nullptr, 1.f, 0.f);
    yse_player_destroy(nullptr);
    CHECK(true); // reached here without dereferencing a NULL handle
  }

  // ─── create -> play actually drives notes into the synth ──────────────────

  TEST_CASE("c-api player: create(synth) binds the player and play() drives notes") {
    YseSystem* sys = yse_system_get();
    REQUIRE(sys != nullptr);
    yse_system_close(sys);
    if (yse_system_init_offline(sys) != YSE_OK) return; // unavailable → skip

    g_playerNoteOns.store(0, std::memory_order_relaxed);

    // Synth behind a sound, with a note probe installed so we can see exactly
    // what the player sends it.
    YseSynth* syn = yse_synth_create();
    REQUIRE(syn != nullptr);
    REQUIRE(yse_synth_add_voices_sine(syn, 8, 0, 0, 127, 0.001f, 0.001f, 1.0f, 0.05f) == YSE_OK);
    yse_synth_set_note_callback(syn, &countNoteOn);

    YseSound* snd = yse_sound_create();
    REQUIRE(snd != nullptr);
    REQUIRE(yse_synth_attach_to_sound(syn, snd, nullptr, 0.8f) == YSE_OK);
    yse_sound_play(snd);
    REQUIRE(drainUntilVoices(sys, syn, 8));

    // Create the player BOUND to the synth — the #268 create path. A pre-#268
    // unbound player would generate nothing below.
    YsePlayer* pl = yse_player_create(syn);
    REQUIRE(pl != nullptr);
    CHECK(yse_player_is_playing(pl) == 0);

    // Constrain to a C-major scale and set tight, gapless timing so notes fire
    // quickly.
    YseScale* sc = yse_scale_create();
    REQUIRE(sc != nullptr);
    yse_scale_add(sc, 60.f, 12.f);
    yse_scale_add(sc, 62.f, 12.f);
    yse_scale_add(sc, 64.f, 12.f);
    yse_scale_add(sc, 65.f, 12.f);
    yse_scale_add(sc, 67.f, 12.f);
    yse_scale_add(sc, 69.f, 12.f);
    yse_scale_add(sc, 71.f, 12.f);

    yse_player_set_scale(pl, sc, 0.f);
    yse_player_set_minimum_pitch(pl, 48.f, 0.f);
    yse_player_set_maximum_pitch(pl, 84.f, 0.f);
    yse_player_set_minimum_velocity(pl, 0.4f, 0.f);
    yse_player_set_maximum_velocity(pl, 0.9f, 0.f);
    yse_player_set_minimum_gap(pl, 0.f, 0.f);
    yse_player_set_maximum_gap(pl, 0.f, 0.f);
    yse_player_set_minimum_length(pl, 0.02f, 0.f);
    yse_player_set_maximum_length(pl, 0.06f, 0.f);
    yse_player_set_voices(pl, 4, 0.f);

    // Drain the scale + config messages into the impl before generating.
    render(sys, 20);
    yse_player_play(pl);
    CHECK(yse_player_is_playing(pl) == 1);

    render(sys, 3000);

    // The create path connected the player: play() actually produced notes on
    // the synth. (Zero here would mean the player was never bound — the #268
    // regression.)
    CHECK(g_playerNoteOns.load(std::memory_order_relaxed) > 0);

    // stop() halts generation: no NEW note-ons after it settles.
    yse_player_stop(pl);
    CHECK(yse_player_is_playing(pl) == 0);
    render(sys, 300); // let outstanding notes finish
    const int mark = g_playerNoteOns.load(std::memory_order_relaxed);
    render(sys, 600);
    CHECK(g_playerNoteOns.load(std::memory_order_relaxed) == mark);

    yse_player_destroy(pl);
    yse_scale_destroy(sc);
    yse_sound_destroy(snd); // sound before the synth it renders
    yse_synth_destroy(syn);
    drainFor(sys, 300ms); // let the delete jobs free the impls before close
    yse_system_close(sys);
  }

} // TEST_SUITE("playercapi")

// C-API boundary tests for the music primitives in YseEngine/c_api/yse_music.cpp
// (issue #568, the follow-up half of #417 / epic #420).
//
// yse_music.cpp wraps five types. The player half already has an end-to-end
// suite (Tests/music/test_c_api_player.cpp, issue #268), so it is deliberately
// NOT repeated here — only the three motif-weighting entry points that suite
// never reaches are covered, plus the NULL contracts. The note / pNote / scale
// / motif half had no C-level coverage at all, and that is the bulk of this
// file.
//
// As in test_c_api_surface.cpp the assertions are about the boundary contract:
// NULL-handle safety on every entry point, create/destroy ownership pairs, and
// setter/getter round-trips through the flat ABI. Musical semantics are already
// covered in C++ by test_scale.cpp / test_note.cpp / test_motif.cpp.
//
// note / pNote / scale / motif need no engine at all. The three player motif
// cases do, and they use the suite's shared offline bootstrap.

#include <doctest/doctest.h>

#include <string>

#include "support/capilowcov_offline.hpp"

#include "yse_c/yse_common.h"
#include "yse_c/yse_music.h"
#include "yse_c/yse_synth.h"

TEST_SUITE("capilowcov") {

  // ─── note ──────────────────────────────────────────────────────────────────

  TEST_CASE("c-api note: create / accessors round-trip and destroy") {
    YseNote* n = yse_note_create(60.0f, 0.8f, 0.25f, 3);
    REQUIRE(n != nullptr);

    CHECK(yse_note_get_pitch(n) == doctest::Approx(60.0f));
    CHECK(yse_note_get_volume(n) == doctest::Approx(0.8f));
    CHECK(yse_note_get_length(n) == doctest::Approx(0.25f));
    CHECK(yse_note_get_channel(n) == 3);

    // The bulk setter replaces every field at once.
    yse_note_set(n, 64.0f, 0.5f, 0.5f, 7);
    CHECK(yse_note_get_pitch(n) == doctest::Approx(64.0f));
    CHECK(yse_note_get_volume(n) == doctest::Approx(0.5f));
    CHECK(yse_note_get_length(n) == doctest::Approx(0.5f));
    CHECK(yse_note_get_channel(n) == 7);

    // ... and the per-field setters change exactly one field each.
    yse_note_set_pitch(n, 67.0f);
    yse_note_set_volume(n, 0.9f);
    yse_note_set_length(n, 0.125f);
    yse_note_set_channel(n, 1);
    CHECK(yse_note_get_pitch(n) == doctest::Approx(67.0f));
    CHECK(yse_note_get_volume(n) == doctest::Approx(0.9f));
    CHECK(yse_note_get_length(n) == doctest::Approx(0.125f));
    CHECK(yse_note_get_channel(n) == 1);

    yse_note_destroy(n);
  }

  TEST_CASE("c-api note: every entry point is NULL-safe") {
    yse_note_set(nullptr, 1.f, 1.f, 1.f, 1);
    yse_note_set_pitch(nullptr, 1.f);
    yse_note_set_volume(nullptr, 1.f);
    yse_note_set_length(nullptr, 1.f);
    yse_note_set_channel(nullptr, 1);
    CHECK(yse_note_get_pitch(nullptr) == doctest::Approx(0.0f));
    CHECK(yse_note_get_volume(nullptr) == doctest::Approx(0.0f));
    CHECK(yse_note_get_length(nullptr) == doctest::Approx(0.0f));
    CHECK(yse_note_get_channel(nullptr) == 0);
    yse_note_destroy(nullptr);
  }

  // ─── pNote ─────────────────────────────────────────────────────────────────

  TEST_CASE("c-api pnote: create / accessors round-trip and destroy") {
    YsePNote* n = yse_pnote_create(1.5f, 62.0f, 0.7f, 0.3f, 2);
    REQUIRE(n != nullptr);

    CHECK(yse_pnote_get_position(n) == doctest::Approx(1.5f));
    CHECK(yse_pnote_get_pitch(n) == doctest::Approx(62.0f));
    CHECK(yse_pnote_get_volume(n) == doctest::Approx(0.7f));
    CHECK(yse_pnote_get_length(n) == doctest::Approx(0.3f));

    yse_pnote_set_position(n, 2.25f);
    yse_pnote_set_pitch(n, 65.0f);
    yse_pnote_set_volume(n, 0.4f);
    yse_pnote_set_length(n, 0.6f);
    CHECK(yse_pnote_get_position(n) == doctest::Approx(2.25f));
    CHECK(yse_pnote_get_pitch(n) == doctest::Approx(65.0f));
    CHECK(yse_pnote_get_volume(n) == doctest::Approx(0.4f));
    CHECK(yse_pnote_get_length(n) == doctest::Approx(0.6f));

    yse_pnote_destroy(n);
  }

  TEST_CASE("c-api pnote: every entry point is NULL-safe") {
    yse_pnote_set_position(nullptr, 1.f);
    yse_pnote_set_pitch(nullptr, 1.f);
    yse_pnote_set_volume(nullptr, 1.f);
    yse_pnote_set_length(nullptr, 1.f);
    CHECK(yse_pnote_get_position(nullptr) == doctest::Approx(0.0f));
    CHECK(yse_pnote_get_pitch(nullptr) == doctest::Approx(0.0f));
    CHECK(yse_pnote_get_volume(nullptr) == doctest::Approx(0.0f));
    CHECK(yse_pnote_get_length(nullptr) == doctest::Approx(0.0f));
    yse_pnote_destroy(nullptr);
  }

  // ─── scale ─────────────────────────────────────────────────────────────────

  TEST_CASE("c-api scale: add / has / remove / nearest / size / clear") {
    YseScale* s = yse_scale_create();
    REQUIRE(s != nullptr);
    CHECK(yse_scale_size(s) == 0u);

    // step <= 0 adds only the exact pitch, which keeps the size arithmetic
    // below independent of the octave-replication range.
    yse_scale_add(s, 60.0f, 0.0f);
    yse_scale_add(s, 64.0f, 0.0f);
    yse_scale_add(s, 67.0f, 0.0f);
    CHECK(yse_scale_size(s) == 3u);
    CHECK(yse_scale_has(s, 60.0f) == 1);
    CHECK(yse_scale_has(s, 61.0f) == 0);

    // The nearest in-scale pitch to a non-member is one of the members.
    const float nearest = yse_scale_nearest(s, 63.0f);
    CHECK((nearest == doctest::Approx(64.0f) || nearest == doctest::Approx(60.0f)));

    yse_scale_remove(s, 64.0f, 0.0f);
    CHECK(yse_scale_has(s, 64.0f) == 0);
    CHECK(yse_scale_size(s) == 2u);

    yse_scale_clear(s);
    CHECK(yse_scale_size(s) == 0u);
    CHECK(yse_scale_has(s, 60.0f) == 0);

    yse_scale_destroy(s);
  }

  TEST_CASE("c-api scale: octave replication follows the step argument") {
    YseScale* s = yse_scale_create();
    REQUIRE(s != nullptr);

    // The default 12-semitone step replicates the pitch at every octave.
    yse_scale_add(s, 60.0f, 12.0f);
    CHECK(yse_scale_has(s, 60.0f) == 1);
    CHECK(yse_scale_has(s, 72.0f) == 1);
    CHECK(yse_scale_has(s, 48.0f) == 1);
    CHECK(yse_scale_size(s) > 1u);

    yse_scale_remove(s, 60.0f, 12.0f);
    CHECK(yse_scale_has(s, 72.0f) == 0);

    yse_scale_destroy(s);
  }

  TEST_CASE("c-api scale: every entry point is NULL-safe") {
    yse_scale_add(nullptr, 60.f, 12.f);
    yse_scale_remove(nullptr, 60.f, 12.f);
    yse_scale_clear(nullptr);
    CHECK(yse_scale_has(nullptr, 60.f) == 0);
    CHECK(yse_scale_nearest(nullptr, 60.f) == doctest::Approx(0.0f));
    CHECK(yse_scale_size(nullptr) == 0u);
    yse_scale_destroy(nullptr);
  }

  // ─── motif ─────────────────────────────────────────────────────────────────

  TEST_CASE("c-api motif: add / size / length / transpose / clear") {
    YseMotif* m = yse_motif_create();
    REQUIRE(m != nullptr);
    CHECK(yse_motif_empty(m) == 1);
    CHECK(yse_motif_size(m) == 0u);

    YsePNote* a = yse_pnote_create(0.0f, 60.0f, 0.8f, 0.25f, 0);
    YsePNote* b = yse_pnote_create(1.0f, 64.0f, 0.8f, 0.25f, 0);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);

    yse_motif_add(m, a);
    yse_motif_add(m, b);
    CHECK(yse_motif_empty(m) == 0);
    CHECK(yse_motif_size(m) == 2u);

    // add() copies the note, so the source pNotes can go now.
    yse_pnote_destroy(a);
    yse_pnote_destroy(b);

    yse_motif_set_length(m, 4.0f);
    CHECK(yse_motif_get_length(m) == doctest::Approx(4.0f));

    // The auto form derives the length from the notes instead — a different,
    // positive value here (last note at 1.0 + 0.25 long).
    yse_motif_set_length_auto(m);
    CHECK(yse_motif_get_length(m) > 0.0f);
    CHECK(yse_motif_get_length(m) < 4.0f);

    yse_motif_transpose(m, 2.0f); // shifts every note; size is unchanged
    CHECK(yse_motif_size(m) == 2u);

    YseScale* s = yse_scale_create();
    REQUIRE(s != nullptr);
    yse_scale_add(s, 60.0f, 12.0f);
    yse_motif_set_first_pitch(m, s);
    yse_scale_destroy(s);

    yse_motif_clear(m);
    CHECK(yse_motif_empty(m) == 1);
    CHECK(yse_motif_size(m) == 0u);

    yse_motif_destroy(m);
  }

  TEST_CASE("c-api motif: every entry point is NULL-safe") {
    YseMotif* m = yse_motif_create();
    REQUIRE(m != nullptr);

    yse_motif_add(nullptr, nullptr);
    yse_motif_add(m, nullptr); // live motif, NULL note — still a no-op
    yse_motif_clear(nullptr);
    yse_motif_set_length(nullptr, 1.f);
    yse_motif_set_length_auto(nullptr);
    yse_motif_transpose(nullptr, 1.f);
    yse_motif_set_first_pitch(nullptr, nullptr);
    yse_motif_set_first_pitch(m, nullptr); // live motif, NULL scale
    CHECK(yse_motif_get_length(nullptr) == doctest::Approx(0.0f));
    CHECK(yse_motif_empty(nullptr) == 0); // NULL is not "an empty motif"
    CHECK(yse_motif_size(nullptr) == 0u);
    CHECK(yse_motif_size(m) == 0u); // the NULL-note add above changed nothing
    yse_motif_destroy(nullptr);

    yse_motif_destroy(m);
  }

  // ─── player motif weighting ────────────────────────────────────────────────
  //
  // The rest of the player surface is covered end-to-end by the `playercapi`
  // suite (#268); only the three motif entry points it never reaches are
  // exercised here.

  TEST_CASE("c-api player motif surface is NULL-safe") {
    YseMotif* m = yse_motif_create();
    REQUIRE(m != nullptr);

    yse_player_add_motif(nullptr, nullptr, 1);
    yse_player_add_motif(nullptr, m, 1);
    yse_player_remove_motif(nullptr, nullptr);
    yse_player_remove_motif(nullptr, m);
    yse_player_adjust_motif_weight(nullptr, nullptr, 1);
    yse_player_adjust_motif_weight(nullptr, m, 1);

    yse_motif_destroy(m);
    CHECK(true); // reached here without dereferencing a NULL handle
  }

  TEST_CASE("c-api player: motifs can be added, reweighted and removed") {
    if (!capilowcov::ensureOffline()) return; // engine unavailable → skip

    // yse_player_create() needs a live synth handle (#268); a bare
    // yse_synth_create() is enough — no voices or sound attachment is required
    // to exercise the motif table.
    YseSynth* syn = yse_synth_create();
    REQUIRE(syn != nullptr);

    YsePlayer* p = yse_player_create(syn);
    REQUIRE(p != nullptr);

    YseMotif* m = yse_motif_create();
    REQUIRE(m != nullptr);
    YsePNote* n = yse_pnote_create(0.0f, 60.0f, 0.8f, 0.25f, 0);
    REQUIRE(n != nullptr);
    yse_motif_add(m, n);
    yse_pnote_destroy(n);
    yse_motif_set_length_auto(m);

    // Add, reweight, remove — each is a queued message to the player impl, so
    // pump the engine between them and assert the player survives the round
    // rather than reaching into its private motif table.
    yse_player_add_motif(p, m, 1);
    capilowcov::pump(5);
    yse_player_adjust_motif_weight(p, m, 5);
    capilowcov::pump(5);
    yse_player_remove_motif(p, m);
    capilowcov::pump(5);

    CHECK(yse_player_is_playing(p) == 0);

    yse_player_destroy(p);
    yse_motif_destroy(m);
    capilowcov::pump(5); // let the delete jobs run before the synth goes
    yse_synth_destroy(syn);
    capilowcov::pump(5);
  }

} // TEST_SUITE("capilowcov")

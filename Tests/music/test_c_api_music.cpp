// C-API boundary tests for the music primitives in YseEngine/c_api/yse_music.cpp
// (issue #568, the follow-up half of #417 / epic #420).
//
// yse_music.cpp wraps five types. The player half already has an end-to-end
// suite (Tests/music/test_c_api_player.cpp, issue #268), so it is deliberately
// NOT repeated here — only the motif entry points that suite never reaches are
// covered: the three motif-weighting calls, and (issue #913) the three
// motif-mode probabilities, observed in the pitches the synth receives. The
// note / pNote / scale
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

#include <array>
#include <atomic>
#include <string>

#include "support/capilowcov_offline.hpp"
#include "support/timer_pacing.hpp"

#include "yse_c/yse_common.h"
#include "yse_c/yse_music.h"
#include "yse_c/yse_sound.h"
#include "yse_c/yse_synth.h"

namespace {

  // ─── player motif-mode rig (issue #913) ─────────────────────────────────────
  //
  // The three motif-mode probabilities are only observable in the notes the
  // player sends, so the rig records every note-on pitch a synth receives
  // through its note hook (captureless, hence the global). The hook runs on the
  // audio thread; offline, that is this thread inside render_offline, but the
  // counters are atomic anyway.
  std::array<std::atomic<int>, 128> g_motifPitchOns{};

  void YSE_C_CALLBACK recordMotifPitch(int note_on, float* note_number, float* /*velocity*/) {
    if (note_on == 0 || note_number == nullptr) return;
    const int pitch = static_cast<int>(*note_number);
    if (pitch >= 0 && pitch < 128) g_motifPitchOns[pitch].fetch_add(1, std::memory_order_relaxed);
  }

  void resetMotifPitches() {
    for (auto& c : g_motifPitchOns)
      c.store(0, std::memory_order_relaxed);
  }

  int motifPitchOns(int pitch) {
    return g_motifPitchOns[pitch].load(std::memory_order_relaxed);
  }

  int motifNoteOns() {
    int total = 0;
    for (const auto& c : g_motifPitchOns)
      total += c.load(std::memory_order_relaxed);
    return total;
  }

  // Pump until `pred` holds, bounded in reference-timer ticks (issue #753).
  template <typename P> bool pumpUntil(P pred) {
    return TestHelpers::pacedPump(4000, pred, [] { capilowcov::pump(1); }, 0);
  }

  // A synth behind a playing sound, a player bound to it, and one motif:
  //
  //   pitch 60 at 0.0, 67 at 0.1, 64 at 0.2 — each 0.2 s, motif length 0.5
  //
  // The player's pitch range is pinned to exactly 60, so a random note is
  // always 60 and a motif is always transposed to start on 60. Every pitch
  // other than 60 therefore comes from a motif, and which ones arrive says
  // which motif mode the player is in:
  //
  //   * whole motif        60, 67, 64
  //   * partial motif      a piece of it re-based to 60: [60], [60 67] or
  //                        [67] -> [60]. A piece never holds all three notes
  //                        (start in [0, 2), count in [1, size - start)), so 64
  //                        never sounds.
  //   * fitted to {60 + 12k}  67 -> 72, 64 -> 60: 67 never sounds.
  struct MotifRig {
    YseSynth* syn = nullptr;
    YseSound* snd = nullptr;
    YsePlayer* pl = nullptr;
    YseMotif* motif = nullptr;
    YseScale* scale = nullptr;

    bool setUp() {
      syn = yse_synth_create();
      if (syn == nullptr) return false;
      if (yse_synth_add_voices_sine(syn, 8, 0, 0, 127, 0.001f, 0.001f, 1.0f, 0.05f) != YSE_OK)
        return false;
      yse_synth_set_note_callback(syn, &recordMotifPitch);
      snd = yse_sound_create();
      if (snd == nullptr) return false;
      if (yse_synth_attach_to_sound(syn, snd, nullptr, 0.8f) != YSE_OK) return false;
      yse_sound_play(snd);
      if (!pumpUntil([this] { return yse_synth_get_num_voices(syn) >= 8; })) return false;

      pl = yse_player_create(syn);
      if (pl == nullptr) return false;

      motif = yse_motif_create();
      const float pitches[] = {60.f, 67.f, 64.f};
      for (int i = 0; i < 3; ++i) {
        YsePNote* n = yse_pnote_create(0.1f * static_cast<float>(i), pitches[i], 0.8f, 0.2f, 0);
        yse_motif_add(motif, n);
        yse_pnote_destroy(n);
      }
      yse_motif_set_length(motif, 0.5f);

      scale = yse_scale_create();
      yse_scale_add(scale, 60.f, 12.f);

      yse_player_set_minimum_pitch(pl, 60.f, 0.f);
      yse_player_set_maximum_pitch(pl, 60.f, 0.f);
      yse_player_set_minimum_velocity(pl, 0.5f, 0.f);
      yse_player_set_maximum_velocity(pl, 0.9f, 0.f);
      yse_player_set_minimum_gap(pl, 0.f, 0.f);
      yse_player_set_maximum_gap(pl, 0.f, 0.f);
      yse_player_set_minimum_length(pl, 0.05f, 0.f);
      yse_player_set_maximum_length(pl, 0.1f, 0.f);
      yse_player_set_voices(pl, 4, 0.f);
      yse_player_set_scale(pl, scale, 0.f);
      yse_player_add_motif(pl, motif, 1);
      return true;
    }

    // Drain the queued configuration into the player, then start it with a
    // clean record.
    void start() {
      capilowcov::pump(10);
      resetMotifPitches();
      yse_player_play(pl);
    }

    // Enough notes for an absence check to mean something.
    bool heardAtLeast(int notes) {
      return pumpUntil([notes] { return motifNoteOns() >= notes; });
    }

    ~MotifRig() {
      if (pl != nullptr) {
        yse_player_stop(pl);
        yse_player_destroy(pl);
      }
      capilowcov::pump(5); // the player lets go of the motif and scale first
      if (motif != nullptr) yse_motif_destroy(motif);
      if (scale != nullptr) yse_scale_destroy(scale);
      if (snd != nullptr) yse_sound_destroy(snd); // sound before its synth
      if (syn != nullptr) yse_synth_destroy(syn);
      capilowcov::pump(5);
    }
  };

} // namespace

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
    CHECK(yse_pnote_get_channel(n) == 2); // the create() channel (#909)

    yse_pnote_set_position(n, 2.25f);
    yse_pnote_set_pitch(n, 65.0f);
    yse_pnote_set_volume(n, 0.4f);
    yse_pnote_set_length(n, 0.6f);
    yse_pnote_set_channel(n, 7);
    CHECK(yse_pnote_get_position(n) == doctest::Approx(2.25f));
    CHECK(yse_pnote_get_pitch(n) == doctest::Approx(65.0f));
    CHECK(yse_pnote_get_volume(n) == doctest::Approx(0.4f));
    CHECK(yse_pnote_get_length(n) == doctest::Approx(0.6f));
    CHECK(yse_pnote_get_channel(n) == 7);

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
    yse_pnote_set_channel(nullptr, 3);
    CHECK(yse_pnote_get_channel(nullptr) == 0);
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

  // ─── player motif modes (issue #913) ───────────────────────────────────────
  //
  // Each probability is driven at its two ends, 0 and 1, where the outcome is
  // certain rather than statistical: the end that forbids a pitch runs first
  // from a clean start and must never produce it, then the other end must.

  TEST_CASE("c-api player: play_motifs switches between random notes and motifs (#913)") {
    if (!capilowcov::ensureOffline()) return; // engine unavailable → skip
    MotifRig rig;
    REQUIRE(rig.setUp());

    yse_player_play_partial_motifs(rig.pl, 0.f, 0.f);
    yse_player_fit_motifs_to_scale(rig.pl, 0.f, 0.f);
    yse_player_play_motifs(rig.pl, 0.f, 0.f);
    rig.start();

    // 0: random notes only, and the pinned range makes every one of them 60.
    REQUIRE(rig.heardAtLeast(20));
    CHECK(motifNoteOns() == motifPitchOns(60));

    // 1: motifs only — the whole motif, so its other two pitches sound.
    yse_player_play_motifs(rig.pl, 1.f, 0.f);
    CHECK(pumpUntil([] { return motifPitchOns(67) > 0 && motifPitchOns(64) > 0; }));
  }

  TEST_CASE("c-api player: play_partial_motifs plays pieces of a motif (#913)") {
    if (!capilowcov::ensureOffline()) return; // engine unavailable → skip
    MotifRig rig;
    REQUIRE(rig.setUp());

    yse_player_play_motifs(rig.pl, 1.f, 0.f);
    yse_player_fit_motifs_to_scale(rig.pl, 0.f, 0.f);
    yse_player_play_partial_motifs(rig.pl, 1.f, 0.f);
    rig.start();

    // 1: only pieces, and no piece reaches the motif's third note.
    REQUIRE(rig.heardAtLeast(20));
    CHECK(motifPitchOns(64) == 0);

    // 0: whole motifs again, third note included.
    yse_player_play_partial_motifs(rig.pl, 0.f, 0.f);
    CHECK(pumpUntil([] { return motifPitchOns(64) > 0; }));
  }

  TEST_CASE("c-api player: fit_motifs_to_scale snaps motif notes to the scale (#913)") {
    if (!capilowcov::ensureOffline()) return; // engine unavailable → skip
    MotifRig rig;
    REQUIRE(rig.setUp());

    yse_player_play_motifs(rig.pl, 1.f, 0.f);
    yse_player_play_partial_motifs(rig.pl, 0.f, 0.f);
    yse_player_fit_motifs_to_scale(rig.pl, 1.f, 0.f);
    rig.start();

    // 1: every motif note is moved onto the scale's pitch class, so only 60
    // and 72 sound — 67 never does.
    REQUIRE(rig.heardAtLeast(20));
    CHECK(motifPitchOns(67) == 0);
    CHECK(motifPitchOns(64) == 0);
    CHECK(motifNoteOns() == motifPitchOns(60) + motifPitchOns(72));

    // 0: motifs as written.
    yse_player_fit_motifs_to_scale(rig.pl, 0.f, 0.f);
    CHECK(pumpUntil([] { return motifPitchOns(67) > 0; }));
  }

} // TEST_SUITE("capilowcov")

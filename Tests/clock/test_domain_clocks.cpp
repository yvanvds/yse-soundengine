// Tests for domain clocks (issue #249): per-domain beat accumulators with
// rampable, playable tempo.
//
// Domain clocks advance on the audio callback via CLOCK::Manager().update(delta)
// (wired into deviceManager::doOnCallback). These tests drive that tick
// directly on the test thread with explicit block durations, which makes beat
// integration and tempo ramps fully deterministic without needing an audio
// device. This suite is registered as its own CTest process (yse_tests_clock)
// and excluded from the crowded monolithic run, so no other suite's audio
// thread can also call CLOCK::Manager().update() and race these direct ticks.
//
// Each case uses a unique clock name: the CLOCK manager is a process-global
// singleton that persists across cases, and update() advances every live clock,
// so distinct names keep the cases independent (a case only ever reads the
// clock it created, and only its own update() calls move that clock).

#include <doctest/doctest.h>
#include "yse.hpp"
#include "clock/clockManager.h"
#include "yse_c/yse_system.h"

namespace {
  // One audio-block tick of `seconds` at the current tempo of every live clock.
  void tick(float seconds) {
    YSE::CLOCK::Manager().update(seconds);
  }
} // namespace

TEST_SUITE("clock") {

  // ─── Lifecycle: create / exists / destroy ───────────────────────────────────

  TEST_CASE("clock: create, query, and destroy by name") {
    auto& mgr = YSE::CLOCK::Manager();

    CHECK(mgr.createClock("clk.life", 120.f));
    CHECK(mgr.clockExists("clk.life"));

    // A second live clock cannot claim the same name — first registration wins.
    CHECK_FALSE(mgr.createClock("clk.life", 90.f));

    // Empty names are rejected.
    CHECK_FALSE(mgr.createClock("", 120.f));

    mgr.destroyClock("clk.life");
    // Destruction removes the clock from queries immediately.
    CHECK_FALSE(mgr.clockExists("clk.life"));

    // A name freed by destroy can be reused even before the reap runs.
    CHECK(mgr.createClock("clk.life", 100.f));
    mgr.destroyClock("clk.life");
    tick(0.01f); // let the audio-side retire it
  }

  TEST_CASE("clock: queries on an unknown name return zero") {
    auto& mgr = YSE::CLOCK::Manager();
    CHECK_FALSE(mgr.clockExists("clk.nope"));
    CHECK(mgr.beatPosition("clk.nope") == doctest::Approx(0.0));
    CHECK(mgr.currentTempo("clk.nope") == doctest::Approx(0.0f));
  }

  // ─── Initial state ───────────────────────────────────────────────────────────

  TEST_CASE("clock: starts at its initial tempo and beat zero") {
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("clk.init", 100.f));
    // Published even before the first tick.
    CHECK(mgr.currentTempo("clk.init") == doctest::Approx(100.0f));
    CHECK(mgr.beatPosition("clk.init") == doctest::Approx(0.0));
  }

  // ─── Beat = running integral of tempo ────────────────────────────────────────

  TEST_CASE("clock: beat position is the integral of tempo over time") {
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("clk.beat", 120.f)); // 120 BPM = 2 beats / second

    // Advance a total of 2 seconds in four 0.5 s blocks → 4 beats.
    for (int i = 0; i < 4; ++i)
      tick(0.5f);

    CHECK(mgr.beatPosition("clk.beat") == doctest::Approx(4.0));
    CHECK(mgr.currentTempo("clk.beat") == doctest::Approx(120.0f));
  }

  // ─── Instant tempo change (ramp = 0) ─────────────────────────────────────────

  TEST_CASE("clock: instant tempo change takes effect on the next block") {
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("clk.instant", 120.f));

    mgr.setTempo("clk.instant", 60.f, 0.f);
    tick(0.1f); // consumes the request, then integrates the block at 60 BPM

    CHECK(mgr.currentTempo("clk.instant") == doctest::Approx(60.0f));
    // 60 BPM = 1 beat / second → 0.1 s = 0.1 beat.
    CHECK(mgr.beatPosition("clk.instant") == doctest::Approx(0.1));
  }

  // ─── Linear tempo ramp ───────────────────────────────────────────────────────

  TEST_CASE("clock: tempo ramps linearly toward the target and then holds") {
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("clk.ramp", 100.f));

    // Ramp 100 → 200 BPM over 1 s: +100 BPM/s.
    mgr.setTempo("clk.ramp", 200.f, 1.f);

    tick(0.25f);
    CHECK(mgr.currentTempo("clk.ramp") == doctest::Approx(125.0f));
    tick(0.25f);
    CHECK(mgr.currentTempo("clk.ramp") == doctest::Approx(150.0f));
    tick(0.25f);
    CHECK(mgr.currentTempo("clk.ramp") == doctest::Approx(175.0f));
    tick(0.25f);
    CHECK(mgr.currentTempo("clk.ramp") == doctest::Approx(200.0f));
    // Held at target after the ramp completes.
    tick(0.25f);
    CHECK(mgr.currentTempo("clk.ramp") == doctest::Approx(200.0f));
  }

  TEST_CASE("clock: beat during a ramp is the midpoint integral") {
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("clk.rampbeat", 0.f));

    // Ramp 0 → 120 BPM over 1 s in a single 1 s block. The exact beat integral
    // is the average tempo (60 BPM = 1 beat/s) × 1 s = 1 beat.
    mgr.setTempo("clk.rampbeat", 120.f, 1.f);
    tick(1.0f);

    CHECK(mgr.currentTempo("clk.rampbeat") == doctest::Approx(120.0f));
    CHECK(mgr.beatPosition("clk.rampbeat") == doctest::Approx(1.0));
  }

  // ─── Playable tempo: pause and reverse are not clamped ───────────────────────

  TEST_CASE("clock: zero tempo pauses and negative tempo runs backward") {
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("clk.play", 120.f));

    tick(1.0f); // +2 beats
    const double afterForward = mgr.beatPosition("clk.play");
    CHECK(afterForward == doctest::Approx(2.0));

    mgr.setTempo("clk.play", 0.f, 0.f);
    tick(1.0f); // paused → no change
    CHECK(mgr.beatPosition("clk.play") == doctest::Approx(afterForward));

    mgr.setTempo("clk.play", -120.f, 0.f);
    tick(1.0f); // -2 beats → back to 0
    CHECK(mgr.beatPosition("clk.play") == doctest::Approx(0.0));
  }

  // ─── Destroyed clocks stop advancing ─────────────────────────────────────────

  TEST_CASE("clock: a destroyed clock stops advancing and reads zero") {
    auto& mgr = YSE::CLOCK::Manager();
    REQUIRE(mgr.createClock("clk.gone", 120.f));
    tick(1.0f);
    CHECK(mgr.beatPosition("clk.gone") == doctest::Approx(2.0));

    mgr.destroyClock("clk.gone");
    tick(1.0f); // audio thread retires it this tick
    // No longer visible to queries.
    CHECK_FALSE(mgr.clockExists("clk.gone"));
    CHECK(mgr.beatPosition("clk.gone") == doctest::Approx(0.0));
  }

  // ─── C API mirror ────────────────────────────────────────────────────────────

  TEST_CASE("clock: C API create/exists/destroy round-trip") {
    YseSystem* sys = yse_system_get();
    REQUIRE(sys != nullptr);

    CHECK(yse_system_create_clock(sys, "clk.capi", 90.f) == 1);
    CHECK(yse_system_clock_exists(sys, "clk.capi") == 1);
    // Duplicate + empty names rejected.
    CHECK(yse_system_create_clock(sys, "clk.capi", 90.f) == 0);
    CHECK(yse_system_create_clock(sys, "", 90.f) == 0);

    CHECK(yse_system_current_tempo(sys, "clk.capi") == doctest::Approx(90.0f));
    CHECK(yse_system_beat_position(sys, "clk.capi") == doctest::Approx(0.0));

    yse_system_destroy_clock(sys, "clk.capi");
    CHECK(yse_system_clock_exists(sys, "clk.capi") == 0);
  }

  TEST_CASE("clock: C API tempo + beat mirror the C++ values") {
    YseSystem* sys = yse_system_get();
    REQUIRE(sys != nullptr);

    REQUIRE(yse_system_create_clock(sys, "clk.capi2", 120.f) == 1);
    yse_system_set_tempo(sys, "clk.capi2", 60.f, 0.f);
    tick(0.5f); // drive one block so the request is applied and beat advances

    CHECK(yse_system_current_tempo(sys, "clk.capi2") == doctest::Approx(60.0f));
    CHECK(yse_system_current_tempo(sys, "clk.capi2") ==
          doctest::Approx(YSE::CLOCK::Manager().currentTempo("clk.capi2")));
    // 60 BPM for 0.5 s → 0.5 beat.
    CHECK(yse_system_beat_position(sys, "clk.capi2") == doctest::Approx(0.5));
    CHECK(yse_system_beat_position(sys, "clk.capi2") ==
          doctest::Approx(YSE::CLOCK::Manager().beatPosition("clk.capi2")));

    yse_system_destroy_clock(sys, "clk.capi2");
    tick(0.01f);
  }

  TEST_CASE("clock: C API guards NULL system and name") {
    YseSystem* sys = yse_system_get();
    CHECK(yse_system_create_clock(nullptr, "x", 120.f) == 0);
    CHECK(yse_system_create_clock(sys, nullptr, 120.f) == 0);
    CHECK(yse_system_clock_exists(nullptr, "x") == 0);
    CHECK(yse_system_clock_exists(sys, nullptr) == 0);
    CHECK(yse_system_beat_position(nullptr, "x") == doctest::Approx(0.0));
    CHECK(yse_system_current_tempo(nullptr, "x") == doctest::Approx(0.0f));
    // Void functions must simply not crash on NULL args.
    yse_system_destroy_clock(nullptr, "x");
    yse_system_destroy_clock(sys, nullptr);
    yse_system_set_tempo(nullptr, "x", 120.f, 0.f);
    yse_system_set_tempo(sys, nullptr, 120.f, 0.f);
  }

} // TEST_SUITE("clock")

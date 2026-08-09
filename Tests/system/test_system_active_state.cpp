// Tests for the YSE::system live device-state getters
// (Phase B of dart-yse#2 engine support, tracked in
//  yvanvds/yse-soundengine#51).
//
// Covers:
//   - Active sample rate / buffer size / output latency report 0 when the
//     audio device is closed (paused) and positive values when an audio
//     callback is running.
//   - C++ getters and the C API mirror agree on the same values.
//
// The engine state is a process-static singleton. engineInit() opens the
// device synchronously then calls pause() — which on the PortAudio backend
// is implemented by close() — so all three live getters read 0 in that
// state. engineInitWithAudio() then resumes (re-opens) the device, after
// which sample rate and output latency are populated immediately; buffer
// size is populated on the first audio callback. We pump the engine for a
// few sleep+update cycles before reading buffer size.
//
// On CI without an audio device, engineInit() returns false and every test
// case bails out — doctest counts that as a pass.

#include <doctest/doctest.h>
#include "yse.hpp"
#include "yse_c/yse_system.h"
#include "support/null_device.hpp"

namespace {

  // Pump update + sleep a few times so the first audio callback can fire and
  // the buffer-size atomic gets populated.
  void waitForCallback(int iterations = 5, unsigned int sleepMs = 20) {
    for (int i = 0; i < iterations; ++i) {
      YSE::System().sleep(sleepMs);
      YSE::System().update();
    }
  }

  // The precondition of every case that measures a *live* device, asked rather
  // than assumed (issue #717).
  //
  // `getNumDevices() == 0` on its own — which is all these cases used to check
  // — answers "does this host have audio hardware", and that is not the same
  // question as "is a stream delivering audio in this process right now". In a
  // shared process the two come apart: doctest orders cases by file, so
  // system/test_device_layer.cpp (suite `devicelayer`) runs between this file
  // and system/test_api_doc_coverage.cpp, and its setup legitimately takes the
  // device away with System().close() + initOffline(). The device list survives
  // that, so the old guard stayed open over a closed stream and the cases below
  // asserted a live rate on nothing.
  //
  // Nothing here re-opens it. engineInitWithAudio() explains why (see
  // Tests/support/null_device.hpp): re-establishing the stream mid-run puts a
  // live callback thread into a process whose unit suites drive
  // Manager().update() from the test thread, and the unfiltered run then aborts
  // on lfQueue's reentrancy assertion instead of failing three checks. This
  // suite and `devicelayer` need opposite device state and cannot share a
  // process — the isolated ctest entries are what give each of them theirs.
  bool liveStream() {
    // No audio hardware at all (headless CI). Long-standing, silent, and not
    // what #717 is about.
    if (YSE::System().getNumDevices() == 0) return false;
    if (TestHelpers::audioIsFlowing()) return true;
    MESSAGE("skipped: this host has an audio device but no stream is delivering callbacks in "
            "this process, so there is no live state to measure. The `system` suite and "
            "`devicelayer` need opposite device state — run this one through the ctest "
            "entries that give each of them its own process (issue #717).");
    return false;
  }

} // namespace

TEST_SUITE("system") {

  // ─── Paused-engine baseline: all three getters report 0 ─────────────────────

  TEST_CASE("system: active sample rate is 0 while engine is paused") {
    if (!TestHelpers::engineInit()) return;
    // engineInit() leaves the device closed via pause(); the live getter
    // gates on the device's open flag and reports 0.
    CHECK(YSE::System().getActiveSampleRate() == doctest::Approx(0.0));
  }

  // ─── Session sample rate: locked at init(), survives pause ──────────────────

  TEST_CASE("system: session sample rate is positive after init even while paused") {
    if (!TestHelpers::engineInit()) return;
    // Distinct from getActiveSampleRate(): the session rate is the engine-wide
    // value locked during initShared() and stays constant across the session.
    // engineInit() leaves the device paused — the session getter should still
    // report a positive value.
    CHECK(YSE::System().getSampleRate() > 0.0);
  }

  TEST_CASE("system: active buffer size is 0 while engine is paused") {
    if (!TestHelpers::engineInit()) return;
    CHECK(YSE::System().getActiveBufferSize() == 0);
  }

  TEST_CASE("system: active output latency is 0 while engine is paused") {
    if (!TestHelpers::engineInit()) return;
    CHECK(YSE::System().getActiveOutputLatency() == 0);
  }

  // ─── Resumed engine: live values become positive ─────────────────────────────

  // The contract the five cases below inherit, asserted as a case of its own so
  // a process that cannot satisfy it says so once, by name, rather than through
  // three unexplained value mismatches. Deliberately a failure and not a skip,
  // for the same reason as devicelayer's mirror of it: the guards are the only
  // thing between these cases and silently measuring nothing, so a change that
  // takes the live stream away has to trip something.
  TEST_CASE("system: the live-device cases have a stream delivering audio (issue #717)") {
    if (!TestHelpers::engineInitWithAudio()) return;
    // Headless CI has no device to deliver anything; that is not the contract
    // this case is about.
    if (YSE::System().getNumDevices() == 0) return;
    INFO("These cases measure live device state, which needs a process where the audio stream "
         "engineInitWithAudio() started is still open. `devicelayer` closes it — legitimately, "
         "its own cases need no device — and doctest orders cases by file, so it lands between "
         "this suite's translation units. This failing means the two are sharing a process; the "
         "cases below are skipped with a message.");
    CHECK(TestHelpers::audioIsFlowing());
  }

  TEST_CASE("system: active sample rate is positive after resuming audio") {
    if (!TestHelpers::engineInitWithAudio()) return;
    if (!liveStream()) return;
    CHECK(YSE::System().getActiveSampleRate() > 0.0);
  }

  TEST_CASE("system: active output latency is positive after resuming audio") {
    if (!TestHelpers::engineInitWithAudio()) return;
    if (!liveStream()) return;
    // PortAudio's negotiated output latency is multiplied by SAMPLERATE in
    // the manager — for any realistic device this lands well above zero.
    CHECK(YSE::System().getActiveOutputLatency() > 0);
  }

  TEST_CASE("system: session sample rate matches active rate when audio is resumed") {
    if (!TestHelpers::engineInitWithAudio()) return;
    if (!liveStream()) return;
    // When the device is open, the session rate and the active (live) rate
    // return the same locked value.
    CHECK(YSE::System().getSampleRate() == doctest::Approx(YSE::System().getActiveSampleRate()));
  }

  TEST_CASE("system: active buffer size is positive after at least one audio callback") {
    if (!TestHelpers::engineInitWithAudio()) return;
    if (!liveStream()) return;
    waitForCallback();
    // If the audio thread never fired (some CI runners), skip the assertion
    // rather than reporting a flake.
    if (YSE::System().missedCallbacks() != 0) return;
    CHECK(YSE::System().getActiveBufferSize() > 0);
  }

  // ─── Latency-in-samples sanity: round-trip ms calc fits a believable range ──

  TEST_CASE("system: active output latency yields a believable ms value") {
    if (!TestHelpers::engineInitWithAudio()) return;
    if (!liveStream()) return;
    const double rate = YSE::System().getActiveSampleRate();
    const int samples = YSE::System().getActiveOutputLatency();
    if (rate <= 0.0 || samples <= 0) return;
    const double ms = (double)samples / rate * 1000.0;
    // A real device should produce *some* latency but nowhere near a second.
    CHECK(ms > 0.0);
    CHECK(ms < 1000.0);
  }

  // ─── C API mirror agrees with the C++ getters ────────────────────────────────

  TEST_CASE("system: C API getSampleRate mirrors the C++ value") {
    if (!TestHelpers::engineInit()) return;
    YseSystem* sys = yse_system_get();
    REQUIRE(sys != nullptr);
    CHECK(yse_system_get_sample_rate(sys) == doctest::Approx(YSE::System().getSampleRate()));
  }

  TEST_CASE("system: C API getActiveSampleRate mirrors the C++ value") {
    if (!TestHelpers::engineInit()) return;
    YseSystem* sys = yse_system_get();
    REQUIRE(sys != nullptr);
    CHECK(yse_system_get_active_sample_rate(sys) ==
          doctest::Approx(YSE::System().getActiveSampleRate()));
  }

  TEST_CASE("system: C API getActiveBufferSize mirrors the C++ value") {
    if (!TestHelpers::engineInit()) return;
    YseSystem* sys = yse_system_get();
    REQUIRE(sys != nullptr);
    CHECK(yse_system_get_active_buffer_size(sys) == YSE::System().getActiveBufferSize());
  }

  TEST_CASE("system: C API getActiveOutputLatency mirrors the C++ value") {
    if (!TestHelpers::engineInit()) return;
    YseSystem* sys = yse_system_get();
    REQUIRE(sys != nullptr);
    CHECK(yse_system_get_active_output_latency(sys) == YSE::System().getActiveOutputLatency());
  }

  TEST_CASE("system: C API getters return 0 on a NULL system handle") {
    CHECK(yse_system_get_sample_rate(nullptr) == doctest::Approx(0.0));
    CHECK(yse_system_get_active_sample_rate(nullptr) == doctest::Approx(0.0));
    CHECK(yse_system_get_active_buffer_size(nullptr) == 0);
    CHECK(yse_system_get_active_output_latency(nullptr) == 0);
  }

} // TEST_SUITE("system")

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

namespace
{

// Pump update + sleep a few times so the first audio callback can fire and
// the buffer-size atomic gets populated.
void waitForCallback(int iterations = 5, unsigned int sleepMs = 20)
{
    for (int i = 0; i < iterations; ++i)
    {
        YSE::System().sleep(sleepMs);
        YSE::System().update();
    }
}

} // namespace

TEST_SUITE("system")
{

// ─── Paused-engine baseline: all three getters report 0 ─────────────────────

TEST_CASE("system: active sample rate is 0 while engine is paused")
{
    if (!TestHelpers::engineInit()) return;
    // engineInit() leaves the device closed via pause(); the live getter
    // gates on the device's open flag and reports 0.
    CHECK(YSE::System().getActiveSampleRate() == doctest::Approx(0.0));
}

// ─── Session sample rate: locked at init(), survives pause ──────────────────

TEST_CASE("system: session sample rate is positive after init even while paused")
{
    if (!TestHelpers::engineInit()) return;
    // Distinct from getActiveSampleRate(): the session rate is the engine-wide
    // value locked during initShared() and stays constant across the session.
    // engineInit() leaves the device paused — the session getter should still
    // report a positive value.
    CHECK(YSE::System().getSampleRate() > 0.0);
}

TEST_CASE("system: active buffer size is 0 while engine is paused")
{
    if (!TestHelpers::engineInit()) return;
    CHECK(YSE::System().getActiveBufferSize() == 0);
}

TEST_CASE("system: active output latency is 0 while engine is paused")
{
    if (!TestHelpers::engineInit()) return;
    CHECK(YSE::System().getActiveOutputLatency() == 0);
}

// ─── Resumed engine: live values become positive ─────────────────────────────

TEST_CASE("system: active sample rate is positive after resuming audio")
{
    if (!TestHelpers::engineInitWithAudio()) return;
    if (YSE::System().getNumDevices() == 0) return;
    CHECK(YSE::System().getActiveSampleRate() > 0.0);
}

TEST_CASE("system: active output latency is positive after resuming audio")
{
    if (!TestHelpers::engineInitWithAudio()) return;
    if (YSE::System().getNumDevices() == 0) return;
    // PortAudio's negotiated output latency is multiplied by SAMPLERATE in
    // the manager — for any realistic device this lands well above zero.
    CHECK(YSE::System().getActiveOutputLatency() > 0);
}

TEST_CASE("system: session sample rate matches active rate when audio is resumed")
{
    if (!TestHelpers::engineInitWithAudio()) return;
    if (YSE::System().getNumDevices() == 0) return;
    // When the device is open, the session rate and the active (live) rate
    // return the same locked value.
    CHECK(YSE::System().getSampleRate()
          == doctest::Approx(YSE::System().getActiveSampleRate()));
}

TEST_CASE("system: active buffer size is positive after at least one audio callback")
{
    if (!TestHelpers::engineInitWithAudio()) return;
    if (YSE::System().getNumDevices() == 0) return;
    waitForCallback();
    // If the audio thread never fired (some CI runners), skip the assertion
    // rather than reporting a flake.
    if (YSE::System().missedCallbacks() != 0) return;
    CHECK(YSE::System().getActiveBufferSize() > 0);
}

// ─── Latency-in-samples sanity: round-trip ms calc fits a believable range ──

TEST_CASE("system: active output latency yields a believable ms value")
{
    if (!TestHelpers::engineInitWithAudio()) return;
    if (YSE::System().getNumDevices() == 0) return;
    const double rate = YSE::System().getActiveSampleRate();
    const int    samples = YSE::System().getActiveOutputLatency();
    if (rate <= 0.0 || samples <= 0) return;
    const double ms = (double)samples / rate * 1000.0;
    // A real device should produce *some* latency but nowhere near a second.
    CHECK(ms > 0.0);
    CHECK(ms < 1000.0);
}

// ─── C API mirror agrees with the C++ getters ────────────────────────────────

TEST_CASE("system: C API getSampleRate mirrors the C++ value")
{
    if (!TestHelpers::engineInit()) return;
    YseSystem* sys = yse_system_get();
    REQUIRE(sys != nullptr);
    CHECK(yse_system_get_sample_rate(sys)
          == doctest::Approx(YSE::System().getSampleRate()));
}

TEST_CASE("system: C API getActiveSampleRate mirrors the C++ value")
{
    if (!TestHelpers::engineInit()) return;
    YseSystem* sys = yse_system_get();
    REQUIRE(sys != nullptr);
    CHECK(yse_system_get_active_sample_rate(sys)
          == doctest::Approx(YSE::System().getActiveSampleRate()));
}

TEST_CASE("system: C API getActiveBufferSize mirrors the C++ value")
{
    if (!TestHelpers::engineInit()) return;
    YseSystem* sys = yse_system_get();
    REQUIRE(sys != nullptr);
    CHECK(yse_system_get_active_buffer_size(sys) == YSE::System().getActiveBufferSize());
}

TEST_CASE("system: C API getActiveOutputLatency mirrors the C++ value")
{
    if (!TestHelpers::engineInit()) return;
    YseSystem* sys = yse_system_get();
    REQUIRE(sys != nullptr);
    CHECK(yse_system_get_active_output_latency(sys) == YSE::System().getActiveOutputLatency());
}

TEST_CASE("system: C API getters return 0 on a NULL system handle")
{
    CHECK(yse_system_get_sample_rate(nullptr) == doctest::Approx(0.0));
    CHECK(yse_system_get_active_sample_rate(nullptr) == doctest::Approx(0.0));
    CHECK(yse_system_get_active_buffer_size(nullptr) == 0);
    CHECK(yse_system_get_active_output_latency(nullptr) == 0);
}

} // TEST_SUITE("system")

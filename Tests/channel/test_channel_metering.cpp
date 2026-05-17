// Tests for the channel output peak-metering API (Phase A of dart-yse#1
// engine support, tracked in yvanvds/yse-soundengine#50).
//
// Covers the API surface on YSE::channel:
//   - getNumOutputs(): 0 on default-constructed channel, > 0 after init.
//   - getPeakLinear*() / getPeakDb*(): silent-state values (linear == 0,
//     dB == -120 floor) on a paused-audio engine.
//   - Bounds: out-of-range outputIdx returns 0 (linear) / -120 (dB).
//   - Pre-built channel parity: master/FX/music/ambient/voice/gui all
//     report a consistent getNumOutputs.
//
// Live-audio metering (volume = 0 muting post peak, sine source producing
// non-zero peak) is covered by an integration-style test that only runs
// under engineInitWithAudio() — skipped on CI without a default device.

#include <doctest/doctest.h>
#include <chrono>
#include <thread>
#include "channel/channelInterface.hpp"
#include "channel/channelManager.h"
#include "sound/soundManager.h"
#include "internal/time.h"
#include "support/null_device.hpp"

namespace
{
// The dB floor returned for silent / out-of-range peaks. Kept in sync with
// the linearToDb helper in channelInterface.cpp.
constexpr float kDbFloor = -120.f;

// Channel setup is async: c.create() queues setup() onto the slow-pool;
// the audio-thread-side promote-from-toLoad pass then sizes `out`. Pump
// both managers a few times so freshly created channels reach OBJECT_READY
// and getNumOutputs() reflects the device layout.
void drainChannels(int iterations = 8)
{
	for (int i = 0; i < iterations; ++i)
	{
		YSE::INTERNAL::Time().update();
		YSE::SOUND::Manager().update();
		YSE::CHANNEL::Manager().update();
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
}
} // namespace

TEST_SUITE("channel")
{

	// ─── Default-constructed channel: zero outputs, zero peaks ──────────────────

	TEST_CASE("channel metering: default-constructed channel reports zero outputs")
	{
		YSE::channel c;
		CHECK(c.getNumOutputs() == 0);
	}

	TEST_CASE("channel metering: default-constructed channel returns 0 linear peak")
	{
		YSE::channel c;
		CHECK(c.getPeakLinearPre() == doctest::Approx(0.f));
		CHECK(c.getPeakLinearPost() == doctest::Approx(0.f));
	}

	TEST_CASE("channel metering: default-constructed channel returns -120 dB peak")
	{
		YSE::channel c;
		CHECK(c.getPeakDbPre() == doctest::Approx(kDbFloor));
		CHECK(c.getPeakDbPost() == doctest::Approx(kDbFloor));
	}

	TEST_CASE(
		"channel metering: default-constructed channel returns 0 / -120 for any per-output index")
	{
		YSE::channel c;
		CHECK(c.getPeakLinearPre(0) == doctest::Approx(0.f));
		CHECK(c.getPeakLinearPost(0) == doctest::Approx(0.f));
		CHECK(c.getPeakDbPre(0) == doctest::Approx(kDbFloor));
		CHECK(c.getPeakDbPost(0) == doctest::Approx(kDbFloor));
	}

	// ─── After engine init: master channel has outputs, peaks are silent ─────────

	TEST_CASE("channel metering: master channel reports non-zero output count after init")
	{
		if (!TestHelpers::engineInit())
			return;
		CHECK(YSE::ChannelMaster().getNumOutputs() > 0);
	}

	TEST_CASE("channel metering: master channel peaks are silent under paused audio")
	{
		if (!TestHelpers::engineInit())
			return;
		// Audio thread is paused by engineInit(); dsp()/buffersToParent() never
		// ran, so the published peaks remain at their zero-initialised state.
		CHECK(YSE::ChannelMaster().getPeakLinearPre() == doctest::Approx(0.f));
		CHECK(YSE::ChannelMaster().getPeakLinearPost() == doctest::Approx(0.f));
		CHECK(YSE::ChannelMaster().getPeakDbPre() == doctest::Approx(kDbFloor));
		CHECK(YSE::ChannelMaster().getPeakDbPost() == doctest::Approx(kDbFloor));
	}

	TEST_CASE("channel metering: pre-built channels report identical output count")
	{
		if (!TestHelpers::engineInit())
			return;
		// Master is set up synchronously (setMaster), but the five leaf channels
		// are created the same way as user channels — their setup() runs on the
		// slow-pool. Drain so they've reached OBJECT_READY and `out` is sized.
		drainChannels();
		const int n = YSE::ChannelMaster().getNumOutputs();
		CHECK(YSE::ChannelFX().getNumOutputs() == n);
		CHECK(YSE::ChannelMusic().getNumOutputs() == n);
		CHECK(YSE::ChannelAmbient().getNumOutputs() == n);
		CHECK(YSE::ChannelVoice().getNumOutputs() == n);
		CHECK(YSE::ChannelGui().getNumOutputs() == n);
	}

	TEST_CASE("channel metering: pre-built channels start silent")
	{
		if (!TestHelpers::engineInit())
			return;
		CHECK(YSE::ChannelFX().getPeakLinearPost() == doctest::Approx(0.f));
		CHECK(YSE::ChannelMusic().getPeakLinearPost() == doctest::Approx(0.f));
		CHECK(YSE::ChannelAmbient().getPeakLinearPost() == doctest::Approx(0.f));
		CHECK(YSE::ChannelVoice().getPeakLinearPost() == doctest::Approx(0.f));
		CHECK(YSE::ChannelGui().getPeakLinearPost() == doctest::Approx(0.f));
	}

	// ─── User-created channel inherits the device's output layout ───────────────

	TEST_CASE("channel metering: user-created channel inherits the master's output count")
	{
		if (!TestHelpers::engineInit())
			return;
		YSE::channel c;
		c.create("metering_test_channel", YSE::ChannelFX());
		// setup() runs on the slow-pool — drain until the impl reaches OBJECT_READY
		// and `out` has been sized from CHANNEL::Manager().getNumberOfOutputs().
		drainChannels();
		CHECK(c.getNumOutputs() == YSE::ChannelMaster().getNumOutputs());
		CHECK(c.getPeakLinearPost() == doctest::Approx(0.f));
	}

	// ─── Out-of-range output indices return safe defaults ───────────────────────

	TEST_CASE("channel metering: negative outputIdx returns 0 / -120")
	{
		if (!TestHelpers::engineInit())
			return;
		CHECK(YSE::ChannelMaster().getPeakLinearPre(-1) == doctest::Approx(0.f));
		CHECK(YSE::ChannelMaster().getPeakLinearPost(-1) == doctest::Approx(0.f));
		CHECK(YSE::ChannelMaster().getPeakDbPre(-1) == doctest::Approx(kDbFloor));
		CHECK(YSE::ChannelMaster().getPeakDbPost(-1) == doctest::Approx(kDbFloor));
	}

	TEST_CASE("channel metering: outputIdx past the end returns 0 / -120")
	{
		if (!TestHelpers::engineInit())
			return;
		const int oob = YSE::ChannelMaster().getNumOutputs() + 32;
		CHECK(YSE::ChannelMaster().getPeakLinearPre(oob) == doctest::Approx(0.f));
		CHECK(YSE::ChannelMaster().getPeakLinearPost(oob) == doctest::Approx(0.f));
		CHECK(YSE::ChannelMaster().getPeakDbPre(oob) == doctest::Approx(kDbFloor));
		CHECK(YSE::ChannelMaster().getPeakDbPost(oob) == doctest::Approx(kDbFloor));
	}

	TEST_CASE("channel metering: per-output peaks at valid indices match the silent state")
	{
		if (!TestHelpers::engineInit())
			return;
		const int n = YSE::ChannelMaster().getNumOutputs();
		REQUIRE(n > 0);
		for (int i = 0; i < n; ++i)
		{
			CHECK(YSE::ChannelMaster().getPeakLinearPost(i) == doctest::Approx(0.f));
			CHECK(YSE::ChannelMaster().getPeakDbPost(i) == doctest::Approx(kDbFloor));
		}
	}

} // TEST_SUITE("channel")

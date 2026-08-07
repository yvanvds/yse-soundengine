// Regression coverage for issue #660: a zero-length update tick must not
// poison the doppler playback rate with NaN and freeze the playhead.
//
// INTERNAL::Time()'s delta came from std::clock(), which is millisecond
// quantised on the MSVC / MSYS2 runtimes. Two engine update ticks inside the
// same millisecond therefore reported delta == 0 (issue #667 has since moved
// the clock to a monotonic steady_clock, which makes that rare rather than
// routine — the guard under test here is the backstop either way), and
// implementationObject::update() divided by it unconditionally:
//
//   velocityVec = (newPos - lastPos) * (1 / 0)   ->   (0,0,0) * inf   ->   NaN
//
// The stationary source is the case that breaks, which is what made the bug
// look arbitrary. From there nothing rejects the NaN — the "did anything move"
// guard, computeDopplerRatio's clamps and abstractSoundFile::read()'s playhead
// recalibration are all ordinary comparisons, and every one of them is false
// for NaN. So `pos += speed` latched filePtr at NaN permanently and the render
// loop kept indexing the source with (UInt)NaN on the audio thread.
//
// This is the user-visible level: a real sound on a real channel, rendered
// offline through the whole mix path, with doppler left ON (the default — the
// #658 coverage had to set doppler(false) to get a deterministic playhead,
// which is precisely this bug in its workaround form). The pure guards are
// unit-tested in Tests/dsp/test_panner.cpp; the listener's identical divide in
// Tests/listener/test_listener.cpp.
//
// Fail-without-fix (verified against the unpatched engine on Windows): the
// tight render loop below produces zero-length ticks within a few iterations,
// after which s.time() is NaN — std::isfinite fails and the playhead never
// advances again.
//
// Platform note: the zero-length tick needs a coarse clock to be observable, so
// the loop reports whether one actually occurred. The assertions hold either
// way — a finite, advancing playhead is required on every platform — so the
// case is meaningful (if less pointed) where the clock is finer grained, which
// after #667 is everywhere.

#include <doctest/doctest.h>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include "yse.hpp"
#include "channel/channelInterface.hpp"
#include "dsp/buffer.hpp"
#include "internal/time.h"
#include "sound/soundInterface.hpp"
#include "support/null_device.hpp"

namespace {

  // Long enough that the render loop below cannot reach the end of a
  // non-looping sound, so "the playhead advanced and did not wrap" stays an
  // unambiguous readout.
  const UInt kDopplerFrames = 65536;

  // File scope on purpose: soundInterface.hpp's buffer overloads require the
  // buffer to outlive the sound, and SOUND::Manager keeps the soundFile (which
  // holds a raw MULTICHANNELBUFFER*) in its dedup list well past the end of the
  // test case. Same reasoning as test_sound_multichannel_buffer.cpp.
  MULTICHANNELBUFFER g_dopplerSource(1);

} // namespace

TEST_SUITE("sound") {

  TEST_CASE("sound: a zero-length update tick leaves the playhead finite and moving (#660)") {
    if (!TestHelpers::engineInit()) return;

    g_dopplerSource[0].resize(kDopplerFrames);
    {
      Flt* p = g_dopplerSource[0].getPtr();
      for (UInt n = 0; n < kDopplerFrames; n++)
        p[n] = 0.5f; // constant DC — the channel peak is a clean "is it rendering"
    }

    // Dedicated child channel so the peak below reflects only this sound.
    YSE::channel ch;
    ch.create("doppler660", YSE::ChannelMaster());

    YSE::sound s;
    s.create(g_dopplerSource, &ch, /*loop=*/false, /*volume=*/0.8f);
    if (!s.isValid()) return;
    s.relative(true); // pan straight from position, listener orientation ignored
    s.size(10.f); // near field: rolloff clamps to 1
    s.pos(YSE::Pos(0.f, 0.f, 1.f)); // stationary — the case a zero tick NaNs
    // doppler() is deliberately left at its default (on): it is the path under test.
    s.play();

    // Settle with sleeps so the async slow-pool setup can promote the sound.
    // These ticks are milliseconds apart, so the velocity divide is well fed.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline && !s.isReady()) {
      YSE::System().update();
      YSE::System().renderOffline(2);
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(s.isReady());
    REQUIRE(s.length() == kDopplerFrames);

    // A few fed ticks first, so the playhead is demonstrably moving before the
    // zero-length ticks start.
    for (int i = 0; i < 4; i++) {
      YSE::System().update();
      YSE::System().renderOffline(2);
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    const float before = s.time();
    REQUIRE(std::isfinite(before));

    // The repro: pump with NO sleep, so consecutive control ticks land inside
    // the same millisecond and Time().delta() measures 0.
    bool sawZeroTick = false;
    for (int i = 0; i < 60; i++) {
      YSE::System().update();
      YSE::System().renderOffline(2);
      if (YSE::INTERNAL::Time().delta() == 0.f) sawZeroTick = true;
    }

    INFO("zero-length update tick observed: " << sawZeroTick);
    const float after = s.time();
    CHECK(std::isfinite(after)); // pre-fix: NaN, latched for the rest of playback
    CHECK(after > before); // pre-fix: frozen — NaN is not > anything
    CHECK(after < static_cast<float>(kDopplerFrames)); // no wrap: still the first pass
    CHECK(s.isPlaying());
    CHECK(ch.getPeakLinearPre(0) > 0.01f);

    s.stop();
  }

} // TEST_SUITE("sound")

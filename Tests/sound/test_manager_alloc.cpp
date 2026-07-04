// Engine-level RT-allocation test for the manager bookkeeping (issue #194).
//
// This drives the *real* engine — real channels, real sound impls, the real
// message pipe and the real SOUND/CHANNEL manager update() functions — rather
// than a data structure in isolation, so it exercises the actual audio-thread
// paths (promote → connect, sync → reparent, release → disconnect) that used to
// churn std::forward_list nodes on the heap.
//
// Like every other engine test here it drives Manager().update() from the test
// thread with the audio stream paused (see support/null_device.hpp), so it only
// runs where a default audio device is present and skips gracefully otherwise
// (headless CI). Locally it fails on the pre-#194 code (per-move push_front
// allocations) and passes after the intrusive-list conversion.

#include <doctest/doctest.h>

#include <chrono>
#include <thread>
#include <vector>

#include "yse.hpp"
#include "channel/channelInterface.hpp"
#include "channel/channelManager.h"
#include "sound/soundInterface.hpp"
#include "sound/soundManager.h"
#include "dsp/dspObject.hpp"
#include "internal/time.h"
#include "support/alloc_probe.hpp"
#include "support/null_device.hpp"

using namespace std::chrono_literals;

namespace {

  struct SilentSource : YSE::DSP::dspSourceObject {
    void process(YSE::SOUND_STATUS&) override {}
    void frequency(float) override {}
  };

  // Outlives every sound impl created in this TU (see test_sound_impl.cpp for
  // the source-lifetime rationale).
  SilentSource g_src;

  // One pump of the manager update()s the audio thread would normally drive.
  void pump(int n = 1) {
    for (int i = 0; i < n; ++i) {
      YSE::INTERNAL::Time().update();
      YSE::CHANNEL::Manager().update();
      YSE::SOUND::Manager().update();
      std::this_thread::sleep_for(2ms);
    }
  }

} // namespace

TEST_SUITE("sound") {

  TEST_CASE(
      "managers: reparenting sounds churns the audio-thread lists without allocating (#194)") {
    if (!TestHelpers::engineInit()) return;

    // Two user channels to shuttle sounds between.
    YSE::channel chA;
    YSE::channel chB;
    chA.create("allocTestA", YSE::ChannelFX());
    chB.create("allocTestB", YSE::ChannelFX());

    // A handful of DSP-source sounds, all parented to chA to start.
    constexpr int kSounds = 6;
    std::vector<YSE::sound> sounds(kSounds);
    for (auto& s : sounds)
      s.create(g_src, &chA);

    // Settle everything OUTSIDE the probe: channels promote into the manager's
    // inUse list, the slow-pool sets each sound up, and the sounds promote and
    // connect into chA's `sounds` list. After this the slow pool is idle, so the
    // probed region below only sees the audio-thread manager work.
    pump(24);

    {
      TestHelpers::ProbeScope probe;
      // Repeatedly reparent every sound between chA and chB. Each move fires a
      // MOVE message that the manager's sync() turns into a disconnect from the
      // old parent's `sounds` list and a push_front into the new one — the exact
      // per-tick list churn that allocated a forward_list node before #194.
      for (int round = 0; round < 20; ++round) {
        YSE::channel& target = (round % 2 == 0) ? chB : chA;
        for (auto& s : sounds)
          s.moveTo(target);
        pump(2);
      }
      // A few pure steady-state pumps with no structural change at all.
      pump(4);

      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    // Sanity: the engine survived the churn and the sounds are still usable.
    for (auto& s : sounds)
      CHECK(s.isValid());

    // Tear the sounds down and let the release/disconnect path run — must not
    // crash or corrupt the lists (functional check, not an allocation one:
    // destruction legitimately frees the canonical `implementations` nodes on
    // the slow pool).
    for (auto& s : sounds)
      s.stop();
    pump(8);
  }

} // TEST_SUITE("sound")

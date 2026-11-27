// Tests for DSP::voiceAllocator (issue #1040): the note-to-voice policy that
// `.poly` and `~poly` share, usable without a patcher. Free voices first, the
// lowest one; the oldest when stealing; none otherwise; a busy test the caller
// passes; releases by pitch, oldest first, over the whole capacity; and no
// allocation after construction.

#include <doctest/doctest.h>
#include <utility>
#include <vector>
#include "dsp/routing/voiceAllocator.hpp"
#include "support/alloc_probe.hpp"

using YSE::DSP::voiceAllocator;

TEST_SUITE("dsp") {

  TEST_CASE("voiceAllocator: a free voice, the lowest first; none when full (#1040)") {
    voiceAllocator pool(4);
    CHECK(pool.capacity() == 4);
    CHECK(pool.voices() == 4);
    CHECK_FALSE(pool.steal());
    CHECK(pool.noteOn(60).voice == 0);
    CHECK(pool.noteOn(64).voice == 1);
    CHECK(pool.noteOn(67).voice == 2);
    CHECK(pool.noteOn(72).voice == 3);
    CHECK(pool.heldCount() == 4);
    const voiceAllocator::allocation none = pool.noteOn(76);
    CHECK(none.voice == voiceAllocator::NO_VOICE);
    CHECK_FALSE(none.stolen);
    CHECK(pool.heldCount() == 4);
    // A release frees that voice, and it is the lowest free one again.
    CHECK(pool.noteOff(64) == 1);
    CHECK_FALSE(pool.holds(1));
    CHECK(pool.pitch(1) == -1);
    CHECK(pool.noteOn(76).voice == 1);
    CHECK(pool.pitch(1) == 76);
    CHECK(pool.noteOff(99) == voiceAllocator::NO_VOICE);
  }

  TEST_CASE("voiceAllocator: stealing takes the voice started longest ago (#1040)") {
    voiceAllocator pool(3);
    pool.steal(true);
    pool.noteOn(60);
    pool.noteOn(62);
    pool.noteOn(64);
    // 62 is released and replayed: now 60 is the oldest, then 64, then 62.
    pool.noteOff(62);
    pool.noteOn(62);
    const voiceAllocator::allocation a = pool.noteOn(70);
    CHECK(a.voice == 0);
    CHECK(a.stolen);
    CHECK(a.stolenPitch == 60);
    const voiceAllocator::allocation b = pool.noteOn(71);
    CHECK(b.voice == 2);
    CHECK(b.stolenPitch == 64);
    CHECK(pool.heldCount() == 3);
    // The stolen note is gone: its release finds no voice.
    CHECK(pool.noteOff(60) == voiceAllocator::NO_VOICE);
  }

  TEST_CASE("voiceAllocator: a busy voice is skipped, and stolen last-started-first (#1040)") {
    voiceAllocator pool(4);
    std::vector<bool> busy = {true, false, true, false};
    const auto isBusy = [&busy](int v) { return busy[static_cast<std::size_t>(v)]; };
    CHECK(pool.choose(isBusy) == 1);
    CHECK(pool.noteOn(60, isBusy).voice == 1);
    CHECK(pool.noteOn(62, isBusy).voice == 3);
    CHECK(pool.noteOn(64, isBusy).voice == voiceAllocator::NO_VOICE);
    // Started without a pitch (choose + touch): busy, never held.
    busy[0] = false;
    const int v = pool.choose(isBusy);
    CHECK(v == 0);
    pool.touch(v);
    CHECK_FALSE(pool.holds(0));
    busy[0] = true;
    // All busy or held. Stealing takes the oldest start: voice 2 was never
    // started, so it is older than every voice that was.
    pool.steal(true);
    CHECK(pool.choose(isBusy) == 2);
    pool.touch(2);
    CHECK(pool.choose(isBusy) == 1);
  }

  TEST_CASE("voiceAllocator: a skipped voice is neither free nor stolen (#1294)") {
    voiceAllocator pool(3);
    const auto none = [](int) { return false; };
    std::vector<bool> skipped = {true, false, false};
    const auto isSkipped = [&skipped](int v) { return skipped[static_cast<std::size_t>(v)]; };
    // Voice 0 is free but skipped: the next one.
    CHECK(pool.choose(none, isSkipped) == 1);
    CHECK(pool.noteOn(60, none, isSkipped).voice == 1);
    CHECK(pool.noteOn(62, none, isSkipped).voice == 2);
    CHECK(pool.noteOn(64, none, isSkipped).voice == voiceAllocator::NO_VOICE);
    // Stealing: voice 0 was never started, so it is the oldest; skipped, it
    // is passed over for the oldest voice that is not, voice 1.
    pool.steal(true);
    const voiceAllocator::allocation a = pool.noteOn(66, none, isSkipped);
    CHECK(a.voice == 1);
    CHECK(a.stolenPitch == 60);
    // Unskipped, it is chosen again.
    skipped[0] = false;
    CHECK(pool.choose(none, isSkipped) == 0);
    // Every voice skipped: nothing, even with stealing on.
    skipped = {true, true, true};
    CHECK(pool.choose(none, isSkipped) == voiceAllocator::NO_VOICE);
    CHECK(pool.noteOn(68, none, isSkipped).voice == voiceAllocator::NO_VOICE);
    // A skipped voice still releases what it holds.
    CHECK(pool.noteOff(62) == 2);
  }

  TEST_CASE("voiceAllocator: a pitch held twice is released oldest first (#1040)") {
    voiceAllocator pool(4);
    CHECK(pool.noteOn(60).voice == 0);
    CHECK(pool.noteOn(60).voice == 1);
    CHECK(pool.noteOff(60) == 0);
    CHECK(pool.noteOff(60) == 1);
    CHECK(pool.noteOff(60) == voiceAllocator::NO_VOICE);
  }

  TEST_CASE("voiceAllocator: the size bounds allocation, not releases (#1040)") {
    voiceAllocator pool(8);
    pool.voices(0);
    CHECK(pool.voices() == 1);
    pool.voices(100);
    CHECK(pool.voices() == 8);
    for (int p = 0; p < 6; p++)
      pool.noteOn(60 + p);
    pool.voices(2);
    CHECK(pool.noteOn(80).voice == voiceAllocator::NO_VOICE);
    // A note above the new size is still released.
    CHECK(pool.noteOff(65) == 5);
    std::vector<std::pair<int, int>> released;
    CHECK(pool.releaseAll([&](int v, int p) { released.emplace_back(v, p); }) == 5);
    const std::vector<std::pair<int, int>> expected = {{0, 60}, {1, 61}, {2, 62}, {3, 63}, {4, 64}};
    CHECK(released == expected);
    CHECK(pool.heldCount() == 0);
    // Any int is a pitch.
    pool.voices(8);
    CHECK(pool.noteOn(-5000).voice == 0);
    CHECK(pool.pitch(0) == -5000);
    pool.reset();
    CHECK(pool.heldCount() == 0);
    CHECK(pool.voices() == 8);
    CHECK(pool.capacity() == 8);
    voiceAllocator tiny(0);
    CHECK(tiny.capacity() == 1);
  }

  TEST_CASE("voiceAllocator: nothing allocates after the constructor (#1040)") {
    voiceAllocator pool(16);
    pool.steal(true);
    TestHelpers::ProbeScope probe;
    for (int i = 0; i < 1000; i++) {
      pool.noteOn(i % 40, [i](int v) { return ((v + i) % 3) == 0; });
      if ((i % 3) == 0) pool.noteOff((i / 2) % 40);
      if ((i % 97) == 0) pool.releaseAll([](int, int) {});
      pool.touch(pool.choose());
    }
    CHECK(TestHelpers::g_alloc_count.load() == 0);
  }

} // TEST_SUITE("dsp")

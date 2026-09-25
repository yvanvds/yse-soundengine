// Voice-slice policy tests (issue #860, epic #856).
//
// A channel's sounds are partitioned into voice slices: stable containers, one
// render-graph leaf each, every slice accumulating into its own buffers and the
// channel's mix task summing them in fixed slice order. These cases pin the
// control half of that design, driven directly on CHANNEL::implementationObject
// with bare SOUND impls (the pattern test_channel_dsp uses): a sound joins the
// least-loaded slice, a new slice opens only when every active one is at
// capacity, a slice closes only empty and with hysteresis, and moving a sound
// never links it into two lists. The render half — slices rendering in
// parallel and summing bit-exactly — is the #857 golden test's swarm channel
// and the churn stress case in Tests/channel/test_render_golden.cpp.
//
// Nothing here renders, so no engine session is needed: connect()/disconnect()
// touch only the audio-thread lists and the render graph's dirty flag.

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "channel/channelImplementation.h"
#include "channel/channelMessage.h"
#include "sound/soundImplementation.h"
#include "sound/soundMessage.h"

namespace {

  using YSE::CHANNEL::MAX_SLICES;
  using YSE::CHANNEL::SLICE_CAPACITY;
  using ChannelImpl = YSE::CHANNEL::implementationObject;
  using SoundImpl = YSE::SOUND::implementationObject;

  // A pool of never-set-up sound impls. Their destructors skip the parent
  // disconnect (connectedToParent is only set by the audio thread's
  // doThisWhenReady), so each case disconnects what it connected.
  struct Sounds {
    explicit Sounds(int n) {
      for (int i = 0; i < n; ++i)
        items.push_back(std::make_unique<SoundImpl>(nullptr));
    }
    SoundImpl* operator[](int i) {
      return items[static_cast<std::size_t>(i)].get();
    }
    std::vector<std::unique_ptr<SoundImpl>> items;
  };

  int sumOfLoads(const ChannelImpl& ch) {
    int total = 0;
    for (int i = 0; i < MAX_SLICES; ++i)
      total += ch.getSliceLoad(i);
    return total;
  }

} // namespace

TEST_SUITE("channel") {

  TEST_CASE("voice slices: one slice up to capacity, a new one only when every slice is full") {
    ChannelImpl ch(nullptr);
    Sounds s(3 * SLICE_CAPACITY + 1);
    CHECK(ch.getActiveSlices() == 1);

    for (int i = 0; i < SLICE_CAPACITY; ++i)
      ch.connect(s[i]);
    CHECK(ch.getActiveSlices() == 1); // a full slice is not yet a reason to split
    CHECK(ch.getSliceLoad(0) == SLICE_CAPACITY);

    ch.connect(s[SLICE_CAPACITY]);
    CHECK(ch.getActiveSlices() == 2);
    CHECK(ch.getSliceLoad(1) == 1);

    for (int i = SLICE_CAPACITY + 1; i < 3 * SLICE_CAPACITY + 1; ++i)
      ch.connect(s[i]);
    CHECK(ch.getActiveSlices() == 4);
    CHECK(ch.getSliceLoad(0) == SLICE_CAPACITY);
    CHECK(ch.getSliceLoad(1) == SLICE_CAPACITY);
    CHECK(ch.getSliceLoad(2) == SLICE_CAPACITY);
    CHECK(ch.getSliceLoad(3) == 1);
    CHECK(ch.getSoundCount() == 3 * SLICE_CAPACITY + 1);
    CHECK(sumOfLoads(ch) == ch.getSoundCount());

    for (int i = 0; i < 3 * SLICE_CAPACITY + 1; ++i)
      CHECK(ch.disconnect(s[i]));
    CHECK(ch.getSoundCount() == 0);
    CHECK(ch.getActiveSlices() == 1); // every slice emptied, so all but slice 0 close
  }

  TEST_CASE("voice slices: a sound joins the least-loaded slice, without opening a new one") {
    ChannelImpl ch(nullptr);
    Sounds s(2 * SLICE_CAPACITY + 16);
    for (int i = 0; i < 2 * SLICE_CAPACITY + 6; ++i)
      ch.connect(s[i]);
    REQUIRE(ch.getActiveSlices() == 3);
    REQUIRE(ch.getSliceLoad(2) == 6);

    // Swarm churn: voices leave slice 0 (the first SLICE_CAPACITY connected)
    // and new ones arrive. The arrivals fill the lightest slice — slice 2 until
    // it catches up with slice 0 — and never change the slice count, which is
    // what would rebuild the render graph.
    for (int i = 0; i < 10; ++i)
      ch.disconnect(s[i]);
    CHECK(ch.getSliceLoad(0) == SLICE_CAPACITY - 10);
    for (int i = 2 * SLICE_CAPACITY + 6; i < 2 * SLICE_CAPACITY + 16; ++i)
      ch.connect(s[i]);
    CHECK(ch.getActiveSlices() == 3);
    CHECK(ch.getSliceLoad(2) == 16);
    CHECK(ch.getSliceLoad(0) == SLICE_CAPACITY - 10);
    CHECK(sumOfLoads(ch) == ch.getSoundCount());

    for (int i = 10; i < 2 * SLICE_CAPACITY + 16; ++i)
      ch.disconnect(s[i]);
    CHECK(ch.getSoundCount() == 0);
  }

  TEST_CASE("voice slices: a slice closes only empty, with half a slice of hysteresis") {
    ChannelImpl ch(nullptr);
    Sounds s(SLICE_CAPACITY + 1);
    for (int i = 0; i < SLICE_CAPACITY + 1; ++i)
      ch.connect(s[i]);
    REQUIRE(ch.getActiveSlices() == 2);
    REQUIRE(ch.getSliceLoad(1) == 1);

    // One voice hovering across the boundary: leaving empties slice 1, but
    // SLICE_CAPACITY sounds do not fit one slice with slack, so it stays open
    // and the voice's return reuses it. No open/close per connect.
    SoundImpl* hover = s[SLICE_CAPACITY];
    for (int round = 0; round < 4; ++round) {
      ch.disconnect(hover);
      CHECK(ch.getActiveSlices() == 2);
      CHECK(ch.getSliceLoad(1) == 0);
      ch.connect(hover);
      CHECK(ch.getActiveSlices() == 2);
      CHECK(ch.getSliceLoad(1) == 1);
    }

    // Drain to half a slice with slice 1 empty: now it closes.
    ch.disconnect(hover);
    for (int i = 0; i < SLICE_CAPACITY / 2 - 1; ++i) {
      ch.disconnect(s[i]);
      CHECK(ch.getActiveSlices() == 2);
    }
    ch.disconnect(s[SLICE_CAPACITY / 2 - 1]);
    CHECK(ch.getSoundCount() == SLICE_CAPACITY / 2);
    CHECK(ch.getActiveSlices() == 1);

    for (int i = SLICE_CAPACITY / 2; i < SLICE_CAPACITY; ++i)
      ch.disconnect(s[i]);
    CHECK(ch.getSoundCount() == 0);
  }

  TEST_CASE("voice slices: past MAX_SLICES the capacity is soft and the load stays balanced") {
    ChannelImpl ch(nullptr);
    const int n = MAX_SLICES * SLICE_CAPACITY + MAX_SLICES + 5;
    Sounds s(n);
    for (int i = 0; i < n; ++i)
      ch.connect(s[i]);
    CHECK(ch.getActiveSlices() == MAX_SLICES);
    int lo = n;
    int hi = 0;
    for (int i = 0; i < MAX_SLICES; ++i) {
      lo = std::min(lo, ch.getSliceLoad(i));
      hi = std::max(hi, ch.getSliceLoad(i));
    }
    CHECK(hi - lo <= 1);
    CHECK(sumOfLoads(ch) == n);
    CHECK(ch.getSliceLoad(-1) == -1);
    CHECK(ch.getSliceLoad(MAX_SLICES) == -1);
    for (int i = 0; i < n; ++i)
      ch.disconnect(s[i]);
  }

  TEST_CASE(
      "voice slices: moving a sound unlinks it from its old slice, even on the same channel") {
    ChannelImpl a(nullptr);
    ChannelImpl b(nullptr);
    Sounds s(3);
    a.connect(s[0]);
    a.connect(s[1]);
    REQUIRE(a.getSoundCount() == 2);

    b.connect(s[0]); // a sound's MOVE message
    CHECK(a.getSoundCount() == 1);
    CHECK(b.getSoundCount() == 1);
    CHECK(sumOfLoads(a) == 1);

    // Re-connecting to the channel a sound is already in must not link it
    // twice (before #860 this pushed the node onto the same list again).
    b.connect(s[0]);
    CHECK(b.getSoundCount() == 1);
    CHECK(sumOfLoads(b) == 1);

    // Disconnecting from a channel the sound is not in is a no-op.
    CHECK_FALSE(a.disconnect(s[0]));
    CHECK_FALSE(a.disconnect(s[2]));
    CHECK(a.getSoundCount() == 1);

    CHECK(b.disconnect(s[0]));
    CHECK(a.disconnect(s[1]));
    CHECK(a.getSoundCount() == 0);
    CHECK(b.getSoundCount() == 0);
  }

  TEST_CASE("voice slices: a released channel hands every slice's sounds to its parent") {
    ChannelImpl parent(nullptr);
    ChannelImpl child(nullptr);
    parent.connect(&child);
    Sounds s(SLICE_CAPACITY * 2 + 3);
    for (int i = 0; i < SLICE_CAPACITY * 2 + 3; ++i)
      child.connect(s[i]);
    REQUIRE(child.getActiveSlices() == 3);

    child.childrenToParent();
    CHECK(child.getSoundCount() == 0);
    CHECK(child.getActiveSlices() == 1);
    CHECK(parent.getSoundCount() == SLICE_CAPACITY * 2 + 3);
    CHECK(sumOfLoads(parent) == SLICE_CAPACITY * 2 + 3);
    CHECK(parent.getActiveSlices() == 3);

    for (int i = 0; i < SLICE_CAPACITY * 2 + 3; ++i)
      CHECK(parent.disconnect(s[i]));
    parent.disconnect(&child);
  }

} // TEST_SUITE("channel")

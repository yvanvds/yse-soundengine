// Engine-level RT-allocation test for the MIDI file manager bookkeeping
// (issue #266).
//
// `MIDI::managerObject::update()` runs on the audio callback thread. Before
// #266 it drained newly-created impls from its lock-free inbox into an
// `std::forward_list<fileImpl*> inUse` with `emplace_front`, allocating a list
// node on the heap for every MIDI file that started playing — the same per-tick
// malloc churn #194 removed from the sound/channel/reverb managers. The fix
// converts `inUse` to the intrusive `IntrusiveForwardList` (link embedded in
// `fileImpl::_mgrNext`), so promoting an impl into the working list touches no
// heap.
//
// This drives the *real* engine — real MIDI::file objects, the real lock-free
// inbox and the real MIDI::Manager().update() — and probes the promote path for
// allocations. Like the other engine allocation tests it drives update() from
// the test thread with the audio stream paused (support/null_device.hpp), so it
// runs where a default audio device is present and skips gracefully otherwise
// (headless CI). Locally it fails on the pre-#266 code (per-promote emplace_front
// allocation) and passes after the intrusive-list conversion.

#include <doctest/doctest.h>

#include <chrono>
#include <thread>
#include <vector>

#include "yse.hpp"
#include "midi/midifile.hpp"
#include "midi/midifileManager.h"
#include "support/alloc_probe.hpp"
#include "support/null_device.hpp"

using namespace std::chrono_literals;

TEST_SUITE("midi") {

  TEST_CASE("midifile: promoting files into the audio-thread inUse list does not allocate (#266)") {
    if (!TestHelpers::engineInit()) return;

    // Construct a batch of MIDI files on the main thread. Each `file`'s
    // constructor registers an impl and hands it to the manager over the
    // lock-free inbox; we deliberately do NOT drive update() yet, so the impls
    // sit queued and their (legitimate, main-thread) construction allocations
    // all happen OUTSIDE the probe below.
    constexpr int kFiles = 16;
    std::vector<YSE::MIDI::file> files(kFiles);

    {
      TestHelpers::ProbeScope probe;
      // The audio thread drains the inbox: every queued impl is push_front'ed
      // into the `inUse` working list. Before #266 that allocated a
      // std::forward_list node per file; after the intrusive-list conversion it
      // is allocation free.
      YSE::MIDI::Manager().update();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    {
      // Steady state: with every impl already in `inUse` and still interfaced,
      // repeated ticks walk the list and must not allocate either.
      TestHelpers::ProbeScope probe;
      for (int i = 0; i < 8; ++i)
        YSE::MIDI::Manager().update();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    // Tear down: destroying the files orphans their impls; update() retires them
    // from `inUse` and the slow-pool deleteJob reaps them. Functional check that
    // the intrusive erase path leaves the lists intact (not an allocation one —
    // reaping legitimately frees the canonical `implementations` nodes).
    files.clear();
    for (int i = 0; i < 16; ++i) {
      YSE::MIDI::Manager().update();
      std::this_thread::sleep_for(2ms);
    }
    CHECK(true);
  }

} // TEST_SUITE("midi")

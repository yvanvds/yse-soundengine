// Regression tests for the swappable GraphState + atomic publish rework of the
// patcher (issue #226). These drive patcherImplementation::Calculate directly
// (the audio-thread entry point) and read the rendered `output`, so they
// exercise the snapshot path — the topology the audio thread walks comes from
// the published GraphState, not the live object wiring.
//
// A ~noise -> ~dac graph is the simplest DSP flow: ~noise is a start point with
// no inlets and emits a non-silent buffer every block; ~dac sums its buffer
// input into the patcher output. Connecting/disconnecting/deleting between
// Calculate calls must change what the next block renders, proving the
// build-and-swap actually reaches the audio thread.
//
// No audio device required.

#include <doctest/doctest.h>
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "dsp/buffer.hpp"
#include <chrono>
#include <thread>

using YSE::PATCHER::patcherImplementation;

TEST_SUITE("patcher") {

  TEST_CASE("graphstate: Calculate takes no lock and renders silence when empty") {
    patcherImplementation p(1, nullptr);
    p.Calculate(YSE::T_DSP); // no active snapshot yet — must be safe
    CHECK(p.output[0].isSilent());
  }

  TEST_CASE("graphstate: Connect through the swap changes what the block renders") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* noise = p.CreateObject(YSE::OBJ::D_NOISE, "");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(noise != nullptr);
    REQUIRE(dac != nullptr);

    // Unconnected: the DAC has no buffer input, so the output stays silent.
    p.Calculate(YSE::T_DSP);
    CHECK(p.output[0].isSilent());

    // Connect noise -> dac and render: the swap must take effect immediately.
    p.Connect(noise, 0, dac, 0);
    p.Calculate(YSE::T_DSP);
    CHECK_FALSE(p.output[0].isSilent());
  }

  TEST_CASE("graphstate: Disconnect through the swap silences the next block") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* noise = p.CreateObject(YSE::OBJ::D_NOISE, "");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(noise, 0, dac, 0);

    p.Calculate(YSE::T_DSP);
    REQUIRE_FALSE(p.output[0].isSilent());

    p.Disconnect(noise, 0, dac, 0);
    p.Calculate(YSE::T_DSP);
    CHECK(p.output[0].isSilent());
  }

  TEST_CASE("graphstate: deleting a connected source is safe and silences output") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* noise = p.CreateObject(YSE::OBJ::D_NOISE, "");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(noise, 0, dac, 0);

    p.Calculate(YSE::T_DSP);
    REQUIRE_FALSE(p.output[0].isSilent());

    // The deleted object must not be freed while a block could still walk the
    // retired snapshot; the next Calculate must not touch it.
    p.DeleteObject(noise);
    p.Calculate(YSE::T_DSP);
    CHECK(p.output[0].isSilent());
  }

  TEST_CASE("graphstate: repeated live edits interleaved with rendering stay stable") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* noise = p.CreateObject(YSE::OBJ::D_NOISE, "");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");

    // Toggle the connection many times, rendering blocks in between, so the
    // retire/reclaim path runs repeatedly. Output must track the current wiring
    // every time, and nothing may be freed early (an ASan build would trip).
    for (int i = 0; i < 64; ++i) {
      p.Connect(noise, 0, dac, 0);
      p.Calculate(YSE::T_DSP);
      CHECK_FALSE(p.output[0].isSilent());

      p.Disconnect(noise, 0, dac, 0);
      p.Calculate(YSE::T_DSP);
      p.Calculate(YSE::T_DSP);
      CHECK(p.output[0].isSilent());
    }
  }

  // ---- Background reclamation of retired GraphState/objects (issue #227) ----

  TEST_CASE("reclaim: deleting a source leaves its former sink usable") {
    // The retired source's inlet/outlet destructors call Disconnect on their
    // peers; UnwireFromPeers must have cleared that wiring at delete time so the
    // deferred teardown touches only the retired object, never the live dac. If
    // it didn't, the dac's inlet would be left dangling and reusing it would
    // corrupt the graph.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* noise = p.CreateObject(YSE::OBJ::D_NOISE, "");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(noise, 0, dac, 0);
    p.Calculate(YSE::T_DSP);
    REQUIRE_FALSE(p.output[0].isSilent());

    p.DeleteObject(noise);
    p.Calculate(YSE::T_DSP);
    CHECK(p.output[0].isSilent());

    // Wire a fresh source into the surviving dac and render: the dac's inlet
    // must still be clean.
    YSE::pHandle* noise2 = p.CreateObject(YSE::OBJ::D_NOISE, "");
    p.Connect(noise2, 0, dac, 0);
    p.Calculate(YSE::T_DSP);
    CHECK_FALSE(p.output[0].isSilent());
  }

  TEST_CASE("reclaim: the background pool drains retired snapshots, not the dtor") {
    // Every connect/disconnect retires a GraphState. If reclamation were broken
    // the retire lists would grow with the edit count (only the destructor would
    // ever free them). With the background pool draining once the audio epoch has
    // advanced two blocks past retirement, the pending count stays small no
    // matter how many edits churn through. Each Calculate advances the epoch.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* noise = p.CreateObject(YSE::OBJ::D_NOISE, "");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");

    for (int i = 0; i < 200; ++i) {
      p.Connect(noise, 0, dac, 0);
      p.Calculate(YSE::T_DSP);
      p.Disconnect(noise, 0, dac, 0);
      p.Calculate(YSE::T_DSP);
      p.Calculate(YSE::T_DSP);
    }

    // Let the background worker catch up. Re-arm with a light edit each spin so a
    // reclaimer that gave up on a momentarily-stalled epoch is retriggered; the
    // rendered blocks keep the epoch moving so it can cross the +2 grace.
    for (int spins = 0; spins < 2000 && p.PendingRetired() > 4; ++spins) {
      p.Connect(noise, 0, dac, 0);
      p.Calculate(YSE::T_DSP);
      p.Disconnect(noise, 0, dac, 0);
      p.Calculate(YSE::T_DSP);
      p.Calculate(YSE::T_DSP);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // A handful of just-retired snapshots may still be inside the +2 grace, but
    // the 400+ retired over the churn must be gone — reclaimed by the pool, not
    // waiting for teardown.
    CHECK(p.PendingRetired() <= 4);
  }

  TEST_CASE("graphstate: ParseJSON publishes a renderable graph in one swap") {
    patcherImplementation src(1, nullptr);
    YSE::pHandle* noise = src.CreateObject(YSE::OBJ::D_NOISE, "");
    YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
    src.Connect(noise, 0, dac, 0);
    std::string dump = src.DumpJSON();

    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    dst.Calculate(YSE::T_DSP);
    CHECK_FALSE(dst.output[0].isSilent());
  }

  // ---- Off-thread file handlers / retired fileHandlerActive (issue #228) ----

  TEST_CASE("filehandlers: ParseJSON publishes exactly one graph for the whole file") {
    // The parse builds every object + connection under one lock and swaps once.
    // On a fresh patcher active_ starts null, so a correct single publish retires
    // nothing. If the per-edit publish had crept back (the old fileHandlerActive
    // batching lost), each CreateObject/Connect would retire an intermediate
    // graph and PendingRetired() would climb with the object/edge count.
    patcherImplementation src(1, nullptr);
    src.Connect(src.CreateObject(YSE::OBJ::D_NOISE, ""), 0, src.CreateObject(YSE::OBJ::D_DAC, ""),
                0);
    std::string dump = src.DumpJSON();

    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.PendingRetired() == 0);
    dst.Calculate(YSE::T_DSP);
    CHECK_FALSE(dst.output[0].isSilent());
  }

  // ---- One-sided edges from refused connections (issue #237) ----

  TEST_CASE("connect: a refused buffer connection leaves no one-sided outlet edge") {
    // An inlet holds at most one buffer source (dspConnection is a single
    // slot), so a second buffer connection into the same inlet is refused on
    // the inlet side. The outlet must not record the edge in that case: a
    // one-sided outlet->inlet edge is invisible to Disconnect and
    // UnwireFromPeers (both clean up from the inlet's records), so it gets
    // compiled into every GraphState built after the target object is deleted
    // and the audio thread walks a freed inlet through the *live* snapshot —
    // the #237 use-after-free.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* noiseA = p.CreateObject(YSE::OBJ::D_NOISE, "");
    YSE::pHandle* noiseB = p.CreateObject(YSE::OBJ::D_NOISE, "");
    YSE::pHandle* sw = p.CreateObject(YSE::OBJ::G_SWITCH, "3");

    p.Connect(noiseA, 0, sw, 0);
    CHECK(noiseA->GetConnections(0) == 1);

    // Second buffer source into the same inlet: refused by the inlet, so the
    // outlet side must stay clean too.
    p.Connect(noiseB, 0, sw, 0);
    CHECK(noiseB->GetConnections(0) == 0);
  }

  TEST_CASE("connect: deleting the target of a refused connection leaves no dangling edge") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* noiseA = p.CreateObject(YSE::OBJ::D_NOISE, "");
    YSE::pHandle* noiseB = p.CreateObject(YSE::OBJ::D_NOISE, "");
    YSE::pHandle* sw = p.CreateObject(YSE::OBJ::G_SWITCH, "3");
    p.Connect(noiseA, 0, sw, 0);
    p.Connect(noiseB, 0, sw, 0); // refused: inlet 0 already has a buffer source

    p.DeleteObject(sw);
    CHECK(noiseA->GetConnections(0) == 0);
    CHECK(noiseB->GetConnections(0) == 0);

    // Keep rendering while the background pool reclaims the deleted gSwitch.
    // Both noises are start points, so every block resolves their outlet
    // targets from the pinned snapshot; a leftover one-sided edge would make
    // these blocks read the freed inlet (an ASan/TSan build trips here).
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    for (int spins = 0; spins < 2000 && p.PendingRetired() > 4; ++spins) {
      p.Connect(noiseA, 0, dac, 0);
      p.Calculate(YSE::T_DSP);
      p.Disconnect(noiseA, 0, dac, 0);
      p.Calculate(YSE::T_DSP);
      p.Calculate(YSE::T_DSP);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    p.Calculate(YSE::T_DSP);
    CHECK(p.output[0].isSilent());
  }

  TEST_CASE("filehandlers: Clear silences the next block and is reclaim-safe") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* noise = p.CreateObject(YSE::OBJ::D_NOISE, "");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(noise, 0, dac, 0);
    p.Calculate(YSE::T_DSP);
    REQUIRE_FALSE(p.output[0].isSilent());

    // Clearing a rendering patcher publishes an empty graph and retires the old
    // objects; a block that ran the retired snapshot must not touch freed memory
    // (an ASan build would trip). The next block renders silence.
    p.Clear();
    p.Calculate(YSE::T_DSP);
    CHECK(p.output[0].isSilent());

    // The patcher is reusable after a clear: re-parse a fresh graph into it.
    patcherImplementation src(1, nullptr);
    src.Connect(src.CreateObject(YSE::OBJ::D_NOISE, ""), 0, src.CreateObject(YSE::OBJ::D_DAC, ""),
                0);
    p.ParseJSON(src.DumpJSON());
    p.Calculate(YSE::T_DSP);
    CHECK_FALSE(p.output[0].isSilent());
  }

  // ---- Graph-id recompaction on an empty patcher (issue #355) ----

  TEST_CASE("graphids: repeated Clear + rebuild reuses the id space") {
    // graphId is a per-patcher monotonic counter that sizes every new
    // GraphState's id-indexed tables (outletTargets / inletHasDsp). Without
    // recompaction the counters — and thus each snapshot's tables and per-edit
    // rebuild cost — grow with the *total* objects ever created, unbounded over a
    // long live-coding / preset-reload session. Recompacting when the patcher
    // goes empty restarts the ids, so a fresh noise->dac always lands in the same
    // small id range no matter how many Clear cycles preceded it.
    patcherImplementation p(1, nullptr);
    std::size_t baseOut = 0;
    std::size_t baseIn = 0;
    for (int i = 0; i < 50; ++i) {
      YSE::pHandle* noise = p.CreateObject(YSE::OBJ::D_NOISE, "");
      YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(noise != nullptr);
      REQUIRE(dac != nullptr);
      p.Connect(noise, 0, dac, 0);
      p.Calculate(YSE::T_DSP);
      CHECK_FALSE(p.output[0].isSilent());

      if (i == 0) {
        baseOut = p.OutletIdSpace();
        baseIn = p.InletIdSpace();
      }
      // The id high-water mark must not grow with the iteration count: every
      // rebuild starts from a compacted id space. Before the fix these climbed
      // linearly and this would fail within a few iterations.
      CHECK(p.OutletIdSpace() == baseOut);
      CHECK(p.InletIdSpace() == baseIn);

      p.Clear();
      p.Calculate(YSE::T_DSP);
      CHECK(p.output[0].isSilent());
      // Clear empties the patcher, so the id counters reset to 0.
      CHECK(p.OutletIdSpace() == 0);
      CHECK(p.InletIdSpace() == 0);
    }
    // Sanity: the graph really did consume outlet ids, so the invariant above is
    // meaningful (not vacuously satisfied by an all-zero id space).
    CHECK(baseOut > 0);
  }

  TEST_CASE("graphids: deleting every object recompacts the id space") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* noise = p.CreateObject(YSE::OBJ::D_NOISE, "");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(noise, 0, dac, 0);
    const std::size_t firstOut = p.OutletIdSpace();
    const std::size_t firstIn = p.InletIdSpace();
    REQUIRE(firstOut > 0);

    // Deleting objects one by one down to empty must recompact just like Clear.
    p.DeleteObject(noise);
    p.DeleteObject(dac);
    p.Calculate(YSE::T_DSP);
    CHECK(p.OutletIdSpace() == 0);
    CHECK(p.InletIdSpace() == 0);

    // Rebuilding the same graph reuses the same id range instead of extending it.
    YSE::pHandle* noise2 = p.CreateObject(YSE::OBJ::D_NOISE, "");
    YSE::pHandle* dac2 = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(noise2, 0, dac2, 0);
    CHECK(p.OutletIdSpace() == firstOut);
    CHECK(p.InletIdSpace() == firstIn);
    p.Calculate(YSE::T_DSP);
    CHECK_FALSE(p.output[0].isSilent());
  }

  // ---- Free-list recycling of retired graph ids during churn (issue #364) ----

  TEST_CASE("graphids: continuous create/delete churn recycles ids via the free-list") {
    // #355's recompaction only fires when the patcher goes empty. A patcher that
    // is continuously edited while at least one object always survives never
    // empties, so the counters — and every snapshot's id-indexed tables — would
    // climb with the lifetime create count. The free-list (issue #364) bounds
    // that: the reclaimer returns a deleted object's ids once no snapshot can
    // still index them, and AssignGraphIds pulls them before extending the
    // counter, so the id space tracks the peak *simultaneous* object count.
    patcherImplementation p(1, nullptr);
    // A persistent noise->dac keeps one object live for the whole test, so the
    // patcher never empties and compaction never runs — recycling is then the
    // only mechanism that can keep the id space from growing. Toggling this edge
    // also re-arms the background reclaimer each drain spin (a stalled epoch
    // otherwise ends the reclaim chain until the next structural edit).
    YSE::pHandle* noise = p.CreateObject(YSE::OBJ::D_NOISE, "");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(noise != nullptr);
    REQUIRE(dac != nullptr);
    p.Connect(noise, 0, dac, 0);
    p.Calculate(YSE::T_DSP);

    const std::size_t baseOut = p.OutletIdSpace();
    const std::size_t baseIn = p.InletIdSpace();

    // The first temp (a ~+ has two inlets and one outlet) finds an empty
    // free-list, so it stamps fresh ids and lifts the high-water mark by exactly
    // one temp's worth of pins. That peak is the bound every later temp must
    // respect.
    YSE::pHandle* first = p.CreateObject(YSE::OBJ::D_ADD, "");
    REQUIRE(first != nullptr);
    const std::size_t peakOut = p.OutletIdSpace();
    const std::size_t peakIn = p.InletIdSpace();
    REQUIRE(peakOut > baseOut); // the temp really consumed an outlet id
    REQUIRE(peakIn > baseIn); // ... and inlet ids
    p.DeleteObject(first);

    for (int i = 0; i < 40; ++i) {
      // Drain the reclaimer so the previous temp's ids are back on the free-list
      // before the next create. Each spin toggles the persistent edge (re-arming
      // the reclaimer) and renders blocks (advancing the audio epoch across the
      // +2 grace); the background pool then frees the temp and recycles its ids
      // (RecycleObjectIds pushes all of a temp's inlet + outlet ids under one
      // reclaimMtx_ hold, so FreeIdCount jumps from 0 to the full count at once).
      for (int spins = 0; spins < 3000 && p.FreeIdCount() == 0; ++spins) {
        p.Disconnect(noise, 0, dac, 0);
        p.Calculate(YSE::T_DSP);
        p.Connect(noise, 0, dac, 0);
        p.Calculate(YSE::T_DSP);
        p.Calculate(YSE::T_DSP);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      REQUIRE(p.FreeIdCount() > 0); // the deleted temp's ids are recyclable now

      YSE::pHandle* temp = p.CreateObject(YSE::OBJ::D_ADD, "");
      REQUIRE(temp != nullptr);
      // The create must have pulled recycled ids instead of extending the
      // counters: the high-water mark never climbs past the single-temp peak,
      // no matter how many temps churned through. Before the free-list these
      // grew by one temp's pins every iteration.
      CHECK(p.OutletIdSpace() == peakOut);
      CHECK(p.InletIdSpace() == peakIn);
      p.DeleteObject(temp);
    }
  }

  TEST_CASE("graphids: churning a source into a live sink stays correct with recycling") {
    // Reusing an id must not corrupt what the audio thread renders: an outlet
    // whose id was recycled from a deleted object must resolve *its own* targets
    // in the pinned snapshot, never a stale entry. Repeatedly wire a fresh source
    // into the surviving dac, render, then delete it. Each create-after-delete
    // recycles the previous source's ids (the edits re-arm the reclaimer and the
    // renders advance the epoch), so the recycled outlet is exercised in the
    // signal path every iteration. An ASan/TSan build additionally trips on any
    // dangling reuse.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(dac != nullptr);

    for (int i = 0; i < 30; ++i) {
      YSE::pHandle* noise = p.CreateObject(YSE::OBJ::D_NOISE, "");
      REQUIRE(noise != nullptr);
      p.Connect(noise, 0, dac, 0);
      p.Calculate(YSE::T_DSP);
      CHECK_FALSE(p.output[0].isSilent()); // the (recycled) outlet resolves its target

      p.Disconnect(noise, 0, dac, 0);
      p.DeleteObject(noise);
      p.Calculate(YSE::T_DSP);
      p.Calculate(YSE::T_DSP);
      CHECK(p.output[0].isSilent()); // source gone -> silence, no stale target
    }
  }

} // TEST_SUITE("patcher")

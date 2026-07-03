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

} // TEST_SUITE("patcher")

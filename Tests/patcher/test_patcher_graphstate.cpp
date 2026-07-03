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

} // TEST_SUITE("patcher")

// Signal inlet semantics (issue #1036): fan-in summing, unconnected inlets
// reading as constants, a connected signal winning over a float, and the graph
// staying consistent while cords come and go during rendering.
//
// `~+ x` with nothing connected is the constant source throughout: its left
// inlet reads as 0 and its right as x, so it renders x every block.

#include <doctest/doctest.h>
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "dsp/buffer.hpp"
#include "support/alloc_probe.hpp"
#include <atomic>
#include <cmath>
#include <thread>

using YSE::PATCHER::patcherImplementation;

namespace {
  // Every sample of the first output channel equals `value`.
  bool OutputIs(patcherImplementation& p, float value) {
    YSE::DSP::buffer& out = p.output[0];
    if (out.getLength() == 0) return false;
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      if (std::fabs(out.getPtr()[i] - value) > 1e-6f) return false;
    }
    return true;
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("signal inlets: an object with no signal source renders its constant (#1036)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::D_ADD, "0.25");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(add, 0, dac, 0);

    p.Calculate(YSE::T_DSP);
    CHECK(OutputIs(p, 0.25f));
  }

  TEST_CASE("signal inlets: several cords into one inlet are summed (#1036)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* a = p.CreateObject(YSE::OBJ::D_ADD, "0.25");
    YSE::pHandle* b = p.CreateObject(YSE::OBJ::D_ADD, "0.5");
    YSE::pHandle* c = p.CreateObject(YSE::OBJ::D_ADD, "0.125");
    YSE::pHandle* mul = p.CreateObject(YSE::OBJ::D_MULTIPLY, "2");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");

    SUBCASE("into an object: it computes once, on the sum") {
      p.Connect(a, 0, mul, 0);
      p.Connect(b, 0, mul, 0);
      p.Connect(c, 0, mul, 0);
      p.Connect(mul, 0, dac, 0);
      p.Calculate(YSE::T_DSP);
      CHECK(OutputIs(p, 1.75f)); // (0.25 + 0.5 + 0.125) * 2
    }

    SUBCASE("into ~dac") {
      p.Connect(a, 0, dac, 0);
      p.Connect(b, 0, dac, 0);
      p.Calculate(YSE::T_DSP);
      CHECK(OutputIs(p, 0.75f));
    }

    SUBCASE("the sum starts over every block") {
      p.Connect(a, 0, dac, 0);
      p.Connect(b, 0, dac, 0);
      p.Calculate(YSE::T_DSP);
      p.Calculate(YSE::T_DSP);
      CHECK(OutputIs(p, 0.75f));
    }
  }

  TEST_CASE("signal inlets: the same cord twice is one cord (#1036)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* a = p.CreateObject(YSE::OBJ::D_ADD, "0.25");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(a, 0, dac, 0);
    p.Connect(a, 0, dac, 0);
    CHECK(a->GetConnections(0) == 1);
    p.Calculate(YSE::T_DSP);
    CHECK(OutputIs(p, 0.25f));
  }

  TEST_CASE("signal inlets: a connected signal wins over a float, which takes over on disconnect "
            "(#1036)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* source = p.CreateObject(YSE::OBJ::D_ADD, "0.5");
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::D_ADD, "");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(source, 0, add, 1);
    p.Connect(add, 0, dac, 0);

    add->SetFloatData(1, 10.f);
    p.Calculate(YSE::T_DSP);
    CHECK(OutputIs(p, 0.5f)); // the float is stored, not heard

    p.Disconnect(source, 0, add, 1);
    p.Calculate(YSE::T_DSP);
    CHECK(OutputIs(p, 10.f)); // unconnected: the last float is the constant

    p.Connect(source, 0, add, 1);
    p.Calculate(YSE::T_DSP);
    CHECK(OutputIs(p, 0.5f));
  }

  TEST_CASE("signal inlets: ~vcf runs with only its audio input wired (#1036)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* noise = p.CreateObject(YSE::OBJ::D_NOISE, "");
    YSE::pHandle* vcf = p.CreateObject(YSE::OBJ::D_VCF, "5 1000");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(noise, 0, vcf, 0);
    p.Connect(vcf, 0, dac, 0);

    p.Calculate(YSE::T_DSP);
    CHECK_FALSE(p.output[0].isSilent());
  }

  TEST_CASE("signal inlets: summing and the constant fill allocate nothing (#1036)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* a = p.CreateObject(YSE::OBJ::D_ADD, "0.25");
    YSE::pHandle* b = p.CreateObject(YSE::OBJ::D_ADD, "0.5");
    YSE::pHandle* lp = p.CreateObject(YSE::OBJ::D_LOWPASS, "");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(a, 0, dac, 0);
    p.Connect(b, 0, dac, 0);
    p.Connect(lp, 0, dac, 1);
    p.Calculate(YSE::T_DSP); // first block outside the probe

    {
      TestHelpers::ProbeScope probe;
      for (int i = 0; i < 16; ++i)
        p.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(OutputIs(p, 0.75f));
  }

  TEST_CASE("signal inlets: cords added and removed while rendering keep every block whole "
            "(#1036)") {
    // One render thread, one editor toggling a second source into the dac. Each
    // block runs against one pinned snapshot, so it hears one source or both,
    // never a partial sum and never a stall waiting for a source the snapshot
    // does not have.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* a = p.CreateObject(YSE::OBJ::D_ADD, "0.25");
    YSE::pHandle* b = p.CreateObject(YSE::OBJ::D_ADD, "0.5");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(a, 0, dac, 0);

    std::atomic<bool> stop{false};
    std::atomic<int> bad{0};
    std::atomic<int> blocks{0};
    std::thread render([&] {
      while (!stop.load(std::memory_order_relaxed)) {
        p.Calculate(YSE::T_DSP);
        if (!OutputIs(p, 0.25f) && !OutputIs(p, 0.75f)) bad.fetch_add(1);
        blocks.fetch_add(1, std::memory_order_relaxed);
      }
    });

    for (int i = 0; i < 2000; ++i) {
      p.Connect(b, 0, dac, 0);
      p.Disconnect(b, 0, dac, 0);
    }
    // Keep editing until the renderer has demonstrably overlapped the edits.
    while (blocks.load() < 100) {
      p.Connect(b, 0, dac, 0);
      p.Disconnect(b, 0, dac, 0);
    }
    stop = true;
    render.join();

    CHECK(bad.load() == 0);
    p.Calculate(YSE::T_DSP);
    CHECK(OutputIs(p, 0.25f));
  }

} // TEST_SUITE("patcher")

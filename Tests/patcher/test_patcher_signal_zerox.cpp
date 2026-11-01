// Tests for the ~zerox patcher object (patcher/analysis/dZerox), issue #1124.
// The crossings are DSP::zeroCrossing's and are tested in
// Tests/dsp/test_zero_crossing.cpp; these show that the signal, the numbers,
// the 'set' message and the click volume argument reach it, that the count
// and the clicks leave on the right outlets, and that it works in a rendered
// ~phasor -> ~- -> ~zerox -> ~dac patch after a JSON round trip.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/analysis/dZerox.h"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::PATCHER::dZerox;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr unsigned int Quarter = N / 4;
  constexpr unsigned int Half = N / 2;

  // ~zerox with a buffer sink on each outlet.
  struct Rig {
    BufferSink count;
    BufferSink click;
    dZerox zerox;

    Rig() {
      zerox.ConnectOutlet(count.GetInlet(0), 0);
      count.ConnectInlet(zerox.GetOutlet(0), 0);
      zerox.ConnectOutlet(click.GetInlet(0), 1);
      click.ConnectInlet(zerox.GetOutlet(1), 0);
    }
    // One block with nothing connected: the inlet's number.
    void Block() {
      zerox.ResetDSP();
      zerox.Calculate(YSE::T_DSP);
      REQUIRE(count.received != nullptr);
      REQUIRE(click.received != nullptr);
    }
    void Block(buffer& in) {
      zerox.ResetDSP();
      zerox.GetInlet(0)->SetBuffer(&in, YSE::T_DSP); // the last input: runs CALC()
      REQUIRE(count.received != nullptr);
      REQUIRE(click.received != nullptr);
    }
    float Count() const {
      return count.received->getPtr()[N - 1];
    }
    float Click(unsigned int i) const {
      return click.received->getPtr()[i];
    }
    // The sum of the click outlet over the block.
    float Clicks() const {
      float sum = 0.f;
      for (unsigned int i = 0; i < click.received->getLength(); ++i)
        sum += click.received->getPtr()[i];
      return sum;
    }
  };

  // +1 up to `flip` (exclusive), then -1, then +1 again from `back`.
  void Square(buffer& b, unsigned int flip, unsigned int back) {
    for (unsigned int i = 0; i < b.getLength(); ++i)
      b.getPtr()[i] = (i >= flip && i < back) ? -1.f : 1.f;
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~zerox: registry and shape (#1124)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_ZEROX);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~zerox");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 2);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(obj->OutputDataType(1) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_ZEROX));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::ANALYSIS);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~zerox: the count on the left, a click per crossing on the right, across blocks "
            "(#1124)") {
    Rig rig;
    buffer in(N);
    // +1, then -1 from a quarter, +1 again from half: two crossings.
    Square(in, Quarter, Half);
    rig.Block(in);
    for (unsigned int i = 0; i < N; ++i) {
      CAPTURE(i);
      CHECK(rig.count.received->getPtr()[i] == 2.f);
      CHECK(rig.Click(i) == ((i == Quarter || i == Half) ? 1.f : 0.f));
    }
    // The side carries: a block that starts below 0 crosses on sample 0.
    in = -1.f;
    rig.Block(in);
    CHECK(rig.Count() == 1.f);
    CHECK(rig.Click(0) == 1.f);
    CHECK(rig.Clicks() == 1.f);
    // Touching zero and turning back is no crossing.
    in = -1.f;
    in.getPtr()[Quarter] = 0.f;
    rig.Block(in);
    CHECK(rig.Count() == 0.f);
    CHECK(rig.Clicks() == 0.f);
    // Alternating signs: a crossing on every sample.
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = (i % 2 == 0) ? 1.f : -1.f;
    rig.Block(in);
    CHECK(rig.Count() == static_cast<float>(N));
    CHECK(rig.Clicks() == static_cast<float>(N));
  }

  TEST_CASE("~zerox: the click volume from the argument and from 'set' (#1124)") {
    Rig rig;
    rig.zerox.SetParams("0.5");
    CHECK(rig.zerox.GetParams() == "0.5");
    buffer in(N);
    Square(in, Quarter, Half);
    rig.Block(in);
    CHECK(rig.Click(Quarter) == 0.5f);
    CHECK(rig.Click(Half) == 0.5f);
    CHECK(rig.Count() == 2.f); // the count does not scale

    rig.zerox.GetInlet(0)->SetMessage("set -2", YSE::T_GUI);
    in = -1.f;
    rig.Block(in); // crosses at 0 from the +1 the last block ended on
    CHECK(rig.Click(0) == -2.f);
    CHECK(rig.zerox.GetParams() == "0.5"); // 'set' is not saved, as for ~slide

    // Malformed: ignored. A word other than set, NaN, nothing after set.
    rig.zerox.SetMessage("set nan", 0.f);
    rig.zerox.SetMessage("set", 0.f);
    rig.zerox.SetMessage("volume 3", 0.f);
    rig.zerox.SetMessage("", 0.f);
    in = 1.f;
    rig.Block(in);
    CHECK(rig.Click(0) == -2.f);
  }

  TEST_CASE("~zerox: a number in the inlet is the input while no signal is connected; a signal "
            "wins (#1124, #1036)") {
    Rig rig;
    rig.Block();
    CHECK(rig.Count() == 0.f); // 0: no side
    rig.zerox.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    rig.Block();
    CHECK(rig.Count() == 0.f); // the first side
    rig.zerox.GetInlet(0)->SetFloat(-0.5f, YSE::T_GUI);
    rig.Block();
    CHECK(rig.Count() == 1.f);
    CHECK(rig.Click(0) == 1.f);
    rig.Block();
    CHECK(rig.Count() == 0.f); // a constant does not cross again
    rig.zerox.GetInlet(0)->SetInt(2, YSE::T_GUI);
    rig.Block();
    CHECK(rig.Count() == 1.f);
    // A connected signal wins over the number.
    buffer in(N);
    in = -1.f;
    rig.Block(in);
    CHECK(rig.Count() == 1.f);
    in = 1.f;
    in.getPtr()[N - 1] = -1.f;
    rig.Block(in);
    CHECK(rig.Count() == 2.f);
    CHECK(rig.Click(0) == 1.f);
    CHECK(rig.Click(N - 1) == 1.f);
  }

  TEST_CASE("~zerox: the click volume survives a DumpJSON / ParseJSON round trip (#1124)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_ZEROX, "0.125") != nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_ZEROX, "") != nullptr);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* a = dst.GetHandleFromID(0);
    YSE::pHandle* b = dst.GetHandleFromID(1);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(std::string(a->Type()) == "~zerox");
    CHECK(a->GetParams() == "0.125");
    CHECK(b->GetParams() == "");
  }

  TEST_CASE("~zerox: in a rendered patch, a centred ~phasor crosses twice per cycle, after a "
            "JSON round trip (#1124)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      // 375 Hz at 48 kHz: one cycle per block, -0.5 to 0.5.
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* centre = src.CreateObject(YSE::OBJ::D_SUBSTRACT, "0.5");
      YSE::pHandle* zerox = src.CreateObject(YSE::OBJ::D_ZEROX, "0.25");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(centre != nullptr);
      REQUIRE(zerox != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, centre, 0);
      src.Connect(centre, 0, zerox, 0);
      src.Connect(zerox, 0, dac, 0);
      src.Connect(zerox, 1, dac, 1);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    dst.Calculate(YSE::T_DSP); // the first block: the first side is not a crossing
    for (int block = 0; block < 6; ++block) {
      CAPTURE(block);
      dst.Calculate(YSE::T_DSP);
      // The wrap below 0 near the start, the rise through 0 near the middle.
      unsigned int clicks = 0;
      for (unsigned int i = 0; i < N; ++i) {
        CHECK(dst.output[0].getPtr()[i] == 2.f);
        const float c = dst.output[1].getPtr()[i];
        CHECK((c == 0.f || c == 0.25f));
        if (c != 0.f) {
          ++clicks;
          CHECK((i <= 1 || (i >= Half - 1 && i <= Half + 1)));
        }
      }
      CHECK(clicks == 2);
    }
  }

  TEST_CASE("~zerox: numbers, 'set' and blocks on the audio thread allocate nothing (#1124)") {
    Rig rig;
    buffer in(N);
    Square(in, Quarter, Half);
    const std::string set = "set 0.75";
    rig.Block(in);
    {
      TestHelpers::ProbeScope probe;
      rig.zerox.GetInlet(0)->SetFloat(0.3f, YSE::T_DSP);
      rig.zerox.GetInlet(0)->SetInt(-1, YSE::T_DSP);
      rig.zerox.GetInlet(0)->SetMessage(set, YSE::T_DSP);
      rig.Block();
      rig.Block(in);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(rig.Click(Quarter) == 0.75f);
  }

} // TEST_SUITE("patcher")

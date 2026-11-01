// Tests for the ~edge patcher object (patcher/analysis/dEdge), issue #1121.
// The transitions are DSP::edgeDetector's and are tested in
// Tests/dsp/test_edge_detector.cpp; these show that the signal and the
// numbers reach it, that its bangs go out only through the armed outlets
// (#1037), and that it works in a rendered ~phasor -> ~> -> ~edge ->
// .counter patch after a JSON round trip.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/analysis/dEdge.h"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BangSink;
using TestHelpers::OrderSink;
using TestHelpers::Wire;
using YSE::DSP::buffer;
using YSE::PATCHER::dEdge;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // ~edge with a bang sink on each outlet. Block() runs one block and the
  // flush the patcher runs after its traversal.
  struct Rig {
    BangSink rise;
    BangSink fall;
    dEdge edge;

    Rig() {
      Wire(edge, 0, rise);
      Wire(edge, 1, fall);
    }
    void Block() {
      edge.Calculate(YSE::T_DSP);
      edge.FlushArmedOutlets(YSE::T_GUI);
    }
    void Block(buffer& in) {
      edge.ResetDSP();
      edge.GetInlet(0)->SetBuffer(&in, YSE::T_DSP); // the last input: runs CALC()
      edge.FlushArmedOutlets(YSE::T_GUI);
    }
  };

  int GuiInt(YSE::pHandle* h) {
    return std::stoi(h->GetGuiValue());
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~edge: registry and shape (#1121)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_EDGE);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~edge");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 2);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BANG);
    CHECK(obj->OutputDataType(1) == YSE::OUT_TYPE::BANG);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_EDGE));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::ANALYSIS);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 0);
  }

  TEST_CASE("~edge: CALC() only arms; the bang goes out on the flush (#1121, #1037)") {
    Rig rig;
    buffer in(N);
    in = 0.f;
    in.getPtr()[10] = 1.f;
    rig.edge.ResetDSP();
    rig.edge.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
    CHECK(rig.rise.bangCount == 0);
    CHECK(rig.fall.bangCount == 0);
    CHECK(rig.edge.GetOutlet(0)->Armed());
    CHECK(rig.edge.GetOutlet(1)->Armed());
    rig.edge.FlushArmedOutlets(YSE::T_GUI);
    CHECK(rig.rise.bangCount == 1);
    CHECK(rig.fall.bangCount == 1);
    CHECK_FALSE(rig.edge.GetOutlet(0)->Armed());
  }

  TEST_CASE("~edge: zero to non-zero bangs left, non-zero to zero bangs right (#1121)") {
    Rig rig;
    buffer in(N);
    in = 0.f;
    rig.Block(in);
    CHECK(rig.rise.bangCount == 0);
    CHECK(rig.fall.bangCount == 0);

    in = 0.5f; // on from the first sample: a rise
    rig.Block(in);
    CHECK(rig.rise.bangCount == 1);
    CHECK(rig.fall.bangCount == 0);

    in = -2.f; // still non-zero: nothing
    rig.Block(in);
    CHECK(rig.rise.bangCount == 1);
    CHECK(rig.fall.bangCount == 0);

    in = 0.f; // the state carried across the block edge gives a fall at sample 0
    rig.Block(in);
    CHECK(rig.rise.bangCount == 1);
    CHECK(rig.fall.bangCount == 1);
  }

  TEST_CASE("~edge: several transitions of a kind in a block are one bang (#1121, #1037)") {
    Rig rig;
    buffer in(N);
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = (i / 8u) % 2u == 0 ? 0.f : 1.f; // 8 rises, 7 falls
    rig.Block(in);
    CHECK(rig.rise.bangCount == 1);
    CHECK(rig.fall.bangCount == 1);
  }

  TEST_CASE("~edge: the right outlet goes out before the left (#1121, #1037)") {
    std::vector<char> log;
    OrderSink rise;
    OrderSink fall;
    rise.log = fall.log = &log;
    rise.tag = 'r';
    fall.tag = 'f';
    dEdge edge;
    Wire(edge, 0, rise);
    Wire(edge, 1, fall);
    buffer in(N);
    in = 0.f;
    in.getPtr()[3] = 1.f;
    edge.ResetDSP();
    edge.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
    edge.FlushArmedOutlets(YSE::T_GUI);
    CHECK(std::string(log.begin(), log.end()) == "fr");
  }

  TEST_CASE("~edge: a number turning zero or non-zero is a transition (#1121, #1036)") {
    Rig rig;
    rig.Block(); // unconnected and no number: reads 0
    CHECK(rig.rise.bangCount == 0);

    rig.edge.GetInlet(0)->SetFloat(0.25f, YSE::T_GUI);
    CHECK(rig.rise.bangCount == 0); // a number only sets the input
    rig.Block();
    CHECK(rig.rise.bangCount == 1);
    rig.Block(); // the same number: steady
    rig.edge.GetInlet(0)->SetInt(3, YSE::T_GUI);
    rig.Block(); // non-zero to non-zero: nothing
    CHECK(rig.rise.bangCount == 1);
    CHECK(rig.fall.bangCount == 0);

    rig.edge.GetInlet(0)->SetInt(0, YSE::T_GUI);
    rig.Block();
    CHECK(rig.fall.bangCount == 1);
    rig.edge.GetInlet(0)->SetFloat(-0.f, YSE::T_GUI); // -0 is zero
    rig.Block();
    CHECK(rig.rise.bangCount == 1);
    CHECK(rig.fall.bangCount == 1);
  }

  TEST_CASE("~edge: a connected signal wins over the number (#1121, #1036)") {
    Rig rig;
    rig.edge.GetInlet(0)->SetFloat(5.f, YSE::T_GUI);
    buffer in(N);
    in = 0.f;
    rig.Block(in);
    CHECK(rig.rise.bangCount == 0);
    CHECK(rig.fall.bangCount == 0);
  }

  TEST_CASE("~edge: in a rendered patch, a gated ~phasor bangs each cycle, after a JSON round "
            "trip (#1121)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    unsigned int risesID = 0;
    unsigned int fallsID = 0;
    {
      patcherImplementation src(1, nullptr);
      // 375 Hz at 48 kHz: one cycle per block. Above 0.5 for its second half.
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* gate = src.CreateObject(YSE::OBJ::D_GREATER, "0.5");
      YSE::pHandle* edge = src.CreateObject(YSE::OBJ::D_EDGE, "");
      YSE::pHandle* rises = src.CreateObject(YSE::OBJ::G_COUNTER, "");
      YSE::pHandle* falls = src.CreateObject(YSE::OBJ::G_COUNTER, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(gate != nullptr);
      REQUIRE(edge != nullptr);
      REQUIRE(rises != nullptr);
      REQUIRE(falls != nullptr);
      src.Connect(ramp, 0, gate, 0);
      src.Connect(gate, 0, edge, 0);
      src.Connect(edge, 0, rises, 0);
      src.Connect(edge, 1, falls, 0);
      risesID = rises->GetID();
      fallsID = falls->GetID();
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* rises = dst.GetHandleFromID(risesID);
    YSE::pHandle* falls = dst.GetHandleFromID(fallsID);
    REQUIRE(rises != nullptr);
    REQUIRE(falls != nullptr);
    CHECK(std::string(rises->Type()) == YSE::OBJ::G_COUNTER);

    // Every block rises halfway through; every block but the first also falls
    // at the wrap, on its first sample.
    for (int block = 1; block <= 6; ++block) {
      CAPTURE(block);
      dst.Calculate(YSE::T_DSP);
      CHECK(GuiInt(rises) == block);
      CHECK(GuiInt(falls) == block - 1);
    }
  }

  TEST_CASE("~edge: numbers, blocks and the flush on the audio thread allocate nothing (#1121)") {
    Rig rig;
    buffer in(N);
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = (i % 5u) == 0 ? 1.f : 0.f;
    rig.Block(in);
    {
      TestHelpers::ProbeScope probe;
      rig.edge.GetInlet(0)->SetFloat(0.3f, YSE::T_DSP);
      rig.edge.GetInlet(0)->SetInt(0, YSE::T_DSP);
      rig.Block(in);
      rig.Block(in);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(rig.rise.bangCount == 3);
    CHECK(rig.fall.bangCount == 3);
  }

} // TEST_SUITE("patcher")

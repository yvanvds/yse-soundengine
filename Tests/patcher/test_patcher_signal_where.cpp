// Tests for the ~where patcher object (patcher/analysis/dWhere), issue #1078.
// The signals are DSP::phasorWhere's and are tested in
// Tests/dsp/test_phasor_where.cpp; these show that the input reaches it and
// both outputs leave it, alone and in a rendered patch (a ~phasor measured by
// ~where) after a JSON round trip.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/analysis/dWhere.h"
#include "dsp/generators/phasor.hpp"
#include "dsp/generators/phasorWhere.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::phasorWhere;
using YSE::PATCHER::dWhere;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double MS = 1000.0 / 48000.0;

  void Wire(dWhere& obj, BufferSink& elapsed, BufferSink& remaining) {
    obj.ConnectOutlet(elapsed.GetInlet(0), 0);
    elapsed.ConnectInlet(obj.GetOutlet(0), 0);
    obj.ConnectOutlet(remaining.GetInlet(0), 1);
    remaining.ConnectInlet(obj.GetOutlet(1), 0);
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~where: registry and shape (#1078)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_WHERE);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~where");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 2);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(obj->OutputDataType(1) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_WHERE));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::ANALYSIS);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 0);
  }

  TEST_CASE("~where: a phasor in, elapsed and remaining out, against the DSP class (#1078)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink elapsed;
    BufferSink remaining;
    dWhere obj;
    Wire(obj, elapsed, remaining);
    phasorWhere reference;
    phasor objSource;
    phasor refSource;

    for (unsigned int block = 0; block < 3; ++block) {
      CAPTURE(block);
      obj.ResetDSP(); // as the patcher does before every block
      obj.GetInlet(0)->SetBuffer(&objSource(375.f, N), YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(elapsed.received != nullptr);
      REQUIRE(remaining.received != nullptr);
      CHECK(
          TestHelpers::buffersNearlyEqual(*elapsed.received, reference(refSource(375.f, N)), 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(*remaining.received, reference.remaining(), 0.f));
      // 128 samples per cycle: 32 samples in, 96 to go.
      CHECK(elapsed.received->getPtr()[32] == doctest::Approx(32 * MS));
      CHECK(remaining.received->getPtr()[32] == doctest::Approx(96 * MS));
    }

    // Unconnected: the inlet reads 0, which never moves.
    BufferSink e2;
    BufferSink r2;
    dWhere idle;
    Wire(idle, e2, r2);
    idle.Calculate(YSE::T_DSP);
    REQUIRE(e2.received != nullptr);
    REQUIRE(r2.received != nullptr);
    CHECK(e2.received->getPtr()[N - 1] == 0.f);
    CHECK(r2.received->getPtr()[N - 1] == 0.f);

    // No messages: they are ignored.
    obj.SetMessage("bang", 0.f);
    obj.SetMessage("reset 1", 0.f);
  }

  TEST_CASE("~where: in a rendered patch, a ~phasor measured by ~where, after a JSON round trip "
            "(#1078)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* where = src.CreateObject(YSE::OBJ::D_WHERE, "");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(where != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, where, 0);
      src.Connect(where, 0, dac, 0);
      src.Connect(where, 1, dac, 1);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    phasor source;
    phasorWhere reference;
    for (int block = 0; block < 4; ++block) {
      CAPTURE(block);
      dst.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(source(375.f, N)), 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[1], reference.remaining(), 0.f));
      CHECK(dst.output[0].getPtr()[100] == doctest::Approx(100 * MS));
      CHECK(dst.output[1].getPtr()[100] == doctest::Approx(28 * MS));
    }
  }

  TEST_CASE("~where: blocks on the audio thread allocate nothing (#1078)") {
    BufferSink elapsed;
    BufferSink remaining;
    dWhere obj;
    Wire(obj, elapsed, remaining);
    buffer in(N);
    in = 0.5f;
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.Calculate(YSE::T_DSP);
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

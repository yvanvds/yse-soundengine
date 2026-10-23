// Tests for the ~+= patcher object (patcher/math/dPlusEquals), issue #1047.
// The running sum is DSP::accumulator's and is tested in
// Tests/dsp/test_accumulator.cpp; these show that pins, messages and
// parameters reach it.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/dPlusEquals.h"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::PATCHER::dPlusEquals;
using YSE::PATCHER::patcherImplementation;

namespace {
  void Wire(dPlusEquals& op, BufferSink& sink) {
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);
  }

  constexpr float N = static_cast<float>(YSE::STANDARD_BUFFERSIZE);
  constexpr unsigned int Last = YSE::STANDARD_BUFFERSIZE - 1;
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~+=: registry and shape (#1047)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_PLUSEQUALS);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~+=");
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> obj(
        YSE::PATCHER::Register().Get(YSE::OBJ::D_PLUSEQUALS));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MATH);
    CHECK(obj->IsDSPObject());
  }

  TEST_CASE("~+=: a number on the left inlet is summed every sample (#1047)") {
    dPlusEquals op;
    BufferSink sink;
    Wire(op, sink);

    op.Calculate(YSE::T_DSP); // nothing set: sums 0
    REQUIRE(sink.received != nullptr);
    CHECK(sink.received->getPtr()[Last] == 0.f);

    op.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(sink.received->getPtr()[0] == 1.f);
    CHECK(sink.received->getPtr()[Last] == N);

    op.GetInlet(0)->SetInt(2, YSE::T_GUI);
    op.Calculate(YSE::T_DSP); // continues from N
    CHECK(sink.received->getPtr()[0] == N + 2.f);
  }

  TEST_CASE("~+=: initial, bang and set (#1047)") {
    dPlusEquals op;
    BufferSink sink;
    Wire(op, sink);
    op.SetParams("100");
    op.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);

    op.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CHECK(sink.received->getPtr()[0] == 101.f); // starts at initial

    op.GetInlet(0)->SetBang(YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(sink.received->getPtr()[0] == 1.f); // bang: back to 0, not initial

    op.GetInlet(0)->SetMessage("set 50", YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(sink.received->getPtr()[0] == 51.f);

    op.GetInlet(0)->SetMessage("set nonsense", YSE::T_GUI); // ignored
    op.Calculate(YSE::T_DSP);
    CHECK(sink.received->getPtr()[0] == 50.f + N + 1.f);
  }

  TEST_CASE("~+=: signals on both inlets; the reset signal restarts the sum (#1047, #1036)") {
    dPlusEquals op;
    BufferSink sink;
    Wire(op, sink);
    op.GetInlet(0)->SetFloat(100.f, YSE::T_GUI); // a connected signal wins

    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE), reset(YSE::STANDARD_BUFFERSIZE);
    in = 1.f;
    reset = 0.f;
    reset.getPtr()[10] = 1.f;
    op.GetInlet(1)->SetBuffer(&reset, YSE::T_GUI);
    op.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CHECK(sink.received->getPtr()[9] == 10.f);
    CHECK(sink.received->getPtr()[10] == 1.f);
    CHECK(sink.received->getPtr()[Last] == N - 10.f);
  }

  TEST_CASE("~+=: initial survives a DumpJSON / ParseJSON round trip (#1047)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* sum = src.CreateObject(YSE::OBJ::D_PLUSEQUALS, "2.5");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      src.Connect(sum, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    dst.Calculate(YSE::T_DSP);
    // Unconnected input reads as 0: the sum stays at initial.
    CHECK(dst.output[0].getPtr()[0] == doctest::Approx(2.5f));
    CHECK(dst.output[0].getPtr()[Last] == doctest::Approx(2.5f));
  }

  TEST_CASE("~+=: in a patch, a ramp from a host number, reset by a host bang (#1047)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* sum = p.CreateObject(YSE::OBJ::D_PLUSEQUALS, "10");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(sum, 0, dac, 0);
    sum->SetFloatData(0, 0.5f);

    p.Calculate(YSE::T_DSP);
    for (unsigned int i : {0u, 1u, 50u, YSE::STANDARD_BUFFERSIZE - 1}) {
      CAPTURE(i);
      CHECK(p.output[0].getPtr()[i] == doctest::Approx(10.f + 0.5f * (i + 1)));
    }
    p.Calculate(YSE::T_DSP); // the sum carries over the block boundary
    CHECK(p.output[0].getPtr()[0] == doctest::Approx(10.f + 0.5f * (N + 1)));

    sum->SetBang(0);
    p.Calculate(YSE::T_DSP);
    CHECK(p.output[0].getPtr()[0] == doctest::Approx(0.5f));

    // A ~phasor into the reset inlet: it is 0 only at its first sample, so
    // every later sample resets and the output is the input itself.
    YSE::pHandle* ramp = p.CreateObject(YSE::OBJ::D_PHASOR, "1000");
    p.Connect(ramp, 0, sum, 1);
    p.Calculate(YSE::T_DSP);
    CHECK(p.output[0].getPtr()[Last] == doctest::Approx(0.5f));
  }

  TEST_CASE("~+=: messages and blocks on the audio thread allocate nothing (#1047)") {
    dPlusEquals op;
    BufferSink sink;
    Wire(op, sink);
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    in = 0.5f;
    op.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      op.GetInlet(0)->SetFloat(0.3f, YSE::T_DSP);
      op.GetInlet(0)->SetInt(2, YSE::T_DSP);
      op.GetInlet(0)->SetBang(YSE::T_DSP);
      op.GetInlet(0)->SetMessage("set 4", YSE::T_DSP);
      op.Calculate(YSE::T_DSP);
      op.GetInlet(1)->SetBuffer(&in, YSE::T_DSP);
      op.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

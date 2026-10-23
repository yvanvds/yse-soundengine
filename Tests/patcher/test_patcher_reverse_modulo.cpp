// Tests for the ~!-, ~!/ and ~% patcher objects (patcher/math/dReverseSubstract,
// dReverseDivide, dModulo), issue #1046. The sample math is DSP::reverseSubtract,
// reverseDivide and modulo's, tested in Tests/dsp/test_arithmetic_operators.cpp;
// these show that pins and parameters reach it.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/dReverseSubstract.h"
#include "patcher/math/dReverseDivide.h"
#include "patcher/math/dModulo.h"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::PATCHER::dModulo;
using YSE::PATCHER::dReverseDivide;
using YSE::PATCHER::dReverseSubstract;
using YSE::PATCHER::patcherImplementation;

namespace {
  void Wire(YSE::PATCHER::pObject& op, BufferSink& sink) {
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);
  }

  bool AllEqual(YSE::DSP::buffer* b, float expected) {
    if (b == nullptr) return false;
    for (unsigned int i = 0; i < b->getLength(); ++i)
      if (b->getPtr()[i] != doctest::Approx(expected)) return false;
    return true;
  }

  // 100 samples a cycle at whatever rate the test binary runs.
  std::string HundredSampleFrequency() {
    return std::to_string(static_cast<float>(YSE::SAMPLERATE) / 100.f);
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("reverse/modulo: registry and shape (#1046)") {
    YSE::patcher p;
    p.create(1);
    for (const char* type :
         {YSE::OBJ::D_REVERSESUBSTRACT, YSE::OBJ::D_REVERSEDIVIDE, YSE::OBJ::D_MODULO}) {
      CAPTURE(type);
      YSE::pHandle* h = p.CreateObject(type);
      REQUIRE(h != nullptr);
      CHECK(std::string(h->Type()) == type);
      CHECK(h->GetInputs() == 2);
      CHECK(h->GetOutputs() == 1);
      CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(type));
      REQUIRE(obj != nullptr);
      CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MATH);
      CHECK(obj->IsDSPObject());
    }
    CHECK(std::string(YSE::OBJ::D_REVERSESUBSTRACT) == "~!-");
    CHECK(std::string(YSE::OBJ::D_REVERSEDIVIDE) == "~!/");
    CHECK(std::string(YSE::OBJ::D_MODULO) == "~%");
  }

  TEST_CASE("~!-: defaults, numbers on both inlets, and a creation argument (#1046)") {
    dReverseSubstract op;
    BufferSink sink;
    Wire(op, sink);

    op.Calculate(YSE::T_DSP); // 0 - 0
    CHECK(AllEqual(sink.received, 0.f));

    op.GetInlet(0)->SetFloat(0.25f, YSE::T_GUI);
    op.GetInlet(1)->SetFloat(1.f, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(AllEqual(sink.received, 0.75f)); // right - left

    op.GetInlet(0)->SetInt(3, YSE::T_GUI);
    op.GetInlet(1)->SetInt(10, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(AllEqual(sink.received, 7.f));

    dReverseSubstract withArg;
    BufferSink sink2;
    Wire(withArg, sink2);
    withArg.SetParams("1");
    withArg.GetInlet(0)->SetFloat(0.25f, YSE::T_GUI);
    withArg.Calculate(YSE::T_DSP);
    CHECK(AllEqual(sink2.received, 0.75f));
  }

  TEST_CASE("~!-: connected signals win over the numbers (#1046, #1036)") {
    dReverseSubstract op;
    BufferSink sink;
    Wire(op, sink);
    op.GetInlet(0)->SetFloat(100.f, YSE::T_GUI);
    op.GetInlet(1)->SetFloat(100.f, YSE::T_GUI);

    YSE::DSP::buffer left(YSE::STANDARD_BUFFERSIZE), right(YSE::STANDARD_BUFFERSIZE);
    left = 2.f;
    right = 5.f;
    op.GetInlet(1)->SetBuffer(&right, YSE::T_GUI);
    op.GetInlet(0)->SetBuffer(&left, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(AllEqual(sink.received, 3.f));
  }

  TEST_CASE("~!/: defaults to the reciprocal, and a left 0 gives 0 (#1046)") {
    dReverseDivide op;
    BufferSink sink;
    Wire(op, sink);

    op.Calculate(YSE::T_DSP); // 1 / 0 -> 0
    CHECK(AllEqual(sink.received, 0.f));

    YSE::DSP::buffer left(YSE::STANDARD_BUFFERSIZE);
    left = 4.f;
    op.GetInlet(0)->SetBuffer(&left, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(AllEqual(sink.received, 0.25f)); // 1 / 4

    op.GetInlet(1)->SetInt(2, YSE::T_GUI);
    op.GetInlet(0)->SetBuffer(&left, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(AllEqual(sink.received, 0.5f)); // 2 / 4

    YSE::DSP::buffer right(YSE::STANDARD_BUFFERSIZE);
    right = 12.f;
    op.GetInlet(1)->SetBuffer(&right, YSE::T_GUI);
    op.GetInlet(0)->SetBuffer(&left, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(AllEqual(sink.received, 3.f)); // 12 / 4
  }

  TEST_CASE("~%: remainder with the sign of the left input, divisor 0 gives 0 (#1046)") {
    dModulo op;
    BufferSink sink;
    Wire(op, sink);

    op.GetInlet(0)->SetFloat(2.75f, YSE::T_GUI);
    op.Calculate(YSE::T_DSP); // default divisor 1
    CHECK(AllEqual(sink.received, 0.75f));

    op.GetInlet(0)->SetFloat(-5.5f, YSE::T_GUI);
    op.GetInlet(1)->SetFloat(2.f, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(AllEqual(sink.received, -1.5f));

    op.GetInlet(1)->SetInt(0, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(AllEqual(sink.received, 0.f));

    YSE::DSP::buffer left(YSE::STANDARD_BUFFERSIZE), right(YSE::STANDARD_BUFFERSIZE);
    left = 7.f;
    right = 4.f;
    op.GetInlet(1)->SetBuffer(&right, YSE::T_GUI);
    op.GetInlet(0)->SetBuffer(&left, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(AllEqual(sink.received, 3.f));
  }

  TEST_CASE("reverse/modulo: parameters survive a DumpJSON / ParseJSON round trip (#1046)") {
    std::string dump;
    {
      patcherImplementation src(3, nullptr);
      YSE::pHandle* sub = src.CreateObject(YSE::OBJ::D_REVERSESUBSTRACT, "1.5");
      YSE::pHandle* div = src.CreateObject(YSE::OBJ::D_REVERSEDIVIDE, "3");
      YSE::pHandle* mod = src.CreateObject(YSE::OBJ::D_MODULO, "0.75");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      src.Connect(sub, 0, dac, 0);
      src.Connect(div, 0, dac, 1);
      src.Connect(mod, 0, dac, 2);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(3, nullptr);
    dst.ParseJSON(dump);
    dst.Calculate(YSE::T_DSP);
    // Unconnected left inlets read as 0.
    CHECK(dst.output[0].getPtr()[0] == doctest::Approx(1.5f)); // 1.5 - 0
    CHECK(dst.output[1].getPtr()[0] == doctest::Approx(0.f)); // 3 / 0 -> 0
    CHECK(dst.output[2].getPtr()[0] == doctest::Approx(0.f)); // 0 % 0.75
  }

  TEST_CASE("reverse/modulo: in a patch, a phase ramp through ~!- 1, ~!/ 1 and ~% 0.25 (#1046)") {
    patcherImplementation p(3, nullptr);
    YSE::pHandle* ramp = p.CreateObject(YSE::OBJ::D_PHASOR, HundredSampleFrequency());
    YSE::pHandle* sub = p.CreateObject(YSE::OBJ::D_REVERSESUBSTRACT, "1");
    YSE::pHandle* div = p.CreateObject(YSE::OBJ::D_REVERSEDIVIDE, "1");
    YSE::pHandle* mod = p.CreateObject(YSE::OBJ::D_MODULO, "0.25");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(ramp, 0, sub, 0);
    p.Connect(ramp, 0, div, 0);
    p.Connect(ramp, 0, mod, 0);
    p.Connect(sub, 0, dac, 0);
    p.Connect(div, 0, dac, 1);
    p.Connect(mod, 0, dac, 2);
    p.Calculate(YSE::T_DSP);

    // The ramp is i / 100 at sample i.
    for (unsigned int i : {0u, 10u, 30u, 50u, 99u}) {
      CAPTURE(i);
      const float x = static_cast<float>(i) / 100.f;
      CHECK(p.output[0].getPtr()[i] == doctest::Approx(1.f - x).epsilon(1e-4));
      CHECK(p.output[2].getPtr()[i] == doctest::Approx(std::fmod(x, 0.25f)).epsilon(1e-3));
    }
    CHECK(p.output[1].getPtr()[0] == 0.f); // 1 / 0 -> 0
    CHECK(p.output[1].getPtr()[50] == doctest::Approx(2.f).epsilon(1e-4));
    CHECK(p.output[1].getPtr()[25] == doctest::Approx(4.f).epsilon(1e-4));
  }

  TEST_CASE("reverse/modulo: numbers and blocks on the audio thread allocate nothing (#1046)") {
    dReverseSubstract sub;
    dReverseDivide div;
    dModulo mod;
    BufferSink s1, s2, s3;
    Wire(sub, s1);
    Wire(div, s2);
    Wire(mod, s3);
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    in = 0.5f;
    sub.Calculate(YSE::T_DSP);
    div.Calculate(YSE::T_DSP);
    mod.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      for (YSE::PATCHER::pObject* op :
           {static_cast<YSE::PATCHER::pObject*>(&sub), static_cast<YSE::PATCHER::pObject*>(&div),
            static_cast<YSE::PATCHER::pObject*>(&mod)}) {
        op->GetInlet(0)->SetFloat(0.3f, YSE::T_DSP);
        op->GetInlet(0)->SetInt(2, YSE::T_DSP);
        op->GetInlet(1)->SetFloat(0.7f, YSE::T_DSP);
        op->GetInlet(1)->SetInt(3, YSE::T_DSP);
        op->Calculate(YSE::T_DSP);
        op->GetInlet(1)->SetBuffer(&in, YSE::T_DSP);
        op->GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
        op->Calculate(YSE::T_DSP);
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

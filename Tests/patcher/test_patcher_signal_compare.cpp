// Tests for the ~== ~!= ~> ~>= ~< ~<= ~minimum ~maximum patcher objects
// (patcher/math/dCompare), issue #1048. The sample math is DSP::compare,
// minimum and maximum's, tested in Tests/dsp/test_compare.cpp; these show
// that pins and parameters reach it.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/dCompare.h"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
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

  // Left and right numbers into a fresh T, one block out, the value of its
  // first sample. The sink is declared first so it outlives the object.
  template <class T> float Numbers(float left, float right) {
    BufferSink sink;
    T op;
    Wire(op, sink);
    op.GetInlet(0)->SetFloat(left, YSE::T_GUI);
    op.GetInlet(1)->SetFloat(right, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    REQUIRE(AllEqual(sink.received, sink.received->getPtr()[0]));
    return sink.received->getPtr()[0];
  }

  // 100 samples a cycle at whatever rate the test binary runs.
  std::string HundredSampleFrequency() {
    return std::to_string(static_cast<float>(YSE::SAMPLERATE) / 100.f);
  }

  struct Family {
    const char* type;
    bool fuzzy;
  };

  const Family kFamily[] = {
      {YSE::OBJ::D_EQUAL, true},        {YSE::OBJ::D_NOTEQUAL, true}, {YSE::OBJ::D_GREATER, false},
      {YSE::OBJ::D_GREATEREQUAL, true}, {YSE::OBJ::D_LESS, false},    {YSE::OBJ::D_LESSEQUAL, true},
      {YSE::OBJ::D_MINIMUM, false},     {YSE::OBJ::D_MAXIMUM, false},
  };
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("signal compare: registry, shape and parameters (#1048)") {
    YSE::patcher p;
    p.create(1);
    for (const Family& f : kFamily) {
      CAPTURE(f.type);
      YSE::pHandle* h = p.CreateObject(f.type);
      REQUIRE(h != nullptr);
      CHECK(std::string(h->Type()) == f.type);
      CHECK(h->GetInputs() == 2);
      CHECK(h->GetOutputs() == 1);
      CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(f.type));
      REQUIRE(obj != nullptr);
      CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MATH);
      CHECK(obj->IsDSPObject());
      CHECK(std::string(obj->Type()) == f.type);
    }
    CHECK(std::string(YSE::OBJ::D_EQUAL) == "~==");
    CHECK(std::string(YSE::OBJ::D_NOTEQUAL) == "~!=");
    CHECK(std::string(YSE::OBJ::D_GREATER) == "~>");
    CHECK(std::string(YSE::OBJ::D_GREATEREQUAL) == "~>=");
    CHECK(std::string(YSE::OBJ::D_LESS) == "~<");
    CHECK(std::string(YSE::OBJ::D_LESSEQUAL) == "~<=");
    CHECK(std::string(YSE::OBJ::D_MINIMUM) == "~minimum");
    CHECK(std::string(YSE::OBJ::D_MAXIMUM) == "~maximum");
  }

  TEST_CASE("signal compare: numbers on both inlets reach each operator (#1048)") {
    using namespace YSE::PATCHER;
    CHECK(Numbers<dEqual>(2.f, 2.f) == 1.f);
    CHECK(Numbers<dEqual>(2.f, 3.f) == 0.f);
    CHECK(Numbers<dNotEqual>(2.f, 3.f) == 1.f);
    CHECK(Numbers<dNotEqual>(3.f, 3.f) == 0.f);
    CHECK(Numbers<dGreater>(3.f, 2.f) == 1.f);
    CHECK(Numbers<dGreater>(2.f, 2.f) == 0.f);
    CHECK(Numbers<dGreaterEqual>(2.f, 2.f) == 1.f);
    CHECK(Numbers<dGreaterEqual>(1.f, 2.f) == 0.f);
    CHECK(Numbers<dLess>(2.f, 3.f) == 1.f);
    CHECK(Numbers<dLess>(3.f, 3.f) == 0.f);
    CHECK(Numbers<dLessEqual>(3.f, 3.f) == 1.f);
    CHECK(Numbers<dLessEqual>(4.f, 3.f) == 0.f);
    CHECK(Numbers<dMinimum>(-2.f, 5.f) == -2.f);
    CHECK(Numbers<dMaximum>(-2.f, 5.f) == 5.f);
  }

  TEST_CASE("signal compare: defaults, ints, and creation arguments (#1048)") {
    using namespace YSE::PATCHER;
    dGreater op;
    BufferSink sink;
    Wire(op, sink);
    op.Calculate(YSE::T_DSP); // 0 > 0
    CHECK(AllEqual(sink.received, 0.f));
    op.GetInlet(0)->SetInt(1, YSE::T_GUI);
    op.Calculate(YSE::T_DSP); // 1 > 0
    CHECK(AllEqual(sink.received, 1.f));
    op.GetInlet(1)->SetInt(4, YSE::T_GUI);
    op.Calculate(YSE::T_DSP); // 1 > 4
    CHECK(AllEqual(sink.received, 0.f));

    dMaximum withArg;
    BufferSink sink2;
    Wire(withArg, sink2);
    withArg.SetParams("0.5");
    withArg.GetInlet(0)->SetFloat(0.25f, YSE::T_GUI);
    withArg.Calculate(YSE::T_DSP);
    CHECK(AllEqual(sink2.received, 0.5f));

    // ~== 1 0.1: the second argument is the fuzzy tolerance.
    dEqual fuzzy;
    BufferSink sink3;
    Wire(fuzzy, sink3);
    fuzzy.SetParams("1 0.1");
    fuzzy.GetInlet(0)->SetFloat(1.05f, YSE::T_GUI);
    fuzzy.Calculate(YSE::T_DSP);
    CHECK(AllEqual(sink3.received, 1.f));
    fuzzy.GetInlet(0)->SetFloat(1.2f, YSE::T_GUI);
    fuzzy.Calculate(YSE::T_DSP);
    CHECK(AllEqual(sink3.received, 0.f));

    // Without fuzzy the same values compare exactly.
    dEqual exact;
    BufferSink sink4;
    Wire(exact, sink4);
    exact.SetParams("1");
    exact.GetInlet(0)->SetFloat(1.05f, YSE::T_GUI);
    exact.Calculate(YSE::T_DSP);
    CHECK(AllEqual(sink4.received, 0.f));
  }

  TEST_CASE("signal compare: connected signals win over the numbers (#1048, #1036)") {
    using namespace YSE::PATCHER;
    YSE::DSP::buffer left(YSE::STANDARD_BUFFERSIZE), right(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < left.getLength(); ++i) {
      left.getPtr()[i] = static_cast<float>(i);
      right.getPtr()[i] = 63.5f;
    }

    dLess op;
    BufferSink sink;
    Wire(op, sink);
    op.GetInlet(0)->SetFloat(100.f, YSE::T_GUI);
    op.GetInlet(1)->SetFloat(-100.f, YSE::T_GUI);
    op.GetInlet(1)->SetBuffer(&right, YSE::T_GUI);
    op.GetInlet(0)->SetBuffer(&left, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    REQUIRE(sink.received->getLength() == YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < sink.received->getLength(); ++i) {
      CAPTURE(i);
      CHECK(sink.received->getPtr()[i] == (i < 64 ? 1.f : 0.f));
    }

    // Left signal against the right number.
    dMinimum lo;
    BufferSink sink2;
    Wire(lo, sink2);
    lo.GetInlet(1)->SetFloat(10.f, YSE::T_GUI);
    lo.GetInlet(0)->SetBuffer(&left, YSE::T_GUI);
    lo.Calculate(YSE::T_DSP);
    REQUIRE(sink2.received != nullptr);
    for (unsigned int i = 0; i < sink2.received->getLength(); ++i) {
      CAPTURE(i);
      CHECK(sink2.received->getPtr()[i] == std::min(static_cast<float>(i), 10.f));
    }
  }

  TEST_CASE("signal compare: parameters survive a DumpJSON / ParseJSON round trip (#1048)") {
    std::string dump;
    {
      patcherImplementation src(3, nullptr);
      YSE::pHandle* eq = src.CreateObject(YSE::OBJ::D_EQUAL, "0.05 0.1");
      YSE::pHandle* le = src.CreateObject(YSE::OBJ::D_LESSEQUAL, "-0.25 0.5");
      YSE::pHandle* mx = src.CreateObject(YSE::OBJ::D_MAXIMUM, "0.75");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      src.Connect(eq, 0, dac, 0);
      src.Connect(le, 0, dac, 1);
      src.Connect(mx, 0, dac, 2);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(3, nullptr);
    dst.ParseJSON(dump);
    dst.Calculate(YSE::T_DSP);
    // Unconnected left inlets read as 0.
    CHECK(dst.output[0].getPtr()[0] == 1.f); // |0 - 0.05| <= 0.1
    CHECK(dst.output[1].getPtr()[0] == 1.f); // 0 <= -0.25 + 0.5
    CHECK(dst.output[2].getPtr()[0] == doctest::Approx(0.75f)); // max(0, 0.75)
  }

  TEST_CASE("signal compare: in a patch, a phase ramp through ~> ~<= ~minimum ~maximum (#1048)") {
    patcherImplementation p(4, nullptr);
    YSE::pHandle* ramp = p.CreateObject(YSE::OBJ::D_PHASOR, HundredSampleFrequency());
    YSE::pHandle* gt = p.CreateObject(YSE::OBJ::D_GREATER, "0.5");
    YSE::pHandle* le = p.CreateObject(YSE::OBJ::D_LESSEQUAL, "0.25");
    YSE::pHandle* lo = p.CreateObject(YSE::OBJ::D_MINIMUM, "0.3");
    YSE::pHandle* hi = p.CreateObject(YSE::OBJ::D_MAXIMUM, "0.6");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    for (YSE::pHandle* h : {gt, le, lo, hi})
      p.Connect(ramp, 0, h, 0);
    p.Connect(gt, 0, dac, 0);
    p.Connect(le, 0, dac, 1);
    p.Connect(lo, 0, dac, 2);
    p.Connect(hi, 0, dac, 3);
    p.Calculate(YSE::T_DSP);

    // The ramp is i / 100 at sample i; stay clear of the exact thresholds.
    for (unsigned int i : {0u, 10u, 20u, 30u, 49u, 51u, 70u, 99u}) {
      CAPTURE(i);
      const float x = static_cast<float>(i) / 100.f;
      CHECK(p.output[0].getPtr()[i] == (x > 0.5f ? 1.f : 0.f));
      CHECK(p.output[1].getPtr()[i] == (x <= 0.25f ? 1.f : 0.f));
      CHECK(p.output[2].getPtr()[i] == doctest::Approx(std::min(x, 0.3f)).epsilon(1e-4));
      CHECK(p.output[3].getPtr()[i] == doctest::Approx(std::max(x, 0.6f)).epsilon(1e-4));
    }
  }

  TEST_CASE("signal compare: numbers and blocks on the audio thread allocate nothing (#1048)") {
    using namespace YSE::PATCHER;
    dEqual eq;
    dNotEqual ne;
    dGreater gt;
    dGreaterEqual ge;
    dLess lt;
    dLessEqual le;
    dMinimum lo;
    dMaximum hi;
    pObject* all[] = {&eq, &ne, &gt, &ge, &lt, &le, &lo, &hi};
    BufferSink sinks[8];
    for (unsigned int i = 0; i < 8; ++i) {
      Wire(*all[i], sinks[i]);
      all[i]->Calculate(YSE::T_DSP);
    }
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    in = 0.5f;
    {
      TestHelpers::ProbeScope probe;
      for (pObject* op : all) {
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

// MSP retrofit of the ~+ ~- ~* ~/ patcher objects (patcher/math/dAdd,
// dSubstract, dMultiply, dDivide), issue #1044. The sample math is
// DSP::buffer's operators, tested in Tests/dsp/test_buffer.cpp; these show the
// MSP surface of plus~ / minus~ / times~ / div~ reaches it:
//
//   - both inlets take a signal or a number; a number is used while no signal
//     is connected to that inlet, and a connected signal wins (#1036);
//   - the right number is the creation argument and the "right" parameter;
//   - a divisor of 0 gives 0, the same for a number and a signal;
//   - the numbers are atomics a control thread can write while the audio
//     thread calculates (the concurrency case is a TSan gate).

#include <doctest/doctest.h>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/dAdd.h"
#include "patcher/math/dSubstract.h"
#include "patcher/math/dMultiply.h"
#include "patcher/math/dDivide.h"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::PATCHER::dAdd;
using YSE::PATCHER::dDivide;
using YSE::PATCHER::dMultiply;
using YSE::PATCHER::dSubstract;
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

  TEST_CASE("~+ ~- ~* ~/: registry and shape (#1044)") {
    YSE::patcher p;
    p.create(1);
    for (const char* type :
         {YSE::OBJ::D_ADD, YSE::OBJ::D_SUBSTRACT, YSE::OBJ::D_MULTIPLY, YSE::OBJ::D_DIVIDE}) {
      CAPTURE(type);
      YSE::pHandle* h = p.CreateObject(type);
      REQUIRE(h != nullptr);
      CHECK(h->GetInputs() == 2);
      CHECK(h->GetOutputs() == 1);
      CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(type));
      REQUIRE(obj != nullptr);
      CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MATH);
      CHECK(obj->IsDSPObject());
    }
  }

  TEST_CASE("~+ ~- ~* ~/: a number on the left inlet stands in for the left signal (#1044)") {
    // MSP: a float or int in the left inlet is used while no signal is
    // connected there.
    dAdd add;
    dSubstract sub;
    dMultiply mul;
    dDivide div;
    BufferSink sa, ss, sm, sd;
    Wire(add, sa);
    Wire(sub, ss);
    Wire(mul, sm);
    Wire(div, sd);

    for (YSE::PATCHER::pObject* op :
         {static_cast<YSE::PATCHER::pObject*>(&add), static_cast<YSE::PATCHER::pObject*>(&sub),
          static_cast<YSE::PATCHER::pObject*>(&mul), static_cast<YSE::PATCHER::pObject*>(&div)}) {
      op->GetInlet(0)->SetFloat(6.f, YSE::T_GUI);
      op->GetInlet(1)->SetFloat(2.f, YSE::T_GUI);
      op->Calculate(YSE::T_DSP);
    }
    CHECK(AllEqual(sa.received, 8.f));
    CHECK(AllEqual(ss.received, 4.f));
    CHECK(AllEqual(sm.received, 12.f));
    CHECK(AllEqual(sd.received, 3.f));

    // An int works as the equivalent float.
    for (YSE::PATCHER::pObject* op :
         {static_cast<YSE::PATCHER::pObject*>(&add), static_cast<YSE::PATCHER::pObject*>(&sub),
          static_cast<YSE::PATCHER::pObject*>(&mul), static_cast<YSE::PATCHER::pObject*>(&div)}) {
      op->GetInlet(0)->SetInt(10, YSE::T_GUI);
      op->Calculate(YSE::T_DSP);
    }
    CHECK(AllEqual(sa.received, 12.f));
    CHECK(AllEqual(ss.received, 8.f));
    CHECK(AllEqual(sm.received, 20.f));
    CHECK(AllEqual(sd.received, 5.f));
  }

  TEST_CASE("~+ ~- ~* ~/: a left number against a right signal (#1044)") {
    dSubstract sub;
    dDivide div;
    BufferSink ss, sd;
    Wire(sub, ss);
    Wire(div, sd);
    YSE::DSP::buffer right(YSE::STANDARD_BUFFERSIZE);
    right = 4.f;

    sub.GetInlet(0)->SetFloat(10.f, YSE::T_GUI);
    sub.GetInlet(1)->SetFloat(100.f, YSE::T_GUI); // ignored: a signal is connected
    sub.GetInlet(1)->SetBuffer(&right, YSE::T_DSP);
    sub.Calculate(YSE::T_DSP);
    CHECK(AllEqual(ss.received, 6.f));

    div.GetInlet(0)->SetFloat(10.f, YSE::T_GUI);
    div.GetInlet(1)->SetBuffer(&right, YSE::T_DSP);
    div.Calculate(YSE::T_DSP);
    CHECK(AllEqual(sd.received, 2.5f));
  }

  TEST_CASE("~+ ~- ~* ~/: connected signals on both inlets win over both numbers (#1044)") {
    dMultiply mul;
    BufferSink sink;
    Wire(mul, sink);
    mul.GetInlet(0)->SetFloat(100.f, YSE::T_GUI);
    mul.GetInlet(1)->SetFloat(100.f, YSE::T_GUI);

    YSE::DSP::buffer left(YSE::STANDARD_BUFFERSIZE), right(YSE::STANDARD_BUFFERSIZE);
    left = 3.f;
    right = 5.f;
    mul.GetInlet(1)->SetBuffer(&right, YSE::T_DSP);
    mul.GetInlet(0)->SetBuffer(&left, YSE::T_DSP);
    mul.Calculate(YSE::T_DSP);
    CHECK(AllEqual(sink.received, 15.f));
  }

  TEST_CASE("~/: a divisor of 0 gives 0, as a number and as a signal (#1044)") {
    dDivide div;
    BufferSink sink;
    Wire(div, sink);
    YSE::DSP::buffer left(YSE::STANDARD_BUFFERSIZE);
    left = 5.f;

    // Number 0: the result is 0, the same as a signal divisor of 0 (it used to
    // pass the left signal through unchanged).
    div.GetInlet(1)->SetFloat(0.f, YSE::T_GUI);
    div.GetInlet(0)->SetBuffer(&left, YSE::T_DSP);
    div.Calculate(YSE::T_DSP);
    CHECK(AllEqual(sink.received, 0.f));

    div.GetInlet(1)->SetInt(0, YSE::T_GUI);
    div.GetInlet(0)->SetFloat(7.f, YSE::T_GUI);
    div.Calculate(YSE::T_DSP);
    CHECK(AllEqual(sink.received, 0.f));

    YSE::DSP::buffer zero(YSE::STANDARD_BUFFERSIZE);
    zero = 0.f;
    div.GetInlet(1)->SetBuffer(&zero, YSE::T_DSP);
    div.GetInlet(0)->SetBuffer(&left, YSE::T_DSP);
    div.Calculate(YSE::T_DSP);
    CHECK(AllEqual(sink.received, 0.f));
  }

  TEST_CASE("~+ ~- ~* ~/: defaults pass the signal through, a creation argument sets the right "
            "number (#1044)") {
    patcherImplementation p(8, nullptr);
    const char* types[] = {YSE::OBJ::D_ADD, YSE::OBJ::D_SUBSTRACT, YSE::OBJ::D_MULTIPLY,
                           YSE::OBJ::D_DIVIDE};
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    for (int i = 0; i < 4; ++i) {
      YSE::pHandle* bare = p.CreateObject(types[i], "");
      YSE::pHandle* withArg = p.CreateObject(types[i], "2");
      bare->SetFloatData(0, 6.f); // the left number stands in for a signal of 6
      withArg->SetFloatData(0, 6.f);
      p.Connect(bare, 0, dac, i);
      p.Connect(withArg, 0, dac, 4 + i);
    }
    p.Calculate(YSE::T_DSP);
    for (int i = 0; i < 4; ++i) {
      CAPTURE(types[i]);
      CHECK(p.output[i].getPtr()[0] == doctest::Approx(6.f));
    }
    CHECK(p.output[4].getPtr()[0] == doctest::Approx(8.f));
    CHECK(p.output[5].getPtr()[0] == doctest::Approx(4.f));
    CHECK(p.output[6].getPtr()[0] == doctest::Approx(12.f));
    CHECK(p.output[7].getPtr()[0] == doctest::Approx(3.f));
  }

  TEST_CASE(
      "~+ ~- ~* ~/: the right number argument survives a DumpJSON / ParseJSON round trip (#1044)") {
    std::string dump;
    {
      patcherImplementation src(4, nullptr);
      YSE::pHandle* add = src.CreateObject(YSE::OBJ::D_ADD, "1.5");
      YSE::pHandle* sub = src.CreateObject(YSE::OBJ::D_SUBSTRACT, "2.5");
      YSE::pHandle* mul = src.CreateObject(YSE::OBJ::D_MULTIPLY, "0.5");
      YSE::pHandle* div = src.CreateObject(YSE::OBJ::D_DIVIDE, "4");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      int ch = 0;
      for (YSE::pHandle* h : {add, sub, mul, div}) {
        src.Connect(h, 0, dac, ch++);
      }
      dump = src.DumpJSON();
    }
    patcherImplementation dst(4, nullptr);
    dst.ParseJSON(dump);
    // The left number is an inlet value, not a parameter: set it again.
    for (unsigned int i = 0; i < dst.Objects(); ++i) {
      YSE::pHandle* h = dst.GetHandleFromList(i);
      if (std::string(h->Type()) != YSE::OBJ::D_DAC) h->SetFloatData(0, 6.f);
    }
    dst.Calculate(YSE::T_DSP);
    CHECK(dst.output[0].getPtr()[0] == doctest::Approx(7.5f));
    CHECK(dst.output[1].getPtr()[0] == doctest::Approx(3.5f));
    CHECK(dst.output[2].getPtr()[0] == doctest::Approx(3.f));
    CHECK(dst.output[3].getPtr()[0] == doctest::Approx(1.5f));
  }

  TEST_CASE("~+ ~- ~* ~/: in a patch, a phase ramp scaled to -1..1 and divided (#1044)") {
    patcherImplementation p(2, nullptr);
    YSE::pHandle* ramp = p.CreateObject(YSE::OBJ::D_PHASOR, HundredSampleFrequency());
    YSE::pHandle* mul = p.CreateObject(YSE::OBJ::D_MULTIPLY, "2");
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::D_ADD, "-1");
    YSE::pHandle* div = p.CreateObject(YSE::OBJ::D_DIVIDE, "");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(ramp, 0, mul, 0);
    p.Connect(mul, 0, add, 0);
    p.Connect(add, 0, dac, 0);
    // 1 / ramp: the left number over a right signal, 0 where the ramp is 0.
    p.Connect(ramp, 0, div, 1);
    p.Connect(div, 0, dac, 1);
    div->SetFloatData(0, 1.f);
    p.Calculate(YSE::T_DSP);

    // The ramp is i / 100 at sample i.
    for (unsigned int i : {0u, 10u, 25u, 50u, 99u}) {
      CAPTURE(i);
      const float x = static_cast<float>(i) / 100.f;
      CHECK(p.output[0].getPtr()[i] == doctest::Approx(2.f * x - 1.f).epsilon(1e-4));
    }
    CHECK(p.output[1].getPtr()[0] == 0.f);
    CHECK(p.output[1].getPtr()[25] == doctest::Approx(4.f).epsilon(1e-4));
    CHECK(p.output[1].getPtr()[50] == doctest::Approx(2.f).epsilon(1e-4));
  }

  TEST_CASE("~+ ~- ~* ~/: numbers and blocks on the audio thread allocate nothing (#1044)") {
    dAdd add;
    dSubstract sub;
    dMultiply mul;
    dDivide div;
    BufferSink s1, s2, s3, s4;
    Wire(add, s1);
    Wire(sub, s2);
    Wire(mul, s3);
    Wire(div, s4);
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    in = 0.5f;
    YSE::PATCHER::pObject* ops[] = {&add, &sub, &mul, &div};
    for (YSE::PATCHER::pObject* op : ops)
      op->Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      for (YSE::PATCHER::pObject* op : ops) {
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

  TEST_CASE("~+ ~/: a control thread sets the numbers while the audio thread calculates (#1044)") {
    // The numbers were a plain float on ~+ and ~/ while the audio thread read
    // them: a data race. A TSan build flags the old code; every build checks
    // that each block comes out whole, from one value of each number.
    dAdd add;
    dDivide div;
    BufferSink sa, sd;
    Wire(add, sa);
    Wire(div, sd);
    add.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    add.GetInlet(1)->SetFloat(1.f, YSE::T_GUI);
    div.GetInlet(0)->SetFloat(8.f, YSE::T_GUI);
    div.GetInlet(1)->SetFloat(2.f, YSE::T_GUI);

    std::atomic<bool> stop{false};
    std::thread control([&] {
      int n = 0;
      while (!stop.load()) {
        const float v = (n++ % 2) ? 1.f : 2.f;
        add.GetInlet(1)->SetFloat(v, YSE::T_GUI);
        div.GetInlet(1)->SetFloat(v * 2.f, YSE::T_GUI);
      }
    });

    bool whole = true;
    for (int block = 0; block < 2000; ++block) {
      add.Calculate(YSE::T_DSP);
      div.Calculate(YSE::T_DSP);
      const float a = sa.received->getPtr()[0];
      const float d = sd.received->getPtr()[0];
      if (!(a == 2.f || a == 3.f) || !AllEqual(sa.received, a)) whole = false;
      if (!(d == 4.f || d == 2.f) || !AllEqual(sd.received, d)) whole = false;
    }
    stop.store(true);
    control.join();
    CHECK(whole);
  }

} // TEST_SUITE("patcher")

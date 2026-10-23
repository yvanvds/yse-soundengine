// Tests for the signal trig family ~cos ~cosx ~sinx ~tanx ~acos ~asin ~atan
// ~atan2 ~cosh ~sinh ~tanh ~acosh ~asinh ~atanh (patcher/math/dTrig), issue
// #1049. The sample math is DSP::trig, DSP::arcTangent2 and DSP::cosine's,
// tested in Tests/dsp/test_trig.cpp; these show that pins and parameters
// reach it, in isolation and in a running patch.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/dTrig.h"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::PATCHER::patcherImplementation;

namespace {
  const float kPi = 3.14159265f;

  void Wire(YSE::PATCHER::pObject& op, BufferSink& sink) {
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);
  }

  bool AllNear(YSE::DSP::buffer* b, float expected) {
    if (b == nullptr) return false;
    for (unsigned int i = 0; i < b->getLength(); ++i)
      if (b->getPtr()[i] != doctest::Approx(expected).epsilon(1e-3)) return false;
    return true;
  }

  // A number into a fresh one-inlet T, one block out, its first sample. The
  // sink is declared first so it outlives the object.
  template <class T> float Number(float in) {
    BufferSink sink;
    T op;
    Wire(op, sink);
    op.GetInlet(0)->SetFloat(in, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    REQUIRE(sink.received->getLength() == YSE::STANDARD_BUFFERSIZE);
    REQUIRE(AllNear(sink.received, sink.received->getPtr()[0]));
    return sink.received->getPtr()[0];
  }

  // 100 samples a cycle at whatever rate the test binary runs.
  std::string HundredSampleFrequency() {
    return std::to_string(static_cast<float>(YSE::SAMPLERATE) / 100.f);
  }

  struct Family {
    const char* type;
    const char* name;
    int inputs;
  };

  const Family kFamily[] = {
      {YSE::OBJ::D_COS, "~cos", 1},     {YSE::OBJ::D_COSX, "~cosx", 1},
      {YSE::OBJ::D_SINX, "~sinx", 1},   {YSE::OBJ::D_TANX, "~tanx", 1},
      {YSE::OBJ::D_ACOS, "~acos", 1},   {YSE::OBJ::D_ASIN, "~asin", 1},
      {YSE::OBJ::D_ATAN, "~atan", 1},   {YSE::OBJ::D_ATAN2, "~atan2", 2},
      {YSE::OBJ::D_COSH, "~cosh", 1},   {YSE::OBJ::D_SINH, "~sinh", 1},
      {YSE::OBJ::D_TANH, "~tanh", 1},   {YSE::OBJ::D_ACOSH, "~acosh", 1},
      {YSE::OBJ::D_ASINH, "~asinh", 1}, {YSE::OBJ::D_ATANH, "~atanh", 1},
  };
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("signal trig: registry, names and shape (#1049)") {
    YSE::patcher p;
    p.create(1);
    for (const Family& f : kFamily) {
      CAPTURE(f.name);
      CHECK(std::string(f.type) == f.name);
      YSE::pHandle* h = p.CreateObject(f.type);
      REQUIRE(h != nullptr);
      CHECK(std::string(h->Type()) == f.name);
      CHECK(h->GetInputs() == f.inputs);
      CHECK(h->GetOutputs() == 1);
      CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(f.type));
      REQUIRE(obj != nullptr);
      CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MATH);
      CHECK(obj->IsDSPObject());
      CHECK(std::string(obj->Type()) == f.name);
    }
  }

  TEST_CASE("signal trig: a number reaches each function (#1049)") {
    using namespace YSE::PATCHER;
    CHECK(Number<dCos>(0.f) == doctest::Approx(1.f).epsilon(1e-3));
    CHECK(Number<dCos>(0.5f) == doctest::Approx(-1.f).epsilon(1e-3)); // phase, not radians
    CHECK(Number<dCos>(0.25f) == doctest::Approx(0.f).epsilon(1e-3).scale(1.f));
    CHECK(Number<dCosx>(kPi) == doctest::Approx(-1.f));
    CHECK(Number<dSinx>(kPi / 2.f) == doctest::Approx(1.f));
    CHECK(Number<dTanx>(kPi / 4.f) == doctest::Approx(1.f));
    CHECK(Number<dAcos>(0.f) == doctest::Approx(kPi / 2.f));
    CHECK(Number<dAsin>(1.f) == doctest::Approx(kPi / 2.f));
    CHECK(Number<dAtan>(1.f) == doctest::Approx(kPi / 4.f));
    CHECK(Number<dCosh>(1.f) == doctest::Approx(std::cosh(1.f)));
    CHECK(Number<dSinh>(1.f) == doctest::Approx(std::sinh(1.f)));
    CHECK(Number<dTanh>(1.f) == doctest::Approx(std::tanh(1.f)));
    CHECK(Number<dAcosh>(2.f) == doctest::Approx(std::acosh(2.f)));
    CHECK(Number<dAsinh>(1.f) == doctest::Approx(std::asinh(1.f)));
    CHECK(Number<dAtanh>(0.5f) == doctest::Approx(std::atanh(0.5f)));
  }

  TEST_CASE("signal trig: out-of-domain numbers give finite edge values (#1049)") {
    using namespace YSE::PATCHER;
    CHECK(Number<dAcos>(2.f) == 0.f);
    CHECK(Number<dAsin>(-2.f) == doctest::Approx(-kPi / 2.f));
    CHECK(Number<dAcosh>(0.f) == 0.f);
    CHECK(std::isfinite(Number<dAtanh>(1.f)));
    CHECK(std::isfinite(Number<dCosh>(1000.f)));
    CHECK(std::isfinite(Number<dSinh>(-1000.f)));
  }

  TEST_CASE("signal trig: ints, defaults and a connected signal (#1049, #1036)") {
    using namespace YSE::PATCHER;
    BufferSink sink;
    dCosx op;
    Wire(op, sink);
    op.Calculate(YSE::T_DSP); // unconnected and untouched: cos(0)
    CHECK(AllNear(sink.received, 1.f));
    op.GetInlet(0)->SetInt(2, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(AllNear(sink.received, std::cos(2.f)));

    // A connected signal wins over the number.
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < in.getLength(); ++i)
      in.getPtr()[i] = static_cast<float>(i) * 0.05f;
    op.GetInlet(0)->SetFloat(100.f, YSE::T_GUI);
    op.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    REQUIRE(sink.received->getLength() == YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < sink.received->getLength(); ++i) {
      CAPTURE(i);
      CHECK(sink.received->getPtr()[i] == doctest::Approx(std::cos(in.getPtr()[i])));
    }
  }

  TEST_CASE("signal trig: ~atan2 takes y left and x right, numbers or signals (#1049)") {
    using namespace YSE::PATCHER;
    BufferSink sink;
    dAtan2 op;
    Wire(op, sink);
    op.Calculate(YSE::T_DSP); // the origin
    CHECK(AllNear(sink.received, 0.f));
    op.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    op.Calculate(YSE::T_DSP); // (0, 1): straight up
    CHECK(AllNear(sink.received, kPi / 2.f));
    op.GetInlet(1)->SetInt(-1, YSE::T_GUI);
    op.Calculate(YSE::T_DSP); // (-1, 1)
    CHECK(AllNear(sink.received, 3.f * kPi / 4.f));
    op.GetInlet(0)->SetInt(-1, YSE::T_GUI);
    op.Calculate(YSE::T_DSP); // (-1, -1)
    CHECK(AllNear(sink.received, -3.f * kPi / 4.f));

    // Creation argument: x.
    BufferSink sink2;
    dAtan2 withArg;
    Wire(withArg, sink2);
    withArg.SetParams("-1");
    withArg.Calculate(YSE::T_DSP); // (-1, 0)
    CHECK(AllNear(sink2.received, kPi));

    // Signals on both inlets win over the numbers.
    YSE::DSP::buffer y(YSE::STANDARD_BUFFERSIZE), x(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < y.getLength(); ++i) {
      const float t = (static_cast<float>(i) - 63.5f) / 64.f * 3.f;
      y.getPtr()[i] = std::sin(t);
      x.getPtr()[i] = std::cos(t);
    }
    op.GetInlet(1)->SetBuffer(&x, YSE::T_GUI);
    op.GetInlet(0)->SetBuffer(&y, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    for (unsigned int i = 0; i < sink.received->getLength(); ++i) {
      CAPTURE(i);
      const float t = (static_cast<float>(i) - 63.5f) / 64.f * 3.f;
      CHECK(sink.received->getPtr()[i] == doctest::Approx(t).epsilon(1e-5));
    }
  }

  TEST_CASE("signal trig: ~atan2's x survives a DumpJSON / ParseJSON round trip (#1049)") {
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* a2 = src.CreateObject(YSE::OBJ::D_ATAN2, "-1");
      YSE::pHandle* th = src.CreateObject(YSE::OBJ::D_TANH, "");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      src.Connect(a2, 0, dac, 0);
      src.Connect(th, 0, dac, 1);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);
    dst.Calculate(YSE::T_DSP);
    // Unconnected y reads as 0: the angle of (-1, 0) is pi.
    CHECK(dst.output[0].getPtr()[0] == doctest::Approx(kPi));
    CHECK(dst.output[1].getPtr()[0] == 0.f); // tanh(0)
  }

  TEST_CASE("signal trig: in a patch, ~phasor through ~cos, ~cosx/~sinx into ~atan2, ~tanh "
            "(#1049)") {
    patcherImplementation p(4, nullptr);
    YSE::pHandle* ramp = p.CreateObject(YSE::OBJ::D_PHASOR, HundredSampleFrequency());
    YSE::pHandle* cos = p.CreateObject(YSE::OBJ::D_COS, "");
    // ramp * 2 radians, through cosx and sinx, back to its angle with atan2.
    YSE::pHandle* scale = p.CreateObject(YSE::OBJ::D_MULTIPLY, "2");
    YSE::pHandle* cosx = p.CreateObject(YSE::OBJ::D_COSX, "");
    YSE::pHandle* sinx = p.CreateObject(YSE::OBJ::D_SINX, "");
    YSE::pHandle* a2 = p.CreateObject(YSE::OBJ::D_ATAN2, "");
    // a hot cosine soft-clipped.
    YSE::pHandle* gain = p.CreateObject(YSE::OBJ::D_MULTIPLY, "5");
    YSE::pHandle* tanh = p.CreateObject(YSE::OBJ::D_TANH, "");
    YSE::pHandle* acos = p.CreateObject(YSE::OBJ::D_ACOS, "");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");

    p.Connect(ramp, 0, cos, 0);
    p.Connect(ramp, 0, scale, 0);
    p.Connect(scale, 0, cosx, 0);
    p.Connect(scale, 0, sinx, 0);
    p.Connect(sinx, 0, a2, 0);
    p.Connect(cosx, 0, a2, 1);
    p.Connect(cos, 0, gain, 0);
    p.Connect(gain, 0, tanh, 0);
    p.Connect(gain, 0, acos, 0); // out of acos's domain most of the time
    p.Connect(cos, 0, dac, 0);
    p.Connect(a2, 0, dac, 1);
    p.Connect(tanh, 0, dac, 2);
    p.Connect(acos, 0, dac, 3);
    p.Calculate(YSE::T_DSP);

    for (unsigned int i : {0u, 10u, 25u, 40u, 50u, 75u, 99u}) {
      CAPTURE(i);
      const float phase = static_cast<float>(i) / 100.f;
      const float c = std::cos(2.f * kPi * phase);
      CHECK(p.output[0].getPtr()[i] == doctest::Approx(c).epsilon(1e-3).scale(1.f));
      CHECK(p.output[1].getPtr()[i] == doctest::Approx(2.f * phase).epsilon(1e-3).scale(1.f));
      CHECK(p.output[2].getPtr()[i] == doctest::Approx(std::tanh(5.f * c)).epsilon(1e-2));
      const float clamped = std::fmin(std::fmax(5.f * c, -1.f), 1.f);
      CHECK(p.output[3].getPtr()[i] ==
            doctest::Approx(std::acos(clamped)).epsilon(1e-2).scale(1.f));
    }
    for (unsigned int ch = 0; ch < 4; ++ch)
      for (unsigned int i = 0; i < p.output[ch].getLength(); ++i)
        CHECK(std::isfinite(p.output[ch].getPtr()[i]));
  }

  TEST_CASE("signal trig: numbers and blocks on the audio thread allocate nothing (#1049)") {
    using namespace YSE::PATCHER;
    BufferSink sinks[14]; // declared first so they outlive the objects
    dCos c;
    dCosx cx;
    dSinx sx;
    dTanx tx;
    dAcos ac;
    dAsin as;
    dAtan at;
    dCosh ch;
    dSinh sh;
    dTanh th;
    dAcosh ach;
    dAsinh ash;
    dAtanh ath;
    dAtan2 a2;
    pObject* all[] = {&c, &cx, &sx, &tx, &ac, &as, &at, &ch, &sh, &th, &ach, &ash, &ath, &a2};
    for (unsigned int i = 0; i < 14; ++i) {
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
        op->Calculate(YSE::T_DSP);
        op->GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
        op->Calculate(YSE::T_DSP);
      }
      a2.GetInlet(1)->SetFloat(0.7f, YSE::T_DSP);
      a2.GetInlet(1)->SetInt(3, YSE::T_DSP);
      a2.Calculate(YSE::T_DSP);
      a2.GetInlet(1)->SetBuffer(&in, YSE::T_DSP);
      a2.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      a2.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

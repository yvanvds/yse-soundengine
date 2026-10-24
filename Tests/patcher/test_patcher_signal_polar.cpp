// Tests for ~cartopol ~poltocar ~phasewrap (patcher/math/dPolar), issue #1053.
// The sample math is DSP::cartToPolar, DSP::polarToCart and DSP::phaseWrap's,
// tested in Tests/dsp/test_polar.cpp; these show that pins and parameters reach
// it, in isolation and in a running patch.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/dPolar.h"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::PATCHER::patcherImplementation;

namespace {
  const float kPi = 3.14159265f;

  bool AllNear(YSE::DSP::buffer* b, float expected) {
    if (b == nullptr) return false;
    if (b->getLength() != YSE::STANDARD_BUFFERSIZE) return false;
    for (unsigned int i = 0; i < b->getLength(); ++i)
      if (b->getPtr()[i] != doctest::Approx(expected).epsilon(1e-4).scale(1.f)) return false;
    return true;
  }

  // 100 samples a cycle at whatever rate the test binary runs.
  std::string HundredSampleFrequency() {
    return std::to_string(static_cast<float>(YSE::SAMPLERATE) / 100.f);
  }

  struct Family {
    const char* type;
    const char* name;
    int inputs;
    int outputs;
  };

  const Family kFamily[] = {
      {YSE::OBJ::D_CARTOPOL, "~cartopol", 2, 2},
      {YSE::OBJ::D_POLTOCAR, "~poltocar", 2, 2},
      {YSE::OBJ::D_PHASEWRAP, "~phasewrap", 1, 1},
  };
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("signal polar: registry, names and shape (#1053)") {
    YSE::patcher p;
    p.create(1);
    for (const Family& f : kFamily) {
      CAPTURE(f.name);
      CHECK(std::string(f.type) == f.name);
      YSE::pHandle* h = p.CreateObject(f.type);
      REQUIRE(h != nullptr);
      CHECK(std::string(h->Type()) == f.name);
      CHECK(h->GetInputs() == f.inputs);
      CHECK(h->GetOutputs() == f.outputs);
      for (int o = 0; o < f.outputs; ++o)
        CHECK(h->OutputDataType(o) == YSE::OUT_TYPE::BUFFER);

      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(f.type));
      REQUIRE(obj != nullptr);
      CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MATH);
      CHECK(obj->IsDSPObject());
      CHECK(std::string(obj->Type()) == f.name);
    }
  }

  TEST_CASE("signal polar: ~cartopol numbers, ints, argument and signals (#1053, #1036)") {
    using namespace YSE::PATCHER;
    BufferSink mag, phase; // declared first so they outlive the object
    dCarToPol op;
    TestHelpers::Wire(op, 0, mag);
    TestHelpers::Wire(op, 1, phase);
    op.Calculate(YSE::T_DSP); // the origin
    CHECK(AllNear(mag.received, 0.f));
    CHECK(AllNear(phase.received, 0.f));
    op.GetInlet(0)->SetFloat(3.f, YSE::T_GUI);
    op.GetInlet(1)->SetInt(4, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(AllNear(mag.received, 5.f));
    CHECK(AllNear(phase.received, std::atan2(4.f, 3.f)));
    op.GetInlet(0)->SetInt(-1, YSE::T_GUI);
    op.GetInlet(1)->SetFloat(-1.f, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(AllNear(mag.received, std::sqrt(2.f)));
    CHECK(AllNear(phase.received, -3.f * kPi / 4.f));

    // Creation argument: the imaginary part.
    BufferSink mag2, phase2;
    dCarToPol withArg;
    TestHelpers::Wire(withArg, 0, mag2);
    TestHelpers::Wire(withArg, 1, phase2);
    withArg.SetParams("2");
    withArg.Calculate(YSE::T_DSP); // (0, 2)
    CHECK(AllNear(mag2.received, 2.f));
    CHECK(AllNear(phase2.received, kPi / 2.f));

    // Signals on both inlets win over the numbers.
    YSE::DSP::buffer re(YSE::STANDARD_BUFFERSIZE), im(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < re.getLength(); ++i) {
      const float t = (static_cast<float>(i) - 63.5f) / 64.f * 3.f;
      re.getPtr()[i] = 0.7f * std::cos(t);
      im.getPtr()[i] = 0.7f * std::sin(t);
    }
    op.GetInlet(1)->SetBuffer(&im, YSE::T_GUI);
    op.GetInlet(0)->SetBuffer(&re, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    REQUIRE(mag.received != nullptr);
    REQUIRE(phase.received != nullptr);
    REQUIRE(phase.received->getLength() == YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < re.getLength(); ++i) {
      CAPTURE(i);
      const float t = (static_cast<float>(i) - 63.5f) / 64.f * 3.f;
      CHECK(mag.received->getPtr()[i] == doctest::Approx(0.7f).epsilon(1e-5));
      CHECK(phase.received->getPtr()[i] == doctest::Approx(t).epsilon(1e-5));
    }
  }

  TEST_CASE("signal polar: ~poltocar numbers, argument and signals (#1053, #1036)") {
    using namespace YSE::PATCHER;
    BufferSink re, im;
    dPolToCar op;
    TestHelpers::Wire(op, 0, re);
    TestHelpers::Wire(op, 1, im);
    op.Calculate(YSE::T_DSP); // magnitude 0
    CHECK(AllNear(re.received, 0.f));
    CHECK(AllNear(im.received, 0.f));
    op.GetInlet(0)->SetInt(2, YSE::T_GUI);
    op.Calculate(YSE::T_DSP); // phase 0
    CHECK(AllNear(re.received, 2.f));
    CHECK(AllNear(im.received, 0.f));
    op.GetInlet(1)->SetFloat(kPi / 2.f, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(AllNear(re.received, 0.f));
    CHECK(AllNear(im.received, 2.f));

    BufferSink re2, im2;
    dPolToCar withArg;
    TestHelpers::Wire(withArg, 0, re2);
    TestHelpers::Wire(withArg, 1, im2);
    withArg.SetParams("3.14159265");
    withArg.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    withArg.Calculate(YSE::T_DSP);
    CHECK(AllNear(re2.received, -0.5f));
    CHECK(AllNear(im2.received, 0.f));

    // A phase signal wins over the number.
    YSE::DSP::buffer ph(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < ph.getLength(); ++i)
      ph.getPtr()[i] = static_cast<float>(i) * 0.05f;
    op.GetInlet(1)->SetBuffer(&ph, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    REQUIRE(re.received != nullptr);
    REQUIRE(im.received != nullptr);
    for (unsigned int i = 0; i < ph.getLength(); ++i) {
      CAPTURE(i);
      CHECK(re.received->getPtr()[i] ==
            doctest::Approx(2.f * std::cos(ph.getPtr()[i])).epsilon(1e-5).scale(1.f));
      CHECK(im.received->getPtr()[i] ==
            doctest::Approx(2.f * std::sin(ph.getPtr()[i])).epsilon(1e-5).scale(1.f));
    }
  }

  TEST_CASE("signal polar: ~phasewrap numbers and signals (#1053, #1036)") {
    using namespace YSE::PATCHER;
    BufferSink out;
    dPhaseWrap op;
    TestHelpers::Wire(op, 0, out);
    op.Calculate(YSE::T_DSP);
    CHECK(AllNear(out.received, 0.f));
    op.GetInlet(0)->SetFloat(1.5f * kPi, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(AllNear(out.received, -0.5f * kPi));
    op.GetInlet(0)->SetInt(-4, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(AllNear(out.received, -4.f + 2.f * kPi));

    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < in.getLength(); ++i)
      in.getPtr()[i] = static_cast<float>(i) * 0.2f;
    op.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    REQUIRE(out.received != nullptr);
    for (unsigned int i = 0; i < in.getLength(); ++i) {
      CAPTURE(i);
      CHECK(out.received->getPtr()[i] == YSE::DSP::phaseWrap::process(in.getPtr()[i]));
      CHECK(std::fabs(out.received->getPtr()[i]) <= 3.1415927f);
    }
  }

  TEST_CASE("signal polar: right-inlet numbers survive a DumpJSON / ParseJSON round trip "
            "(#1053)") {
    std::string dump;
    {
      patcherImplementation src(4, nullptr);
      YSE::pHandle* c2p = src.CreateObject(YSE::OBJ::D_CARTOPOL, "-1");
      YSE::pHandle* p2c = src.CreateObject(YSE::OBJ::D_POLTOCAR, "1.5");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      src.Connect(c2p, 0, dac, 0);
      src.Connect(c2p, 1, dac, 1);
      src.Connect(p2c, 0, dac, 2);
      src.Connect(p2c, 1, dac, 3);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(4, nullptr);
    dst.ParseJSON(dump);
    dst.Calculate(YSE::T_DSP);
    // ~cartopol of (0, -1): magnitude 1, phase -pi/2.
    CHECK(dst.output[0].getPtr()[0] == doctest::Approx(1.f));
    CHECK(dst.output[1].getPtr()[0] == doctest::Approx(-kPi / 2.f));
    // ~poltocar of magnitude 0: the origin, whatever the stored phase.
    CHECK(dst.output[2].getPtr()[0] == 0.f);
    CHECK(dst.output[3].getPtr()[0] == 0.f);

    // The stored phase is really there: drive the magnitude and look again.
    std::string dump2;
    {
      patcherImplementation src(2, nullptr);
      // A constant magnitude 2: the distance of (0, 2).
      YSE::pHandle* mag = src.CreateObject(YSE::OBJ::D_CARTOPOL, "2");
      YSE::pHandle* p2c = src.CreateObject(YSE::OBJ::D_POLTOCAR, "1.5");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      src.Connect(mag, 0, p2c, 0);
      src.Connect(p2c, 0, dac, 0);
      src.Connect(p2c, 1, dac, 1);
      dump2 = src.DumpJSON();
    }
    patcherImplementation dst2(2, nullptr);
    dst2.ParseJSON(dump2);
    dst2.Calculate(YSE::T_DSP);
    CHECK(dst2.output[0].getPtr()[0] == doctest::Approx(2.f * std::cos(1.5f)));
    CHECK(dst2.output[1].getPtr()[0] == doctest::Approx(2.f * std::sin(1.5f)));
  }

  TEST_CASE("signal polar: in a patch, ~cosx/~sinx through ~cartopol and back through "
            "~poltocar, a ramp through ~phasewrap (#1053)") {
    patcherImplementation p(5, nullptr);
    YSE::pHandle* ramp = p.CreateObject(YSE::OBJ::D_PHASOR, HundredSampleFrequency());
    // ramp * 6 radians: the angle passes pi, so the phase outlet wraps.
    YSE::pHandle* scale = p.CreateObject(YSE::OBJ::D_MULTIPLY, "6");
    YSE::pHandle* cosx = p.CreateObject(YSE::OBJ::D_COSX, "");
    YSE::pHandle* sinx = p.CreateObject(YSE::OBJ::D_SINX, "");
    YSE::pHandle* half = p.CreateObject(YSE::OBJ::D_MULTIPLY, "0.5");
    YSE::pHandle* c2p = p.CreateObject(YSE::OBJ::D_CARTOPOL, "");
    YSE::pHandle* p2c = p.CreateObject(YSE::OBJ::D_POLTOCAR, "");
    YSE::pHandle* wrap = p.CreateObject(YSE::OBJ::D_PHASEWRAP, "");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");

    p.Connect(ramp, 0, scale, 0);
    p.Connect(scale, 0, cosx, 0);
    p.Connect(scale, 0, sinx, 0);
    p.Connect(sinx, 0, half, 0);
    p.Connect(cosx, 0, c2p, 0); // real: cos t
    p.Connect(half, 0, c2p, 1); // imaginary: 0.5 sin t
    p.Connect(c2p, 0, p2c, 0);
    p.Connect(c2p, 1, p2c, 1);
    p.Connect(scale, 0, wrap, 0);
    p.Connect(c2p, 0, dac, 0);
    p.Connect(c2p, 1, dac, 1);
    p.Connect(p2c, 0, dac, 2);
    p.Connect(p2c, 1, dac, 3);
    p.Connect(wrap, 0, dac, 4);
    p.Calculate(YSE::T_DSP);

    for (unsigned int i : {0u, 10u, 25u, 40u, 52u, 60u, 75u, 99u}) {
      CAPTURE(i);
      const float t = 6.f * static_cast<float>(i) / 100.f;
      const float re = std::cos(t);
      const float im = 0.5f * std::sin(t);
      CHECK(p.output[0].getPtr()[i] == doctest::Approx(std::hypot(re, im)).epsilon(1e-3));
      CHECK(p.output[1].getPtr()[i] ==
            doctest::Approx(std::atan2(im, re)).epsilon(1e-3).scale(1.f));
      CHECK(p.output[2].getPtr()[i] == doctest::Approx(re).epsilon(1e-3).scale(1.f));
      CHECK(p.output[3].getPtr()[i] == doctest::Approx(im).epsilon(1e-3).scale(1.f));
      const float wrapped = t > kPi ? t - 2.f * kPi : t;
      CHECK(p.output[4].getPtr()[i] == doctest::Approx(wrapped).epsilon(1e-3).scale(1.f));
    }
    for (unsigned int ch = 0; ch < 5; ++ch)
      for (unsigned int i = 0; i < p.output[ch].getLength(); ++i)
        CHECK(std::isfinite(p.output[ch].getPtr()[i]));
  }

  TEST_CASE("signal polar: numbers and blocks on the audio thread allocate nothing (#1053)") {
    using namespace YSE::PATCHER;
    BufferSink sinks[5]; // declared first so they outlive the objects
    dCarToPol c2p;
    dPolToCar p2c;
    dPhaseWrap wrap;
    TestHelpers::Wire(c2p, 0, sinks[0]);
    TestHelpers::Wire(c2p, 1, sinks[1]);
    TestHelpers::Wire(p2c, 0, sinks[2]);
    TestHelpers::Wire(p2c, 1, sinks[3]);
    TestHelpers::Wire(wrap, 0, sinks[4]);
    pObject* all[] = {&c2p, &p2c, &wrap};
    for (pObject* op : all)
      op->Calculate(YSE::T_DSP);
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
      for (pObject* op : {static_cast<pObject*>(&c2p), static_cast<pObject*>(&p2c)}) {
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

// Tests for the ~downsamp patcher object (patcher/math/dDownsample), issue
// #1109. The downsampling is DSP::downsample's and is tested in
// Tests/dsp/test_downsample.cpp; these show that the signals, numbers and
// argument reach it, alone and in a rendered patch after a JSON round trip,
// bit-exact on a ramp from ~phasor.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/dDownsample.h"
#include "dsp/operators/downsample.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::downsample;
using YSE::DSP::phasor;
using YSE::PATCHER::dDownsample;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dDownsample& obj, BufferSink& out) {
    obj.ConnectOutlet(out.GetInlet(0), 0);
    out.ConnectInlet(obj.GetOutlet(0), 0);
  }

  void Fill(buffer& b, unsigned int start) {
    for (unsigned int i = 0; i < b.getLength(); ++i) {
      const auto n = static_cast<float>(start + i);
      b.getPtr()[i] = 0.8f * std::sin(n * 0.01f) + 0.15f * std::sin(n * 2.1f);
    }
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~downsamp: registry and shape (#1109)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_DOWNSAMPLE);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~downsamp");
    CHECK(obj->GetInputs() == 2);
    CHECK(obj->GetOutputs() == 1);
    for (unsigned int i = 0; i < 2; ++i)
      CHECK(obj->IsDSPInput(i));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_DOWNSAMPLE));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::MATH);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~downsamp: signals and numbers against the DSP class (#1109, #1036)") {
    BufferSink out;
    dDownsample obj;
    Wire(obj, out);
    downsample reference;
    buffer objIn(N);
    buffer refIn(N);
    unsigned int start = 0;
    float period = downsample::DEFAULT_PERIOD;

    const auto step = [&](buffer* p = nullptr) {
      obj.ResetDSP(); // as the patcher does before every block
      Fill(objIn, start);
      Fill(refIn, start);
      start += N;
      obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
      if (p != nullptr) obj.GetInlet(1)->SetBuffer(p, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(out.received != nullptr);
      buffer ps(N);
      ps = period;
      if (p != nullptr) ps = *p;
      CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(refIn, ps), 0.f));
    };

    // Default: every sample, bit for bit.
    step();
    {
      buffer expected(N);
      Fill(expected, 0);
      CHECK(TestHelpers::buffersNearlyEqual(*out.received, expected, 0.f));
    }

    // Numbers in the right inlet, floats and ints.
    obj.GetInlet(1)->SetInt(4, YSE::T_GUI);
    period = 4.f;
    step();
    // Held four samples, counted on from the last take of the block before
    // (its last sample, at period 1): takes at samples 3, 7, 11 ...
    {
      buffer expected(N);
      Fill(expected, start - N);
      const float* y = out.received->getPtr();
      for (unsigned int i = 0; i < N; ++i) {
        CAPTURE(i);
        if (i % 4 == 3)
          CHECK(y[i] == expected.getPtr()[i]);
        else if (i > 0)
          CHECK(y[i] == y[i - 1]);
      }
    }
    obj.GetInlet(1)->SetFloat(2.5f, YSE::T_GUI);
    period = 2.5f;
    step();
    obj.GetInlet(1)->SetInt(1, YSE::T_GUI);
    period = 1.f;
    step();
    // Below 1: the DSP class raises it to 1.
    obj.GetInlet(1)->SetFloat(-3.f, YSE::T_GUI);
    period = -3.f;
    step();
    CHECK(reference.period() == 1.f);
    obj.GetInlet(1)->SetFloat(6.f, YSE::T_GUI);
    period = 6.f;
    step();

    // A signal wins over the number and is read per sample.
    buffer p(N);
    for (unsigned int i = 0; i < N; ++i)
      p.getPtr()[i] = i < N / 2 ? 3.f : 16.f;
    step(&p);
    CHECK(reference.period() == 16.f);
    // Back to unconnected: the last number again.
    step();
    CHECK(reference.period() == 6.f);

    // Unconnected input: silence.
    BufferSink idleOut;
    dDownsample idle;
    Wire(idle, idleOut);
    idle.SetParams("4");
    idle.Calculate(YSE::T_DSP);
    REQUIRE(idleOut.received != nullptr);
    CHECK(idleOut.received->getPtr()[N - 1] == 0.f);
  }

  TEST_CASE("~downsamp: the argument is the period (#1109)") {
    const auto check = [](const std::string& args, float period) {
      CAPTURE(args);
      BufferSink out;
      dDownsample obj;
      Wire(obj, out);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      downsample reference;
      buffer objIn(N);
      buffer refIn(N);
      buffer ps(N);
      ps = period;
      for (unsigned int block = 0; block < 3; ++block) {
        CAPTURE(block);
        obj.ResetDSP();
        Fill(objIn, block * N);
        Fill(refIn, block * N);
        obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
        obj.Calculate(YSE::T_DSP);
        REQUIRE(out.received != nullptr);
        CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(refIn, ps), 0.f));
      }
    };

    check("8", 8.f);
    check("2.5", 2.5f);
    check("1", 1.f);
    check("300", 300.f);
    check("", downsample::DEFAULT_PERIOD);
  }

  TEST_CASE("~downsamp: in a rendered patch, a ramp from ~phasor is held every N samples, with "
            "a number from a receive, after a JSON round trip (#1109)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* down = src.CreateObject(YSE::OBJ::D_DOWNSAMPLE, "8");
      YSE::pHandle* period = src.CreateObject(YSE::OBJ::G_RECEIVE, "period1109");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(down != nullptr);
      REQUIRE(period != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, down, 0);
      src.Connect(period, 0, down, 1);
      src.Connect(down, 0, dac, 0);
      dump = src.DumpJSON();
    }
    CHECK(dump.find("~downsamp") != std::string::npos);
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    // The same ramp, made here: ~phasor 375 at 48 kHz rises 1/128 per sample.
    phasor rampRef;
    downsample reference;
    reference.period(8.f);
    for (int block = 0; block < 8; ++block) {
      CAPTURE(block);
      if (block == 4) {
        REQUIRE(dst.PassData(2, "period1109", YSE::T_GUI));
        reference.period(2.f);
      }
      dst.Calculate(YSE::T_DSP);
      buffer& expected = reference(rampRef(375.f, N));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], expected, 0.f));

      // Each take held for the period.
      const auto hold = static_cast<unsigned int>(reference.period());
      const float* y = dst.output[0].getPtr();
      for (unsigned int i = 0; i < N; ++i) {
        CAPTURE(i);
        if (i % hold != 0) CHECK(y[i] == y[i - 1]);
      }
    }
    // The first block by hand at 8: the ramp at samples 0, 8, 16 ...
    {
      patcherImplementation again(2, nullptr);
      again.ParseJSON(dump);
      again.Calculate(YSE::T_DSP);
      const float* y = again.output[0].getPtr();
      CHECK(y[0] == 0.f);
      CHECK(y[7] == 0.f);
      CHECK(y[8] == 0.0625f); // 8 / 128
      CHECK(y[15] == 0.0625f);
      CHECK(y[64] == 0.5f);
      CHECK(y[127] == 0.9375f); // 120 / 128
    }
  }

  TEST_CASE("~downsamp: numbers and blocks on the audio thread allocate nothing (#1109)") {
    BufferSink out;
    dDownsample obj;
    Wire(obj, out);
    buffer in(N);
    buffer p(N);
    in = 0.5f;
    p = 4.f;
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(1)->SetFloat(2.5f, YSE::T_DSP);
      obj.GetInlet(1)->SetInt(8, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.GetInlet(1)->SetBuffer(&p, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(*out.received, in, 0.f));
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

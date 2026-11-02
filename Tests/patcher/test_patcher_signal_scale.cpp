// Tests for the ~scale patcher object (patcher/math/dScale), issue #1114. The
// mapping is DSP::scale's and is tested in Tests/dsp/test_scale.cpp; these
// show that the signals, numbers, messages and arguments reach it, alone and
// in rendered patches after a JSON round trip.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/dScale.h"
#include "dsp/operators/scale.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::scale;
using YSE::PATCHER::dScale;
using YSE::PATCHER::patcherImplementation;
using MODE = YSE::DSP::scale::MODE;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  struct Setting {
    float il, ih, ol, oh, e;
  };

  void Wire(dScale& obj, BufferSink& out) {
    obj.ConnectOutlet(out.GetInlet(0), 0);
    out.ConnectInlet(obj.GetOutlet(0), 0);
  }

  void Fill(buffer& b, unsigned int start) {
    for (unsigned int i = 0; i < b.getLength(); ++i) {
      const auto n = static_cast<float>(start + i);
      b.getPtr()[i] = 1.4f * std::sin(n * 0.013f) + 0.3f * std::sin(n * 2.1f);
    }
  }

  // Runs one block through obj and checks it against the DSP class at the
  // given mode and settings.
  void CheckBlock(dScale& obj, BufferSink& out, unsigned int start, MODE how, Setting c) {
    buffer objIn(N);
    buffer refIn(N);
    Fill(objIn, start);
    Fill(refIn, start);
    obj.ResetDSP(); // as the patcher does before every block
    obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    REQUIRE(out.received != nullptr);
    scale reference;
    reference.mode(how).inRange(c.il, c.ih).outRange(c.ol, c.oh).exponent(c.e);
    CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(refIn), 0.f));
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~scale: registry and shape (#1114)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_SCALE);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~scale");
    CHECK(obj->GetInputs() == 6);
    CHECK(obj->GetOutputs() == 1);
    for (unsigned int i = 0; i < 6; ++i)
      CHECK(obj->IsDSPInput(i));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_SCALE));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::MATH);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 6);
  }

  TEST_CASE("~scale: numbers into the setting inlets and the classic message (#1114, #1036)") {
    BufferSink out;
    dScale obj;
    Wire(obj, out);
    unsigned int start = 0;
    const auto step = [&](MODE how, Setting c) {
      CheckBlock(obj, out, start, how, c);
      start += N;
    };

    // Default: 0..1 onto 0..1, straight.
    step(MODE::MODERN, {0.f, 1.f, 0.f, 1.f, 1.f});
    obj.GetInlet(1)->SetFloat(-1.f, YSE::T_GUI);
    step(MODE::MODERN, {-1.f, 1.f, 0.f, 1.f, 1.f});
    obj.GetInlet(2)->SetInt(2, YSE::T_GUI);
    step(MODE::MODERN, {-1.f, 2.f, 0.f, 1.f, 1.f});
    obj.GetInlet(3)->SetFloat(200.f, YSE::T_GUI);
    obj.GetInlet(4)->SetInt(800, YSE::T_GUI);
    step(MODE::MODERN, {-1.f, 2.f, 200.f, 800.f, 1.f});
    obj.GetInlet(5)->SetFloat(2.f, YSE::T_GUI);
    step(MODE::MODERN, {-1.f, 2.f, 200.f, 800.f, 2.f});
    obj.GetInlet(5)->SetInt(3, YSE::T_GUI);
    step(MODE::MODERN, {-1.f, 2.f, 200.f, 800.f, 3.f});
    // Not finite: the DSP class keeps the last setting.
    obj.GetInlet(3)->SetFloat(std::nanf(""), YSE::T_GUI);
    step(MODE::MODERN, {-1.f, 2.f, 200.f, 800.f, 3.f});

    obj.GetInlet(3)->SetFloat(900.f, YSE::T_GUI);
    obj.GetInlet(0)->SetMessage("classic 2", YSE::T_GUI);
    step(MODE::MODERN_INVERSE, {-1.f, 2.f, 900.f, 800.f, 3.f});
    obj.GetInlet(0)->SetMessage("classic 1", YSE::T_GUI);
    obj.GetInlet(5)->SetFloat(1.06f, YSE::T_GUI);
    step(MODE::CLASSIC, {-1.f, 2.f, 900.f, 800.f, 1.06f});
    // Not a formula: ignored.
    obj.GetInlet(0)->SetMessage("classic banana", YSE::T_GUI);
    step(MODE::CLASSIC, {-1.f, 2.f, 900.f, 800.f, 1.06f});
    obj.GetInlet(0)->SetMessage("classic 3", YSE::T_GUI);
    step(MODE::CLASSIC, {-1.f, 2.f, 900.f, 800.f, 1.06f});
    obj.GetInlet(0)->SetMessage("classic 0.5", YSE::T_GUI);
    step(MODE::CLASSIC, {-1.f, 2.f, 900.f, 800.f, 1.06f});
    obj.GetInlet(0)->SetMessage("classic", YSE::T_GUI);
    step(MODE::CLASSIC, {-1.f, 2.f, 900.f, 800.f, 1.06f});
    obj.GetInlet(0)->SetMessage("modern 0", YSE::T_GUI);
    step(MODE::CLASSIC, {-1.f, 2.f, 900.f, 800.f, 1.06f});
    obj.GetInlet(0)->SetMessage("classic 0", YSE::T_GUI);
    obj.GetInlet(5)->SetFloat(2.f, YSE::T_GUI);
    step(MODE::MODERN, {-1.f, 2.f, 900.f, 800.f, 2.f});
    // Numbers and messages are not saved: the argument string stays.
    CHECK(obj.GetParams().empty());
  }

  TEST_CASE("~scale: signals in the setting inlets are read per sample (#1114, #1036)") {
    BufferSink out;
    dScale obj;
    Wire(obj, out);
    obj.SetParams("-1 1 0 10 2");
    buffer in(N);
    buffer il(N);
    buffer ih(N);
    buffer ol(N);
    buffer oh(N);
    buffer ex(N);
    Fill(in, 0);
    for (unsigned int i = 0; i < N; ++i) {
      il.getPtr()[i] = -1.f - static_cast<float>(i % 7) * 0.1f;
      ih.getPtr()[i] = 1.f + static_cast<float>(i % 3) * 0.25f;
      ol.getPtr()[i] = static_cast<float>(i % 5);
      oh.getPtr()[i] = 10.f - static_cast<float>(i % 4);
      ex.getPtr()[i] = 0.5f + static_cast<float>(i % 6) * 0.3f;
    }
    obj.ResetDSP();
    obj.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
    obj.GetInlet(1)->SetBuffer(&il, YSE::T_GUI);
    obj.GetInlet(2)->SetBuffer(&ih, YSE::T_GUI);
    obj.GetInlet(3)->SetBuffer(&ol, YSE::T_GUI);
    obj.GetInlet(4)->SetBuffer(&oh, YSE::T_GUI);
    obj.GetInlet(5)->SetBuffer(&ex, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    REQUIRE(out.received != nullptr);
    scale reference;
    CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(in, il, ih, ol, oh, ex), 0.f));

    // Back to unconnected: the arguments again.
    CheckBlock(obj, out, N, MODE::MODERN, {-1.f, 1.f, 0.f, 10.f, 2.f});
  }

  TEST_CASE("~scale: a number in the left inlet is used while no signal is connected (#1114)") {
    BufferSink out;
    dScale obj;
    Wire(obj, out);
    obj.SetParams("0 10 100 200");
    obj.Calculate(YSE::T_DSP);
    REQUIRE(out.received != nullptr);
    CHECK(out.received->getPtr()[0] == 100.f);
    obj.GetInlet(0)->SetFloat(2.5f, YSE::T_GUI);
    obj.ResetDSP();
    obj.Calculate(YSE::T_DSP);
    for (unsigned int i = 0; i < N; ++i)
      CHECK(out.received->getPtr()[i] == 125.f);
    obj.GetInlet(0)->SetInt(-5, YSE::T_GUI);
    obj.ResetDSP();
    obj.Calculate(YSE::T_DSP);
    CHECK(out.received->getPtr()[N - 1] == 50.f);
  }

  TEST_CASE("~scale: the arguments are the ranges, the exponent and classic (#1114)") {
    const auto check = [](const std::string& args, MODE how, Setting c) {
      CAPTURE(args);
      BufferSink out;
      dScale obj;
      Wire(obj, out);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      for (unsigned int block = 0; block < 3; ++block) {
        CAPTURE(block);
        CheckBlock(obj, out, block * N, how, c);
      }
    };

    check("", MODE::MODERN, {0.f, 1.f, 0.f, 1.f, 1.f});
    check("-1 1", MODE::MODERN, {-1.f, 1.f, 0.f, 1.f, 1.f});
    check("-1 1 200 800", MODE::MODERN, {-1.f, 1.f, 200.f, 800.f, 1.f});
    check("0 1 0 1 2", MODE::MODERN, {0.f, 1.f, 0.f, 1.f, 2.f});
    check("0 1 1 0 2 2", MODE::MODERN_INVERSE, {0.f, 1.f, 1.f, 0.f, 2.f});
    check("0 1 0 1 1.06 1", MODE::CLASSIC, {0.f, 1.f, 0.f, 1.f, 1.06f});
    check("0 1 0 1 2 7", MODE::MODERN, {0.f, 1.f, 0.f, 1.f, 2.f}); // unknown: modern
  }

  TEST_CASE("~scale: in a rendered patch, ~phasor -> ~scale 0 1 -1 1 -> ~dac turns a ramp into "
            "a bipolar sawtooth, after a JSON round trip (#1114)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* map = src.CreateObject(YSE::OBJ::D_SCALE, "0 1 -1 1");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(map != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, map, 0);
      src.Connect(map, 0, dac, 0);
      dump = src.DumpJSON();
    }
    CHECK(dump.find("~scale") != std::string::npos);
    CHECK(dump.find("0 1 -1 1") != std::string::npos);
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    // ~phasor 375 at 48 kHz rises 1/128 per sample: sample i of a block is
    // (i mod 128) / 128, mapped onto -1..1 exactly as -1 + (i mod 128) / 64.
    for (int block = 0; block < 3; ++block) {
      CAPTURE(block);
      dst.Calculate(YSE::T_DSP);
      const float* y = dst.output[0].getPtr();
      for (unsigned int i = 0; i < N; ++i) {
        CAPTURE(i);
        CHECK(y[i] == -1.f + static_cast<float>(i % 128) / 64.f);
      }
    }
  }

  TEST_CASE("~scale: ~phasor -> ~scale bends a ramp against the DSP classes, with the exponent "
            "from a receive, after a JSON round trip (#1114)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* map = src.CreateObject(YSE::OBJ::D_SCALE, "0 1 8 0 2 2");
      YSE::pHandle* curve = src.CreateObject(YSE::OBJ::G_RECEIVE, "curve1114");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(map != nullptr);
      REQUIRE(curve != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, map, 0);
      src.Connect(curve, 0, map, 5);
      src.Connect(map, 0, dac, 0);
      dump = src.DumpJSON();
    }
    CHECK(dump.find("0 1 8 0 2 2") != std::string::npos);
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    phasor rampRef;
    scale reference;
    reference.mode(MODE::MODERN_INVERSE).outRange(8.f, 0.f).exponent(2.f);
    for (int block = 0; block < 6; ++block) {
      CAPTURE(block);
      if (block == 3) {
        REQUIRE(dst.PassData(0.5f, "curve1114", YSE::T_GUI));
        reference.exponent(0.5f);
      }
      dst.Calculate(YSE::T_DSP);
      buffer& expected = reference(rampRef(375.f, N));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], expected, 0.f));
    }
    // By hand, the downward range in classic 2 inverts the exponent 2 to
    // 0.5: 8 - 8 * sqrt(n), so n = 1/4 gives 4 and n = 1/16 gives 6.
    {
      patcherImplementation again(2, nullptr);
      again.ParseJSON(dump);
      again.Calculate(YSE::T_DSP);
      const float* y = again.output[0].getPtr();
      CHECK(y[0] == 8.f);
      CHECK(y[8] == 6.f);
      CHECK(y[32] == 4.f);
      CHECK(y[128 - 1] == doctest::Approx(8.f - 8.f * std::sqrt(127.f / 128.f)));
    }
  }

  TEST_CASE("~scale: numbers, messages and blocks on the audio thread allocate nothing "
            "(#1114)") {
    BufferSink out;
    dScale obj;
    Wire(obj, out);
    buffer in(N);
    buffer lo(N);
    buffer hi(N);
    in = 0.5f;
    lo = -1.f;
    hi = 2.f;
    const std::string classicMessage = "classic 1";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(1)->SetFloat(-0.5f, YSE::T_DSP);
      obj.GetInlet(2)->SetInt(3, YSE::T_DSP);
      obj.GetInlet(5)->SetFloat(1.06f, YSE::T_DSP);
      obj.GetInlet(0)->SetFloat(0.5f, YSE::T_DSP);
      obj.SetMessage(classicMessage, 0.f);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.GetInlet(1)->SetBuffer(&lo, YSE::T_DSP);
      obj.GetInlet(2)->SetBuffer(&hi, YSE::T_DSP);
      obj.GetInlet(3)->SetBuffer(&lo, YSE::T_DSP);
      obj.GetInlet(4)->SetBuffer(&hi, YSE::T_DSP);
      obj.GetInlet(5)->SetBuffer(&hi, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

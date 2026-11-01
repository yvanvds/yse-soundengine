// Tests for the ~pong patcher object (patcher/math/dPong), issue #1113. The
// folding, wrapping and clipping are DSP::fold's and are tested in
// Tests/dsp/test_fold.cpp; these show that the signals, numbers, messages and
// arguments reach it, alone and in rendered patches after a JSON round trip.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/dPong.h"
#include "dsp/operators/fold.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::fold;
using YSE::DSP::phasor;
using YSE::PATCHER::dPong;
using YSE::PATCHER::patcherImplementation;
using MODE = YSE::DSP::fold::MODE;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dPong& obj, BufferSink& out) {
    obj.ConnectOutlet(out.GetInlet(0), 0);
    out.ConnectInlet(obj.GetOutlet(0), 0);
  }

  void Fill(buffer& b, unsigned int start) {
    for (unsigned int i = 0; i < b.getLength(); ++i) {
      const auto n = static_cast<float>(start + i);
      b.getPtr()[i] = 2.4f * std::sin(n * 0.013f) + 0.3f * std::sin(n * 2.1f);
    }
  }

  // Runs one block through obj and checks it against the DSP class at the
  // given mode and range.
  void CheckBlock(dPong& obj, BufferSink& out, unsigned int start, MODE how, float lo, float hi) {
    buffer objIn(N);
    buffer refIn(N);
    Fill(objIn, start);
    Fill(refIn, start);
    obj.ResetDSP(); // as the patcher does before every block
    obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    REQUIRE(out.received != nullptr);
    fold reference;
    reference.mode(how).range(lo, hi);
    CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(refIn), 0.f));
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~pong: registry and shape (#1113)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_PONG);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~pong");
    CHECK(obj->GetInputs() == 3);
    CHECK(obj->GetOutputs() == 1);
    for (unsigned int i = 0; i < 3; ++i)
      CHECK(obj->IsDSPInput(i));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_PONG));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::MATH);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 3);
  }

  TEST_CASE("~pong: numbers into the limit inlets and messages (#1113, #1036)") {
    BufferSink out;
    dPong obj;
    Wire(obj, out);
    unsigned int start = 0;
    const auto step = [&](MODE how, float lo, float hi) {
      CheckBlock(obj, out, start, how, lo, hi);
      start += N;
    };

    // Default: fold into 0..1.
    step(MODE::FOLD, 0.f, 1.f);
    obj.GetInlet(1)->SetFloat(-0.5f, YSE::T_GUI);
    step(MODE::FOLD, -0.5f, 1.f);
    obj.GetInlet(2)->SetInt(2, YSE::T_GUI);
    step(MODE::FOLD, -0.5f, 2.f);
    obj.GetInlet(1)->SetInt(-1, YSE::T_GUI);
    obj.GetInlet(2)->SetFloat(0.75f, YSE::T_GUI);
    step(MODE::FOLD, -1.f, 0.75f);
    // Reversed limits are swapped.
    obj.GetInlet(1)->SetFloat(1.5f, YSE::T_GUI);
    obj.GetInlet(2)->SetFloat(-1.5f, YSE::T_GUI);
    step(MODE::FOLD, -1.5f, 1.5f);
    // Not finite: the DSP class keeps the last limit.
    obj.GetInlet(1)->SetFloat(std::nanf(""), YSE::T_GUI);
    step(MODE::FOLD, 1.5f, -1.5f);

    obj.GetInlet(0)->SetMessage("mode wrap", YSE::T_GUI);
    step(MODE::WRAP, -1.5f, 1.5f);
    obj.GetInlet(0)->SetMessage("mode clip", YSE::T_GUI);
    step(MODE::CLIP, -1.5f, 1.5f);
    obj.GetInlet(0)->SetMessage("mode 1", YSE::T_GUI);
    step(MODE::WRAP, -1.5f, 1.5f);
    // Not a mode: ignored.
    obj.GetInlet(0)->SetMessage("mode banana", YSE::T_GUI);
    step(MODE::WRAP, -1.5f, 1.5f);
    obj.GetInlet(0)->SetMessage("mode 3", YSE::T_GUI);
    step(MODE::WRAP, -1.5f, 1.5f);
    obj.GetInlet(0)->SetMessage("mode 1.5", YSE::T_GUI);
    step(MODE::WRAP, -1.5f, 1.5f);
    obj.GetInlet(0)->SetMessage("mode fold", YSE::T_GUI);
    step(MODE::FOLD, -1.5f, 1.5f);
    obj.GetInlet(0)->SetMessage("mode 2", YSE::T_GUI);
    step(MODE::CLIP, -1.5f, 1.5f);
    obj.GetInlet(0)->SetMessage("mode 0", YSE::T_GUI);
    step(MODE::FOLD, -1.5f, 1.5f);

    obj.GetInlet(0)->SetMessage("range -0.25 0.5", YSE::T_GUI);
    step(MODE::FOLD, -0.25f, 0.5f);
    // Needs two numbers.
    obj.GetInlet(0)->SetMessage("range 3", YSE::T_GUI);
    step(MODE::FOLD, -0.25f, 0.5f);
    obj.GetInlet(0)->SetMessage("range a b", YSE::T_GUI);
    step(MODE::FOLD, -0.25f, 0.5f);
    // Messages are not saved: the argument string stays.
    CHECK(obj.GetParams().empty());
  }

  TEST_CASE("~pong: signals in the limit inlets are read per sample (#1113, #1036)") {
    BufferSink out;
    dPong obj;
    Wire(obj, out);
    obj.SetParams("1 -1 1");
    buffer in(N);
    buffer lo(N);
    buffer hi(N);
    Fill(in, 0);
    for (unsigned int i = 0; i < N; ++i) {
      lo.getPtr()[i] = -0.2f - static_cast<float>(i % 7) * 0.1f;
      hi.getPtr()[i] = 0.3f + static_cast<float>(i % 3) * 0.25f;
    }
    obj.ResetDSP();
    obj.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
    obj.GetInlet(1)->SetBuffer(&lo, YSE::T_GUI);
    obj.GetInlet(2)->SetBuffer(&hi, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    REQUIRE(out.received != nullptr);
    fold reference;
    reference.mode(MODE::WRAP);
    CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(in, lo, hi), 0.f));

    // Back to unconnected: the argument limits again.
    CheckBlock(obj, out, N, MODE::WRAP, -1.f, 1.f);
  }

  TEST_CASE("~pong: a number in the left inlet is used while no signal is connected (#1113)") {
    BufferSink out;
    dPong obj;
    Wire(obj, out);
    obj.Calculate(YSE::T_DSP);
    REQUIRE(out.received != nullptr);
    CHECK(out.received->getPtr()[0] == 0.f);
    obj.GetInlet(0)->SetFloat(1.25f, YSE::T_GUI);
    obj.ResetDSP();
    obj.Calculate(YSE::T_DSP);
    for (unsigned int i = 0; i < N; ++i)
      CHECK(out.received->getPtr()[i] == 0.75f);
    obj.GetInlet(0)->SetInt(-3, YSE::T_GUI);
    obj.ResetDSP();
    obj.Calculate(YSE::T_DSP);
    CHECK(out.received->getPtr()[N - 1] == 1.f);
  }

  TEST_CASE("~pong: the arguments are the mode, low and high (#1113)") {
    const auto check = [](const std::string& args, MODE how, float lo, float hi) {
      CAPTURE(args);
      BufferSink out;
      dPong obj;
      Wire(obj, out);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      for (unsigned int block = 0; block < 3; ++block) {
        CAPTURE(block);
        CheckBlock(obj, out, block * N, how, lo, hi);
      }
    };

    check("", MODE::FOLD, 0.f, 1.f);
    check("1", MODE::WRAP, 0.f, 1.f);
    check("2 -0.5", MODE::CLIP, -0.5f, 1.f); // high stays 1, as in MSP
    check("0 -1 1", MODE::FOLD, -1.f, 1.f);
    check("1 2 -2", MODE::WRAP, -2.f, 2.f);
    check("7 -1 1", MODE::FOLD, -1.f, 1.f); // an unknown mode folds
  }

  TEST_CASE("~pong: in a rendered patch, ~phasor -> ~* 4 -> ~pong 1 wraps a ramp into a "
            "sawtooth four times as fast, after a JSON round trip (#1113)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* gain = src.CreateObject(YSE::OBJ::D_MULTIPLY, "4");
      YSE::pHandle* pong = src.CreateObject(YSE::OBJ::D_PONG, "1 0 1");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(gain != nullptr);
      REQUIRE(pong != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, gain, 0);
      src.Connect(gain, 0, pong, 0);
      src.Connect(pong, 0, dac, 0);
      dump = src.DumpJSON();
    }
    CHECK(dump.find("~pong") != std::string::npos);
    CHECK(dump.find("1 0 1") != std::string::npos);
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    // ~phasor 375 at 48 kHz rises 1/128 per sample, so 4 times that rises
    // 1/32 and the wrap is exact: sample i of a block is (i mod 32) / 32.
    for (int block = 0; block < 3; ++block) {
      CAPTURE(block);
      dst.Calculate(YSE::T_DSP);
      const float* y = dst.output[0].getPtr();
      for (unsigned int i = 0; i < N; ++i) {
        CAPTURE(i);
        CHECK(y[i] == static_cast<float>(i % 32) / 32.f);
      }
    }
  }

  TEST_CASE("~pong: ~phasor -> ~* 4 -> ~pong folds against the DSP classes, with the low "
            "limit from a receive, after a JSON round trip (#1113)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* gain = src.CreateObject(YSE::OBJ::D_MULTIPLY, "4");
      YSE::pHandle* pong = src.CreateObject(YSE::OBJ::D_PONG, "0 0.5 1.5");
      YSE::pHandle* lowIn = src.CreateObject(YSE::OBJ::G_RECEIVE, "low1113");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(gain != nullptr);
      REQUIRE(pong != nullptr);
      REQUIRE(lowIn != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, gain, 0);
      src.Connect(gain, 0, pong, 0);
      src.Connect(lowIn, 0, pong, 1);
      src.Connect(pong, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    phasor rampRef;
    fold reference;
    reference.range(0.5f, 1.5f);
    for (int block = 0; block < 6; ++block) {
      CAPTURE(block);
      if (block == 3) {
        REQUIRE(dst.PassData(-1.f, "low1113", YSE::T_GUI));
        reference.low(-1.f);
      }
      dst.Calculate(YSE::T_DSP);
      buffer& r = rampRef(375.f, N);
      buffer scaled(N);
      for (unsigned int i = 0; i < N; ++i)
        scaled.getPtr()[i] = r.getPtr()[i] * 4.f;
      buffer& expected = reference(scaled);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], expected, 0.f));
      for (unsigned int i = 0; i < N; ++i) {
        CHECK(dst.output[0].getPtr()[i] >= (block < 3 ? 0.5f : -1.f));
        CHECK(dst.output[0].getPtr()[i] <= 1.5f);
      }
    }
    // By hand, at 0.5..1.5: the ramp 0..4 folds as 0 -> 1, 0.5 -> 0.5,
    // 1.5 -> 1.5, 2.5 -> 0.5, 3.5 -> 1.5.
    {
      patcherImplementation again(2, nullptr);
      again.ParseJSON(dump);
      again.Calculate(YSE::T_DSP);
      const float* y = again.output[0].getPtr();
      CHECK(y[0] == 1.f);
      CHECK(y[16] == 0.5f);
      CHECK(y[48] == 1.5f);
      CHECK(y[64] == 1.f);
      CHECK(y[80] == 0.5f);
      CHECK(y[112] == 1.5f);
      CHECK(y[120] == 1.25f);
    }
  }

  TEST_CASE("~pong: numbers, messages and blocks on the audio thread allocate nothing "
            "(#1113)") {
    BufferSink out;
    dPong obj;
    Wire(obj, out);
    buffer in(N);
    buffer lo(N);
    buffer hi(N);
    in = 2.5f;
    lo = -1.f;
    hi = 1.f;
    const std::string modeMessage = "mode wrap";
    const std::string rangeMessage = "range -2 2";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(1)->SetFloat(-0.5f, YSE::T_DSP);
      obj.GetInlet(2)->SetInt(3, YSE::T_DSP);
      obj.GetInlet(0)->SetFloat(0.5f, YSE::T_DSP);
      obj.SetMessage(modeMessage, 0.f);
      obj.SetMessage(rangeMessage, 0.f);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.GetInlet(1)->SetBuffer(&lo, YSE::T_DSP);
      obj.GetInlet(2)->SetBuffer(&hi, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

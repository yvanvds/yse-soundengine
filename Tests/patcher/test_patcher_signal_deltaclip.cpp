// Tests for the ~deltaclip patcher object (patcher/filters/dDeltaclip),
// issue #1107. The limiting is DSP::slewLimiter's and is tested in
// Tests/dsp/test_slew_limiter.cpp; these show that the signals, numbers,
// lists, reset and arguments reach it, alone and in a rendered patch after a
// JSON round trip, bit-exact on a square from ~phasor through ~<.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/filters/dDeltaclip.h"
#include "dsp/filters/slewLimiter.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::slewLimiter;
using YSE::PATCHER::dDeltaclip;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dDeltaclip& obj, BufferSink& out) {
    obj.ConnectOutlet(out.GetInlet(0), 0);
    out.ConnectInlet(obj.GetOutlet(0), 0);
  }

  // A test signal: a slow sine with a fast one on top, so the output both
  // rises and falls within a block, sometimes faster than the limits.
  void Fill(buffer& b, unsigned int start) {
    for (unsigned int i = 0; i < b.getLength(); ++i) {
      const auto n = static_cast<float>(start + i);
      b.getPtr()[i] = std::sin(n * 0.01f) + 0.5f * std::sin(n * 2.1f);
    }
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~deltaclip: registry and shape (#1107)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_DELTACLIP);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~deltaclip");
    CHECK(obj->GetInputs() == 3);
    CHECK(obj->GetOutputs() == 1);
    for (unsigned int i = 0; i < 3; ++i)
      CHECK(obj->IsDSPInput(i));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_DELTACLIP));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::FILTER);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~deltaclip: signals, numbers, lists and reset against the DSP class "
            "(#1107, #1036)") {
    BufferSink out;
    dDeltaclip obj;
    Wire(obj, out);
    slewLimiter reference;
    buffer objIn(N);
    buffer refIn(N);
    unsigned int start = 0;

    const auto step = [&](buffer* lo = nullptr, buffer* hi = nullptr) {
      obj.ResetDSP(); // as the patcher does before every block
      Fill(objIn, start);
      Fill(refIn, start);
      start += N;
      obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
      if (lo != nullptr) obj.GetInlet(1)->SetBuffer(lo, YSE::T_GUI);
      if (hi != nullptr) obj.GetInlet(2)->SetBuffer(hi, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(out.received != nullptr);
      buffer l(N);
      buffer h(N);
      l = reference.minimum();
      h = reference.maximum();
      if (lo != nullptr) l = *lo;
      if (hi != nullptr) h = *hi;
      CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(refIn, l, h), 0.f));
    };

    // Default: 0 and 0, the output holds at 0.
    step();
    for (unsigned int i = 0; i < N; ++i)
      CHECK(out.received->getPtr()[i] == 0.f);

    // Numbers in the two right inlets; ints too.
    obj.GetInlet(2)->SetFloat(0.05f, YSE::T_GUI);
    reference.maximum(0.05f);
    step();
    obj.GetInlet(1)->SetFloat(-0.02f, YSE::T_GUI);
    reference.minimum(-0.02f);
    step();
    obj.GetInlet(1)->SetInt(-1, YSE::T_GUI);
    obj.GetInlet(2)->SetInt(1, YSE::T_GUI);
    reference.minimum(-1.f).maximum(1.f);
    step();

    // A list of numbers in any inlet, also as text through a receive: the
    // minimum, then the maximum; a single number sets the minimum.
    obj.GetInlet(0)->SetList("-0.1 0.3", YSE::T_GUI);
    reference.minimum(-0.1f).maximum(0.3f);
    step();
    obj.GetInlet(0)->SetMessage("-0.2", YSE::T_GUI);
    reference.minimum(-0.2f);
    step();
    // A bare number in a right inlet is that inlet's number.
    obj.GetInlet(2)->SetMessage("0.15", YSE::T_GUI);
    reference.maximum(0.15f);
    step();

    // Signals win over the numbers and are read per sample.
    buffer lo(N);
    buffer hi(N);
    for (unsigned int i = 0; i < N; ++i) {
      lo.getPtr()[i] = -0.001f * static_cast<float>(i);
      hi.getPtr()[i] = 0.5f - 0.001f * static_cast<float>(i);
    }
    step(&lo, &hi);
    CHECK(reference.minimum() == lo.getPtr()[N - 1]);
    // Only the maximum connected: the minimum number again.
    reference.minimum(-0.2f);
    step(nullptr, &hi);

    // Back to unconnected: the last numbers again.
    reference.minimum(-0.2f).maximum(0.15f);
    step();

    // Unknown or malformed messages and non-finite numbers: ignored.
    obj.SetMessage("limits 3", 0.f);
    obj.SetMessage("abc 5", 0.f);
    obj.SetMessage("nan 5", 0.f);
    obj.SetMessage("", 0.f);
    obj.GetInlet(1)->SetFloat(std::nanf(""), YSE::T_GUI);
    obj.GetInlet(2)->SetFloat(std::nanf(""), YSE::T_GUI);
    step();
    // A list whose second token is not a number sets only the minimum.
    obj.GetInlet(0)->SetList("-0.3 nan", YSE::T_GUI);
    reference.minimum(-0.3f);
    step();

    // reset in any inlet sets both limits back to 0: the output holds.
    obj.GetInlet(2)->SetMessage("reset", YSE::T_GUI);
    reference.minimum(0.f).maximum(0.f);
    step();
    const float held = out.received->getPtr()[0];
    for (unsigned int i = 0; i < N; ++i)
      CHECK(out.received->getPtr()[i] == held);
    obj.GetInlet(0)->SetMessage("-1 1", YSE::T_GUI);
    reference.minimum(-1.f).maximum(1.f);
    step();
    obj.GetInlet(0)->SetList("reset", YSE::T_GUI);
    reference.minimum(0.f).maximum(0.f);
    step();

    // Unconnected input: silence.
    BufferSink idleOut;
    dDeltaclip idle;
    Wire(idle, idleOut);
    idle.SetParams("-1 1");
    idle.Calculate(YSE::T_DSP);
    REQUIRE(idleOut.received != nullptr);
    CHECK(idleOut.received->getPtr()[N - 1] == 0.f);
  }

  TEST_CASE("~deltaclip: arguments are the minimum and the maximum (#1107)") {
    const auto check = [](const std::string& args, float lo, float hi) {
      CAPTURE(args);
      BufferSink out;
      dDeltaclip obj;
      Wire(obj, out);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      slewLimiter reference;
      reference.minimum(lo).maximum(hi);
      buffer objIn(N);
      buffer refIn(N);
      for (unsigned int block = 0; block < 3; ++block) {
        CAPTURE(block);
        obj.ResetDSP();
        Fill(objIn, block * N);
        Fill(refIn, block * N);
        obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
        obj.Calculate(YSE::T_DSP);
        REQUIRE(out.received != nullptr);
        CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(refIn), 0.f));
      }
    };

    check("-0.05", -0.05f, slewLimiter::DEFAULT_LIMIT);
    check("-0.05 0.1", -0.05f, 0.1f);
    // A third number is ignored, a word skipped.
    check("-0.05 0.1 3", -0.05f, 0.1f);
    check("x -0.2 0.4", -0.2f, 0.4f);
    check("", slewLimiter::DEFAULT_LIMIT, slewLimiter::DEFAULT_LIMIT);
  }

  TEST_CASE("~deltaclip: in a rendered patch, a square from ~phasor becomes a trapezoid, with "
            "numbers and commands from receives, after a JSON round trip (#1107)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* square = src.CreateObject(YSE::OBJ::D_LESS, "0.5");
      YSE::pHandle* limit = src.CreateObject(YSE::OBJ::D_DELTACLIP, "-0.125 0.0625");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1107");
      YSE::pHandle* hi = src.CreateObject(YSE::OBJ::G_RECEIVE, "hi1107");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(square != nullptr);
      REQUIRE(limit != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(hi != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, square, 0);
      src.Connect(square, 0, limit, 0);
      src.Connect(cmd, 0, limit, 0);
      src.Connect(hi, 0, limit, 2);
      src.Connect(limit, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    // The same square, made here: 1 for the first half of each 128-sample
    // cycle of ~phasor 375 at 48 kHz, else 0.
    phasor rampRef;
    slewLimiter reference;
    reference.minimum(-0.125f).maximum(0.0625f);
    buffer squareRef(N);
    for (int block = 0; block < 10; ++block) {
      CAPTURE(block);
      if (block == 3) {
        REQUIRE(dst.PassData(0.25f, "hi1107", YSE::T_GUI));
        reference.maximum(0.25f);
      }
      if (block == 5) {
        REQUIRE(dst.PassData("reset", "cmd1107", YSE::T_GUI));
        reference.minimum(0.f).maximum(0.f);
      }
      if (block == 7) {
        REQUIRE(dst.PassData("-0.5 0.5", "cmd1107", YSE::T_GUI));
        reference.minimum(-0.5f).maximum(0.5f);
      }
      dst.Calculate(YSE::T_DSP);
      buffer& ramp = rampRef(375.f, N);
      for (unsigned int i = 0; i < N; ++i)
        squareRef.getPtr()[i] = ramp.getPtr()[i] < 0.5f ? 1.f : 0.f;
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(squareRef), 0.f));

      const float* y = dst.output[0].getPtr();
      for (unsigned int i = 1; i < N; ++i) {
        CAPTURE(i);
        const float change = y[i] - y[i - 1];
        CHECK(change >= reference.minimum());
        CHECK(change <= reference.maximum());
      }
    }
    // At -0.125 and 0.0625 the first rise takes 16 samples, landing on 1.
    {
      patcherImplementation again(2, nullptr);
      again.ParseJSON(dump);
      again.Calculate(YSE::T_DSP);
      const float* y = again.output[0].getPtr();
      CHECK(y[0] == 0.0625f);
      CHECK(y[14] == 0.9375f);
      CHECK(y[15] == 1.f);
    }
  }

  TEST_CASE("~deltaclip: numbers, messages and blocks on the audio thread allocate nothing "
            "(#1107)") {
    BufferSink out;
    dDeltaclip obj;
    Wire(obj, out);
    buffer in(N);
    buffer lo(N);
    buffer hi(N);
    in = 0.5f;
    lo = -0.1f;
    hi = 0.1f;
    const std::string list = "-0.2 0.2";
    const std::string number = "-0.3";
    const std::string reset = "reset";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(1)->SetFloat(-0.5f, YSE::T_DSP);
      obj.GetInlet(1)->SetInt(-1, YSE::T_DSP);
      obj.GetInlet(2)->SetFloat(0.5f, YSE::T_DSP);
      obj.GetInlet(2)->SetInt(1, YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(number, YSE::T_DSP);
      obj.GetInlet(0)->SetList(list, YSE::T_DSP);
      obj.GetInlet(1)->SetList(reset, YSE::T_DSP);
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

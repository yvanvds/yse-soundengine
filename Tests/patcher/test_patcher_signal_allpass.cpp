// Tests for the ~allpass patcher object (patcher/filters/dAllpass), issue
// #1093. The filtering is DSP::allpass's and is tested in
// Tests/dsp/test_allpass.cpp; these show that the signals, numbers, lists,
// clear and arguments (the maximum delay included) reach it, alone and in a
// rendered patch after a JSON round trip.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/filters/dAllpass.h"
#include "dsp/filters/allpass.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::allpass;
using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::PATCHER::dAllpass;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dAllpass& obj, BufferSink& out) {
    obj.ConnectOutlet(out.GetInlet(0), 0);
    out.ConnectInlet(obj.GetOutlet(0), 0);
  }

  // A test signal: two sines, one low and one high.
  void Fill(buffer& b, unsigned int start) {
    for (unsigned int i = 0; i < b.getLength(); ++i) {
      const auto n = static_cast<float>(start + i);
      b.getPtr()[i] = std::sin(n * 0.01f) + 0.5f * std::sin(n * 2.1f);
    }
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~allpass: registry and shape (#1093)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_ALLPASS);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~allpass");
    CHECK(obj->GetInputs() == 3);
    CHECK(obj->GetOutputs() == 1);
    for (unsigned int i = 0; i < 3; ++i)
      CHECK(obj->IsDSPInput(i));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_ALLPASS));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::FILTER);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~allpass: signals, numbers, lists and clear against the DSP class (#1093, "
            "#1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink out;
    dAllpass obj;
    Wire(obj, out);
    allpass reference;
    buffer objIn(N);
    buffer refIn(N);
    unsigned int start = 0;

    int stepCount = 0;
    const auto step = [&](buffer* delay = nullptr, buffer* gain = nullptr) {
      CAPTURE(stepCount);
      stepCount++;
      obj.ResetDSP(); // as the patcher does before every block
      Fill(objIn, start);
      Fill(refIn, start);
      start += N;
      obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
      if (delay != nullptr) obj.GetInlet(1)->SetBuffer(delay, YSE::T_GUI);
      if (gain != nullptr) obj.GetInlet(2)->SetBuffer(gain, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(out.received != nullptr);
      buffer d(N);
      buffer g(N);
      d = reference.delay();
      g = reference.gain();
      if (delay != nullptr) d = *delay;
      if (gain != nullptr) g = *gain;
      CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(refIn, d, g), 0.f));
    };

    // Default: delay 0 (one sample), gain 0: the input one sample late.
    step();
    step();
    CHECK(out.received->getPtr()[N - 1] != 0.f);

    // Numbers in the two right inlets; ints too.
    obj.GetInlet(1)->SetFloat(2.5f, YSE::T_GUI);
    obj.GetInlet(2)->SetFloat(0.7f, YSE::T_GUI);
    reference.delay(2.5f).gain(0.7f);
    step();
    step();
    obj.GetInlet(1)->SetInt(4, YSE::T_GUI);
    obj.GetInlet(2)->SetInt(-1, YSE::T_GUI);
    reference.delay(4.f).gain(-1.f);
    step();
    // At -1 the output is the input, exactly.
    CHECK(TestHelpers::buffersNearlyEqual(*out.received, objIn, 0.f));
    obj.GetInlet(2)->SetFloat(0.5f, YSE::T_GUI);
    reference.gain(0.5f);
    step();

    // A list in any inlet: delay, then gain; a shorter one sets the delay
    // only. Also as a list (text through a receive).
    obj.GetInlet(0)->SetList("1.25 0.8", YSE::T_GUI);
    reference.delay(1.25f).gain(0.8f);
    step();
    obj.GetInlet(0)->SetMessage("3.5", YSE::T_GUI);
    reference.delay(3.5f);
    step();
    // A single number typed into a right inlet is that inlet's number.
    obj.GetInlet(2)->SetMessage("0.25", YSE::T_GUI);
    reference.gain(0.25f);
    step();
    obj.GetInlet(1)->SetList("0.33 -0.6", YSE::T_GUI);
    reference.delay(0.33f).gain(-0.6f);
    step();

    // clear in any inlet silences the lines before the next block.
    obj.GetInlet(2)->SetMessage("clear", YSE::T_GUI);
    reference.clear();
    step();
    obj.GetInlet(0)->SetList("clear", YSE::T_GUI);
    reference.clear();
    step();

    // Signals win over the numbers and are read per sample.
    buffer delay(N);
    buffer gain(N);
    for (int i = 0; i < static_cast<int>(N); ++i) {
      delay.getPtr()[i] = 0.5f + 0.05f * static_cast<float>(i);
      gain.getPtr()[i] = -0.9f + 0.0125f * static_cast<float>(i);
    }
    step(&delay, &gain);
    CHECK(reference.delay() == delay.getPtr()[N - 1]);
    // Only the gain connected: the delay number again.
    reference.delay(0.33f);
    step(nullptr, &gain);

    // Back to unconnected: the last numbers again.
    reference.delay(0.33f).gain(-0.6f);
    step();

    // Unknown or malformed messages and non-finite numbers: ignored.
    obj.SetMessage("delay", 0.f);
    obj.SetMessage("abc 3", 0.f);
    obj.SetMessage("nan 0.5", 0.f);
    obj.SetMessage("", 0.f);
    obj.GetInlet(1)->SetFloat(std::nanf(""), YSE::T_GUI);
    obj.GetInlet(2)->SetFloat(std::nanf(""), YSE::T_GUI);
    step();
    // A list whose second token is not a number sets the delay only.
    obj.SetMessage("2 inf", 0.f);
    reference.delay(2.f);
    step();

    // Unconnected input: silence.
    BufferSink idleOut;
    dAllpass idle;
    Wire(idle, idleOut);
    idle.GetInlet(1)->SetFloat(1.f, YSE::T_GUI);
    idle.GetInlet(2)->SetFloat(0.5f, YSE::T_GUI);
    idle.Calculate(YSE::T_DSP);
    REQUIRE(idleOut.received != nullptr);
    CHECK(idleOut.received->getPtr()[N - 1] == 0.f);
  }

  TEST_CASE("~allpass: arguments are the maximum delay, the delay and the gain (#1093)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto check = [](dAllpass& obj, BufferSink& out, const std::string& args, float maximum,
                          float delay, float gain) {
      CAPTURE(args);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      allpass reference(maximum);
      reference.delay(delay).gain(gain);
      buffer objIn(N);
      buffer refIn(N);
      for (unsigned int block = 0; block < 4; ++block) {
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

    {
      BufferSink out;
      dAllpass obj;
      Wire(obj, out);
      check(obj, out, "50 12.5 0.7", 50.f, 12.5f, 0.7f);
    }
    {
      // The default maximum is 10 ms: a longer delay is clamped to it,
      // while a larger maximum lets it through.
      BufferSink out;
      dAllpass obj;
      Wire(obj, out);
      check(obj, out, "10 30 0", 10.f, 30.f, 0.f);
      BufferSink longerOut;
      dAllpass longer;
      Wire(longer, longerOut);
      check(longer, longerOut, "40 30 0", 40.f, 30.f, 0.f);
      CHECK_FALSE(TestHelpers::buffersNearlyEqual(*out.received, *longerOut.received, 0.f));
    }
    {
      // One object, its arguments set again: the lines follow each maximum
      // and go back to the default.
      BufferSink out;
      dAllpass obj;
      Wire(obj, out);
      check(obj, out, "40 30 0.5", 40.f, 30.f, 0.5f);
      check(obj, out, "10", allpass::DEFAULT_MAXIMUM_DELAY, 0.f, 0.f);
      check(obj, out, "5", 5.f, 0.f, 0.f);
      // A fourth number is ignored, a word skipped.
      check(obj, out, "20 x 4 0.25 9", 20.f, 4.f, 0.25f);
    }
  }

  TEST_CASE("~allpass: in a rendered patch, diffusing a ~phasor with numbers and commands from "
            "receives, after a JSON round trip (#1093)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* filter = src.CreateObject(YSE::OBJ::D_ALLPASS, "30 1.5 0.6");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1093");
      YSE::pHandle* gain = src.CreateObject(YSE::OBJ::G_RECEIVE, "gain1093");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(filter != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(gain != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, filter, 0);
      src.Connect(cmd, 0, filter, 0);
      src.Connect(gain, 0, filter, 2);
      src.Connect(filter, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    phasor rampRef;
    allpass reference(30.f);
    reference.delay(1.5f).gain(0.6f);
    for (int block = 0; block < 14; ++block) {
      CAPTURE(block);
      if (block == 2) {
        REQUIRE(dst.PassData(-0.4f, "gain1093", YSE::T_GUI));
        reference.gain(-0.4f);
      }
      if (block == 4) {
        // Past the default 10 ms: only the saved maximum of 30 allows it.
        REQUIRE(dst.PassData("25 0.9", "cmd1093", YSE::T_GUI));
        reference.delay(25.f).gain(0.9f);
      }
      if (block == 8) {
        REQUIRE(dst.PassData("clear", "cmd1093", YSE::T_GUI));
        reference.clear();
      }
      if (block == 10) {
        REQUIRE(dst.PassData(1.f, "gain1093", YSE::T_GUI));
        reference.gain(1.f);
      }
      dst.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(rampRef(375.f, N)), 0.f));
      // Closed form: after a clear the delay (1200 samples) holds silence, so
      // the first block is -g x exactly.
      if (block == 8) {
        buffer ramp(N);
        phasor rampAgain;
        for (int skip = 0; skip < 8; ++skip)
          rampAgain(375.f, N);
        ramp = rampAgain(375.f, N);
        for (unsigned int i = 0; i < N; ++i) {
          CAPTURE(i);
          CHECK(dst.output[0].getPtr()[i] == -0.9f * ramp.getPtr()[i]);
        }
      }
      // At gain 1 the output is the inverted input, exactly.
      if (block >= 10) {
        buffer ramp(N);
        phasor rampAgain;
        for (int skip = 0; skip < block; ++skip)
          rampAgain(375.f, N);
        ramp = rampAgain(375.f, N);
        for (unsigned int i = 0; i < N; ++i) {
          CAPTURE(i);
          CHECK(dst.output[0].getPtr()[i] == -ramp.getPtr()[i]);
        }
      }
    }
  }

  TEST_CASE("~allpass: numbers, messages and blocks on the audio thread allocate nothing "
            "(#1093)") {
    BufferSink out;
    dAllpass obj;
    Wire(obj, out);
    obj.SetParams("100 20 0.5");
    buffer in(N);
    buffer delay(N);
    buffer gain(N);
    in = 0.5f;
    delay = 7.f;
    gain = 0.3f;
    const std::string list = "15 0.4";
    const std::string single = "3";
    const std::string clear = "clear";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(1)->SetFloat(5.5f, YSE::T_DSP);
      obj.GetInlet(1)->SetInt(6, YSE::T_DSP);
      obj.GetInlet(2)->SetFloat(0.25f, YSE::T_DSP);
      obj.GetInlet(2)->SetInt(1, YSE::T_DSP);
      obj.GetInlet(0)->SetList(list, YSE::T_DSP);
      obj.GetInlet(2)->SetMessage(single, YSE::T_DSP);
      obj.GetInlet(1)->SetList(clear, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.GetInlet(1)->SetBuffer(&delay, YSE::T_DSP);
      obj.GetInlet(2)->SetBuffer(&gain, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

// Tests for the ~reson patcher object (patcher/filters/dReson), issue #1088.
// The filtering is DSP::reson's and is tested in Tests/dsp/test_reson.cpp;
// these show that the signals, numbers, keyword messages, lists, clear and
// arguments reach it, alone and in a rendered patch after a JSON round trip.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/filters/dReson.h"
#include "dsp/filters/reson.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::reson;
using YSE::PATCHER::dReson;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dReson& obj, BufferSink& out) {
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

  TEST_CASE("~reson: registry and shape (#1088)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_RESON);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~reson");
    CHECK(obj->GetInputs() == 4);
    CHECK(obj->GetOutputs() == 1);
    for (unsigned int i = 0; i < 4; ++i)
      CHECK(obj->IsDSPInput(i));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_RESON));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::FILTER);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~reson: signals, numbers, keywords, lists and clear against the DSP class "
            "(#1088, #1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink out;
    dReson obj;
    Wire(obj, out);
    reson reference;
    buffer objIn(N);
    buffer refIn(N);
    unsigned int start = 0;

    const auto step = [&](buffer* gain = nullptr, buffer* frequency = nullptr,
                          buffer* q = nullptr) {
      obj.ResetDSP(); // as the patcher does before every block
      Fill(objIn, start);
      Fill(refIn, start);
      start += N;
      obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
      if (gain != nullptr) obj.GetInlet(1)->SetBuffer(gain, YSE::T_GUI);
      if (frequency != nullptr) obj.GetInlet(2)->SetBuffer(frequency, YSE::T_GUI);
      if (q != nullptr) obj.GetInlet(3)->SetBuffer(q, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(out.received != nullptr);
      buffer g(N);
      buffer f(N);
      buffer s(N);
      g = reference.gain();
      f = reference.frequency();
      s = reference.q();
      if (gain != nullptr) g = *gain;
      if (frequency != nullptr) f = *frequency;
      if (q != nullptr) s = *q;
      CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(refIn, g, f, s), 0.f));
    };

    // Default: gain 0, frequency 0, nothing through.
    step();
    CHECK(out.received->getPtr()[N - 1] == 0.f);

    // Numbers in the three right inlets; ints too.
    obj.GetInlet(1)->SetFloat(0.8f, YSE::T_GUI);
    obj.GetInlet(2)->SetFloat(1000.f, YSE::T_GUI);
    reference.gain(0.8f).frequency(1000.f);
    step();
    CHECK(out.received->getPtr()[N - 1] != 0.f);
    obj.GetInlet(3)->SetFloat(12.f, YSE::T_GUI);
    reference.q(12.f);
    step();
    obj.GetInlet(1)->SetInt(1, YSE::T_GUI);
    obj.GetInlet(2)->SetInt(3000, YSE::T_GUI);
    obj.GetInlet(3)->SetInt(4, YSE::T_GUI);
    reference.gain(1.f).frequency(3000.f).q(4.f);
    step();

    // Keyword messages in any inlet, and as a list (text through a receive).
    obj.GetInlet(0)->SetMessage("cf 500", YSE::T_GUI);
    reference.frequency(500.f);
    step();
    obj.GetInlet(3)->SetMessage("q 40", YSE::T_GUI);
    obj.GetInlet(2)->SetMessage("gain 0.3", YSE::T_GUI);
    reference.q(40.f).gain(0.3f);
    step();
    obj.GetInlet(1)->SetList("freq 2500", YSE::T_GUI);
    reference.frequency(2500.f);
    step();

    // A list of numbers: gain, center frequency, Q; a shorter one sets the
    // first ones only.
    obj.GetInlet(0)->SetList("0.6 800 20", YSE::T_GUI);
    reference.gain(0.6f).frequency(800.f).q(20.f);
    step();
    obj.GetInlet(0)->SetList("0.9 1200", YSE::T_GUI);
    reference.gain(0.9f).frequency(1200.f);
    step();
    obj.GetInlet(0)->SetMessage("0.7", YSE::T_GUI);
    reference.gain(0.7f);
    step();

    // clear in any inlet resets the state before the next block.
    obj.GetInlet(3)->SetMessage("clear", YSE::T_GUI);
    reference.clear();
    step();
    obj.GetInlet(0)->SetList("clear", YSE::T_GUI);
    reference.clear();
    step();

    // Signals win over the numbers and are read per sample.
    buffer gain(N);
    buffer frequency(N);
    buffer q(N);
    for (unsigned int i = 0; i < N; ++i) {
      gain.getPtr()[i] = 0.2f + 0.005f * static_cast<float>(i);
      frequency.getPtr()[i] = 400.f + 20.f * static_cast<float>(i);
      q.getPtr()[i] = 2.f + 0.1f * static_cast<float>(i);
    }
    step(&gain, &frequency, &q);
    CHECK(reference.frequency() == frequency.getPtr()[N - 1]);
    // Only the frequency connected: the gain and Q numbers again.
    reference.gain(0.7f).q(20.f);
    step(nullptr, &frequency, nullptr);

    // Back to unconnected: the last numbers again.
    reference.gain(0.7f).frequency(1200.f).q(20.f);
    step();

    // Unknown or malformed messages and non-finite numbers: ignored.
    obj.SetMessage("cf", 0.f);
    obj.SetMessage("cf abc", 0.f);
    obj.SetMessage("q nan", 0.f);
    obj.SetMessage("Q 300", 0.f);
    obj.SetMessage("bandwidth 300", 0.f);
    obj.SetMessage("nan 5 5", 0.f);
    obj.SetMessage("", 0.f);
    obj.GetInlet(1)->SetFloat(std::nanf(""), YSE::T_GUI);
    obj.GetInlet(2)->SetFloat(std::nanf(""), YSE::T_GUI);
    obj.GetInlet(3)->SetFloat(std::nanf(""), YSE::T_GUI);
    step();
    step();

    // Unconnected input: silence.
    BufferSink idleOut;
    dReson idle;
    Wire(idle, idleOut);
    idle.GetInlet(1)->SetFloat(1.f, YSE::T_GUI);
    idle.GetInlet(2)->SetFloat(1000.f, YSE::T_GUI);
    idle.GetInlet(3)->SetFloat(10.f, YSE::T_GUI);
    idle.Calculate(YSE::T_DSP);
    REQUIRE(idleOut.received != nullptr);
    CHECK(idleOut.received->getPtr()[N - 1] == 0.f);
  }

  TEST_CASE("~reson: arguments are the gain, the center frequency and the Q (#1088)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto check = [](const std::string& args, float gain, float frequency, float q) {
      CAPTURE(args);
      BufferSink out;
      dReson obj;
      Wire(obj, out);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      reson reference;
      reference.gain(gain).frequency(frequency).q(q);
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

    check("1 1500", 1.f, 1500.f, reson::MIN_Q);
    check("0.5 800 20", 0.5f, 800.f, 20.f);
    // A fourth number is ignored; keywords set any value.
    check("0.5 800 20 3", 0.5f, 800.f, 20.f);
    check("q 30 cf 2000 gain 0.4", 0.4f, 2000.f, 30.f);
    check("0.9 freq 600 q 5", 0.9f, 600.f, 5.f);
    // A keyword without a number is skipped.
    check("gain", 0.f, 0.f, reson::MIN_Q);
    check("", 0.f, 0.f, reson::MIN_Q);
  }

  TEST_CASE("~reson: in a rendered patch, filtering a ~phasor with numbers and commands from "
            "receives, after a JSON round trip (#1088)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* filter = src.CreateObject(YSE::OBJ::D_RESON, "0.8 1000 10");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1088");
      YSE::pHandle* q = src.CreateObject(YSE::OBJ::G_RECEIVE, "q1088");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(filter != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(q != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, filter, 0);
      src.Connect(cmd, 0, filter, 0);
      src.Connect(q, 0, filter, 3);
      src.Connect(filter, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    phasor rampRef;
    reson reference;
    reference.gain(0.8f).frequency(1000.f).q(10.f);
    for (int block = 0; block < 12; ++block) {
      CAPTURE(block);
      if (block == 2) {
        REQUIRE(dst.PassData(40.f, "q1088", YSE::T_GUI));
        reference.q(40.f);
      }
      if (block == 4) {
        REQUIRE(dst.PassData("cf 3000", "cmd1088", YSE::T_GUI));
        reference.frequency(3000.f);
      }
      if (block == 6) {
        REQUIRE(dst.PassData("clear", "cmd1088", YSE::T_GUI));
        reference.clear();
      }
      if (block == 8) {
        REQUIRE(dst.PassData("0.25 500 2", "cmd1088", YSE::T_GUI));
        reference.gain(0.25f).frequency(500.f).q(2.f);
      }
      if (block == 10) {
        REQUIRE(dst.PassData("cf 0", "cmd1088", YSE::T_GUI));
        reference.frequency(0.f);
      }
      dst.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(rampRef(375.f, N)), 0.f));
      // Closed form: from block 6 on the state starts at 0 and the ramp's
      // first sample is 0 (6 * 128 mod 128), so the output starts at 0.
      if (block == 6) CHECK(dst.output[0].getPtr()[0] == 0.f);
      // A center frequency of 0 passes nothing.
      if (block >= 10) {
        for (unsigned int i = 0; i < N; ++i) {
          CAPTURE(i);
          CHECK(dst.output[0].getPtr()[i] == 0.f);
        }
      }
    }
  }

  TEST_CASE("~reson: numbers, messages and blocks on the audio thread allocate nothing "
            "(#1088)") {
    BufferSink out;
    dReson obj;
    Wire(obj, out);
    buffer in(N);
    buffer gain(N);
    buffer frequency(N);
    buffer q(N);
    in = 0.5f;
    gain = 0.5f;
    frequency = 700.f;
    q = 7.f;
    const std::string setFrequency = "cf 900";
    const std::string setQ = "q 3";
    const std::string list = "0.5 600 9";
    const std::string clear = "clear";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(1)->SetFloat(0.5f, YSE::T_DSP);
      obj.GetInlet(1)->SetInt(1, YSE::T_DSP);
      obj.GetInlet(2)->SetFloat(500.f, YSE::T_DSP);
      obj.GetInlet(2)->SetInt(600, YSE::T_DSP);
      obj.GetInlet(3)->SetFloat(5.f, YSE::T_DSP);
      obj.GetInlet(3)->SetInt(8, YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(setFrequency, YSE::T_DSP);
      obj.GetInlet(3)->SetList(setQ, YSE::T_DSP);
      obj.GetInlet(0)->SetList(list, YSE::T_DSP);
      obj.GetInlet(1)->SetList(clear, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.GetInlet(1)->SetBuffer(&gain, YSE::T_DSP);
      obj.GetInlet(2)->SetBuffer(&frequency, YSE::T_DSP);
      obj.GetInlet(3)->SetBuffer(&q, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

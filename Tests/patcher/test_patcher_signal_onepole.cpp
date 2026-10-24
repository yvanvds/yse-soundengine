// Tests for the ~onepole patcher object (patcher/filters/dOnePole), issue
// #1086. The filtering is DSP::onePole's and is tested in
// Tests/dsp/test_one_pole.cpp; these show that the signals, numbers, mode
// words, clear and arguments reach it, alone and in a rendered patch after a
// JSON round trip.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/filters/dOnePole.h"
#include "dsp/filters/onePole.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::onePole;
using YSE::DSP::phasor;
using YSE::PATCHER::dOnePole;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dOnePole& obj, BufferSink& out) {
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

  TEST_CASE("~onepole: registry and shape (#1086)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_ONEPOLE);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~onepole");
    CHECK(obj->GetInputs() == 2);
    CHECK(obj->GetOutputs() == 1);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->IsDSPInput(1));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_ONEPOLE));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::FILTER);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~onepole: signals, numbers, mode words and clear against the DSP class (#1086, "
            "#1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink out;
    dOnePole obj;
    Wire(obj, out);
    onePole reference;
    buffer objIn(N);
    buffer refIn(N);
    unsigned int start = 0;

    const auto step = [&](buffer* freq = nullptr) {
      obj.ResetDSP(); // as the patcher does before every block
      Fill(objIn, start);
      Fill(refIn, start);
      start += N;
      obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
      if (freq != nullptr) obj.GetInlet(1)->SetBuffer(freq, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(out.received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(refIn), 0.f));
    };

    // Default: 0 Hz, nothing through.
    step();
    CHECK(out.received->getPtr()[N - 1] == 0.f);

    // A number in the right inlet is the frequency in Hz; an int too.
    obj.GetInlet(1)->SetFloat(1000.f, YSE::T_GUI);
    reference.frequency(1000.f);
    step();
    CHECK(out.received->getPtr()[N - 1] != 0.f);
    obj.GetInlet(1)->SetInt(4000, YSE::T_GUI);
    reference.frequency(4000.f);
    step();

    // The mode words, in either inlet, in either case for hz.
    obj.GetInlet(0)->SetMessage("linear", YSE::T_GUI);
    obj.GetInlet(1)->SetFloat(0.25f, YSE::T_GUI);
    reference.mode(onePole::LINEAR).frequency(0.25f);
    step();
    obj.GetInlet(1)->SetMessage("radians", YSE::T_GUI);
    reference.mode(onePole::RADIANS);
    step();
    obj.GetInlet(0)->SetMessage("Hz", YSE::T_GUI);
    reference.mode(onePole::HZ);
    step();
    obj.GetInlet(1)->SetMessage("radians", YSE::T_GUI);
    obj.GetInlet(1)->SetMessage("hz", YSE::T_GUI);
    step();

    // A command that arrives as a list (text through a receive).
    obj.GetInlet(1)->SetList("linear", YSE::T_GUI);
    reference.mode(onePole::LINEAR);
    step();
    obj.GetInlet(0)->SetList("hz", YSE::T_GUI);
    reference.mode(onePole::HZ);
    step();

    // clear in either inlet resets the state before the next block.
    obj.GetInlet(1)->SetMessage("clear", YSE::T_GUI);
    reference.clear();
    step();
    obj.GetInlet(0)->SetList("clear", YSE::T_GUI);
    reference.clear();
    step();

    // A frequency signal wins over the number; its first sample is read.
    buffer freq(N);
    for (unsigned int i = 0; i < N; ++i)
      freq.getPtr()[i] = 2000.f + static_cast<float>(i);
    reference.frequency(2000.f);
    step(&freq);
    CHECK(reference.frequency() == 2000.f);

    // Back to unconnected: the last number again (0.25, now read in Hz).
    reference.frequency(0.25f);
    step();

    // Unknown or malformed messages and non-finite numbers: ignored.
    obj.SetMessage("freq 300", 0.f);
    obj.SetMessage("lin", 0.f);
    obj.SetMessage("", 0.f);
    obj.GetInlet(1)->SetFloat(std::nanf(""), YSE::T_GUI);
    step();
    step();

    // Unconnected input: silence.
    BufferSink idleOut;
    dOnePole idle;
    Wire(idle, idleOut);
    idle.GetInlet(1)->SetFloat(1000.f, YSE::T_GUI);
    idle.Calculate(YSE::T_DSP);
    REQUIRE(idleOut.received != nullptr);
    CHECK(idleOut.received->getPtr()[N - 1] == 0.f);
  }

  TEST_CASE("~onepole: arguments are the frequency and a mode word (#1086)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto check = [](const std::string& args, onePole& reference) {
      CAPTURE(args);
      BufferSink out;
      dOnePole obj;
      Wire(obj, out);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
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

    onePole hz;
    hz.frequency(1500.f);
    check("1500", hz);
    onePole linear;
    linear.mode(onePole::LINEAR).frequency(0.5f);
    check("0.5 linear", linear);
    // The word may come first; a later number is ignored; case is free.
    onePole radians;
    radians.mode(onePole::RADIANS).frequency(0.2f);
    check("Radians 0.2 0.9", radians);
    onePole explicitHz;
    explicitHz.frequency(800.f);
    check("800 Hz", explicitHz);
    onePole none;
    check("", none);
  }

  TEST_CASE("~onepole: in a rendered patch, filtering a ~phasor with numbers and commands from "
            "receives, after a JSON round trip (#1086)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* filter = src.CreateObject(YSE::OBJ::D_ONEPOLE, "1000");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1086");
      YSE::pHandle* freq = src.CreateObject(YSE::OBJ::G_RECEIVE, "freq1086");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(filter != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(freq != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, filter, 0);
      src.Connect(cmd, 0, filter, 0);
      src.Connect(freq, 0, filter, 1);
      src.Connect(filter, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    phasor rampRef;
    onePole reference;
    reference.frequency(1000.f);
    for (int block = 0; block < 10; ++block) {
      CAPTURE(block);
      if (block == 2) {
        REQUIRE(dst.PassData(5000.f, "freq1086", YSE::T_GUI));
        reference.frequency(5000.f);
      }
      if (block == 4) {
        REQUIRE(dst.PassData("linear", "cmd1086", YSE::T_GUI));
        REQUIRE(dst.PassData(0.1f, "freq1086", YSE::T_GUI));
        reference.mode(onePole::LINEAR).frequency(0.1f);
      }
      if (block == 6) {
        REQUIRE(dst.PassData("clear", "cmd1086", YSE::T_GUI));
        reference.clear();
      }
      if (block == 8) {
        REQUIRE(dst.PassData("radians", "freq1086", YSE::T_GUI));
        REQUIRE(dst.PassData(1.f, "freq1086", YSE::T_GUI));
        reference.mode(onePole::RADIANS).frequency(1.f);
      }
      dst.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(rampRef(375.f, N)), 0.f));
      // Closed form: from block 6 on the state starts at 0, so the first
      // sample is a0 times the ramp's (6 * 128 mod 128 = 0, so 0).
      if (block == 6) CHECK(dst.output[0].getPtr()[0] == 0.f);
      // With a0 = 1 the ramp passes: sample n reads (n mod 128) / 128.
      if (block >= 8) {
        for (unsigned int i = 0; i < N; ++i) {
          CAPTURE(i);
          CHECK(dst.output[0].getPtr()[i] ==
                doctest::Approx(static_cast<float>(i) / 128.f).epsilon(1e-5));
        }
      }
    }
  }

  TEST_CASE("~onepole: numbers, messages and blocks on the audio thread allocate nothing "
            "(#1086)") {
    BufferSink out;
    dOnePole obj;
    Wire(obj, out);
    buffer in(N);
    buffer freq(N);
    in = 0.5f;
    freq = 700.f;
    const std::string linear = "linear";
    const std::string radians = "radians";
    const std::string hz = "Hz";
    const std::string clear = "clear";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(1)->SetFloat(500.f, YSE::T_DSP);
      obj.GetInlet(1)->SetInt(600, YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(linear, YSE::T_DSP);
      obj.GetInlet(1)->SetMessage(radians, YSE::T_DSP);
      obj.GetInlet(0)->SetList(hz, YSE::T_DSP);
      obj.GetInlet(1)->SetList(clear, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.GetInlet(1)->SetBuffer(&freq, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

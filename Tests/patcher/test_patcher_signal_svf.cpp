// Tests for the ~svf patcher object (patcher/filters/dSvf), issue #1089.
// The filtering is DSP::stateVariableFilter's and is tested in
// Tests/dsp/test_state_variable_filter.cpp; these show that the signals,
// numbers, mode words, keyword messages, lists, clear and arguments reach it
// and that all four outlets carry their output, alone and in a rendered patch
// after a JSON round trip.

#include <doctest/doctest.h>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/filters/dSvf.h"
#include "dsp/filters/stateVariableFilter.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::PATCHER::dSvf;
using YSE::PATCHER::patcherImplementation;
using svf = YSE::DSP::stateVariableFilter;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  struct Sinks {
    std::array<BufferSink, 4> out;
  };

  void Wire(dSvf& obj, Sinks& s) {
    for (unsigned int i = 0; i < 4; ++i) {
      obj.ConnectOutlet(s.out[i].GetInlet(0), i);
      s.out[i].ConnectInlet(obj.GetOutlet(i), 0);
    }
  }

  // The four outlets against the four outputs of `reference`, which has just
  // processed the same block.
  void CheckOutlets(Sinks& s, svf& reference) {
    const std::array<buffer*, 4> expected{&reference.lowpass(), &reference.highpass(),
                                          &reference.bandpass(), &reference.notch()};
    for (unsigned int i = 0; i < 4; ++i) {
      CAPTURE(i);
      REQUIRE(s.out[i].received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*s.out[i].received, *expected[i], 0.f));
    }
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

  TEST_CASE("~svf: registry and shape (#1089)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_SVF);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~svf");
    CHECK(obj->GetInputs() == 3);
    CHECK(obj->GetOutputs() == 4);
    for (unsigned int i = 0; i < 3; ++i)
      CHECK(obj->IsDSPInput(i));
    for (unsigned int i = 0; i < 4; ++i)
      CHECK(obj->OutputDataType(i) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_SVF));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::FILTER);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~svf: signals, numbers, modes, keywords, lists and clear against the DSP class "
            "(#1089, #1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    Sinks out;
    dSvf obj;
    Wire(obj, out);
    svf reference;
    reference.resonance(0.01f); // the object's default
    buffer objIn(N);
    buffer refIn(N);
    unsigned int start = 0;

    const auto step = [&](buffer* frequency = nullptr, buffer* resonance = nullptr) {
      obj.ResetDSP(); // as the patcher does before every block
      Fill(objIn, start);
      Fill(refIn, start);
      start += N;
      obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
      if (frequency != nullptr) obj.GetInlet(1)->SetBuffer(frequency, YSE::T_GUI);
      if (resonance != nullptr) obj.GetInlet(2)->SetBuffer(resonance, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      buffer f(N);
      buffer r(N);
      f = reference.frequency();
      r = reference.resonance();
      if (frequency != nullptr) f = *frequency;
      if (resonance != nullptr) r = *resonance;
      reference(refIn, f, r);
      CheckOutlets(out, reference);
    };

    // Default: frequency 0, lowpass and bandpass silent, highpass the input.
    step();
    CHECK(out.out[0].received->getPtr()[N - 1] == 0.f);
    CHECK(out.out[1].received->getPtr()[N - 1] == objIn.getPtr()[N - 1]);

    // Numbers in the two right inlets; ints too.
    obj.GetInlet(1)->SetFloat(1000.f, YSE::T_GUI);
    reference.frequency(1000.f);
    step();
    CHECK(out.out[0].received->getPtr()[N - 1] != 0.f);
    obj.GetInlet(2)->SetFloat(0.8f, YSE::T_GUI);
    reference.resonance(0.8f);
    step();
    obj.GetInlet(1)->SetInt(3000, YSE::T_GUI);
    obj.GetInlet(2)->SetInt(1, YSE::T_GUI);
    reference.frequency(3000.f).resonance(1.f);
    step();

    // Mode words in any inlet and letter case.
    obj.GetInlet(1)->SetFloat(0.25f, YSE::T_GUI);
    obj.GetInlet(2)->SetMessage("linear", YSE::T_GUI);
    reference.frequency(0.25f).mode(svf::LINEAR);
    step();
    obj.GetInlet(0)->SetMessage("RADIANS", YSE::T_GUI);
    reference.mode(svf::RADIANS);
    step();
    obj.GetInlet(1)->SetList("Hz", YSE::T_GUI);
    obj.GetInlet(1)->SetFloat(2000.f, YSE::T_GUI);
    reference.mode(svf::HZ).frequency(2000.f);
    step();

    // Keyword messages in any inlet, and as a list (text through a receive).
    obj.GetInlet(0)->SetMessage("freq 500", YSE::T_GUI);
    reference.frequency(500.f);
    step();
    obj.GetInlet(2)->SetMessage("q 0.4", YSE::T_GUI);
    obj.GetInlet(1)->SetMessage("cf 700", YSE::T_GUI);
    reference.resonance(0.4f).frequency(700.f);
    step();
    obj.GetInlet(1)->SetList("resonance 0.95", YSE::T_GUI);
    reference.resonance(0.95f);
    step();

    // A list of numbers: frequency, resonance; a shorter one sets the first.
    obj.GetInlet(0)->SetList("800 0.6", YSE::T_GUI);
    reference.frequency(800.f).resonance(0.6f);
    step();
    obj.GetInlet(0)->SetMessage("1200", YSE::T_GUI);
    reference.frequency(1200.f);
    step();

    // clear in any inlet resets the state before the next block.
    obj.GetInlet(2)->SetMessage("clear", YSE::T_GUI);
    reference.clear();
    step();
    obj.GetInlet(0)->SetList("clear", YSE::T_GUI);
    reference.clear();
    step();

    // Signals win over the numbers and are read per sample.
    buffer frequency(N);
    buffer resonance(N);
    for (unsigned int i = 0; i < N; ++i) {
      frequency.getPtr()[i] = 400.f + 20.f * static_cast<float>(i);
      resonance.getPtr()[i] = static_cast<float>(i) / static_cast<float>(N);
    }
    step(&frequency, &resonance);
    CHECK(reference.frequency() == frequency.getPtr()[N - 1]);
    // Only the frequency connected: the resonance number again.
    reference.resonance(0.6f);
    step(&frequency, nullptr);

    // Back to unconnected: the last numbers again.
    reference.frequency(1200.f).resonance(0.6f);
    step();

    // Unknown or malformed messages and non-finite numbers: ignored.
    obj.SetMessage("freq", 0.f);
    obj.SetMessage("freq abc", 0.f);
    obj.SetMessage("q nan", 0.f);
    obj.SetMessage("Q 0.3", 0.f);
    obj.SetMessage("bandwidth 300", 0.f);
    obj.SetMessage("nan 5", 0.f);
    obj.SetMessage("hertz", 0.f);
    obj.SetMessage("", 0.f);
    obj.GetInlet(1)->SetFloat(std::nanf(""), YSE::T_GUI);
    obj.GetInlet(2)->SetFloat(std::nanf(""), YSE::T_GUI);
    step();
    step();

    // Unconnected input: silence on every outlet.
    Sinks idleOut;
    dSvf idle;
    Wire(idle, idleOut);
    idle.GetInlet(1)->SetFloat(1000.f, YSE::T_GUI);
    idle.GetInlet(2)->SetFloat(0.9f, YSE::T_GUI);
    idle.Calculate(YSE::T_DSP);
    for (unsigned int i = 0; i < 4; ++i) {
      REQUIRE(idleOut.out[i].received != nullptr);
      CHECK(idleOut.out[i].received->getPtr()[N - 1] == 0.f);
    }
  }

  TEST_CASE("~svf: arguments are the frequency, the resonance and a mode (#1089)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto check = [](const std::string& args, float frequency, float resonance,
                          svf::FREQUENCY_MODE mode) {
      CAPTURE(args);
      Sinks out;
      dSvf obj;
      Wire(obj, out);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      svf reference;
      reference.frequency(frequency).resonance(resonance).mode(mode);
      buffer objIn(N);
      buffer refIn(N);
      for (unsigned int block = 0; block < 3; ++block) {
        CAPTURE(block);
        obj.ResetDSP();
        Fill(objIn, block * N);
        Fill(refIn, block * N);
        obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
        obj.Calculate(YSE::T_DSP);
        reference(refIn);
        CheckOutlets(out, reference);
      }
    };

    check("1500", 1500.f, 0.01f, svf::HZ);
    check("800 0.7", 800.f, 0.7f, svf::HZ);
    check("0.25 0.8 linear", 0.25f, 0.8f, svf::LINEAR);
    check("radians 0.5 0.3", 0.5f, 0.3f, svf::RADIANS);
    check("0.5 linear Hz", 0.5f, 0.01f, svf::HZ);
    // A third number is ignored; keywords set any value.
    check("800 0.7 3", 800.f, 0.7f, svf::HZ);
    check("q 0.9 freq 2000", 2000.f, 0.9f, svf::HZ);
    check("600 resonance 0.5", 600.f, 0.5f, svf::HZ);
    // A keyword without a number is skipped.
    check("q", 0.f, 0.01f, svf::HZ);
    check("", 0.f, 0.01f, svf::HZ);
  }

  TEST_CASE("~svf: in a rendered patch, all four outlets filter a ~phasor with numbers and "
            "commands from receives, after a JSON round trip (#1089)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(4, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* filter = src.CreateObject(YSE::OBJ::D_SVF, "1000 0.8");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1089");
      YSE::pHandle* res = src.CreateObject(YSE::OBJ::G_RECEIVE, "res1089");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(filter != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(res != nullptr);
      REQUIRE(dac != nullptr);
      REQUIRE(dac->GetInputs() == 4);
      src.Connect(ramp, 0, filter, 0);
      src.Connect(cmd, 0, filter, 0);
      src.Connect(res, 0, filter, 2);
      for (int i = 0; i < 4; ++i)
        src.Connect(filter, i, dac, i);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(4, nullptr);
    dst.ParseJSON(dump);

    phasor rampRef;
    svf reference;
    reference.frequency(1000.f).resonance(0.8f);
    for (int block = 0; block < 12; ++block) {
      CAPTURE(block);
      if (block == 2) {
        REQUIRE(dst.PassData(0.95f, "res1089", YSE::T_GUI));
        reference.resonance(0.95f);
      }
      if (block == 4) {
        REQUIRE(dst.PassData("freq 3000", "cmd1089", YSE::T_GUI));
        reference.frequency(3000.f);
      }
      if (block == 6) {
        REQUIRE(dst.PassData("clear", "cmd1089", YSE::T_GUI));
        reference.clear();
      }
      if (block == 8) {
        REQUIRE(dst.PassData("linear", "cmd1089", YSE::T_GUI));
        REQUIRE(dst.PassData("0.1 0.5", "cmd1089", YSE::T_GUI));
        reference.mode(svf::LINEAR).frequency(0.1f).resonance(0.5f);
      }
      if (block == 10) {
        REQUIRE(dst.PassData("freq 0", "cmd1089", YSE::T_GUI));
        reference.frequency(0.f);
      }
      dst.Calculate(YSE::T_DSP);
      buffer& ramp = rampRef(375.f, N);
      reference(ramp);
      const std::array<buffer*, 4> expected{&reference.lowpass(), &reference.highpass(),
                                            &reference.bandpass(), &reference.notch()};
      for (int i = 0; i < 4; ++i) {
        CAPTURE(i);
        CHECK(TestHelpers::buffersNearlyEqual(dst.output[i], *expected[i], 0.f));
      }
      // Closed form: lowpass + highpass + k * bandpass is the input, and the
      // notch is lowpass + highpass.
      const double k = reference.coefficient().k;
      for (unsigned int n = 0; n < N; ++n) {
        CAPTURE(n);
        const float lp = dst.output[0].getPtr()[n];
        const float hp = dst.output[1].getPtr()[n];
        const float bp = dst.output[2].getPtr()[n];
        const float notch = dst.output[3].getPtr()[n];
        const double x = ramp.getPtr()[n];
        CHECK(lp + hp + k * bp == doctest::Approx(x).epsilon(1e-4));
        CHECK(notch == doctest::Approx(lp + hp).epsilon(1e-4));
      }
      // A frequency of 0: the lowpass and bandpass are silent, the highpass
      // and notch pass the ramp.
      if (block >= 10) {
        for (unsigned int i = 0; i < N; ++i) {
          CAPTURE(i);
          CHECK(dst.output[0].getPtr()[i] == 0.f);
          CHECK(dst.output[2].getPtr()[i] == 0.f);
        }
      }
    }
  }

  TEST_CASE("~svf: numbers, messages and blocks on the audio thread allocate nothing "
            "(#1089)") {
    Sinks out;
    dSvf obj;
    Wire(obj, out);
    buffer in(N);
    buffer frequency(N);
    buffer resonance(N);
    in = 0.5f;
    frequency = 700.f;
    resonance = 0.7f;
    const std::string setFrequency = "freq 900";
    const std::string setQ = "q 0.3";
    const std::string list = "600 0.9";
    const std::string mode = "linear";
    const std::string clear = "clear";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(1)->SetFloat(500.f, YSE::T_DSP);
      obj.GetInlet(1)->SetInt(600, YSE::T_DSP);
      obj.GetInlet(2)->SetFloat(0.5f, YSE::T_DSP);
      obj.GetInlet(2)->SetInt(1, YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(setFrequency, YSE::T_DSP);
      obj.GetInlet(2)->SetList(setQ, YSE::T_DSP);
      obj.GetInlet(0)->SetList(list, YSE::T_DSP);
      obj.GetInlet(1)->SetMessage(mode, YSE::T_DSP);
      obj.GetInlet(1)->SetList(clear, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.GetInlet(1)->SetBuffer(&frequency, YSE::T_DSP);
      obj.GetInlet(2)->SetBuffer(&resonance, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

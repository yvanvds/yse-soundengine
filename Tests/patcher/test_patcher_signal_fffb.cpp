// Tests for the ~fffb patcher object (patcher/filters/dFffb), issue #1096.
// The filtering is DSP::filterBank's and is tested in
// Tests/dsp/test_filter_bank.cpp; these show that the signal, the messages,
// clear and the arguments reach it, that every outlet carries its filter, and
// that it works in a rendered patch after a JSON round trip.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/filters/dFffb.h"
#include "dsp/filters/filterBank.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::filterBank;
using YSE::DSP::phasor;
using YSE::PATCHER::dFffb;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  struct Sinks {
    std::vector<std::unique_ptr<BufferSink>> out;
  };

  void Wire(dFffb& obj, Sinks& s) {
    s.out.clear();
    for (unsigned int i = 0; i < obj.FilterCount(); ++i) {
      s.out.push_back(std::make_unique<BufferSink>());
      obj.ConnectOutlet(s.out[i]->GetInlet(0), i);
      s.out[i]->ConnectInlet(obj.GetOutlet(i), 0);
    }
  }

  // Every outlet against the outputs of `reference`, which has just
  // processed the same block.
  void CheckOutlets(Sinks& s, filterBank& reference) {
    REQUIRE(s.out.size() == reference.size());
    for (unsigned int i = 0; i < reference.size(); ++i) {
      CAPTURE(i);
      REQUIRE(s.out[i]->received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*s.out[i]->received, reference.output(i), 0.f));
    }
  }

  void Fill(buffer& b, unsigned int start) {
    for (unsigned int i = 0; i < b.getLength(); ++i) {
      const auto n = static_cast<float>(start + i);
      b.getPtr()[i] = std::sin(n * 0.03f) + 0.5f * std::sin(n * 0.4f) + 0.25f * std::sin(n * 1.9f);
    }
  }

  // Runs `obj` and `reference` over the same block.
  struct Stepper {
    dFffb& obj;
    Sinks& out;
    filterBank& reference;
    unsigned int start = 0;
    buffer objIn{N};
    buffer refIn{N};

    void operator()() {
      obj.ResetDSP(); // as the patcher does before every block
      Fill(objIn, start);
      Fill(refIn, start);
      start += N;
      obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      reference(refIn);
      CheckOutlets(out, reference);
    }
  };
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~fffb: registry and shape (#1096)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_FFFB);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~fffb");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 4);
    CHECK(obj->IsDSPInput(0));
    for (int i = 0; i < 4; ++i)
      CHECK(obj->OutputDataType(i) == YSE::OUT_TYPE::BUFFER);

    YSE::pHandle* eight = p.CreateObject(YSE::OBJ::D_FFFB, "8 200 H");
    REQUIRE(eight != nullptr);
    CHECK(eight->GetOutputs() == 8);
    YSE::pHandle* many = p.CreateObject(YSE::OBJ::D_FFFB, "500");
    REQUIRE(many != nullptr);
    CHECK(many->GetOutputs() == 128);
    YSE::pHandle* none = p.CreateObject(YSE::OBJ::D_FFFB, "0");
    REQUIRE(none != nullptr);
    CHECK(none->GetOutputs() == 1);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_FFFB));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::FILTER);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~fffb: messages and clear against the DSP class (#1096)") {
    TestHelpers::ScopedSampleRate rate(48000);
    Sinks out;
    dFffb obj;
    Wire(obj, out);
    filterBank reference;
    Stepper step{obj, out, reference};

    // Defaults: octaves from 100 Hz, every outlet carries signal.
    step();
    for (unsigned int i = 0; i < 4; ++i)
      CHECK(out.out[i]->received->getPtr()[N - 1] != 0.f);

    // Indexed settings from an index on.
    obj.SetMessage("freq 1 1500 2500", 0.f);
    reference.frequency(1, 1500.f).frequency(2, 2500.f);
    step();
    obj.GetInlet(0)->SetMessage("q 0 3 4 5 6", YSE::T_GUI);
    reference.q(0, 3.f).q(1, 4.f).q(2, 5.f).q(3, 6.f);
    step();
    obj.GetInlet(0)->SetList("gain 2 0.5", YSE::T_GUI);
    reference.gain(2, 0.5f);
    step();
    // Values past the last filter, and after a word, are ignored.
    obj.SetMessage("gain 3 0.25 9 9", 0.f);
    reference.gain(3, 0.25f);
    step();
    obj.SetMessage("freq 0 300 x 900", 0.f);
    reference.frequency(0, 300.f);
    step();

    // Every filter at once, Max's capitals too.
    obj.SetMessage("qall 12", 0.f);
    reference.qAll(12.f);
    step();
    obj.SetMessage("QAll 15", 0.f);
    reference.qAll(15.f);
    step();
    obj.SetMessage("gainAll 0.8", 0.f);
    reference.gainAll(0.8f);
    step();
    obj.SetMessage("freqall 1000", 0.f);
    reference.frequencyAll(1000.f);
    step();
    obj.SetMessage("Q 1 40", 0.f);
    reference.q(1, 40.f);
    step();

    // Spreads.
    obj.SetMessage("freqratio 440 2", 0.f);
    reference.frequencyRatio(440.f, 2.f);
    step();
    CHECK(reference.frequency(3) == 3520.f);
    obj.SetMessage("freqRatio 100 H", 0.f);
    reference.harmonic(100.f);
    step();
    CHECK(reference.frequency(3) == 400.f);

    // A list: index, frequency, optionally the Q.
    obj.GetInlet(0)->SetList("2 5000", YSE::T_GUI);
    reference.frequency(2, 5000.f);
    step();
    obj.SetMessage("3 7000 25", 0.f);
    reference.frequency(3, 7000.f).q(3, 25.f);
    step();

    // clear, also as a list through a receive.
    obj.SetMessage("clear", 0.f);
    reference.clear();
    step();
    obj.GetInlet(0)->SetList("CLEAR", YSE::T_GUI);
    reference.clear();
    step();

    // Ignored: unknown words, bad indexes, missing or non-finite values, a
    // ratio not above 0.
    for (const char* m :
         {"abc", "", "freq", "freq -1 500", "freq 1.5 500", "freq 4 500", "freq 0 nan", "qall",
          "gainall inf", "freqratio 100", "freqratio 100 0", "freqratio 100 -2", "freqratio x 2",
          "4 500", "-1 500", "1", "1 nan", "freqall nan"})
      obj.SetMessage(m, 0.f);
    obj.GetInlet(0)->SetFloat(300.f, YSE::T_GUI);
    step();
    step();

    // Unconnected input: silence on every outlet.
    Sinks idleOut;
    dFffb idle;
    Wire(idle, idleOut);
    idle.Calculate(YSE::T_DSP);
    for (unsigned int i = 0; i < 4; ++i) {
      REQUIRE(idleOut.out[i]->received != nullptr);
      CHECK(idleOut.out[i]->received->getPtr()[N - 1] == 0.f);
    }
  }

  TEST_CASE("~fffb: the arguments are count, frequency, ratio or H, Q and gain (#1096)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto check = [](const std::string& args, filterBank reference) {
      CAPTURE(args);
      Sinks out; // before the object, so it outlives it
      dFffb obj;
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      REQUIRE(obj.FilterCount() == reference.size());
      Wire(obj, out);
      Stepper step{obj, out, reference};
      for (unsigned int block = 0; block < 3; ++block) {
        CAPTURE(block);
        step();
      }
    };

    check("", filterBank());
    check("abc", filterBank());
    check("6", filterBank(6));
    {
      filterBank b(3);
      b.frequencyRatio(250.f, 2.f);
      check("3 250", b);
    }
    {
      filterBank b(5);
      b.frequencyRatio(110.f, 1.5f).qAll(30.f).gainAll(0.5f);
      check("5 110 1.5 30 0.5", b);
    }
    {
      filterBank b(8);
      b.harmonic(200.f).qAll(20.f).gainAll(0.25f);
      check("8 200 H 20 0.25", b);
      check("8 200 h 20 0.25", b);
    }
    {
      // A ratio not above 0 is ignored: the bank keeps its default octaves.
      filterBank b(2);
      b.qAll(4.f);
      check("2 300 0 4", b);
    }
    check("0", filterBank(1));
    check("1000", filterBank(128));
  }

  TEST_CASE("~fffb: in a rendered patch, two bands of a ~phasor set through receives, after a "
            "JSON round trip (#1096)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* bank = src.CreateObject(YSE::OBJ::D_FFFB, "2 375 H 30 0.5");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1096");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(bank != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(dac != nullptr);
      REQUIRE(bank->GetOutputs() == 2);
      REQUIRE(dac->GetInputs() == 2);
      src.Connect(ramp, 0, bank, 0);
      src.Connect(cmd, 0, bank, 0);
      src.Connect(bank, 0, dac, 0);
      src.Connect(bank, 1, dac, 1);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    phasor rampRef;
    filterBank reference(2);
    reference.harmonic(375.f).qAll(30.f).gainAll(0.5f);
    for (int block = 0; block < 400; ++block) {
      CAPTURE(block);
      if (block == 100) {
        REQUIRE(dst.PassData("gain 1 2", "cmd1096", YSE::T_GUI));
        reference.gain(1, 2.f);
      }
      if (block == 200) {
        REQUIRE(dst.PassData("clear", "cmd1096", YSE::T_GUI));
        reference.clear();
      }
      if (block == 250) {
        REQUIRE(dst.PassData("freqratio 750 2", "cmd1096", YSE::T_GUI));
        reference.frequencyRatio(750.f, 2.f);
      }
      dst.Calculate(YSE::T_DSP);
      buffer& r = rampRef(375.f, N);
      reference(r);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference.output(0), 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[1], reference.output(1), 0.f));

      // A ramp's harmonic k has amplitude 1 / (pi k): the two filters pick
      // out the fundamental and the second harmonic at their gains.
      if (block == 99 || block == 199 || block == 399) {
        float peak0 = 0.f;
        float peak1 = 0.f;
        for (unsigned int i = 0; i < N; ++i) {
          peak0 = std::max(peak0, std::fabs(dst.output[0].getPtr()[i]));
          peak1 = std::max(peak1, std::fabs(dst.output[1].getPtr()[i]));
        }
        const float pi = 3.14159265f;
        const float g1 = block == 99 ? 0.5f : 2.f;
        const float h0 = block == 399 ? 2.f : 1.f;
        const float h1 = block == 399 ? 4.f : 2.f;
        CHECK(peak0 == doctest::Approx(0.5f / (pi * h0)).epsilon(0.1));
        CHECK(peak1 == doctest::Approx(g1 / (pi * h1)).epsilon(0.1));
      }
    }
  }

  TEST_CASE("~fffb: messages and blocks on the audio thread allocate nothing (#1096)") {
    Sinks out;
    dFffb obj;
    Wire(obj, out);
    buffer in(N);
    in = 0.5f;
    const std::string freq = "freq 0 300 400";
    const std::string all = "qall 7";
    const std::string ratio = "freqratio 100 H";
    const std::string list = "1 900 4";
    const std::string clear = "clear";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetMessage(freq, YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(all, YSE::T_DSP);
      obj.GetInlet(0)->SetList(ratio, YSE::T_DSP);
      obj.GetInlet(0)->SetList(list, YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(clear, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

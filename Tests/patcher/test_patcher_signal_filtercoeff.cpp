// Tests for the ~filtercoeff patcher object (patcher/filters/dFiltercoeff), issue
// #1092. The coefficients are DSP::filterCoefficients' and are tested in
// Tests/dsp/test_filter_coefficients.cpp; these show that the signals,
// numbers, type words, lists, resamp and arguments reach it, and that its five
// outlets drive ~biquad in a rendered patch after a JSON round trip.

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
#include "patcher/filters/dFiltercoeff.h"
#include "dsp/filters.hpp"
#include "dsp/filters/filterCoefficients.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::biQuad;
using YSE::DSP::buffer;
using YSE::DSP::filterCoefficients;
using YSE::DSP::phasor;
using YSE::PATCHER::dFiltercoeff;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // One sink per outlet, declared before the object.
  struct Sinks {
    std::array<BufferSink, 5> out;
  };

  void Wire(dFiltercoeff& obj, Sinks& sinks) {
    for (int i = 0; i < 5; ++i)
      TestHelpers::Wire(obj, i, sinks.out[i], 0);
  }

  // Each outlet matches the reference's output exactly.
  void Same(Sinks& sinks, filterCoefficients& reference) {
    for (unsigned int i = 0; i < 5; ++i) {
      CAPTURE(i);
      REQUIRE(sinks.out[i].received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*sinks.out[i].received, reference.output(i), 0.f));
    }
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~filtercoeff: registry and shape (#1092)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_FILTERCOEFF);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~filtercoeff");
    CHECK(obj->GetInputs() == 3);
    CHECK(obj->GetOutputs() == 5);
    for (unsigned int i = 0; i < 3; ++i)
      CHECK(obj->IsDSPInput(i));
    for (unsigned int i = 0; i < 5; ++i)
      CHECK(obj->OutputDataType(i) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_FILTERCOEFF));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::FILTER);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~filtercoeff: signals, numbers, type words, lists and resamp against the DSP "
            "class (#1092, #1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    Sinks sinks;
    dFiltercoeff obj;
    Wire(obj, sinks);
    filterCoefficients reference;
    int stepIndex = 0;

    const auto step = [&](buffer* frequency = nullptr, buffer* gain = nullptr,
                          buffer* q = nullptr) {
      ++stepIndex;
      CAPTURE(stepIndex);
      obj.ResetDSP(); // as the patcher does before every block
      if (frequency != nullptr) obj.GetInlet(0)->SetBuffer(frequency, YSE::T_GUI);
      if (gain != nullptr) obj.GetInlet(1)->SetBuffer(gain, YSE::T_GUI);
      if (q != nullptr) obj.GetInlet(2)->SetBuffer(q, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      buffer f(N);
      buffer g(N);
      buffer s(N);
      f = reference.frequency();
      g = reference.gain();
      s = reference.q();
      if (frequency != nullptr) f = *frequency;
      if (gain != nullptr) g = *gain;
      if (q != nullptr) s = *q;
      reference(f, g, s);
      Same(sinks, reference);
    };

    // Default: a Butterworth lowpass at 1000 Hz.
    step();
    CHECK(sinks.out[0].received->getPtr()[0] ==
          doctest::Approx(filterCoefficients::Compute(filterCoefficients::LOWPASS, 1000.f, 1.f,
                                                      0.70710678f, 48000.f)
                              .a0));

    // Numbers in the three inlets; ints too.
    obj.GetInlet(0)->SetFloat(2500.f, YSE::T_GUI);
    obj.GetInlet(2)->SetFloat(3.f, YSE::T_GUI);
    reference.frequency(2500.f).q(3.f);
    step();
    obj.GetInlet(0)->SetInt(800, YSE::T_GUI);
    obj.GetInlet(1)->SetInt(2, YSE::T_GUI);
    obj.GetInlet(2)->SetInt(5, YSE::T_GUI);
    reference.frequency(800.f).gain(2.f).q(5.f);
    step();
    obj.GetInlet(1)->SetFloat(0.5f, YSE::T_GUI);
    reference.gain(0.5f);
    step();

    // Type words, alone and with arguments, in any inlet and as a list.
    obj.GetInlet(0)->SetMessage("peaknotch", YSE::T_GUI);
    reference.type(filterCoefficients::PEAKNOTCH);
    step();
    obj.GetInlet(2)->SetMessage("lowshelf 300 4 0.8", YSE::T_GUI);
    reference.type(filterCoefficients::LOWSHELF).frequency(300.f).gain(4.f).q(0.8f);
    step();
    // A type without a gain takes the frequency and the Q.
    obj.GetInlet(1)->SetList("bandpass 1200 10", YSE::T_GUI);
    reference.type(filterCoefficients::BANDPASS).frequency(1200.f).q(10.f);
    step();
    CHECK(reference.gain() == 4.f);
    obj.GetInlet(0)->SetMessage("gainresonant 600 0.25 7", YSE::T_GUI);
    reference.type(filterCoefficients::GAINRESONANT).frequency(600.f).gain(0.25f).q(7.f);
    step();
    // A shorter argument list sets the first ones only.
    obj.GetInlet(0)->SetMessage("highpass 5000", YSE::T_GUI);
    reference.type(filterCoefficients::HIGHPASS).frequency(5000.f);
    step();
    const char* words[] = {"off",       "lowpass",   "highpass",     "bandpass",
                           "bandstop",  "peaknotch", "lowshelf",     "highshelf",
                           "resonant",  "allpass",   "gainlpass",    "gainhpass",
                           "gainbpass", "gainbstop", "gainresonant", "gainapass"};
    for (int t = 0; t < filterCoefficients::TYPE_COUNT; ++t) {
      CAPTURE(words[t]);
      obj.GetInlet(t % 3)->SetMessage(words[t], YSE::T_GUI);
      reference.type(static_cast<filterCoefficients::TYPE>(t));
      step();
    }
    // off: the input passes.
    obj.GetInlet(0)->SetMessage("off", YSE::T_GUI);
    reference.type(filterCoefficients::OFF);
    step();
    CHECK(sinks.out[0].received->getPtr()[0] == 1.f);
    CHECK(sinks.out[3].received->getPtr()[0] == 0.f);
    obj.GetInlet(0)->SetMessage("lowpass", YSE::T_GUI);
    reference.type(filterCoefficients::LOWPASS);

    // A list of numbers: the frequency, the gain, the Q.
    obj.GetInlet(0)->SetList("700 1.5 2", YSE::T_GUI);
    reference.frequency(700.f).gain(1.5f).q(2.f);
    step();
    obj.GetInlet(0)->SetMessage("900", YSE::T_GUI);
    reference.frequency(900.f);
    step();

    // resamp: one computation in n samples, counted across blocks.
    obj.GetInlet(0)->SetMessage("resamp 3", YSE::T_GUI);
    reference.resampling(3);
    buffer sweep(N);
    for (unsigned int i = 0; i < N; ++i)
      sweep.getPtr()[i] = 200.f + 10.f * static_cast<float>(i);
    step(&sweep);
    CHECK(sinks.out[0].received->getPtr()[1] == sinks.out[0].received->getPtr()[0]);
    CHECK(sinks.out[0].received->getPtr()[3] != sinks.out[0].received->getPtr()[2]);
    step(&sweep);
    obj.GetInlet(1)->SetList("resamp 1", YSE::T_GUI);
    reference.resampling(1);

    // Signals win over the numbers and are read per sample.
    buffer gain(N);
    buffer q(N);
    for (unsigned int i = 0; i < N; ++i) {
      gain.getPtr()[i] = 0.5f + 0.01f * static_cast<float>(i);
      q.getPtr()[i] = 1.f + 0.05f * static_cast<float>(i);
    }
    obj.GetInlet(0)->SetMessage("gainlpass", YSE::T_GUI);
    reference.type(filterCoefficients::GAINLPASS);
    step(&sweep, &gain, &q);
    CHECK(reference.frequency() == sweep.getPtr()[N - 1]);
    // Only the gain connected: the other two read their last numbers again.
    reference.frequency(900.f).q(2.f);
    step(nullptr, &gain, nullptr);
    // Back to unconnected: the last numbers again.
    reference.gain(1.5f);
    step();
    obj.GetInlet(0)->SetFloat(1100.f, YSE::T_GUI);
    obj.GetInlet(2)->SetFloat(4.f, YSE::T_GUI);
    reference.frequency(1100.f).q(4.f);
    step();

    // Unknown or malformed messages and non-finite numbers: ignored.
    obj.SetMessage("lowpas", 0.f);
    obj.SetMessage("resamp", 0.f);
    obj.SetMessage("resamp x", 0.f);
    obj.SetMessage("nan 5 5", 0.f);
    obj.SetMessage("", 0.f);
    obj.GetInlet(0)->SetFloat(std::nanf(""), YSE::T_GUI);
    obj.GetInlet(1)->SetFloat(std::nanf(""), YSE::T_GUI);
    obj.GetInlet(2)->SetFloat(std::nanf(""), YSE::T_GUI);
    step();
    step();
  }

  TEST_CASE("~filtercoeff: arguments are a filter type and a resampling factor (#1092)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto check = [](const std::string& args, filterCoefficients::TYPE type,
                          unsigned int factor) {
      CAPTURE(args);
      Sinks sinks;
      dFiltercoeff obj;
      Wire(obj, sinks);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      filterCoefficients reference;
      reference.type(type).resampling(factor);
      buffer f(N);
      buffer g(N);
      buffer q(N);
      for (unsigned int i = 0; i < N; ++i)
        f.getPtr()[i] = 100.f + 5.f * static_cast<float>(i);
      g = 1.f;
      q = 0.70710678f;
      for (unsigned int block = 0; block < 3; ++block) {
        CAPTURE(block);
        obj.ResetDSP();
        obj.GetInlet(0)->SetBuffer(&f, YSE::T_GUI);
        obj.Calculate(YSE::T_DSP);
        reference(f, g, q);
        Same(sinks, reference);
      }
    };

    check("", filterCoefficients::LOWPASS, 1);
    check("highshelf", filterCoefficients::HIGHSHELF, 1);
    check("peaknotch 4", filterCoefficients::PEAKNOTCH, 4);
    check("allpass 0", filterCoefficients::ALLPASS, 1);
    check("8", filterCoefficients::LOWPASS, 8);
    check("off 100000", filterCoefficients::OFF, filterCoefficients::MAX_RESAMPLING);
    // An unknown word is skipped.
    check("bogus resonant 2", filterCoefficients::RESONANT, 2);
  }

  TEST_CASE("~filtercoeff: in a rendered patch, driving ~biquad from receives after a JSON "
            "round trip (#1092)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* coeff = src.CreateObject(YSE::OBJ::D_FILTERCOEFF, "resonant");
      YSE::pHandle* filter = src.CreateObject(YSE::OBJ::D_BIQUAD, "");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1092");
      YSE::pHandle* q = src.CreateObject(YSE::OBJ::G_RECEIVE, "q1092");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(coeff != nullptr);
      REQUIRE(filter != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(q != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, filter, 0);
      for (int i = 0; i < 5; ++i)
        src.Connect(coeff, i, filter, i + 1);
      src.Connect(cmd, 0, coeff, 0);
      src.Connect(q, 0, coeff, 2);
      src.Connect(filter, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    phasor rampRef;
    filterCoefficients coeffRef;
    coeffRef.type(filterCoefficients::RESONANT);
    biQuad filterRef;
    for (int block = 0; block < 12; ++block) {
      CAPTURE(block);
      if (block == 2) {
        REQUIRE(dst.PassData(6.f, "q1092", YSE::T_GUI));
        coeffRef.q(6.f);
      }
      if (block == 4) {
        REQUIRE(dst.PassData("lowpass 3000 0.7071", "cmd1092", YSE::T_GUI));
        coeffRef.type(filterCoefficients::LOWPASS).frequency(3000.f).q(0.7071f);
      }
      if (block == 6) {
        REQUIRE(dst.PassData("gainlpass 3000 0.5 0.7071", "cmd1092", YSE::T_GUI));
        coeffRef.type(filterCoefficients::GAINLPASS).gain(0.5f);
      }
      if (block == 8) {
        REQUIRE(dst.PassData("off", "cmd1092", YSE::T_GUI));
        coeffRef.type(filterCoefficients::OFF);
      }
      if (block == 10) {
        REQUIRE(dst.PassData("resamp 16", "cmd1092", YSE::T_GUI));
        REQUIRE(dst.PassData("peaknotch 900 3 2", "cmd1092", YSE::T_GUI));
        coeffRef.resampling(16)
            .type(filterCoefficients::PEAKNOTCH)
            .frequency(900.f)
            .gain(3.f)
            .q(2.f);
      }
      dst.Calculate(YSE::T_DSP);
      buffer& in = rampRef(375.f, N);
      buffer f(N);
      buffer g(N);
      buffer s(N);
      f = coeffRef.frequency();
      g = coeffRef.gain();
      s = coeffRef.q();
      coeffRef(f, g, s);
      buffer& expected = filterRef(in, &coeffRef.a0(), &coeffRef.a1(), &coeffRef.a2(),
                                   &coeffRef.b1(), &coeffRef.b2());
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], expected, 0.f));
      // Closed form: off makes ~biquad pass the ramp unchanged.
      if (block == 8 || block == 9) {
        for (unsigned int i = 0; i < N; ++i) {
          CAPTURE(i);
          CHECK(dst.output[0].getPtr()[i] == in.getPtr()[i]);
        }
      }
    }
  }

  TEST_CASE("~filtercoeff: numbers, messages and blocks on the audio thread allocate nothing "
            "(#1092)") {
    Sinks sinks;
    dFiltercoeff obj;
    Wire(obj, sinks);
    buffer frequency(N);
    buffer gain(N);
    buffer q(N);
    frequency = 700.f;
    gain = 0.5f;
    q = 7.f;
    const std::string type = "peaknotch 900 2 3";
    const std::string list = "500 1 2";
    const std::string resamp = "resamp 4";
    const std::string off = "off";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      for (int i = 0; i < 3; ++i) {
        obj.GetInlet(i)->SetFloat(0.5f + static_cast<float>(i), YSE::T_DSP);
        obj.GetInlet(i)->SetInt(i + 1, YSE::T_DSP);
      }
      obj.GetInlet(0)->SetMessage(type, YSE::T_DSP);
      obj.GetInlet(1)->SetList(list, YSE::T_DSP);
      obj.GetInlet(2)->SetMessage(resamp, YSE::T_DSP);
      obj.GetInlet(0)->SetList(off, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&frequency, YSE::T_DSP);
      obj.GetInlet(1)->SetBuffer(&gain, YSE::T_DSP);
      obj.GetInlet(2)->SetBuffer(&q, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

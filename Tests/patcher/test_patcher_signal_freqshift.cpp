// Tests for the ~freqshift patcher object (patcher/filters/dFreqshift), issue
// #1110. The shifting is DSP::frequencyShifter's and is tested in
// Tests/dsp/test_frequency_shifter.cpp; these show that the signals, numbers,
// messages and argument reach it and that both outlets carry their side,
// alone and in rendered patches after a JSON round trip: bit-exact on a
// ~phasor, and a ~cycle moved up and down by the shift.

#include <doctest/doctest.h>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/filters/dFreqshift.h"
#include "dsp/modules/frequencyShifter.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::frequencyShifter;
using YSE::DSP::phasor;
using YSE::PATCHER::dFreqshift;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;

  struct Sinks {
    std::array<BufferSink, 2> out;
  };

  void Wire(dFreqshift& obj, Sinks& s) {
    for (unsigned int i = 0; i < 2; ++i) {
      obj.ConnectOutlet(s.out[i].GetInlet(0), i);
      s.out[i].ConnectInlet(obj.GetOutlet(i), 0);
    }
  }

  void CheckOutlets(Sinks& s, frequencyShifter& reference) {
    REQUIRE(s.out[0].received != nullptr);
    REQUIRE(s.out[1].received != nullptr);
    CHECK(TestHelpers::buffersNearlyEqual(*s.out[0].received, reference.up(), 0.f));
    CHECK(TestHelpers::buffersNearlyEqual(*s.out[1].received, reference.down(), 0.f));
  }

  void Fill(buffer& b, unsigned int start) {
    for (unsigned int i = 0; i < b.getLength(); ++i) {
      const auto n = static_cast<float>(start + i);
      b.getPtr()[i] = std::sin(n * 0.05f) + 0.5f * std::sin(n * 1.3f);
    }
  }

  // The amplitude of `v` at `w` radians per sample, fitted as
  // a cos(w n) + b sin(w n) by least squares.
  double Amplitude(const std::vector<float>& v, double w) {
    double cc = 0.0;
    double ss = 0.0;
    double cs = 0.0;
    double vc = 0.0;
    double vs = 0.0;
    for (std::size_t n = 0; n < v.size(); ++n) {
      const double c = std::cos(w * static_cast<double>(n));
      const double s = std::sin(w * static_cast<double>(n));
      cc += c * c;
      ss += s * s;
      cs += c * s;
      vc += v[n] * c;
      vs += v[n] * s;
    }
    const double det = cc * ss - cs * cs;
    const double a = (vc * ss - vs * cs) / det;
    const double b = (vs * cc - vc * cs) / det;
    return std::hypot(a, b);
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~freqshift: registry and shape (#1110)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_FREQSHIFT);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~freqshift");
    CHECK(obj->GetInputs() == 2);
    CHECK(obj->GetOutputs() == 2);
    for (unsigned int i = 0; i < 2; ++i)
      CHECK(obj->IsDSPInput(i));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(obj->OutputDataType(1) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_FREQSHIFT));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::FILTER);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~freqshift: signals, numbers and messages against the DSP class (#1110, #1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    Sinks out;
    dFreqshift obj;
    Wire(obj, out);
    frequencyShifter reference;
    buffer objIn(N);
    buffer refIn(N);
    unsigned int start = 0;
    float shift = frequencyShifter::DEFAULT_SHIFT;

    const auto step = [&](buffer* s = nullptr) {
      obj.ResetDSP(); // as the patcher does before every block
      Fill(objIn, start);
      Fill(refIn, start);
      start += N;
      obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
      if (s != nullptr) obj.GetInlet(1)->SetBuffer(s, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      buffer shifts(N);
      shifts = shift;
      if (s != nullptr) shifts = *s;
      reference(refIn, shifts);
      CheckOutlets(out, reference);
    };

    // Default: no shift, both outlets alike.
    step();
    CHECK(TestHelpers::buffersNearlyEqual(*out.out[0].received, *out.out[1].received, 0.f));

    // Numbers in the right inlet, floats and ints.
    obj.GetInlet(1)->SetFloat(123.5f, YSE::T_GUI);
    shift = 123.5f;
    step();
    CHECK(reference.shift() == 123.5f);
    obj.GetInlet(1)->SetInt(-40, YSE::T_GUI);
    shift = -40.f;
    step();
    CHECK(reference.shift() == -40.f);

    // clear and resetphase, in either inlet, as messages and as text.
    obj.GetInlet(0)->SetMessage("resetphase", YSE::T_GUI);
    reference.resetPhase();
    step();
    obj.GetInlet(1)->SetMessage("clear", YSE::T_GUI);
    reference.clear();
    step();
    obj.GetInlet(0)->SetList("clear", YSE::T_GUI);
    reference.clear();
    step();
    obj.GetInlet(1)->SetList("resetphase", YSE::T_GUI);
    reference.resetPhase();
    step();
    obj.SetMessage("  resetphase", 0.f);
    reference.resetPhase();
    step();
    // Both in one block: the filter and the phase start again.
    obj.SetMessage("clear", 0.f);
    obj.SetMessage("resetphase", 0.f);
    reference.clear();
    reference.resetPhase();
    step();

    // Unknown messages: ignored.
    obj.SetMessage("abc", 0.f);
    obj.SetMessage("resetphases", 0.f);
    obj.SetMessage("", 0.f);
    step();

    // A signal wins over the number and is read per sample.
    buffer s(N);
    for (unsigned int i = 0; i < N; ++i)
      s.getPtr()[i] = i < N / 2 ? 300.f : -1000.f;
    step(&s);
    CHECK(reference.shift() == -1000.f);
    // Back to unconnected: the last number again.
    step();
    CHECK(reference.shift() == -40.f);

    // Unconnected input: silence on both outlets.
    Sinks idleOut;
    dFreqshift idle;
    Wire(idle, idleOut);
    idle.SetParams("100");
    idle.Calculate(YSE::T_DSP);
    REQUIRE(idleOut.out[0].received != nullptr);
    REQUIRE(idleOut.out[1].received != nullptr);
    for (unsigned int i = 0; i < N; ++i) {
      CAPTURE(i);
      CHECK(idleOut.out[0].received->getPtr()[i] == 0.f);
      CHECK(idleOut.out[1].received->getPtr()[i] == 0.f);
    }
  }

  TEST_CASE("~freqshift: the argument is the shift (#1110)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto check = [](const std::string& args, float shift) {
      CAPTURE(args);
      Sinks out;
      dFreqshift obj;
      Wire(obj, out);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      frequencyShifter reference;
      buffer objIn(N);
      buffer refIn(N);
      buffer shifts(N);
      shifts = shift;
      for (unsigned int block = 0; block < 3; ++block) {
        CAPTURE(block);
        obj.ResetDSP();
        Fill(objIn, block * N);
        Fill(refIn, block * N);
        obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
        obj.Calculate(YSE::T_DSP);
        reference(refIn, shifts);
        CheckOutlets(out, reference);
      }
    };

    check("100", 100.f);
    check("-37.5", -37.5f);
    check("0", 0.f);
    check("", frequencyShifter::DEFAULT_SHIFT);
  }

  TEST_CASE("~freqshift: in a rendered patch, both sides of a ~phasor with a number and "
            "resetphase from receives, after a JSON round trip (#1110)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* shifter = src.CreateObject(YSE::OBJ::D_FREQSHIFT, "250");
      YSE::pHandle* amount = src.CreateObject(YSE::OBJ::G_RECEIVE, "shift1110");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1110");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(shifter != nullptr);
      REQUIRE(amount != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(dac != nullptr);
      REQUIRE(dac->GetInputs() == 2);
      src.Connect(ramp, 0, shifter, 0);
      src.Connect(amount, 0, shifter, 1);
      src.Connect(cmd, 0, shifter, 0);
      src.Connect(shifter, 0, dac, 0);
      src.Connect(shifter, 1, dac, 1);
      dump = src.DumpJSON();
    }
    CHECK(dump.find("~freqshift") != std::string::npos);
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    phasor rampRef;
    frequencyShifter reference;
    buffer shifts(N);
    shifts = 250.f;
    for (int block = 0; block < 10; ++block) {
      CAPTURE(block);
      if (block == 4) {
        REQUIRE(dst.PassData(-1200.5f, "shift1110", YSE::T_GUI));
        shifts = -1200.5f;
      }
      if (block == 7) {
        REQUIRE(dst.PassData("resetphase", "cmd1110", YSE::T_GUI));
        reference.resetPhase();
      }
      dst.Calculate(YSE::T_DSP);
      reference(rampRef(375.f, N), shifts);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference.up(), 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[1], reference.down(), 0.f));
      // At phase 0 the carrier is (1, 0): both sides start equal.
      if (block == 0 || block == 7) CHECK(dst.output[0].getPtr()[0] == dst.output[1].getPtr()[0]);
    }
  }

  TEST_CASE("~freqshift: ~cycle -> ~freqshift moves a sine up and down by the shift, after a "
            "JSON round trip (#1110)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* osc = src.CreateObject(YSE::OBJ::D_CYCLE, "1000");
      YSE::pHandle* shifter = src.CreateObject(YSE::OBJ::D_FREQSHIFT, "200");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(osc != nullptr);
      REQUIRE(shifter != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(osc, 0, shifter, 0);
      src.Connect(shifter, 0, dac, 0);
      src.Connect(shifter, 1, dac, 1);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    std::vector<float> up;
    std::vector<float> down;
    for (int block = 0; block < 200; ++block) {
      dst.Calculate(YSE::T_DSP);
      if (block < 100) continue; // let the filter settle (~0.27 s)
      for (unsigned int i = 0; i < N; ++i) {
        up.push_back(dst.output[0].getPtr()[i]);
        down.push_back(dst.output[1].getPtr()[i]);
      }
    }
    const double higher = 2.0 * PI * 1200.0 / 48000.0;
    const double lower = 2.0 * PI * 800.0 / 48000.0;
    CHECK(Amplitude(up, higher) == doctest::Approx(1.0).epsilon(0.01));
    CHECK(Amplitude(up, lower) < 0.01);
    CHECK(Amplitude(down, lower) == doctest::Approx(1.0).epsilon(0.01));
    CHECK(Amplitude(down, higher) < 0.01);
  }

  TEST_CASE("~freqshift: numbers, messages and blocks on the audio thread allocate nothing "
            "(#1110)") {
    Sinks out;
    dFreqshift obj;
    Wire(obj, out);
    buffer in(N);
    buffer s(N);
    in = 0.5f;
    s = 75.f;
    const std::string clear = "clear";
    const std::string reset = "resetphase";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(1)->SetFloat(12.5f, YSE::T_DSP);
      obj.GetInlet(1)->SetInt(30, YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(clear, YSE::T_DSP);
      obj.GetInlet(1)->SetList(reset, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.GetInlet(1)->SetBuffer(&s, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

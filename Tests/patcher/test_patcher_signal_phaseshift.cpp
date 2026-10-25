// Tests for the ~phaseshift patcher object (patcher/filters/dPhaseshift),
// issue #1098. The filtering is DSP::phaseShift's and is tested in
// Tests/dsp/test_phase_shift.cpp; these show that the signals, numbers,
// lists, clear and arguments reach it, alone and in rendered patches after a
// JSON round trip: bit-exact on a ~phasor, and as a phaser's notch when a
// ~cycle at the center frequency is mixed with its phase-shifted copy.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/filters/dPhaseshift.h"
#include "dsp/filters/phaseShift.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::phaseShift;
using YSE::DSP::phasor;
using YSE::PATCHER::dPhaseshift;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dPhaseshift& obj, BufferSink& out) {
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

  TEST_CASE("~phaseshift: registry and shape (#1098)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_PHASESHIFT);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~phaseshift");
    CHECK(obj->GetInputs() == 3);
    CHECK(obj->GetOutputs() == 1);
    for (unsigned int i = 0; i < 3; ++i)
      CHECK(obj->IsDSPInput(i));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_PHASESHIFT));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::FILTER);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~phaseshift: signals, numbers, lists and clear against the DSP class "
            "(#1098, #1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink out;
    dPhaseshift obj;
    Wire(obj, out);
    phaseShift reference;
    buffer objIn(N);
    buffer refIn(N);
    unsigned int start = 0;

    const auto step = [&](buffer* frequency = nullptr, buffer* q = nullptr) {
      obj.ResetDSP(); // as the patcher does before every block
      Fill(objIn, start);
      Fill(refIn, start);
      start += N;
      obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
      if (frequency != nullptr) obj.GetInlet(1)->SetBuffer(frequency, YSE::T_GUI);
      if (q != nullptr) obj.GetInlet(2)->SetBuffer(q, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(out.received != nullptr);
      buffer f(N);
      buffer s(N);
      f = reference.frequency();
      s = reference.q();
      if (frequency != nullptr) f = *frequency;
      if (q != nullptr) s = *q;
      CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(refIn, f, s), 0.f));
    };

    // Default: 1000 Hz, Q 1; the signal passes, phase shifted.
    step();
    CHECK(out.received->getPtr()[N - 1] != 0.f);

    // Numbers in the two right inlets; ints too.
    obj.GetInlet(1)->SetFloat(300.f, YSE::T_GUI);
    reference.frequency(300.f);
    step();
    obj.GetInlet(2)->SetFloat(6.f, YSE::T_GUI);
    reference.q(6.f);
    step();
    obj.GetInlet(1)->SetInt(3000, YSE::T_GUI);
    obj.GetInlet(2)->SetInt(2, YSE::T_GUI);
    reference.frequency(3000.f).q(2.f);
    step();

    // A list of numbers in any inlet, also as text through a receive: the
    // frequency, then the Q; a single number sets the frequency.
    obj.GetInlet(0)->SetList("800 4", YSE::T_GUI);
    reference.frequency(800.f).q(4.f);
    step();
    obj.GetInlet(0)->SetMessage("1200", YSE::T_GUI);
    reference.frequency(1200.f);
    step();
    // A bare number in a right inlet is that inlet's number.
    obj.GetInlet(2)->SetMessage("3", YSE::T_GUI);
    reference.q(3.f);
    step();
    obj.GetInlet(1)->SetList("500 0.5", YSE::T_GUI);
    reference.frequency(500.f).q(0.5f);
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
    buffer q(N);
    for (unsigned int i = 0; i < N; ++i) {
      frequency.getPtr()[i] = 400.f + 20.f * static_cast<float>(i);
      q.getPtr()[i] = 0.7f + 0.05f * static_cast<float>(i);
    }
    step(&frequency, &q);
    CHECK(reference.frequency() == frequency.getPtr()[N - 1]);
    // Only the Q connected: the frequency number again.
    reference.frequency(500.f);
    step(nullptr, &q);

    // Back to unconnected: the last numbers again.
    reference.frequency(500.f).q(0.5f);
    step();

    // Unknown or malformed messages and non-finite numbers: ignored.
    obj.SetMessage("freq 300", 0.f);
    obj.SetMessage("abc 5", 0.f);
    obj.SetMessage("nan 5", 0.f);
    obj.SetMessage("", 0.f);
    obj.GetInlet(1)->SetFloat(std::nanf(""), YSE::T_GUI);
    obj.GetInlet(2)->SetFloat(std::nanf(""), YSE::T_GUI);
    step();
    step();
    // A list whose second token is not a number sets only the frequency.
    obj.GetInlet(0)->SetList("700 nan", YSE::T_GUI);
    reference.frequency(700.f);
    step();

    // Unconnected input: silence.
    BufferSink idleOut;
    dPhaseshift idle;
    Wire(idle, idleOut);
    idle.Calculate(YSE::T_DSP);
    REQUIRE(idleOut.received != nullptr);
    CHECK(idleOut.received->getPtr()[N - 1] == 0.f);
  }

  TEST_CASE("~phaseshift: arguments are the center frequency and the Q (#1098)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto check = [](const std::string& args, float frequency, float q) {
      CAPTURE(args);
      BufferSink out;
      dPhaseshift obj;
      Wire(obj, out);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      phaseShift reference;
      reference.frequency(frequency).q(q);
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

    check("1500", 1500.f, phaseShift::DEFAULT_Q);
    check("800 2", 800.f, 2.f);
    // A third number is ignored, a word skipped.
    check("800 2 3", 800.f, 2.f);
    check("x 600 5", 600.f, 5.f);
    check("", phaseShift::DEFAULT_FREQUENCY, phaseShift::DEFAULT_Q);
  }

  TEST_CASE("~phaseshift: in a rendered patch, filtering a ~phasor with numbers and commands "
            "from receives, after a JSON round trip (#1098)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* filter = src.CreateObject(YSE::OBJ::D_PHASESHIFT, "2000 3");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1098");
      YSE::pHandle* q = src.CreateObject(YSE::OBJ::G_RECEIVE, "q1098");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(filter != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(q != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, filter, 0);
      src.Connect(cmd, 0, filter, 0);
      src.Connect(q, 0, filter, 2);
      src.Connect(filter, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    phasor rampRef;
    phaseShift reference;
    reference.frequency(2000.f).q(3.f);
    for (int block = 0; block < 10; ++block) {
      CAPTURE(block);
      if (block == 2) {
        REQUIRE(dst.PassData(0.5f, "q1098", YSE::T_GUI));
        reference.q(0.5f);
      }
      if (block == 4) {
        REQUIRE(dst.PassData("clear", "cmd1098", YSE::T_GUI));
        reference.clear();
      }
      if (block == 6) {
        REQUIRE(dst.PassData("400 8", "cmd1098", YSE::T_GUI));
        reference.frequency(400.f).q(8.f);
      }
      dst.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(rampRef(375.f, N)), 0.f));
      // From block 4 on the state starts at 0 and the ramp's first sample is
      // 0 (4 * 128 mod 128), so the output starts at 0.
      if (block == 4) CHECK(dst.output[0].getPtr()[0] == 0.f);
    }
  }

  TEST_CASE("~phaseshift: a ~cycle mixed with its phase-shifted copy is cancelled at the center "
            "frequency and passes an octave away, after a JSON round trip (#1098)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto render = [](const char* cycle) {
      std::string dump;
      {
        patcherImplementation src(1, nullptr);
        YSE::pHandle* osc = src.CreateObject(YSE::OBJ::D_CYCLE, cycle);
        YSE::pHandle* shift = src.CreateObject(YSE::OBJ::D_PHASESHIFT, "1000 2");
        YSE::pHandle* mix = src.CreateObject(YSE::OBJ::D_ADD, "");
        YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
        REQUIRE(osc != nullptr);
        REQUIRE(shift != nullptr);
        REQUIRE(mix != nullptr);
        REQUIRE(dac != nullptr);
        src.Connect(osc, 0, shift, 0);
        src.Connect(osc, 0, mix, 0);
        src.Connect(shift, 0, mix, 1);
        src.Connect(mix, 0, dac, 0);
        dump = src.DumpJSON();
      }
      patcherImplementation dst(1, nullptr);
      dst.ParseJSON(dump);
      // A second for the ringing to settle, then the peak of the next block.
      for (int block = 0; block < 48000 / static_cast<int>(N); ++block)
        dst.Calculate(YSE::T_DSP);
      double peak = 0.0;
      for (int block = 0; block < 4; ++block) {
        dst.Calculate(YSE::T_DSP);
        for (unsigned int i = 0; i < N; ++i)
          peak = std::max(peak, static_cast<double>(std::fabs(dst.output[0].getPtr()[i])));
      }
      return peak;
    };
    // At the center the copy is the input inverted: the mix cancels.
    CHECK(render("1000") < 0.01);
    // An octave above, the copy lags by about 320 degrees (Q 2): the mix
    // passes it at nearly twice the level of the input, never more.
    const double octave = render("2000");
    CHECK(octave > 1.5);
    CHECK(octave <= 2.0 + 1e-3);
  }

  TEST_CASE("~phaseshift: numbers, messages and blocks on the audio thread allocate nothing "
            "(#1098)") {
    BufferSink out;
    dPhaseshift obj;
    Wire(obj, out);
    buffer in(N);
    buffer frequency(N);
    buffer q(N);
    in = 0.5f;
    frequency = 700.f;
    q = 7.f;
    const std::string list = "600 9";
    const std::string number = "900";
    const std::string clear = "clear";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(1)->SetFloat(500.f, YSE::T_DSP);
      obj.GetInlet(1)->SetInt(600, YSE::T_DSP);
      obj.GetInlet(2)->SetFloat(5.f, YSE::T_DSP);
      obj.GetInlet(2)->SetInt(8, YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(number, YSE::T_DSP);
      obj.GetInlet(0)->SetList(list, YSE::T_DSP);
      obj.GetInlet(1)->SetList(clear, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.GetInlet(1)->SetBuffer(&frequency, YSE::T_DSP);
      obj.GetInlet(2)->SetBuffer(&q, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

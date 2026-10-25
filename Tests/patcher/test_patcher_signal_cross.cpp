// Tests for the ~cross patcher object (patcher/filters/dCross), issue #1095.
// The filtering is DSP::crossover's and is tested in
// Tests/dsp/test_crossover.cpp; these show that the signals, numbers,
// messages, clear and the argument reach it and that both outlets carry their
// band, alone and in a rendered patch after a JSON round trip.

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
#include "patcher/filters/dCross.h"
#include "dsp/filters/crossover.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::crossover;
using YSE::DSP::phasor;
using YSE::PATCHER::dCross;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  struct Sinks {
    std::array<BufferSink, 2> out;
  };

  void Wire(dCross& obj, Sinks& s) {
    for (unsigned int i = 0; i < 2; ++i) {
      obj.ConnectOutlet(s.out[i].GetInlet(0), i);
      s.out[i].ConnectInlet(obj.GetOutlet(i), 0);
    }
  }

  // Both outlets against the bands of `reference`, which has just processed
  // the same block.
  void CheckOutlets(Sinks& s, crossover& reference) {
    REQUIRE(s.out[0].received != nullptr);
    REQUIRE(s.out[1].received != nullptr);
    CHECK(TestHelpers::buffersNearlyEqual(*s.out[0].received, reference.lowpass(), 0.f));
    CHECK(TestHelpers::buffersNearlyEqual(*s.out[1].received, reference.highpass(), 0.f));
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

  TEST_CASE("~cross: registry and shape (#1095)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_CROSS);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~cross");
    CHECK(obj->GetInputs() == 2);
    CHECK(obj->GetOutputs() == 2);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->IsDSPInput(1));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(obj->OutputDataType(1) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_CROSS));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::FILTER);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~cross: signals, numbers, messages and clear against the DSP class (#1095, "
            "#1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    Sinks out;
    dCross obj;
    Wire(obj, out);
    crossover reference;
    buffer objIn(N);
    buffer refIn(N);
    unsigned int start = 0;

    const auto step = [&](buffer* cutoff = nullptr) {
      obj.ResetDSP(); // as the patcher does before every block
      Fill(objIn, start);
      Fill(refIn, start);
      start += N;
      obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
      if (cutoff != nullptr) obj.GetInlet(1)->SetBuffer(cutoff, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      buffer c(N);
      c = reference.cutoff();
      if (cutoff != nullptr) c = *cutoff;
      reference(refIn, c);
      CheckOutlets(out, reference);
    };

    // Default: 1000 Hz, both bands carry signal.
    step();
    CHECK(reference.cutoff() == 1000.f);
    CHECK(out.out[0].received->getPtr()[N - 1] != 0.f);
    CHECK(out.out[1].received->getPtr()[N - 1] != 0.f);

    // Numbers and ints in the right inlet.
    obj.GetInlet(1)->SetFloat(300.f, YSE::T_GUI);
    reference.cutoff(300.f);
    step();
    obj.GetInlet(1)->SetInt(5000, YSE::T_GUI);
    reference.cutoff(5000.f);
    step();

    // A number as text in any inlet (a list through a receive).
    obj.GetInlet(0)->SetMessage("2500", YSE::T_GUI);
    reference.cutoff(2500.f);
    step();
    obj.GetInlet(1)->SetList("700", YSE::T_GUI);
    reference.cutoff(700.f);
    step();

    // clear in either inlet resets the state before the next block.
    obj.GetInlet(1)->SetMessage("clear", YSE::T_GUI);
    reference.clear();
    step();
    obj.GetInlet(0)->SetList("clear", YSE::T_GUI);
    reference.clear();
    step();

    // A signal wins over the number and is read per sample.
    buffer cutoff(N);
    for (unsigned int i = 0; i < N; ++i)
      cutoff.getPtr()[i] = 400.f + 20.f * static_cast<float>(i);
    step(&cutoff);
    CHECK(reference.cutoff() == cutoff.getPtr()[N - 1]);

    // Back to unconnected: the last number again.
    reference.cutoff(700.f);
    step();

    // Unknown or malformed messages and non-finite numbers: ignored.
    obj.SetMessage("cutoff 300", 0.f);
    obj.SetMessage("abc", 0.f);
    obj.SetMessage("nan", 0.f);
    obj.SetMessage("", 0.f);
    obj.GetInlet(1)->SetFloat(std::nanf(""), YSE::T_GUI);
    step();
    step();

    // A cutoff of 0: silence below, the input above.
    obj.GetInlet(1)->SetFloat(0.f, YSE::T_GUI);
    reference.cutoff(0.f);
    step();
    for (unsigned int i = 0; i < N; ++i) {
      CAPTURE(i);
      CHECK(out.out[0].received->getPtr()[i] == 0.f);
      CHECK(out.out[1].received->getPtr()[i] == objIn.getPtr()[i]);
    }

    // Unconnected input: silence on both outlets.
    Sinks idleOut;
    dCross idle;
    Wire(idle, idleOut);
    idle.GetInlet(1)->SetFloat(1000.f, YSE::T_GUI);
    idle.Calculate(YSE::T_DSP);
    REQUIRE(idleOut.out[0].received != nullptr);
    REQUIRE(idleOut.out[1].received != nullptr);
    CHECK(idleOut.out[0].received->getPtr()[N - 1] == 0.f);
    CHECK(idleOut.out[1].received->getPtr()[N - 1] == 0.f);
  }

  TEST_CASE("~cross: the argument is the cutoff (#1095)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto check = [](const std::string& args, float cutoff) {
      CAPTURE(args);
      Sinks out;
      dCross obj;
      Wire(obj, out);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      crossover reference;
      reference.cutoff(cutoff);
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

    check("1500", 1500.f);
    check("80", 80.f);
    // A second number is ignored; a word before the number is skipped.
    check("800 0.7", 800.f);
    check("x 300", 300.f);
    check("", 1000.f);
    check("abc", 1000.f);
  }

  TEST_CASE("~cross: in a rendered patch, both bands of a ~phasor with numbers and commands "
            "from receives, summing back flat, after a JSON round trip (#1095)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* split = src.CreateObject(YSE::OBJ::D_CROSS, "1200");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1095");
      YSE::pHandle* freq = src.CreateObject(YSE::OBJ::G_RECEIVE, "freq1095");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(split != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(freq != nullptr);
      REQUIRE(dac != nullptr);
      REQUIRE(dac->GetInputs() == 2);
      src.Connect(ramp, 0, split, 0);
      src.Connect(cmd, 0, split, 0);
      src.Connect(freq, 0, split, 1);
      src.Connect(split, 0, dac, 0);
      src.Connect(split, 1, dac, 1);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    phasor rampRef;
    crossover reference;
    reference.cutoff(1200.f);
    for (int block = 0; block < 10; ++block) {
      CAPTURE(block);
      if (block == 2) {
        REQUIRE(dst.PassData(300.f, "freq1095", YSE::T_GUI));
        reference.cutoff(300.f);
      }
      if (block == 4) {
        REQUIRE(dst.PassData("4000", "cmd1095", YSE::T_GUI));
        reference.cutoff(4000.f);
      }
      if (block == 6) {
        REQUIRE(dst.PassData("clear", "cmd1095", YSE::T_GUI));
        reference.clear();
      }
      if (block == 8) {
        REQUIRE(dst.PassData(0.f, "freq1095", YSE::T_GUI));
        reference.cutoff(0.f);
      }
      dst.Calculate(YSE::T_DSP);
      buffer& ramp = rampRef(375.f, N);
      reference(ramp);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference.lowpass(), 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[1], reference.highpass(), 0.f));
      // From block 6 on the state starts at 0 and the ramp's first sample is
      // 0 (6 * 128 mod 128), so both bands start at 0.
      if (block == 6) {
        CHECK(dst.output[0].getPtr()[0] == 0.f);
        CHECK(dst.output[1].getPtr()[0] == 0.f);
      }
      // A cutoff of 0: the lowpass is silent, the highpass the ramp itself.
      if (block >= 8) {
        for (unsigned int i = 0; i < N; ++i) {
          CAPTURE(i);
          CHECK(dst.output[0].getPtr()[i] == 0.f);
        }
        CHECK(TestHelpers::buffersNearlyEqual(dst.output[1], ramp, 0.f));
      }
    }
  }

  TEST_CASE("~cross: numbers, messages and blocks on the audio thread allocate nothing "
            "(#1095)") {
    Sinks out;
    dCross obj;
    Wire(obj, out);
    buffer in(N);
    buffer cutoff(N);
    in = 0.5f;
    cutoff = 700.f;
    const std::string setCutoff = "900";
    const std::string clear = "clear";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(1)->SetFloat(500.f, YSE::T_DSP);
      obj.GetInlet(1)->SetInt(600, YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(setCutoff, YSE::T_DSP);
      obj.GetInlet(1)->SetList(setCutoff, YSE::T_DSP);
      obj.GetInlet(0)->SetList(clear, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.GetInlet(1)->SetBuffer(&cutoff, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

// Tests for the ~lores patcher object (patcher/filters/dLores), issue #1087.
// The filtering is DSP::lores's and is tested in Tests/dsp/test_lores.cpp;
// these show that the signals, numbers, keyword messages, clear and arguments
// reach it, alone and in a rendered patch after a JSON round trip.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/filters/dLores.h"
#include "dsp/filters/lores.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::lores;
using YSE::DSP::phasor;
using YSE::PATCHER::dLores;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dLores& obj, BufferSink& out) {
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

  TEST_CASE("~lores: registry and shape (#1087)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_LORES);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~lores");
    CHECK(obj->GetInputs() == 3);
    CHECK(obj->GetOutputs() == 1);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->IsDSPInput(1));
    CHECK(obj->IsDSPInput(2));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_LORES));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::FILTER);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~lores: signals, numbers, keywords and clear against the DSP class (#1087, "
            "#1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink out;
    dLores obj;
    Wire(obj, out);
    lores reference;
    buffer objIn(N);
    buffer refIn(N);
    unsigned int start = 0;

    const auto step = [&](buffer* cutoff = nullptr, buffer* resonance = nullptr) {
      obj.ResetDSP(); // as the patcher does before every block
      Fill(objIn, start);
      Fill(refIn, start);
      start += N;
      obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
      if (cutoff != nullptr) obj.GetInlet(1)->SetBuffer(cutoff, YSE::T_GUI);
      if (resonance != nullptr) obj.GetInlet(2)->SetBuffer(resonance, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(out.received != nullptr);
      buffer c(N);
      buffer r(N);
      c = reference.cutoff();
      r = reference.resonance();
      if (cutoff != nullptr) c = *cutoff;
      if (resonance != nullptr) r = *resonance;
      CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(refIn, c, r), 0.f));
    };

    // Default: cutoff 0, nothing through.
    step();
    CHECK(out.received->getPtr()[N - 1] == 0.f);

    // Numbers in the middle and right inlets; ints too.
    obj.GetInlet(1)->SetFloat(1000.f, YSE::T_GUI);
    reference.cutoff(1000.f);
    step();
    CHECK(out.received->getPtr()[N - 1] != 0.f);
    obj.GetInlet(2)->SetFloat(0.8f, YSE::T_GUI);
    reference.resonance(0.8f);
    step();
    obj.GetInlet(1)->SetInt(3000, YSE::T_GUI);
    obj.GetInlet(2)->SetInt(0, YSE::T_GUI);
    reference.cutoff(3000.f).resonance(0.f);
    step();

    // Keyword messages in any inlet, and as a list (text through a receive).
    obj.GetInlet(0)->SetMessage("cutoff 500", YSE::T_GUI);
    reference.cutoff(500.f);
    step();
    obj.GetInlet(2)->SetMessage("resonance 0.95", YSE::T_GUI);
    reference.resonance(0.95f);
    step();
    obj.GetInlet(1)->SetList("cutoff 2500", YSE::T_GUI);
    obj.GetInlet(0)->SetList("resonance 0.5", YSE::T_GUI);
    reference.cutoff(2500.f).resonance(0.5f);
    step();

    // clear in any inlet resets the state before the next block.
    obj.GetInlet(2)->SetMessage("clear", YSE::T_GUI);
    reference.clear();
    step();
    obj.GetInlet(0)->SetList("clear", YSE::T_GUI);
    reference.clear();
    step();

    // Signals win over the numbers and are read per sample.
    buffer cutoff(N);
    buffer resonance(N);
    for (unsigned int i = 0; i < N; ++i) {
      cutoff.getPtr()[i] = 400.f + 20.f * static_cast<float>(i);
      resonance.getPtr()[i] = 0.9f * static_cast<float>(i) / static_cast<float>(N);
    }
    step(&cutoff, &resonance);
    CHECK(reference.cutoff() == cutoff.getPtr()[N - 1]);
    // Only the cutoff connected: the resonance number (0.5) again.
    reference.resonance(0.5f);
    step(&cutoff, nullptr);

    // Back to unconnected: the last numbers again.
    reference.cutoff(2500.f).resonance(0.5f);
    step();

    // Unknown or malformed messages and non-finite numbers: ignored.
    obj.SetMessage("cutoff", 0.f);
    obj.SetMessage("cutoff abc", 0.f);
    obj.SetMessage("resonance nan", 0.f);
    obj.SetMessage("freq 300", 0.f);
    obj.SetMessage("", 0.f);
    obj.GetInlet(1)->SetFloat(std::nanf(""), YSE::T_GUI);
    obj.GetInlet(2)->SetFloat(std::nanf(""), YSE::T_GUI);
    step();
    step();

    // Unconnected input: silence.
    BufferSink idleOut;
    dLores idle;
    Wire(idle, idleOut);
    idle.GetInlet(1)->SetFloat(1000.f, YSE::T_GUI);
    idle.GetInlet(2)->SetFloat(0.9f, YSE::T_GUI);
    idle.Calculate(YSE::T_DSP);
    REQUIRE(idleOut.received != nullptr);
    CHECK(idleOut.received->getPtr()[N - 1] == 0.f);
  }

  TEST_CASE("~lores: arguments are the cutoff and the resonance (#1087)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto check = [](const std::string& args, float cutoff, float resonance) {
      CAPTURE(args);
      BufferSink out;
      dLores obj;
      Wire(obj, out);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      lores reference;
      reference.cutoff(cutoff).resonance(resonance);
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

    check("1500", 1500.f, 0.f);
    check("800 0.7", 800.f, 0.7f);
    // A third number is ignored; keywords set either value.
    check("800 0.7 0.1", 800.f, 0.7f);
    check("resonance 0.9 cutoff 2000", 2000.f, 0.9f);
    check("600 resonance 0.4", 600.f, 0.4f);
    // A keyword without a number is skipped.
    check("cutoff", 0.f, 0.f);
    check("", 0.f, 0.f);
  }

  TEST_CASE("~lores: in a rendered patch, filtering a ~phasor with numbers and commands from "
            "receives, after a JSON round trip (#1087)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* filter = src.CreateObject(YSE::OBJ::D_LORES, "1000 0.6");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1087");
      YSE::pHandle* res = src.CreateObject(YSE::OBJ::G_RECEIVE, "res1087");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(filter != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(res != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, filter, 0);
      src.Connect(cmd, 0, filter, 0);
      src.Connect(res, 0, filter, 2);
      src.Connect(filter, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    phasor rampRef;
    lores reference;
    reference.cutoff(1000.f).resonance(0.6f);
    for (int block = 0; block < 10; ++block) {
      CAPTURE(block);
      if (block == 2) {
        REQUIRE(dst.PassData(0.95f, "res1087", YSE::T_GUI));
        reference.resonance(0.95f);
      }
      if (block == 4) {
        REQUIRE(dst.PassData("cutoff 4000", "cmd1087", YSE::T_GUI));
        reference.cutoff(4000.f);
      }
      if (block == 6) {
        REQUIRE(dst.PassData("clear", "cmd1087", YSE::T_GUI));
        reference.clear();
      }
      if (block == 8) {
        REQUIRE(dst.PassData("resonance 0", "res1087", YSE::T_GUI));
        REQUIRE(dst.PassData("cutoff 0", "cmd1087", YSE::T_GUI));
        reference.resonance(0.f).cutoff(0.f);
      }
      dst.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(rampRef(375.f, N)), 0.f));
      // Closed form: from block 6 on the state starts at 0 and the ramp's
      // first sample is 0 (6 * 128 mod 128), so the output starts at 0.
      if (block == 6) CHECK(dst.output[0].getPtr()[0] == 0.f);
      // A cutoff of 0 passes nothing.
      if (block >= 8) {
        for (unsigned int i = 0; i < N; ++i) {
          CAPTURE(i);
          CHECK(dst.output[0].getPtr()[i] == 0.f);
        }
      }
    }
  }

  TEST_CASE("~lores: numbers, messages and blocks on the audio thread allocate nothing "
            "(#1087)") {
    BufferSink out;
    dLores obj;
    Wire(obj, out);
    buffer in(N);
    buffer cutoff(N);
    buffer resonance(N);
    in = 0.5f;
    cutoff = 700.f;
    resonance = 0.7f;
    const std::string setCutoff = "cutoff 900";
    const std::string setResonance = "resonance 0.3";
    const std::string clear = "clear";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(1)->SetFloat(500.f, YSE::T_DSP);
      obj.GetInlet(1)->SetInt(600, YSE::T_DSP);
      obj.GetInlet(2)->SetFloat(0.5f, YSE::T_DSP);
      obj.GetInlet(2)->SetInt(1, YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(setCutoff, YSE::T_DSP);
      obj.GetInlet(2)->SetList(setResonance, YSE::T_DSP);
      obj.GetInlet(1)->SetList(clear, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.GetInlet(1)->SetBuffer(&cutoff, YSE::T_DSP);
      obj.GetInlet(2)->SetBuffer(&resonance, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

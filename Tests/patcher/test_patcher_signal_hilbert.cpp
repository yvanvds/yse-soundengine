// Tests for the ~hilbert patcher object (patcher/filters/dHilbert), issue
// #1097. The filtering is DSP::hilbert's and is tested in
// Tests/dsp/test_hilbert.cpp; these show that the signal and clear reach it
// and that both outlets carry their part, alone and in rendered patches after
// a JSON round trip: bit-exact on a ~phasor, and as the envelope of a ~cycle
// through ~cartopol.

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
#include "patcher/filters/dHilbert.h"
#include "dsp/modules/hilbert.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::hilbert;
using YSE::DSP::phasor;
using YSE::PATCHER::dHilbert;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  struct Sinks {
    std::array<BufferSink, 2> out;
  };

  void Wire(dHilbert& obj, Sinks& s) {
    for (unsigned int i = 0; i < 2; ++i) {
      obj.ConnectOutlet(s.out[i].GetInlet(0), i);
      s.out[i].ConnectInlet(obj.GetOutlet(i), 0);
    }
  }

  void CheckOutlets(Sinks& s, hilbert& reference) {
    REQUIRE(s.out[0].received != nullptr);
    REQUIRE(s.out[1].received != nullptr);
    CHECK(TestHelpers::buffersNearlyEqual(*s.out[0].received, reference.real(), 0.f));
    CHECK(TestHelpers::buffersNearlyEqual(*s.out[1].received, reference.imaginary(), 0.f));
  }

  void Fill(buffer& b, unsigned int start) {
    for (unsigned int i = 0; i < b.getLength(); ++i) {
      const auto n = static_cast<float>(start + i);
      b.getPtr()[i] = std::sin(n * 0.05f) + 0.5f * std::sin(n * 1.3f);
    }
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~hilbert: registry and shape (#1097)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_HILBERT);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~hilbert");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 2);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(obj->OutputDataType(1) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_HILBERT));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::FILTER);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 0);
  }

  TEST_CASE("~hilbert: the signal and clear against the DSP class (#1097, #1036)") {
    Sinks out;
    dHilbert obj;
    Wire(obj, out);
    hilbert reference;
    buffer objIn(N);
    buffer refIn(N);
    unsigned int start = 0;

    const auto step = [&]() {
      obj.ResetDSP(); // as the patcher does before every block
      Fill(objIn, start);
      Fill(refIn, start);
      start += N;
      obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      reference(refIn);
      CheckOutlets(out, reference);
    };

    step();
    step();
    CHECK(out.out[0].received->getPtr()[N - 1] != 0.f);
    CHECK(out.out[1].received->getPtr()[N - 1] != 0.f);

    // clear as a message and as text (a list through a receive).
    obj.GetInlet(0)->SetMessage("clear", YSE::T_GUI);
    reference.clear();
    step();
    obj.GetInlet(0)->SetList("clear", YSE::T_GUI);
    reference.clear();
    step();
    obj.SetMessage("  clear", 0.f);
    reference.clear();
    step();

    // Unknown messages and numbers: ignored.
    obj.SetMessage("abc", 0.f);
    obj.SetMessage("clearer", 0.f);
    obj.SetMessage("", 0.f);
    obj.GetInlet(0)->SetMessage("2", YSE::T_GUI);
    step();
    step();

    // Unconnected input: silence on both outlets.
    Sinks idleOut;
    dHilbert idle;
    Wire(idle, idleOut);
    idle.Calculate(YSE::T_DSP);
    REQUIRE(idleOut.out[0].received != nullptr);
    REQUIRE(idleOut.out[1].received != nullptr);
    for (unsigned int i = 0; i < N; ++i) {
      CAPTURE(i);
      CHECK(idleOut.out[0].received->getPtr()[i] == 0.f);
      CHECK(idleOut.out[1].received->getPtr()[i] == 0.f);
    }
  }

  TEST_CASE("~hilbert: in a rendered patch, both parts of a ~phasor with clear from a "
            "receive, after a JSON round trip (#1097)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* quad = src.CreateObject(YSE::OBJ::D_HILBERT, "");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1097");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(quad != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(dac != nullptr);
      REQUIRE(dac->GetInputs() == 2);
      src.Connect(ramp, 0, quad, 0);
      src.Connect(cmd, 0, quad, 0);
      src.Connect(quad, 0, dac, 0);
      src.Connect(quad, 1, dac, 1);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    phasor rampRef;
    hilbert reference;
    for (int block = 0; block < 8; ++block) {
      CAPTURE(block);
      if (block == 4) {
        REQUIRE(dst.PassData("clear", "cmd1097", YSE::T_GUI));
        reference.clear();
      }
      dst.Calculate(YSE::T_DSP);
      buffer& ramp = rampRef(375.f, N);
      reference(ramp);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference.real(), 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[1], reference.imaginary(), 0.f));
      // From block 4 on the state starts at 0 and the ramp's first sample is
      // 0 (4 * 128 mod 128), so both parts start at 0.
      if (block == 4) {
        CHECK(dst.output[0].getPtr()[0] == 0.f);
        CHECK(dst.output[1].getPtr()[0] == 0.f);
      }
    }
  }

  TEST_CASE("~hilbert: ~cycle -> ~hilbert -> ~cartopol gives a sine's envelope, after a JSON "
            "round trip (#1097)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* osc = src.CreateObject(YSE::OBJ::D_CYCLE, "1000");
      YSE::pHandle* quad = src.CreateObject(YSE::OBJ::D_HILBERT, "");
      YSE::pHandle* polar = src.CreateObject(YSE::OBJ::D_CARTOPOL, "");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(osc != nullptr);
      REQUIRE(quad != nullptr);
      REQUIRE(polar != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(osc, 0, quad, 0);
      src.Connect(quad, 0, polar, 0);
      src.Connect(quad, 1, polar, 1);
      src.Connect(polar, 0, dac, 0);
      src.Connect(polar, 1, dac, 1);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    float previousPhase = 0.f;
    unsigned int advancing = 0;
    unsigned int counted = 0;
    for (int block = 0; block < 40; ++block) {
      CAPTURE(block);
      dst.Calculate(YSE::T_DSP);
      if (block < 20) continue; // let the filter settle (~0.05 s)
      for (unsigned int i = 0; i < N; ++i) {
        CAPTURE(i);
        // At 1 kHz the parts are 90 degrees apart within 0.3 degrees: a
        // steady magnitude of 1.
        CHECK(dst.output[0].getPtr()[i] == doctest::Approx(1.0).epsilon(0.01));
        // The imaginary part lags, so the phase advances (a positive
        // frequency) by 2 pi 1000 / 48000 per sample, wrapping at pi.
        const float phase = dst.output[1].getPtr()[i];
        if (counted > 0) {
          float d = phase - previousPhase;
          if (d < -3.14159265f) d += 6.28318531f;
          if (d > 0.12f && d < 0.14f) advancing++;
        }
        previousPhase = phase;
        counted++;
      }
    }
    CHECK(advancing == counted - 1);
  }

  TEST_CASE("~hilbert: messages and blocks on the audio thread allocate nothing (#1097)") {
    Sinks out;
    dHilbert obj;
    Wire(obj, out);
    buffer in(N);
    in = 0.5f;
    const std::string clear = "clear";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetMessage(clear, YSE::T_DSP);
      obj.GetInlet(0)->SetList(clear, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

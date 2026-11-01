// Tests for the ~spike patcher object (patcher/analysis/dSpike), issue #1123.
// The intervals are DSP::spikeDetector's and are tested in
// Tests/dsp/test_spike_detector.cpp; these show that the signal, the numbers,
// the bang and the refractory period reach it, that the interval goes out
// only through the armed outlet (#1037), and that it works in a rendered
// ~phasor -> ~> -> ~spike -> .f patch after a JSON round trip.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/analysis/dSpike.h"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::FloatSink;
using TestHelpers::ScopedSampleRate;
using TestHelpers::Wire;
using YSE::DSP::buffer;
using YSE::PATCHER::dSpike;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // ~spike with a float sink on its outlet. Block() runs one block and the
  // flush the patcher runs after its traversal, and says whether a number
  // went out.
  struct Rig {
    FloatSink out;
    dSpike spike;

    Rig() {
      Wire(spike, 0, out);
    }
    bool Block() {
      out.gotFloat = false;
      spike.Calculate(YSE::T_DSP);
      spike.FlushArmedOutlets(YSE::T_GUI);
      return out.gotFloat;
    }
    bool Block(buffer& in) {
      out.gotFloat = false;
      spike.ResetDSP();
      spike.GetInlet(0)->SetBuffer(&in, YSE::T_DSP); // the last input: runs CALC()
      spike.FlushArmedOutlets(YSE::T_GUI);
      return out.gotFloat;
    }
  };

  // A block that is zero but for a one-sample pulse at each given index.
  void Pulses(buffer& b, std::initializer_list<unsigned int> at) {
    b = 0.f;
    for (unsigned int i : at)
      b.getPtr()[i] = 1.f;
  }

  float GuiFloat(YSE::pHandle* h) {
    return std::stof(h->GetGuiValue());
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~spike: registry and shape (#1123)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_SPIKE);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~spike");
    CHECK(obj->GetInputs() == 2);
    CHECK(obj->GetOutputs() == 1);
    CHECK(obj->IsDSPInput(0));
    CHECK_FALSE(obj->IsDSPInput(1));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_SPIKE));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::ANALYSIS);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~spike: CALC() only arms; the interval goes out on the flush (#1123, #1037)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    buffer in(N);
    Pulses(in, {10});
    rig.spike.ResetDSP();
    rig.spike.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
    CHECK_FALSE(rig.out.gotFloat);
    CHECK(rig.spike.GetOutlet(0)->Armed());
    rig.spike.FlushArmedOutlets(YSE::T_GUI);
    CHECK(rig.out.gotFloat);
    CHECK(rig.out.received == doctest::Approx(10.f));
    CHECK_FALSE(rig.spike.GetOutlet(0)->Armed());
  }

  TEST_CASE("~spike: the ms between rises, across blocks; the last of several (#1123, #1037)") {
    ScopedSampleRate rate(1000); // one sample per millisecond
    Rig rig;
    buffer in(N);
    Pulses(in, {N / 4});
    REQUIRE(rig.Block(in));
    CHECK(rig.out.received == doctest::Approx(N / 4.f)); // the first: since the start

    in = 0.f;
    CHECK_FALSE(rig.Block(in)); // no rise: nothing out

    Pulses(in, {N / 8});
    REQUIRE(rig.Block(in));
    CHECK(rig.out.received == doctest::Approx(2.f * N - N / 4.f + N / 8.f));

    Pulses(in, {N / 4, N / 2, N - 3}); // three: the slot keeps the last
    REQUIRE(rig.Block(in));
    CHECK(rig.out.received == doctest::Approx(N / 2.f - 3.f));
  }

  TEST_CASE("~spike: the refractory period from the argument and the right inlet (#1123)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.spike.SetParams("50");
    buffer in(N);
    Pulses(in, {0, 30, 60, 90, 120}); // every 30 ms: reported at 0, 60, 120
    REQUIRE(rig.Block(in));
    CHECK(rig.out.received == doctest::Approx(60.f));

    rig.spike.GetInlet(1)->SetFloat(20.f, YSE::T_GUI); // every rise again
    Pulses(in, {22});
    REQUIRE(rig.Block(in));
    CHECK(rig.out.received == doctest::Approx(N - 120.f + 22.f));

    rig.spike.GetInlet(1)->SetInt(200, YSE::T_GUI);
    Pulses(in, {N / 2});
    CHECK_FALSE(rig.Block(in)); // N + N/2 - 22 < 200 ms
    in = 0.f;
    rig.Block(in);
    Pulses(in, {N / 4});
    REQUIRE(rig.Block(in)); // 3N + N/4 - 22 >= 200
    CHECK(rig.out.received == doctest::Approx(3.f * N + N / 4.f - 22.f));

    rig.spike.GetInlet(1)->SetFloat(-5.f, YSE::T_GUI); // negative is 0
    Pulses(in, {1});
    REQUIRE(rig.Block(in));
    CHECK(rig.out.received == doctest::Approx(N - N / 4.f + 1.f));
  }

  TEST_CASE("~spike: a bang restarts the refractory period (#1123)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.spike.GetInlet(1)->SetFloat(N / 2.f, YSE::T_GUI);
    buffer in(N);
    Pulses(in, {0});
    REQUIRE(rig.Block(in));
    in = 0.f;
    rig.Block(in); // the period ran out

    rig.spike.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(rig.out.gotFloat); // a bang sends nothing
    Pulses(in, {N / 4}); // inside the restarted period
    CHECK_FALSE(rig.Block(in));
    Pulses(in, {N / 4}); // past it, N + N/4 after the bang
    REQUIRE(rig.Block(in));
    CHECK(rig.out.received == doctest::Approx(3.f * N + N / 4.f)); // since the last reported

    // Without the bang that first rise would have been reported.
    Rig plain;
    plain.spike.GetInlet(1)->SetFloat(N / 2.f, YSE::T_GUI);
    Pulses(in, {0});
    REQUIRE(plain.Block(in));
    in = 0.f;
    plain.Block(in);
    Pulses(in, {N / 4});
    CHECK(plain.Block(in));
  }

  TEST_CASE("~spike: a number turning non-zero is a rise; a signal wins (#1123, #1036)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    CHECK_FALSE(rig.Block()); // unconnected and no number: reads 0
    rig.spike.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    REQUIRE(rig.Block()); // a rise on the first sample of the second block
    CHECK(rig.out.received == doctest::Approx(static_cast<float>(N)));
    rig.spike.GetInlet(0)->SetInt(0, YSE::T_GUI);
    CHECK_FALSE(rig.Block());
    rig.spike.GetInlet(0)->SetInt(2, YSE::T_GUI);
    REQUIRE(rig.Block());
    CHECK(rig.out.received == doctest::Approx(2.f * N));

    Rig wins;
    wins.spike.GetInlet(0)->SetFloat(5.f, YSE::T_GUI);
    buffer in(N);
    in = 0.f;
    CHECK_FALSE(wins.Block(in));
  }

  TEST_CASE("~spike: the refractory period survives a DumpJSON / ParseJSON round trip "
            "(#1123)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* a = src.CreateObject(YSE::OBJ::D_SPIKE, "12.5");
      REQUIRE(a != nullptr);
      YSE::pHandle* b = src.CreateObject(YSE::OBJ::D_SPIKE, "");
      REQUIRE(b != nullptr);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* h = dst.GetHandleFromID(0);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~spike");
    CHECK(h->GetParams() == "12.5");
  }

  TEST_CASE("~spike: in a rendered patch, a gated ~phasor gives its period, after a JSON round "
            "trip (#1123)") {
    ScopedSampleRate rate(48000);
    std::string dump;
    unsigned int everyID = 0;
    unsigned int heldID = 0;
    {
      patcherImplementation src(1, nullptr);
      // 375 Hz at 48 kHz: one cycle per block, above 0.5 for its second half.
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* gate = src.CreateObject(YSE::OBJ::D_GREATER, "0.5");
      YSE::pHandle* every = src.CreateObject(YSE::OBJ::D_SPIKE, "");
      // 4 ms is 192 samples: every other cycle
      YSE::pHandle* held = src.CreateObject(YSE::OBJ::D_SPIKE, "4");
      YSE::pHandle* everyOut = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      YSE::pHandle* heldOut = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(gate != nullptr);
      REQUIRE(every != nullptr);
      REQUIRE(held != nullptr);
      REQUIRE(everyOut != nullptr);
      REQUIRE(heldOut != nullptr);
      src.Connect(ramp, 0, gate, 0);
      src.Connect(gate, 0, every, 0);
      src.Connect(gate, 0, held, 0);
      src.Connect(every, 0, everyOut, 0);
      src.Connect(held, 0, heldOut, 0);
      everyID = everyOut->GetID();
      heldID = heldOut->GetID();
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* everyOut = dst.GetHandleFromID(everyID);
    YSE::pHandle* heldOut = dst.GetHandleFromID(heldID);
    REQUIRE(everyOut != nullptr);
    REQUIRE(heldOut != nullptr);

    const float block = N * 1000.f / 48000.f; // 2.666... ms
    dst.Calculate(YSE::T_DSP);
    dst.Calculate(YSE::T_DSP);
    for (int i = 0; i < 6; ++i) {
      CAPTURE(i);
      dst.Calculate(YSE::T_DSP);
      CHECK(GuiFloat(everyOut) == doctest::Approx(block).epsilon(1e-4));
      CHECK(GuiFloat(heldOut) == doctest::Approx(2.f * block).epsilon(1e-4));
    }
  }

  TEST_CASE("~spike: numbers, a bang, blocks and the flush on the audio thread allocate nothing "
            "(#1123)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    buffer in(N);
    Pulses(in, {0, N / 2});
    rig.Block(in);
    {
      TestHelpers::ProbeScope probe;
      rig.spike.GetInlet(0)->SetFloat(0.3f, YSE::T_DSP);
      rig.spike.GetInlet(0)->SetBang(YSE::T_DSP);
      rig.spike.GetInlet(1)->SetFloat(1.f, YSE::T_DSP);
      rig.spike.GetInlet(1)->SetInt(2, YSE::T_DSP);
      rig.Block(in);
      rig.Block(in);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(rig.out.received == doctest::Approx(N / 2.f));
  }

} // TEST_SUITE("patcher")

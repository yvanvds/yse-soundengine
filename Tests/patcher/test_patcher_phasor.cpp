// Tests for the ~phasor patcher object (patcher/generatorObjects/dPhasor),
// issue #1045. The ramp itself is DSP::phasor's and is tested in
// Tests/dsp/test_phasor.cpp; these show that pins, messages and parameters
// reach it.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/generatorObjects/dPhasor.h"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::PATCHER::dPhasor;
using YSE::PATCHER::patcherImplementation;

namespace {
  // 100 samples a cycle at whatever rate the test binary runs.
  float HundredSampleFrequency() {
    return static_cast<float>(YSE::SAMPLERATE) / 100.f;
  }

  void Wire(dPhasor& ramp, BufferSink& sink) {
    ramp.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(ramp.GetOutlet(0), 0);
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("phasor: registry and shape (#1045)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_PHASOR);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~phasor");
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::D_PHASOR));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::OSC);
    CHECK(obj->IsDSPObject());
  }

  TEST_CASE("phasor: the left float sets the frequency (#1045)") {
    dPhasor ramp;
    BufferSink sink;
    Wire(ramp, sink);
    ramp.GetInlet(0)->SetFloat(HundredSampleFrequency(), YSE::T_GUI);
    ramp.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CHECK(sink.received->getPtr()[0] == doctest::Approx(0.f));
    CHECK(sink.received->getPtr()[50] == doctest::Approx(0.5f).epsilon(1e-5));
  }

  TEST_CASE("phasor: a connected frequency signal wins over the float (#1045, #1036)") {
    dPhasor ramp;
    BufferSink sink;
    Wire(ramp, sink);
    ramp.GetInlet(0)->SetFloat(0.f, YSE::T_GUI);
    YSE::DSP::buffer frequency(YSE::STANDARD_BUFFERSIZE);
    frequency = HundredSampleFrequency();
    ramp.GetInlet(0)->SetBuffer(&frequency, YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CHECK(sink.received->getPtr()[50] == doctest::Approx(0.5f).epsilon(1e-5));
  }

  TEST_CASE("phasor: the right inlet jumps the phase, reset returns to phaseoffset (#1045)") {
    dPhasor ramp;
    BufferSink sink;
    Wire(ramp, sink);
    ramp.SetParams("0 0.25"); // frequency 0, phaseoffset 0.25

    ramp.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CHECK(sink.received->getPtr()[0] == doctest::Approx(0.25f)); // starts at phaseoffset

    ramp.GetInlet(1)->SetFloat(0.75f, YSE::T_GUI);
    ramp.Calculate(YSE::T_DSP);
    CHECK(sink.received->getPtr()[0] == doctest::Approx(0.75f));

    ramp.GetInlet(0)->SetMessage("reset", YSE::T_GUI);
    ramp.Calculate(YSE::T_DSP);
    CHECK(sink.received->getPtr()[0] == doctest::Approx(0.25f)); // jump left phaseoffset alone

    ramp.GetInlet(0)->SetMessage("phaseoffset 0.5", YSE::T_GUI);
    ramp.GetInlet(0)->SetMessage("reset", YSE::T_GUI);
    ramp.Calculate(YSE::T_DSP);
    CHECK(sink.received->getPtr()[0] == doctest::Approx(0.5f));
  }

  TEST_CASE("phasor: parameters survive a DumpJSON / ParseJSON round trip (#1045)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "2.5 0.125");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      src.Connect(ramp, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    dst.Calculate(YSE::T_DSP);
    // phaseoffset 0.125, then 2.5 Hz per sample.
    const float step = 2.5f / static_cast<float>(YSE::SAMPLERATE);
    CHECK(dst.output[0].getPtr()[0] == doctest::Approx(0.125f));
    CHECK(dst.output[0].getPtr()[10] == doctest::Approx(0.125f + 10.f * step));
  }

  TEST_CASE("phasor: in a patch, unconnected, it renders its creation frequency (#1045)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* ramp =
        p.CreateObject(YSE::OBJ::D_PHASOR, std::to_string(HundredSampleFrequency()));
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC);
    p.Connect(ramp, 0, dac, 0);
    CHECK(ramp->GetConnections(0) == 1);
  }

  TEST_CASE("phasor: syncupdate holds a new frequency until the wrap, a bang takes it now; an "
            "interval without a clock holds (#1278)") {
    TestHelpers::ScopedSampleRate rate(48000);
    dPhasor ramp;
    BufferSink sink;
    Wire(ramp, sink);
    // 375 Hz is 1/128 of a cycle a sample at 48 kHz, 750 Hz 1/64: exact.
    ramp.SetParams("375 0.5 0 0 1"); // frequency, phaseoffset, periodbeats, lock, syncupdate
    ramp.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CHECK(sink.received->getPtr()[64] == 0.f); // wraps half way, ends at 0.5

    ramp.GetInlet(0)->SetFloat(750.f, YSE::T_GUI);
    ramp.Calculate(YSE::T_DSP);
    CHECK(sink.received->getPtr()[1] - sink.received->getPtr()[0] ==
          doctest::Approx(1.f / 128.f)); // still 375 Hz
    CHECK(sink.received->getPtr()[65] == doctest::Approx(1.f / 64.f)); // 750 from the wrap

    ramp.GetInlet(0)->SetFloat(375.f, YSE::T_GUI);
    ramp.GetInlet(0)->SetBang(YSE::T_GUI); // take it now
    ramp.Calculate(YSE::T_DSP);
    CHECK(sink.received->getPtr()[1] - sink.received->getPtr()[0] == doctest::Approx(1.f / 128.f));

    // A note value with no transport: nothing to follow, so the ramp holds.
    ramp.GetInlet(0)->SetMessage("4n", YSE::T_GUI);
    ramp.Calculate(YSE::T_DSP);
    CHECK(sink.received->getPtr()[0] == sink.received->getPtr()[YSE::STANDARD_BUFFERSIZE - 1]);
    // A number is Hz again.
    ramp.GetInlet(0)->SetFloat(375.f, YSE::T_GUI);
    ramp.Calculate(YSE::T_DSP);
    CHECK(sink.received->getPtr()[1] - sink.received->getPtr()[0] == doctest::Approx(1.f / 128.f));
  }

  TEST_CASE("phasor: messages and blocks on the audio thread allocate nothing (#1045)") {
    dPhasor ramp;
    BufferSink sink;
    Wire(ramp, sink);
    ramp.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      ramp.GetInlet(0)->SetFloat(440.f, YSE::T_DSP);
      ramp.GetInlet(1)->SetFloat(0.5f, YSE::T_DSP);
      ramp.GetInlet(0)->SetMessage("reset", YSE::T_DSP);
      ramp.GetInlet(0)->SetMessage("8nt", YSE::T_DSP);
      ramp.GetInlet(0)->SetMessage("960 ticks", YSE::T_DSP);
      ramp.GetInlet(0)->SetMessage("lock 1", YSE::T_DSP);
      ramp.GetInlet(0)->SetMessage("syncupdate 1", YSE::T_DSP);
      ramp.GetInlet(0)->SetBang(YSE::T_DSP);
      ramp.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

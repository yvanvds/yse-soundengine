// Tests for the ~saw, ~rect and ~tri patcher objects
// (patcher/generatorObjects/dSaw, dRect, dTri), issue #1055. The waveforms
// are DSP::blSaw / blRect / blTri's and are tested in
// Tests/dsp/test_band_limited_oscillators.cpp; these show that numbers,
// signals, the sync inlet, synctrig and the arguments reach them, alone and
// in a rendered patch after a JSON round trip.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/generatorObjects/dSaw.h"
#include "patcher/generatorObjects/dRect.h"
#include "patcher/generatorObjects/dTri.h"
#include "dsp/generators/bandLimitedOscillators.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::blRect;
using YSE::DSP::blSaw;
using YSE::DSP::blTri;
using YSE::DSP::buffer;
using YSE::PATCHER::dRect;
using YSE::PATCHER::dSaw;
using YSE::PATCHER::dTri;
using YSE::PATCHER::patcherImplementation;

namespace {
  template <class Object> void Wire(Object& osc, BufferSink& sink) {
    osc.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(osc.GetOutlet(0), 0);
  }

  void CheckShape(const char* name, unsigned int inputs, unsigned int params) {
    CAPTURE(name);
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* h = p.CreateObject(name);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == name);
    CHECK(h->GetInputs() == inputs);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    for (unsigned int i = 0; i < inputs; ++i)
      CHECK(h->IsDSPInput(i));

    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(name));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::OSC);
    CHECK(obj->IsDSPObject());
    CHECK(obj->GetParamCount() == params);
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~saw ~rect ~tri: registry and shape (#1055)") {
    CheckShape(YSE::OBJ::D_SAW, 2, 1);
    CheckShape(YSE::OBJ::D_RECT, 3, 2);
    CheckShape(YSE::OBJ::D_TRI, 3, 2);
  }

  TEST_CASE("~saw: band-limited, 440 Hz by default, numbers set the frequency (#1055)") {
    TestHelpers::ScopedSampleRate rate(44100);
    dSaw saw;
    BufferSink sink;
    Wire(saw, sink);
    blSaw reference;
    saw.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(440.f), 0.f));

    saw.GetInlet(0)->SetFloat(3000.f, YSE::T_GUI);
    saw.Calculate(YSE::T_DSP);
    CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(3000.f), 0.f));

    saw.GetInlet(0)->SetInt(100, YSE::T_GUI);
    saw.Calculate(YSE::T_DSP);
    CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(100.f), 0.f));
  }

  TEST_CASE("~saw: a frequency signal wins; sync and synctrig (#1055, #1036)") {
    TestHelpers::ScopedSampleRate rate(44100);
    dSaw saw;
    BufferSink sink;
    Wire(saw, sink);
    buffer frequency(YSE::STANDARD_BUFFERSIZE);
    frequency = 1200.f;
    blSaw reference;
    saw.GetInlet(0)->SetBuffer(&frequency, YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(frequency), 0.f));

    buffer sync(YSE::STANDARD_BUFFERSIZE);
    sync = 0.f;
    for (unsigned int i = 40; i < sync.getLength(); ++i)
      sync.getPtr()[i] = 0.4f;
    // 0.4 does not cross the default 0.5: no reset. T_GUI only stores the
    // buffer (a T_DSP delivery computes at once); the inlet keeps the
    // frequency signal from the block before.
    saw.GetInlet(1)->SetBuffer(&sync, YSE::T_GUI);
    saw.Calculate(YSE::T_DSP);
    CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(frequency, sync), 0.f));

    // With synctrig 0.3 it does, and the cycle restarts at sample 40.
    saw.GetInlet(0)->SetMessage("synctrig 0.3", YSE::T_GUI);
    reference.syncThreshold(0.3f);
    sync = 0.f;
    for (unsigned int i = 40; i < sync.getLength(); ++i)
      sync.getPtr()[i] = 0.4f;
    saw.GetInlet(1)->SetBuffer(&sync, YSE::T_GUI);
    saw.Calculate(YSE::T_DSP);
    buffer& expected = reference(frequency, sync);
    CHECK(TestHelpers::buffersNearlyEqual(*sink.received, expected, 0.f));
    blSaw fresh;
    buffer& start = fresh(1200.f);
    CHECK(sink.received->getPtr()[40] == doctest::Approx(start.getPtr()[0]));
    CHECK(sink.received->getPtr()[41] == doctest::Approx(start.getPtr()[1]));
  }

  TEST_CASE("~rect: arguments, width numbers and a width signal (#1055)") {
    TestHelpers::ScopedSampleRate rate(44100);
    dRect rect;
    BufferSink sink;
    Wire(rect, sink);
    rect.SetParams("500 0.25");
    blRect reference;
    reference.width(0.25f);
    rect.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(500.f), 0.f));

    rect.GetInlet(1)->SetFloat(0.75f, YSE::T_GUI);
    reference.width(0.75f);
    rect.Calculate(YSE::T_DSP);
    CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(500.f), 0.f));

    rect.GetInlet(1)->SetInt(0, YSE::T_GUI);
    reference.width(0.f);
    rect.Calculate(YSE::T_DSP);
    CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(500.f), 0.f));

    buffer width(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < width.getLength(); ++i)
      width.getPtr()[i] = 0.1f + 0.8f * static_cast<float>(i) / 128.f;
    buffer frequency(YSE::STANDARD_BUFFERSIZE);
    frequency = 500.f;
    rect.GetInlet(1)->SetBuffer(&width, YSE::T_GUI);
    rect.Calculate(YSE::T_DSP);
    CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(frequency, width), 0.f));
  }

  TEST_CASE("~tri: arguments, duty numbers, a duty signal and sync (#1055)") {
    TestHelpers::ScopedSampleRate rate(44100);
    dTri tri;
    BufferSink sink;
    Wire(tri, sink);
    tri.SetParams("300 0.2");
    blTri reference;
    reference.duty(0.2f);
    tri.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(300.f), 0.f));

    tri.GetInlet(1)->SetFloat(0.9f, YSE::T_GUI);
    reference.duty(0.9f);
    tri.GetInlet(0)->SetInt(600, YSE::T_GUI);
    tri.Calculate(YSE::T_DSP);
    CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(600.f), 0.f));

    buffer duty(YSE::STANDARD_BUFFERSIZE);
    duty = 0.6f;
    buffer sync(YSE::STANDARD_BUFFERSIZE);
    sync = 0.f;
    for (unsigned int i = 64; i < sync.getLength(); ++i)
      sync.getPtr()[i] = 1.f;
    buffer frequency(YSE::STANDARD_BUFFERSIZE);
    frequency = 600.f;
    tri.GetInlet(1)->SetBuffer(&duty, YSE::T_GUI);
    tri.GetInlet(2)->SetBuffer(&sync, YSE::T_GUI);
    tri.Calculate(YSE::T_DSP);
    CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(frequency, duty, sync), 0.f));
  }

  TEST_CASE("~saw ~rect ~tri: in a rendered patch, synced by ~phasor, after a JSON round trip "
            "(#1055)") {
    TestHelpers::ScopedSampleRate rate(44100);
    std::string dump;
    {
      patcherImplementation src(3, nullptr);
      YSE::pHandle* sync = src.CreateObject(YSE::OBJ::D_PHASOR, "150");
      YSE::pHandle* saw = src.CreateObject(YSE::OBJ::D_SAW, "380");
      YSE::pHandle* width = src.CreateObject(YSE::OBJ::D_SIG, "0.3");
      YSE::pHandle* rect = src.CreateObject(YSE::OBJ::D_RECT, "220");
      YSE::pHandle* tri = src.CreateObject(YSE::OBJ::D_TRI, "330 0.7");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(sync != nullptr);
      REQUIRE(rect != nullptr);
      REQUIRE(tri != nullptr);
      src.Connect(sync, 0, saw, 1);
      src.Connect(width, 0, rect, 1);
      src.Connect(saw, 0, dac, 0);
      src.Connect(rect, 0, dac, 1);
      src.Connect(tri, 0, dac, 2);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(3, nullptr);
    dst.ParseJSON(dump);

    YSE::DSP::phasor syncReference;
    blSaw sawReference;
    blRect rectReference;
    blTri triReference;
    triReference.duty(0.7f);
    buffer sawFrequency(YSE::STANDARD_BUFFERSIZE);
    sawFrequency = 380.f;
    buffer rectFrequency(YSE::STANDARD_BUFFERSIZE);
    rectFrequency = 220.f;
    buffer rectWidth(YSE::STANDARD_BUFFERSIZE);
    rectWidth = 0.3f;
    for (int block = 0; block < 8; ++block) {
      CAPTURE(block);
      dst.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0],
                                            sawReference(sawFrequency, syncReference(150.f)), 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[1], rectReference(rectFrequency, rectWidth),
                                            0.f));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[2], triReference(330.f), 0.f));
    }
    for (int channel = 0; channel < 3; ++channel)
      CHECK(TestHelpers::measureRms(dst.output[channel]) > 0.1f);
  }

  TEST_CASE("~saw ~rect ~tri: numbers, messages and blocks allocate nothing (#1055)") {
    dSaw saw;
    dRect rect;
    dTri tri;
    BufferSink sawSink;
    BufferSink rectSink;
    BufferSink triSink;
    Wire(saw, sawSink);
    Wire(rect, rectSink);
    Wire(tri, triSink);
    buffer signal(YSE::STANDARD_BUFFERSIZE);
    signal = 0.5f;
    saw.Calculate(YSE::T_DSP);
    rect.Calculate(YSE::T_DSP);
    tri.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      saw.GetInlet(0)->SetFloat(220.f, YSE::T_DSP);
      saw.GetInlet(0)->SetMessage("synctrig 0.1", YSE::T_DSP);
      saw.GetInlet(1)->SetBuffer(&signal, YSE::T_DSP);
      saw.Calculate(YSE::T_DSP);
      rect.GetInlet(1)->SetFloat(0.2f, YSE::T_DSP);
      rect.GetInlet(2)->SetBuffer(&signal, YSE::T_DSP);
      rect.Calculate(YSE::T_DSP);
      rect.GetInlet(1)->SetBuffer(&signal, YSE::T_DSP);
      rect.Calculate(YSE::T_DSP);
      tri.GetInlet(1)->SetInt(1, YSE::T_DSP);
      tri.GetInlet(0)->SetMessage("synctrig 0.9", YSE::T_DSP);
      tri.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

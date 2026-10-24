// Tests for the ~shape patcher object (patcher/generatorObjects/dShape),
// issue #1071. The signal is DSP::shapeEnvelope's and is tested in
// Tests/dsp/test_shape_envelope.cpp; these show that numbers, lists, messages
// and arguments reach it, alone and in a rendered patch (a ~phasor playing an
// envelope sent through a receive) after a JSON round trip.

#include <doctest/doctest.h>
#include <algorithm>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/generatorObjects/dShape.h"
#include "dsp/generators/shapeEnvelope.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::shapeEnvelope;
using YSE::PATCHER::dShape;
using YSE::PATCHER::patcherImplementation;

namespace {
  void Wire(dShape& obj, BufferSink& sink) {
    obj.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(obj.GetOutlet(0), 0);
  }

  void FillRamp(buffer& b, unsigned int start, unsigned int period) {
    for (unsigned int i = 0; i < b.getLength(); ++i)
      b.getPtr()[i] = static_cast<float>((start + i) % period) / static_cast<float>(period);
  }

  // Runs `blocks` blocks of a ramp through `obj` and `reference`, comparing.
  void Compare(dShape& obj, BufferSink& sink, shapeEnvelope& reference, unsigned int& clock,
               unsigned int blocks, unsigned int period) {
    buffer ramp(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int block = 0; block < blocks; ++block) {
      CAPTURE(clock);
      FillRamp(ramp, clock, period);
      clock += YSE::STANDARD_BUFFERSIZE;
      obj.GetInlet(0)->SetBuffer(&ramp, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(sink.received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(ramp), 0.f));
    }
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~shape: registry and shape (#1071)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_SHAPE);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~shape");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 1);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_SHAPE));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::OSC);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 3);
  }

  TEST_CASE("~shape: numbers, lists and messages against the DSP class (#1071)") {
    dShape obj;
    BufferSink sink;
    Wire(obj, sink);
    shapeEnvelope reference;
    unsigned int clock = 0;
    const unsigned int period = 640;

    // No envelope yet: 0.
    Compare(obj, sink, reference, clock, 2, period);

    // A float is the initial value of the next list; alone it changes nothing.
    obj.GetInlet(0)->SetFloat(0.25f, YSE::T_GUI);
    Compare(obj, sink, reference, clock, 1, period);
    obj.GetInlet(0)->SetList("1 100 0 300", YSE::T_GUI);
    {
      const float values[] = {1.f, 0.f};
      const float times[] = {100.f, 300.f};
      REQUIRE(reference.breakpoints(0.25f, values, times, nullptr, 2));
    }
    Compare(obj, sink, reference, clock, 10, period);
    CHECK(obj.Envelope().count() == 2);
    CHECK(obj.Envelope().initial() == 0.25f);

    // An odd list leads with the initial value; an int sets it too.
    obj.GetInlet(0)->SetList("0.5 1 10 0.5 0 0 20", YSE::T_GUI);
    {
      const float values[] = {1.f, 0.5f, 0.f};
      const float times[] = {10.f, 0.f, 20.f};
      REQUIRE(reference.breakpoints(0.5f, values, times, nullptr, 3));
    }
    Compare(obj, sink, reference, clock, 6, period);
    obj.GetInlet(0)->SetInt(-1, YSE::T_GUI);
    obj.GetInlet(0)->SetList("1 50", YSE::T_GUI);
    {
      const float values[] = {1.f};
      const float times[] = {50.f};
      REQUIRE(reference.breakpoints(-1.f, values, times, nullptr, 1));
    }
    Compare(obj, sink, reference, clock, 6, period);

    // curvemode 1: triplets, played curved.
    obj.SetMessage("curvemode 1", 0.f);
    obj.GetInlet(0)->SetList("0 1 100 0.5 0 300 -0.5", YSE::T_GUI);
    reference.curved(true);
    {
      const float values[] = {1.f, 0.f};
      const float times[] = {100.f, 300.f};
      const float curves[] = {0.5f, -0.5f};
      REQUIRE(reference.breakpoints(0.f, values, times, curves, 2));
    }
    Compare(obj, sink, reference, clock, 6, period);
    CHECK(obj.Envelope().curved());
    CHECK(obj.Envelope().curve(1) == -0.5f);

    // curvemode 0: pairs again, curves played linear.
    obj.SetMessage("curvemode 0", 0.f);
    reference.curved(false);
    Compare(obj, sink, reference, clock, 2, period);

    // Messages that arrive as lists (a receive) set the attributes too.
    obj.GetInlet(0)->SetList("constant 1", YSE::T_GUI);
    obj.GetInlet(0)->SetList("syncupdate 1", YSE::T_GUI);
    reference.constant(1).syncUpdate(true);
    Compare(obj, sink, reference, clock, 4, period);
    CHECK(obj.Envelope().constant() == 1);
    CHECK(obj.Envelope().syncUpdate());

    // Refused whole and counted: a word inside, half a pair, too many points.
    CHECK(obj.Dropped() == 0);
    obj.GetInlet(0)->SetList("1 2 x", YSE::T_GUI);
    obj.SetMessage("curvemode 1", 0.f);
    obj.GetInlet(0)->SetList("1 2 3 4 5", YSE::T_GUI);
    obj.SetMessage("curvemode 0", 0.f);
    std::string many;
    for (unsigned int i = 0; i <= shapeEnvelope::DEFAULT_POINTS; ++i)
      many += "1 1 ";
    obj.GetInlet(0)->SetList(many, YSE::T_GUI);
    CHECK(obj.Dropped() == 3);
    // Unknown or malformed messages: ignored.
    obj.SetMessage("constant", 0.f);
    obj.SetMessage("height 3", 0.f);
    Compare(obj, sink, reference, clock, 2, period);
    CHECK(obj.Envelope().count() == 2);
  }

  TEST_CASE("~shape: arguments constant curvemode syncupdate (#1071)") {
    dShape obj;
    BufferSink sink;
    Wire(obj, sink);
    obj.SetParams("1 1 1");
    obj.Calculate(YSE::T_DSP);
    CHECK(obj.Envelope().constant() == 1);
    CHECK(obj.Envelope().curved());
    CHECK(obj.Envelope().syncUpdate());

    // syncupdate holds the envelope until the ramp starts again.
    buffer ramp(YSE::STANDARD_BUFFERSIZE);
    FillRamp(ramp, 0, 1000);
    obj.GetInlet(0)->SetBuffer(&ramp, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    obj.GetInlet(0)->SetList("0 1 10 0", YSE::T_GUI);
    FillRamp(ramp, 128, 1000);
    obj.GetInlet(0)->SetBuffer(&ramp, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CHECK(obj.Envelope().pending());
    CHECK(obj.Envelope().count() == 0);

    dShape plain;
    plain.SetParams("");
    plain.Calculate(YSE::T_DSP);
    CHECK(plain.Envelope().constant() == 0);
    CHECK_FALSE(plain.Envelope().curved());
    CHECK_FALSE(plain.Envelope().syncUpdate());
  }

  TEST_CASE("~shape: in a rendered patch, a ~phasor plays an envelope sent through a receive, "
            "after a JSON round trip (#1071)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "37.5");
      YSE::pHandle* env = src.CreateObject(YSE::OBJ::D_SHAPE, "1 0 1");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1071");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(env != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, env, 0);
      src.Connect(cmd, 0, env, 0);
      src.Connect(env, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);

    YSE::DSP::phasor rampReference;
    shapeEnvelope reference;
    reference.constant(1).syncUpdate(true);
    float peak = 0.f;
    for (int block = 0; block < 60; ++block) {
      CAPTURE(block);
      if (block == 2) {
        REQUIRE(dst.PassData("1 5 0 95", "cmd1071", YSE::T_GUI));
        const float values[] = {1.f, 0.f};
        const float times[] = {5.f, 95.f};
        REQUIRE(reference.breakpoints(0.f, values, times, nullptr, 2));
      }
      if (block == 30) {
        REQUIRE(dst.PassData("syncupdate 0", "cmd1071", YSE::T_GUI));
        REQUIRE(dst.PassData("0.5 0.5 1", "cmd1071", YSE::T_GUI));
        reference.syncUpdate(false);
        const float values[] = {0.5f};
        const float times[] = {1.f};
        REQUIRE(reference.breakpoints(0.5f, values, times, nullptr, 1));
      }
      dst.Calculate(YSE::T_DSP);
      buffer& phase = rampReference(37.5f);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(phase), 0.f));
      if (block < 30) {
        for (unsigned int i = 0; i < dst.output[0].getLength(); ++i)
          peak = std::max(peak, dst.output[0].getPtr()[i]);
      }
    }
    // The envelope reached its top: it waited for a wrap (syncupdate from the
    // saved argument), then played its 5 ms attack.
    CHECK(peak > 0.9f);
    CHECK(dst.output[0].getPtr()[0] == 0.5f);
  }

  TEST_CASE("~shape: numbers, lists, messages and blocks on the audio thread allocate nothing "
            "(#1071)") {
    dShape obj;
    BufferSink sink;
    Wire(obj, sink);
    buffer ramp(YSE::STANDARD_BUFFERSIZE);
    FillRamp(ramp, 0, 512);
    const std::string list = "0 1 10 0.5 0 20 -0.5";
    const std::string pairs = "1 10 0 20";
    const std::string mode = "curvemode 1";
    const std::string sync = "syncupdate 1";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetFloat(0.5f, YSE::T_DSP);
      obj.GetInlet(0)->SetInt(1, YSE::T_DSP);
      obj.GetInlet(0)->SetList(pairs, YSE::T_DSP);
      obj.SetMessage(mode, 0.f);
      obj.GetInlet(0)->SetList(list, YSE::T_DSP);
      obj.GetInlet(0)->SetList(sync, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      obj.GetInlet(0)->SetBuffer(&ramp, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(obj.Envelope().count() == 2);
  }

} // TEST_SUITE("patcher")

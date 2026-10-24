// Tests for the ~techno patcher object (patcher/generatorObjects/dTechno),
// issue #1068. The sequencing is DSP::techno's and is tested in
// Tests/dsp/test_techno.cpp; these show that the phase, numbers, every
// message and the argument reach it, in order, alone and in a rendered patch
// (a ~phasor driving it, steps set through a receive) after a JSON round trip.

#include <doctest/doctest.h>
#include <algorithm>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/generatorObjects/dTechno.h"
#include "dsp/generators/phasor.hpp"
#include "dsp/generators/techno.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::techno;
using YSE::PATCHER::dTechno;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  struct sinks {
    BufferSink pitch;
    BufferSink envelope;
    BufferSink position;
  };

  void Wire(dTechno& obj, sinks& out) {
    TestHelpers::Wire(obj, 0, out.pitch, 0);
    TestHelpers::Wire(obj, 1, out.envelope, 0);
    TestHelpers::Wire(obj, 2, out.position, 0);
  }

  // The object's three outlets equal the reference's outputs for `phase`.
  void CheckAgainst(sinks& out, techno& reference, buffer& phase) {
    REQUIRE(out.pitch.received != nullptr);
    REQUIRE(out.envelope.received != nullptr);
    REQUIRE(out.position.received != nullptr);
    buffer& pitch = reference(phase);
    CHECK(TestHelpers::buffersNearlyEqual(*out.pitch.received, pitch, 0.f));
    CHECK(TestHelpers::buffersNearlyEqual(*out.envelope.received, reference.envelope(), 0.f));
    CHECK(TestHelpers::buffersNearlyEqual(*out.position.received, reference.stepPosition(), 0.f));
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~techno: registry and shape (#1068)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* seq = p.CreateObject(YSE::OBJ::D_TECHNO);
    REQUIRE(seq != nullptr);
    CHECK(std::string(seq->Type()) == "~techno");
    CHECK(seq->GetInputs() == 1);
    CHECK(seq->GetOutputs() == 3);
    CHECK(seq->IsDSPInput(0));
    for (int i = 0; i < 3; ++i)
      CHECK(seq->OutputDataType(i) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::D_TECHNO));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::SEQUENCE);
    CHECK(obj->IsDSPObject());
    CHECK(obj->GetParamCount() == 1);
  }

  TEST_CASE("~techno: every message maps onto the DSP class, in the order sent (#1068)") {
    sinks out;
    dTechno obj;
    Wire(obj, out);
    // length first, then pos: applied in order, so the pos survives.
    obj.SetMessage("length 4", 0.f);
    obj.SetMessage("pos 2 0.6", 0.f);
    obj.SetMessage("pitch 1 300", 0.f);
    obj.SetMessage("amplitude 2 0.5", 0.f);
    obj.SetMessage("curve 1 2", 0.f);
    obj.SetMessage("attack 3 4", 0.f);
    obj.GetInlet(0)->SetMessage("decay 0 0.5", YSE::T_GUI);
    obj.GetInlet(0)->SetFloat(0.4f, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);

    const techno& seq = obj.Sequencer();
    CHECK(seq.length() == 4);
    CHECK(seq.position(2) == doctest::Approx(0.6f));
    CHECK(seq.pitch(1) == 300.f);
    CHECK(seq.amplitude(2) == 0.5f);
    CHECK(seq.curve(1) == 2.f);
    CHECK(seq.attack(3) == 4.f);
    CHECK(seq.decay(0) == 0.5f);

    techno reference;
    reference.length(4).position(2, 0.6f).pitch(1, 300.f).amplitude(2, 0.5f).curve(1, 2.f);
    reference.attack(3, 4.f).decay(0, 0.5f);
    // An unconnected inlet reads its last number as a constant phase.
    buffer phase(N);
    phase = 0.4f;
    CheckAgainst(out, reference, phase);

    // An int is a phase too.
    obj.GetInlet(0)->SetInt(1, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CHECK(out.position.received->getPtr()[0] == doctest::Approx(4.f));

    // repeatpos: 8 steps swung in pairs.
    obj.SetMessage("length 8", 0.f);
    obj.SetMessage("repeatpos 0.66", 0.f);
    obj.Calculate(YSE::T_DSP);
    CHECK(obj.Sequencer().length() == 8);
    CHECK(obj.Sequencer().position(1) == doctest::Approx(0.25f * 0.66f));
    CHECK(obj.Sequencer().position(3) == doctest::Approx(0.25f * 1.66f));
    // Refused by the DSP class (3 does not divide 8): unchanged.
    obj.SetMessage("repeatpos 0.3 0.6", 0.f);
    obj.Calculate(YSE::T_DSP);
    CHECK(obj.Sequencer().position(1) == doctest::Approx(0.25f * 0.66f));
    // A later length spaces evenly again.
    obj.SetMessage("length 8", 0.f);
    obj.Calculate(YSE::T_DSP);
    CHECK(obj.Sequencer().position(1) == doctest::Approx(0.125f));
    CHECK(obj.Dropped() == 0);
  }

  TEST_CASE("~techno: malformed messages and steps out of range are ignored (#1068)") {
    sinks out;
    dTechno obj;
    Wire(obj, out);
    obj.SetMessage("length 4", 0.f);
    obj.Calculate(YSE::T_DSP);
    for (const char* bad :
         {"pitch", "pitch 1", "pitch x 300", "pitch 1 x", "pitch -1 300", "pitch 16 300", "length",
          "length x", "length -2", "wobble 1 2", "", "   "}) {
      CAPTURE(bad);
      obj.SetMessage(bad, 0.f);
    }
    obj.Calculate(YSE::T_DSP);
    const techno& seq = obj.Sequencer();
    CHECK(seq.length() == 4);
    for (unsigned int k = 0; k < seq.capacity(); ++k)
      CHECK(seq.pitch(k) == 440.f);
    // A step number is truncated.
    obj.SetMessage("pitch 2.9 330", 0.f);
    obj.Calculate(YSE::T_DSP);
    CHECK(obj.Sequencer().pitch(2) == 330.f);
  }

  TEST_CASE("~techno: the steps argument sets the capacity (#1068)") {
    sinks out;
    dTechno obj;
    Wire(obj, out);
    CHECK(obj.Sequencer().capacity() == techno::DEFAULT_STEPS);
    obj.SetParams("4");
    CHECK(obj.Sequencer().capacity() == 4);
    CHECK(obj.Sequencer().length() == 1);
    obj.SetMessage("length 10", 0.f);
    obj.SetMessage("pitch 4 100", 0.f);
    obj.SetMessage("pitch 3 100", 0.f);
    obj.Calculate(YSE::T_DSP);
    CHECK(obj.Sequencer().length() == 4);
    CHECK(obj.Sequencer().pitch(3) == 100.f);

    obj.SetParams("0");
    CHECK(obj.Sequencer().capacity() == 1);
    obj.SetParams("5000");
    CHECK(obj.Sequencer().capacity() == techno::MAX_STEPS);
    obj.SetParams("");
    CHECK(obj.Sequencer().capacity() == techno::DEFAULT_STEPS);
  }

  TEST_CASE("~techno: a full ring refuses whole requests and counts them (#1068)") {
    sinks out;
    dTechno obj;
    Wire(obj, out);
    // 16 steps: a 256-event ring.
    for (int i = 0; i < 300; ++i)
      obj.SetMessage("pitch 0 " + std::to_string(i), 0.f);
    CHECK(obj.Dropped() == 44);
    obj.Calculate(YSE::T_DSP);
    CHECK(obj.Sequencer().pitch(0) == 255.f);
    // Room again after the block.
    obj.SetMessage("pitch 0 1", 0.f);
    obj.Calculate(YSE::T_DSP);
    CHECK(obj.Sequencer().pitch(0) == 1.f);
    CHECK(obj.Dropped() == 44);
  }

  TEST_CASE("~techno: in a rendered patch, driven by ~phasor, after a JSON round trip (#1068)") {
    TestHelpers::ScopedSampleRate rate(44100);
    std::string dump;
    {
      patcherImplementation src(3, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "50");
      YSE::pHandle* seq = src.CreateObject(YSE::OBJ::D_TECHNO, "8");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "seq1068");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(seq != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, seq, 0);
      src.Connect(cmd, 0, seq, 0);
      src.Connect(seq, 0, dac, 0);
      src.Connect(seq, 1, dac, 1);
      src.Connect(seq, 2, dac, 2);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(3, nullptr);
    dst.ParseJSON(dump);

    YSE::DSP::phasor ramp;
    techno reference(8);
    for (int block = 0; block < 10; ++block) {
      CAPTURE(block);
      if (block == 1) {
        for (const char* message : {"length 4", "pitch 0 110", "pitch 1 220", "pitch 2 330",
                                    "pitch 3 440", "curve 2 1", "decay 1 3", "amplitude 3 0.5"}) {
          REQUIRE(dst.PassData(std::string(message), "seq1068", YSE::T_GUI));
        }
        reference.length(4).pitch(0, 110.f).pitch(1, 220.f).pitch(2, 330.f).pitch(3, 440.f);
        reference.curve(2, 1.f).decay(1, 3.f).amplitude(3, 0.5f);
      }
      if (block == 5) {
        REQUIRE(dst.PassData(std::string("repeatpos 0.7"), "seq1068", YSE::T_GUI));
        const float swing[] = {0.7f};
        REQUIRE(reference.repeatPositions(swing, 1));
      }
      dst.Calculate(YSE::T_DSP);
      buffer& phase = ramp(50.f);
      buffer& pitch = reference(phase);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], pitch, 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[1], reference.envelope(), 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[2], reference.stepPosition(), 0.f));
    }
    // The sequence moved through more than one step and pitch.
    float lowest = 1e9f;
    float highest = 0.f;
    for (unsigned int i = 0; i < N; ++i) {
      lowest = std::min(lowest, dst.output[0].getPtr()[i]);
      highest = std::max(highest, dst.output[0].getPtr()[i]);
    }
    CHECK(lowest < highest);
  }

  TEST_CASE("~techno: numbers, messages and blocks on the audio thread allocate nothing (#1068)") {
    sinks out;
    dTechno obj;
    Wire(obj, out);
    buffer phase(N);
    phase = 0.3f;
    const std::string length = "length 8";
    const std::string pitch = "pitch 3 220";
    const std::string pos = "pos 3 0.4";
    const std::string repeat = "repeatpos 0.25 0.5 0.75";
    const std::string decay = "decay 2 4";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetFloat(0.5f, YSE::T_DSP);
      obj.GetInlet(0)->SetInt(0, YSE::T_DSP);
      obj.SetMessage(length, 0.f);
      obj.SetMessage(pitch, 0.f);
      obj.SetMessage(pos, 0.f);
      obj.SetMessage(repeat, 0.f);
      obj.SetMessage(decay, 0.f);
      obj.Calculate(YSE::T_DSP);
      obj.GetInlet(0)->SetBuffer(&phase, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

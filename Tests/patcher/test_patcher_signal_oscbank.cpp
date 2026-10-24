// Tests for the ~oscbank and ~ioscbank patcher objects
// (patcher/generatorObjects/dOscbank), issue #1061. The signal is
// DSP::oscillatorBank's and is tested in Tests/dsp/test_oscillator_bank.cpp;
// these show that numbers, messages, signals and the arguments reach it, in
// order, alone and in a rendered patch after a JSON round trip.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/generatorObjects/dOscbank.h"
#include "dsp/generators/oscillatorBank.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::oscillatorBank;
using YSE::PATCHER::dIoscbank;
using YSE::PATCHER::dOscbank;
using YSE::PATCHER::oscillatorBankObject;
using YSE::PATCHER::patcherImplementation;

namespace {
  void Wire(oscillatorBankObject& obj, BufferSink& sink) {
    obj.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(obj.GetOutlet(0), 0);
  }

  void Message(oscillatorBankObject& obj, const std::string& text, int inlet = 0) {
    obj.GetInlet(inlet)->SetMessage(text, YSE::T_GUI);
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~oscbank / ~ioscbank: registry and shape (#1061)") {
    for (const char* type : {YSE::OBJ::D_OSCBANK, YSE::OBJ::D_IOSCBANK}) {
      CAPTURE(type);
      YSE::patcher p;
      p.create(1);
      YSE::pHandle* h = p.CreateObject(type);
      REQUIRE(h != nullptr);
      CHECK(std::string(h->Type()) == type);
      CHECK(h->GetInputs() == 4);
      CHECK(h->GetOutputs() == 1);
      for (int i = 0; i < 4; ++i)
        CHECK(h->IsDSPInput(i));
      CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(type));
      REQUIRE(obj != nullptr);
      CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::OSC);
      CHECK(obj->IsDSPObject());
      CHECK(obj->GetParamCount() == (std::string(type) == "~oscbank" ? 4 : 3));
    }
    dOscbank stepped;
    dIoscbank smooth;
    CHECK_FALSE(stepped.Bank().interpolating());
    CHECK(smooth.Bank().interpolating());
    CHECK(stepped.Bank().capacity() == 64);
  }

  TEST_CASE("~oscbank: numbers set the oscillator the index names, in the order sent "
            "(#1061)") {
    dOscbank obj;
    BufferSink sink;
    Wire(obj, sink);
    obj.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CHECK(TestHelpers::measureRms(*sink.received) == 0.f);

    // Two oscillators set in one go, before one block.
    obj.GetInlet(3)->SetInt(1, YSE::T_GUI);
    obj.GetInlet(0)->SetFloat(440.f, YSE::T_GUI);
    obj.GetInlet(1)->SetFloat(0.5f, YSE::T_GUI);
    obj.GetInlet(3)->SetFloat(2.f, YSE::T_GUI);
    obj.GetInlet(0)->SetInt(660, YSE::T_GUI);
    obj.GetInlet(1)->SetFloat(0.25f, YSE::T_GUI);
    // An index past the capacity names nothing: the numbers are dropped.
    obj.GetInlet(3)->SetInt(64, YSE::T_GUI);
    obj.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CHECK(obj.Bank().frequency(1) == 440.f);
    CHECK(obj.Bank().magnitude(1) == 0.5f);
    CHECK(obj.Bank().frequency(2) == 660.f);
    CHECK(obj.Bank().magnitude(2) == 0.25f);
    CHECK(obj.Dropped() == 0);

    oscillatorBank reference;
    reference.set(1, 440.f, 0.5f).set(2, 660.f, 0.25f);
    CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(), 0.f));

    // The phase inlet sets a phase only with framesync on.
    obj.GetInlet(3)->SetInt(5, YSE::T_GUI);
    obj.GetInlet(2)->SetFloat(0.5f, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CHECK(obj.Bank().phase(5) == 0.f);
    Message(obj, "framesync 1");
    obj.GetInlet(2)->SetFloat(0.5f, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CHECK(obj.Bank().frameSync());
    CHECK(obj.Bank().phase(5) == doctest::Approx(0.5f));
  }

  TEST_CASE("~oscbank / ~ioscbank: messages (#1061)") {
    dOscbank obj;
    BufferSink sink;
    Wire(obj, sink);
    Message(obj, "set 100 0.5 200 0.25 300");
    obj.Calculate(YSE::T_DSP);
    CHECK(obj.Bank().frequency(0) == 100.f);
    CHECK(obj.Bank().magnitude(0) == 0.5f);
    CHECK(obj.Bank().frequency(1) == 200.f);
    CHECK(obj.Bank().magnitude(1) == 0.25f);
    // The odd 300 has no magnitude: not a pair.
    CHECK(obj.Bank().frequency(2) == 0.f);

    // set fades the others; words are skipped.
    obj.GetInlet(3)->SetInt(10, YSE::T_GUI);
    obj.GetInlet(1)->SetFloat(1.f, YSE::T_GUI);
    Message(obj, "set 50 word 0.75", 2);
    obj.Calculate(YSE::T_DSP);
    CHECK(obj.Bank().frequency(0) == 50.f);
    CHECK(obj.Bank().magnitude(0) == 0.75f);
    CHECK(obj.Bank().magnitude(1) == 0.f);
    CHECK(obj.Bank().magnitude(10) == 0.f);

    Message(obj, "silence");
    obj.Calculate(YSE::T_DSP);
    CHECK(obj.Bank().magnitude(0) == 0.f);
    CHECK(obj.Bank().frequency(0) == 50.f);
    Message(obj, "clear");
    obj.Calculate(YSE::T_DSP);
    CHECK(obj.Bank().frequency(0) == 0.f);

    Message(obj, "size 2");
    Message(obj, "freqsmooth 8");
    Message(obj, "magsmooth 4.0");
    Message(obj, "tabpoints 1000");
    Message(obj, "copybuf somebuffer");
    Message(obj, "nonsense 3");
    Message(obj, "size"); // no number: ignored
    obj.Calculate(YSE::T_DSP);
    CHECK(obj.Bank().size() == 2);
    CHECK(obj.Bank().frequencySmoothing() == 8);
    CHECK(obj.Bank().magnitudeSmoothing() == 4);
    CHECK(obj.Bank().tableSize() == 1024);
    Message(obj, "framesync 1");
    obj.Calculate(YSE::T_DSP);
    CHECK(obj.Bank().frameSync());
    Message(obj, "framesync 0");
    Message(obj, "magsmooth -3");
    obj.Calculate(YSE::T_DSP);
    CHECK_FALSE(obj.Bank().frameSync());
    CHECK(obj.Bank().magnitudeSmoothing() == 0);

    // ~ioscbank has no tabpoints: its table stays 4096 points.
    dIoscbank smooth;
    Message(smooth, "tabpoints 1000");
    Message(smooth, "size 3");
    smooth.Calculate(YSE::T_DSP);
    CHECK(smooth.Bank().tableSize() == 4096);
    CHECK(smooth.Bank().size() == 3);
  }

  TEST_CASE("~oscbank / ~ioscbank: the arguments (#1061)") {
    dOscbank obj;
    obj.SetParams("8 16 32 256");
    CHECK(obj.Bank().capacity() == 8);
    CHECK(obj.Bank().size() == 8);
    CHECK(obj.Bank().frequencySmoothing() == 16);
    CHECK(obj.Bank().magnitudeSmoothing() == 32);
    CHECK(obj.Bank().tableSize() == 256);
    CHECK_FALSE(obj.Bank().interpolating());

    obj.SetParams("0 -1 -1 3");
    CHECK(obj.Bank().capacity() == 1);
    CHECK(obj.Bank().frequencySmoothing() == 0);
    CHECK(obj.Bank().magnitudeSmoothing() == 0);
    CHECK(obj.Bank().tableSize() == 4);
    obj.SetParams("99999");
    CHECK(obj.Bank().capacity() == 4096);
    obj.SetParams("");
    CHECK(obj.Bank().capacity() == 64);
    CHECK(obj.Bank().frequencySmoothing() == 1);
    CHECK(obj.Bank().tableSize() == 4096);

    dIoscbank smooth;
    smooth.SetParams("4 2 3");
    CHECK(smooth.Bank().capacity() == 4);
    CHECK(smooth.Bank().frequencySmoothing() == 2);
    CHECK(smooth.Bank().magnitudeSmoothing() == 3);
    CHECK(smooth.Bank().tableSize() == 4096);
    CHECK(smooth.Bank().interpolating());
  }

  TEST_CASE("~ioscbank: signals set oscillators sample by sample, against DSP::oscillatorBank "
            "(#1061)") {
    dIoscbank obj;
    BufferSink sink;
    Wire(obj, sink);
    obj.SetParams("4");
    buffer freq(YSE::STANDARD_BUFFERSIZE);
    buffer mag(YSE::STANDARD_BUFFERSIZE);
    buffer index(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < freq.getLength(); ++i) {
      freq.getPtr()[i] = 100.f * static_cast<float>(1 + (i % 4));
      mag.getPtr()[i] = 0.1f * static_cast<float>(1 + (i % 4));
      index.getPtr()[i] = static_cast<float>(i % 4);
    }
    oscillatorBank reference(4, true);

    // Cold inlets with T_GUI, then a calculation.
    obj.GetInlet(1)->SetBuffer(&mag, YSE::T_GUI);
    obj.GetInlet(3)->SetBuffer(&index, YSE::T_GUI);
    obj.GetInlet(0)->SetBuffer(&freq, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(freq, mag, index), 0.f));
    CHECK(obj.Bank().frequency(3) == 400.f);

    // The next block: what arrived is forgotten. An unconnected index reads
    // its number; so does the magnitude.
    obj.ResetDSP();
    obj.GetInlet(3)->SetInt(2, YSE::T_GUI);
    obj.GetInlet(1)->SetFloat(0.5f, YSE::T_GUI);
    obj.GetInlet(0)->SetBuffer(&freq, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    buffer two(YSE::STANDARD_BUFFERSIZE);
    buffer half(YSE::STANDARD_BUFFERSIZE);
    two = 2.f;
    half = 0.5f;
    CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(freq, half, two), 0.f));
    CHECK(obj.Bank().frequency(2) == 400.f); // the block's last sample
    CHECK(obj.Bank().magnitude(2) == 0.5f);

    // With a signal connected, the numbers are values, not events: the
    // frequency number did not land on oscillator 2 by itself.
    obj.ResetDSP();
    obj.GetInlet(0)->SetFloat(5.f, YSE::T_GUI);
    obj.GetInlet(1)->SetBuffer(&mag, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    buffer five(YSE::STANDARD_BUFFERSIZE);
    five = 5.f;
    CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(five, mag, two), 0.f));

    // A phase signal is used with framesync.
    obj.ResetDSP();
    Message(obj, "framesync 1");
    buffer phase(YSE::STANDARD_BUFFERSIZE);
    phase = 0.25f;
    obj.GetInlet(2)->SetBuffer(&phase, YSE::T_GUI);
    obj.GetInlet(1)->SetBuffer(&mag, YSE::T_GUI);
    obj.GetInlet(3)->SetBuffer(&index, YSE::T_GUI);
    obj.GetInlet(0)->SetBuffer(&freq, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    reference.frameSync(true);
    CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(freq, mag, phase, index), 0.f));
  }

  TEST_CASE("~oscbank: a full ring refuses requests and counts them (#1061)") {
    dOscbank obj;
    BufferSink sink;
    Wire(obj, sink);
    // 64 oscillators: a ring of 256 events.
    for (int i = 0; i < 300; ++i)
      obj.GetInlet(0)->SetFloat(static_cast<float>(i), YSE::T_GUI);
    CHECK(obj.Dropped() == 300 - 256);
    // A set that does not fit is refused whole.
    Message(obj, "set 1 1");
    CHECK(obj.Dropped() == 300 - 256 + 1);
    obj.Calculate(YSE::T_DSP);
    CHECK(obj.Bank().frequency(0) == 255.f);
    Message(obj, "set 1 1");
    obj.Calculate(YSE::T_DSP);
    CHECK(obj.Bank().frequency(0) == 1.f);
    CHECK(obj.Dropped() == 300 - 256 + 1);
  }

  TEST_CASE("~oscbank / ~ioscbank: in a rendered patch, set from message boxes banged through "
            "receives and driven by signals, after a JSON round trip (#1061)") {
    TestHelpers::ScopedSampleRate rate(44100);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* recv = src.CreateObject(YSE::OBJ::G_RECEIVE, "set1061");
      YSE::pHandle* setBox = src.CreateObject(YSE::OBJ::G_MESSAGE, "set 440 0.5 880 0.25");
      YSE::pHandle* hush = src.CreateObject(YSE::OBJ::G_RECEIVE, "hush1061");
      YSE::pHandle* hushBox = src.CreateObject(YSE::OBJ::G_MESSAGE, "silence");
      YSE::pHandle* stepped = src.CreateObject(YSE::OBJ::D_OSCBANK, "4 1 0 1024");
      YSE::pHandle* freq = src.CreateObject(YSE::OBJ::D_SIG, "330");
      YSE::pHandle* mag = src.CreateObject(YSE::OBJ::D_SIG, "0.5");
      YSE::pHandle* smooth = src.CreateObject(YSE::OBJ::D_IOSCBANK, "2 1 64");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(recv != nullptr);
      REQUIRE(setBox != nullptr);
      REQUIRE(hush != nullptr);
      REQUIRE(hushBox != nullptr);
      REQUIRE(stepped != nullptr);
      REQUIRE(freq != nullptr);
      REQUIRE(mag != nullptr);
      REQUIRE(smooth != nullptr);
      src.Connect(recv, 0, setBox, 0);
      src.Connect(setBox, 0, stepped, 0);
      src.Connect(hush, 0, hushBox, 0);
      src.Connect(hushBox, 0, stepped, 0);
      src.Connect(freq, 0, smooth, 0);
      src.Connect(mag, 0, smooth, 1);
      src.Connect(stepped, 0, dac, 0);
      src.Connect(smooth, 0, dac, 1);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    oscillatorBank steppedReference(4, false, 1024);
    oscillatorBank smoothReference(2, true);
    smoothReference.magnitudeSmoothing(64);
    buffer freq(YSE::STANDARD_BUFFERSIZE);
    buffer mag(YSE::STANDARD_BUFFERSIZE);
    buffer index(YSE::STANDARD_BUFFERSIZE);
    freq = 330.f;
    mag = 0.5f;
    index = 0.f;
    const float pairs[] = {440.f, 0.5f, 880.f, 0.25f};
    for (int block = 0; block < 6; ++block) {
      CAPTURE(block);
      if (block == 2) {
        REQUIRE(dst.PassBang("set1061", YSE::T_GUI));
        steppedReference.setPairs(pairs, 2);
      }
      if (block == 4) {
        REQUIRE(dst.PassBang("hush1061", YSE::T_GUI));
        steppedReference.silence();
      }
      dst.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], steppedReference(), 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[1], smoothReference(freq, mag, index), 0.f));
      if (block == 2 || block == 3) CHECK(TestHelpers::measureRms(dst.output[0]) > 0.1f);
      if (block >= 4) CHECK(TestHelpers::measureRms(dst.output[0]) == 0.f);
    }
    CHECK(TestHelpers::measureRms(dst.output[1]) > 0.3f);
  }

  TEST_CASE("~oscbank: numbers, messages and blocks on the audio thread allocate nothing "
            "(#1061)") {
    dOscbank obj;
    BufferSink sink;
    Wire(obj, sink);
    buffer freq(YSE::STANDARD_BUFFERSIZE);
    buffer index(YSE::STANDARD_BUFFERSIZE);
    freq = 220.f;
    index = 3.f;
    const std::string set = "set 100 0.5 200 0.25 300 0.125";
    const std::string clear = "clear";
    const std::string size = "size 16";
    const std::string smooth = "magsmooth 32";
    const std::string sync = "framesync 1";
    const std::string table = "tabpoints 2048";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(3)->SetInt(2, YSE::T_DSP);
      obj.GetInlet(0)->SetFloat(440.f, YSE::T_DSP);
      obj.GetInlet(1)->SetFloat(0.5f, YSE::T_DSP);
      obj.GetInlet(2)->SetInt(0, YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(set, YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(size, YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(smooth, YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(table, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(sync, YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(clear, YSE::T_DSP);
      obj.GetInlet(3)->SetBuffer(&index, YSE::T_DSP);
      obj.GetInlet(0)->SetBuffer(&freq, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(obj.Bank().size() == 16);
    CHECK(obj.Bank().tableSize() == 2048);
  }

} // TEST_SUITE("patcher")

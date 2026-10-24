// Tests for the ~oscbank and ~ioscbank patcher objects
// (patcher/generatorObjects/dOscbank), issue #1061. The signal is
// DSP::oscillatorBank's and is tested in Tests/dsp/test_oscillator_bank.cpp;
// these show that numbers, messages, signals and the arguments reach it, in
// order, alone and in a rendered patch after a JSON round trip.

#include <doctest/doctest.h>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "dsp/sampling/sampleBuffer.hpp"
#include "patcher/io/fileScheduler.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/generatorObjects/dOscbank.h"
#include "dsp/generators/oscillatorBank.hpp"
#include "headers/constants.hpp"
#include "pool_blocker.hpp"
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

  // A buffer of `frames` samples counting up from `first` in channel 0, and
  // -100 in a second channel, so a table from the wrong channel shows.
  std::shared_ptr<YSE::DSP::sampleBuffer> CountingBuffer(const std::string& name,
                                                         unsigned int frames, float first = 0.f) {
    std::shared_ptr<YSE::DSP::sampleBuffer> b = YSE::DSP::sampleBufferRegistry::acquire(name);
    b->setSize(frames, 2);
    std::vector<float> values(frames);
    for (unsigned int i = 0; i < frames; ++i)
      values[i] = first + static_cast<float>(i);
    b->setSamples(0, 0, values.data(), frames);
    std::vector<float> other(frames, -100.f);
    b->setSamples(1, 0, other.data(), frames);
    return b;
  }

  // One oscillator at magnitude 1 into a dac, in a patcher, for copybuf
  // (#1196). On ~oscbank with an 8-point table at 6000 Hz and 48 kHz it
  // steps one point per sample, so output[0][i] is table point i % 8.
  struct CopyRig {
    TestHelpers::ScopedSampleRate rate{48000};
    patcherImplementation p{1, nullptr};
    YSE::pHandle* handle = nullptr;
    oscillatorBankObject* bank = nullptr;

    explicit CopyRig(const char* type = YSE::OBJ::D_OSCBANK, const std::string& args = "1 1 0 8",
                     const std::string& set = "set 6000 1") {
      handle = p.CreateObject(type, args);
      REQUIRE(handle != nullptr);
      YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      p.Connect(handle, 0, dac, 0);
      bank = static_cast<oscillatorBankObject*>(p.GetObjectFromID(handle->GetID()));
      REQUIRE(bank != nullptr);
      Message(set);
      p.Calculate(YSE::T_DSP);
    }

    void Message(const std::string& text, YSE::THREAD thread = YSE::T_GUI) {
      bank->GetInlet(0)->SetMessage(text, thread);
    }

    // The copy task runs on the background pool; the next block takes the
    // table, and the task that frees the old storage runs after it.
    const float* Settle() {
      YSE::PATCHER::fileScheduler* io = p.FileIO();
      REQUIRE(io != nullptr);
      io->WaitIdle();
      p.Calculate(YSE::T_DSP);
      io->WaitIdle();
      return p.output[0].getPtr();
    }

    // Whether the block just rendered is table points `expected` over and
    // over (8 of them).
    bool Reads(const float* expected) {
      const float* out = p.output[0].getPtr();
      for (unsigned int i = 0; i < YSE::STANDARD_BUFFERSIZE; ++i)
        if (out[i] != expected[i % 8]) return false;
      return true;
    }
  };
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

  TEST_CASE("~oscbank / ~ioscbank: in a rendered patch, set by commands passed through a "
            "receive and driven by signals, after a JSON round trip (#1061, #1195)") {
    TestHelpers::ScopedSampleRate rate(44100);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* recv = src.CreateObject(YSE::OBJ::G_RECEIVE, "bank1061");
      YSE::pHandle* stepped = src.CreateObject(YSE::OBJ::D_OSCBANK, "4 1 0 1024");
      YSE::pHandle* freq = src.CreateObject(YSE::OBJ::D_SIG, "330");
      YSE::pHandle* mag = src.CreateObject(YSE::OBJ::D_SIG, "0.5");
      YSE::pHandle* smooth = src.CreateObject(YSE::OBJ::D_IOSCBANK, "2 1 64");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(recv != nullptr);
      REQUIRE(stepped != nullptr);
      REQUIRE(freq != nullptr);
      REQUIRE(mag != nullptr);
      REQUIRE(smooth != nullptr);
      src.Connect(recv, 0, stepped, 0);
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
        REQUIRE(dst.PassData("set 440 0.5 880 0.25", "bank1061", YSE::T_GUI));
        steppedReference.setPairs(pairs, 2);
      }
      if (block == 4) {
        REQUIRE(dst.PassData("silence", "bank1061", YSE::T_GUI));
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

  // ─── copybuf (#1196) ────────────────────────────────────────────────────────

  TEST_CASE("~oscbank: copybuf makes the table from a buffer's first channel, from an offset "
            "(#1196)") {
    std::shared_ptr<YSE::DSP::sampleBuffer> a = CountingBuffer("oscbank_1196_a", 16);
    CopyRig r;
    CHECK_FALSE(r.bank->Bank().customTable());
    CHECK(r.p.output[0].getPtr()[2] == doctest::Approx(1.f)); // the sine, a quarter in

    r.Message("copybuf oscbank_1196_a");
    r.Settle();
    const float fromZero[] = {0.f, 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f};
    CHECK(r.Reads(fromZero));
    CHECK(r.bank->Bank().customTable());
    CHECK(r.bank->Bank().tableSize() == 8);
    CHECK_FALSE(r.bank->ReleasePending()); // the storage it let go is freed

    r.Message("copybuf oscbank_1196_a 4");
    r.Settle();
    const float fromFour[] = {4.f, 5.f, 6.f, 7.f, 8.f, 9.f, 10.f, 11.f};
    CHECK(r.Reads(fromFour));

    // Past the end of the buffer the table is 0; a negative offset is 0.
    r.Message("copybuf oscbank_1196_a 12");
    r.Settle();
    const float shortTail[] = {12.f, 13.f, 14.f, 15.f, 0.f, 0.f, 0.f, 0.f};
    CHECK(r.Reads(shortTail));
    r.Message("copybuf oscbank_1196_a -3");
    r.Settle();
    CHECK(r.Reads(fromZero));

    // A name no buffer has, or none at all, changes nothing.
    r.Message("copybuf oscbank_1196_nobody");
    r.Message("copybuf");
    r.Message("copybuf " + std::string(YSE::PATCHER::fileScheduler::PATH_CAPACITY, 'x'));
    r.Settle();
    CHECK(r.Reads(fromZero));

    // A later tabpoints goes back to the sine.
    r.Message("tabpoints 8");
    r.Settle();
    CHECK_FALSE(r.bank->Bank().customTable());
    CHECK(r.p.output[0].getPtr()[2] == doctest::Approx(1.f));
    CHECK(r.p.FileIO()->Dropped() == 0);
    CHECK_FALSE(r.bank->ReleasePending());
  }

  TEST_CASE("~oscbank: a tabpoints before a copybuf sizes the table it lands in (#1196)") {
    std::shared_ptr<YSE::DSP::sampleBuffer> a = CountingBuffer("oscbank_1196_size", 32, 1.f);
    CopyRig r;
    // Both before one block: the ring's tabpoints first, then the table, read
    // at the new size (four points, one per two samples at 6000 Hz).
    r.Message("tabpoints 4");
    r.Message("copybuf oscbank_1196_size");
    r.Settle();
    CHECK(r.bank->Bank().tableSize() == 4);
    CHECK(r.bank->Bank().customTable());
    const float halfSpeed[] = {1.f, 1.f, 2.f, 2.f, 3.f, 3.f, 4.f, 4.f};
    CHECK(r.Reads(halfSpeed));
  }

  TEST_CASE("~ioscbank: copybuf fills its 4096-point table, the rest 0 (#1196)") {
    std::shared_ptr<YSE::DSP::sampleBuffer> a = CountingBuffer("ioscbank_1196", 100, 1.f);
    // One table point per sample: 4096 points a cycle.
    const float frequency = static_cast<float>(48000.0 / 4096.0);
    CopyRig r(YSE::OBJ::D_IOSCBANK, "1 1 0", "set " + std::to_string(frequency) + " 1");
    oscillatorBank reference(1, true);
    reference.set(0, frequency, 1.f);
    reference(); // the rig's first block, on the sine

    // From 50: samples 51 to 100, then 0 for the rest of the 4096 points.
    r.Message("copybuf ioscbank_1196 50");
    r.Settle();
    CHECK(r.bank->Bank().customTable());
    CHECK(r.bank->Bank().tableSize() == 4096);
    std::vector<float> copied(50);
    a->getSamples(0, 50, copied.data(), 50);
    reference.setTable(copied.data(), 50);
    CHECK(TestHelpers::buffersNearlyEqual(r.p.output[0], reference(), 0.f));
    // Round the whole table once: the copied points come by, the rest is 0.
    float highest = 0.f;
    for (unsigned int block = 0; block < 6144 / YSE::STANDARD_BUFFERSIZE; ++block) {
      r.p.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(r.p.output[0], reference(), 0.f));
      for (unsigned int i = 0; i < YSE::STANDARD_BUFFERSIZE; ++i)
        if (r.p.output[0].getPtr()[i] > highest) highest = r.p.output[0].getPtr()[i];
    }
    CHECK(highest > 99.f);
    CHECK(highest <= 100.f);
  }

  TEST_CASE("~oscbank: of copybufs sent before a block the last wins (#1196)") {
    std::shared_ptr<YSE::DSP::sampleBuffer> a = CountingBuffer("oscbank_1196_last_a", 8);
    std::shared_ptr<YSE::DSP::sampleBuffer> b = CountingBuffer("oscbank_1196_last_b", 8, 100.f);
    CopyRig r;
    r.Message("copybuf oscbank_1196_last_a");
    r.Message("copybuf oscbank_1196_last_b");
    r.Settle();
    const float fromB[] = {100.f, 101.f, 102.f, 103.f, 104.f, 105.f, 106.f, 107.f};
    CHECK(r.Reads(fromB));
    CHECK_FALSE(r.bank->ReleasePending());
  }

  TEST_CASE("~oscbank: copybuf is not saved by DumpJSON (#1196)") {
    std::shared_ptr<YSE::DSP::sampleBuffer> a = CountingBuffer("oscbank_1196_json", 8, 1.f);
    CopyRig r;
    const std::string before = r.p.DumpJSON();
    r.Message("copybuf oscbank_1196_json");
    r.Settle();
    CHECK(r.bank->Bank().customTable());
    // The arguments are what is saved, so a reload starts from the sine.
    CHECK(r.p.DumpJSON() == before);
  }

  TEST_CASE("~oscbank: a copybuf in flight survives the object's deletion (#1196)") {
    std::shared_ptr<YSE::DSP::sampleBuffer> a = CountingBuffer("oscbank_1196_deleted", 64);
    const long held = a.use_count();
    {
      CopyRig r;
      r.Message("copybuf oscbank_1196_deleted");
      r.p.DeleteObject(r.handle);
      r.p.FileIO()->WaitIdle();
      r.p.Calculate(YSE::T_DSP);
      r.p.FileIO()->WaitIdle();
      CHECK(r.p.FileIO()->Dropped() == 0);
    }
    // Nothing still holds the buffer; the table the task made went with the
    // mailbox (ASan sees a leak or a use after free here).
    CHECK(a.use_count() == held);
  }

  TEST_CASE("~oscbank / ~ioscbank: copybuf allocates nothing on the audio thread (#1196)") {
    std::shared_ptr<YSE::DSP::sampleBuffer> a = CountingBuffer("oscbank_1196_alloc_a", 4096);
    std::shared_ptr<YSE::DSP::sampleBuffer> b = CountingBuffer("oscbank_1196_alloc_b", 4096, 1.f);
    for (const char* type : {YSE::OBJ::D_OSCBANK, YSE::OBJ::D_IOSCBANK}) {
      CAPTURE(type);
      CopyRig r(type, std::string(type) == YSE::OBJ::D_OSCBANK ? "1 1 0 8" : "1 1 0");
      // Built before the probe: the strings themselves allocate.
      const std::vector<std::string> messages = {"copybuf oscbank_1196_alloc_a 3",
                                                 "copybuf oscbank_1196_alloc_b",
                                                 "copybuf oscbank_1196_alloc_a"};
      for (const std::string& m : messages) {
        CAPTURE(m);
        {
          TestHelpers::ProbeScope probe;
          r.Message(m, YSE::T_DSP);
          CHECK(TestHelpers::g_alloc_count.load() == 0);
        }
        r.p.FileIO()->WaitIdle();
        {
          // The block that swaps the table in and asks for the old one to go.
          TestHelpers::ProbeScope probe;
          r.p.Calculate(YSE::T_DSP);
          CHECK(TestHelpers::g_alloc_count.load() == 0);
        }
        r.p.FileIO()->WaitIdle();
        CHECK(r.bank->Bank().customTable());
        CHECK_FALSE(r.bank->ReleasePending());
      }
    }
  }

  TEST_CASE("~ioscbank: a control thread sends copybuf while the audio thread renders (#1196)") {
    // Two constant buffers longer than the table: every block reads one whole
    // table, never freed or half-copied memory. Under ASan or TSan a broken
    // handoff shows up here.
    std::shared_ptr<YSE::DSP::sampleBuffer> ones =
        YSE::DSP::sampleBufferRegistry::acquire("oscbank_1196_race_1");
    ones->setSize(4096, 1);
    ones->fill(1.f);
    std::shared_ptr<YSE::DSP::sampleBuffer> twos =
        YSE::DSP::sampleBufferRegistry::acquire("oscbank_1196_race_2");
    twos->setSize(8192, 1);
    twos->fill(2.f);
    // One oscillator sweeping the whole table every block.
    CopyRig r(YSE::OBJ::D_IOSCBANK, "1 1 0",
              "set " + std::to_string(48000.0 / YSE::STANDARD_BUFFERSIZE) + " 1");
    r.Message("copybuf oscbank_1196_race_1");
    r.Settle();
    const std::string toOnes = "copybuf oscbank_1196_race_1";
    const std::string toTwos = "copybuf oscbank_1196_race_2";

    std::atomic<bool> stop{false};
    std::thread control([&] {
      int n = 0;
      while (!stop.load()) {
        r.Message((n++ % 2) != 0 ? toOnes : toTwos);
        std::this_thread::yield();
      }
    });

    bool whole = true;
    int switches = 0;
    float last = 1.f;
    for (int block = 0; block < 20000 && (block < 2000 || switches == 0); ++block) {
      r.p.Calculate(YSE::T_DSP);
      const float* out = r.p.output[0].getPtr();
      const float v = out[0];
      if (!(v == 1.f || v == 2.f)) whole = false;
      for (unsigned int i = 1; i < YSE::STANDARD_BUFFERSIZE; ++i)
        if (out[i] != v) whole = false;
      if (v != last) switches++;
      last = v;
    }
    stop.store(true);
    control.join();
    CHECK(whole);
    CHECK(switches > 0);

    // Quiet again, everything let go is freed.
    r.Settle();
    r.Settle();
    CHECK_FALSE(r.bank->ReleasePending());
  }

  TEST_CASE("~oscbank: in a rendered patch, copybuf from a ~buffer filled by messages, both "
            "through receives, after a JSON round trip (#1196, #1195)") {
    TestHelpers::ScopedSampleRate rate(44100);
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* table = src.CreateObject(YSE::OBJ::D_BUFFER, "oscbank_1196_patch");
      YSE::pHandle* toTable = src.CreateObject(YSE::OBJ::G_RECEIVE, "table1196");
      YSE::pHandle* toBank = src.CreateObject(YSE::OBJ::G_RECEIVE, "bank1196");
      YSE::pHandle* bank = src.CreateObject(YSE::OBJ::D_OSCBANK, "2 1 0 1024");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(table != nullptr);
      REQUIRE(toTable != nullptr);
      REQUIRE(toBank != nullptr);
      REQUIRE(bank != nullptr);
      src.Connect(toTable, 0, table, 0);
      src.Connect(toBank, 0, bank, 0);
      src.Connect(bank, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    REQUIRE(dst.FileIO() != nullptr);
    const std::string saved = dst.DumpJSON();

    // `reference` follows the patch; `sine` is what the bank would play had
    // copybuf not reached it.
    oscillatorBank reference(2, false, 1024);
    oscillatorBank sine(2, false, 1024);
    const float pairs[] = {440.f, 0.5f, 660.f, 0.25f};
    for (int block = 0; block < 6; ++block) {
      CAPTURE(block);
      if (block == 1) {
        REQUIRE(dst.PassData("set 440 0.5 660 0.25", "bank1196", YSE::T_GUI));
        reference.setPairs(pairs, 2);
        sine.setPairs(pairs, 2);
      }
      if (block == 3) {
        // Three cycles of a sine in the buffer, and the bank's table from it.
        // The receives hand the messages on at the top of this block; the
        // ~buffer tasks run first, as tasks run in order. The pool is parked
        // so none of them can land in this block: the bank plays the sine.
        TestHelpers::PoolBlocker blocker;
        REQUIRE(blocker.Park());
        REQUIRE(dst.PassData("sizeinsamps 1024", "table1196", YSE::T_GUI));
        REQUIRE(dst.PassData("fill sin 3", "table1196", YSE::T_GUI));
        REQUIRE(dst.PassData("copybuf oscbank_1196_patch", "bank1196", YSE::T_GUI));
        dst.Calculate(YSE::T_DSP);
        CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(), 0.f));
        CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], sine(), 0.f));
        blocker.Unpark();
        dst.FileIO()->WaitIdle();
        std::shared_ptr<YSE::DSP::sampleBuffer> filled =
            YSE::DSP::sampleBufferRegistry::find("oscbank_1196_patch");
        REQUIRE(filled != nullptr);
        REQUIRE(filled->frames() == 1024);
        std::vector<float> samples(1024);
        filled->getSamples(0, 0, samples.data(), 1024);
        reference.setTable(samples.data(), 1024);
        continue;
      }
      dst.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(), 0.f));
      buffer& unchanged = sine();
      if (block >= 1) CHECK(TestHelpers::measureRms(dst.output[0]) > 0.1f);
      // Before the copybuf landed the bank plays the sine; from block 4 on,
      // the buffer's three cycles.
      if (block < 3) CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], unchanged, 0.f));
      if (block > 3) CHECK_FALSE(TestHelpers::buffersNearlyEqual(dst.output[0], unchanged, 0.01f));
    }
    // Not saved with the patch.
    CHECK(dst.DumpJSON() == saved);
    CHECK(dst.FileIO()->Dropped() == 0);
  }

} // TEST_SUITE("patcher")

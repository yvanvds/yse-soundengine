// Tests for the signal conversion objects ~mtof ~ftom ~atodb ~dbtoa
// ~mstosamps ~sampstoms (patcher/math/dConvert), issue #1052. The sample math
// is DSP::midiToFreq / freqToMidi / ampToDb / dbToAmp / msToSamples /
// samplesToMs (Tests/dsp/test_units.cpp); these show that pins, messages and
// parameters reach it, in isolation and in a running patch.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/dConvert.h"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using TestHelpers::FloatSink;
using TestHelpers::ListSink;
using YSE::PATCHER::patcherImplementation;

namespace {
  struct SampleRateGuard {
    UInt saved;
    SampleRateGuard() : saved(YSE::SAMPLERATE) {}
    ~SampleRateGuard() {
      YSE::SAMPLERATE = saved;
    }
  };

  void Wire(YSE::PATCHER::pObject& op, BufferSink& sink) {
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);
  }

  bool AllEqual(YSE::DSP::buffer* b, float expected) {
    if (b == nullptr) return false;
    for (unsigned int i = 0; i < b->getLength(); ++i)
      if (b->getPtr()[i] != expected) return false;
    return true;
  }

  // A number into inlet 0 of a fresh T (with creation arguments `args` and
  // an optional message), one block out, its first sample. The sink is
  // declared first so it outlives the object.
  template <class T> float Number(float in, const char* args = "", const char* message = "") {
    BufferSink sink;
    T op;
    Wire(op, sink);
    if (args[0] != '\0') op.SetParams(args);
    if (message[0] != '\0') op.GetInlet(0)->SetMessage(message, YSE::T_GUI);
    op.GetInlet(0)->SetFloat(in, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    REQUIRE(sink.received->getLength() == YSE::STANDARD_BUFFERSIZE);
    REQUIRE(AllEqual(sink.received, sink.received->getPtr()[0]));
    return sink.received->getPtr()[0];
  }

  std::vector<float> Numbers(const std::string& list) {
    std::vector<float> out;
    std::istringstream in(list);
    float f = 0.f;
    while (in >> f)
      out.push_back(f);
    return out;
  }

  struct Family {
    const char* type;
    const char* name;
    int outputs;
  };

  const Family kFamily[] = {
      {YSE::OBJ::D_MTOF, "~mtof", 1},           {YSE::OBJ::D_FTOM, "~ftom", 1},
      {YSE::OBJ::D_ATODB, "~atodb", 1},         {YSE::OBJ::D_DBTOA, "~dbtoa", 1},
      {YSE::OBJ::D_MSTOSAMPS, "~mstosamps", 2}, {YSE::OBJ::D_SAMPSTOMS, "~sampstoms", 2},
  };
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("signal convert: registry, names and shape (#1052)") {
    YSE::patcher p;
    p.create(1);
    for (const Family& f : kFamily) {
      CAPTURE(f.name);
      CHECK(std::string(f.type) == f.name);
      YSE::pHandle* h = p.CreateObject(f.type);
      REQUIRE(h != nullptr);
      CHECK(std::string(h->Type()) == f.name);
      CHECK(h->GetInputs() == 1);
      CHECK(h->GetOutputs() == f.outputs);
      CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
      if (f.outputs == 2) CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::ANY);

      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(f.type));
      REQUIRE(obj != nullptr);
      CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MATH);
      CHECK(obj->IsDSPObject());
      CHECK(std::string(obj->Type()) == f.name);
    }
  }

  TEST_CASE("signal convert: ~mtof / ~ftom with base and ref from arguments and messages "
            "(#1052)") {
    using namespace YSE::PATCHER;
    CHECK(Number<dMtof>(69.f) == doctest::Approx(440.f));
    CHECK(Number<dMtof>(81.f) == doctest::Approx(880.f));
    CHECK(Number<dMtof>(60.f) == doctest::Approx(261.6256f).epsilon(1e-4));
    CHECK(Number<dFtom>(440.f) == doctest::Approx(69.f));
    CHECK(Number<dFtom>(220.f) == doctest::Approx(57.f));
    // Retuned: A4 = 432, or middle C = 256.
    CHECK(Number<dMtof>(69.f, "432") == doctest::Approx(432.f));
    CHECK(Number<dMtof>(60.f, "256 60") == doctest::Approx(256.f));
    CHECK(Number<dMtof>(72.f, "", "base 256") == doctest::Approx(256.f * std::pow(2.f, 0.25f)));
    CHECK(Number<dMtof>(72.f, "256", "ref 60") == doctest::Approx(512.f));
    CHECK(Number<dFtom>(256.f, "256 60") == doctest::Approx(60.f));
    CHECK(Number<dFtom>(432.f, "", "base 432") == doctest::Approx(69.f));
    CHECK(Number<dFtom>(440.f, "", "ref 57") == doctest::Approx(57.f));
    // Nonsense is ignored.
    CHECK(Number<dMtof>(69.f, "", "base banana") == doctest::Approx(440.f));
    CHECK(Number<dMtof>(69.f, "", "tune 3") == doctest::Approx(440.f));
    // ~ftom never sends -inf or NaN (#1017).
    CHECK(Number<dFtom>(0.f) == -1500.f);
    CHECK(Number<dFtom>(-100.f) == -1500.f);
    CHECK(std::isfinite(Number<dMtof>(5000.f)));
  }

  TEST_CASE("signal convert: ~atodb / ~dbtoa in dBFS (#1052)") {
    using namespace YSE::PATCHER;
    CHECK(Number<dAtoDb>(1.f) == doctest::Approx(0.f));
    CHECK(Number<dAtoDb>(0.1f) == doctest::Approx(-20.f));
    CHECK(Number<dAtoDb>(-0.1f) == doctest::Approx(-20.f));
    CHECK(Number<dAtoDb>(0.f) == YSE::DSP::DB_FLOOR);
    CHECK(Number<dDbtoA>(0.f) == doctest::Approx(1.f));
    CHECK(Number<dDbtoA>(-20.f) == doctest::Approx(0.1f));
    CHECK(Number<dDbtoA>(-999.f) == 0.f);
    CHECK(std::isfinite(Number<dDbtoA>(5000.f)));

    // An int is a number too.
    BufferSink sink;
    dDbtoA op;
    Wire(op, sink);
    op.GetInlet(0)->SetInt(20, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CHECK(sink.received->getPtr()[0] == doctest::Approx(10.f));
  }

  TEST_CASE("signal convert: ~mstosamps / ~sampstoms follow the live sample rate (#1052)") {
    using namespace YSE::PATCHER;
    SampleRateGuard guard;
    BufferSink sink;
    dMsToSamps op;
    Wire(op, sink);
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < in.getLength(); ++i)
      in.getPtr()[i] = static_cast<float>(i);
    op.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
    for (UInt rate : {44100u, 96000u}) {
      CAPTURE(rate);
      YSE::SAMPLERATE = rate; // after construction: nothing is cached
      op.Calculate(YSE::T_DSP);
      REQUIRE(sink.received != nullptr);
      for (unsigned int i = 0; i < sink.received->getLength(); ++i)
        CHECK(sink.received->getPtr()[i] ==
              doctest::Approx(static_cast<float>(i) * static_cast<float>(rate) / 1000.f));
    }

    YSE::SAMPLERATE = 48000;
    CHECK(Number<dMsToSamps>(10.f) == doctest::Approx(480.f));
    CHECK(Number<dSampsToMs>(480.f) == doctest::Approx(10.f));
    YSE::SAMPLERATE = 44100;
    CHECK(Number<dSampsToMs>(44100.f) == doctest::Approx(1000.f));
  }

  TEST_CASE("signal convert: numbers and lists convert at once to the right outlet (#1052)") {
    using namespace YSE::PATCHER;
    SampleRateGuard guard;
    YSE::SAMPLERATE = 48000;

    FloatSink numbers;
    ListSink lists;
    BufferSink signal;
    dMsToSamps op;
    Wire(op, signal);
    TestHelpers::Wire(op, 1, numbers);

    op.GetInlet(0)->SetFloat(2.5f, YSE::T_GUI);
    CHECK(numbers.gotFloat);
    CHECK(numbers.received == doctest::Approx(120.f));
    numbers.gotFloat = false;
    op.GetInlet(0)->SetInt(3, YSE::T_GUI);
    CHECK(numbers.gotFloat);
    CHECK(numbers.received == doctest::Approx(144.f));
    // The number is also the signal while nothing is connected (#1036).
    op.Calculate(YSE::T_DSP);
    CHECK(AllEqual(signal.received, 144.f));

    FloatSink msNumbers;
    BufferSink msSignal;
    dSampsToMs back;
    Wire(back, msSignal);
    TestHelpers::Wire(back, 1, msNumbers);
    back.GetInlet(0)->SetFloat(24000.f, YSE::T_GUI);
    CHECK(msNumbers.received == doctest::Approx(500.f));

    TestHelpers::Wire(op, 1, lists);
    op.GetInlet(0)->SetList("1 2 0.5 -10", YSE::T_GUI);
    REQUIRE(lists.gotList);
    const std::vector<float> got = Numbers(lists.received);
    REQUIRE(got.size() == 4u);
    CHECK(got[0] == doctest::Approx(48.f));
    CHECK(got[1] == doctest::Approx(96.f));
    CHECK(got[2] == doctest::Approx(24.f));
    CHECK(got[3] == doctest::Approx(-480.f));
    // A list does not change the signal.
    op.Calculate(YSE::T_DSP);
    CHECK(AllEqual(signal.received, 144.f));
  }

  TEST_CASE("signal convert: parameters survive a DumpJSON / ParseJSON round trip (#1052)") {
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* mtof = src.CreateObject(YSE::OBJ::D_MTOF, "256 60");
      YSE::pHandle* ftom = src.CreateObject(YSE::OBJ::D_FTOM, "432");
      // A constant 72 in: ~+ with an unconnected left inlet outputs its argument.
      YSE::pHandle* note = src.CreateObject(YSE::OBJ::D_ADD, "72");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      src.Connect(note, 0, mtof, 0);
      src.Connect(mtof, 0, ftom, 0);
      src.Connect(mtof, 0, dac, 0);
      src.Connect(ftom, 0, dac, 1);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);
    dst.Calculate(YSE::T_DSP);
    CHECK(dst.output[0].getPtr()[0] == doctest::Approx(512.f)); // C above 256 Hz C
    // 512 Hz against A4 = 432: 69 + 12 log2(512 / 432).
    CHECK(dst.output[1].getPtr()[0] ==
          doctest::Approx(69.f + 12.f * std::log2(512.f / 432.f)).epsilon(1e-4));
  }

  TEST_CASE("signal convert: in a patch, notes, levels and times through the chain (#1052)") {
    SampleRateGuard guard;
    YSE::SAMPLERATE = 48000;
    patcherImplementation p(4, nullptr);
    // A ramp of notes 0..127 across the block: ~phasor at one cycle per block
    // times 128.
    const std::string blockFreq =
        std::to_string(static_cast<float>(YSE::SAMPLERATE) / YSE::STANDARD_BUFFERSIZE);
    YSE::pHandle* ramp = p.CreateObject(YSE::OBJ::D_PHASOR, blockFreq);
    YSE::pHandle* scale = p.CreateObject(YSE::OBJ::D_MULTIPLY, "128");
    YSE::pHandle* mtof = p.CreateObject(YSE::OBJ::D_MTOF, "");
    YSE::pHandle* ftom = p.CreateObject(YSE::OBJ::D_FTOM, "");
    YSE::pHandle* gain = p.CreateObject(YSE::OBJ::D_MULTIPLY, "-0.5"); // 0 .. -64 dB
    YSE::pHandle* dbtoa = p.CreateObject(YSE::OBJ::D_DBTOA, "");
    YSE::pHandle* atodb = p.CreateObject(YSE::OBJ::D_ATODB, "");
    YSE::pHandle* toSamples = p.CreateObject(YSE::OBJ::D_MSTOSAMPS, "");
    YSE::pHandle* toMs = p.CreateObject(YSE::OBJ::D_SAMPSTOMS, "");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");

    p.Connect(ramp, 0, scale, 0);
    p.Connect(scale, 0, mtof, 0);
    p.Connect(mtof, 0, ftom, 0);
    p.Connect(scale, 0, gain, 0);
    p.Connect(gain, 0, dbtoa, 0);
    p.Connect(dbtoa, 0, atodb, 0);
    p.Connect(scale, 0, toSamples, 0);
    p.Connect(toSamples, 0, toMs, 0);
    p.Connect(mtof, 0, dac, 0);
    p.Connect(ftom, 0, dac, 1);
    p.Connect(atodb, 0, dac, 2);
    p.Connect(toMs, 0, dac, 3);
    p.Calculate(YSE::T_DSP);

    for (unsigned int i : {0u, 1u, 30u, 64u, 69u, 100u, 127u}) {
      CAPTURE(i);
      // Recover the note the ramp produced from the ms <-> samples round
      // trip (exact up to rounding), then check every branch against it.
      const float note = p.output[3].getPtr()[i];
      CHECK(note >= 0.f);
      CHECK(note < 128.f);
      CHECK(p.output[1].getPtr()[i] == doctest::Approx(note).epsilon(1e-3).scale(1.f));
      CHECK(p.output[0].getPtr()[i] ==
            doctest::Approx(440.f * std::pow(2.f, (note - 69.f) / 12.f)).epsilon(1e-3));
      CHECK(p.output[2].getPtr()[i] == doctest::Approx(-0.5f * note).epsilon(1e-3).scale(1.f));
    }
    // The ramp really moves through the notes.
    CHECK(p.output[3].getPtr()[100] - p.output[3].getPtr()[0] ==
          doctest::Approx(100.f).epsilon(1e-2));
    for (unsigned int ch = 0; ch < 4; ++ch)
      for (unsigned int i = 0; i < p.output[ch].getLength(); ++i)
        CHECK(std::isfinite(p.output[ch].getPtr()[i]));
  }

  TEST_CASE("signal convert: numbers, messages, lists and blocks on the audio thread allocate "
            "nothing (#1052)") {
    using namespace YSE::PATCHER;
    BufferSink sinks[6]; // declared first so they outlive the objects
    FloatSink numberSink;
    ListSink listSink;
    dMtof mtof;
    dFtom ftom;
    dAtoDb atodb;
    dDbtoA dbtoa;
    dMsToSamps toSamples;
    dSampsToMs toMs;
    pObject* all[] = {&mtof, &ftom, &atodb, &dbtoa, &toSamples, &toMs};
    for (unsigned int i = 0; i < 6; ++i) {
      Wire(*all[i], sinks[i]);
      all[i]->Calculate(YSE::T_DSP);
    }
    TestHelpers::Wire(toSamples, 1, numberSink);
    TestHelpers::Wire(toMs, 1, listSink);
    // Grow the list sink's string ahead, so only the object is measured.
    listSink.received.reserve(1024);
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    in = 0.5f;
    {
      TestHelpers::ProbeScope probe;
      for (pObject* op : all) {
        op->GetInlet(0)->SetFloat(0.3f, YSE::T_DSP);
        op->GetInlet(0)->SetInt(2, YSE::T_DSP);
        op->Calculate(YSE::T_DSP);
        op->GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
        op->Calculate(YSE::T_DSP);
      }
      mtof.GetInlet(0)->SetMessage("base 432", YSE::T_DSP);
      ftom.GetInlet(0)->SetMessage("ref 60", YSE::T_DSP);
      toMs.GetInlet(0)->SetList("1 2 3 4.5 1000", YSE::T_DSP);
      mtof.Calculate(YSE::T_DSP);
      ftom.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

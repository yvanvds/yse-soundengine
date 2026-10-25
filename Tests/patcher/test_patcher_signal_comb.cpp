// Tests for the ~comb and ~teeth patcher objects (patcher/delay/dComb,
// patcher/delay/dTeeth), issue #1094. The filtering is DSP::comb's and is
// tested in Tests/dsp/test_comb.cpp; these show that the signals, numbers,
// lists, clear and arguments (the maximum delay included) reach it, alone and
// in a rendered patch after a JSON round trip.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/delay/dComb.h"
#include "patcher/delay/dTeeth.h"
#include "dsp/delay/comb.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::comb;
using YSE::DSP::phasor;
using YSE::PATCHER::dComb;
using YSE::PATCHER::dTeeth;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  template <typename T> void Wire(T& obj, BufferSink& out) {
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

  void Shape(const char* type, unsigned int inlets) {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(type);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == type);
    CHECK(obj->GetInputs() == inlets);
    CHECK(obj->GetOutputs() == 1);
    for (unsigned int i = 0; i < inlets; ++i)
      CHECK(obj->IsDSPInput(i));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(type));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::DELAY);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~comb and ~teeth: registry and shape (#1094)") {
    Shape(YSE::OBJ::D_COMB, 5);
    Shape(YSE::OBJ::D_TEETH, 6);
  }

  TEST_CASE("~comb: signals, numbers, lists and clear against the DSP class (#1094, #1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink out;
    dComb obj;
    Wire(obj, out);
    comb reference;
    buffer objIn(N);
    buffer refIn(N);
    unsigned int start = 0;

    int stepCount = 0;
    // Signals for inlets 1-4, or null for the number.
    const auto step = [&](buffer* d = nullptr, buffer* a = nullptr, buffer* b = nullptr,
                          buffer* c = nullptr) {
      CAPTURE(stepCount);
      stepCount++;
      obj.ResetDSP(); // as the patcher does before every block
      Fill(objIn, start);
      Fill(refIn, start);
      start += N;
      obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
      buffer* signals[] = {d, a, b, c};
      for (unsigned int i = 0; i < 4; ++i)
        if (signals[i] != nullptr) obj.GetInlet(i + 1)->SetBuffer(signals[i], YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(out.received != nullptr);
      buffer rd(N);
      buffer ra(N);
      buffer rb(N);
      buffer rc(N);
      rd = reference.feedbackDelay();
      ra = reference.gain();
      rb = reference.feedforward();
      rc = reference.feedback();
      if (d != nullptr) rd = *d;
      if (a != nullptr) ra = *a;
      if (b != nullptr) rb = *b;
      if (c != nullptr) rc = *c;
      CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(refIn, rd, ra, rb, rc), 0.f));
    };

    // Default: every gain 0, silent.
    step();
    CHECK(out.received->getPtr()[N - 1] == 0.f);

    // Numbers in the right inlets; ints too.
    obj.GetInlet(1)->SetFloat(2.5f, YSE::T_GUI);
    obj.GetInlet(2)->SetFloat(0.5f, YSE::T_GUI);
    obj.GetInlet(3)->SetFloat(0.25f, YSE::T_GUI);
    obj.GetInlet(4)->SetFloat(0.7f, YSE::T_GUI);
    reference.delay(2.5f).gain(0.5f).feedforward(0.25f).feedback(0.7f);
    step();
    step();
    CHECK(out.received->getPtr()[N - 1] != 0.f);
    obj.GetInlet(1)->SetInt(4, YSE::T_GUI);
    obj.GetInlet(2)->SetInt(0, YSE::T_GUI);
    obj.GetInlet(3)->SetInt(1, YSE::T_GUI);
    obj.GetInlet(4)->SetInt(0, YSE::T_GUI);
    reference.delay(4.f).gain(0.f).feedforward(1.f).feedback(0.f);
    step();
    obj.GetInlet(4)->SetFloat(-0.5f, YSE::T_GUI);
    reference.feedback(-0.5f);
    step();

    // A list in any inlet: delay, a, b, c; a shorter one sets the first.
    obj.GetInlet(0)->SetList("1.25 0.8 0.1 0.6", YSE::T_GUI);
    reference.delay(1.25f).gain(0.8f).feedforward(0.1f).feedback(0.6f);
    step();
    obj.GetInlet(3)->SetList("3.5 0.3", YSE::T_GUI);
    reference.delay(3.5f).gain(0.3f);
    step();
    obj.GetInlet(0)->SetMessage("0.75", YSE::T_GUI);
    reference.delay(0.75f);
    step();
    // A single number typed into a right inlet is that inlet's number.
    obj.GetInlet(4)->SetMessage("0.25", YSE::T_GUI);
    reference.feedback(0.25f);
    step();

    // clear in any inlet silences the lines before the next block.
    obj.GetInlet(4)->SetMessage("clear", YSE::T_GUI);
    reference.clear();
    step();
    obj.GetInlet(0)->SetList("clear", YSE::T_GUI);
    reference.clear();
    step();

    // Signals win over the numbers and are read per sample.
    buffer delay(N);
    buffer gain(N);
    buffer ff(N);
    buffer fb(N);
    for (int i = 0; i < static_cast<int>(N); ++i) {
      const auto k = static_cast<float>(i);
      delay.getPtr()[i] = 0.5f + 0.05f * k;
      gain.getPtr()[i] = 1.f - 0.01f * k;
      ff.getPtr()[i] = -0.3f + 0.01f * k;
      fb.getPtr()[i] = -0.9f + 0.0125f * k;
    }
    step(&delay, &gain, &ff, &fb);
    CHECK(reference.feedbackDelay() == delay.getPtr()[N - 1]);
    // Only some connected: the numbers for the rest.
    reference.delay(0.75f).feedforward(0.1f);
    step(nullptr, &gain, nullptr, &fb);
    // Back to unconnected: the last numbers again.
    reference.delay(0.75f).gain(0.3f).feedforward(0.1f).feedback(0.25f);
    step();

    // Unknown or malformed messages and non-finite numbers: ignored.
    obj.SetMessage("delay", 0.f);
    obj.SetMessage("abc 3", 0.f);
    obj.SetMessage("nan 0.5", 0.f);
    obj.SetMessage("", 0.f);
    for (unsigned int i = 1; i < 5; ++i)
      obj.GetInlet(i)->SetFloat(std::nanf(""), YSE::T_GUI);
    step();
    // A list stops at the first token that is not a number.
    obj.SetMessage("2 0.4 inf 0.9", 0.f);
    reference.delay(2.f).gain(0.4f);
    step();

    // Unconnected input: silence.
    BufferSink idleOut;
    dComb idle;
    Wire(idle, idleOut);
    idle.GetInlet(1)->SetFloat(1.f, YSE::T_GUI);
    idle.GetInlet(2)->SetFloat(1.f, YSE::T_GUI);
    idle.GetInlet(4)->SetFloat(0.5f, YSE::T_GUI);
    idle.Calculate(YSE::T_DSP);
    REQUIRE(idleOut.received != nullptr);
    CHECK(idleOut.received->getPtr()[N - 1] == 0.f);
  }

  TEST_CASE("~teeth: signals, numbers, lists and clear against the DSP class (#1094, #1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink out;
    dTeeth obj;
    Wire(obj, out);
    comb reference;
    buffer objIn(N);
    buffer refIn(N);
    unsigned int start = 0;

    int stepCount = 0;
    // Signals for inlets 1-5, or null for the number.
    const auto step = [&](buffer* df = nullptr, buffer* db = nullptr, buffer* a = nullptr,
                          buffer* b = nullptr, buffer* c = nullptr) {
      CAPTURE(stepCount);
      stepCount++;
      obj.ResetDSP();
      Fill(objIn, start);
      Fill(refIn, start);
      start += N;
      obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
      buffer* signals[] = {df, db, a, b, c};
      for (unsigned int i = 0; i < 5; ++i)
        if (signals[i] != nullptr) obj.GetInlet(i + 1)->SetBuffer(signals[i], YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(out.received != nullptr);
      buffer rdf(N);
      buffer rdb(N);
      buffer ra(N);
      buffer rb(N);
      buffer rc(N);
      rdf = reference.feedforwardDelay();
      rdb = reference.feedbackDelay();
      ra = reference.gain();
      rb = reference.feedforward();
      rc = reference.feedback();
      if (df != nullptr) rdf = *df;
      if (db != nullptr) rdb = *db;
      if (a != nullptr) ra = *a;
      if (b != nullptr) rb = *b;
      if (c != nullptr) rc = *c;
      CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(refIn, rdf, rdb, ra, rb, rc),
                                            0.f));
    };

    step();
    CHECK(out.received->getPtr()[N - 1] == 0.f);

    // Numbers in each right inlet; the two delays apart.
    obj.GetInlet(1)->SetFloat(1.5f, YSE::T_GUI);
    obj.GetInlet(2)->SetFloat(4.25f, YSE::T_GUI);
    obj.GetInlet(3)->SetFloat(1.f, YSE::T_GUI);
    obj.GetInlet(4)->SetFloat(0.5f, YSE::T_GUI);
    obj.GetInlet(5)->SetFloat(0.8f, YSE::T_GUI);
    reference.feedforwardDelay(1.5f).feedbackDelay(4.25f).gain(1.f).feedforward(0.5f).feedback(
        0.8f);
    step();
    step();
    obj.GetInlet(1)->SetInt(2, YSE::T_GUI);
    obj.GetInlet(2)->SetInt(3, YSE::T_GUI);
    obj.GetInlet(5)->SetInt(-1, YSE::T_GUI);
    reference.feedforwardDelay(2.f).feedbackDelay(3.f).feedback(-1.f);
    step();
    obj.GetInlet(5)->SetFloat(0.5f, YSE::T_GUI);
    reference.feedback(0.5f);
    step();

    // A list in any inlet: ffdelay, fbdelay, a, b, c.
    obj.GetInlet(2)->SetList("0.7 2.2 0.5 0.25 0.9", YSE::T_GUI);
    reference.feedforwardDelay(0.7f).feedbackDelay(2.2f).gain(0.5f).feedforward(0.25f).feedback(
        0.9f);
    step();
    obj.GetInlet(0)->SetList("1.1 1.9", YSE::T_GUI);
    reference.feedforwardDelay(1.1f).feedbackDelay(1.9f);
    step();
    obj.GetInlet(3)->SetMessage("0.6", YSE::T_GUI);
    reference.gain(0.6f);
    step();

    obj.GetInlet(5)->SetList("clear", YSE::T_GUI);
    reference.clear();
    step();

    buffer df(N);
    buffer db(N);
    buffer gain(N);
    for (int i = 0; i < static_cast<int>(N); ++i) {
      const auto k = static_cast<float>(i);
      df.getPtr()[i] = 0.2f + 0.04f * k;
      db.getPtr()[i] = 5.f - 0.03f * k;
      gain.getPtr()[i] = 0.5f + 0.01f * k;
    }
    step(&df, &db, &gain);
    CHECK(reference.feedforwardDelay() == df.getPtr()[N - 1]);
    CHECK(reference.feedbackDelay() == db.getPtr()[N - 1]);
    reference.feedforwardDelay(1.1f).gain(0.6f);
    step(nullptr, &db);
    reference.feedbackDelay(1.9f);
    step();
  }

  TEST_CASE("~comb and ~teeth: arguments are the maximum delay, the delays and the gains "
            "(#1094)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto run = [](auto& obj, BufferSink& out, comb& reference) {
      buffer objIn(N);
      buffer refIn(N);
      for (unsigned int block = 0; block < 4; ++block) {
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
    const auto checkComb = [&](dComb& obj, BufferSink& out, const std::string& args, float maximum,
                               float delay, float a, float b, float c) {
      CAPTURE(args);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      comb reference(maximum);
      reference.delay(delay).gain(a).feedforward(b).feedback(c);
      run(obj, out, reference);
    };
    const auto checkTeeth = [&](dTeeth& obj, BufferSink& out, const std::string& args,
                                float maximum, float ffDelay, float fbDelay, float a, float b,
                                float c) {
      CAPTURE(args);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      comb reference(maximum);
      reference.feedforwardDelay(ffDelay).feedbackDelay(fbDelay).gain(a).feedforward(b).feedback(c);
      run(obj, out, reference);
    };

    {
      BufferSink out;
      dComb obj;
      Wire(obj, out);
      checkComb(obj, out, "50 5 1 0 0.9", 50.f, 5.f, 1.f, 0.f, 0.9f);
      // Set again: the lines follow each maximum and go back to the default.
      checkComb(obj, out, "40 30 0.5 0.5 0.5", 40.f, 30.f, 0.5f, 0.5f, 0.5f);
      checkComb(obj, out, "10 30 0 1 0", 10.f, 30.f, 0.f, 1.f, 0.f);
      checkComb(obj, out, "10", comb::DEFAULT_MAXIMUM_DELAY, 0.f, 0.f, 0.f, 0.f);
      // A sixth number is ignored, a word skipped.
      checkComb(obj, out, "20 x 4 0.25 0.5 0.6 9", 20.f, 4.f, 0.25f, 0.5f, 0.6f);
    }
    {
      // The default maximum is 10 ms: a longer delay is clamped to it, while
      // a larger maximum lets it through.
      BufferSink out;
      dTeeth obj;
      Wire(obj, out);
      checkTeeth(obj, out, "10 30 2 0 1 0", 10.f, 30.f, 2.f, 0.f, 1.f, 0.f);
      BufferSink longerOut;
      dTeeth longer;
      Wire(longer, longerOut);
      checkTeeth(longer, longerOut, "40 30 2 0 1 0", 40.f, 30.f, 2.f, 0.f, 1.f, 0.f);
      CHECK_FALSE(TestHelpers::buffersNearlyEqual(*out.received, *longerOut.received, 0.f));
      checkTeeth(obj, out, "50 3 7 1 0.5 0.8", 50.f, 3.f, 7.f, 1.f, 0.5f, 0.8f);
      checkTeeth(obj, out, "5", 5.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    }
  }

  TEST_CASE("~comb and ~teeth: in a rendered patch, filtering a ~phasor with commands from "
            "receives, after a JSON round trip (#1094)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* combObj = src.CreateObject(YSE::OBJ::D_COMB, "30 1.5 1 0 0.6");
      YSE::pHandle* teethObj = src.CreateObject(YSE::OBJ::D_TEETH, "30 0.5 2 1 0.5 0.5");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1094");
      YSE::pHandle* fb = src.CreateObject(YSE::OBJ::G_RECEIVE, "fb1094");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(combObj != nullptr);
      REQUIRE(teethObj != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(fb != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, combObj, 0);
      src.Connect(combObj, 0, teethObj, 0);
      src.Connect(cmd, 0, combObj, 0);
      src.Connect(fb, 0, teethObj, 5);
      src.Connect(teethObj, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    phasor rampRef;
    comb combRef(30.f);
    comb teethRef(30.f);
    combRef.delay(1.5f).gain(1.f).feedforward(0.f).feedback(0.6f);
    teethRef.feedforwardDelay(0.5f).feedbackDelay(2.f).gain(1.f).feedforward(0.5f).feedback(0.5f);
    for (int block = 0; block < 12; ++block) {
      CAPTURE(block);
      if (block == 2) {
        REQUIRE(dst.PassData(-0.4f, "fb1094", YSE::T_GUI));
        teethRef.feedback(-0.4f);
      }
      if (block == 4) {
        // Past the default 10 ms: only the saved maximum of 30 allows it.
        REQUIRE(dst.PassData("25 0.5 0.5 0.9", "cmd1094", YSE::T_GUI));
        combRef.delay(25.f).gain(0.5f).feedforward(0.5f).feedback(0.9f);
      }
      if (block == 8) {
        REQUIRE(dst.PassData("clear", "cmd1094", YSE::T_GUI));
        combRef.clear();
      }
      dst.Calculate(YSE::T_DSP);
      buffer& combed = combRef(rampRef(375.f, N));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], teethRef(combed), 0.f));
      CHECK(dst.output[0].getPtr()[N - 1] != 0.f);
    }
  }

  TEST_CASE("~comb and ~teeth: numbers, messages and blocks on the audio thread allocate "
            "nothing (#1094)") {
    buffer in(N);
    buffer sig(N);
    in = 0.5f;
    sig = 0.3f;
    const std::string list = "15 0.4 0.3 0.2 0.1";
    const std::string single = "3";
    const std::string clear = "clear";
    {
      BufferSink out;
      dComb obj;
      Wire(obj, out);
      obj.SetParams("100 20 1 0.5 0.5");
      obj.Calculate(YSE::T_DSP);
      TestHelpers::ProbeScope probe;
      for (unsigned int i = 1; i < 5; ++i) {
        obj.GetInlet(i)->SetFloat(0.5f, YSE::T_DSP);
        obj.GetInlet(i)->SetInt(1, YSE::T_DSP);
      }
      obj.GetInlet(0)->SetList(list, YSE::T_DSP);
      obj.GetInlet(2)->SetMessage(single, YSE::T_DSP);
      obj.GetInlet(1)->SetList(clear, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      for (unsigned int i = 0; i < 5; ++i)
        obj.GetInlet(i)->SetBuffer(i == 0 ? &in : &sig, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    {
      BufferSink out;
      dTeeth obj;
      Wire(obj, out);
      obj.SetParams("100 20 10 1 0.5 0.5");
      obj.Calculate(YSE::T_DSP);
      TestHelpers::ProbeScope probe;
      for (unsigned int i = 1; i < 6; ++i) {
        obj.GetInlet(i)->SetFloat(0.5f, YSE::T_DSP);
        obj.GetInlet(i)->SetInt(1, YSE::T_DSP);
      }
      obj.GetInlet(0)->SetList(list, YSE::T_DSP);
      obj.GetInlet(3)->SetMessage(single, YSE::T_DSP);
      obj.GetInlet(1)->SetList(clear, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      for (unsigned int i = 0; i < 6; ++i)
        obj.GetInlet(i)->SetBuffer(i == 0 ? &in : &sig, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

// Tests for the ~average patcher object (patcher/analysis/dAverage), issue
// #1118. The averages are DSP::movingAverage's and are tested in
// Tests/dsp/test_moving_average.cpp; these show that the signal, the numbers,
// the mode words and the arguments reach it, bit-exact against the DSP class,
// alone and in a rendered ~phasor -> ~- -> ~average -> ~dac patch after a JSON
// round trip.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/analysis/dAverage.h"
#include "dsp/analysis/movingAverage.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::movingAverage;
using YSE::DSP::phasor;
using YSE::PATCHER::dAverage;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dAverage& obj, BufferSink& out) {
    obj.ConnectOutlet(out.GetInlet(0), 0);
    out.ConnectInlet(obj.GetOutlet(0), 0);
  }

  // A slow sine with a fast one on top and some DC.
  void Fill(buffer& b, unsigned int start) {
    for (unsigned int i = 0; i < b.getLength(); ++i) {
      const auto n = static_cast<float>(start + i);
      b.getPtr()[i] = 0.1f + std::sin(n * 0.03f) + 0.2f * std::sin(n * 2.1f);
    }
  }

  // One block of Fill() through the object and the reference; the outputs
  // must agree to the bit.
  void Step(dAverage& obj, BufferSink& out, movingAverage& reference, unsigned int& start) {
    buffer objIn(N);
    buffer refIn(N);
    Fill(objIn, start);
    Fill(refIn, start);
    start += N;
    obj.ResetDSP(); // as the patcher does before every block
    obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_DSP); // the last input: runs CALC()
    REQUIRE(out.received != nullptr);
    CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(refIn), 0.f));
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~average: registry and shape (#1118)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_AVERAGE);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~average");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 1);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_AVERAGE));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::ANALYSIS);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~average: the signal, numbers and mode words against the DSP class (#1118)") {
    BufferSink out;
    dAverage obj;
    Wire(obj, out);
    movingAverage reference; // 100 samples, bipolar: the defaults
    unsigned int start = 0;

    Step(obj, out, reference, start);
    Step(obj, out, reference, start);

    // A number sets the interval; an int too.
    obj.GetInlet(0)->SetFloat(30.4f, YSE::T_GUI);
    reference.length(30);
    Step(obj, out, reference, start);
    obj.GetInlet(0)->SetInt(57, YSE::T_GUI);
    reference.length(57);
    Step(obj, out, reference, start);

    // Beyond the maximum clamps to it, below 1 to 1.
    obj.GetInlet(0)->SetInt(5000, YSE::T_GUI);
    reference.length(100);
    Step(obj, out, reference, start);
    obj.GetInlet(0)->SetFloat(-3.f, YSE::T_GUI);
    reference.length(1);
    Step(obj, out, reference, start);
    obj.GetInlet(0)->SetInt(64, YSE::T_GUI);
    reference.length(64);

    // The mode words, and 'mode <word>'.
    obj.GetInlet(0)->SetMessage("rms", YSE::T_GUI);
    reference.mode(movingAverage::RMS);
    Step(obj, out, reference, start);
    obj.GetInlet(0)->SetMessage("absolute", YSE::T_GUI);
    reference.mode(movingAverage::ABSOLUTE);
    Step(obj, out, reference, start);
    obj.GetInlet(0)->SetMessage("mode rms", YSE::T_GUI);
    reference.mode(movingAverage::RMS);
    Step(obj, out, reference, start);
    obj.GetInlet(0)->SetMessage("mode bipolar", YSE::T_GUI);
    reference.mode(movingAverage::BIPOLAR);
    Step(obj, out, reference, start);

    // Unknown words and NaN change nothing.
    obj.GetInlet(0)->SetMessage("loud", YSE::T_GUI);
    obj.GetInlet(0)->SetMessage("mode loud", YSE::T_GUI);
    obj.GetInlet(0)->SetFloat(std::nanf(""), YSE::T_GUI);
    Step(obj, out, reference, start);
  }

  TEST_CASE("~average: unconnected, the inlet reads as silence (#1118, #1036)") {
    BufferSink out;
    dAverage obj;
    Wire(obj, out);
    obj.GetInlet(0)->SetFloat(10.f, YSE::T_GUI); // the interval, not an input
    obj.ResetDSP();
    obj.Calculate(YSE::T_DSP);
    REQUIRE(out.received != nullptr);
    for (unsigned int i = 0; i < N; ++i)
      CHECK(out.received->getPtr()[i] == 0.f);
  }

  TEST_CASE("~average: arguments are the maximum interval and the mode (#1118)") {
    const auto check = [](const std::string& args, unsigned int capacity,
                          movingAverage::MODE mode) {
      CAPTURE(args);
      BufferSink out;
      dAverage obj;
      Wire(obj, out);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      movingAverage reference(capacity);
      reference.mode(mode);
      unsigned int start = 0;
      for (int block = 0; block < 3; ++block)
        Step(obj, out, reference, start);
      // The maximum also caps a later interval.
      obj.GetInlet(0)->SetInt(1000000, YSE::T_GUI);
      Step(obj, out, reference, start);
    };

    check("", 100, movingAverage::BIPOLAR);
    check("1000", 1000, movingAverage::BIPOLAR);
    check("1000 rms", 1000, movingAverage::RMS);
    check("16 absolute", 16, movingAverage::ABSOLUTE);
    // The mode alone keeps the default maximum; a word that is no mode is
    // skipped, a second number ignored.
    check("rms", 100, movingAverage::RMS);
    check("x 40 7 absolute", 40, movingAverage::ABSOLUTE);
    // Out of range clamps.
    check("0", 1, movingAverage::BIPOLAR);

    // A reparse back to no arguments restores the defaults.
    BufferSink out;
    dAverage obj;
    Wire(obj, out);
    obj.SetParams("8 rms");
    obj.SetParams("");
    movingAverage reference;
    unsigned int start = 0;
    Step(obj, out, reference, start);
    Step(obj, out, reference, start);
  }

  TEST_CASE("~average: the arguments survive a DumpJSON / ParseJSON round trip (#1118)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* a = src.CreateObject(YSE::OBJ::D_AVERAGE, "1000 rms");
      REQUIRE(a != nullptr);
      YSE::pHandle* b = src.CreateObject(YSE::OBJ::D_AVERAGE, "");
      REQUIRE(b != nullptr);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* a = dst.GetHandleFromID(0);
    YSE::pHandle* b = dst.GetHandleFromID(1);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(std::string(a->Type()) == "~average");
    CHECK(a->GetParams() == "1000 rms");
    CHECK(b->GetParams() == "");
  }

  TEST_CASE("~average: in a rendered patch, a centred ~phasor in rms mode, with an interval and "
            "a mode from receives, after a JSON round trip (#1118)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      // 375 Hz at 48 kHz: one cycle per block, from -0.5 up to just below 0.5.
      YSE::pHandle* osc = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* centre = src.CreateObject(YSE::OBJ::D_SUBSTRACT, "0.5");
      YSE::pHandle* avg = src.CreateObject(YSE::OBJ::D_AVERAGE, "128 rms");
      YSE::pHandle* control = src.CreateObject(YSE::OBJ::G_RECEIVE, "avg1118");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(osc != nullptr);
      REQUIRE(centre != nullptr);
      REQUIRE(avg != nullptr);
      REQUIRE(control != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(osc, 0, centre, 0);
      src.Connect(centre, 0, avg, 0);
      src.Connect(control, 0, avg, 0);
      src.Connect(avg, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);

    phasor rampRef;
    movingAverage reference(128);
    reference.mode(movingAverage::RMS);
    buffer centred(N);
    for (int block = 0; block < 8; ++block) {
      CAPTURE(block);
      if (block == 3) {
        REQUIRE(dst.PassData(32.f, "avg1118", YSE::T_GUI));
        reference.length(32);
      }
      if (block == 5) {
        REQUIRE(dst.PassData("absolute", "avg1118", YSE::T_GUI));
        reference.mode(movingAverage::ABSOLUTE);
      }
      dst.Calculate(YSE::T_DSP);
      buffer& ramp = rampRef(375.f, N);
      for (unsigned int i = 0; i < N; ++i)
        centred.getPtr()[i] = ramp.getPtr()[i] - 0.5f;
      buffer& expected = reference(centred);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], expected, 0.f));
      if (block == 2) {
        // A full window of one ramp cycle: the rms of a centred sawtooth is
        // 1 / sqrt(12).
        CHECK(dst.output[0].getPtr()[N - 1] ==
              doctest::Approx(1.0 / std::sqrt(12.0)).epsilon(1e-3));
      }
    }
  }

  TEST_CASE("~average: numbers, messages and blocks on the audio thread allocate nothing "
            "(#1118)") {
    BufferSink out;
    dAverage obj;
    Wire(obj, out);
    obj.SetParams("1000 rms");
    buffer in(N);
    Fill(in, 0);
    const std::string rms = "rms";
    const std::string absolute = "mode absolute";
    obj.ResetDSP();
    obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetFloat(20.f, YSE::T_DSP);
      obj.GetInlet(0)->SetInt(500, YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(rms, YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(absolute, YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.GetInlet(0)->SetInt(1000, YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.ResetDSP();
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    REQUIRE(out.received != nullptr);
  }

} // TEST_SUITE("patcher")

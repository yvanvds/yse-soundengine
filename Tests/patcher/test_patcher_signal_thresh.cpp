// Tests for the ~thresh patcher object (patcher/analysis/dThresh), issue #1122.
// The gate is DSP::schmittTrigger's and is tested in
// Tests/dsp/test_schmitt_trigger.cpp; these show that the signals, numbers,
// lists and arguments reach it, alone and in a rendered patch after a JSON
// round trip, bit-exact against the DSP classes on a ~phasor.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/analysis/dThresh.h"
#include "dsp/analysis/schmittTrigger.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::schmittTrigger;
using YSE::PATCHER::dThresh;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dThresh& obj, BufferSink& out) {
    obj.ConnectOutlet(out.GetInlet(0), 0);
    out.ConnectInlet(obj.GetOutlet(0), 0);
  }

  // A slow sine with a fast one on top: it crosses any level in [-1, 1]
  // several times near it.
  void Fill(buffer& b, unsigned int start) {
    for (unsigned int i = 0; i < b.getLength(); ++i) {
      const auto n = static_cast<float>(start + i);
      b.getPtr()[i] = std::sin(n * 0.03f) + 0.2f * std::sin(n * 2.1f);
    }
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~thresh: registry and shape (#1122)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_THRESH);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~thresh");
    CHECK(obj->GetInputs() == 3);
    CHECK(obj->GetOutputs() == 1);
    for (unsigned int i = 0; i < 3; ++i)
      CHECK(obj->IsDSPInput(i));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_THRESH));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::ANALYSIS);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~thresh: signals, numbers and lists against the DSP class (#1122, #1036)") {
    BufferSink out;
    dThresh obj;
    Wire(obj, out);
    schmittTrigger reference;
    buffer objIn(N);
    buffer refIn(N);
    unsigned int start = 0;

    const auto step = [&](buffer* low = nullptr, buffer* high = nullptr) {
      obj.ResetDSP(); // as the patcher does before every block
      Fill(objIn, start);
      Fill(refIn, start);
      start += N;
      obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
      if (low != nullptr) obj.GetInlet(1)->SetBuffer(low, YSE::T_GUI);
      if (high != nullptr) obj.GetInlet(2)->SetBuffer(high, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(out.received != nullptr);
      buffer l(N);
      buffer h(N);
      l = reference.low();
      h = reference.high();
      if (low != nullptr) l = *low;
      if (high != nullptr) h = *high;
      CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(refIn, l, h), 0.f));
    };

    // Default: 0 and 0, the sign of the input as 1 / 0.
    step();
    for (unsigned int i = 0; i < N; ++i)
      CHECK(out.received->getPtr()[i] == (objIn.getPtr()[i] >= 0.f ? 1.f : 0.f));

    // Numbers in the two right inlets; ints too.
    obj.GetInlet(2)->SetFloat(0.6f, YSE::T_GUI);
    reference.high(0.6f);
    step();
    obj.GetInlet(1)->SetFloat(-0.4f, YSE::T_GUI);
    reference.low(-0.4f);
    step();
    obj.GetInlet(1)->SetInt(-1, YSE::T_GUI);
    obj.GetInlet(2)->SetInt(1, YSE::T_GUI);
    reference.low(-1.f).high(1.f);
    step();

    // A list of two numbers in any inlet, also as text through a receive:
    // low, then high.
    obj.GetInlet(1)->SetList("-0.2 0.3", YSE::T_GUI);
    reference.low(-0.2f).high(0.3f);
    step();
    obj.GetInlet(0)->SetMessage("-0.5 0.5", YSE::T_GUI);
    reference.low(-0.5f).high(0.5f);
    step();
    // A bare number in a right inlet is that inlet's number.
    obj.GetInlet(2)->SetMessage("0.7", YSE::T_GUI);
    reference.high(0.7f);
    step();

    // Signals win over the numbers and are read per sample.
    buffer low(N);
    buffer high(N);
    for (unsigned int i = 0; i < N; ++i) {
      low.getPtr()[i] = -0.9f + 0.001f * static_cast<float>(i);
      high.getPtr()[i] = 0.9f - 0.001f * static_cast<float>(i);
    }
    step(&low, &high);
    // Only high connected: the low number again.
    reference.low(-0.5f);
    step(nullptr, &high);

    // Back to unconnected: the last numbers again.
    reference.low(-0.5f).high(0.7f);
    step();

    // Malformed lists, words and NaN: ignored; a list whose second token is
    // not a number sets only low.
    obj.SetMessage("abc 5", 0.f);
    obj.SetMessage("nan 5", 0.f);
    obj.SetMessage("", 0.f);
    obj.GetInlet(1)->SetFloat(std::nanf(""), YSE::T_GUI);
    obj.GetInlet(2)->SetFloat(std::nanf(""), YSE::T_GUI);
    step();
    obj.GetInlet(0)->SetList("-0.1 nan", YSE::T_GUI);
    reference.low(-0.1f);
    step();
  }

  TEST_CASE("~thresh: a number in the left inlet is the input while no signal is connected "
            "(#1122, #1036)") {
    BufferSink out;
    dThresh obj;
    Wire(obj, out);
    obj.SetParams("0.2 0.8");
    const auto block = [&]() {
      obj.ResetDSP();
      obj.Calculate(YSE::T_DSP);
      REQUIRE(out.received != nullptr);
      return out.received->getPtr()[N - 1];
    };
    CHECK(block() == 0.f); // input 0
    obj.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    CHECK(block() == 0.f); // between: holds 0
    obj.GetInlet(0)->SetFloat(0.9f, YSE::T_GUI);
    CHECK(block() == 1.f);
    obj.GetInlet(0)->SetInt(0, YSE::T_GUI);
    // 0 is at or below the low threshold: closes.
    CHECK(block() == 0.f);
    obj.GetInlet(0)->SetInt(1, YSE::T_GUI);
    CHECK(block() == 1.f);
    obj.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    CHECK(block() == 1.f); // between: holds 1, across blocks
  }

  TEST_CASE("~thresh: arguments are the low and high thresholds and survive a reparse "
            "(#1122)") {
    const auto check = [](const std::string& args, float low, float high) {
      CAPTURE(args);
      BufferSink out;
      dThresh obj;
      Wire(obj, out);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      schmittTrigger reference;
      reference.low(low).high(high);
      buffer objIn(N);
      buffer refIn(N);
      for (unsigned int block = 0; block < 3; ++block) {
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

    check("0.1 0.5", 0.1f, 0.5f);
    // A single argument is the low threshold; high stays 0 (Max's rule).
    check("0.5", 0.5f, 0.f);
    // A third number is ignored, a word skipped.
    check("-0.3 0.4 9", -0.3f, 0.4f);
    check("x -0.3 0.4", -0.3f, 0.4f);
    check("", 0.f, 0.f);
  }

  TEST_CASE("~thresh: in a rendered patch, gating a ~phasor with thresholds from arguments and "
            "receives, after a JSON round trip (#1122)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* osc = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* gate = src.CreateObject(YSE::OBJ::D_THRESH, "0.25 0.75");
      YSE::pHandle* both = src.CreateObject(YSE::OBJ::G_RECEIVE, "both1122");
      YSE::pHandle* high = src.CreateObject(YSE::OBJ::G_RECEIVE, "high1122");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(osc != nullptr);
      REQUIRE(gate != nullptr);
      REQUIRE(both != nullptr);
      REQUIRE(high != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(osc, 0, gate, 0);
      src.Connect(both, 0, gate, 1);
      src.Connect(high, 0, gate, 2);
      src.Connect(gate, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);

    phasor rampRef;
    schmittTrigger reference;
    reference.low(0.25f).high(0.75f);
    for (int block = 0; block < 8; ++block) {
      CAPTURE(block);
      if (block == 3) {
        REQUIRE(dst.PassData(0.9f, "high1122", YSE::T_GUI));
        reference.high(0.9f);
      }
      if (block == 5) {
        REQUIRE(dst.PassData("0.5 0.5", "both1122", YSE::T_GUI));
        reference.low(0.5f).high(0.5f);
      }
      dst.Calculate(YSE::T_DSP);
      buffer& expected = reference(rampRef(375.f, N));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], expected, 0.f));
      // The gate opens and closes in every block: at 375 Hz and 48 kHz a ramp
      // lasts 128 samples, at least one opening and one wrap per block.
      unsigned int ones = 0;
      for (unsigned int i = 0; i < N; ++i)
        ones += dst.output[0].getPtr()[i] == 1.f ? 1u : 0u;
      CHECK(ones > 0);
      CHECK(ones < N);
    }
  }

  TEST_CASE("~thresh: numbers, lists and blocks on the audio thread allocate nothing (#1122)") {
    BufferSink out;
    dThresh obj;
    Wire(obj, out);
    buffer in(N);
    buffer low(N);
    buffer high(N);
    in = 0.5f;
    low = -0.2f;
    high = 0.2f;
    const std::string list = "-0.3 0.3";
    const std::string number = "0.4";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetFloat(0.1f, YSE::T_DSP);
      obj.GetInlet(0)->SetInt(1, YSE::T_DSP);
      obj.GetInlet(1)->SetFloat(-0.5f, YSE::T_DSP);
      obj.GetInlet(1)->SetInt(-1, YSE::T_DSP);
      obj.GetInlet(2)->SetFloat(0.5f, YSE::T_DSP);
      obj.GetInlet(2)->SetInt(1, YSE::T_DSP);
      obj.GetInlet(2)->SetMessage(number, YSE::T_DSP);
      obj.GetInlet(1)->SetList(list, YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(list, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.GetInlet(1)->SetBuffer(&low, YSE::T_DSP);
      obj.GetInlet(2)->SetBuffer(&high, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

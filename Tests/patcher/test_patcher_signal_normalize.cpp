// Tests for the ~normalize patcher object (patcher/dynamics/dNormalize), issue
// #1111. The scaling is DSP::normalize's and is tested in
// Tests/dsp/test_normalize.cpp; these show that the signals, numbers,
// messages and argument reach it, alone and in rendered patches after a JSON
// round trip: bit-exact on a ~phasor, and a quiet ~cycle brought up to the
// amplitude.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/dynamics/dNormalize.h"
#include "dsp/modules/normalize.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::normalize;
using YSE::DSP::phasor;
using YSE::PATCHER::dNormalize;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dNormalize& obj, BufferSink& out) {
    obj.ConnectOutlet(out.GetInlet(0), 0);
    out.ConnectInlet(obj.GetOutlet(0), 0);
  }

  // Swells over the first blocks, so the peak keeps rising for a while.
  void Fill(buffer& b, unsigned int start) {
    for (unsigned int i = 0; i < b.getLength(); ++i) {
      const auto n = static_cast<float>(start + i);
      b.getPtr()[i] = (0.1f + n * 0.0005f) * std::sin(n * 0.05f) + 0.05f * std::sin(n * 1.3f);
    }
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~normalize: registry and shape (#1111)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_NORMALIZE);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~normalize");
    CHECK(obj->GetInputs() == 2);
    CHECK(obj->GetOutputs() == 1);
    for (unsigned int i = 0; i < 2; ++i)
      CHECK(obj->IsDSPInput(i));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_NORMALIZE));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::DYNAMICS);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~normalize: signals, numbers and messages against the DSP class (#1111, #1036)") {
    BufferSink out;
    dNormalize obj;
    Wire(obj, out);
    normalize reference;
    buffer objIn(N);
    buffer refIn(N);
    unsigned int start = 0;
    float amplitude = normalize::DEFAULT_AMPLITUDE;

    const auto step = [&](buffer* a = nullptr) {
      obj.ResetDSP(); // as the patcher does before every block
      Fill(objIn, start);
      Fill(refIn, start);
      start += N;
      obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
      if (a != nullptr) obj.GetInlet(1)->SetBuffer(a, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(out.received != nullptr);
      buffer as(N);
      as = amplitude;
      if (a != nullptr) as = *a;
      CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(refIn, as), 0.f));
    };

    step();
    step();
    CHECK(reference.peak() > 0.1f);

    // Numbers in the right inlet are the amplitude, floats and ints.
    obj.GetInlet(1)->SetFloat(0.25f, YSE::T_GUI);
    amplitude = 0.25f;
    step();
    obj.GetInlet(1)->SetInt(2, YSE::T_GUI);
    amplitude = 2.f;
    step();
    CHECK(reference.amplitude() == 2.f);

    // Numbers in the left inlet set the peak.
    obj.GetInlet(0)->SetFloat(5.f, YSE::T_GUI);
    reference.peak(5.f);
    step();
    CHECK(reference.peak() == 5.f); // the signal stays below it
    obj.GetInlet(0)->SetInt(3, YSE::T_GUI);
    reference.peak(3.f);
    step();
    CHECK(reference.peak() == 3.f);

    // reset, in either inlet, as a message and as text; with a number it
    // sets the peak to it.
    obj.GetInlet(0)->SetMessage("reset", YSE::T_GUI);
    reference.reset();
    step();
    obj.GetInlet(1)->SetList("reset 4", YSE::T_GUI);
    reference.peak(4.f);
    step();
    CHECK(reference.peak() == 4.f);
    obj.SetMessage("  reset   -6.5 ", 0.f);
    reference.peak(6.5f);
    step();
    CHECK(reference.peak() == 6.5f);
    obj.SetMessage("reset 0", 0.f);
    reference.reset();
    step();

    // Unknown messages and malformed numbers: ignored.
    obj.SetMessage("abc", 0.f);
    obj.SetMessage("resets", 0.f);
    obj.SetMessage("reset x", 0.f);
    obj.SetMessage("", 0.f);
    step();

    // A signal wins over the number and is read per sample.
    buffer a(N);
    for (unsigned int i = 0; i < N; ++i)
      a.getPtr()[i] = i < N / 2 ? 0.5f : -1.f;
    step(&a);
    CHECK(reference.amplitude() == -1.f);
    // Back to unconnected: the last number again.
    step();
    CHECK(reference.amplitude() == 2.f);

    // Unconnected input: silence.
    BufferSink idleOut;
    dNormalize idle;
    Wire(idle, idleOut);
    idle.SetParams("0.5");
    idle.Calculate(YSE::T_DSP);
    REQUIRE(idleOut.received != nullptr);
    for (unsigned int i = 0; i < N; ++i)
      CHECK(idleOut.received->getPtr()[i] == 0.f);
  }

  TEST_CASE("~normalize: the argument is the amplitude (#1111)") {
    const auto check = [](const std::string& args, float amplitude) {
      CAPTURE(args);
      BufferSink out;
      dNormalize obj;
      Wire(obj, out);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      normalize reference;
      reference.amplitude(amplitude);
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

    check("0.5", 0.5f);
    check("2", 2.f);
    check("-1", -1.f);
    check("", normalize::DEFAULT_AMPLITUDE);
  }

  TEST_CASE("~normalize: in a rendered patch, a ramp from ~phasor with the amplitude, the peak "
            "and reset from receives, after a JSON round trip (#1111)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* norm = src.CreateObject(YSE::OBJ::D_NORMALIZE, "0.5");
      YSE::pHandle* amount = src.CreateObject(YSE::OBJ::G_RECEIVE, "amp1111");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1111");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(norm != nullptr);
      REQUIRE(amount != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, norm, 0);
      src.Connect(amount, 0, norm, 1);
      src.Connect(cmd, 0, norm, 0);
      src.Connect(norm, 0, dac, 0);
      dump = src.DumpJSON();
    }
    CHECK(dump.find("~normalize") != std::string::npos);
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    // The same ramp, made here: ~phasor 375 at 48 kHz rises 1/128 per sample.
    phasor rampRef;
    normalize reference;
    reference.amplitude(0.5f);
    for (int block = 0; block < 10; ++block) {
      CAPTURE(block);
      if (block == 3) {
        REQUIRE(dst.PassData(0.75f, "amp1111", YSE::T_GUI));
        reference.amplitude(0.75f);
      }
      if (block == 5) {
        REQUIRE(dst.PassData(4.f, "cmd1111", YSE::T_GUI));
        reference.peak(4.f);
      }
      if (block == 7) {
        REQUIRE(dst.PassData("reset", "cmd1111", YSE::T_GUI));
        reference.reset();
      }
      dst.Calculate(YSE::T_DSP);
      buffer& expected = reference(rampRef(375.f, N));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], expected, 0.f));
    }
    // The first block by hand: each rising sample is a new peak, so the
    // output is the amplitude (0 for the first sample of the ramp).
    {
      patcherImplementation again(2, nullptr);
      again.ParseJSON(dump);
      again.Calculate(YSE::T_DSP);
      const float* y = again.output[0].getPtr();
      CHECK(y[0] == 0.f);
      CHECK(y[1] == 0.5f);
      CHECK(y[64] == 0.5f);
      CHECK(y[127] == 0.5f);
    }
  }

  TEST_CASE("~normalize: ~cycle -> ~* 0.01 -> ~normalize 0.5 brings the sine up to 0.5, after a "
            "JSON round trip (#1111)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* osc = src.CreateObject(YSE::OBJ::D_CYCLE, "375");
      YSE::pHandle* quiet = src.CreateObject(YSE::OBJ::D_MULTIPLY, "0.01");
      YSE::pHandle* norm = src.CreateObject(YSE::OBJ::D_NORMALIZE, "0.5");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(osc != nullptr);
      REQUIRE(quiet != nullptr);
      REQUIRE(norm != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(osc, 0, quiet, 0);
      src.Connect(quiet, 0, norm, 0);
      src.Connect(norm, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    // 375 Hz at 48 kHz: one cycle per block. After the first, the output is
    // the sine at 0.5 and its largest sample is 0.5.
    for (int block = 0; block < 6; ++block) {
      CAPTURE(block);
      dst.Calculate(YSE::T_DSP);
      if (block == 0) continue;
      float largest = 0.f;
      for (unsigned int i = 0; i < N; ++i)
        largest = std::fmax(largest, std::fabs(dst.output[0].getPtr()[i]));
      CHECK(largest <= 0.5f);
      CHECK(largest == doctest::Approx(0.5f).epsilon(0.001));
    }
  }

  TEST_CASE("~normalize: numbers, messages and blocks on the audio thread allocate nothing "
            "(#1111)") {
    BufferSink out;
    dNormalize obj;
    Wire(obj, out);
    buffer in(N);
    buffer a(N);
    in = 0.5f;
    a = 0.25f;
    const std::string reset = "reset";
    const std::string resetTo = "reset 2";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(1)->SetFloat(0.5f, YSE::T_DSP);
      obj.GetInlet(1)->SetInt(1, YSE::T_DSP);
      obj.GetInlet(0)->SetFloat(2.f, YSE::T_DSP);
      obj.GetInlet(0)->SetInt(1, YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(reset, YSE::T_DSP);
      obj.GetInlet(1)->SetList(resetTo, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.GetInlet(1)->SetBuffer(&a, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

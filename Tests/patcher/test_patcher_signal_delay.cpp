// Tests for the ~delay patcher object (patcher/delay/dDelay), issue #1104.
// The delay is DSP::sampleDelay's and is tested in
// Tests/dsp/test_sample_delay.cpp; these show that the numbers, signals,
// time values, ramp, maxsize, clear and arguments reach it, alone and in
// rendered patches after a JSON round trip: bit-exact on a ~phasor with
// commands through receives, a ~sig delay signal, and a ~delay 0 that lines
// up sample for sample with the undelayed signal.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/delay/dDelay.h"
#include "dsp/delay/sampleDelay.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::sampleDelay;
using YSE::PATCHER::dDelay;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dDelay& obj, BufferSink& out) {
    obj.ConnectOutlet(out.GetInlet(0), 0);
    out.ConnectInlet(obj.GetOutlet(0), 0);
  }

  // A test signal with no two equal samples in a block, so any offset shows.
  void Fill(buffer& b, unsigned int start) {
    for (unsigned int i = 0; i < b.getLength(); ++i) {
      const auto n = static_cast<float>(start + i);
      b.getPtr()[i] = std::sin(n * 0.013f) + 0.001f * n;
    }
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~delay: registry and shape, apart from .delay (#1104)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_DELAY);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~delay");
    CHECK(obj->GetInputs() == 2);
    CHECK(obj->GetOutputs() == 1);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->IsDSPInput(1));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    // The control-rate .delay is another object.
    YSE::pHandle* control = p.CreateObject(YSE::OBJ::G_DELAY);
    REQUIRE(control != nullptr);
    CHECK(std::string(control->Type()) == ".delay");

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_DELAY));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::DELAY);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~delay: numbers, time values, ramp, maxsize, clear and signals against the DSP "
            "class (#1104, #1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink out;
    dDelay obj;
    Wire(obj, out);
    sampleDelay reference;
    buffer objIn(N);
    buffer refIn(N);
    unsigned int start = 0;

    const auto step = [&](buffer* delay = nullptr) {
      obj.ResetDSP(); // as the patcher does before every block
      Fill(objIn, start);
      Fill(refIn, start);
      start += N;
      obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
      if (delay != nullptr) obj.GetInlet(1)->SetBuffer(delay, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(out.received != nullptr);
      buffer& expected = delay != nullptr ? reference(refIn, *delay) : reference(refIn);
      CHECK(TestHelpers::buffersNearlyEqual(*out.received, expected, 0.f));
    };

    // Default: no delay, the signal passes.
    step();
    CHECK(TestHelpers::buffersNearlyEqual(*out.received, objIn, 0.f));

    // A number in the right inlet, the fraction dropped; an int.
    obj.GetInlet(1)->SetFloat(17.8f, YSE::T_GUI);
    reference.delay(17.8f);
    step();
    obj.GetInlet(1)->SetInt(300, YSE::T_GUI);
    reference.delay(300.f);
    step();

    // Time values in any inlet, also as text through a receive.
    obj.GetInlet(0)->SetMessage("2 ms", YSE::T_GUI);
    reference.delayMs(2.f);
    step();
    CHECK(reference.delaySamples() == 96);
    obj.GetInlet(1)->SetList("40 samples", YSE::T_GUI);
    reference.delay(40.f);
    step();
    obj.GetInlet(0)->SetList("delay 1.5 ms", YSE::T_GUI);
    reference.delayMs(1.5f);
    step();
    obj.GetInlet(1)->SetMessage("delay 12", YSE::T_GUI);
    reference.delay(12.f);
    step();
    // A bare number in the right inlet is that inlet's number.
    obj.GetInlet(1)->SetMessage("25", YSE::T_GUI);
    reference.delay(25.f);
    step();

    // A ramp crossfades the next change over 1 ms, across blocks.
    obj.GetInlet(0)->SetMessage("ramp 1", YSE::T_GUI);
    reference.ramp(1.f);
    obj.GetInlet(1)->SetFloat(200.f, YSE::T_GUI);
    reference.delay(200.f);
    step();
    CHECK_FALSE(reference.ramping());
    obj.GetInlet(0)->SetMessage("ramp 20", YSE::T_GUI);
    reference.ramp(20.f);
    obj.GetInlet(1)->SetFloat(5.f, YSE::T_GUI);
    reference.delay(5.f);
    step();
    CHECK(reference.ramping());
    step();
    step();

    // maxsize clamps the delay and clears the line; clear silences it.
    obj.GetInlet(1)->SetMessage("maxsize 3", YSE::T_GUI);
    reference.maximumDelay(3);
    step();
    CHECK(reference.delaySamples() == 3);
    obj.GetInlet(0)->SetList("clear", YSE::T_GUI);
    reference.clear();
    step();
    obj.GetInlet(0)->SetMessage("maxsize 100000", YSE::T_GUI);
    reference.maximumDelay(100000);
    step();
    CHECK(reference.maximumDelay() == sampleDelay::DEFAULT_MEMORY);

    // A signal wins over the number and is read per sample, interpolated.
    buffer sweep(N);
    for (unsigned int i = 0; i < N; ++i)
      sweep.getPtr()[i] = 0.5f + 0.3f * static_cast<float>(i);
    step(&sweep);
    // Gone: the number again.
    step();

    // Unknown or malformed messages and non-finite numbers: ignored.
    obj.SetMessage("abc 5", 0.f);
    obj.SetMessage("nan", 0.f);
    obj.SetMessage("5 hz", 0.f);
    obj.SetMessage("5 ms 3", 0.f);
    obj.SetMessage("4n", 0.f);
    obj.SetMessage("ramp -3", 0.f);
    obj.SetMessage("ramp", 0.f);
    obj.SetMessage("maxsize", 0.f);
    obj.SetMessage("delay", 0.f);
    obj.SetMessage("", 0.f);
    obj.GetInlet(1)->SetFloat(std::nanf(""), YSE::T_GUI);
    step();
    step();

    // Unconnected input: silence.
    BufferSink idleOut;
    dDelay idle;
    Wire(idle, idleOut);
    idle.Calculate(YSE::T_DSP);
    REQUIRE(idleOut.received != nullptr);
    CHECK(idleOut.received->getPtr()[N - 1] == 0.f);
  }

  TEST_CASE("~delay: arguments are the memory, the delay and the ramp (#1104)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto check = [](const std::string& args, unsigned int memory, float delay, float ramp) {
      CAPTURE(args);
      BufferSink out;
      dDelay obj;
      Wire(obj, out);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      sampleDelay reference(memory);
      reference.delay(delay).ramp(ramp);
      buffer objIn(N);
      buffer refIn(N);
      for (unsigned int block = 0; block < 4; ++block) {
        CAPTURE(block);
        if (block == 2) {
          // A change shows the ramp.
          obj.GetInlet(1)->SetFloat(delay + 9.f, YSE::T_GUI);
          reference.delay(delay + 9.f);
        }
        obj.ResetDSP();
        Fill(objIn, block * N);
        Fill(refIn, block * N);
        obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
        obj.Calculate(YSE::T_DSP);
        REQUIRE(out.received != nullptr);
        CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(refIn), 0.f));
      }
    };

    check("1000", 1000, 0.f, 0.f);
    check("1000 700", 1000, 700.f, 0.f);
    check("1000 700 5", 1000, 700.f, 5.f);
    // The delay is clamped to the memory; a fourth number is ignored, a word
    // skipped.
    check("100 700", 100, 700.f, 0.f);
    check("2000 30 2 9", 2000, 30.f, 2.f);
    check("x 600 20", 600, 20.f, 0.f);
    check("", sampleDelay::DEFAULT_MEMORY, 0.f, 0.f);
    check("-5 3", 0, 3.f, 0.f);
  }

  TEST_CASE("~delay: in a rendered patch, delaying a ~phasor with numbers and commands from "
            "receives, after a JSON round trip (#1104)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* late = src.CreateObject(YSE::OBJ::D_DELAY, "2000 37 1");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1104");
      YSE::pHandle* time = src.CreateObject(YSE::OBJ::G_RECEIVE, "time1104");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(late != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(time != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, late, 0);
      src.Connect(cmd, 0, late, 0);
      src.Connect(time, 0, late, 1);
      src.Connect(late, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    phasor rampRef;
    sampleDelay reference(2000);
    reference.delay(37.f).ramp(1.f);
    for (int block = 0; block < 12; ++block) {
      CAPTURE(block);
      if (block == 2) {
        REQUIRE(dst.PassData(1500.f, "time1104", YSE::T_GUI));
        reference.delay(1500.f);
      }
      if (block == 5) {
        REQUIRE(dst.PassData("10 ms", "cmd1104", YSE::T_GUI));
        reference.delayMs(10.f);
      }
      if (block == 7) {
        REQUIRE(dst.PassData("ramp 0", "cmd1104", YSE::T_GUI));
        REQUIRE(dst.PassData(3, "time1104", YSE::T_GUI));
        reference.ramp(0.f).delay(3.f);
      }
      if (block == 9) {
        REQUIRE(dst.PassData("clear", "cmd1104", YSE::T_GUI));
        reference.clear();
      }
      dst.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(rampRef(375.f, N)), 0.f));
    }
  }

  TEST_CASE("~delay: a ~sig delay signal, and ~delay 0 lining up with the undelayed signal, "
            "rendered after a JSON round trip (#1104)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* time = src.CreateObject(YSE::OBJ::D_SIG, "12.5");
      YSE::pHandle* swept = src.CreateObject(YSE::OBJ::D_DELAY, "100");
      YSE::pHandle* none = src.CreateObject(YSE::OBJ::D_DELAY, "100 0");
      YSE::pHandle* diff = src.CreateObject(YSE::OBJ::D_SUBSTRACT, "");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(time != nullptr);
      REQUIRE(swept != nullptr);
      REQUIRE(none != nullptr);
      REQUIRE(diff != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, swept, 0);
      src.Connect(time, 0, swept, 1);
      src.Connect(swept, 0, dac, 0);
      // ramp - (ramp through ~delay 0): silence when nothing is late.
      src.Connect(ramp, 0, diff, 0);
      src.Connect(ramp, 0, none, 0);
      src.Connect(none, 0, diff, 1);
      src.Connect(diff, 0, dac, 1);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    phasor rampRef;
    sampleDelay reference(100);
    buffer delays(N);
    delays = 12.5f;
    for (int block = 0; block < 6; ++block) {
      CAPTURE(block);
      dst.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(rampRef(375.f, N), delays),
                                            0.f));
      const float* aligned = dst.output[1].getPtr();
      for (unsigned int i = 0; i < N; ++i)
        CHECK(aligned[i] == 0.f);
    }
  }

  TEST_CASE("~delay: numbers, messages and blocks on the audio thread allocate nothing "
            "(#1104)") {
    BufferSink out;
    dDelay obj;
    Wire(obj, out);
    buffer in(N);
    buffer delay(N);
    in = 0.5f;
    delay = 7.25f;
    const std::string ms = "3 ms";
    const std::string samples = "delay 40 samples";
    const std::string ramp = "ramp 5";
    const std::string maxsize = "maxsize 200";
    const std::string clear = "clear";
    const std::string number = "9";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(1)->SetFloat(5.f, YSE::T_DSP);
      obj.GetInlet(1)->SetInt(6, YSE::T_DSP);
      obj.GetInlet(1)->SetMessage(number, YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(ms, YSE::T_DSP);
      obj.GetInlet(0)->SetList(samples, YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(ramp, YSE::T_DSP);
      obj.GetInlet(1)->SetList(maxsize, YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(clear, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.GetInlet(1)->SetBuffer(&delay, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

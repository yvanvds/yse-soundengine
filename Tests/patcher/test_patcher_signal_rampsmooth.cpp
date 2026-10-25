// Tests for the ~rampsmooth patcher object (patcher/filters/dRampsmooth),
// issue #1100. The smoothing is DSP::rampSmooth's and is tested in
// Tests/dsp/test_ramp_smooth.cpp; these show that the signals, numbers, lists,
// ramp / rampup / rampdown and arguments reach it, alone and in rendered
// patches after a JSON round trip: bit-exact on a stepped ~phasor, and as a
// declicker on a gate switched by messages.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/filters/dRampsmooth.h"
#include "dsp/filters/rampSmooth.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::rampSmooth;
using YSE::PATCHER::dRampsmooth;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dRampsmooth& obj, BufferSink& out) {
    obj.ConnectOutlet(out.GetInlet(0), 0);
    out.ConnectInlet(obj.GetOutlet(0), 0);
  }

  // A test signal of held values, so ramps both start and run to the end,
  // and both rise and fall within a block.
  void Fill(buffer& b, unsigned int start) {
    for (unsigned int i = 0; i < b.getLength(); ++i) {
      const unsigned int held = (start + i) / 24;
      const auto n = static_cast<float>(held);
      b.getPtr()[i] = std::sin(n * 0.7f) + 0.5f * std::sin(n * 2.1f);
    }
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~rampsmooth: registry and shape (#1100)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_RAMPSMOOTH);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~rampsmooth");
    CHECK(obj->GetInputs() == 3);
    CHECK(obj->GetOutputs() == 1);
    for (unsigned int i = 0; i < 3; ++i)
      CHECK(obj->IsDSPInput(i));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_RAMPSMOOTH));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::FILTER);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~rampsmooth: signals, numbers, lists and attributes against the DSP class "
            "(#1100, #1036)") {
    BufferSink out;
    dRampsmooth obj;
    Wire(obj, out);
    rampSmooth reference;
    buffer objIn(N);
    buffer refIn(N);
    unsigned int start = 0;

    const auto step = [&](buffer* up = nullptr, buffer* down = nullptr) {
      obj.ResetDSP(); // as the patcher does before every block
      Fill(objIn, start);
      Fill(refIn, start);
      start += N;
      obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
      if (up != nullptr) obj.GetInlet(1)->SetBuffer(up, YSE::T_GUI);
      if (down != nullptr) obj.GetInlet(2)->SetBuffer(down, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(out.received != nullptr);
      buffer u(N);
      buffer d(N);
      u = reference.up();
      d = reference.down();
      if (up != nullptr) u = *up;
      if (down != nullptr) d = *down;
      CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(refIn, u, d), 0.f));
    };

    // Default: 1 and 1, the signal passes through.
    step();
    CHECK(TestHelpers::buffersNearlyEqual(*out.received, objIn, 0.f));

    // Numbers in the two right inlets; ints too.
    obj.GetInlet(1)->SetFloat(5.f, YSE::T_GUI);
    reference.up(5.f);
    step();
    obj.GetInlet(2)->SetFloat(40.f, YSE::T_GUI);
    reference.down(40.f);
    step();
    obj.GetInlet(1)->SetInt(3, YSE::T_GUI);
    obj.GetInlet(2)->SetInt(12, YSE::T_GUI);
    reference.up(3.f).down(12.f);
    step();

    // A list of numbers in any inlet, also as text through a receive: ramp
    // up, then ramp down; a single number sets ramp up.
    obj.GetInlet(0)->SetList("8 20", YSE::T_GUI);
    reference.up(8.f).down(20.f);
    step();
    obj.GetInlet(0)->SetMessage("2", YSE::T_GUI);
    reference.up(2.f);
    step();
    // A bare number in a right inlet is that inlet's number.
    obj.GetInlet(2)->SetMessage("6", YSE::T_GUI);
    reference.down(6.f);
    step();

    // Max's ramp message and attributes, in any inlet.
    obj.GetInlet(1)->SetMessage("ramp 15", YSE::T_GUI);
    reference.ramp(15.f);
    step();
    obj.GetInlet(0)->SetMessage("rampup 20", YSE::T_GUI);
    reference.up(20.f);
    step();
    obj.GetInlet(2)->SetList("rampdown 18", YSE::T_GUI);
    reference.down(18.f);
    step();

    // Signals win over the numbers and are read per sample.
    buffer up(N);
    buffer down(N);
    for (unsigned int i = 0; i < N; ++i) {
      up.getPtr()[i] = 1.f + 0.25f * static_cast<float>(i);
      down.getPtr()[i] = 40.f - 0.125f * static_cast<float>(i);
    }
    step(&up, &down);
    CHECK(reference.up() == up.getPtr()[N - 1]);
    // Only ramp down connected: the ramp-up number again.
    reference.up(20.f);
    step(nullptr, &down);

    // Back to unconnected: the last numbers again.
    reference.up(20.f).down(18.f);
    step();

    // Unknown or malformed messages and non-finite numbers: ignored.
    obj.SetMessage("reset", 0.f);
    obj.SetMessage("abc 5", 0.f);
    obj.SetMessage("nan 5", 0.f);
    obj.SetMessage("ramp", 0.f);
    obj.SetMessage("rampup nan", 0.f);
    obj.SetMessage("rampdown", 0.f);
    obj.SetMessage("", 0.f);
    obj.GetInlet(1)->SetFloat(std::nanf(""), YSE::T_GUI);
    obj.GetInlet(2)->SetFloat(std::nanf(""), YSE::T_GUI);
    step();
    step();
    // A list whose second token is not a number sets only ramp up.
    obj.GetInlet(0)->SetList("7 nan", YSE::T_GUI);
    reference.up(7.f);
    step();

    // Unconnected input: silence.
    BufferSink idleOut;
    dRampsmooth idle;
    Wire(idle, idleOut);
    idle.Calculate(YSE::T_DSP);
    REQUIRE(idleOut.received != nullptr);
    CHECK(idleOut.received->getPtr()[N - 1] == 0.f);
  }

  TEST_CASE("~rampsmooth: arguments are ramp up and ramp down (#1100)") {
    const auto check = [](const std::string& args, float up, float down) {
      CAPTURE(args);
      BufferSink out;
      dRampsmooth obj;
      Wire(obj, out);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      rampSmooth reference;
      reference.up(up).down(down);
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

    check("10", 10.f, rampSmooth::DEFAULT_RAMP);
    check("10 100", 10.f, 100.f);
    // A third number is ignored, a word skipped.
    check("10 100 3", 10.f, 100.f);
    check("x 4 50", 4.f, 50.f);
    check("", rampSmooth::DEFAULT_RAMP, rampSmooth::DEFAULT_RAMP);
  }

  TEST_CASE("~rampsmooth: in a rendered patch, smoothing a stepped ~phasor with numbers and "
            "commands from receives, after a JSON round trip (#1100)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      // A phasor quantised to eight steps: held values with jumps between.
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* scale = src.CreateObject(YSE::OBJ::D_MULTIPLY, "8");
      YSE::pHandle* steps = src.CreateObject(YSE::OBJ::D_TRUNC, "");
      YSE::pHandle* smooth = src.CreateObject(YSE::OBJ::D_RAMPSMOOTH, "4 30");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1100");
      YSE::pHandle* down = src.CreateObject(YSE::OBJ::G_RECEIVE, "down1100");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(scale != nullptr);
      REQUIRE(steps != nullptr);
      REQUIRE(smooth != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(down != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, scale, 0);
      src.Connect(scale, 0, steps, 0);
      src.Connect(steps, 0, smooth, 0);
      src.Connect(cmd, 0, smooth, 0);
      src.Connect(down, 0, smooth, 2);
      src.Connect(smooth, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    // The same chain outside the patcher: the rendered ~phasor ~* ~trunc
    // output is taken from a second patch without the smoother.
    std::string plainDump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* scale = src.CreateObject(YSE::OBJ::D_MULTIPLY, "8");
      YSE::pHandle* steps = src.CreateObject(YSE::OBJ::D_TRUNC, "");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, scale, 0);
      src.Connect(scale, 0, steps, 0);
      src.Connect(steps, 0, dac, 0);
      plainDump = src.DumpJSON();
    }
    patcherImplementation plain(2, nullptr);
    plain.ParseJSON(plainDump);

    rampSmooth reference;
    reference.up(4.f).down(30.f);
    for (int block = 0; block < 10; ++block) {
      CAPTURE(block);
      if (block == 2) {
        REQUIRE(dst.PassData(2.f, "down1100", YSE::T_GUI));
        reference.down(2.f);
      }
      if (block == 4) {
        REQUIRE(dst.PassData("ramp 50", "cmd1100", YSE::T_GUI));
        reference.ramp(50.f);
      }
      if (block == 6) {
        REQUIRE(dst.PassData("rampdown 9", "cmd1100", YSE::T_GUI));
        reference.down(9.f);
      }
      if (block == 8) {
        REQUIRE(dst.PassData("1 1", "cmd1100", YSE::T_GUI));
        reference.up(1.f).down(1.f);
      }
      dst.Calculate(YSE::T_DSP);
      plain.Calculate(YSE::T_DSP);
      buffer stepped(N);
      stepped = plain.output[0];
      // The steps really are steps: held values with jumps.
      if (block == 0) {
        const float* s = stepped.getPtr();
        CHECK(*std::max_element(s, s + N) - *std::min_element(s, s + N) >= 1.f);
      }
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(stepped), 0.f));
    }
    // At 1 and 1 the steps pass unchanged.
    CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], plain.output[0], 0.f));
  }

  TEST_CASE("~rampsmooth: declicks a gate switched by messages, after a JSON round trip "
            "(#1100)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto render = [](const char* args) {
      std::string dump;
      {
        patcherImplementation src(1, nullptr);
        YSE::pHandle* gate = src.CreateObject(YSE::OBJ::D_SIG, "0");
        YSE::pHandle* in = src.CreateObject(YSE::OBJ::G_RECEIVE, "gate1100");
        YSE::pHandle* smooth = src.CreateObject(YSE::OBJ::D_RAMPSMOOTH, args);
        YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
        REQUIRE(gate != nullptr);
        REQUIRE(in != nullptr);
        REQUIRE(smooth != nullptr);
        REQUIRE(dac != nullptr);
        src.Connect(in, 0, gate, 0);
        src.Connect(gate, 0, smooth, 0);
        src.Connect(smooth, 0, dac, 0);
        dump = src.DumpJSON();
      }
      patcherImplementation dst(1, nullptr);
      dst.ParseJSON(dump);
      dst.Calculate(YSE::T_DSP);
      // The gate opens: the largest jump between samples over the next
      // blocks, and where the output ends up.
      REQUIRE(dst.PassData(1.f, "gate1100", YSE::T_GUI));
      float previous = dst.output[0].getPtr()[N - 1];
      float largest = 0.f;
      for (int block = 0; block < 8; ++block) {
        dst.Calculate(YSE::T_DSP);
        for (unsigned int i = 0; i < N; ++i) {
          const float y = dst.output[0].getPtr()[i];
          largest = std::max(largest, std::fabs(y - previous));
          previous = y;
        }
      }
      return std::make_pair(largest, previous);
    };
    // Over 480 samples (10 ms): steps of 1/480, ending on 1.
    const auto smooth = render("480 480");
    CHECK(smooth.first <= 1.f / 480.f + 1e-6f);
    CHECK(smooth.first >= 1.f / 480.f - 1e-6f);
    CHECK(smooth.second == 1.f);
    // Without a ramp the gate clicks: one jump of 1.
    const auto click = render("");
    CHECK(click.first == 1.f);
    CHECK(click.second == 1.f);
  }

  TEST_CASE("~rampsmooth: numbers, messages and blocks on the audio thread allocate nothing "
            "(#1100)") {
    BufferSink out;
    dRampsmooth obj;
    Wire(obj, out);
    buffer in(N);
    buffer up(N);
    buffer down(N);
    in = 0.5f;
    up = 7.f;
    down = 70.f;
    const std::string list = "6 9";
    const std::string number = "9";
    const std::string attribute = "rampdown 40";
    const std::string ramp = "ramp 12";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(1)->SetFloat(5.f, YSE::T_DSP);
      obj.GetInlet(1)->SetInt(6, YSE::T_DSP);
      obj.GetInlet(2)->SetFloat(50.f, YSE::T_DSP);
      obj.GetInlet(2)->SetInt(80, YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(number, YSE::T_DSP);
      obj.GetInlet(0)->SetList(list, YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(attribute, YSE::T_DSP);
      obj.GetInlet(1)->SetList(ramp, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.GetInlet(1)->SetBuffer(&up, YSE::T_DSP);
      obj.GetInlet(2)->SetBuffer(&down, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

// Tests for the ~slide patcher object (patcher/filters/dSlide), issue #1099.
// The smoothing is DSP::slide's and is tested in Tests/dsp/test_slide.cpp;
// these show that the signals, numbers, lists, reset, slideup / slidedown and
// arguments reach it, alone and in rendered patches after a JSON round trip:
// bit-exact on a ~phasor, and as an envelope follower on a rectified ~cycle.

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
#include "patcher/filters/dSlide.h"
#include "dsp/filters/slide.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::slide;
using YSE::PATCHER::dSlide;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dSlide& obj, BufferSink& out) {
    obj.ConnectOutlet(out.GetInlet(0), 0);
    out.ConnectInlet(obj.GetOutlet(0), 0);
  }

  // A test signal: a slow sine with a fast one on top, so the output both
  // rises and falls within a block.
  void Fill(buffer& b, unsigned int start) {
    for (unsigned int i = 0; i < b.getLength(); ++i) {
      const auto n = static_cast<float>(start + i);
      b.getPtr()[i] = std::sin(n * 0.01f) + 0.5f * std::sin(n * 2.1f);
    }
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~slide: registry and shape (#1099)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_SLIDE);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~slide");
    CHECK(obj->GetInputs() == 3);
    CHECK(obj->GetOutputs() == 1);
    for (unsigned int i = 0; i < 3; ++i)
      CHECK(obj->IsDSPInput(i));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_SLIDE));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::FILTER);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~slide: signals, numbers, lists, attributes and reset against the DSP class "
            "(#1099, #1036)") {
    BufferSink out;
    dSlide obj;
    Wire(obj, out);
    slide reference;
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

    // A list of numbers in any inlet, also as text through a receive: slide
    // up, then slide down; a single number sets slide up.
    obj.GetInlet(0)->SetList("8 80", YSE::T_GUI);
    reference.up(8.f).down(80.f);
    step();
    obj.GetInlet(0)->SetMessage("2", YSE::T_GUI);
    reference.up(2.f);
    step();
    // A bare number in a right inlet is that inlet's number.
    obj.GetInlet(2)->SetMessage("6", YSE::T_GUI);
    reference.down(6.f);
    step();

    // Max's attributes as messages, in any inlet.
    obj.GetInlet(0)->SetMessage("slideup 20", YSE::T_GUI);
    reference.up(20.f);
    step();
    obj.GetInlet(1)->SetList("slidedown 200", YSE::T_GUI);
    reference.down(200.f);
    step();

    // reset in any inlet returns the output to 0 before the next block.
    obj.GetInlet(2)->SetMessage("reset", YSE::T_GUI);
    reference.reset();
    step();
    obj.GetInlet(0)->SetList("reset", YSE::T_GUI);
    reference.reset();
    step();

    // Signals win over the numbers and are read per sample.
    buffer up(N);
    buffer down(N);
    for (unsigned int i = 0; i < N; ++i) {
      up.getPtr()[i] = 1.f + 0.5f * static_cast<float>(i);
      down.getPtr()[i] = 300.f - static_cast<float>(i);
    }
    step(&up, &down);
    CHECK(reference.up() == up.getPtr()[N - 1]);
    // Only slide down connected: the slide-up number again.
    reference.up(20.f);
    step(nullptr, &down);

    // Back to unconnected: the last numbers again.
    reference.up(20.f).down(200.f);
    step();

    // Unknown or malformed messages and non-finite numbers: ignored.
    obj.SetMessage("slide 300", 0.f);
    obj.SetMessage("abc 5", 0.f);
    obj.SetMessage("nan 5", 0.f);
    obj.SetMessage("slideup", 0.f);
    obj.SetMessage("slidedown nan", 0.f);
    obj.SetMessage("", 0.f);
    obj.GetInlet(1)->SetFloat(std::nanf(""), YSE::T_GUI);
    obj.GetInlet(2)->SetFloat(std::nanf(""), YSE::T_GUI);
    step();
    step();
    // A list whose second token is not a number sets only slide up.
    obj.GetInlet(0)->SetList("7 nan", YSE::T_GUI);
    reference.up(7.f);
    step();

    // Unconnected input: silence.
    BufferSink idleOut;
    dSlide idle;
    Wire(idle, idleOut);
    idle.Calculate(YSE::T_DSP);
    REQUIRE(idleOut.received != nullptr);
    CHECK(idleOut.received->getPtr()[N - 1] == 0.f);
  }

  TEST_CASE("~slide: arguments are slide up and slide down (#1099)") {
    const auto check = [](const std::string& args, float up, float down) {
      CAPTURE(args);
      BufferSink out;
      dSlide obj;
      Wire(obj, out);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      slide reference;
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

    check("10", 10.f, slide::DEFAULT_SLIDE);
    check("10 1000", 10.f, 1000.f);
    // A third number is ignored, a word skipped.
    check("10 1000 3", 10.f, 1000.f);
    check("x 4 50", 4.f, 50.f);
    check("", slide::DEFAULT_SLIDE, slide::DEFAULT_SLIDE);
  }

  TEST_CASE("~slide: in a rendered patch, smoothing a ~phasor with numbers and commands from "
            "receives, after a JSON round trip (#1099)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* smooth = src.CreateObject(YSE::OBJ::D_SLIDE, "4 30");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1099");
      YSE::pHandle* down = src.CreateObject(YSE::OBJ::G_RECEIVE, "down1099");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(smooth != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(down != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, smooth, 0);
      src.Connect(cmd, 0, smooth, 0);
      src.Connect(down, 0, smooth, 2);
      src.Connect(smooth, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    phasor rampRef;
    slide reference;
    reference.up(4.f).down(30.f);
    for (int block = 0; block < 10; ++block) {
      CAPTURE(block);
      if (block == 2) {
        REQUIRE(dst.PassData(2.f, "down1099", YSE::T_GUI));
        reference.down(2.f);
      }
      if (block == 4) {
        REQUIRE(dst.PassData("reset", "cmd1099", YSE::T_GUI));
        reference.reset();
      }
      if (block == 6) {
        REQUIRE(dst.PassData("slideup 50", "cmd1099", YSE::T_GUI));
        reference.up(50.f);
      }
      if (block == 8) {
        REQUIRE(dst.PassData("1 1", "cmd1099", YSE::T_GUI));
        reference.up(1.f).down(1.f);
      }
      dst.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(rampRef(375.f, N)), 0.f));
    }
    // At 1 and 1 the ramp passes unchanged.
    phasor alone;
    for (int block = 0; block < 9; ++block)
      alone(375.f, N);
    CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], alone(375.f, N), 0.f));
  }

  TEST_CASE("~slide: a rectified ~cycle through a fast-rise slow-fall ~slide follows its "
            "envelope, after a JSON round trip (#1099)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto render = [](const char* args) {
      std::string dump;
      {
        patcherImplementation src(1, nullptr);
        YSE::pHandle* osc = src.CreateObject(YSE::OBJ::D_CYCLE, "2000");
        YSE::pHandle* rectify = src.CreateObject(YSE::OBJ::D_ABS, "");
        YSE::pHandle* follow = src.CreateObject(YSE::OBJ::D_SLIDE, args);
        YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
        REQUIRE(osc != nullptr);
        REQUIRE(rectify != nullptr);
        REQUIRE(follow != nullptr);
        REQUIRE(dac != nullptr);
        src.Connect(osc, 0, rectify, 0);
        src.Connect(rectify, 0, follow, 0);
        src.Connect(follow, 0, dac, 0);
        dump = src.DumpJSON();
      }
      patcherImplementation dst(1, nullptr);
      dst.ParseJSON(dump);
      // A tenth of a second to settle, then the range of the next blocks.
      for (int block = 0; block < 4800 / static_cast<int>(N); ++block)
        dst.Calculate(YSE::T_DSP);
      float lo = 2.f;
      float hi = -1.f;
      for (int block = 0; block < 8; ++block) {
        dst.Calculate(YSE::T_DSP);
        const float* y = dst.output[0].getPtr();
        lo = std::min(lo, *std::min_element(y, y + N));
        hi = std::max(hi, *std::max_element(y, y + N));
      }
      return std::make_pair(lo, hi);
    };
    // Rising at once and falling over 2000 samples: the peak, with little
    // ripple.
    const auto follower = render("1 2000");
    CHECK(follower.second <= 1.f + 1e-6f);
    CHECK(follower.first >= 0.99f);
    // Without a slide the rectified sine passes: from 0 to 1.
    const auto plain = render("");
    CHECK(plain.first < 0.05f);
    CHECK(plain.second > 0.95f);
  }

  TEST_CASE("~slide: numbers, messages and blocks on the audio thread allocate nothing "
            "(#1099)") {
    BufferSink out;
    dSlide obj;
    Wire(obj, out);
    buffer in(N);
    buffer up(N);
    buffer down(N);
    in = 0.5f;
    up = 7.f;
    down = 70.f;
    const std::string list = "6 9";
    const std::string number = "9";
    const std::string attribute = "slidedown 40";
    const std::string reset = "reset";
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
      obj.GetInlet(1)->SetList(reset, YSE::T_DSP);
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

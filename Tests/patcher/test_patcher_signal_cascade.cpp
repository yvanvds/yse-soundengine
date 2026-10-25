// Tests for the ~cascade patcher object (patcher/filters/dCascade), issue
// #1091. The filtering is DSP::biquadCascade's and is tested in
// Tests/dsp/test_biquad_cascade.cpp; these show that the lists, clear,
// bypass, zero, smooth and arguments (the number of sections included) reach
// it, in either inlet, alone, from another thread, and in a rendered patch
// after a JSON round trip.

#include <doctest/doctest.h>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/filters/dCascade.h"
#include "dsp/filters/biquadCascade.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::biquadCascade;
using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::PATCHER::dCascade;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dCascade& obj, BufferSink& out) {
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

  void Set(biquadCascade& fn, const std::vector<float>& c) {
    fn.coefficients(c.data(), static_cast<unsigned int>(c.size()));
  }

  bool Silent(buffer& b) {
    for (unsigned int i = 0; i < b.getLength(); ++i)
      if (b.getPtr()[i] != 0.f) return false;
    return true;
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~cascade: registry and shape (#1091)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_CASCADE);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~cascade");
    CHECK(obj->GetInputs() == 2);
    CHECK(obj->GetOutputs() == 1);
    CHECK(obj->IsDSPInput(0));
    CHECK_FALSE(obj->IsDSPInput(1));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_CASCADE));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::FILTER);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~cascade: lists, clear, bypass, zero and smooth in either inlet against the DSP "
            "class (#1091, #1036)") {
    BufferSink out;
    dCascade obj;
    Wire(obj, out);
    biquadCascade reference;
    buffer objIn(N);
    buffer refIn(N);
    unsigned int start = 0;

    const auto step = [&]() {
      obj.ResetDSP(); // as the patcher does before every block
      Fill(objIn, start);
      Fill(refIn, start);
      start += N;
      obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(out.received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(refIn), 0.f));
    };

    // Default: no sections, the input passes.
    step();
    Fill(refIn, 0);
    CHECK(out.received->getPtr()[N - 1] == refIn.getPtr()[N - 1]);

    // A list of two sections in the right inlet, then three in the left.
    const std::vector<float> two = {0.2f, 0.4f, 0.2f, -0.6f, 0.2f, 0.5f, 0.f, 0.f, -0.5f, 0.f};
    obj.GetInlet(1)->SetList("0.2 0.4 0.2 -0.6 0.2 0.5 0 0 -0.5 0", YSE::T_GUI);
    Set(reference, two);
    step();
    CHECK(out.received->getPtr()[N - 1] != refIn.getPtr()[N - 1]);
    std::vector<float> three = two;
    three.insert(three.end(), {0.9f, -0.3f, 0.1f, -0.4f, 0.5f});
    obj.GetInlet(0)->SetList("0.2 0.4 0.2 -0.6 0.2 0.5 0 0 -0.5 0 0.9 -0.3 0.1 -0.4 0.5",
                             YSE::T_GUI);
    Set(reference, three);
    step();
    step();

    // As text through a receive (a message); an incomplete trailing set is
    // ignored, a list without one complete set changes nothing.
    obj.GetInlet(1)->SetMessage("1 0 0 -0.5 0 7 7", YSE::T_GUI);
    Set(reference, {1.f, 0.f, 0.f, -0.5f, 0.f});
    step();
    obj.GetInlet(0)->SetMessage("0.5 0.5", YSE::T_GUI);
    obj.GetInlet(1)->SetMessage("0.5", YSE::T_GUI);
    step();

    // smooth 0: changes apply at once; smooth 1 again.
    obj.GetInlet(1)->SetMessage("smooth 0", YSE::T_GUI);
    reference.smooth(false);
    obj.GetInlet(0)->SetList("0.3 0 0 0.2 0", YSE::T_GUI);
    Set(reference, {0.3f, 0.f, 0.f, 0.2f, 0.f});
    step();
    obj.GetInlet(0)->SetList("smooth 1", YSE::T_GUI);
    reference.smooth(true);
    obj.GetInlet(1)->SetList("0.6 0.1 0 0.1 0", YSE::T_GUI);
    Set(reference, {0.6f, 0.1f, 0.f, 0.1f, 0.f});
    step();

    // bypass passes the input, zero silences; bypass wins.
    obj.GetInlet(1)->SetMessage("bypass 1", YSE::T_GUI);
    reference.bypass(true);
    step();
    Fill(refIn, start - N);
    CHECK(TestHelpers::buffersNearlyEqual(*out.received, refIn, 0.f));
    obj.GetInlet(0)->SetMessage("zero 1", YSE::T_GUI);
    reference.zero(true);
    step();
    obj.GetInlet(0)->SetList("bypass 0", YSE::T_GUI);
    reference.bypass(false);
    step();
    CHECK(Silent(*out.received));
    obj.GetInlet(1)->SetList("zero 0", YSE::T_GUI);
    reference.zero(false);
    step();

    // clear in either inlet silences; the next list applies at once.
    obj.GetInlet(1)->SetMessage("clear", YSE::T_GUI);
    reference.clear();
    step();
    CHECK(Silent(*out.received));
    obj.GetInlet(0)->SetList("0.2 0.4 0.2 -0.6 0.2 0.5 0 0 -0.5 0 0.9 -0.3 0.1 -0.4 0.5",
                             YSE::T_GUI);
    Set(reference, three);
    step();
    // A list then clear before the block: silent. clear then a list: the list.
    obj.GetInlet(0)->SetList("0.5 0 0 0 0", YSE::T_GUI);
    obj.GetInlet(0)->SetList("clear", YSE::T_GUI);
    reference.clear();
    step();
    CHECK(Silent(*out.received));
    obj.GetInlet(1)->SetMessage("clear", YSE::T_GUI);
    obj.GetInlet(1)->SetMessage("0.5 0 0 0 0", YSE::T_GUI);
    reference.clear();
    Set(reference, {0.5f, 0.f, 0.f, 0.f, 0.f});
    step();
    Fill(refIn, start - N);
    CHECK(out.received->getPtr()[5] == 0.5f * refIn.getPtr()[5]);

    // Unknown or malformed messages and non-finite numbers: ignored.
    obj.SetMessage("smooth", 0.f);
    obj.SetMessage("bypass abc", 0.f);
    obj.SetMessage("zero", 0.f);
    obj.SetMessage("dictionary lowpass", 0.f);
    obj.SetMessage("nan 1 1 1 1 1", 0.f);
    obj.SetMessage("", 0.f);
    obj.GetInlet(1)->SetFloat(0.5f, YSE::T_GUI);
    obj.GetInlet(1)->SetInt(2, YSE::T_GUI);
    step();
    step();
    CHECK(obj.Dropped() == 0);

    // Unconnected input: silence.
    BufferSink idleOut;
    dCascade idle;
    Wire(idle, idleOut);
    idle.GetInlet(1)->SetList("1 0 0 -0.5 0", YSE::T_GUI);
    idle.Calculate(YSE::T_DSP);
    REQUIRE(idleOut.received != nullptr);
    CHECK(Silent(*idleOut.received));
  }

  TEST_CASE("~cascade: arguments are the room, the starting coefficients and smooth (#1091)") {
    const auto check = [](const std::string& args, unsigned int capacity,
                          const std::vector<float>& coefficients, bool smooth) {
      CAPTURE(args);
      BufferSink out;
      dCascade obj;
      Wire(obj, out);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      CHECK(obj.Capacity() == capacity);
      // The arguments are the starting coefficients, from the first sample.
      biquadCascade reference(capacity);
      reference.smooth(smooth);
      Set(reference, coefficients);
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
      // The smoothing argument holds for later lists.
      obj.GetInlet(1)->SetList("0.25 0 0 0 0", YSE::T_GUI);
      reference.coefficients(std::vector<float>{0.25f, 0.f, 0.f, 0.f, 0.f}.data(), 5);
      obj.ResetDSP();
      Fill(objIn, 3 * N);
      Fill(refIn, 3 * N);
      obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(refIn), 0.f));
    };

    check("", 16, {}, true);
    check("4", 4, {}, true);
    check("2 0.5 0 0 -0.5 0 1 0.2 0 0.3 0", 2,
          {0.5f, 0.f, 0.f, -0.5f, 0.f, 1.f, 0.2f, 0.f, 0.3f, 0.f}, true);
    // More sets than room: the room grows to fit; an incomplete set is ignored.
    check("1 0.5 0 0 -0.5 0 1 0.2 0 0.3 0 9", 2,
          {0.5f, 0.f, 0.f, -0.5f, 0.f, 1.f, 0.2f, 0.f, 0.3f, 0.f}, true);
    check("smooth 0 3 0.3 0 0 0.4 0", 3, {0.3f, 0.f, 0.f, 0.4f, 0.f}, false);
    check("3 0.3 0 0 0.4 0 smooth 0", 3, {0.3f, 0.f, 0.f, 0.4f, 0.f}, false);
    // Clamped to 1 .. 64.
    check("0", 1, {}, true);
    check("1000", 64, {}, true);
    check("smooth", 16, {}, true);

    // Re-setting the parameters rebuilds the room and forgets the old list.
    BufferSink out;
    dCascade obj;
    Wire(obj, out);
    obj.SetParams("8 0.5 0 0 0 0");
    CHECK(obj.Capacity() == 8);
    obj.SetParams("");
    CHECK(obj.Capacity() == 16);
    buffer in(N);
    Fill(in, 0);
    obj.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    REQUIRE(out.received != nullptr);
    buffer expected(N);
    Fill(expected, 0);
    CHECK(TestHelpers::buffersNearlyEqual(*out.received, expected, 0.f));
  }

  TEST_CASE("~cascade: a list written on another thread is applied whole or not yet, never "
            "torn (#1091)") {
    BufferSink out;
    dCascade obj;
    Wire(obj, out);
    obj.SetParams("8 smooth 0");
    // Eight gain-only sections, all 1 or all 2: the output of a constant 1 is
    // 1 or 256, and any mix of the two lists would be a power of two between.
    std::string ones;
    std::string twos;
    for (int i = 0; i < 8; ++i) {
      ones += "1 0 0 0 0 ";
      twos += "2 0 0 0 0 ";
    }
    std::atomic<bool> done{false};
    std::thread writer([&] {
      for (int i = 0; i < 4000; ++i)
        obj.GetInlet(1)->SetList((i % 2) != 0 ? twos : ones, YSE::T_GUI);
      done = true;
    });
    buffer in(N);
    in = 1.f;
    int blocks = 0;
    bool consistent = true;
    while (!done.load() || blocks < 10) {
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(out.received != nullptr);
      const float first = out.received->getPtr()[0];
      for (unsigned int i = 0; i < N; ++i) {
        const float v = out.received->getPtr()[i];
        if (v != first || (v != 1.f && v != 256.f)) consistent = false;
      }
      ++blocks;
    }
    writer.join();
    CHECK(consistent);
    // One writer: nothing dropped.
    CHECK(obj.Dropped() == 0);
  }

  TEST_CASE("~cascade: in a rendered patch, filtering a ~phasor with lists and commands from a "
            "receive, after a JSON round trip (#1091)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* filter = src.CreateObject(YSE::OBJ::D_CASCADE, "2 0.5 0 0 -0.5 0");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1091");
      YSE::pHandle* list = src.CreateObject(YSE::OBJ::G_RECEIVE, "list1091");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(filter != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(list != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, filter, 0);
      src.Connect(cmd, 0, filter, 0);
      src.Connect(list, 0, filter, 1);
      src.Connect(filter, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    phasor rampRef;
    biquadCascade reference(2);
    Set(reference, {0.5f, 0.f, 0.f, -0.5f, 0.f});
    for (int block = 0; block < 14; ++block) {
      CAPTURE(block);
      if (block == 2) {
        // Two sections: a delay of one sample times 0.5, then of two times 3.
        REQUIRE(dst.PassData("0 0.5 0 0 0 0 0 3 0 0", "list1091", YSE::T_GUI));
        Set(reference, {0.f, 0.5f, 0.f, 0.f, 0.f, 0.f, 0.f, 3.f, 0.f, 0.f});
      }
      if (block == 4) {
        REQUIRE(dst.PassData("bypass 1", "cmd1091", YSE::T_GUI));
        reference.bypass(true);
      }
      if (block == 5) {
        REQUIRE(dst.PassData("bypass 0", "cmd1091", YSE::T_GUI));
        REQUIRE(dst.PassData("zero 1", "cmd1091", YSE::T_GUI));
        reference.bypass(false).zero(true);
      }
      if (block == 6) {
        REQUIRE(dst.PassData("zero 0", "cmd1091", YSE::T_GUI));
        REQUIRE(dst.PassData("clear", "cmd1091", YSE::T_GUI));
        reference.zero(false).clear();
      }
      if (block == 8) {
        REQUIRE(dst.PassData("smooth 0", "cmd1091", YSE::T_GUI));
        REQUIRE(dst.PassData("0 0 1 0 0 1 0 0 0 0", "cmd1091", YSE::T_GUI));
        reference.smooth(false);
        Set(reference, {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f});
      }
      buffer& ramped = rampRef(375.f, N);
      std::vector<float> dry(ramped.getPtr(), ramped.getPtr() + N);
      dst.Calculate(YSE::T_DSP);
      const float* y = dst.output[0].getPtr();
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(ramped), 0.f));
      // Closed forms. Block 3: the ramp delayed by three samples, times 1.5
      // (the smoothed move is over after block 2).
      if (block == 3) {
        for (unsigned int i = 3; i < N; ++i) {
          CAPTURE(i);
          CHECK(y[i] == doctest::Approx(1.5f * dry[i - 3]));
        }
      }
      if (block == 4) {
        for (unsigned int i = 0; i < N; ++i)
          CHECK(y[i] == dry[i]);
      }
      if (block == 5 || block == 6 || block == 7) {
        for (unsigned int i = 0; i < N; ++i)
          CHECK(y[i] == 0.f);
      }
      // A two-sample delay, from silence, applied at once.
      if (block == 8) {
        CHECK(y[0] == 0.f);
        CHECK(y[1] == 0.f);
        for (unsigned int i = 2; i < N; ++i)
          CHECK(y[i] == dry[i - 2]);
      }
    }
  }

  TEST_CASE("~cascade: lists, messages and blocks on the audio thread allocate nothing "
            "(#1091)") {
    BufferSink out;
    dCascade obj;
    Wire(obj, out);
    buffer in(N);
    in = 0.5f;
    const std::string list = "0.5 0.1 0.1 -0.2 0.1 0.4 0 0 -0.3 0";
    const std::string smooth = "smooth 0";
    const std::string bypass = "bypass 1";
    const std::string zero = "zero 1";
    const std::string clear = "clear";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(1)->SetList(list, YSE::T_DSP);
      obj.GetInlet(0)->SetList(list, YSE::T_DSP);
      obj.GetInlet(1)->SetMessage(smooth, YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(bypass, YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(zero, YSE::T_DSP);
      obj.GetInlet(1)->SetList(clear, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.GetInlet(1)->SetList(list, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

// Tests for the ~biquad patcher object (patcher/filters/dBiquad), issue #1090.
// The filtering is the coefficient path of DSP::biQuad and is tested in
// Tests/dsp/test_biquad_coefficients.cpp; these show that the signals,
// numbers, lists, smooth, stoke, clear and arguments reach it, alone and in a
// rendered patch after a JSON round trip.

#include <doctest/doctest.h>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/filters/dBiquad.h"
#include "dsp/filters.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::biQuad;
using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::PATCHER::dBiquad;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dBiquad& obj, BufferSink& out) {
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

  using Numbers = std::array<float, 5>;
  using Signals = std::array<buffer*, 5>;

  // One block of the reference as the wrapper drives it: the numbers of the
  // unconnected coefficients, then the block with the connected ones.
  buffer& Reference(biQuad& ref, buffer& in, const Numbers& numbers, const Signals& signals) {
    const float none = std::numeric_limits<float>::quiet_NaN();
    Numbers scalars{};
    for (int i = 0; i < 5; ++i)
      scalars[i] = signals[i] == nullptr ? numbers[i] : none;
    ref.coefficients(scalars[0], scalars[1], scalars[2], scalars[3], scalars[4]);
    return ref(in, signals[0], signals[1], signals[2], signals[3], signals[4]);
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~biquad: registry and shape (#1090)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_BIQUAD);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~biquad");
    CHECK(obj->GetInputs() == 6);
    CHECK(obj->GetOutputs() == 1);
    for (unsigned int i = 0; i < 6; ++i)
      CHECK(obj->IsDSPInput(i));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_BIQUAD));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::FILTER);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~biquad: signals, numbers, lists, smooth, stoke and clear against the DSP class "
            "(#1090, #1036)") {
    BufferSink out;
    dBiquad obj;
    Wire(obj, out);
    biQuad reference;
    Numbers numbers{0.f, 0.f, 0.f, 0.f, 0.f};
    buffer objIn(N);
    buffer refIn(N);
    unsigned int start = 0;

    const auto step = [&](Signals signals = {nullptr, nullptr, nullptr, nullptr, nullptr}) {
      obj.ResetDSP(); // as the patcher does before every block
      Fill(objIn, start);
      Fill(refIn, start);
      start += N;
      obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
      for (int i = 0; i < 5; ++i)
        if (signals[i] != nullptr) obj.GetInlet(i + 1)->SetBuffer(signals[i], YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(out.received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*out.received,
                                            Reference(reference, refIn, numbers, signals), 0.f));
    };

    // Default: all coefficients 0, nothing through.
    step();
    CHECK(out.received->getPtr()[N - 1] == 0.f);

    // Numbers in the five right inlets (smoothed), ints too.
    obj.GetInlet(1)->SetFloat(0.5f, YSE::T_GUI);
    obj.GetInlet(4)->SetFloat(-0.5f, YSE::T_GUI);
    numbers[0] = 0.5f;
    numbers[3] = -0.5f;
    step();
    CHECK(out.received->getPtr()[N - 1] != 0.f);
    obj.GetInlet(2)->SetFloat(0.25f, YSE::T_GUI);
    obj.GetInlet(3)->SetFloat(0.125f, YSE::T_GUI);
    obj.GetInlet(5)->SetFloat(0.3f, YSE::T_GUI);
    numbers[1] = 0.25f;
    numbers[2] = 0.125f;
    numbers[4] = 0.3f;
    step();
    obj.GetInlet(1)->SetInt(1, YSE::T_GUI);
    obj.GetInlet(5)->SetInt(0, YSE::T_GUI);
    numbers[0] = 1.f;
    numbers[4] = 0.f;
    step();

    // A list of numbers in any inlet, also as text through a receive; a
    // shorter one sets the first ones only.
    obj.GetInlet(0)->SetList("0.2 0.4 0.2 -0.6 0.2", YSE::T_GUI);
    numbers = {0.2f, 0.4f, 0.2f, -0.6f, 0.2f};
    step();
    obj.GetInlet(3)->SetMessage("0.3 0.1", YSE::T_GUI);
    numbers[0] = 0.3f;
    numbers[1] = 0.1f;
    step();
    obj.GetInlet(0)->SetMessage("0.9", YSE::T_GUI);
    numbers[0] = 0.9f;
    step();

    // smooth 0: changes apply at once; smooth 1 again.
    obj.GetInlet(2)->SetMessage("smooth 0", YSE::T_GUI);
    reference.smooth(false);
    obj.GetInlet(0)->SetList("0.5 0 0 0.5 0", YSE::T_GUI);
    numbers = {0.5f, 0.f, 0.f, 0.5f, 0.f};
    step();
    obj.GetInlet(0)->SetList("smooth 1", YSE::T_GUI);
    reference.smooth(true);
    obj.GetInlet(1)->SetFloat(0.7f, YSE::T_GUI);
    numbers[0] = 0.7f;
    step();

    // stoke preloads the state; clear in any inlet resets it.
    obj.GetInlet(0)->SetMessage("stoke 0.5 -0.25 0.1 0.2", YSE::T_GUI);
    reference.stoke(0.5f, -0.25f, 0.1f, 0.2f);
    step();
    obj.GetInlet(5)->SetList("stoke 0.3", YSE::T_GUI);
    reference.stoke(0.3f);
    step();
    obj.GetInlet(4)->SetMessage("clear", YSE::T_GUI);
    reference.clear();
    step();
    obj.GetInlet(0)->SetList("clear", YSE::T_GUI);
    reference.clear();
    step();

    // Signals win over the numbers and are read per sample.
    buffer a0(N);
    buffer b1(N);
    for (unsigned int i = 0; i < N; ++i) {
      a0.getPtr()[i] = 0.2f + 0.003f * static_cast<float>(i);
      b1.getPtr()[i] = -0.8f + 0.005f * static_cast<float>(i);
    }
    step({&a0, nullptr, nullptr, &b1, nullptr});
    CHECK(reference.coefficients().a0 == a0.getPtr()[N - 1]);
    // Only b1 connected: a0's number again, moved to from the signal's last
    // value.
    step({nullptr, nullptr, nullptr, &b1, nullptr});
    // Back to unconnected: the last numbers again.
    step();
    step();

    // Unknown or malformed messages and non-finite numbers: ignored.
    obj.SetMessage("smooth", 0.f);
    obj.SetMessage("smooth abc", 0.f);
    obj.SetMessage("dictionary lowpass", 0.f);
    obj.SetMessage("a0 5", 0.f);
    obj.SetMessage("nan 5 5", 0.f);
    obj.SetMessage("", 0.f);
    for (int i = 1; i <= 5; ++i)
      obj.GetInlet(i)->SetFloat(std::nanf(""), YSE::T_GUI);
    step();
    step();

    // Unconnected input: silence.
    BufferSink idleOut;
    dBiquad idle;
    Wire(idle, idleOut);
    idle.GetInlet(1)->SetFloat(1.f, YSE::T_GUI);
    idle.GetInlet(4)->SetFloat(-0.5f, YSE::T_GUI);
    idle.Calculate(YSE::T_DSP);
    REQUIRE(idleOut.received != nullptr);
    CHECK(idleOut.received->getPtr()[N - 1] == 0.f);
  }

  TEST_CASE("~biquad: arguments are the five coefficients and smooth (#1090)") {
    const auto check = [](const std::string& args, const Numbers& numbers, bool smooth) {
      CAPTURE(args);
      BufferSink out;
      dBiquad obj;
      Wire(obj, out);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      biQuad reference;
      reference.smooth(smooth);
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
        CHECK(TestHelpers::buffersNearlyEqual(
            *out.received,
            Reference(reference, refIn, numbers, {nullptr, nullptr, nullptr, nullptr, nullptr}),
            0.f));
      }
      // The arguments are the starting coefficients: no ramp from 0.
      biQuad fresh;
      fresh.coefficients(numbers[0], numbers[1], numbers[2], numbers[3], numbers[4]);
      buffer first(N);
      Fill(first, 0);
      dBiquad again;
      BufferSink againOut;
      Wire(again, againOut);
      again.SetParams(args);
      again.GetInlet(0)->SetBuffer(&first, YSE::T_GUI);
      again.Calculate(YSE::T_DSP);
      REQUIRE(againOut.received != nullptr);
      buffer firstRef(N);
      Fill(firstRef, 0);
      CHECK(TestHelpers::buffersNearlyEqual(*againOut.received, fresh(firstRef), 0.f));
    };

    check("0.5 0 0 -0.5 0", {0.5f, 0.f, 0.f, -0.5f, 0.f}, true);
    check("1 0.5", {1.f, 0.5f, 0.f, 0.f, 0.f}, true);
    // A sixth number is ignored; smooth anywhere.
    check("0.2 0.4 0.2 -0.6 0.2 9", {0.2f, 0.4f, 0.2f, -0.6f, 0.2f}, true);
    check("smooth 0 0.3 0 0 0.4 0", {0.3f, 0.f, 0.f, 0.4f, 0.f}, false);
    check("0.3 0 0 0.4 0 smooth 0", {0.3f, 0.f, 0.f, 0.4f, 0.f}, false);
    // smooth without a number is skipped.
    check("smooth", {0.f, 0.f, 0.f, 0.f, 0.f}, true);
    check("", {0.f, 0.f, 0.f, 0.f, 0.f}, true);
  }

  TEST_CASE("~biquad: in a rendered patch, filtering a ~phasor with coefficients and commands "
            "from receives, after a JSON round trip (#1090)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* filter = src.CreateObject(YSE::OBJ::D_BIQUAD, "0.5 0 0 -0.5 0");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1090");
      YSE::pHandle* b2 = src.CreateObject(YSE::OBJ::G_RECEIVE, "b21090");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(filter != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(b2 != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, filter, 0);
      src.Connect(cmd, 0, filter, 0);
      src.Connect(b2, 0, filter, 5);
      src.Connect(filter, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    phasor rampRef;
    biQuad reference;
    Numbers numbers{0.5f, 0.f, 0.f, -0.5f, 0.f};
    const Signals none{nullptr, nullptr, nullptr, nullptr, nullptr};
    for (int block = 0; block < 14; ++block) {
      CAPTURE(block);
      if (block == 2) {
        REQUIRE(dst.PassData(0.2f, "b21090", YSE::T_GUI));
        numbers[4] = 0.2f;
      }
      if (block == 4) {
        REQUIRE(dst.PassData("0.1 0.2 0.1 -0.9 0.4", "cmd1090", YSE::T_GUI));
        numbers = {0.1f, 0.2f, 0.1f, -0.9f, 0.4f};
      }
      if (block == 6) {
        REQUIRE(dst.PassData("clear", "cmd1090", YSE::T_GUI));
        reference.clear();
      }
      if (block == 8) {
        REQUIRE(dst.PassData("smooth 0", "cmd1090", YSE::T_GUI));
        REQUIRE(dst.PassData("0 0 0 0 0", "cmd1090", YSE::T_GUI));
        reference.smooth(false);
        numbers = {0.f, 0.f, 0.f, 0.f, 0.f};
      }
      if (block == 10) {
        // A ringing oscillator: poles on the unit circle, preloaded.
        REQUIRE(dst.PassData("0 0 0 -1.9 1", "cmd1090", YSE::T_GUI));
        REQUIRE(dst.PassData("stoke 0.5", "cmd1090", YSE::T_GUI));
        numbers = {0.f, 0.f, 0.f, -1.9f, 1.f};
        reference.stoke(0.5f);
      }
      dst.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(
          dst.output[0], Reference(reference, rampRef(375.f, N), numbers, none), 0.f));
      // Closed form: from block 6 the state starts at 0 and the ramp's first
      // sample is 0 (6 * 128 mod 128), so the output starts at 0.
      if (block == 6) CHECK(dst.output[0].getPtr()[0] == 0.f);
      // All coefficients 0, unsmoothed: nothing through.
      if (block == 8 || block == 9) {
        for (unsigned int i = 0; i < N; ++i) {
          CAPTURE(i);
          CHECK(dst.output[0].getPtr()[i] == 0.f);
        }
      }
      // Ringing with no input path: y(n) = 1.9 y(n-1) - y(n-2) from
      // y(-1) = 0.5, y(-2) = 0.
      if (block == 10) {
        CHECK(dst.output[0].getPtr()[0] == doctest::Approx(0.95f));
        CHECK(dst.output[0].getPtr()[1] == doctest::Approx(1.9f * 0.95f - 0.5f));
      }
      if (block >= 10) {
        float peak = 0.f;
        for (unsigned int i = 0; i < N; ++i)
          peak = std::max(peak, std::abs(dst.output[0].getPtr()[i]));
        CHECK(peak > 0.5f);
        CHECK(peak < 2.f);
      }
    }
  }

  TEST_CASE("~biquad: numbers, messages and blocks on the audio thread allocate nothing "
            "(#1090)") {
    BufferSink out;
    dBiquad obj;
    Wire(obj, out);
    buffer in(N);
    buffer a0(N);
    buffer b1(N);
    in = 0.5f;
    a0 = 0.5f;
    b1 = -0.3f;
    const std::string list = "0.5 0.1 0.1 -0.2 0.1";
    const std::string smooth = "smooth 0";
    const std::string stoke = "stoke 0.1 0.2 0.3 0.4";
    const std::string clear = "clear";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      for (int i = 1; i <= 5; ++i) {
        obj.GetInlet(i)->SetFloat(0.1f * static_cast<float>(i), YSE::T_DSP);
        obj.GetInlet(i)->SetInt(0, YSE::T_DSP);
      }
      obj.GetInlet(0)->SetList(list, YSE::T_DSP);
      obj.GetInlet(3)->SetMessage(smooth, YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(stoke, YSE::T_DSP);
      obj.GetInlet(1)->SetList(clear, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.GetInlet(1)->SetBuffer(&a0, YSE::T_DSP);
      obj.GetInlet(4)->SetBuffer(&b1, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

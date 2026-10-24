// Tests for the ~count patcher object (patcher/generatorObjects/dCount),
// issue #1066. The counting is DSP::sampleCounter's and is tested in
// Tests/dsp/test_sample_counter.cpp; these show that numbers, messages, a
// list, arguments and an enable signal reach it, alone and in a rendered
// patch (controlled through receives) after a JSON round trip.

#include <doctest/doctest.h>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/generatorObjects/dCount.h"
#include "dsp/generators/sampleCounter.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::sampleCounter;
using YSE::PATCHER::dCount;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // Both outlets of `obj` into the two sinks.
  void Wire(dCount& obj, BufferSink& count, BufferSink& carry) {
    TestHelpers::Wire(obj, 0, count, 0);
    TestHelpers::Wire(obj, 1, carry, 0);
  }

  bool Constant(buffer& block, float value) {
    if (block.getLength() != N) return false;
    for (unsigned int i = 0; i < block.getLength(); ++i)
      if (block.getPtr()[i] != value) return false;
    return true;
  }

  // Every sample of `block` is `first` + its index.
  bool Ramp(buffer& block, float first) {
    if (block.getLength() != N) return false;
    for (unsigned int i = 0; i < block.getLength(); ++i)
      if (block.getPtr()[i] != first + static_cast<float>(i)) return false;
    return true;
  }

  void CheckHead(buffer& block, const std::vector<float>& expected) {
    REQUIRE(block.getLength() >= expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
      CAPTURE(i);
      CHECK(block.getPtr()[i] == expected[i]);
    }
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~count: registry and shape (#1066)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* count = p.CreateObject(YSE::OBJ::D_COUNT);
    REQUIRE(count != nullptr);
    CHECK(std::string(count->Type()) == "~count");
    CHECK(count->GetInputs() == 2);
    CHECK(count->GetOutputs() == 2);
    CHECK(count->IsDSPInput(0));
    CHECK_FALSE(count->IsDSPInput(1));
    CHECK(count->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(count->OutputDataType(1) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::D_COUNT));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::TIME);
    CHECK(obj->IsDSPObject());
    CHECK(obj->GetParamCount() == 3);
  }

  TEST_CASE("~count: bang, int, float, min / set / stop and the limit inlet (#1066)") {
    BufferSink count;
    BufferSink carry;
    dCount obj;
    Wire(obj, count, carry);
    // Stopped by default, at 0.
    obj.Calculate(YSE::T_DSP);
    REQUIRE(count.received != nullptr);
    REQUIRE(carry.received != nullptr);
    CHECK(Constant(*count.received, 0.f));
    CHECK(Constant(*carry.received, 0.f));

    // bang: count from the initial value, on across blocks.
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CHECK(Ramp(*count.received, 0.f));
    obj.Calculate(YSE::T_DSP);
    CHECK(Ramp(*count.received, static_cast<float>(N)));

    // int: a new initial value, restarted from.
    obj.GetInlet(0)->SetInt(100, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CHECK(Ramp(*count.received, 100.f));

    // float: a new initial value without a restart (truncated).
    obj.GetInlet(0)->SetFloat(5.75f, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CHECK(Ramp(*count.received, 100.f + static_cast<float>(N)));

    // stop: back to the initial value.
    obj.SetMessage("stop", 0.f);
    obj.Calculate(YSE::T_DSP);
    CHECK(Constant(*count.received, 5.f));
    obj.SetMessage("set 7", 0.f);
    obj.Calculate(YSE::T_DSP);
    CHECK(Constant(*count.received, 7.f));
    obj.SetMessage("min 8", 0.f);
    obj.Calculate(YSE::T_DSP);
    CHECK(Constant(*count.received, 8.f));

    // The limit: an int, and a float without a restart.
    obj.GetInlet(1)->SetInt(11, YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CheckHead(*count.received, {8.f, 9.f, 10.f, 8.f, 9.f, 10.f, 8.f});
    CheckHead(*carry.received, {0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 2.f});
    obj.GetInlet(1)->SetFloat(0.f, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    const float next = count.received->getPtr()[0];
    CHECK(Ramp(*count.received, next));

    // Not a number, another word, autoreset: ignored.
    obj.SetMessage("stop", 0.f);
    obj.SetMessage("min x", 0.f);
    obj.SetMessage("autoreset 1", 0.f);
    obj.SetMessage("reset 3", 0.f);
    obj.GetInlet(0)->SetFloat(std::numeric_limits<float>::quiet_NaN(), YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CHECK(Constant(*count.received, 8.f));
  }

  TEST_CASE("~count: a list sets initial, limit and enable (#1066)") {
    BufferSink count;
    BufferSink carry;
    dCount obj;
    Wire(obj, count, carry);
    // Through the inlet, as a message box's text arrives.
    obj.GetInlet(0)->SetMessage("2 5 1 1", YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    REQUIRE(count.received != nullptr);
    CheckHead(*count.received, {2.f, 3.f, 4.f, 2.f, 3.f, 4.f});
    CheckHead(*carry.received, {0.f, 0.f, 0.f, 1.f, 1.f, 1.f});
    // enable 0 stops.
    obj.GetInlet(0)->SetMessage("4 0 0", YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CHECK(Constant(*count.received, 4.f));
    // Two numbers: initial and limit, no start.
    obj.GetInlet(0)->SetMessage("6 9", YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CHECK(Constant(*count.received, 6.f));
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    CheckHead(*count.received, {6.f, 7.f, 8.f, 6.f});
  }

  TEST_CASE("~count: arguments, and a live re-parse of enable (#1066)") {
    BufferSink count;
    BufferSink carry;
    dCount obj;
    Wire(obj, count, carry);
    // initial limit enable autoreset: counts from the first block.
    obj.SetParams("10 14 1 1");
    obj.Calculate(YSE::T_DSP);
    REQUIRE(count.received != nullptr);
    CheckHead(*count.received, {10.f, 11.f, 12.f, 13.f, 10.f});

    BufferSink idleCount;
    BufferSink idleCarry;
    dCount idle;
    Wire(idle, idleCount, idleCarry);
    idle.SetParams("3");
    idle.Calculate(YSE::T_DSP);
    REQUIRE(idleCount.received != nullptr);
    CHECK(Constant(*idleCount.received, 3.f));
    // enable switched on afterwards: it starts, as a rebuilt object would.
    idle.SetParams("3 0 1");
    idle.Calculate(YSE::T_DSP);
    CHECK(Ramp(*idleCount.received, 3.f));
    idle.SetParams("3 0 0");
    idle.Calculate(YSE::T_DSP);
    CHECK(Constant(*idleCount.received, 3.f));
  }

  TEST_CASE("~count: an enable signal against the DSP class (#1066)") {
    buffer enable(N);
    for (unsigned int i = 0; i < N; ++i)
      enable.getPtr()[i] = (i / 37) % 2 == 0 ? 1.f : 0.f;

    BufferSink count;
    BufferSink carry;
    dCount obj;
    Wire(obj, count, carry);
    obj.SetParams("4 20");
    sampleCounter reference;
    reference.minimum(4).limit(20);
    for (int block = 0; block < 3; ++block) {
      CAPTURE(block);
      obj.GetInlet(0)->SetBuffer(&enable, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(count.received != nullptr);
      buffer& expected = reference(enable);
      CHECK(TestHelpers::buffersNearlyEqual(*count.received, expected, 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(*carry.received, reference.carry(), 0.f));
    }
  }

  TEST_CASE("~count: in a rendered patch, controlled through receives, after a JSON round trip "
            "(#1066)") {
    TestHelpers::ScopedSampleRate rate(44100);
    std::string dump;
    {
      patcherImplementation src(3, nullptr);
      YSE::pHandle* looping = src.CreateObject(YSE::OBJ::D_COUNT, "0 100 1");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1066");
      YSE::pHandle* lim = src.CreateObject(YSE::OBJ::G_RECEIVE, "lim1066");
      YSE::pHandle* gated = src.CreateObject(YSE::OBJ::D_COUNT, "5");
      YSE::pHandle* gate = src.CreateObject(YSE::OBJ::D_SIG, "1");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(looping != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(lim != nullptr);
      REQUIRE(gated != nullptr);
      REQUIRE(gate != nullptr);
      src.Connect(cmd, 0, looping, 0);
      src.Connect(lim, 0, looping, 1);
      src.Connect(gate, 0, gated, 0);
      src.Connect(looping, 0, dac, 0);
      src.Connect(looping, 1, dac, 1);
      src.Connect(gated, 0, dac, 2);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(3, nullptr);
    dst.ParseJSON(dump);

    sampleCounter looping;
    looping.limit(100).start();
    sampleCounter gated;
    gated.minimum(5).start();
    for (int block = 0; block < 8; ++block) {
      CAPTURE(block);
      if (block == 2) {
        REQUIRE(dst.PassData(1000, "lim1066", YSE::T_GUI));
        looping.limit(1000);
      }
      if (block == 4) {
        REQUIRE(dst.PassData("stop", "cmd1066", YSE::T_GUI));
        looping.stop();
      }
      if (block == 5) {
        REQUIRE(dst.PassData(30, "cmd1066", YSE::T_GUI));
        looping.start(30);
      }
      if (block == 7) {
        REQUIRE(dst.PassData("bang", "cmd1066", YSE::T_GUI));
        looping.start();
      }
      dst.Calculate(YSE::T_DSP);
      buffer& expected = looping();
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], expected, 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[1], looping.carry(), 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[2], gated(), 0.f));
    }
    CHECK(dst.output[0].getPtr()[N - 1] == 30.f + static_cast<float>(N - 1));
  }

  TEST_CASE("~count: numbers, messages and blocks on the audio thread allocate nothing (#1066)") {
    BufferSink count;
    BufferSink carry;
    dCount obj;
    Wire(obj, count, carry);
    buffer enable(N);
    enable = 1.f;
    const std::string list = "2 50 1";
    const std::string stop = "stop";
    const std::string set = "set 4";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetBang(YSE::T_DSP);
      obj.GetInlet(0)->SetInt(3, YSE::T_DSP);
      obj.GetInlet(0)->SetFloat(2.f, YSE::T_DSP);
      obj.GetInlet(1)->SetInt(40, YSE::T_DSP);
      obj.GetInlet(1)->SetFloat(30.f, YSE::T_DSP);
      obj.SetMessage(list, 0.f);
      obj.SetMessage(set, 0.f);
      obj.SetMessage(stop, 0.f);
      obj.Calculate(YSE::T_DSP);
      obj.GetInlet(0)->SetBuffer(&enable, YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

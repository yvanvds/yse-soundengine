// Tests for the ~stepdiv patcher object (patcher/generatorObjects/dStepDiv),
// issue #1075. The signal is DSP::phasorStepDiv's and is tested in
// Tests/dsp/test_phasor_stepdiv.cpp; these show that signals, bangs, numbers,
// lists, messages and arguments reach it, driven and free running, alone and
// in rendered patches after a JSON round trip.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/generatorObjects/dStepDiv.h"
#include "dsp/generators/phasor.hpp"
#include "dsp/generators/phasorStepDiv.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::phasorStepDiv;
using YSE::PATCHER::dStepDiv;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dStepDiv& obj, BufferSink& ramp, BufferSink& index) {
    obj.ConnectOutlet(ramp.GetInlet(0), 0);
    ramp.ConnectInlet(obj.GetOutlet(0), 0);
    obj.ConnectOutlet(index.GetInlet(0), 1);
    index.ConnectInlet(obj.GetOutlet(1), 0);
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~stepdiv: registry and shape (#1075)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_STEPDIV);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~stepdiv");
    CHECK(obj->GetInputs() == 2);
    CHECK(obj->GetOutputs() == 2);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(obj->OutputDataType(1) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_STEPDIV));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::OSC);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 3);
  }

  TEST_CASE("~stepdiv: driven, numbers, lists and messages against the DSP class (#1075, "
            "#1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink ramp;
    BufferSink index;
    dStepDiv obj;
    Wire(obj, ramp, index);
    phasorStepDiv reference;
    // One 128-sample input cycle per block, for the object and the reference.
    phasor objSource;
    phasor refSource;

    const auto step = [&]() {
      obj.ResetDSP(); // as the patcher does before every block
      buffer& in = objSource(375.f, N);
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(ramp.received != nullptr);
      REQUIRE(index.received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*ramp.received, reference(refSource(375.f, N)), 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(*index.received, reference.index(), 0.f));
    };

    // Default: no pattern, the input passed through, index 0.
    step();
    CHECK(ramp.received->getPtr()[N - 1] == doctest::Approx(127.f / 128.f));
    CHECK(index.received->getPtr()[N - 1] == 0.f);

    // A pattern message: two thirds, then a third.
    obj.SetMessage("pattern 50 25", 0.f);
    const float times[] = {50.f, 25.f};
    reference.pattern(times, 2);
    step();
    CHECK(index.received->getPtr()[85] == 0.f);
    CHECK(index.received->getPtr()[86] == 1.f);

    // A list: a breakpoint function in line format, pairs by default.
    obj.GetInlet(0)->SetList("0 0 1 500 0 250", YSE::T_GUI);
    const float pairs[] = {0.f, 0.f, 1.f, 500.f, 0.f, 250.f};
    reference.breakpoints(pairs, 6, false);
    step();
    CHECK(index.received->getPtr()[0] == 1.f);
    CHECK(index.received->getPtr()[N - 1] == 2.f);

    // curvemode 1: triples.
    obj.SetMessage("curvemode 1", 0.f);
    obj.GetInlet(0)->SetList("0 0 0 1 300 0.5 0 100 -0.5", YSE::T_GUI);
    const float triples[] = {0.f, 0.f, 0.f, 1.f, 300.f, 0.5f, 0.f, 100.f, -0.5f};
    reference.breakpoints(triples, 9, true);
    step();
    CHECK(index.received->getPtr()[N - 1] == 2.f);
    obj.SetMessage("curvemode 0", 0.f);
    obj.GetInlet(0)->SetList("0 0 1 300", YSE::T_GUI);
    const float pair[] = {0.f, 0.f, 1.f, 300.f};
    reference.breakpoints(pair, 4, false);
    step();

    // A command that arrives as a list (text through a receive) is a command.
    obj.GetInlet(0)->SetList("pattern 3 1", YSE::T_GUI);
    const float threeOne[] = {3.f, 1.f};
    reference.pattern(threeOne, 2);
    step();
    CHECK(index.received->getPtr()[95] == 0.f);
    CHECK(index.received->getPtr()[96] == 1.f);

    // what: onsets.
    obj.SetMessage("what 0.25 0.5 0.75", 0.f);
    const float quarters[] = {0.25f, 0.5f, 0.75f};
    reference.onsets(quarters, 3);
    step();
    CHECK(index.received->getPtr()[N - 1] == 3.f);

    // A number: a pattern of one step, the input passed on as one ramp.
    obj.GetInlet(0)->SetFloat(10.f, YSE::T_GUI);
    const float ten[] = {10.f};
    reference.pattern(ten, 1);
    step();
    obj.GetInlet(0)->SetInt(3, YSE::T_GUI);
    step();

    // syncupdate 1: a new pattern waits for the input to wrap. Each block is
    // one whole cycle, so it lands at the next block's first sample.
    obj.SetMessage("syncupdate 1", 0.f);
    obj.SetMessage("pattern 1 3", 0.f);
    const float oneThree[] = {1.f, 3.f};
    reference.syncUpdate(true).pattern(oneThree, 2);
    step();
    CHECK(index.received->getPtr()[31] == 0.f);
    CHECK(index.received->getPtr()[32] == 1.f);
    obj.SetMessage("pattern", 0.f);
    reference.pattern(nullptr, 0);
    step();
    obj.SetMessage("syncupdate 0", 0.f);
    reference.syncUpdate(false);
    step();

    // Malformed or unknown messages: ignored.
    obj.SetMessage("curvemode", 0.f);
    obj.SetMessage("curvemode 7", 0.f);
    obj.SetMessage("loop x", 0.f);
    obj.SetMessage("syncupdate", 0.f);
    obj.SetMessage("speed 3", 0.f);
    obj.SetMessage("", 0.f);
    step();
    step();
  }

  TEST_CASE("~stepdiv: free running, bang, step inlet, loop and arguments (#1075)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink ramp;
    BufferSink index;
    dStepDiv obj;
    Wire(obj, ramp, index);
    phasorStepDiv reference;

    // Nothing connected: the steps run free over their times in ms.
    const auto step = [&]() {
      obj.ResetDSP();
      obj.Calculate(YSE::T_DSP);
      REQUIRE(ramp.received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*ramp.received, reference(), 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(*index.received, reference.index(), 0.f));
    };

    obj.SetMessage("pattern 1 2", 0.f); // 48 and 96 samples
    const float times[] = {1.f, 2.f};
    reference.pattern(times, 2);
    step();
    CHECK(ramp.received->getPtr()[N - 1] == 0.f); // not started

    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    reference.start();
    for (int block = 0; block < 4; ++block)
      step();
    CHECK(ramp.received->getPtr()[N - 1] == 1.f); // over

    // Right inlet: jump to a step and play on.
    obj.GetInlet(1)->SetInt(1, YSE::T_GUI);
    reference.step(1);
    step();
    CHECK(index.received->getPtr()[0] == 1.f);
    obj.GetInlet(1)->SetFloat(0.9f, YSE::T_GUI);
    reference.step(0);
    step();
    CHECK(index.received->getPtr()[0] == 0.f);

    obj.SetMessage("loop 1", 0.f);
    reference.loop(true);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    reference.start();
    for (int block = 0; block < 6; ++block)
      step();
    obj.SetMessage("loop 0", 0.f);
    reference.loop(false);
    for (int block = 0; block < 4; ++block)
      step();
    CHECK(ramp.received->getPtr()[N - 1] == 1.f);

    // Arguments: curvemode loop syncupdate.
    BufferSink argRamp;
    BufferSink argIndex;
    dStepDiv args;
    Wire(args, argRamp, argIndex);
    args.SetParams("1 1 1");
    phasorStepDiv argReference;
    argReference.loop(true).syncUpdate(true);
    args.GetInlet(0)->SetList("0 0 0 1 1 0 0 1 0", YSE::T_GUI); // triples: times 0 1 1
    const float list[] = {0.f, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 1.f, 0.f};
    argReference.breakpoints(list, 9, true);
    args.GetInlet(0)->SetBang(YSE::T_GUI);
    argReference.start();
    for (unsigned int block = 0; block < 4; ++block) {
      CAPTURE(block);
      args.Calculate(YSE::T_DSP);
      REQUIRE(argRamp.received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*argRamp.received, argReference(), 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(*argIndex.received, argReference.index(), 0.f));
    }
    // Times 0 1 1, looping: indices 1 and 2, never 0.
    CHECK(argIndex.received->getPtr()[0] != 0.f);
  }

  TEST_CASE("~stepdiv: in rendered patches, driven by a ~phasor and free running, controlled "
            "through a receive, after a JSON round trip (#1075)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* driven = src.CreateObject(YSE::OBJ::D_STEPDIV, "2 0 1");
      YSE::pHandle* free = src.CreateObject(YSE::OBJ::D_STEPDIV, "2 1 0");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1075");
      YSE::pHandle* go = src.CreateObject(YSE::OBJ::G_RECEIVE, "go1075");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(driven != nullptr);
      REQUIRE(free != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(go != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, driven, 0);
      src.Connect(cmd, 0, driven, 0);
      src.Connect(go, 0, free, 0);
      src.Connect(driven, 0, dac, 0);
      src.Connect(free, 1, dac, 1);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    phasor source;
    phasorStepDiv drivenRef;
    drivenRef.syncUpdate(true);
    phasorStepDiv freeRef;
    freeRef.loop(true);
    for (int block = 0; block < 8; ++block) {
      CAPTURE(block);
      if (block == 1) {
        // A breakpoint list lands at the next input wrap (syncupdate 1).
        REQUIRE(dst.PassData("0 0 1 50 0 25", "cmd1075", YSE::T_GUI));
        const float pairs[] = {0.f, 0.f, 1.f, 50.f, 0.f, 25.f};
        drivenRef.breakpoints(pairs, 6, false);
        REQUIRE(dst.PassData("pattern 1 0.5", "go1075", YSE::T_GUI));
        const float times[] = {1.f, 0.5f};
        freeRef.pattern(times, 2);
        REQUIRE(dst.PassData("bang", "go1075", YSE::T_GUI));
        freeRef.start();
      }
      if (block == 5) {
        REQUIRE(dst.PassData("what 0.5", "cmd1075", YSE::T_GUI));
        const float half[] = {0.5f};
        drivenRef.onsets(half, 1);
      }
      dst.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], drivenRef(source(375.f, N)), 0.f));
      freeRef();
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[1], freeRef.index(), 0.f));
      if (block == 2) {
        CHECK(dst.output[0].getPtr()[85] == doctest::Approx(85.0 * 75.0 / 128.0 / 50.0));
        // 48 + 24 samples, looping from block 1: sample 128 + k is 56 + k
        // into a 72-sample loop.
        CHECK(dst.output[1].getPtr()[0] == 1.f);
        CHECK(dst.output[1].getPtr()[16] == 0.f);
      }
    }
  }

  TEST_CASE("~stepdiv: bangs, numbers, lists, messages and blocks on the audio thread allocate "
            "nothing (#1075)") {
    BufferSink ramp;
    BufferSink index;
    dStepDiv obj;
    Wire(obj, ramp, index);
    buffer in(N);
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = static_cast<float>(i) / N;
    const std::string pattern = "pattern 2 1 1";
    const std::string what = "what 0.25 0.5";
    const std::string list = "0 0 1 50 0 25";
    const std::string mode = "syncupdate 1";
    const std::string loop = "loop 1";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetFloat(4.f, YSE::T_DSP);
      obj.GetInlet(0)->SetInt(4, YSE::T_DSP);
      obj.GetInlet(0)->SetList(list, YSE::T_DSP);
      obj.GetInlet(0)->SetBang(YSE::T_DSP);
      obj.GetInlet(1)->SetInt(1, YSE::T_DSP);
      obj.SetMessage(pattern, 0.f);
      obj.SetMessage(what, 0.f);
      obj.SetMessage(mode, 0.f);
      obj.SetMessage(loop, 0.f);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

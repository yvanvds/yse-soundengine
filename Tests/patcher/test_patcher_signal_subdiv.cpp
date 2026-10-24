// Tests for the ~subdiv patcher object (patcher/generatorObjects/dSubdiv),
// issue #1074. The signal is DSP::phasorSubdiv's and is tested in
// Tests/dsp/test_phasor_subdiv.cpp; these show that signals, numbers,
// messages and arguments reach it, alone and in a rendered patch (a ~phasor
// subdivided by ~subdiv, controlled through a receive) after a JSON round
// trip.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/generatorObjects/dSubdiv.h"
#include "dsp/generators/phasor.hpp"
#include "dsp/generators/phasorSubdiv.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::phasorSubdiv;
using YSE::PATCHER::dSubdiv;
using YSE::PATCHER::patcherImplementation;
using silent = YSE::DSP::phasorSubdiv::silent;
using update = YSE::DSP::phasorSubdiv::update;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dSubdiv& obj, BufferSink& ramp, BufferSink& index) {
    obj.ConnectOutlet(ramp.GetInlet(0), 0);
    ramp.ConnectInlet(obj.GetOutlet(0), 0);
    obj.ConnectOutlet(index.GetInlet(0), 1);
    index.ConnectInlet(obj.GetOutlet(1), 0);
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~subdiv: registry and shape (#1074)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_SUBDIV);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~subdiv");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 2);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(obj->OutputDataType(1) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_SUBDIV));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::OSC);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 4);
  }

  TEST_CASE("~subdiv: numbers, messages and arguments against the DSP class (#1074, #1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink ramp;
    BufferSink index;
    dSubdiv obj;
    Wire(obj, ramp, index);
    phasorSubdiv reference;
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

    // Default: div 1, the input passed through, index 0.
    step();
    CHECK(ramp.received->getPtr()[N - 1] == doctest::Approx(127.f / 128.f));
    CHECK(index.received->getPtr()[N - 1] == 0.f);

    // Numbers set div; a float is truncated.
    obj.GetInlet(0)->SetInt(4, YSE::T_GUI);
    reference.div(4);
    step();
    CHECK(index.received->getPtr()[N - 1] == 3.f);
    obj.GetInlet(0)->SetFloat(3.9f, YSE::T_GUI);
    reference.div(3);
    step();
    CHECK(index.received->getPtr()[N - 1] == 2.f);

    // Messages.
    obj.SetMessage("div 4", 0.f);
    obj.SetMessage("pattern 2 1 1", 0.f);
    const int steps[] = {2, 1, 1};
    reference.div(4).pattern(steps, 3);
    step();
    CHECK(index.received->getPtr()[N - 1] == 2.f);
    obj.SetMessage("prob 0 1", 0.f);
    const float chances[] = {0.f, 1.f};
    reference.prob(chances, 2);
    step();
    CHECK(ramp.received->getPtr()[32] == 0.f); // first step silent
    obj.SetMessage("lockprob 1", 0.f);
    obj.SetMessage("silentmode 1", 0.f);
    reference.lockProb(true).silentMode(silent::minusOne);
    step();
    step();
    obj.SetMessage("syncupdate 2", 0.f);
    obj.SetMessage("pattern", 0.f);
    obj.SetMessage("prob none", 0.f);
    reference.syncUpdate(update::input).pattern(nullptr, 0).prob(nullptr, 0);
    step();
    obj.SetMessage("syncupdate 1", 0.f);
    obj.SetMessage("div 8", 0.f);
    reference.syncUpdate(update::output).div(8);
    step();
    CHECK(index.received->getPtr()[N - 1] == 7.f);
    obj.SetMessage("prob 0.5", 0.f);
    const float half[] = {0.5f};
    reference.prob(half, 1);
    step();
    step();
    obj.SetMessage("prob", 0.f);
    obj.SetMessage("silentmode 0", 0.f);
    obj.SetMessage("lockprob 0", 0.f);
    obj.SetMessage("syncupdate 0", 0.f);
    reference.prob(nullptr, 0).silentMode(silent::hold).lockProb(false).syncUpdate(update::now);
    step();

    // Malformed or unknown messages: ignored.
    obj.SetMessage("div", 0.f);
    obj.SetMessage("div x", 0.f);
    obj.SetMessage("syncupdate 7", 0.f);
    obj.SetMessage("lockprob", 0.f);
    obj.SetMessage("silentmode y", 0.f);
    obj.SetMessage("speed 3", 0.f);
    obj.SetMessage("", 0.f);
    step();
    step();

    // Arguments: div syncupdate silentmode lockprob.
    BufferSink argRamp;
    BufferSink argIndex;
    dSubdiv args;
    Wire(args, argRamp, argIndex);
    args.SetParams("5 1 1 1");
    phasorSubdiv argReference;
    argReference.div(5).syncUpdate(update::output).silentMode(silent::minusOne).lockProb(true);
    phasor argSource;
    phasor argRefSource;
    args.SetMessage("prob 1 0", 0.f);
    const float oneZero[] = {1.f, 0.f};
    argReference.prob(oneZero, 2);
    for (unsigned int block = 0; block < 3; ++block) {
      CAPTURE(block);
      args.GetInlet(0)->SetBuffer(&argSource(375.f, N), YSE::T_GUI);
      args.Calculate(YSE::T_DSP);
      REQUIRE(argRamp.received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*argRamp.received, argReference(argRefSource(375.f, N)),
                                            0.f));
      CHECK(TestHelpers::buffersNearlyEqual(*argIndex.received, argReference.index(), 0.f));
    }
    // Five steps, the odd ones silent at -1.
    CHECK(argIndex.received->getPtr()[N - 1] == 4.f);
    CHECK(argIndex.received->getPtr()[40] == -1.f);
  }

  TEST_CASE("~subdiv: in a rendered patch, a ~phasor subdivided by ~subdiv, controlled through a "
            "receive, after a JSON round trip (#1074)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* split = src.CreateObject(YSE::OBJ::D_SUBDIV, "3 2");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1074");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(split != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, split, 0);
      src.Connect(cmd, 0, split, 0);
      src.Connect(split, 0, dac, 0);
      src.Connect(split, 1, dac, 1);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    phasor source;
    phasorSubdiv reference;
    reference.div(3).syncUpdate(update::input);
    for (int block = 0; block < 8; ++block) {
      CAPTURE(block);
      if (block == 2) {
        // Mid-cycle messages land on the next input wrap (syncupdate 2).
        REQUIRE(dst.PassData("div 4", "cmd1074", YSE::T_GUI));
        REQUIRE(dst.PassData("pattern 1 3", "cmd1074", YSE::T_GUI));
        reference.div(4);
        const int steps[] = {1, 3};
        reference.pattern(steps, 2);
      }
      if (block == 5) {
        REQUIRE(dst.PassData(2.f, "cmd1074", YSE::T_GUI));
        REQUIRE(dst.PassData("pattern", "cmd1074", YSE::T_GUI));
        reference.div(2).pattern(nullptr, 0);
      }
      dst.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(source(375.f, N)), 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[1], reference.index(), 0.f));
      if (block == 3) {
        // 1 3 over div 4: the first step is a quarter, then three quarters.
        CHECK(dst.output[1].getPtr()[31] == 0.f);
        CHECK(dst.output[1].getPtr()[32] == 1.f);
        CHECK(dst.output[0].getPtr()[80] == doctest::Approx(0.5f));
      }
      if (block == 6) CHECK(dst.output[1].getPtr()[N - 1] == 1.f);
    }
  }

  TEST_CASE("~subdiv: numbers, messages and blocks on the audio thread allocate nothing (#1074)") {
    BufferSink ramp;
    BufferSink index;
    dSubdiv obj;
    Wire(obj, ramp, index);
    buffer in(N);
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = static_cast<float>(i) / N;
    const std::string pattern = "pattern 2 1 1";
    const std::string prob = "prob 1 0.5 none 0";
    const std::string mode = "syncupdate 1";
    const std::string lock = "lockprob 1";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetFloat(4.f, YSE::T_DSP);
      obj.GetInlet(0)->SetInt(4, YSE::T_DSP);
      obj.SetMessage(pattern, 0.f);
      obj.SetMessage(prob, 0.f);
      obj.SetMessage(mode, 0.f);
      obj.SetMessage(lock, 0.f);
      obj.Calculate(YSE::T_DSP);
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

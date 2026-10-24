// Tests for the ~stepfun patcher object (patcher/generatorObjects/dStepFun),
// issue #1081. The function is DSP::stepFunction's and is tested in
// Tests/dsp/test_step_function.cpp; these show that the ramp, the step,
// numbers, lists, messages and arguments reach it, alone and in a rendered
// patch with ~stepdiv after a JSON round trip.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/generatorObjects/dStepFun.h"
#include "dsp/generators/phasor.hpp"
#include "dsp/generators/phasorStepDiv.hpp"
#include "dsp/generators/stepFunction.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::phasorStepDiv;
using YSE::DSP::stepFunction;
using YSE::PATCHER::dStepFun;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dStepFun& obj, BufferSink& sink) {
    obj.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(obj.GetOutlet(0), 0);
  }

  bool AllZero(buffer& b) {
    for (unsigned int i = 0; i < b.getLength(); ++i)
      if (b.getPtr()[i] != 0.f) return false;
    return true;
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~stepfun: registry and shape (#1081)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_STEPFUN);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~stepfun");
    CHECK(obj->GetInputs() == 2);
    CHECK(obj->GetOutputs() == 1);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->IsDSPInput(1));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_STEPFUN));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::OSC);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~stepfun: numbers, lists, messages and the step inlet against the DSP class "
            "(#1081, #1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink sink;
    dStepFun obj;
    Wire(obj, sink);
    stepFunction reference;
    // A ramp of 32 samples per cycle (1500 Hz), for the object and the
    // reference.
    phasor objSource;
    phasor refSource;

    const auto step = [&]() {
      obj.ResetDSP(); // as the patcher does before every block
      buffer& in = objSource(1500.f, N);
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(sink.received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(refSource(1500.f, N)), 0.f));
    };

    // Default: no pattern, 0.
    step();
    CHECK(AllZero(*sink.received));

    // A list is a breakpoint function; curvemode 2 (auto) reads pairs.
    obj.GetInlet(0)->SetList("0 0 1 10 0.5 10", YSE::T_GUI);
    const float pairs[] = {0.f, 0.f, 1.f, 10.f, 0.5f, 10.f};
    reference.breakpoints(pairs, 6, false);
    step();
    CHECK_FALSE(AllZero(*sink.received));
    step();

    // A number is a constant, and the first value of a list right after it.
    obj.GetInlet(0)->SetFloat(0.3f, YSE::T_GUI);
    const float constant[] = {0.3f};
    reference.pattern(constant, 1);
    step();
    obj.GetInlet(0)->SetList("1 10 0 10", YSE::T_GUI);
    const float rest[] = {1.f, 10.f, 0.f, 10.f};
    const float initial = 0.3f;
    reference.breakpoints(rest, 4, false, &initial);
    step();
    // ... but not when something came in between; an int is a number too.
    obj.GetInlet(0)->SetInt(1, YSE::T_GUI);
    obj.SetMessage("curvemode 1", 0.f);
    obj.GetInlet(0)->SetList("0 10 0.5 1 10 -0.5", YSE::T_GUI);
    const float triples[] = {0.f, 10.f, 0.5f, 1.f, 10.f, -0.5f};
    reference.breakpoints(triples, 6, true);
    step();

    // curvemode 0: pairs again, and they leave the curves alone.
    obj.SetMessage("curvemode 0", 0.f);
    obj.GetInlet(0)->SetList("0.2 5 0.8 5", YSE::T_GUI);
    const float twoValues[] = {0.2f, 0.8f};
    reference.pattern(twoValues, 2);
    step();

    // pattern and curvepattern; with nothing they clear.
    obj.SetMessage("pattern 0 1 0.5 0.25", 0.f);
    const float four[] = {0.f, 1.f, 0.5f, 0.25f};
    reference.pattern(four, 4);
    step();
    obj.SetMessage("curvepattern 0.5 -0.5", 0.f);
    const float bends[] = {0.5f, -0.5f};
    reference.curvePattern(bends, 2);
    step();
    obj.SetMessage("curvepattern", 0.f);
    reference.curvePattern(nullptr, 0);
    step();

    // A command that arrives as a list (text through a receive) is a command.
    obj.GetInlet(0)->SetList("pattern 0.1 0.9", YSE::T_GUI);
    const float tenNinety[] = {0.1f, 0.9f};
    reference.pattern(tenNinety, 2);
    step();
    obj.SetMessage("pattern 0 1 0.5 0.25", 0.f);
    reference.pattern(four, 4);
    step();

    // bang resets; step <n>; a number in the step inlet.
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    reference.reset();
    step();
    obj.SetMessage("step 2", 0.f);
    reference.step(2);
    step();
    obj.GetInlet(1)->SetFloat(3.7f, YSE::T_GUI);
    reference.step(3);
    step();
    obj.GetInlet(1)->SetInt(-1, YSE::T_GUI);
    reference.step(-1);
    step();

    // syncupdate 1: a new pattern waits for the ramp to wrap.
    obj.SetMessage("syncupdate 1", 0.f);
    obj.SetMessage("pattern 2 3", 0.f);
    const float twoThree[] = {2.f, 3.f};
    reference.syncUpdate(stepFunction::INPUT_RESET).pattern(twoThree, 2);
    step();
    obj.SetMessage("syncupdate 2", 0.f);
    obj.SetMessage("pattern 0 1 0.5", 0.f);
    reference.syncUpdate(stepFunction::SEQUENCE_END).pattern(four, 3);
    step();
    step();
    obj.SetMessage("syncupdate 0", 0.f);
    reference.syncUpdate(stepFunction::IMMEDIATE);
    step();

    // Malformed or unknown messages: ignored.
    obj.SetMessage("curvemode", 0.f);
    obj.SetMessage("curvemode 3", 0.f);
    obj.SetMessage("syncupdate -1", 0.f);
    obj.SetMessage("step x", 0.f);
    obj.SetMessage("speed 3", 0.f);
    obj.SetMessage("", 0.f);
    step();

    // A step signal in inlet 1 replaces the counting of the ramp's cycles.
    for (int block = 0; block < 2; ++block) {
      buffer steps(N);
      for (unsigned int i = 0; i < N; ++i)
        steps.getPtr()[i] = static_cast<float>((i / 16) % 5);
      obj.ResetDSP();
      buffer& in = objSource(1500.f, N);
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
      obj.GetInlet(1)->SetBuffer(&steps, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(sink.received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(refSource(1500.f, N), steps),
                                            0.f));
    }

    // Unconnected: the ramp reads 0, so the step never changes: a constant.
    BufferSink idleSink;
    dStepFun idle;
    Wire(idle, idleSink);
    idle.SetParams("0.25 0.75");
    idle.Calculate(YSE::T_DSP);
    REQUIRE(idleSink.received != nullptr);
    for (unsigned int i = 0; i < idleSink.received->getLength(); ++i)
      REQUIRE(idleSink.received->getPtr()[i] == 0.75f); // step 0 starts at the last value
  }

  TEST_CASE("~stepfun: arguments are the pattern, with the attributes as keywords (#1081)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto check = [](const std::string& args, stepFunction& reference, int curvemode) {
      CAPTURE(args);
      BufferSink sink;
      dStepFun obj;
      Wire(obj, sink);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      phasor objSource;
      phasor refSource;
      for (int block = 0; block < 3; ++block) {
        CAPTURE(block);
        obj.ResetDSP();
        buffer& in = objSource(1500.f, N);
        obj.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
        obj.Calculate(YSE::T_DSP);
        REQUIRE(sink.received != nullptr);
        CHECK(
            TestHelpers::buffersNearlyEqual(*sink.received, reference(refSource(1500.f, N)), 0.f));
      }
      // curvemode reached the list parser: triples set three values, pairs
      // two.
      obj.GetInlet(0)->SetList("0 1 0 1 1 0", YSE::T_GUI);
      const float list[] = {0.f, 1.f, 0.f, 1.f, 1.f, 0.f};
      reference.breakpoints(list, 6, curvemode == 1);
      obj.ResetDSP();
      buffer& in = objSource(1500.f, N);
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(refSource(1500.f, N)), 0.f));
    };

    const float values[] = {0.f, 1.f, 0.5f};
    stepFunction plain;
    plain.pattern(values, 3);
    check("0 1 0.5", plain, 2);

    // curvepattern sends the numbers after it to the curves, pattern back.
    const float curves[] = {0.5f, 0.f, -0.5f};
    stepFunction curved;
    curved.pattern(values, 3).curvePattern(curves, 3).syncUpdate(stepFunction::INPUT_RESET);
    check("0 1 curvepattern 0.5 0 -0.5 syncupdate 1 pattern 0.5", curved, 2);
    stepFunction triples;
    triples.pattern(values, 3);
    check("curvemode 1 0 1 0.5", triples, 1);

    // A keyword without a number, or out of range, is skipped.
    stepFunction skipped;
    skipped.pattern(values, 3);
    check("0 syncupdate 7 1 0.5 curvemode", skipped, 2);

    stepFunction none;
    check("", none, 2);
  }

  TEST_CASE("~stepfun: in a rendered patch, ~stepdiv's ramp and index play a function, and a "
            "~phasor counts steps, with messages from receives, after a JSON round trip "
            "(#1081)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "125");
      YSE::pHandle* div = src.CreateObject(YSE::OBJ::D_STEPDIV, "");
      YSE::pHandle* played = src.CreateObject(YSE::OBJ::D_STEPFUN, "");
      YSE::pHandle* counted = src.CreateObject(YSE::OBJ::D_STEPFUN, "0 1 0.5 curvepattern 0.5");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1081");
      YSE::pHandle* other = src.CreateObject(YSE::OBJ::G_RECEIVE, "other1081");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(div != nullptr);
      REQUIRE(played != nullptr);
      REQUIRE(counted != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(other != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, div, 0);
      src.Connect(div, 0, played, 0);
      src.Connect(div, 1, played, 1);
      src.Connect(ramp, 0, counted, 0);
      src.Connect(cmd, 0, div, 0);
      src.Connect(cmd, 0, played, 0);
      src.Connect(other, 0, counted, 0);
      src.Connect(played, 0, dac, 0);
      src.Connect(counted, 0, dac, 1);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    phasor source;
    phasorStepDiv divRef;
    stepFunction playedRef;
    stepFunction countedRef;
    const float values[] = {0.f, 1.f, 0.5f};
    const float curve[] = {0.5f};
    countedRef.pattern(values, 3).curvePattern(curve, 1);
    const unsigned int period = 384; // 125 Hz at 48 kHz
    unsigned int n = 0;
    for (int block = 0; block < 10; ++block) {
      CAPTURE(block);
      if (block == 2) {
        // One breakpoint function to both: up from 0 to 1 over two thirds of
        // the cycle, down to 0 over the last third.
        REQUIRE(dst.PassData("0 0 1 500 0 250", "cmd1081", YSE::T_GUI));
        const float list[] = {0.f, 0.f, 1.f, 500.f, 0.f, 250.f};
        divRef.breakpoints(list, 6, false);
        playedRef.breakpoints(list, 6, false);
      }
      if (block == 6) {
        REQUIRE(dst.PassData("bang", "other1081", YSE::T_GUI));
        countedRef.reset();
      }
      dst.Calculate(YSE::T_DSP);
      buffer& phase = source(125.f, N);
      buffer& r = divRef(phase);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], playedRef(r, divRef.index()), 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[1], countedRef(phase), 0.f));
      if (block < 2) {
        CHECK(AllZero(dst.output[0])); // no pattern yet
      } else {
        // The closed form of the function over the phasor's cycle.
        for (unsigned int i = 0; i < N; ++i) {
          CAPTURE(i);
          const float p = static_cast<float>((n + i) % period) / static_cast<float>(period);
          const float expected = p < 2.f / 3.f ? p * 1.5f : 1.f - (p - 2.f / 3.f) * 3.f;
          REQUIRE(dst.output[0].getPtr()[i] == doctest::Approx(expected).epsilon(1e-3));
        }
      }
      n += N;
    }
  }

  TEST_CASE("~stepfun: numbers, lists, messages and blocks on the audio thread allocate "
            "nothing (#1081)") {
    BufferSink sink;
    dStepFun obj;
    Wire(obj, sink);
    buffer in(N);
    buffer steps(N);
    for (unsigned int i = 0; i < N; ++i) {
      in.getPtr()[i] = static_cast<float>(i % 16) / 16.f;
      const unsigned int whole = i / 16; // whole steps of 16 samples
      steps.getPtr()[i] = static_cast<float>(whole);
    }
    const std::string list = "0 0 1 10 0.5 10";
    const std::string pattern = "pattern 0 1 0.5";
    const std::string curves = "curvepattern 0.5 0 -0.5";
    const std::string mode = "curvemode 1";
    const std::string sync = "syncupdate 2";
    const std::string stepMsg = "step 2";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetFloat(3.f, YSE::T_DSP);
      obj.GetInlet(0)->SetInt(2, YSE::T_DSP);
      obj.GetInlet(0)->SetList(list, YSE::T_DSP);
      obj.GetInlet(0)->SetBang(YSE::T_DSP);
      obj.GetInlet(1)->SetFloat(1.f, YSE::T_DSP);
      obj.GetInlet(1)->SetInt(2, YSE::T_DSP);
      obj.SetMessage(pattern, 0.f);
      obj.SetMessage(curves, 0.f);
      obj.SetMessage(mode, 0.f);
      obj.GetInlet(0)->SetList(list, YSE::T_DSP);
      obj.SetMessage(sync, 0.f);
      obj.SetMessage(stepMsg, 0.f);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.GetInlet(1)->SetBuffer(&steps, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

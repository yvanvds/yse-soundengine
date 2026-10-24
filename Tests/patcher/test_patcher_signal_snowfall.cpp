// Tests for the ~snowfall patcher object (patcher/generatorObjects/dSnowfall),
// issue #1085. The particle is DSP::snowfall's and is tested in
// Tests/dsp/test_snowfall.cpp; these show that the ramp, numbers, messages
// and arguments reach it, alone and in a rendered patch after a JSON round
// trip.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/generatorObjects/dSnowfall.h"
#include "dsp/generators/snowfall.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::snowfall;
using YSE::PATCHER::dSnowfall;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // Sinks on the first `count` outlets.
  struct Sinks {
    BufferSink out[snowfall::MAX_DIMENSIONS];
    void Wire(dSnowfall& obj) {
      for (int d = 0; d < obj.NumOutputs(); ++d) {
        obj.ConnectOutlet(out[d].GetInlet(0), d);
        out[d].ConnectInlet(obj.GetOutlet(d), 0);
      }
    }
  };

  // One block of `obj` with `ramp` (or nothing) in inlet 0, against the
  // reference's block of the same ramp.
  void Block(dSnowfall& obj, Sinks& sinks, snowfall& reference, buffer* objRamp, buffer& refRamp) {
    obj.ResetDSP(); // as the patcher does before every block
    if (objRamp != nullptr) obj.GetInlet(0)->SetBuffer(objRamp, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    reference(refRamp);
    REQUIRE(static_cast<unsigned int>(obj.NumOutputs()) == reference.dimensions());
    for (unsigned int d = 0; d < reference.dimensions(); ++d) {
      CAPTURE(d);
      REQUIRE(sinks.out[d].received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*sinks.out[d].received, reference.output(d), 0.f));
    }
  }

  // A rig driven by a 375 Hz phasor at 48 kHz.
  struct Rig {
    Sinks sinks;
    dSnowfall obj;
    snowfall reference;
    phasor objRamp, refRamp;

    explicit Rig(const std::string& args = "", unsigned int dims = 1) : reference(dims) {
      if (!args.empty()) obj.SetParams(args);
      sinks.Wire(obj);
    }

    void Step() {
      Block(obj, sinks, reference, &objRamp(375.f, N), refRamp(375.f, N));
    }
  };

} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~snowfall: registry and shape (#1085)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_SNOWFALL);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~snowfall");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 1);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    YSE::pHandle* four = p.CreateObject(YSE::OBJ::D_SNOWFALL, "4");
    REQUIRE(four != nullptr);
    CHECK(four->GetOutputs() == 4);
    for (int d = 0; d < 4; ++d)
      CHECK(four->OutputDataType(d) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_SNOWFALL));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::OSC);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~snowfall: the ramp, numbers and every message against the DSP class (#1085, "
            "#1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    Rig rig("", 1);
    rig.Step();
    // Closed form: direction 1, a new particle from 0 at each phasor cycle.
    for (unsigned int j = 1; j < N; ++j)
      CHECK(rig.sinks.out[0].received->getPtr()[j] == doctest::Approx(j / 48000.0));

    const auto both = [&](const std::string& message) {
      CAPTURE(message);
      rig.obj.SetMessage(message, 0.f);
    };
    both("direction 480");
    both("min -2");
    both("max 2");
    both("scalemin -4");
    both("scalemax 4");
    both("initial 0.5");
    rig.reference.direction(0, 480.f)
        .minimum(0, -2.f)
        .maximum(0, 2.f)
        .scaleMinimum(0, -4.f)
        .scaleMaximum(0, 4.f)
        .initial(0, 0.5f);
    rig.Step();
    // 0.5 + 0.01 j, scaled by 2.
    CHECK(rig.sinks.out[0].received->getPtr()[10] == doctest::Approx(2.0 * (0.5 + 0.1)));

    both("energyramp 1.5");
    both("squish 0.5");
    both("endvalue 0.3");
    both("boundarymode 1");
    both("interval 0.5");
    both("intervaldev 0.25");
    both("wanderprob 0.5");
    both("directiondev 100");
    both("initialdev 0.2");
    rig.reference.energyRamp(0, 1.5f)
        .squish(0, 0.5f)
        .endValue(0, 0.3f)
        .boundaryMode(snowfall::END)
        .interval(0.5f)
        .intervalDeviation(0.25f)
        .wanderProbability(0, 0.5f)
        .directionDeviation(0, 100.f)
        .initialDeviation(0, 0.2f);
    rig.Step();
    rig.Step();

    both("endmode 1");
    both("startmode 1");
    both("boundarymode 2");
    rig.reference.endMode(snowfall::CURRENT_VALUE)
        .startMode(snowfall::CURRENT)
        .boundaryMode(snowfall::WRAP);
    rig.Step();

    // Out of range or not numbers: ignored, as is an unknown word.
    both("boundarymode 4");
    both("endmode 2");
    both("startmode -1");
    both("intervaldev -3");
    both("direction x");
    both("dimensions 3");
    both("bogus 1");
    both("interval");
    rig.Step();
    CHECK(rig.obj.NumOutputs() == 1);

    // A command that came as a list, as text through a receive.
    rig.obj.GetInlet(0)->SetList("max 1", YSE::T_GUI);
    rig.reference.maximum(0, 1.f);
    rig.Step();

    // Unconnected, the inlet reads the last number: a gate.
    snowfall gate;
    Sinks sinks;
    dSnowfall obj;
    sinks.Wire(obj);
    obj.SetMessage("direction 480", 0.f);
    gate.direction(0, 480.f);
    buffer refRamp(N);
    obj.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    refRamp = 0.5f;
    Block(obj, sinks, gate, nullptr, refRamp);
    // 1.28 bounced off the default maximum 1.
    CHECK(sinks.out[0].received->getPtr()[N - 1] == doctest::Approx(2.0 - 0.01 * N));
    obj.GetInlet(0)->SetInt(0, YSE::T_GUI);
    refRamp = 0.f;
    Block(obj, sinks, gate, nullptr, refRamp);
    CHECK(sinks.out[0].received->getPtr()[0] == 0.f);
    obj.GetInlet(0)->SetList("1", YSE::T_GUI);
    refRamp = 1.f;
    Block(obj, sinks, gate, nullptr, refRamp);
    CHECK(gate.alive());
  }

  TEST_CASE("~snowfall: values per dimension go to the dimensions in order (#1085)") {
    TestHelpers::ScopedSampleRate rate(48000);
    Rig rig("3", 3);
    REQUIRE(rig.obj.NumOutputs() == 3);
    rig.obj.SetMessage("direction 480 -240 0 99", 0.f);
    rig.obj.SetMessage("initial 0.1 0.9", 0.f);
    rig.obj.SetMessage("max 10 10 10", 0.f);
    rig.obj.SetMessage("scalemax 10 10 10", 0.f);
    rig.reference.direction(0, 480.f).direction(1, -240.f).direction(2, 0.f);
    rig.reference.initial(0, 0.1f).initial(1, 0.9f);
    for (unsigned int d = 0; d < 3; ++d)
      rig.reference.maximum(d, 10.f).scaleMaximum(d, 10.f);
    rig.Step();
    const float* a = rig.sinks.out[0].received->getPtr();
    const float* b = rig.sinks.out[1].received->getPtr();
    const float* c = rig.sinks.out[2].received->getPtr();
    CHECK(a[10] == doctest::Approx(0.1 + 0.01 * 10));
    CHECK(b[10] == doctest::Approx(0.9 - 0.005 * 10));
    CHECK(c[10] == doctest::Approx(0.0));
  }

  TEST_CASE("~snowfall: arguments are the dimensions, then attribute keywords (#1085)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto check = [](const std::string& args, snowfall& reference) {
      CAPTURE(args);
      Sinks sinks;
      dSnowfall obj;
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      sinks.Wire(obj);
      phasor objRamp, refRamp;
      for (int block = 0; block < 6; ++block) {
        CAPTURE(block);
        Block(obj, sinks, reference, &objRamp(187.5f, N), refRamp(187.5f, N));
      }
    };

    snowfall two(2);
    two.direction(0, 0.5f).direction(1, -0.25f).boundaryMode(snowfall::WRAP);
    check("2 direction 0.5 -0.25 boundarymode 2", two);

    snowfall keyword(4);
    keyword.interval(1.f).wanderProbability(0, 1.f).wanderProbability(3, 0.5f);
    check("dimensions 4 interval 1 wanderprob 1 0 0 0.5", keyword);

    snowfall all(1);
    all.direction(0, 200.f)
        .directionDeviation(0, 50.f)
        .energyRamp(0, 1.f)
        .initial(0, 0.2f)
        .initialDeviation(0, 0.1f)
        .minimum(0, -1.f)
        .maximum(0, 3.f)
        .scaleMinimum(0, 0.f)
        .scaleMaximum(0, 2.f)
        .squish(0, 0.8f)
        .wanderProbability(0, 0.2f)
        .endValue(0, -1.f)
        .interval(5.f)
        .intervalDeviation(2.f)
        .boundaryMode(snowfall::CLIP)
        .endMode(snowfall::CURRENT_VALUE)
        .startMode(snowfall::CURRENT);
    check("direction 200 directiondev 50 energyramp 1 initial 0.2 initialdev 0.1 min -1 max 3 "
          "scalemin 0 scalemax 2 squish 0.8 wanderprob 0.2 endvalue -1 interval 5 intervaldev 2 "
          "boundarymode 3 endmode 1 startmode 1",
          all);

    // A later bare number, out of range values and a keyword without
    // numbers: skipped. Five dimensions is out of range.
    snowfall skipped(1);
    skipped.direction(0, 2.f);
    check("5 7 boundarymode 9 direction 2 3 4 5 6 endmode", skipped);

    snowfall none;
    check("", none);
  }

  TEST_CASE("~snowfall: in a rendered patch, ~phasor drives two dimensions to the dac, with "
            "commands from a receive, after a JSON round trip (#1085)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* snow =
          src.CreateObject(YSE::OBJ::D_SNOWFALL, "2 direction 480 -480 initial 0.5 0.5");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1085");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(snow != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(dac != nullptr);
      REQUIRE(snow->GetOutputs() == 2);
      src.Connect(ramp, 0, snow, 0);
      src.Connect(cmd, 0, snow, 0);
      src.Connect(snow, 0, dac, 0);
      src.Connect(snow, 1, dac, 1);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    phasor rampRef;
    snowfall reference(2);
    reference.direction(0, 480.f).direction(1, -480.f).initial(0, 0.5f).initial(1, 0.5f);
    for (int block = 0; block < 8; ++block) {
      CAPTURE(block);
      if (block == 4) {
        REQUIRE(dst.PassData("boundarymode 2", "cmd1085", YSE::T_GUI));
        REQUIRE(dst.PassData("scalemax 2 4", "cmd1085", YSE::T_GUI));
        reference.boundaryMode(snowfall::WRAP).scaleMaximum(0, 2.f).scaleMaximum(1, 4.f);
      }
      dst.Calculate(YSE::T_DSP);
      reference(rampRef(375.f, N));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference.output(0), 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[1], reference.output(1), 0.f));
      const float* left = dst.output[0].getPtr();
      const float* right = dst.output[1].getPtr();
      for (unsigned int j = 1; j < N; ++j) {
        // The boundary sample rounds to either side of the seam; the DSP
        // class comparison above covers it.
        if (j == 50) continue;
        CAPTURE(j);
        // Closed form: from 0.5 at each cycle, 0.01 a sample each way; up
        // bounces off 1 after 50 samples, down off 0.
        const double up = 0.5 + 0.01 * j;
        const double down = 0.5 - 0.01 * j;
        if (block < 4) {
          CHECK(left[j] == doctest::Approx(up <= 1.0 ? up : 2.0 - up).epsilon(1e-5));
          CHECK(right[j] == doctest::Approx(down >= 0.0 ? down : -down).epsilon(1e-5));
        } else {
          CHECK(left[j] == doctest::Approx(2.0 * (up <= 1.0 ? up : up - 1.0)).epsilon(1e-5));
          CHECK(right[j] == doctest::Approx(4.0 * (down >= 0.0 ? down : down + 1.0)).epsilon(1e-5));
        }
      }
      CHECK(left[0] == 0.f);
    }
  }

  TEST_CASE("~snowfall: numbers, lists, messages and blocks on the audio thread allocate nothing "
            "(#1085)") {
    Sinks sinks;
    dSnowfall obj;
    obj.SetParams("4");
    sinks.Wire(obj);
    buffer in(N);
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = static_cast<float>(i % 32) / 32.f;
    const std::string direction = "direction 1 2 3 4";
    const std::string boundary = "boundarymode 2";
    const std::string list = "wanderprob 0.5 0.5";
    const std::string number = "0.5";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      for (int i = 0; i < 2; ++i) {
        obj.GetInlet(0)->SetFloat(0.5f, YSE::T_DSP);
        obj.GetInlet(0)->SetInt(1, YSE::T_DSP);
        obj.GetInlet(0)->SetList(list, YSE::T_DSP);
        obj.GetInlet(0)->SetList(number, YSE::T_DSP);
        obj.SetMessage(direction, 0.f);
        obj.SetMessage(boundary, 0.f);
        obj.Calculate(YSE::T_DSP);
        obj.ResetDSP();
        obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
        obj.Calculate(YSE::T_DSP);
        obj.ResetDSP();
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

// Tests for the ~curve patcher object (patcher/generatorObjects/dCurve),
// issue #1065. The ramps are DSP::curveSegments' and are tested in
// Tests/dsp/test_curve_segments.cpp; these show that the numbers, lists,
// messages and arguments reach it, that the done bang and the active state
// go out only through the armed outlets (#1037), and that it works in a
// rendered patch after a JSON round trip.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/generatorObjects/dCurve.h"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BangSink;
using TestHelpers::BufferSink;
using TestHelpers::IntSink;
using TestHelpers::ScopedSampleRate;
using TestHelpers::Wire;
using YSE::PATCHER::dCurve;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // ~curve with sinks on its three outlets. Block() runs one block and the
  // flush the patcher runs after its traversal; `done` and `active` say what
  // was sent (active -1 for nothing).
  struct Rig {
    BufferSink out;
    BangSink doneSink;
    IntSink activeSink;
    dCurve curve;
    bool done = false;
    int active = -1;

    Rig() {
      Wire(curve, 0, out);
      Wire(curve, 1, doneSink);
      Wire(curve, 2, activeSink);
    }
    void Block() {
      doneSink.gotBang = false;
      activeSink.gotInt = false;
      curve.ResetDSP();
      curve.Calculate(YSE::T_DSP);
      curve.FlushArmedOutlets(YSE::T_GUI);
      done = doneSink.gotBang;
      active = activeSink.gotInt ? activeSink.received : -1;
    }
    float At(unsigned int i) const {
      return out.received->getPtr()[i];
    }
  };

  // Sample k (0-based) of a power-curve segment from `a` to `b` over `n`
  // samples, the closed form written out independently of the class.
  float Power(float a, float b, unsigned int n, unsigned int k, double c) {
    const double f = static_cast<double>(k + 1) / static_cast<double>(n);
    return static_cast<float>(a + (b - a) * std::pow(f, (1.0 + c) / (1.0 - c)));
  }

  float Exponential(float a, float b, unsigned int n, unsigned int k, double c) {
    const double f = static_cast<double>(k + 1) / static_cast<double>(n);
    const double g = 2.0 * std::log((1.0 + c) / (1.0 - c));
    return static_cast<float>(a + (b - a) * (std::exp(g * f) - 1.0) / (std::exp(g) - 1.0));
  }

  float GuiFloat(YSE::pHandle* h) {
    return std::stof(h->GetGuiValue());
  }

  int GuiInt(YSE::pHandle* h) {
    return std::stoi(h->GetGuiValue());
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~curve: registry and shape (#1065)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_CURVE);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~curve");
    CHECK(obj->GetInputs() == 3);
    CHECK(obj->GetOutputs() == 3);
    CHECK_FALSE(obj->IsDSPInput(0));
    CHECK_FALSE(obj->IsDSPInput(1));
    CHECK_FALSE(obj->IsDSPInput(2));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(obj->OutputDataType(1) == YSE::OUT_TYPE::BANG);
    CHECK(obj->OutputDataType(2) == YSE::OUT_TYPE::INT);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_CURVE));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::OSC);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 3);
  }

  TEST_CASE("~curve: holds 0, or the initial argument, until a target arrives (#1065)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.Block();
    REQUIRE(rig.out.received != nullptr);
    REQUIRE(rig.out.received->getLength() == N);
    for (unsigned int i = 0; i < N; ++i)
      REQUIRE(rig.At(i) == 0.f);
    CHECK_FALSE(rig.done);
    CHECK(rig.active == -1);

    Rig initial;
    initial.curve.SetParams("0.5");
    initial.Block();
    for (unsigned int i = 0; i < N; ++i)
      REQUIRE(initial.At(i) == 0.5f);
  }

  TEST_CASE("~curve: a number ramps over the middle inlet's time with the right inlet's curve; "
            "the time is used once; done and active are armed, then sent (#1065, #1037)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.curve.GetInlet(2)->SetFloat(0.5f, YSE::T_GUI);
    rig.curve.GetInlet(1)->SetInt(200, YSE::T_GUI);
    rig.curve.GetInlet(0)->SetFloat(2.f, YSE::T_GUI);
    CHECK(rig.curve.GetParams() == "");

    rig.curve.ResetDSP();
    rig.curve.Calculate(YSE::T_DSP);
    CHECK_FALSE(rig.activeSink.gotInt); // CALC() only arms
    CHECK(rig.curve.GetOutlet(2)->Armed());
    rig.curve.FlushArmedOutlets(YSE::T_GUI);
    CHECK(rig.activeSink.gotInt);
    CHECK(rig.activeSink.received == 1);
    for (unsigned int k = 0; k < N; ++k)
      REQUIRE(rig.At(k) == doctest::Approx(Power(0.f, 2.f, 200, k, 0.5)));

    rig.Block(); // samples 128-255: the ramp ends at sample 199
    CHECK(rig.done);
    CHECK(rig.active == 0);
    CHECK(rig.At(199 - N) == 2.f);
    CHECK(rig.At(N - 1) == 2.f);
    rig.Block();
    CHECK_FALSE(rig.done);
    CHECK(rig.active == -1);

    // The time was used: the next number is a jump, done in its block.
    rig.curve.GetInlet(0)->SetInt(-1, YSE::T_GUI);
    rig.Block();
    CHECK(rig.At(0) == -1.f);
    CHECK(rig.done);
    CHECK(rig.active == -1); // started and finished in one block
  }

  TEST_CASE("~curve: a list is triplets; a last pair uses the right inlet's curve, a last "
            "single number jumps (#1065)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.curve.GetInlet(2)->SetFloat(-0.4f, YSE::T_GUI);
    rig.curve.GetInlet(0)->SetList("1 20 0.3 0 40", YSE::T_GUI);
    rig.Block();
    for (unsigned int k = 0; k < 20; ++k)
      REQUIRE(rig.At(k) == doctest::Approx(Power(0.f, 1.f, 20, k, 0.3)));
    for (unsigned int k = 0; k < 40; ++k)
      REQUIRE(rig.At(20 + k) == doctest::Approx(Power(1.f, 0.f, 40, k, -0.4)));
    CHECK(rig.At(N - 1) == 0.f);
    CHECK(rig.done);

    // "5" alone is a jump at the end of the list, at sample 10.
    rig.curve.GetInlet(0)->SetList("1 10 0 5", YSE::T_GUI);
    rig.Block();
    for (unsigned int k = 0; k < 9; ++k)
      REQUIRE(rig.At(k) == doctest::Approx(Power(0.f, 1.f, 10, k, 0.0)));
    CHECK(rig.At(9) == 5.f);
    CHECK(rig.At(N - 1) == 5.f);
    CHECK(rig.done);

    rig.curve.GetInlet(0)->SetList("2 10", YSE::T_GUI); // a pair alone
    rig.Block();
    for (unsigned int k = 0; k < 10; ++k)
      REQUIRE(rig.At(k) == doctest::Approx(Power(5.f, 2.f, 10, k, -0.4)));
    CHECK(rig.curve.GetParams() == "");
  }

  TEST_CASE("~curve: arguments set initial value, curve and shapemode (#1065)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.curve.SetParams("1 0.6 1");
    rig.curve.GetInlet(1)->SetFloat(100.f, YSE::T_GUI);
    rig.curve.GetInlet(0)->SetFloat(0.f, YSE::T_GUI);
    rig.Block();
    for (unsigned int k = 0; k < 100; ++k)
      REQUIRE(rig.At(k) == doctest::Approx(Exponential(1.f, 0.f, 100, k, 0.6)).epsilon(1e-5));
  }

  TEST_CASE("~curve: stop, pause, resume, factor and shapemode messages (#1065)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.curve.GetInlet(0)->SetList("512 512 0", YSE::T_GUI);
    rig.Block();
    CHECK(rig.active == 1);
    CHECK(rig.At(N - 1) == doctest::Approx(128.f));

    rig.curve.SetMessage("pause", 0.f);
    rig.Block();
    CHECK(rig.active == 0);
    CHECK(rig.At(0) == doctest::Approx(128.f));
    CHECK(rig.At(N - 1) == doctest::Approx(128.f));

    rig.curve.GetInlet(0)->SetMessage("resume", YSE::T_GUI);
    rig.Block();
    CHECK(rig.active == 1);
    CHECK(rig.At(0) == doctest::Approx(129.f));

    // A word-led list (through a receive) is a message too.
    rig.curve.GetInlet(0)->SetList("stop", YSE::T_GUI);
    rig.Block();
    CHECK(rig.active == 0);
    CHECK_FALSE(rig.done); // a stop is not an end
    rig.Block();
    CHECK(rig.At(N - 1) == doctest::Approx(256.f));

    // factor sets the curve for numbers; shapemode the formula.
    rig.curve.SetMessage("factor 0.5", 0.f);
    rig.curve.SetMessage("shapemode 1", 0.f);
    rig.curve.GetInlet(1)->SetFloat(64.f, YSE::T_GUI);
    rig.curve.GetInlet(0)->SetFloat(0.f, YSE::T_GUI);
    rig.Block();
    for (unsigned int k = 0; k < 64; ++k)
      REQUIRE(rig.At(k) == doctest::Approx(Exponential(256.f, 0.f, 64, k, 0.5)).epsilon(1e-5));

    // Malformed or unknown messages change nothing.
    rig.curve.SetMessage("factor", 0.f);
    rig.curve.SetMessage("factor nan", 0.f);
    rig.curve.SetMessage("shapemode abc", 0.f);
    rig.curve.SetMessage("set 3", 0.f);
    rig.curve.SetMessage("", 0.f);
    rig.curve.GetInlet(1)->SetFloat(64.f, YSE::T_GUI);
    rig.curve.GetInlet(0)->SetFloat(128.f, YSE::T_GUI);
    rig.Block();
    for (unsigned int k = 0; k < 64; ++k)
      REQUIRE(rig.At(k) == doctest::Approx(Exponential(0.f, 128.f, 64, k, 0.5)).epsilon(1e-5));
    CHECK(rig.curve.GetParams() == "");
  }

  TEST_CASE("~curve: a list and a stop in one block apply in the order they came (#1065)") {
    ScopedSampleRate rate(1000);
    Rig stopLast;
    stopLast.curve.GetInlet(0)->SetList("100 100 0", YSE::T_GUI);
    stopLast.curve.SetMessage("stop", 0.f);
    stopLast.Block();
    CHECK(stopLast.At(N - 1) == 0.f);
    CHECK(stopLast.active == -1);

    Rig listLast;
    listLast.curve.SetMessage("stop", 0.f);
    listLast.curve.GetInlet(0)->SetList("100 100 0", YSE::T_GUI);
    listLast.Block();
    CHECK(listLast.At(N - 1) == 100.f);
    CHECK(listLast.done);

    Rig pauseFirst;
    pauseFirst.curve.SetMessage("pause", 0.f);
    pauseFirst.curve.GetInlet(0)->SetList("256 256 0", YSE::T_GUI);
    pauseFirst.Block();
    CHECK(pauseFirst.active == 1); // the list ended the pause
  }

  TEST_CASE("~curve: malformed lists are refused whole, long ones cut to the capacity "
            "(#1065)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.curve.GetInlet(0)->SetList("1 10 0 x 10", YSE::T_GUI);
    rig.curve.GetInlet(0)->SetList("1 nan 0", YSE::T_GUI);
    rig.curve.GetInlet(0)->SetFloat(NAN, YSE::T_GUI);
    CHECK(rig.curve.Dropped() == 3);
    rig.Block();
    CHECK(rig.At(N - 1) == 0.f);
    CHECK(rig.active == -1);

    std::string longList;
    for (unsigned int i = 0; i < YSE::DSP::curveSegments::DEFAULT_POINTS + 1; ++i)
      longList += std::to_string(i + 1) + " 0 0 ";
    rig.curve.GetInlet(0)->SetList(longList, YSE::T_GUI);
    CHECK(rig.curve.Truncated() == 1);
    rig.Block();
    CHECK(rig.At(0) == static_cast<float>(YSE::DSP::curveSegments::DEFAULT_POINTS));
  }

  TEST_CASE("~curve: the arguments survive a DumpJSON / ParseJSON round trip (#1065)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* a = src.CreateObject(YSE::OBJ::D_CURVE, "0.25 -0.5 1");
      REQUIRE(a != nullptr);
      YSE::pHandle* b = src.CreateObject(YSE::OBJ::D_CURVE, "");
      REQUIRE(b != nullptr);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* a = dst.GetHandleFromID(0);
    YSE::pHandle* b = dst.GetHandleFromID(1);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(std::string(a->Type()) == "~curve");
    CHECK(a->GetParams() == "0.25 -0.5 1");
    CHECK(b->GetParams() == "");
  }

  TEST_CASE("~curve: in a rendered patch, a message box plays a list into snapshots, a counter "
            "and the active state, after a JSON round trip (#1065)") {
    ScopedSampleRate rate(1000);
    std::string dump;
    unsigned int boxID = 0;
    unsigned int levelID = 0;
    unsigned int countID = 0;
    unsigned int activeID = 0;
    {
      patcherImplementation src(1, nullptr);
      // From the initial 1: down to 0 over 256 samples with a 0.5 curve,
      // then straight up to 0.5 over 128.
      YSE::pHandle* box = src.CreateObject(YSE::OBJ::G_MESSAGE, "0 256 0.5 0.5 128 0");
      YSE::pHandle* curve = src.CreateObject(YSE::OBJ::D_CURVE, "1");
      YSE::pHandle* snap = src.CreateObject(YSE::OBJ::D_SNAPSHOT, "1 127");
      YSE::pHandle* level = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      YSE::pHandle* count = src.CreateObject(YSE::OBJ::G_COUNTER, "");
      YSE::pHandle* active = src.CreateObject(YSE::OBJ::G_INT, "");
      REQUIRE(box != nullptr);
      REQUIRE(curve != nullptr);
      REQUIRE(snap != nullptr);
      REQUIRE(level != nullptr);
      REQUIRE(count != nullptr);
      REQUIRE(active != nullptr);
      src.Connect(box, 0, curve, 0);
      src.Connect(curve, 0, snap, 0);
      src.Connect(snap, 0, level, 1);
      src.Connect(curve, 1, count, 0);
      src.Connect(curve, 2, active, 1);
      boxID = box->GetID();
      levelID = level->GetID();
      countID = count->GetID();
      activeID = active->GetID();
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* box = dst.GetHandleFromID(boxID);
    YSE::pHandle* level = dst.GetHandleFromID(levelID);
    YSE::pHandle* count = dst.GetHandleFromID(countID);
    YSE::pHandle* active = dst.GetHandleFromID(activeID);
    REQUIRE(box != nullptr);
    REQUIRE(level != nullptr);
    REQUIRE(count != nullptr);
    REQUIRE(active != nullptr);

    dst.Calculate(YSE::T_DSP);
    CHECK(GuiFloat(level) == 1.f); // the initial argument
    CHECK(GuiInt(count) == 0);

    box->SetBang(0);
    // The snapshots report each block's last sample.
    const float levels[] = {Power(1.f, 0.f, 256, N - 1, 0.5), 0.f, 0.5f, 0.5f};
    const int counts[] = {0, 0, 1, 1};
    const int actives[] = {1, 1, 0, 0};
    for (int block = 0; block < 4; ++block) {
      CAPTURE(block);
      dst.Calculate(YSE::T_DSP);
      CHECK(GuiFloat(level) == doctest::Approx(levels[block]));
      CHECK(GuiInt(count) == counts[block]);
      CHECK(GuiInt(active) == actives[block]);
    }
  }

  TEST_CASE("~curve: numbers, lists, messages, blocks and the flush on the audio thread "
            "allocate nothing (#1065)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    const std::string list = "1 50 0.5 0 30 -0.5 0.5";
    const std::string pause = "pause";
    const std::string resume = "resume";
    const std::string factor = "factor 0.2";
    const std::string shape = "shapemode 1";
    const std::string stop = "stop";
    rig.Block();
    {
      TestHelpers::ProbeScope probe;
      rig.curve.GetInlet(2)->SetFloat(0.3f, YSE::T_DSP);
      rig.curve.GetInlet(2)->SetInt(0, YSE::T_DSP);
      rig.curve.GetInlet(1)->SetFloat(20.f, YSE::T_DSP);
      rig.curve.GetInlet(1)->SetInt(10, YSE::T_DSP);
      rig.curve.GetInlet(0)->SetFloat(0.5f, YSE::T_DSP);
      rig.Block();
      rig.curve.GetInlet(0)->SetInt(1, YSE::T_DSP);
      rig.curve.GetInlet(0)->SetList(list, YSE::T_DSP);
      rig.Block();
      rig.curve.GetInlet(0)->SetMessage(pause, YSE::T_DSP);
      rig.Block();
      rig.curve.GetInlet(0)->SetMessage(resume, YSE::T_DSP);
      rig.curve.GetInlet(0)->SetMessage(factor, YSE::T_DSP);
      rig.curve.GetInlet(0)->SetMessage(shape, YSE::T_DSP);
      rig.Block();
      rig.curve.GetInlet(0)->SetMessage(stop, YSE::T_DSP);
      rig.Block();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

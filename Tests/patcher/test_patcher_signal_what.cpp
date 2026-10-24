// Tests for the ~what patcher object (patcher/analysis/dWhat), issue #1079.
// The impulses are DSP::phasorWhat's and are tested in
// Tests/dsp/test_phasor_what.cpp; these show that the signal, numbers, lists,
// messages and arguments reach it, alone and in a rendered patch after a JSON
// round trip.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/analysis/dWhat.h"
#include "dsp/generators/phasor.hpp"
#include "dsp/generators/phasorWhat.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::phasorWhat;
using YSE::PATCHER::dWhat;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dWhat& obj, BufferSink& out) {
    obj.ConnectOutlet(out.GetInlet(0), 0);
    out.ConnectInlet(obj.GetOutlet(0), 0);
  }

  unsigned int Impulses(buffer& b) {
    unsigned int count = 0;
    for (unsigned int i = 0; i < b.getLength(); ++i)
      if (b.getPtr()[i] == 1.f) ++count;
    return count;
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~what: registry and shape (#1079)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_WHAT);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~what");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 1);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_WHAT));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::ANALYSIS);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~what: numbers, lists and messages against the DSP class (#1079, #1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink out;
    dWhat obj;
    Wire(obj, out);
    phasorWhat reference;
    // One 128-sample input cycle per 128 samples, for the object and the
    // reference.
    phasor objSource;
    phasor refSource;

    const auto step = [&]() {
      obj.ResetDSP(); // as the patcher does before every block
      buffer& in = objSource(375.f, N);
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(out.received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(refSource(375.f, N)), 0.f));
    };

    // Default: no values, nothing fires.
    step();
    CHECK(Impulses(*out.received) == 0);

    // A list: four values, four impulses per 128 samples.
    obj.GetInlet(0)->SetList("0 0.25 0.5 0.75", YSE::T_GUI);
    const float quarters[] = {0.f, 0.25f, 0.5f, 0.75f};
    reference.matches(quarters, 4);
    step();
    CHECK(Impulses(*out.received) == 4 * N / 128);
    CHECK(out.received->getPtr()[33] == 1.f);

    // A number replaces them with one value; so does an int.
    obj.GetInlet(0)->SetFloat(0.3f, YSE::T_GUI);
    const float point3[] = {0.3f};
    reference.matches(point3, 1);
    step();
    CHECK(out.received->getPtr()[39] == 1.f);
    obj.GetInlet(0)->SetInt(0, YSE::T_GUI);
    const float zero[] = {0.f};
    reference.matches(zero, 1);
    step();

    // matches and clear.
    obj.SetMessage("matches 0.5 0.75", 0.f);
    const float late[] = {0.5f, 0.75f};
    reference.matches(late, 2);
    step();
    CHECK(Impulses(*out.received) == 2 * N / 128);
    obj.SetMessage("clear", 0.f);
    reference.clear();
    step();
    CHECK(Impulses(*out.received) == 0);
    obj.SetMessage("matches 0.5", 0.f);
    reference.matches(late, 1);
    step();
    obj.SetMessage("matches", 0.f);
    reference.clear();
    step();

    // A command that arrives as a list (text through a receive) is a command.
    obj.GetInlet(0)->SetList("matches 0.25", YSE::T_GUI);
    reference.matches(quarters + 1, 1);
    step();
    CHECK(out.received->getPtr()[33] == 1.f);

    // triggermode: 1 falls only, so a rising ramp fires nothing; 3 equals.
    obj.SetMessage("triggermode 1", 0.f);
    reference.triggerMode(phasorWhat::DESCENDING);
    step();
    CHECK(Impulses(*out.received) == 0);
    obj.SetMessage("triggermode 3", 0.f);
    reference.triggerMode(phasorWhat::EQUALS);
    step();
    CHECK(out.received->getPtr()[32] == 1.f);
    obj.SetMessage("triggermode 0", 0.f);
    reference.triggerMode(phasorWhat::ASCENDING);
    step();

    // syncupdate 1: a new list waits for the input to wrap. Each 128
    // samples is one whole cycle, so it lands at the next one's first sample.
    obj.SetMessage("syncupdate 1", 0.f);
    obj.SetMessage("matches 0.5", 0.f);
    reference.syncUpdate(true).matches(late, 1);
    step();
    obj.SetMessage("syncupdate 0", 0.f);
    reference.syncUpdate(false);
    step();

    // Malformed or unknown messages: ignored.
    obj.SetMessage("triggermode", 0.f);
    obj.SetMessage("triggermode 4", 0.f);
    obj.SetMessage("triggermode -1", 0.f);
    obj.SetMessage("syncupdate x", 0.f);
    obj.SetMessage("speed 3", 0.f);
    obj.SetMessage("", 0.f);
    step();
    step();

    // Unconnected: the inlet reads 0, a ramp that never moves.
    BufferSink idleOut;
    dWhat idle;
    Wire(idle, idleOut);
    idle.GetInlet(0)->SetList("0 0.5", YSE::T_GUI);
    idle.Calculate(YSE::T_DSP);
    REQUIRE(idleOut.received != nullptr);
    CHECK(Impulses(*idleOut.received) == 0);
  }

  TEST_CASE("~what: arguments are the values, with triggermode and syncupdate (#1079)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto check = [](const std::string& args, phasorWhat& reference) {
      CAPTURE(args);
      BufferSink out;
      dWhat obj;
      Wire(obj, out);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      phasor objSource;
      phasor refSource;
      for (int block = 0; block < 3; ++block) {
        CAPTURE(block);
        obj.ResetDSP();
        buffer& in = objSource(-375.f, N);
        obj.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
        obj.Calculate(YSE::T_DSP);
        REQUIRE(out.received != nullptr);
        CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(refSource(-375.f, N)), 0.f));
      }
    };

    const float v[] = {0.25f, 0.6f};
    phasorWhat plain;
    plain.matches(v, 2);
    check("0.25 0.6", plain);

    // The keywords anywhere among the values; a falling ramp in mode 0
    // fires nothing.
    phasorWhat rising;
    rising.matches(v, 2).triggerMode(phasorWhat::ASCENDING);
    check("0.25 triggermode 0 0.6", rising);
    phasorWhat falling;
    falling.matches(v, 2).triggerMode(phasorWhat::DESCENDING).syncUpdate(true);
    check("syncupdate 1 triggermode 1 0.25 0.6", falling);

    // A keyword without a number, or out of range, is skipped.
    phasorWhat skipped;
    skipped.matches(v, 2);
    check("0.25 triggermode 9 0.6 syncupdate", skipped);

    phasorWhat none;
    check("", none);
  }

  TEST_CASE("~what: in a rendered patch, a ~phasor clock with a list from a receive, after a JSON "
            "round trip (#1079)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* what = src.CreateObject(YSE::OBJ::D_WHAT, "triggermode 0 0 0.25 0.5 0.75");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1079");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(what != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, what, 0);
      src.Connect(cmd, 0, what, 0);
      src.Connect(what, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    phasor source;
    phasorWhat reference;
    const float quarters[] = {0.f, 0.25f, 0.5f, 0.75f};
    reference.matches(quarters, 4).triggerMode(phasorWhat::ASCENDING);
    for (int block = 0; block < 6; ++block) {
      CAPTURE(block);
      if (block == 3) {
        REQUIRE(dst.PassData("0.5", "cmd1079", YSE::T_GUI));
        const float half[] = {0.5f};
        reference.matches(half, 1);
      }
      if (block == 4) {
        REQUIRE(dst.PassData("matches 0.125 0.625", "cmd1079", YSE::T_GUI));
        const float two[] = {0.125f, 0.625f};
        reference.matches(two, 2);
      }
      dst.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(source(375.f, N)), 0.f));
      if (block < 3) {
        CHECK(Impulses(dst.output[0]) == 4 * N / 128);
        CHECK(dst.output[0].getPtr()[65] == 1.f);
      }
      if (block == 3) CHECK(Impulses(dst.output[0]) == N / 128);
      if (block == 4) CHECK(dst.output[0].getPtr()[17] == 1.f);
    }
  }

  TEST_CASE("~what: numbers, lists, messages and blocks on the audio thread allocate nothing "
            "(#1079)") {
    BufferSink out;
    dWhat obj;
    Wire(obj, out);
    buffer in(N);
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = static_cast<float>(i) / N;
    const std::string list = "0 0.25 0.5";
    const std::string matches = "matches 0.1 0.9";
    const std::string clear = "clear";
    const std::string mode = "triggermode 3";
    const std::string sync = "syncupdate 1";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetFloat(0.5f, YSE::T_DSP);
      obj.GetInlet(0)->SetInt(0, YSE::T_DSP);
      obj.GetInlet(0)->SetList(list, YSE::T_DSP);
      obj.SetMessage(matches, 0.f);
      obj.SetMessage(clear, 0.f);
      obj.SetMessage(mode, 0.f);
      obj.SetMessage(sync, 0.f);
      obj.Calculate(YSE::T_DSP);
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

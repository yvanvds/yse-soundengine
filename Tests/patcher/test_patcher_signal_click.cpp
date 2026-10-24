// Tests for the ~click patcher object (patcher/generatorObjects/dClick),
// issue #1058. The signal is DSP::click's and is tested in
// Tests/dsp/test_click.cpp; these show that bangs, numbers, `set`, the
// trigger signal and the waveform arguments reach it, alone and in a
// rendered patch driven by a receive after a JSON round trip.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/generatorObjects/dClick.h"
#include "dsp/generators/click.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::PATCHER::dClick;
using YSE::PATCHER::patcherImplementation;

namespace {
  void Wire(dClick& click, BufferSink& sink) {
    click.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(click.GetOutlet(0), 0);
  }

  // The first samples of `block` are `head`, and the rest is silence.
  void CheckHead(buffer& block, const std::vector<float>& head) {
    REQUIRE(block.getLength() == YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < block.getLength(); ++i) {
      const float expected = i < head.size() ? head[i] : 0.f;
      if (block.getPtr()[i] != expected) {
        CAPTURE(i);
        CHECK(block.getPtr()[i] == expected);
        return;
      }
    }
  }

  bool Silent(buffer& block) {
    for (unsigned int i = 0; i < block.getLength(); ++i)
      if (block.getPtr()[i] != 0.f) return false;
    return true;
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~click: registry and shape (#1058)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_CLICK);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~click");
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->IsDSPInput(0));
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::D_CLICK));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::OSC);
    CHECK(obj->IsDSPObject());
    CHECK(obj->GetParamCount() == 1);
  }

  TEST_CASE("~click: a bang is a unit impulse at the next block, then silence (#1058)") {
    dClick click;
    BufferSink sink;
    Wire(click, sink);
    click.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CHECK(Silent(*sink.received));

    click.GetInlet(0)->SetBang(YSE::T_GUI);
    click.Calculate(YSE::T_DSP);
    CheckHead(*sink.received, {1.f});
    click.Calculate(YSE::T_DSP);
    CHECK(Silent(*sink.received));
  }

  TEST_CASE("~click: a float and an int play it scaled (#1058)") {
    dClick click;
    BufferSink sink;
    Wire(click, sink);
    click.SetParams("1 0.5");
    click.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    click.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CheckHead(*sink.received, {0.5f, 0.25f});

    click.GetInlet(0)->SetInt(-2, YSE::T_GUI);
    click.Calculate(YSE::T_DSP);
    CheckHead(*sink.received, {-2.f, -1.f});

    // A message with a number routes to the same handlers.
    click.GetInlet(0)->SetMessage("3", YSE::T_GUI);
    click.Calculate(YSE::T_DSP);
    CheckHead(*sink.received, {3.f, 1.5f});
  }

  TEST_CASE("~click: the arguments are the waveform (#1058)") {
    dClick click;
    BufferSink sink;
    Wire(click, sink);
    click.SetParams("1 0.75 0.5 0.25");
    click.GetInlet(0)->SetBang(YSE::T_GUI);
    click.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CheckHead(*sink.received, {1.f, 0.75f, 0.5f, 0.25f});

    // No arguments: the unit impulse again.
    click.SetParams("");
    click.GetInlet(0)->SetBang(YSE::T_GUI);
    click.Calculate(YSE::T_DSP);
    CheckHead(*sink.received, {1.f});
  }

  TEST_CASE("~click: set stores a new waveform, set alone the unit impulse (#1058)") {
    dClick click;
    BufferSink sink;
    Wire(click, sink);
    click.GetInlet(0)->SetMessage("set 0.5 0.25 0.125", YSE::T_GUI);
    click.GetInlet(0)->SetBang(YSE::T_GUI);
    click.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CheckHead(*sink.received, {0.5f, 0.25f, 0.125f});

    // Of two sets before a block the last wins; tokens that are not numbers
    // are skipped.
    click.GetInlet(0)->SetMessage("set 1 1 1", YSE::T_GUI);
    click.GetInlet(0)->SetMessage("set 0.25 word 0.75", YSE::T_GUI);
    click.GetInlet(0)->SetBang(YSE::T_GUI);
    click.Calculate(YSE::T_DSP);
    CheckHead(*sink.received, {0.25f, 0.75f});

    click.GetInlet(0)->SetMessage("set", YSE::T_GUI);
    click.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    click.Calculate(YSE::T_DSP);
    CheckHead(*sink.received, {0.5f});

    // Up to 256 values are kept: two blocks of 128 then silence.
    std::string longSet = "set";
    for (int i = 0; i < 300; ++i)
      longSet += " 0.5";
    click.GetInlet(0)->SetMessage(longSet, YSE::T_GUI);
    click.GetInlet(0)->SetBang(YSE::T_GUI);
    click.Calculate(YSE::T_DSP);
    CheckHead(*sink.received, std::vector<float>(YSE::STANDARD_BUFFERSIZE, 0.5f));
    click.Calculate(YSE::T_DSP);
    CheckHead(*sink.received,
              std::vector<float>(YSE::DSP::click::MAX_LENGTH - YSE::STANDARD_BUFFERSIZE, 0.5f));
    click.Calculate(YSE::T_DSP);
    CHECK(Silent(*sink.received));
    CHECK(click.Dropped() == 0);
  }

  TEST_CASE("~click: a trigger signal plays it at each rise from 0 (#1058, #1036)") {
    dClick click;
    BufferSink sink;
    Wire(click, sink);
    click.SetParams("1 0.5");
    buffer triggers(YSE::STANDARD_BUFFERSIZE);
    triggers = 0.f;
    triggers.getPtr()[5] = 1.f;
    triggers.getPtr()[6] = 1.f;
    triggers.getPtr()[40] = 0.3f;
    click.GetInlet(0)->SetBuffer(&triggers, YSE::T_DSP);
    REQUIRE(sink.received != nullptr);

    YSE::DSP::click reference;
    const float wave[] = {1.f, 0.5f};
    reference.set(wave, 2);
    CHECK(TestHelpers::buffersNearlyEqual(*sink.received, reference(triggers), 0.f));
    CHECK(sink.received->getPtr()[5] == 1.f);
    CHECK(sink.received->getPtr()[6] == 0.5f);
    CHECK(sink.received->getPtr()[40] == 1.f);
    CHECK(sink.received->getPtr()[41] == 0.5f);
  }

  TEST_CASE("~click: in a rendered patch, banged through a receive, after a JSON round trip "
            "(#1058)") {
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* recv = src.CreateObject(YSE::OBJ::G_RECEIVE, "hit1058");
      YSE::pHandle* impulse = src.CreateObject(YSE::OBJ::D_CLICK, "");
      YSE::pHandle* shaped = src.CreateObject(YSE::OBJ::D_CLICK, "0.5 0.25");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(recv != nullptr);
      REQUIRE(impulse != nullptr);
      REQUIRE(shaped != nullptr);
      src.Connect(recv, 0, impulse, 0);
      src.Connect(recv, 0, shaped, 0);
      src.Connect(impulse, 0, dac, 0);
      src.Connect(shaped, 0, dac, 1);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    dst.Calculate(YSE::T_DSP);
    CHECK(Silent(dst.output[0]));
    CHECK(Silent(dst.output[1]));

    REQUIRE(dst.PassBang("hit1058", YSE::T_GUI));
    dst.Calculate(YSE::T_DSP);
    CheckHead(dst.output[0], {1.f});
    CheckHead(dst.output[1], {0.5f, 0.25f});

    dst.Calculate(YSE::T_DSP);
    CHECK(Silent(dst.output[0]));
    CHECK(Silent(dst.output[1]));

    // A number through the receive scales it.
    REQUIRE(dst.PassData(2.f, "hit1058", YSE::T_GUI));
    dst.Calculate(YSE::T_DSP);
    CheckHead(dst.output[0], {2.f});
    CheckHead(dst.output[1], {1.f, 0.5f});
  }

  TEST_CASE("~click: triggers, set and blocks on the audio thread allocate nothing (#1058)") {
    dClick click;
    click.SetParams("1 0.5 0.25");
    BufferSink sink;
    Wire(click, sink);
    buffer triggers(YSE::STANDARD_BUFFERSIZE);
    triggers = 0.f;
    triggers.getPtr()[7] = 1.f;
    const std::string set = "set 0.5 0.25 0.125 0.0625";
    click.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      click.GetInlet(0)->SetBang(YSE::T_DSP);
      click.Calculate(YSE::T_DSP);
      click.GetInlet(0)->SetFloat(0.5f, YSE::T_DSP);
      click.GetInlet(0)->SetInt(2, YSE::T_DSP);
      click.GetInlet(0)->SetMessage(set, YSE::T_DSP);
      click.Calculate(YSE::T_DSP);
      click.GetInlet(0)->SetBuffer(&triggers, YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

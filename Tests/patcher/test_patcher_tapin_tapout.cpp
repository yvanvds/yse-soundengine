// Tests for the ~tapin / ~tapout patcher objects (patcher/delay/), issue
// #1105. The line and the taps are DSP::delayLine's and DSP::delayTap's and
// are tested in Tests/dsp/test_delayLine.cpp; feedback and the link cord are
// tested in test_patcher_signal_loops.cpp (#1038). These show that pins,
// messages and parameters reach the classes.
//
// Most cases feed a step (a constant 1 from the first block on) and look for
// the sample where it arrives: a tap at a delay of d samples starts at d.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/delay/dTapin.h"
#include "patcher/delay/dTapout.h"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

using YSE::PATCHER::patcherImplementation;

namespace {

  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  std::string Ms(unsigned int samples) {
    return std::to_string(static_cast<double>(samples) * 1000.0 /
                          static_cast<double>(YSE::SAMPLERATE));
  }

  // The first sample index at which output channel `channel` reaches 0.5,
  // within `blocks` blocks; -1 if it never does.
  long Onset(patcherImplementation& p, unsigned int channel, int blocks) {
    long n = 0;
    for (int b = 0; b < blocks; ++b) {
      p.Calculate(YSE::T_DSP);
      for (unsigned int i = 0; i < N; ++i, ++n) {
        if (p.output[channel].getPtr()[i] >= 0.5f) return n;
      }
    }
    return -1;
  }

  struct Rig {
    patcherImplementation p{2, nullptr};
    YSE::pHandle* src;
    YSE::pHandle* tapin;
    YSE::pHandle* tapout;
    YSE::pHandle* dac;

    explicit Rig(const std::string& delays) {
      src = p.CreateObject(YSE::OBJ::D_ADD, "1");
      tapin = p.CreateObject(YSE::OBJ::D_TAPIN, "1000");
      tapout = p.CreateObject(YSE::OBJ::D_TAPOUT, delays);
      dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      p.Connect(src, 0, tapin, 0);
      p.Connect(tapin, 0, tapout, 0);
      p.Connect(tapout, 0, dac, 0);
    }

    void Message(YSE::pHandle* h, const std::string& text) {
      p.GetObjectFromID(h->GetID())->GetInlet(0)->SetMessage(text, YSE::T_GUI);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("tapin / tapout: registry and shape (#1105)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* tapin = p.CreateObject(YSE::OBJ::D_TAPIN);
    YSE::pHandle* tapout = p.CreateObject(YSE::OBJ::D_TAPOUT, "10 20 30");
    YSE::pHandle* single = p.CreateObject(YSE::OBJ::D_TAPOUT);
    REQUIRE(tapin != nullptr);
    REQUIRE(tapout != nullptr);
    REQUIRE(single != nullptr);
    CHECK(std::string(tapin->Type()) == "~tapin");
    CHECK(tapin->GetInputs() == 1);
    CHECK(tapin->GetOutputs() == 1);
    CHECK(tapin->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(std::string(tapout->Type()) == "~tapout");
    CHECK(tapout->GetInputs() == 3);
    CHECK(tapout->GetOutputs() == 3);
    CHECK(single->GetInputs() == 1);
    CHECK(single->GetOutputs() == 1);

    for (const char* type : {YSE::OBJ::D_TAPIN, YSE::OBJ::D_TAPOUT}) {
      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(type));
      REQUIRE(obj != nullptr);
      CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::DELAY);
      CHECK(obj->IsDSPObject());
    }
    YSE::PATCHER::dTapin in;
    CHECK(in.GetOutlet(0)->IsLink());
    YSE::PATCHER::dTapout out;
    CHECK(out.GetInlet(0)->AcceptsLink());
  }

  TEST_CASE("tapin / tapout: each tap has its own delay (#1105)") {
    Rig r(Ms(N + 10) + " " + Ms(2 * N + 5));
    r.p.Connect(r.tapout, 1, r.dac, 1);
    // Both channels from one render: record the onsets together.
    long first = -1;
    long second = -1;
    long n = 0;
    for (int b = 0; b < 5; ++b) {
      r.p.Calculate(YSE::T_DSP);
      for (unsigned int i = 0; i < N; ++i, ++n) {
        if (first < 0 && r.p.output[0].getPtr()[i] >= 0.5f) first = n;
        if (second < 0 && r.p.output[1].getPtr()[i] >= 0.5f) second = n;
      }
    }
    CHECK(first == N + 10);
    CHECK(second == 2 * N + 5);
  }

  TEST_CASE("tapin / tapout: a number sets a tap's delay, a list several (#1105)") {
    Rig r(Ms(N) + " " + Ms(N));
    r.p.Connect(r.tapout, 1, r.dac, 1);

    SUBCASE("a number at the tap's own inlet") {
      r.tapout->SetFloatData(1, std::stof(Ms(N + 33)));
      r.p.Calculate(YSE::T_DSP); // delivers the value, renders block 0
      CHECK(Onset(r.p, 1, 4) == N + 33 - N);
    }
    SUBCASE("a list at the left inlet, from tap 1 on") {
      r.tapout->SetListData(0, Ms(N + 3) + " " + Ms(N + 40));
      r.p.Calculate(YSE::T_DSP);
      CHECK(Onset(r.p, 1, 4) == N + 40 - N);
    }
  }

  TEST_CASE("tapin / tapout: a connected delay signal wins over the number (#1105)") {
    Rig r(Ms(N));
    YSE::pHandle* time = r.p.CreateObject(YSE::OBJ::D_ADD, Ms(N + 20));
    r.p.Connect(time, 0, r.tapout, 0);
    CHECK(Onset(r.p, 0, 4) == N + 20);
  }

  TEST_CASE("tapin / tapout: no ~tapin linked is silence (#1105)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* tapout = p.CreateObject(YSE::OBJ::D_TAPOUT, "10");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(tapout, 0, dac, 0);
    p.Calculate(YSE::T_DSP);
    for (unsigned int i = 0; i < N; ++i)
      CHECK(p.output[0].getPtr()[i] == 0.f);
  }

  TEST_CASE("tapin / tapout: clear and a new maximum silence the line (#1105)") {
    Rig r(Ms(N));
    for (int b = 0; b < 3; ++b)
      r.p.Calculate(YSE::T_DSP);
    REQUIRE(r.p.output[0].getPtr()[0] == 1.f);

    SUBCASE("clear") {
      r.Message(r.tapin, "clear");
    }
    SUBCASE("a number") {
      r.tapin->SetFloatData(0, 500.f);
    }
    r.p.Calculate(YSE::T_DSP); // reads the block before: cleared
    CHECK(r.p.output[0].getPtr()[0] == 0.f);
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.p.output[0].getPtr()[0] == 1.f);
  }

  TEST_CASE("tapin / tapout: freeze keeps replaying what the line holds (#1105)") {
    // A frozen line loops its last maximum delay, so keep that short enough
    // for the line to be full of the step before the freeze.
    Rig r(Ms(N));
    r.tapin->SetFloatData(0, std::stof(Ms(2 * N)));
    for (int b = 0; b < 4; ++b)
      r.p.Calculate(YSE::T_DSP);
    r.Message(r.tapin, "freeze 1");
    r.src->SetFloatData(1, 2.f); // new input: must not be heard while frozen
    for (int b = 0; b < 6; ++b) {
      r.p.Calculate(YSE::T_DSP);
      CHECK(r.p.output[0].getPtr()[N - 1] == 1.f);
    }
    r.Message(r.tapin, "freeze 0");
    r.p.Calculate(YSE::T_DSP);
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.p.output[0].getPtr()[N - 1] == 2.f);
  }

  TEST_CASE("tapin / tapout: parameters survive a DumpJSON / ParseJSON round trip (#1105)") {
    std::string dump;
    {
      Rig r("12.5 40");
      r.p.SetObjectParams(r.tapin, "250");
      dump = r.p.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
  }

  TEST_CASE("tapin / tapout: messages and blocks on the audio thread allocate nothing (#1105)") {
    Rig r(Ms(N) + " " + Ms(2 * N));
    r.p.Calculate(YSE::T_DSP);
    YSE::PATCHER::pObject* tapin = r.p.GetObjectFromID(r.tapin->GetID());
    YSE::PATCHER::pObject* tapout = r.p.GetObjectFromID(r.tapout->GetID());
    {
      TestHelpers::ProbeScope probe;
      tapin->GetInlet(0)->SetMessage("clear", YSE::T_DSP);
      tapin->GetInlet(0)->SetMessage("freeze 1", YSE::T_DSP);
      tapin->GetInlet(0)->SetFloat(300.f, YSE::T_DSP);
      tapout->GetInlet(1)->SetFloat(25.f, YSE::T_DSP);
      tapout->GetInlet(0)->SetList("5 6", YSE::T_DSP);
      for (int b = 0; b < 8; ++b)
        r.p.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

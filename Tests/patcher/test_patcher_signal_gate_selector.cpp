// Tests for the ~gate and ~selector patcher objects (patcher/routing/dGate,
// dSelector), issue #1133. The routing is DSP::signalSelector's and is tested
// in Tests/dsp/test_signal_selector.cpp; these show that the pins, numbers,
// next / bang, the ramptime and stepmode messages and the arguments reach it,
// that a connected control signal wins over the number (#1036), and that both
// objects route signals in rendered patches after a JSON round trip.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/routing/dGate.h"
#include "patcher/routing/dSelector.h"
#include "dsp/routing/signalSelector.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::signalSelector;
using YSE::PATCHER::dGate;
using YSE::PATCHER::dSelector;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Fill(buffer& b, unsigned int channel) {
    for (unsigned int i = 0; i < b.getLength(); ++i)
      b.getPtr()[i] = static_cast<float>(channel * 1000 + i);
  }

  // A rendered patch after a JSON round trip.
  struct roundTrip {
    std::unique_ptr<patcherImplementation> dst;
    explicit roundTrip(const std::string& dump)
      : dst(std::make_unique<patcherImplementation>(2, nullptr)) {
      dst->ParseJSON(dump);
    }
  };
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~gate / ~selector: registry and shape (#1133)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* gate = p.CreateObject(YSE::OBJ::D_GATE);
    REQUIRE(gate != nullptr);
    CHECK(std::string(gate->Type()) == "~gate");
    CHECK(gate->GetInputs() == 2);
    CHECK(gate->GetOutputs() == 1);
    YSE::pHandle* gate4 = p.CreateObject(YSE::OBJ::D_GATE, "4 2");
    REQUIRE(gate4 != nullptr);
    CHECK(gate4->GetInputs() == 2);
    CHECK(gate4->GetOutputs() == 4);
    for (unsigned int i = 0; i < 2; ++i)
      CHECK(gate4->IsDSPInput(i));
    for (unsigned int i = 0; i < 4; ++i)
      CHECK(gate4->OutputDataType(i) == YSE::OUT_TYPE::BUFFER);

    YSE::pHandle* sel = p.CreateObject(YSE::OBJ::D_SELECTOR);
    REQUIRE(sel != nullptr);
    CHECK(std::string(sel->Type()) == "~selector");
    CHECK(sel->GetInputs() == 2);
    CHECK(sel->GetOutputs() == 1);
    YSE::pHandle* sel3 = p.CreateObject(YSE::OBJ::D_SELECTOR, "3");
    REQUIRE(sel3 != nullptr);
    CHECK(sel3->GetInputs() == 4);
    CHECK(sel3->GetOutputs() == 1);
    for (unsigned int i = 0; i < 4; ++i)
      CHECK(sel3->IsDSPInput(i));
    CHECK(sel3->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    for (const char* name : {YSE::OBJ::D_GATE, YSE::OBJ::D_SELECTOR}) {
      CAPTURE(name);
      std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(name));
      REQUIRE(o != nullptr);
      CHECK(o->GetCategory() == YSE::PATCHER::pCategory::ROUTING);
      CHECK(o->IsDSPObject());
      CHECK(o->GetParamCount() == 1);
    }

    // The count is clamped to 1-128.
    dGate many;
    many.SetParams("500");
    CHECK(many.Channels() == signalSelector::MAX_CHANNELS);
    CHECK(many.NumOutputs() == static_cast<int>(signalSelector::MAX_CHANNELS));
    dSelector none;
    none.SetParams("0");
    CHECK(none.Channels() == 1);
    CHECK(none.NumInputs() == 2);
  }

  TEST_CASE("~gate: numbers, next, bang and messages against the DSP class (#1133, #1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink out0;
    BufferSink out1;
    BufferSink out2;
    dGate obj;
    obj.SetParams("3");
    BufferSink* sinks[3] = {&out0, &out1, &out2};
    for (int i = 0; i < 3; ++i)
      TestHelpers::Wire(obj, i, *sinks[i], 0);
    signalSelector reference(3);
    buffer in(N);
    buffer refIn(N);
    unsigned int block = 0;

    const auto step = [&](buffer* control = nullptr) {
      obj.ResetDSP(); // as the patcher does before every block
      Fill(in, block);
      Fill(refIn, block);
      ++block;
      obj.GetInlet(1)->SetBuffer(&in, YSE::T_GUI);
      if (control != nullptr) obj.GetInlet(0)->SetBuffer(control, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      if (control != nullptr)
        reference.route(refIn, *control);
      else
        reference.route(refIn);
      for (unsigned int c = 0; c < 3; ++c) {
        CAPTURE(c);
        REQUIRE(sinks[c]->received != nullptr);
        CHECK(TestHelpers::buffersNearlyEqual(*sinks[c]->received, reference.output(c), 0.f));
      }
    };

    // Closed at the start: all silent.
    step();
    CHECK(out0.received->isSilent());

    obj.GetInlet(0)->SetInt(2, YSE::T_GUI);
    reference.open(2);
    step();
    CHECK(TestHelpers::buffersNearlyEqual(*out1.received, in, 0.f));
    obj.GetInlet(0)->SetFloat(3.7f, YSE::T_GUI);
    reference.open(3);
    step();
    // Above the count: the last; 0 or less closes.
    obj.GetInlet(0)->SetInt(9, YSE::T_GUI);
    reference.open(3);
    step();
    obj.GetInlet(0)->SetInt(-1, YSE::T_GUI);
    reference.open(0);
    step();
    // next and bang step, wrapping.
    obj.GetInlet(0)->SetMessage("next", YSE::T_GUI);
    reference.open(1);
    step();
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    reference.open(2);
    step();
    obj.GetInlet(0)->SetMessage("bang", YSE::T_GUI);
    obj.GetInlet(0)->SetMessage("next", YSE::T_GUI);
    reference.open(1);
    step();
    // A number as text through a receive.
    obj.GetInlet(0)->SetList("3", YSE::T_GUI);
    reference.open(3);
    step();

    // ramptime (and its alias ramp) crossfades the next switch.
    obj.GetInlet(0)->SetMessage("ramptime 2", YSE::T_GUI);
    obj.GetInlet(0)->SetInt(1, YSE::T_GUI);
    reference.rampTime(2.f).open(1);
    step();
    CHECK(reference.gain(1) == 1.f);
    obj.GetInlet(0)->SetList("ramp 4", YSE::T_GUI);
    obj.GetInlet(0)->SetInt(2, YSE::T_GUI);
    reference.rampTime(4.f).open(2);
    step();
    obj.SetMessage("ramptime 0", 0.f);
    reference.rampTime(0.f);
    step();

    // A connected control signal wins over the number, per sample.
    buffer control(N);
    for (unsigned int i = 0; i < N; ++i)
      control.getPtr()[i] = static_cast<float>(i % 4);
    obj.GetInlet(0)->SetInt(1, YSE::T_GUI);
    step(&control);
    // A number sent while it is connected is stored, and read once the cord
    // is gone.
    obj.GetInlet(0)->SetInt(3, YSE::T_GUI);
    step(&control);
    reference.open(3);
    step();
    CHECK(TestHelpers::buffersNearlyEqual(*out2.received, in, 0.f));

    // stepmode: each whole-number change of the signal opens the next.
    obj.GetInlet(0)->SetMessage("stepmode 1", YSE::T_GUI);
    reference.stepMode(true);
    step(&control);
    obj.SetMessage("stepmode 0", 0.f);
    reference.stepMode(false);
    step(&control);

    // Unknown or malformed messages: ignored.
    obj.SetMessage("ramptime", 0.f);
    obj.SetMessage("ramptime nan", 0.f);
    obj.SetMessage("stepmode", 0.f);
    obj.SetMessage("abc 2", 0.f);
    obj.SetMessage("", 0.f);
    step();

    // Unconnected input: silence everywhere.
    BufferSink idleOut;
    dGate idle;
    idle.SetParams("1 1");
    TestHelpers::Wire(idle, 0, idleOut, 0);
    idle.Calculate(YSE::T_DSP);
    REQUIRE(idleOut.received != nullptr);
    CHECK(idleOut.received->isSilent());
  }

  TEST_CASE("~selector: numbers, next, bang and messages against the DSP class (#1133, #1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink out;
    dSelector obj;
    obj.SetParams("3 1");
    TestHelpers::Wire(obj, 0, out, 0);
    signalSelector reference(3, 1);
    buffer ins[3] = {buffer(N), buffer(N), buffer(N)};
    for (unsigned int c = 0; c < 3; ++c)
      Fill(ins[c], c + 1);

    // Input 2 stays unconnected: silent.
    const auto step = [&](buffer* control = nullptr) {
      obj.ResetDSP();
      obj.GetInlet(1)->SetBuffer(&ins[0], YSE::T_GUI);
      obj.GetInlet(3)->SetBuffer(&ins[2], YSE::T_GUI);
      if (control != nullptr) obj.GetInlet(0)->SetBuffer(control, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      buffer* refs[3] = {&ins[0], nullptr, &ins[2]};
      buffer& expected =
          control != nullptr ? reference.select(refs, *control) : reference.select(refs);
      REQUIRE(out.received != nullptr);
      CHECK(TestHelpers::buffersNearlyEqual(*out.received, expected, 0.f));
    };

    // The initially open input.
    step();
    CHECK(TestHelpers::buffersNearlyEqual(*out.received, ins[0], 0.f));
    obj.GetInlet(0)->SetInt(3, YSE::T_GUI);
    reference.open(3);
    step();
    CHECK(TestHelpers::buffersNearlyEqual(*out.received, ins[2], 0.f));
    obj.GetInlet(0)->SetInt(2, YSE::T_GUI);
    reference.open(2);
    step();
    CHECK(out.received->isSilent());
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    reference.open(3);
    step();
    obj.GetInlet(0)->SetMessage("next", YSE::T_GUI);
    reference.open(1);
    step();
    obj.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    reference.open(0);
    step();
    obj.GetInlet(0)->SetMessage("ramptime 1", YSE::T_GUI);
    obj.GetInlet(0)->SetInt(3, YSE::T_GUI);
    reference.rampTime(1.f).open(3);
    step();
    step();

    buffer control(N);
    for (unsigned int i = 0; i < N; ++i)
      control.getPtr()[i] = 1.f + static_cast<float>((i / 16) % 3);
    obj.SetMessage("ramptime 0", 0.f);
    reference.rampTime(0.f);
    step(&control);
    obj.SetMessage("stepmode 1", 0.f);
    reference.stepMode(true);
    step(&control);
    // Disconnected again: the number (3).
    obj.SetMessage("stepmode 0", 0.f);
    reference.stepMode(false).open(3);
    step();
    CHECK(TestHelpers::buffersNearlyEqual(*out.received, ins[2], 0.f));
  }

  TEST_CASE("~gate / ~selector: arguments set the count, the open channel and the attributes "
            "(#1133)") {
    const auto check = [](const std::string& args, unsigned int channels, int open, float ramp,
                          bool steps) {
      CAPTURE(args);
      TestHelpers::ScopedSampleRate rate(48000);
      buffer one(N);
      one = 1.f;
      // The ramp shows in a switch from the open channel to the next one.
      buffer control(N);
      for (unsigned int i = 0; i < N; ++i)
        control.getPtr()[i] = i < N / 2 ? 0.f : 1.f;

      signalSelector reference(channels, open);
      reference.rampTime(ramp).stepMode(steps);

      BufferSink out;
      dSelector sel;
      sel.SetParams(args);
      CHECK(sel.GetParams() == args);
      CHECK(sel.Channels() == channels);
      TestHelpers::Wire(sel, 0, out, 0);
      std::vector<buffer> ins(channels, buffer(N));
      std::vector<buffer*> refs(channels);
      for (unsigned int c = 0; c < channels; ++c) {
        Fill(ins[c], c + 1);
        refs[c] = &ins[c];
      }
      for (unsigned int b = 0; b < 2; ++b) {
        sel.ResetDSP();
        for (unsigned int c = 0; c < channels; ++c)
          sel.GetInlet(static_cast<int>(c) + 1)->SetBuffer(&ins[c], YSE::T_GUI);
        if (b == 1) sel.GetInlet(0)->SetBuffer(&control, YSE::T_GUI);
        sel.Calculate(YSE::T_DSP);
        buffer& expected =
            b == 1 ? reference.select(refs.data(), control) : reference.select(refs.data());
        REQUIRE(out.received != nullptr);
        CHECK(TestHelpers::buffersNearlyEqual(*out.received, expected, 0.f));
      }

      dGate gate;
      gate.SetParams(args);
      CHECK(gate.GetParams() == args);
      CHECK(gate.Channels() == channels);
      CHECK(gate.NumOutputs() == static_cast<int>(channels));
    };

    check("", 1, 0, 0.f, false);
    check("2", 2, 0, 0.f, false);
    check("3 2", 3, 2, 0.f, false);
    check("3 2 ramptime 5", 3, 2, 5.f, false);
    check("4 ramp 3 4 stepmode 1", 4, 4, 3.f, true);
    check("stepmode 1 2 1", 2, 1, 0.f, true);
    // An open channel past the count is the last; a word is skipped; a
    // negative ramp is 0.
    check("2 7", 2, 2, 0.f, false);
    check("x 3 1 ramptime -4", 3, 1, 0.f, false);
  }

  TEST_CASE("~selector: picks among ~sig sources by number and by signal in a rendered patch, "
            "after a JSON round trip (#1133)") {
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* a = src.CreateObject(YSE::OBJ::D_SIG, "10");
      YSE::pHandle* b = src.CreateObject(YSE::OBJ::D_SIG, "20");
      YSE::pHandle* c = src.CreateObject(YSE::OBJ::D_SIG, "30");
      YSE::pHandle* sel = src.CreateObject(YSE::OBJ::D_SELECTOR, "3 2");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "sel1133");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(a != nullptr);
      REQUIRE(b != nullptr);
      REQUIRE(c != nullptr);
      REQUIRE(sel != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(a, 0, sel, 1);
      src.Connect(b, 0, sel, 2);
      src.Connect(c, 0, sel, 3);
      src.Connect(cmd, 0, sel, 0);
      src.Connect(sel, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    const auto all = [&](float value) {
      const float* y = dst.output[0].getPtr();
      for (unsigned int i = 0; i < N; ++i)
        if (y[i] != value) return false;
      return true;
    };

    dst.Calculate(YSE::T_DSP);
    CHECK(all(20.f));
    REQUIRE(dst.PassData(3, "sel1133", YSE::T_GUI));
    dst.Calculate(YSE::T_DSP);
    CHECK(all(30.f));
    REQUIRE(dst.PassData("next", "sel1133", YSE::T_GUI));
    dst.Calculate(YSE::T_DSP);
    CHECK(all(10.f));
    REQUIRE(dst.PassData(0, "sel1133", YSE::T_GUI));
    dst.Calculate(YSE::T_DSP);
    CHECK(all(0.f));

    // A control signal: a ~sig 2 in the left inlet wins over the number.
    std::string signalDump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* a = src.CreateObject(YSE::OBJ::D_SIG, "10");
      YSE::pHandle* b = src.CreateObject(YSE::OBJ::D_SIG, "20");
      YSE::pHandle* which = src.CreateObject(YSE::OBJ::D_SIG, "2");
      YSE::pHandle* sel = src.CreateObject(YSE::OBJ::D_SELECTOR, "2 1");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(sel != nullptr);
      src.Connect(a, 0, sel, 1);
      src.Connect(b, 0, sel, 2);
      src.Connect(which, 0, sel, 0);
      src.Connect(sel, 0, dac, 0);
      signalDump = src.DumpJSON();
    }
    patcherImplementation sig(2, nullptr);
    sig.ParseJSON(signalDump);
    sig.Calculate(YSE::T_DSP);
    for (unsigned int i = 0; i < N; ++i)
      CHECK(sig.output[0].getPtr()[i] == 20.f);
  }

  TEST_CASE("~gate: routes a ~sig to one dac channel or the other, and crossfades with a "
            "ramp time, after a JSON round trip (#1133)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto build = [](const char* args) {
      std::string dump;
      {
        patcherImplementation src(2, nullptr);
        YSE::pHandle* dc = src.CreateObject(YSE::OBJ::D_SIG, "1");
        YSE::pHandle* gate = src.CreateObject(YSE::OBJ::D_GATE, args);
        YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "gate1133");
        YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
        REQUIRE(dc != nullptr);
        REQUIRE(gate != nullptr);
        REQUIRE(cmd != nullptr);
        REQUIRE(dac != nullptr);
        src.Connect(dc, 0, gate, 1);
        src.Connect(cmd, 0, gate, 0);
        src.Connect(gate, 0, dac, 0);
        src.Connect(gate, 1, dac, 1);
        dump = src.DumpJSON();
      }
      return roundTrip(dump);
    };

    // Hard: the whole block is on the open channel.
    {
      roundTrip patch = build("2 1");
      patcherImplementation& dst = *patch.dst;
      dst.Calculate(YSE::T_DSP);
      for (unsigned int i = 0; i < N; ++i) {
        CHECK(dst.output[0].getPtr()[i] == 1.f);
        CHECK(dst.output[1].getPtr()[i] == 0.f);
      }
      REQUIRE(dst.PassData(2, "gate1133", YSE::T_GUI));
      dst.Calculate(YSE::T_DSP);
      CHECK(dst.output[0].getPtr()[0] == 0.f);
      CHECK(dst.output[1].getPtr()[0] == 1.f);
      REQUIRE(dst.PassData(0, "gate1133", YSE::T_GUI));
      dst.Calculate(YSE::T_DSP);
      CHECK(dst.output[0].isSilent());
      CHECK(dst.output[1].isSilent());
    }

    // 5 ms at 48 kHz: 240 samples of crossfade, the two channels summing to
    // the input throughout, no step larger than 1/240.
    {
      roundTrip patch = build("2 1 ramptime 5");
      patcherImplementation& dst = *patch.dst;
      dst.Calculate(YSE::T_DSP);
      REQUIRE(dst.PassData("next", "gate1133", YSE::T_GUI));
      float previous = dst.output[0].getPtr()[N - 1];
      float largest = 0.f;
      for (int block = 0; block < 2; ++block) {
        dst.Calculate(YSE::T_DSP);
        for (unsigned int i = 0; i < N; ++i) {
          const float left = dst.output[0].getPtr()[i];
          const float right = dst.output[1].getPtr()[i];
          CHECK(left + right == doctest::Approx(1.f).epsilon(1e-5));
          largest = std::max(largest, std::fabs(left - previous));
          previous = left;
        }
      }
      CHECK(largest == doctest::Approx(1.f / 240.f).epsilon(1e-3));
      CHECK(dst.output[0].getPtr()[N - 1] == 0.f);
      CHECK(dst.output[1].getPtr()[N - 1] == 1.f);
    }
  }

  TEST_CASE("~gate / ~selector: numbers, messages and blocks on the audio thread allocate "
            "nothing (#1133)") {
    BufferSink gateOut;
    BufferSink selOut;
    dGate gate;
    gate.SetParams("3 1");
    dSelector sel;
    sel.SetParams("3 1");
    TestHelpers::Wire(gate, 0, gateOut, 0);
    TestHelpers::Wire(sel, 0, selOut, 0);
    buffer in(N);
    buffer control(N);
    in = 0.5f;
    for (unsigned int i = 0; i < N; ++i)
      control.getPtr()[i] = static_cast<float>(i % 4);
    const std::string next = "next";
    const std::string ramp = "ramptime 3";
    const std::string mode = "stepmode 1";
    const std::string number = "2";
    gate.Calculate(YSE::T_DSP);
    sel.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      for (YSE::PATCHER::pObject* obj : {static_cast<YSE::PATCHER::pObject*>(&gate),
                                         static_cast<YSE::PATCHER::pObject*>(&sel)}) {
        obj->GetInlet(0)->SetInt(2, YSE::T_DSP);
        obj->GetInlet(0)->SetFloat(3.f, YSE::T_DSP);
        obj->GetInlet(0)->SetBang(YSE::T_DSP);
        obj->GetInlet(0)->SetMessage(next, YSE::T_DSP);
        obj->GetInlet(0)->SetMessage(ramp, YSE::T_DSP);
        obj->GetInlet(0)->SetList(number, YSE::T_DSP);
        obj->Calculate(YSE::T_DSP);
        obj->ResetDSP();
        obj->GetInlet(1)->SetBuffer(&in, YSE::T_DSP);
        obj->GetInlet(0)->SetBuffer(&control, YSE::T_DSP);
        obj->Calculate(YSE::T_DSP);
        obj->GetInlet(0)->SetMessage(mode, YSE::T_DSP);
        obj->ResetDSP();
        obj->GetInlet(1)->SetBuffer(&in, YSE::T_DSP);
        obj->GetInlet(0)->SetBuffer(&control, YSE::T_DSP);
        obj->Calculate(YSE::T_DSP);
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

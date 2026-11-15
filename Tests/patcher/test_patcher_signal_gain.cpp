// Tests for the ~gain patcher object (patcher/dynamics/dGain), MSP's gain~,
// issue #1168. The taper and the ramp are DSP::gainFader's and are tested in
// Tests/dsp/test_gain_fader.cpp; these show that numbers, lists, 'set', the
// settings and the right inlet reach it, that the position goes out the right
// outlet when Max sends it and not on 'set', that the GUI value round-trips
// (issue #551), and that a rendered patch fades and is restored by a .preset
// after a JSON round trip.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/dynamics/dGain.h"
#include "dsp/dynamics/gainFader.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using TestHelpers::IntSink;
using TestHelpers::ScopedSampleRate;
using TestHelpers::Wire;
using YSE::DSP::buffer;
using YSE::DSP::gainFader;
using YSE::PATCHER::dGain;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // A ~gain with a sink on each outlet. Block() runs one block of `in`
  // (ones unless given) and returns the output.
  struct Rig {
    BufferSink signal;
    IntSink position;
    dGain gain;
    buffer ones{N};

    Rig() {
      Wire(gain, 0, signal);
      Wire(gain, 1, position);
      ones = 1.f;
    }
    buffer& Block() {
      return Block(ones);
    }
    buffer& Block(buffer& in) {
      gain.ResetDSP();
      gain.GetInlet(0)->SetBuffer(&in, YSE::T_DSP); // the only input: runs CALC()
      REQUIRE(signal.received != nullptr);
      return *signal.received;
    }
    float Out(unsigned int i) const {
      REQUIRE(signal.received != nullptr);
      return signal.received->getPtr()[i];
    }
    void Send(const std::string& message) {
      gain.GetInlet(0)->SetMessage(message, YSE::T_GUI);
    }
    // Whether the right outlet sent since the last call, and what.
    bool Sent() {
      const bool sent = position.gotInt;
      position.gotInt = false;
      return sent;
    }
  };

  float Gain(int p, int size = 158, float scale = 7.94321f, float inc = 1.071519f) {
    return gainFader::Gain(p, size, scale, inc);
  }

  std::vector<int> Cells(const std::string& value) {
    std::vector<int> all;
    std::istringstream in(value);
    std::string token;
    while (in >> token)
      all.push_back(std::stoi(token));
    return all;
  }

  std::vector<int> Cells(YSE::PATCHER::pObject& o) {
    const std::vector<int> all = Cells(o.GetGuiValue());
    // The bulk read is every cell, in order.
    REQUIRE(all.size() == o.GetGuiValueCount());
    for (unsigned int i = 0; i < o.GetGuiValueCount(); ++i)
      CHECK(std::stoi(o.GetGuiValueAt(i)) == all[i]);
    CHECK(o.GetGuiValueAt(o.GetGuiValueCount()).empty());
    return all;
  }

  float GuiFloat(YSE::pHandle* h) {
    return std::stof(h->GetGuiValue());
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~gain: registry and shape (#1168)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_GAIN);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~gain");
    CHECK(obj->GetInputs() == 2);
    CHECK(obj->GetOutputs() == 2);
    CHECK(obj->IsDSPInput(0));
    CHECK_FALSE(obj->IsDSPInput(1));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(obj->OutputDataType(1) == YSE::OUT_TYPE::INT);
    CHECK(obj->GetGuiValueCount() == 2);
    CHECK(obj->GuiValueIsSettable());

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_GAIN));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::DYNAMICS);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 4);

    // At rest: position 0 of 158, silence.
    Rig rig;
    CHECK(Cells(rig.gain) == std::vector<int>{0, 158});
    buffer& out = rig.Block();
    for (unsigned int i = 0; i < N; ++i)
      CHECK(out.getPtr()[i] == 0.f);
  }

  TEST_CASE("~gain: a number moves the fader like DSP::gainFader and sends the position "
            "(#1168)") {
    ScopedSampleRate rate(1000); // one sample per millisecond
    Rig rig;
    gainFader reference;
    buffer in(N);
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = static_cast<float>(std::sin(0.2 * i));

    // An int: the default 10 ms ramp to unity, the same output as the DSP
    // class sample for sample, and the position out the right outlet.
    rig.gain.GetInlet(0)->SetInt(128, YSE::T_GUI);
    CHECK(rig.Sent());
    CHECK(rig.position.received == 128);
    reference.position(128.f);
    {
      buffer& out = rig.Block(in);
      buffer& expected = reference(in);
      for (unsigned int i = 0; i < N; ++i) {
        CAPTURE(i);
        CHECK(out.getPtr()[i] == expected.getPtr()[i]);
      }
    }
    CHECK(rig.Out(N - 1) == doctest::Approx(std::sin(0.2 * (N - 1)) * Gain(128)));

    // A float is used as its whole part; positions are held to the slider.
    rig.gain.GetInlet(0)->SetFloat(140.9f, YSE::T_GUI);
    CHECK(rig.Sent());
    CHECK(rig.position.received == 140);
    rig.gain.GetInlet(0)->SetInt(500, YSE::T_GUI);
    CHECK(rig.position.received == 157);
    rig.gain.GetInlet(0)->SetFloat(-2.f, YSE::T_GUI);
    CHECK(rig.position.received == 0);
    CHECK(Cells(rig.gain) == std::vector<int>{0, 158});

    // NaN changes and sends nothing.
    rig.gain.GetInlet(0)->SetInt(100, YSE::T_GUI);
    rig.Sent();
    rig.gain.GetInlet(0)->SetFloat(std::nanf(""), YSE::T_GUI);
    CHECK_FALSE(rig.Sent());
    rig.Block();
    CHECK(rig.Out(N - 1) == Gain(100));

    // A bang sends the position again.
    rig.gain.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Sent());
    CHECK(rig.position.received == 100);

    // Unconnected, the inlet reads as silence (#1036).
    rig.gain.ResetDSP();
    rig.gain.Calculate(YSE::T_DSP);
    CHECK(rig.Out(0) == 0.f);
    // Positions are not saved.
    CHECK(rig.gain.GetParams() == "");
  }

  TEST_CASE("~gain: the right inlet and 'interp' set the ramp time, at the live sample rate "
            "(#1168)") {
    Rig rig;
    {
      ScopedSampleRate rate(1000);
      rig.gain.GetInlet(1)->SetFloat(20.f, YSE::T_GUI);
      rig.gain.GetInlet(0)->SetInt(128, YSE::T_GUI);
      rig.Block();
      CHECK(rig.Out(9) == doctest::Approx(Gain(128) / 2.0));
      CHECK(rig.Out(18) < Gain(128));
      CHECK(rig.Out(19) == Gain(128));

      rig.Send("interp 4");
      rig.gain.GetInlet(0)->SetInt(0, YSE::T_GUI);
      rig.Block();
      CHECK(rig.Out(1) == doctest::Approx(Gain(128) / 2.0));
      CHECK(rig.Out(3) == 0.f);

      // 0 jumps; NaN changes nothing.
      rig.gain.GetInlet(1)->SetInt(0, YSE::T_GUI);
      rig.gain.GetInlet(1)->SetFloat(std::nanf(""), YSE::T_GUI);
      rig.gain.GetInlet(0)->SetInt(157, YSE::T_GUI);
      rig.Block();
      CHECK(rig.Out(0) == Gain(157));
    }
    {
      // The argument; at twice the rate, 10 ms is twice the samples.
      ScopedSampleRate rate(2000);
      Rig fresh;
      fresh.gain.SetParams("10");
      fresh.gain.GetInlet(0)->SetInt(128, YSE::T_GUI);
      fresh.Block();
      CHECK(fresh.Out(9) == doctest::Approx(Gain(128) / 2.0));
      CHECK(fresh.Out(18) < Gain(128));
      CHECK(fresh.Out(19) == Gain(128));
      CHECK(fresh.gain.GetParams() == "10");
    }
  }

  TEST_CASE("~gain: 'set' moves the fader without sending; the GUI value round-trips "
            "(#1168, #551)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.gain.SetParams("0");
    rig.Send("set 64");
    CHECK_FALSE(rig.Sent());
    CHECK(Cells(rig.gain) == std::vector<int>{64, 158});
    rig.Block();
    CHECK(rig.Out(0) == Gain(64));

    // The cell writes: 'set 0 <position>' and 'set 1 <size>', unsent.
    rig.Send("set 0 120");
    CHECK(Cells(rig.gain) == std::vector<int>{120, 158});
    rig.Send("set 1 100");
    CHECK(Cells(rig.gain) == std::vector<int>{99, 100});
    rig.Block();
    CHECK(rig.Out(0) == Gain(99, 100));
    rig.Send("set 2 5"); // no such cell
    rig.Send("set 0 abc");
    rig.Send("set abc");
    rig.Send("set");
    CHECK(Cells(rig.gain) == std::vector<int>{99, 100});
    CHECK_FALSE(rig.Sent());

    // The whole state as a list: the size first, then the position, which
    // goes out like a number. One number is the position alone.
    const std::string state = rig.gain.GetGuiValue();
    REQUIRE(state == "99 100");
    IntSink sent;
    dGain other;
    Wire(other, 1, sent);
    other.GetInlet(0)->SetList(state, YSE::T_GUI);
    CHECK(Cells(other) == std::vector<int>{99, 100});
    CHECK(sent.received == 99);
    other.GetInlet(0)->SetList("30", YSE::T_GUI);
    CHECK(Cells(other) == std::vector<int>{30, 100});
    other.GetInlet(0)->SetList("140 158", YSE::T_GUI);
    CHECK(Cells(other) == std::vector<int>{140, 158});
    CHECK(sent.received == 140);
    // Through the patcher's message path, word-first text is the command.
    other.GetInlet(0)->SetList("set 0 12", YSE::T_GUI);
    CHECK(Cells(other) == std::vector<int>{12, 158});
    CHECK(sent.received == 140);
  }

  TEST_CASE("~gain: 'size', 'scale' and 'inc' change the taper (#1168)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.gain.SetParams("0 158 2 2");
    rig.gain.GetInlet(0)->SetInt(157, YSE::T_GUI);
    rig.Block();
    CHECK(rig.Out(0) == 1.f); // 2 * 2 ^ -1

    rig.Send("scale 4");
    rig.Block();
    CHECK(rig.Out(0) == 2.f);
    rig.Send("inc 4");
    rig.Block();
    CHECK(rig.Out(0) == 1.f);

    // A smaller size holds the position to its top, and sends nothing.
    rig.Sent();
    rig.Send("size 10");
    CHECK_FALSE(rig.Sent());
    CHECK(Cells(rig.gain) == std::vector<int>{9, 10});
    rig.Block();
    CHECK(rig.Out(0) == 1.f);
    rig.gain.GetInlet(0)->SetInt(8, YSE::T_GUI);
    rig.Block();
    CHECK(rig.Out(0) == 0.25f);

    // Malformed settings change nothing; an increment of 0 is ignored.
    rig.Send("size abc");
    rig.Send("scale");
    rig.Send("inc 0");
    rig.Send("bgcolor 1 0 0 1");
    rig.Send("mouse 1 2");
    rig.Block();
    CHECK(rig.Out(0) == 0.25f);
    CHECK(Cells(rig.gain) == std::vector<int>{8, 10});

    // A live SetParams that shrinks the size holds the position too.
    rig.gain.SetParams("0 5");
    CHECK(Cells(rig.gain) == std::vector<int>{4, 5});
    rig.gain.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.position.received == 4);
    CHECK(rig.gain.GetParams() == "0 5");
  }

  TEST_CASE("~gain: the arguments survive a DumpJSON / ParseJSON round trip (#1168)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_GAIN, "25 128 2.5 1.122") != nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_GAIN, "") != nullptr);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    const char* params[] = {"25 128 2.5 1.122", ""};
    const std::vector<int> cells[] = {{0, 128}, {0, 158}};
    for (unsigned int i = 0; i < 2; ++i) {
      CAPTURE(i);
      YSE::pHandle* h = dst.GetHandleFromID(i);
      REQUIRE(h != nullptr);
      CHECK(std::string(h->Type()) == "~gain");
      CHECK(h->GetParams() == params[i]);
      CHECK(Cells(h->GetGuiValue()) == cells[i]);
    }
  }

  TEST_CASE("~gain: in a rendered patch the fader moves, reports and is restored by a .preset, "
            "after a JSON round trip (#1168)") {
    ScopedSampleRate rate(48000);
    std::string dump;
    unsigned int gainID = 0;
    unsigned int levelID = 0;
    unsigned int positionID = 0;
    unsigned int presetID = 0;
    {
      patcherImplementation src(1, nullptr);
      // ~sig 1 -> ~gain (20 ms) -> ~number monitoring every 10 ms -> .f;
      // the position goes to a .i; a receive moves the fader.
      YSE::pHandle* control = src.CreateObject(YSE::OBJ::G_RECEIVE, "gain1168");
      YSE::pHandle* source = src.CreateObject(YSE::OBJ::D_SIG, "1");
      YSE::pHandle* gain = src.CreateObject(YSE::OBJ::D_GAIN, "20");
      YSE::pHandle* monitor = src.CreateObject(YSE::OBJ::D_NUMBER, "0 10");
      YSE::pHandle* level = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      YSE::pHandle* position = src.CreateObject(YSE::OBJ::G_INT, "");
      YSE::pHandle* preset = src.CreateObject(YSE::OBJ::G_PRESET, "4");
      REQUIRE(control != nullptr);
      REQUIRE(source != nullptr);
      REQUIRE(gain != nullptr);
      REQUIRE(monitor != nullptr);
      REQUIRE(level != nullptr);
      REQUIRE(position != nullptr);
      REQUIRE(preset != nullptr);
      src.Connect(control, 0, gain, 0);
      src.Connect(source, 0, gain, 0);
      src.Connect(gain, 0, monitor, 0);
      src.Connect(monitor, 1, level, 0);
      src.Connect(gain, 1, position, 0);
      gainID = gain->GetID();
      levelID = level->GetID();
      positionID = position->GetID();
      presetID = preset->GetID();
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* gain = dst.GetHandleFromID(gainID);
    YSE::pHandle* level = dst.GetHandleFromID(levelID);
    YSE::pHandle* position = dst.GetHandleFromID(positionID);
    YSE::pHandle* preset = dst.GetHandleFromID(presetID);
    REQUIRE(gain != nullptr);
    REQUIRE(level != nullptr);
    REQUIRE(position != nullptr);
    REQUIRE(preset != nullptr);
    CHECK(gain->GetParams() == "20");

    // Silence until the fader moves.
    for (int k = 0; k < 4; ++k)
      dst.Calculate(YSE::T_DSP);
    CHECK(GuiFloat(level) == 0.f);

    // To unity: 20 ms is 960 samples, 7.5 blocks. Halfway through, the
    // monitor shows a level on the way up; after it, unity.
    REQUIRE(dst.PassData(128, "gain1168", YSE::T_GUI));
    std::vector<float> seen;
    for (int k = 0; k < 4; ++k) {
      dst.Calculate(YSE::T_DSP);
      seen.push_back(GuiFloat(level));
    }
    CHECK(position->GetGuiValue() == "128");
    CHECK(seen.back() > 0.2f);
    CHECK(seen.back() < 0.8f);
    for (int k = 0; k < 12; ++k)
      dst.Calculate(YSE::T_DSP);
    CHECK(GuiFloat(level) == doctest::Approx(Gain(128)));
    CHECK(Cells(gain->GetGuiValue()) == std::vector<int>{128, 158});

    // A .preset stores the fader and recalls it.
    preset->SetListData(0, "store 1");
    REQUIRE(dst.PassData(148, "gain1168", YSE::T_GUI));
    for (int k = 0; k < 16; ++k)
      dst.Calculate(YSE::T_DSP);
    CHECK(GuiFloat(level) == doctest::Approx(Gain(148)));
    CHECK(20.0 * std::log10(GuiFloat(level)) == doctest::Approx(12.0).epsilon(1e-3));
    preset->SetIntData(0, 1);
    CHECK(Cells(gain->GetGuiValue()) == std::vector<int>{128, 158});
    CHECK(position->GetGuiValue() == "128");
    for (int k = 0; k < 16; ++k)
      dst.Calculate(YSE::T_DSP);
    CHECK(GuiFloat(level) == doctest::Approx(Gain(128)));
  }

  TEST_CASE("~gain: numbers, messages and blocks on the audio thread allocate nothing (#1168)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    const std::string list = "64 158";
    const std::string set = "set 70";
    const std::string cell = "set 0 80";
    const std::string size = "size 150";
    const std::string scale = "scale 7";
    const std::string inc = "inc 1.07";
    const std::string interp = "interp 5";
    rig.Block();
    {
      TestHelpers::ProbeScope probe;
      dGain& g = rig.gain;
      g.GetInlet(0)->SetFloat(100.f, YSE::T_DSP);
      g.GetInlet(0)->SetInt(120, YSE::T_DSP);
      g.GetInlet(0)->SetList(list, YSE::T_DSP);
      g.GetInlet(0)->SetMessage(set, YSE::T_DSP);
      g.GetInlet(0)->SetMessage(cell, YSE::T_DSP);
      g.GetInlet(0)->SetMessage(size, YSE::T_DSP);
      g.GetInlet(0)->SetMessage(scale, YSE::T_DSP);
      g.GetInlet(0)->SetMessage(inc, YSE::T_DSP);
      g.GetInlet(0)->SetMessage(interp, YSE::T_DSP);
      g.GetInlet(0)->SetBang(YSE::T_DSP);
      g.GetInlet(1)->SetFloat(20.f, YSE::T_DSP);
      g.GetInlet(1)->SetInt(20, YSE::T_DSP);
      rig.Block();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    // The last word: the bang, after 'set 0 80'.
    CHECK(rig.position.received == 80);
  }

} // TEST_SUITE("patcher")

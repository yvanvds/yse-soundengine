// Tests for the ~stash patcher object (patcher/filters/dStash), issue #1084.
// The recording and playback are DSP::signalStash's and are tested in
// Tests/dsp/test_signal_stash.cpp; these show that the signals, numbers,
// bangs, lists, messages and arguments reach it, alone and in a rendered
// patch after a JSON round trip.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/filters/dStash.h"
#include "dsp/generators/signalStash.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::signalStash;
using YSE::PATCHER::dStash;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dStash& obj, BufferSink& out) {
    obj.ConnectOutlet(out.GetInlet(0), 0);
    out.ConnectInlet(obj.GetOutlet(0), 0);
  }

  // A counter from `start`: sample i reads start + i.
  void Count(buffer& b, float start) {
    for (unsigned int i = 0; i < b.getLength(); ++i)
      b.getPtr()[i] = start + static_cast<float>(i);
  }

  // Drives a ~stash and a reference DSP class with the same signals: a
  // counter, a 375 Hz phasor recording, a 93.75 Hz phasor advancing and a
  // 187.5 Hz phasor times 6 as the write index.
  struct Rig {
    BufferSink out;
    dStash obj;
    signalStash reference;
    phasor objRecord, refRecord, objAdvance, refAdvance, objWrite, refWrite;
    buffer objIn{N};
    buffer refIn{N};
    buffer objIndex{N};
    buffer refIndex{N};
    float start = 0.f;

    Rig() {
      Wire(obj, out);
    }

    static void Scale(buffer& to, buffer& from) {
      for (unsigned int i = 0; i < N; ++i)
        to.getPtr()[i] = from.getPtr()[i] * 6.f;
    }

    void Step() {
      obj.ResetDSP(); // as the patcher does before every block
      Count(objIn, start);
      Count(refIn, start);
      start += static_cast<float>(N);
      Scale(objIndex, objWrite(187.5f, N));
      Scale(refIndex, refWrite(187.5f, N));
      obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
      obj.GetInlet(1)->SetBuffer(&objRecord(375.f, N), YSE::T_GUI);
      obj.GetInlet(2)->SetBuffer(&objAdvance(93.75f, N), YSE::T_GUI);
      obj.GetInlet(3)->SetBuffer(&objIndex, YSE::T_GUI);
      obj.Calculate(YSE::T_DSP);
      REQUIRE(out.received != nullptr);
      buffer& r = reference(refIn, refRecord(375.f, N), refAdvance(93.75f, N), refIndex);
      CHECK(TestHelpers::buffersNearlyEqual(*out.received, r, 0.f));
    }
  };

} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~stash: registry and shape (#1084)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_STASH);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~stash");
    CHECK(obj->GetInputs() == 4);
    CHECK(obj->GetOutputs() == 1);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->IsDSPInput(1));
    CHECK(obj->IsDSPInput(2));
    CHECK(obj->IsDSPInput(3));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_STASH));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::FILTER);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~stash: signals, numbers, lists and messages against the DSP class (#1084, #1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    Rig rig;

    // Defaults: size 1, both levels 0.5, so with one place every recording
    // (sample 65 + 128k) shows at once.
    rig.Step();
    CHECK(rig.out.received->getPtr()[64] == 0.f);
    CHECK(rig.out.received->getPtr()[65] == 65.f);

    // size and mode 2: four places, played one per quarter of the slow ramp.
    rig.obj.SetMessage("size 4", 0.f);
    rig.obj.SetMessage("mode 2", 0.f);
    rig.reference.size(4).mode(signalStash::NORMALIZED);
    rig.Step();
    rig.Step();
    rig.obj.SetMessage("advancemode 1", 0.f);
    rig.reference.mode(signalStash::INDEX);
    rig.Step();

    // Numbers in the trigger inlets are the levels, as are the messages.
    rig.obj.SetMessage("mode 0", 0.f);
    rig.obj.GetInlet(1)->SetFloat(0.25f, YSE::T_GUI);
    rig.reference.mode(signalStash::NEXT).sampleLevel(0.25f);
    rig.Step();
    rig.obj.GetInlet(1)->SetInt(0, YSE::T_GUI);
    rig.reference.sampleLevel(0.f);
    rig.Step();
    rig.obj.SetMessage("samplelevel 0.5", 0.f);
    rig.obj.GetInlet(2)->SetFloat(0.75f, YSE::T_GUI);
    rig.reference.sampleLevel(0.5f).advanceLevel(0.75f);
    rig.Step();
    rig.obj.GetInlet(2)->SetInt(0, YSE::T_GUI);
    rig.obj.SetMessage("advancelevel 0.125", 0.f);
    rig.reference.advanceLevel(0.125f);
    rig.Step();

    // Trigger modes: falling through the levels, then the phase pass.
    rig.obj.SetMessage("sampletriggermode 1", 0.f);
    rig.obj.SetMessage("advancetriggermode 6", 0.f);
    rig.reference.sampleTriggerMode(signalStash::DESCENDING).advanceTriggerMode(signalStash::PHASE);
    rig.Step();

    // dir: backward, then random and urn from the same default seed.
    rig.obj.SetMessage("dir 1", 0.f);
    rig.reference.direction(signalStash::REVERSE);
    rig.Step();
    rig.obj.SetMessage("dir 2", 0.f);
    rig.reference.direction(signalStash::RANDOM);
    rig.Step();
    rig.obj.SetMessage("dir 3", 0.f);
    rig.reference.direction(signalStash::URN);
    rig.Step();

    // duration, then writemode 1 and 2 on the write index signal, with
    // extend and interp.
    rig.obj.SetMessage("duration 5", 0.f);
    rig.reference.duration(5);
    rig.Step();
    rig.obj.SetMessage("dur 0", 0.f);
    rig.obj.SetMessage("writemode 1", 0.f);
    rig.obj.SetMessage("extend 0", 0.f);
    rig.reference.duration(0).writeMode(signalStash::INDEX).range(signalStash::ZERO);
    rig.Step();
    rig.obj.SetMessage("writemode 2", 0.f);
    rig.obj.SetMessage("extend 1", 0.f);
    rig.obj.SetMessage("mode 1", 0.f);
    rig.obj.SetMessage("interp 1", 0.f);
    rig.reference.writeMode(signalStash::NORMALIZED)
        .range(signalStash::EXTEND)
        .mode(signalStash::INDEX)
        .interpolation(signalStash::LINEAR);
    rig.Step();
    rig.obj.SetMessage("extend 3", 0.f);
    rig.obj.SetMessage("interp 2", 0.f);
    rig.reference.range(signalStash::KEEP).interpolation(signalStash::ROUND);
    rig.Step();

    // A list of numbers fills the memory and sets the size.
    rig.obj.SetMessage("mode 2", 0.f);
    rig.obj.SetMessage("extend 2", 0.f);
    rig.obj.SetMessage("interp 0", 0.f);
    rig.obj.SetMessage("sampletriggermode 0", 0.f);
    rig.obj.SetMessage("samplelevel 2", 0.f); // stop recording over it
    rig.obj.GetInlet(0)->SetList("10 20 30", YSE::T_GUI);
    const float values[] = {10.f, 20.f, 30.f};
    rig.reference.mode(signalStash::NORMALIZED)
        .range(signalStash::WRAP)
        .interpolation(signalStash::TRUNCATE)
        .sampleTriggerMode(signalStash::ASCENDING)
        .sampleLevel(2.f)
        .load(values, 3);
    for (int block = 0; block < 2; ++block) {
      rig.Step();
      CHECK(rig.reference.size() == 3);
      // Closed form: sample n of the slow ramp reads (n mod 512) / 512, and
      // each third of the ramp plays one value.
      const auto first = static_cast<unsigned int>(rig.start) - N;
      for (unsigned int i = 0; i < N; ++i) {
        CAPTURE(i);
        const unsigned int place = ((first + i) % 512) * 3 / 512;
        CHECK(rig.out.received->getPtr()[i] == values[place]);
      }
    }

    // A size message after it, and a command that arrives as a list (text
    // through a receive).
    rig.obj.SetMessage("size 2", 0.f);
    rig.reference.size(2);
    rig.Step();
    rig.obj.GetInlet(0)->SetList("mode 0", YSE::T_GUI);
    rig.reference.mode(signalStash::NEXT);
    rig.Step();

    // Malformed or unknown messages, and maxsize as a message: ignored.
    for (const char* bad : {"dir", "dir 4", "mode -1", "mode 3", "writemode 3", "extend 4",
                            "interp 3", "sampletriggermode 7", "advancetriggermode -1",
                            "duration -1", "size 0", "samplelevel x", "maxsize 4", "speed 3", ""})
      rig.obj.SetMessage(bad, 0.f);
    rig.obj.GetInlet(0)->SetList("x y", YSE::T_GUI);
    rig.Step();
    rig.Step();

    // A size above the capacity clamps to it.
    rig.obj.SetMessage("size 5000", 0.f);
    rig.reference.size(5000);
    CHECK(rig.reference.size() == signalStash::DEFAULT_MAX_SIZE);
    rig.Step();
  }

  TEST_CASE("~stash: numbers write, bangs capture and advance, the write inlet positions "
            "(#1084, #1036)") {
    TestHelpers::ScopedSampleRate rate(48000);
    BufferSink out;
    dStash obj;
    Wire(obj, out);
    signalStash reference;
    buffer zero(N);
    buffer refIn(N);
    buffer refIndex(N);
    const auto step = [&](float input, float index) {
      obj.ResetDSP();
      obj.Calculate(YSE::T_DSP);
      REQUIRE(out.received != nullptr);
      // Unconnected: the input and write index read their last numbers, the
      // triggers 0.
      refIn = input;
      refIndex = index;
      CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(refIn, zero, zero, refIndex),
                                            0.f));
    };

    obj.SetMessage("size 4", 0.f);
    reference.size(4);
    step(0.f, 0.f);

    // A number in the left inlet is written at the write position.
    obj.GetInlet(0)->SetFloat(5.f, YSE::T_GUI);
    reference.write(5.f);
    step(5.f, 0.f);
    CHECK(out.received->getPtr()[0] == 5.f);
    CHECK(out.received->getPtr()[N - 1] == 5.f);

    // A bang in the right inlet moves the write position, a number sets it;
    // writes in between land there, in order.
    obj.GetInlet(3)->SetBang(YSE::T_GUI);
    obj.GetInlet(0)->SetInt(6, YSE::T_GUI);
    obj.GetInlet(3)->SetFloat(3.f, YSE::T_GUI);
    obj.GetInlet(0)->SetFloat(8.f, YSE::T_GUI);
    obj.GetInlet(3)->SetInt(-2, YSE::T_GUI); // wraps to 2
    obj.GetInlet(0)->SetFloat(7.f, YSE::T_GUI);
    reference.advanceWrite().write(6.f).writePosition(3.f).write(8.f).writePosition(-2.f).write(
        7.f);
    step(7.f, -2.f);
    CHECK(reference.value(1) == 6.f);
    CHECK(reference.value(2) == 7.f);
    CHECK(reference.value(3) == 8.f);

    // A bang in the advance inlet moves the output on: places 1, 2, 3.
    for (float expected : {6.f, 7.f, 8.f}) {
      obj.GetInlet(2)->SetBang(YSE::T_GUI);
      reference.advance();
      step(7.f, -2.f);
      CHECK(out.received->getPtr()[0] == expected);
    }

    // A bang in the sample inlet records the first sample of the next block:
    // the input's number, at the write position (2), moving it on.
    obj.GetInlet(0)->SetFloat(9.f, YSE::T_GUI); // also written at place 2
    obj.GetInlet(1)->SetBang(YSE::T_GUI);
    obj.GetInlet(2)->SetBang(YSE::T_GUI); // back to place 0
    reference.write(9.f).capture().advance();
    step(9.f, -2.f);
    CHECK(reference.value(2) == 9.f);
    CHECK(reference.writePosition() == 3);
    CHECK(out.received->getPtr()[0] == 5.f);

    // writemode 1 with the write inlet unconnected: its last number (-2, so
    // place 2) is the index a capture writes at.
    obj.SetMessage("writemode 1", 0.f);
    obj.GetInlet(0)->SetFloat(11.f, YSE::T_GUI);
    obj.GetInlet(1)->SetBang(YSE::T_GUI);
    reference.writeMode(signalStash::INDEX).write(11.f).capture();
    step(11.f, -2.f);
    CHECK(reference.value(2) == 11.f);
    CHECK(reference.value(3) == 11.f); // the number went to the write position
  }

  TEST_CASE("~stash: arguments are the levels, then attribute keywords (#1084)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const auto check = [](const std::string& args, signalStash& reference) {
      CAPTURE(args);
      BufferSink out;
      dStash obj;
      Wire(obj, out);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      phasor objRecord, refRecord, objAdvance, refAdvance, objWrite, refWrite;
      buffer objIn(N);
      buffer refIn(N);
      for (int block = 0; block < 6; ++block) {
        CAPTURE(block);
        obj.ResetDSP();
        Count(objIn, static_cast<float>(block * N));
        Count(refIn, static_cast<float>(block * N));
        obj.GetInlet(0)->SetBuffer(&objIn, YSE::T_GUI);
        obj.GetInlet(1)->SetBuffer(&objRecord(375.f, N), YSE::T_GUI);
        obj.GetInlet(2)->SetBuffer(&objAdvance(-93.75f, N), YSE::T_GUI);
        obj.GetInlet(3)->SetBuffer(&objWrite(187.5f, N), YSE::T_GUI);
        obj.Calculate(YSE::T_DSP);
        REQUIRE(out.received != nullptr);
        CHECK(TestHelpers::buffersNearlyEqual(
            *out.received,
            reference(refIn, refRecord(375.f, N), refAdvance(-93.75f, N), refWrite(187.5f, N)),
            0.f));
      }
    };

    signalStash levels;
    levels.sampleLevel(0.25f).advanceLevel(0.75f).size(3);
    check("0.25 0.75 size 3", levels);

    signalStash one;
    one.sampleLevel(0.1f).size(4).mode(signalStash::NORMALIZED);
    check("0.1 size 4 mode 2", one);

    // maxsize applies first wherever it is, so the size clamps to it.
    signalStash capped(8);
    capped.size(20).mode(signalStash::NORMALIZED);
    CHECK(capped.size() == 8);
    check("size 20 mode 2 maxsize 8", capped);

    signalStash all;
    all.size(6)
        .sampleTriggerMode(signalStash::BOTH)
        .advanceTriggerMode(signalStash::DESCENDING)
        .direction(signalStash::URN)
        .writeMode(signalStash::NORMALIZED)
        .range(signalStash::EXTEND)
        .interpolation(signalStash::ROUND)
        .duration(9);
    check("size 6 sampletriggermode 2 advancetriggermode 1 dir 3 writemode 2 extend 1 interp 2 "
          "dur 9",
          all);

    signalStash index;
    index.size(5).mode(signalStash::INDEX).interpolation(signalStash::LINEAR).advanceLevel(0.3f);
    check("advancemode 1 interp 1 size 5 advancelevel 0.3 duration 0", index);

    // A third number, a keyword without a number, or out of range: skipped.
    signalStash skipped;
    skipped.sampleLevel(0.4f).advanceLevel(0.6f).size(2);
    check("0.4 0.6 0.9 size 2 dir 9 mode -1 extend 7 maxsize 0 samplelevel", skipped);

    signalStash none;
    check("", none);
  }

  TEST_CASE("~stash: in a rendered patch, ~phasor clocks record, play and index a slower ramp, "
            "with lists and commands from a receive, after a JSON round trip (#1084)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* slow = src.CreateObject(YSE::OBJ::D_PHASOR, "46.875");
      YSE::pHandle* record = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* advance = src.CreateObject(YSE::OBJ::D_PHASOR, "93.75");
      YSE::pHandle* write = src.CreateObject(YSE::OBJ::D_PHASOR, "187.5");
      YSE::pHandle* stash = src.CreateObject(YSE::OBJ::D_STASH, "0.5 0.5 size 4 mode 2");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "cmd1084");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(slow != nullptr);
      REQUIRE(record != nullptr);
      REQUIRE(advance != nullptr);
      REQUIRE(write != nullptr);
      REQUIRE(stash != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(slow, 0, stash, 0);
      src.Connect(record, 0, stash, 1);
      src.Connect(advance, 0, stash, 2);
      src.Connect(write, 0, stash, 3);
      src.Connect(cmd, 0, stash, 0);
      src.Connect(stash, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);

    phasor slowRef;
    phasor recordRef;
    phasor advanceRef;
    phasor writeRef;
    signalStash reference;
    reference.size(4).mode(signalStash::NORMALIZED);
    for (int block = 0; block < 10; ++block) {
      CAPTURE(block);
      if (block == 4) {
        REQUIRE(dst.PassData("samplelevel 2", "cmd1084", YSE::T_GUI));
        REQUIRE(dst.PassData("0.1 0.2 0.3 0.4 0.5", "cmd1084", YSE::T_GUI));
        const float values[] = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f};
        reference.sampleLevel(2.f).load(values, 5);
      }
      if (block == 7) {
        // Record again, where the 187.5 Hz ramp says, and read with a
        // linear mix.
        REQUIRE(dst.PassData("samplelevel 0.5", "cmd1084", YSE::T_GUI));
        REQUIRE(dst.PassData("writemode 2", "cmd1084", YSE::T_GUI));
        REQUIRE(dst.PassData("interp 1", "cmd1084", YSE::T_GUI));
        reference.sampleLevel(0.5f)
            .writeMode(signalStash::NORMALIZED)
            .interpolation(signalStash::LINEAR);
      }
      dst.Calculate(YSE::T_DSP);
      buffer& slowBlock = slowRef(46.875f, N);
      CHECK(TestHelpers::buffersNearlyEqual(
          dst.output[0],
          reference(slowBlock, recordRef(375.f, N), advanceRef(93.75f, N), writeRef(187.5f, N)),
          0.f));
      const float* out = dst.output[0].getPtr();
      const unsigned int base = static_cast<unsigned int>(block) * N;
      if (block < 4) {
        // Closed form, as ~sash's: the input reads (n mod 1024) / 1024 and is
        // recorded at 65 + 128k into place k mod 4; place
        // floor((n mod 512) / 128) plays.
        for (unsigned int i = 0; i < N; ++i) {
          const unsigned int n = base + i;
          const unsigned int place = (n % 512) / 128;
          float expected = 0.f;
          for (unsigned int k = place; 65 + 128 * k <= n; k += 4)
            expected = static_cast<float>((65 + 128 * k) % 1024) / 1024.f;
          CAPTURE(n);
          CHECK(out[i] == doctest::Approx(expected));
        }
      } else if (block < 7) {
        // The list: five places, a fifth of the slow ramp each.
        for (unsigned int i = 0; i < N; i += 17) {
          const unsigned int place = ((base + i) % 512) * 5 / 512;
          CAPTURE(i);
          CHECK(out[i] == doctest::Approx(0.1f * static_cast<float>(place + 1)));
        }
      }
    }
  }

  TEST_CASE("~stash: numbers, bangs, lists, messages and blocks on the audio thread allocate "
            "nothing (#1084)") {
    BufferSink out;
    dStash obj;
    Wire(obj, out);
    buffer in(N);
    buffer sample(N);
    buffer advance(N);
    buffer write(N);
    for (unsigned int i = 0; i < N; ++i) {
      in.getPtr()[i] = static_cast<float>(i);
      sample.getPtr()[i] = static_cast<float>(i % 16) / 16.f;
      advance.getPtr()[i] = static_cast<float>(i % 32) / 32.f;
      write.getPtr()[i] = static_cast<float>(i % 7);
    }
    const std::string list = "1 2 3 4 5 6";
    const std::string command = "dir 3";
    const std::string size = "size 8";
    const std::string mode = "mode 2";
    const std::string writemode = "writemode 1";
    const std::string level = "samplelevel 0.3";
    obj.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      for (int i = 0; i < 2; ++i) {
        obj.GetInlet(0)->SetFloat(0.5f, YSE::T_DSP);
        obj.GetInlet(0)->SetInt(1, YSE::T_DSP);
        obj.GetInlet(0)->SetList(list, YSE::T_DSP);
        obj.GetInlet(0)->SetList(command, YSE::T_DSP);
        obj.GetInlet(1)->SetFloat(0.5f, YSE::T_DSP);
        obj.GetInlet(1)->SetInt(0, YSE::T_DSP);
        obj.GetInlet(1)->SetBang(YSE::T_DSP);
        obj.GetInlet(2)->SetFloat(0.5f, YSE::T_DSP);
        obj.GetInlet(2)->SetInt(0, YSE::T_DSP);
        obj.GetInlet(2)->SetBang(YSE::T_DSP);
        obj.GetInlet(3)->SetFloat(2.5f, YSE::T_DSP);
        obj.GetInlet(3)->SetInt(1, YSE::T_DSP);
        obj.GetInlet(3)->SetBang(YSE::T_DSP);
        obj.SetMessage(size, 0.f);
        obj.SetMessage(mode, 0.f);
        obj.SetMessage(writemode, 0.f);
        obj.SetMessage(level, 0.f);
        obj.Calculate(YSE::T_DSP);
        obj.ResetDSP();
        obj.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
        obj.GetInlet(1)->SetBuffer(&sample, YSE::T_DSP);
        obj.GetInlet(2)->SetBuffer(&advance, YSE::T_DSP);
        obj.GetInlet(3)->SetBuffer(&write, YSE::T_DSP);
        obj.Calculate(YSE::T_DSP);
        obj.ResetDSP();
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")

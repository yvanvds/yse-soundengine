// Tests for the ~sync patcher object (patcher/generatorObjects/dSync), issue
// #1131. The timekeeping is DSP::clockFollower's and is tested in
// Tests/dsp/test_clock_follower.cpp; these show that bangs, ints, floats and
// messages reach it in order, that the ramp is the class's, that the reports
// and the MIDI clock go out right to left only through the flush after the
// block (#1037), that the parameters survive a JSON round trip, and, in
// rendered patches, that a ~train click track sets the tempo through a
// .route, that start/stop through a receive drive the clock out, and that
// MIDI beat clock reaches it both through a .rtin cord and through rtport.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "headers/defines.hpp"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/generatorObjects/dSync.h"
#include "dsp/generators/clockFollower.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"
#if YSE_ENABLE_MIDI_DEVICE
#include "midi/midiInHub.h"
#endif

using TestHelpers::BufferSink;
using TestHelpers::ScopedSampleRate;
using TestHelpers::Wire;
using YSE::DSP::buffer;
using YSE::DSP::clockFollower;
using YSE::PATCHER::dSync;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // Logs every int, float and message it receives as "<tag>:<text>" into a
  // shared log, so the order of a flush can be asserted. With `keep` false it
  // only counts, which allocates nothing.
  struct Recorder : TestHelpers::SinkBase {
    std::vector<std::string>* log = nullptr;
    char tag = '?';
    bool keep = true;
    int ints = 0;
    int floats = 0;
    int messages = 0;
    int lastInt = 0;
    float lastFloat = 0.f;

    Recorder() {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) {
        ints++;
        lastInt = v;
        if (keep) Keep(std::to_string(v));
      });
      inputs.back().RegisterFloat([this](float v, int, YSE::THREAD) {
        floats++;
        lastFloat = v;
        if (keep) Keep("f" + std::to_string(v));
      });
    }
    const char* Type() const override {
      return "sync_recorder";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string& message, float) override {
      messages++;
      Keep(message);
    }
    bool HandlesMessages() const override {
      return true;
    }

  private:
    void Keep(const std::string& text) {
      if (keep && log != nullptr) log->push_back(std::string(1, tag) + ":" + text);
    }
  };

  // ~sync with sinks on its three outlets. Block() runs one block and the
  // flush the patcher runs after its traversal.
  struct Rig {
    std::vector<std::string> log;
    BufferSink ramp;
    Recorder report;
    Recorder clock;
    dSync sync;

    Rig() {
      log.reserve(8192);
      report.tag = 'r';
      clock.tag = 'c';
      report.log = &log;
      clock.log = &log;
      Wire(sync, 0, ramp);
      Wire(sync, 1, report);
      Wire(sync, 2, clock);
    }
    buffer& Block(buffer* click = nullptr) {
      log.clear();
      sync.ResetDSP();
      if (click != nullptr) {
        sync.GetInlet(0)->SetBuffer(click, YSE::T_DSP); // runs CALC()
      } else {
        sync.Calculate(YSE::T_DSP);
      }
      CHECK(log.empty()); // nothing is sent from CALC() itself
      sync.FlushArmedOutlets(YSE::T_GUI);
      REQUIRE(ramp.received != nullptr);
      return *ramp.received;
    }
  };

  // The clock bytes of a reference's last report, as the rig logs them.
  std::vector<std::string> Expected(const clockFollower& ref) {
    std::vector<std::string> out;
    out.reserve(ref.clockBytes() + 1);
    for (UInt i = 0; i < ref.clockBytes(); ++i)
      out.push_back("c:" + std::to_string(ref.clockByte(i)));
    return out;
  }

  bool SameRamp(buffer& a, buffer& b) {
    if (a.getLength() != b.getLength()) return false;
    for (unsigned int i = 0; i < a.getLength(); ++i)
      if (a.getPtr()[i] != b.getPtr()[i]) return false;
    return true;
  }

  // "bpm <x>" -> x, or -1 when the text is not a bpm report.
  float BpmOf(const std::string& entry) {
    const std::string prefix = "r:bpm ";
    if (entry.compare(0, prefix.size(), prefix) != 0) return -1.f;
    return std::stof(entry.substr(prefix.size()));
  }

  float GuiFloat(YSE::pHandle* h) {
    return std::stof(h->GetGuiValue());
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~sync: registry and shape (#1131)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_SYNC);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~sync");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 3);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(obj->OutputDataType(1) == YSE::OUT_TYPE::ANY);
    CHECK(obj->OutputDataType(2) == YSE::OUT_TYPE::INT);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_SYNC));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::OSC);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 5);
  }

  TEST_CASE("~sync: the ramp is DSP::clockFollower's; tempo and transport reports go out right "
            "to left after the block (#1131, #1037)") {
    ScopedSampleRate rate(48000);
    Rig rig;
    clockFollower ref;

    buffer& first = rig.Block();
    CHECK(SameRamp(first, ref()));
    CHECK(rig.log == Expected(ref)); // the clock out's first tick
    for (int b = 0; b < 20; ++b) {
      buffer& out = rig.Block();
      CHECK(SameRamp(out, ref()));
      CHECK(rig.log == Expected(ref));
    }

    // 'bpm' and a float set the tempo; the report is the class's.
    rig.sync.GetInlet(0)->SetMessage("bpm 140", YSE::T_GUI);
    ref.tempo(140.f);
    CHECK(SameRamp(rig.Block(), ref()));
    REQUIRE_FALSE(rig.log.empty());
    CHECK(rig.log.back() == "r:bpm 140.");
    rig.sync.GetInlet(0)->SetFloat(90.5f, YSE::T_GUI);
    ref.tempo(90.5f);
    CHECK(SameRamp(rig.Block(), ref()));
    REQUIRE_FALSE(rig.log.empty());
    CHECK(rig.log.back() == "r:bpm 90.5");

    // An int is a MIDI byte, not a tempo: 120 does nothing.
    rig.sync.GetInlet(0)->SetInt(120, YSE::T_GUI);
    CHECK(SameRamp(rig.Block(), ref()));
    CHECK(rig.log == Expected(ref));

    // 'start' and 'bpm' in one block: the clock outlet first, then the report.
    rig.sync.GetInlet(0)->SetMessage("start", YSE::T_GUI);
    rig.sync.GetInlet(0)->SetMessage("bpm 100", YSE::T_GUI);
    ref.start();
    ref.tempo(100.f);
    buffer& started = rig.Block();
    CHECK(SameRamp(started, ref()));
    CHECK(started.getPtr()[0] == 0.f);
    CHECK(rig.log == std::vector<std::string>{"c:250", "c:248", "r:bpm 100."});

    // 'stop' holds the ramp and sends 252.
    rig.sync.GetInlet(0)->SetMessage("stop", YSE::T_GUI);
    ref.stop();
    buffer& stopped = rig.Block();
    CHECK(SameRamp(stopped, ref()));
    CHECK(stopped.getPtr()[0] == stopped.getPtr()[N - 1]);
    CHECK(rig.log == std::vector<std::string>{"c:252"});
    rig.Block();
    CHECK(rig.log.empty());
  }

  TEST_CASE("~sync: bangs are taps; three that agree report 'tap' and the tempo (#1131)") {
    ScopedSampleRate rate(48000);
    Rig rig;
    rig.clock.keep = false; // the clock out is not what this is about
    for (int t = 0; t < 3; ++t) {
      rig.sync.GetInlet(0)->SetBang(YSE::T_GUI);
      rig.Block();
      if (t < 2) {
        CHECK(rig.log == std::vector<std::string>{"r:tap"});
      } else {
        // 180 blocks apart: 0.48 s, 125 BPM (a float, so "125.").
        CHECK(rig.log == std::vector<std::string>{"r:tap", "r:bpm 125."});
      }
      for (int b = 0; b < 179; ++b) {
        rig.Block();
        CHECK(rig.log.empty());
      }
    }
  }

  TEST_CASE("~sync: ints are MIDI bytes: start holds at 0 for the first tick, ticks report "
            "'midi' and give the tempo (#1131)") {
    ScopedSampleRate rate(48000);
    Rig rig;
    clockFollower ref;
    rig.Block();
    ref();
    rig.sync.GetInlet(0)->SetInt(250, YSE::T_GUI);
    ref.midi(250);
    buffer& held = rig.Block();
    CHECK(SameRamp(held, ref()));
    CHECK(held.getPtr()[N - 1] == 0.f);
    CHECK(rig.log == std::vector<std::string>{"c:250"});

    float bpm = -1.f;
    for (int tick = 0; tick < 48; ++tick) {
      rig.sync.GetInlet(0)->SetInt(248, YSE::T_GUI);
      ref.midi(248);
      CHECK(SameRamp(rig.Block(), ref()));
      std::vector<std::string> expected = Expected(ref);
      expected.push_back("r:midi");
      if (ref.tempoChanged()) {
        REQUIRE_FALSE(rig.log.empty());
        bpm = BpmOf(rig.log.back());
        rig.log.pop_back();
      }
      CHECK(rig.log == expected);
      for (int b = 0; b < 7; ++b) { // a tick every 1024 samples
        CHECK(SameRamp(rig.Block(), ref()));
        CHECK(rig.log == Expected(ref));
      }
    }
    CHECK(bpm == doctest::Approx(60.0 / (1024.0 / 48000.0 * 24.0)).epsilon(1e-4));
    // Other ints do nothing.
    rig.sync.GetInlet(0)->SetInt(144, YSE::T_GUI);
    rig.sync.GetInlet(0)->SetInt(-5, YSE::T_GUI);
    CHECK(SameRamp(rig.Block(), ref()));
    CHECK(rig.log == Expected(ref));
  }

  TEST_CASE("~sync: a click track at the inlet sets the tempo (#1131)") {
    ScopedSampleRate rate(48000);
    Rig rig;
    rig.clock.keep = false;
    buffer click(N);
    int clicks = 0;
    float bpm = -1.f;
    for (int b = 0; b < 700; ++b) { // a click every 225 blocks: 100 BPM
      click = 0.f;
      if (b % 225 == 0) click.getPtr()[17] = 0.8f;
      rig.Block(&click);
      for (const std::string& entry : rig.log) {
        if (entry == "r:click") clicks++;
        if (BpmOf(entry) > 0.f) bpm = BpmOf(entry);
      }
    }
    CHECK(clicks == 4);
    CHECK(bpm == doctest::Approx(100.f));
  }

  TEST_CASE("~sync: offset, midioffset and ppq reach the class; malformed messages change "
            "nothing; a full queue drops and counts (#1131)") {
    ScopedSampleRate rate(48000);
    Rig rig;
    clockFollower ref;
    rig.sync.GetInlet(0)->SetMessage("offset 0.25", YSE::T_GUI);
    rig.sync.GetInlet(0)->SetMessage("midioffset 0.5", YSE::T_GUI);
    rig.sync.GetInlet(0)->SetMessage("ppq 48", YSE::T_GUI);
    rig.sync.SetMessage("offset", 0.f);
    rig.sync.SetMessage("offset x", 0.f);
    rig.sync.SetMessage("offsets 0.9", 0.f);
    rig.sync.SetMessage("bpm", 0.f);
    rig.sync.SetMessage("", 0.f);
    ref.offset(0.25f).clockOffset(0.5f).ppq(48);
    for (int b = 0; b < 400; ++b) {
      CHECK(SameRamp(rig.Block(), ref()));
      CHECK(rig.log == Expected(ref));
    }
    CHECK(ref.tempo() == 120.f);

    // ppq is held to 1..192.
    rig.sync.GetInlet(0)->SetMessage("ppq 0", YSE::T_GUI);
    ref.ppq(1);
    for (int b = 0; b < 400; ++b) {
      CHECK(SameRamp(rig.Block(), ref()));
      CHECK(rig.log == Expected(ref));
    }
    rig.sync.GetInlet(0)->SetMessage("ppq 1000", YSE::T_GUI);
    ref.ppq(clockFollower::MAX_PPQ);
    for (int b = 0; b < 50; ++b) {
      CHECK(SameRamp(rig.Block(), ref()));
      CHECK(rig.log == Expected(ref));
    }

    // 300 bangs before a block: the queue holds 256, the rest are counted.
    CHECK(rig.sync.Dropped() == 0);
    for (int t = 0; t < 300; ++t)
      rig.sync.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.sync.Dropped() == 44);
  }

  TEST_CASE("~sync: the parameters through a DumpJSON / ParseJSON round trip; messages are not "
            "saved (#1131)") {
    ScopedSampleRate rate(48000);
    const std::string args = "140 0.25 0.5 48 -1";
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* a = src.CreateObject(YSE::OBJ::D_SYNC, args);
      YSE::pHandle* b = src.CreateObject(YSE::OBJ::D_SYNC, "");
      REQUIRE(a != nullptr);
      REQUIRE(b != nullptr);
      a->SetListData(0, "ppq 12"); // a message: not saved
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* a = dst.GetHandleFromID(0);
    YSE::pHandle* b = dst.GetHandleFromID(1);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(std::string(a->Type()) == "~sync");
    CHECK(a->GetParams() == args);
    CHECK(b->GetParams() == "");

    // The parameters reach the class, and the starting tempo is not a change.
    std::vector<std::string> log;
    BufferSink ramp;
    Recorder report;
    Recorder clock;
    report.log = &log;
    clock.log = &log;
    report.tag = 'r';
    clock.tag = 'c';
    dSync sync;
    sync.SetParams(args);
    Wire(sync, 0, ramp);
    Wire(sync, 1, report);
    Wire(sync, 2, clock);
    clockFollower ref;
    ref.offset(0.25f).clockOffset(0.5f).ppq(48);
    ref.reset(140.f);
    for (int k = 0; k < 300; ++k) {
      log.clear();
      sync.ResetDSP();
      sync.Calculate(YSE::T_DSP);
      sync.FlushArmedOutlets(YSE::T_GUI);
      REQUIRE(ramp.received != nullptr);
      CHECK(SameRamp(*ramp.received, ref()));
      CHECK(log == Expected(ref));
    }
    CHECK(report.messages == 0);
  }

  TEST_CASE("~sync: in a rendered patch, a ~train click track sets the tempo through .route, "
            "and start/stop through a receive drive the clock out (#1131)") {
    ScopedSampleRate rate(48000);
    // The sinks outlive the patcher (sinks.hpp, #967).
    Recorder bpm;
    Recorder clock;
    YSE::pHandle bpmHandle(&bpm);
    YSE::pHandle clockHandle(&clock);
    patcherImplementation patch(1, nullptr);
    YSE::pHandle* train = patch.CreateObject(YSE::OBJ::D_TRAIN, "600");
    YSE::pHandle* sync = patch.CreateObject(YSE::OBJ::D_SYNC, "");
    YSE::pHandle* route = patch.CreateObject(YSE::OBJ::G_ROUTE, "bpm");
    YSE::pHandle* control = patch.CreateObject(YSE::OBJ::G_RECEIVE, "sync1131");
    YSE::pHandle* number = patch.CreateObject(YSE::OBJ::G_FLOAT, "");
    REQUIRE(train != nullptr);
    REQUIRE(sync != nullptr);
    REQUIRE(route != nullptr);
    REQUIRE(control != nullptr);
    REQUIRE(number != nullptr);
    patch.Connect(train, 0, sync, 0);
    patch.Connect(control, 0, sync, 0);
    patch.Connect(sync, 1, route, 0);
    patch.Connect(route, 0, number, 0);
    patch.Connect(route, 0, &bpmHandle, 0);
    patch.Connect(sync, 2, &clockHandle, 0);

    // 2.1 s: a pulse every 0.6 s, enough for three that agree.
    for (int k = 0; k < 790; ++k)
      patch.Calculate(YSE::T_DSP);
    CHECK(bpm.floats + bpm.ints >= 1);
    CHECK(GuiFloat(number) == doctest::Approx(100.f));
    // Running at 120 then 100 BPM from the first block: one tick per 1/24
    // beat, so 2.1 s carries more than 80 of them.
    CHECK(clock.ints > 80);

    REQUIRE(patch.PassData(std::string("stop"), "sync1131", YSE::T_GUI));
    patch.Calculate(YSE::T_DSP);
    CHECK(clock.lastInt == 252);
    const int ticks = clock.ints;
    for (int k = 0; k < 100; ++k)
      patch.Calculate(YSE::T_DSP);
    CHECK(clock.ints == ticks);

    REQUIRE(patch.PassData(std::string("start"), "sync1131", YSE::T_GUI));
    patch.Calculate(YSE::T_DSP);
    CHECK(clock.ints == ticks + 2); // 250, then the first 248
    CHECK(clock.lastInt == 248);
  }

#if YSE_ENABLE_MIDI_DEVICE
  TEST_CASE("~sync: rtport subscribes when the object joins a patcher and lets go when it "
            "leaves (#1131)") {
    patcherImplementation patch(1, nullptr);
    {
      dSync none;
      none.SetParent(&patch);
      CHECK(none.Subscription() == 0); // rtport -1
      none.SetParent(nullptr);
    }
    unsigned int first = 0;
    {
      dSync obj;
      obj.SetParams("120 0 0 24 7");
      CHECK(obj.Subscription() == 0); // not joined: nothing would drain it
      obj.SetParent(&patch);
      first = obj.Subscription();
      CHECK(first != 0);
      obj.SetParent(&patch); // re-parenting re-takes the same slot
      CHECK(obj.Subscription() == first);
      obj.SetParent(nullptr);
      CHECK(obj.Subscription() == 0);
      obj.SetParent(&patch);
      CHECK(obj.Subscription() == first);
    } // the destructor gives it back
    dSync again;
    again.SetParams("120 0 0 24 7");
    again.SetParent(&patch);
    CHECK(again.Subscription() == first); // the slot was free again
    again.SetParent(nullptr);
  }

  TEST_CASE("~sync: in a rendered patch, MIDI beat clock from a port through a .rtin cord and "
            "through rtport gives the same reports (#1131)") {
    ScopedSampleRate rate(48000);
    // Port 7, as the .rtin tests use: a machine with eight MIDI inputs is
    // rare enough that no real device injects into it.
    constexpr unsigned int kPort = 7;
    std::vector<std::string> cordLog;
    std::vector<std::string> portLog;
    Recorder viaCord;
    Recorder viaPort;
    viaCord.log = &cordLog;
    viaPort.log = &portLog;
    viaCord.tag = 'r';
    viaPort.tag = 'r';
    YSE::pHandle cordHandle(&viaCord);
    YSE::pHandle portHandle(&viaPort);
    patcherImplementation patch(1, nullptr);
    YSE::pHandle* rtin = patch.CreateObject(YSE::OBJ::M_RTIN, "7");
    YSE::pHandle* corded = patch.CreateObject(YSE::OBJ::D_SYNC, "");
    YSE::pHandle* direct = patch.CreateObject(YSE::OBJ::D_SYNC, "120 0 0 24 7");
    REQUIRE(rtin != nullptr);
    REQUIRE(corded != nullptr);
    REQUIRE(direct != nullptr);
    patch.Connect(rtin, 0, corded, 0);
    patch.Connect(corded, 1, &cordHandle, 0);
    patch.Connect(direct, 1, &portHandle, 0);

    const auto send = [](unsigned char byte) { YSE::MIDI::InHub().Deliver(kPort, &byte, 1); };
    patch.Calculate(YSE::T_DSP);
    send(250);
    patch.Calculate(YSE::T_DSP);
    for (int tick = 0; tick < 72; ++tick) {
      send(248);
      for (int b = 0; b < 8; ++b)
        patch.Calculate(YSE::T_DSP);
    }
    // One 'midi' a tick, and the tempo of a tick every 1024 samples.
    CHECK(viaCord.messages >= 73);
    CHECK(cordLog == portLog);
    float bpm = -1.f;
    for (const std::string& entry : portLog)
      if (BpmOf(entry) > 0.f) bpm = BpmOf(entry);
    CHECK(bpm == doctest::Approx(60.0 / (1024.0 / 48000.0 * 24.0)).epsilon(1e-4));

    // A real-time byte other than the clock's, and a note, change nothing.
    portLog.clear();
    cordLog.clear();
    send(254);
    const unsigned char note[3] = {0x90, 60, 100};
    YSE::MIDI::InHub().Deliver(kPort, note, 3);
    patch.Calculate(YSE::T_DSP);
    CHECK(portLog.empty());
    CHECK(cordLog.empty());
  }
#endif

  TEST_CASE("~sync: messages, blocks and the flush on the audio thread allocate nothing "
            "(#1131)") {
    ScopedSampleRate rate(48000);
    Rig rig;
    rig.report.keep = false;
    rig.clock.keep = false;
    const std::string bpm = "bpm 130";
    const std::string offset = "offset 0.1";
    const std::string ppq = "ppq 48";
    const std::string start = "start";
    const std::string stop = "stop";
    buffer click(N);
    click = 0.f;
    rig.Block();
    {
      TestHelpers::ProbeScope probe;
      rig.sync.GetInlet(0)->SetMessage(bpm, YSE::T_DSP);
      rig.sync.GetInlet(0)->SetMessage(offset, YSE::T_DSP);
      rig.sync.GetInlet(0)->SetMessage(ppq, YSE::T_DSP);
      rig.sync.GetInlet(0)->SetMessage(start, YSE::T_DSP);
      for (int b = 0; b < 400; ++b) {
        if (b % 100 == 0) rig.sync.GetInlet(0)->SetBang(YSE::T_DSP);
        if (b % 8 == 0) rig.sync.GetInlet(0)->SetInt(248, YSE::T_DSP);
        if (b == 50) rig.sync.GetInlet(0)->SetFloat(110.f, YSE::T_DSP);
        click.getPtr()[3] = b % 150 == 0 ? 1.f : 0.f;
        rig.Block(&click);
      }
      rig.sync.GetInlet(0)->SetMessage(stop, YSE::T_DSP);
      rig.Block();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(rig.report.messages > 0); // the report path ran inside the probe
    CHECK(rig.clock.ints > 0);
  }

} // TEST_SUITE("patcher")

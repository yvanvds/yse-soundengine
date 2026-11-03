// Tests for the ~table patcher object (patcher/sampling/dTable), issue #1148.
// The sample math is DSP::tableLookup's (Tests/dsp/test_table_lookup.cpp) and
// the reader is DSP::sampleBufferReader's (test_sampleBuffer.cpp). These show
// that pins, messages and arguments reach them, in rendered patches heard
// through a ~dac: a ~phasor through inmap, the number fallback, the position
// messages, triggers, random draws, the attribute messages, rebinding, a
// round trip, allocation-free blocks and messages on the audio thread, and
// edits made while it renders. (The control .table has its own file,
// test_patcher_table.cpp.)

#include <doctest/doctest.h>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "dsp/sampling/sampleBuffer.hpp"
#include "headers/constants.hpp"
#include "patcher/io/fileScheduler.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sampling/dTable.h"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

using YSE::STANDARD_BUFFERSIZE;
using YSE::DSP::sampleBuffer;
using YSE::DSP::sampleBufferRegistry;
using YSE::DSP::tableLookup;
using YSE::PATCHER::dTable;
using YSE::PATCHER::patcherImplementation;

namespace {

  // A named one-channel buffer whose entry n is 10 * n.
  std::shared_ptr<sampleBuffer> Tens(const std::string& name, UInt frames) {
    std::shared_ptr<sampleBuffer> b = sampleBufferRegistry::acquire(name);
    b->setSize(frames, 1);
    std::vector<float> values(frames);
    for (UInt n = 0; n < frames; ++n)
      values[n] = 10.f * static_cast<float>(n);
    b->setSamples(0, 0, values.data(), frames);
    return b;
  }

  std::shared_ptr<sampleBuffer> Values(const std::string& name, const std::vector<float>& v) {
    std::shared_ptr<sampleBuffer> b = sampleBufferRegistry::acquire(name);
    b->setSize(static_cast<UInt>(v.size()), 1);
    b->setSamples(0, 0, v.data(), static_cast<UInt>(v.size()));
    return b;
  }

  float Out(patcherImplementation& p, UInt i) {
    return p.output[0].getPtr()[i];
  }

  // [source] -> [~table args] -> [~dac], `source` made from `type` and
  // `sourceArgs`, or no source at all when `type` is empty.
  struct TableRig {
    patcherImplementation p{1, nullptr};
    YSE::pHandle* source = nullptr;
    YSE::pHandle* handle = nullptr;
    YSE::pHandle* dac = nullptr;
    dTable* table = nullptr;

    TableRig(const std::string& args, const char* type, const std::string& sourceArgs) {
      handle = p.CreateObject(YSE::OBJ::D_TABLE, args);
      REQUIRE(handle != nullptr);
      dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(dac != nullptr);
      if (type != nullptr && *type != '\0') {
        source = p.CreateObject(type, sourceArgs);
        REQUIRE(source != nullptr);
        p.Connect(source, 0, handle, 0);
      }
      p.Connect(handle, 0, dac, 0);
      table = static_cast<dTable*>(p.GetObjectFromID(handle->GetID()));
    }

    void Message(const std::string& text, YSE::THREAD thread = YSE::T_GUI) {
      table->GetInlet(0)->SetMessage(text, thread);
    }

    // What the next block reads, at its first sample.
    float Block() {
      p.Calculate(YSE::T_DSP);
      return Out(p, 0);
    }

    // The bind task runs on the background pool, the next block takes the
    // reader, and the task that frees the old one runs after it.
    void Settle() {
      YSE::PATCHER::fileScheduler* io = p.FileIO();
      REQUIRE(io != nullptr);
      io->WaitIdle();
      p.Calculate(YSE::T_DSP);
      io->WaitIdle();
    }
  };

} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~table: registry and shape (#1148)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_TABLE, "shape_1148");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~table");
    CHECK(h->GetInputs() == 1);
    REQUIRE(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    bool listed = false;
    for (const auto& name : YSE::PATCHER::Register().AllNames())
      if (name == YSE::OBJ::D_TABLE) listed = true;
    CHECK(listed);

    dTable standalone;
    CHECK(standalone.GetCategory() == YSE::PATCHER::pCategory::SAMPLING);
    CHECK(standalone.Buffer() == nullptr);
    CHECK(standalone.Lookup().interp() == tableLookup::interpolation::NONE);
    CHECK(standalone.Lookup().range() == 128);
  }

  TEST_CASE("~table: a ~phasor through inmap 0 1 plays the table as a wave (#1148)") {
    // [~table wave 1 2 0 1]: linear, wrapped, a 0-1 phase over the whole
    // table. A ramp table of 10 n read at phase p is 640 p, until the last
    // entry slides back to entry 0.
    auto b = Tens("wave_1148", 64);
    TableRig s("wave_1148 1 2 0 1", YSE::OBJ::D_SIG, "0.25");
    CHECK(s.table->Buffer() == b);
    CHECK(s.Block() == 160.f);
    s.source->SetFloatData(0, 0.51f);
    CHECK(s.Block() == doctest::Approx(326.4f));
    s.source->SetFloatData(0, 0.99f); // 63.36: from 630 a third of the way to 0
    CHECK(s.Block() == doctest::Approx(630.f * 0.64f));
    s.source->SetFloatData(0, 1.25f);
    CHECK(s.Block() == 160.f);

    // Driven by a [~phasor]: the output rises by 640 times the phase step
    // from sample to sample, except where the phase wraps.
    TableRig r("wave_1148 1 2 0 1", YSE::OBJ::D_PHASOR, "100");
    r.p.Calculate(YSE::T_DSP);
    const double step = 640.0 * 100.0 / static_cast<double>(YSE::SAMPLERATE);
    int rising = 0;
    for (UInt i = 1; i < STANDARD_BUFFERSIZE; ++i) {
      const float before = Out(r.p, i - 1);
      const float now = Out(r.p, i);
      CHECK(now >= 0.f);
      CHECK(now <= 630.f);
      if (before < 600.f && now > before) {
        CAPTURE(i);
        CHECK(now - before == doctest::Approx(step).epsilon(1e-3));
        rising++;
      }
    }
    CHECK(rising > static_cast<int>(STANDARD_BUFFERSIZE) / 2);
  }

  TEST_CASE("~table: a number index while no signal is connected; a signal wins (#1148)") {
    auto b = Tens("num_1148", 16);
    TableRig r("num_1148", "", "");
    CHECK(r.Block() == 0.f); // the default position is 0
    r.table->GetInlet(0)->SetInt(5, YSE::T_GUI);
    CHECK(r.Block() == 50.f);
    CHECK(Out(r.p, STANDARD_BUFFERSIZE - 1) == 50.f);
    r.table->GetInlet(0)->SetFloat(7.9f, YSE::T_GUI);
    CHECK(r.Block() == 70.f); // interp 0 truncates
    r.table->GetInlet(0)->SetFloat(-1.f, YSE::T_GUI);
    CHECK(r.Block() == 0.f); // extend 0: outside reads 0
    r.table->GetInlet(0)->SetInt(16, YSE::T_GUI);
    CHECK(r.Block() == 0.f);

    TableRig s("num_1148", YSE::OBJ::D_SIG, "9");
    s.table->GetInlet(0)->SetInt(2, YSE::T_GUI); // the cord wins
    CHECK(s.Block() == 90.f);
    s.p.Disconnect(s.source, 0, s.handle, 0);
    CHECK(s.Block() == 20.f);
  }

  TEST_CASE("~table: goto, next and prev move the position, in order (#1148)") {
    auto b = Tens("pos_1148", 4);
    TableRig r("pos_1148", "", "");
    r.Message("next");
    CHECK(r.Block() == 0.f); // a new table starts at entry 0
    r.Message("next");
    CHECK(r.Block() == 10.f);
    r.Message("prev");
    r.Message("prev");
    CHECK(r.Block() == 30.f); // below 0 wraps to the end
    r.Message("goto 2");
    CHECK(r.Block() == 30.f); // goto alone changes nothing
    r.Message("next");
    CHECK(r.Block() == 20.f); // the goto index
    // Several in one block are applied in the order they came.
    r.Message("next");
    r.Message("goto 0");
    r.Message("next");
    r.Message("next");
    CHECK(r.Block() == 10.f);
    r.table->GetInlet(0)->SetInt(3, YSE::T_GUI);
    r.Message("next");
    CHECK(r.Block() == 0.f); // 3 + 1 wraps
    CHECK(r.table->Dropped() == 0);
  }

  TEST_CASE("~table: inputmode 1 steps on triggers; triggermode picks them (#1148)") {
    // [~count 0 4 1] counts 0 1 2 3 0 1 ...: a rise from 0 once every four
    // samples (triggermode 0), a change every sample (triggermode 1).
    auto b = Tens("step_1148", 8);
    TableRig r("step_1148 0 0 0 0 0 0 1 0", YSE::OBJ::D_COUNT, "0 4 1");
    CHECK(r.table->Lookup().mode() == tableLookup::inputMode::LOOKUP); // until a block
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.table->Lookup().mode() == tableLookup::inputMode::INCREMENT);
    // Sample 1 is the first rise: entry 0, then 1 at sample 5, 2 at 9 ...
    CHECK(Out(r.p, 0) == 0.f);
    CHECK(Out(r.p, 1) == 0.f);
    CHECK(Out(r.p, 4) == 0.f);
    CHECK(Out(r.p, 5) == 10.f);
    CHECK(Out(r.p, 9) == 20.f);
    CHECK(Out(r.p, 33) == 0.f); // the ninth rise wraps round the 8 entries

    TableRig c("step_1148 0 0 0 0 0 0 1 1", YSE::OBJ::D_COUNT, "0 4 1");
    c.p.Calculate(YSE::T_DSP);
    CHECK(Out(c.p, 0) == 0.f);
    CHECK(Out(c.p, 1) == 0.f); // the first change reads entry 0
    CHECK(Out(c.p, 2) == 10.f);
    CHECK(Out(c.p, 3) == 20.f);
    CHECK(Out(c.p, 9) == 0.f);

    // With no cord a number is the trigger, and the mode is a message.
    TableRig m("step_1148", "", "");
    m.Message("inputmode 1");
    m.table->GetInlet(0)->SetInt(1, YSE::T_GUI);
    CHECK(m.Block() == 0.f);
    m.table->GetInlet(0)->SetInt(0, YSE::T_GUI);
    m.table->GetInlet(0)->SetInt(1, YSE::T_GUI);
    CHECK(m.Block() == 10.f);
    m.Message("triggermode 1");
    m.table->GetInlet(0)->SetInt(4, YSE::T_GUI);
    CHECK(m.Block() == 20.f);
  }

  TEST_CASE("~table: bang and inputmode 2 draw by the entries' weights (#1148)") {
    auto b = Values("rand_1148", {0.f, 0.f, 0.f, 5.f, 0.f});
    TableRig r("rand_1148", "", "");
    CHECK(r.Block() == 0.f);
    r.table->GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(r.Block() == 5.f); // the only entry that can be drawn

    auto w = Values("rand_1148_w", {1.f, 0.f, 3.f});
    TableRig t("rand_1148_w 0 0 0 0 0 0 2 1", "", "");
    int heard[4] = {0, 0, 0, 0};
    for (int n = 1; n <= 400; ++n) {
      t.table->GetInlet(0)->SetInt(n, YSE::T_GUI); // a change each time
      const float v = t.Block();
      REQUIRE((v == 1.f || v == 3.f));
      heard[static_cast<int>(v)]++;
    }
    CHECK(heard[3] > heard[1]);
    CHECK(heard[1] > 0);
  }

  TEST_CASE("~table: interp, extend, inmap, outscale, range and signed (#1148)") {
    auto b = Values("attr_1148", {0.f, 64.f, 127.f, -128.f});
    TableRig r("attr_1148", YSE::OBJ::D_SIG, "1.5");
    CHECK(r.Block() == 64.f);
    r.Message("interp 1");
    CHECK(r.Block() == doctest::Approx(95.5f));
    r.Message("interp 2");
    CHECK(r.Block() == 127.f);
    r.Message("interp 0");
    r.source->SetFloatData(0, 5.f);
    CHECK(r.Block() == 0.f);
    r.Message("extend 1");
    CHECK(r.Block() == -128.f);
    r.Message("extend 2");
    CHECK(r.Block() == 64.f); // 5 wraps to 1
    r.Message("extend 9"); // held within 0-3
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.table->Lookup().extend() == tableLookup::outOfRange::HOLD);
    CHECK(r.Block() == 64.f);

    r.Message("extend 0");
    r.Message("inmap 0 1");
    r.source->SetFloatData(0, 0.5f);
    CHECK(r.Block() == 127.f); // half of 4 entries is entry 2
    r.Message("inmap none");
    r.source->SetFloatData(0, 2.f);
    CHECK(r.Block() == 127.f);

    r.Message("outscale 0 1");
    CHECK(r.Block() == 1.f); // 127 is the top of 0 to 127
    r.Message("signed 1");
    CHECK(r.Block() == doctest::Approx(255.f / 256.f));
    r.Message("range 127");
    r.Message("signed 0");
    CHECK(r.Block() == doctest::Approx(127.f / 126.f));
    r.Message("outscale 0 0");
    CHECK(r.Block() == 127.f);
    r.Message("inmap 0"); // one number is not a map: ignored
    r.Message("interp lots");
    CHECK(r.Block() == 127.f);
  }

  TEST_CASE("~table: no name, or a buffer filled later (#1148)") {
    {
      TableRig r("", YSE::OBJ::D_SIG, "1");
      CHECK(r.Block() == 0.f);
    }
    TableRig r("later_1148", YSE::OBJ::D_SIG, "3");
    CHECK(r.Block() == 0.f);
    auto b = Tens("later_1148", 8);
    CHECK(r.Block() == 30.f);
    b->setSize(2);
    CHECK(r.Block() == 0.f); // 3 is now outside
  }

  TEST_CASE("~table reads what ~poke wrote into a ~buffer (#1148)") {
    // The patch a user builds: [~buffer curve] holds the table, [~poke]
    // writes 0.25 at a ~count, and [~table curve] reads it at an index.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* buffer = p.CreateObject(YSE::OBJ::D_BUFFER, "curve_1148");
    YSE::pHandle* value = p.CreateObject(YSE::OBJ::D_SIG, "0.25");
    YSE::pHandle* writeAt = p.CreateObject(YSE::OBJ::D_COUNT, "0 0 1");
    YSE::pHandle* poke = p.CreateObject(YSE::OBJ::D_POKE, "curve_1148");
    YSE::pHandle* readAt = p.CreateObject(YSE::OBJ::D_SIG, "5.5");
    YSE::pHandle* table = p.CreateObject(YSE::OBJ::D_TABLE, "curve_1148 1");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(buffer != nullptr);
    REQUIRE(table != nullptr);
    p.Connect(value, 0, poke, 0);
    p.Connect(writeAt, 0, poke, 1);
    p.Connect(readAt, 0, table, 0);
    p.Connect(table, 0, dac, 0);
    std::shared_ptr<sampleBuffer> curve = sampleBufferRegistry::find("curve_1148");
    REQUIRE(curve != nullptr);
    curve->setSize(STANDARD_BUFFERSIZE * 2);
    p.Calculate(YSE::T_DSP);
    p.Calculate(YSE::T_DSP);
    CHECK(Out(p, 0) == 0.25f);
    // A ~buffer edit is heard from the next block.
    p.GetObjectFromID(buffer->GetID())->GetInlet(0)->SetMessage("fill 0.75", YSE::T_GUI);
    p.FileIO()->WaitIdle();
    p.Calculate(YSE::T_DSP);
    p.FileIO()->WaitIdle();
    p.Calculate(YSE::T_DSP);
    CHECK(Out(p, 0) == 0.75f);
  }

  TEST_CASE("~table: name and set read another buffer from a later block (#1148)") {
    auto a = Tens("set_1148_a", 8);
    auto b = Tens("set_1148_b", 8);
    b->applyOffset(1.f);
    TableRig r("set_1148_a", YSE::OBJ::D_SIG, "3");
    CHECK(r.Block() == 30.f);
    r.Message("name set_1148_b");
    r.Settle();
    CHECK(r.table->Buffer() == b);
    CHECK_FALSE(r.table->ReleasePending());
    CHECK(r.Block() == 31.f);
    r.Message("set set_1148_a");
    r.Settle();
    CHECK(r.table->Buffer() == a);
    r.Message("set");
    r.Settle();
    CHECK(r.table->Buffer() == nullptr);
    CHECK(r.Block() == 0.f);
    CHECK(r.p.FileIO()->Dropped() == 0);

    dTable standalone;
    standalone.SetParams("set_1148_a");
    standalone.GetInlet(0)->SetMessage("name set_1148_b", YSE::T_GUI);
    CHECK(standalone.Buffer() == a);
  }

  TEST_CASE("~table: arguments survive a DumpJSON / ParseJSON round trip; messages are not "
            "saved (#1148)") {
    auto a = Tens("json_1148_a", 8);
    auto b = Tens("json_1148_b", 8);
    const std::string args = "json_1148_a 1 1 0 1 0 2 0 0 64 1";
    std::string dump;
    {
      TableRig r(args, YSE::OBJ::D_SIG, "0.25");
      dump = r.p.DumpJSON();
      r.Message("set json_1148_b");
      r.Message("interp 0");
      r.Message("outscale 0 0");
      r.Settle();
      CHECK(r.table->Buffer() == b);
      CHECK(r.p.DumpJSON() == dump);
    }
    CHECK(dump.find(args) != std::string::npos);
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
    dst.Calculate(YSE::T_DSP);
    // a at 0.25 of 8 entries, linear: entry 2 = 20; outscale 0 2 over the
    // signed range -64 to 64: (20 + 64) / 128 * 2.
    CHECK(Out(dst, 0) == doctest::Approx(84.f / 64.f));
  }

  TEST_CASE("~table: blocks and messages on the audio thread allocate nothing (#1148)") {
    auto a = Tens("rt_1148_a", 1024);
    auto b = Tens("rt_1148_b", 512);
    TableRig r("rt_1148_a 1 2 0 1", YSE::OBJ::D_PHASOR, "3");
    r.Settle();
    // Built before the probe: the strings themselves allocate.
    const std::string messages[] = {"set rt_1148_b", "name rt_1148_a", "goto 3",     "next",
                                    "prev",          "interp 2",       "extend 1",   "inmap 0 1",
                                    "inmap none",    "outscale 0 1",   "range 64",   "signed 1",
                                    "inputmode 2",   "triggermode 1",  "inputmode 0"};
    for (int round = 0; round < 2; ++round) {
      {
        TestHelpers::ProbeScope probe;
        for (const std::string& m : messages)
          r.Message(m, YSE::T_DSP);
        r.table->GetInlet(0)->SetFloat(3.f, YSE::T_DSP);
        r.table->GetInlet(0)->SetInt(4, YSE::T_DSP);
        r.table->GetInlet(0)->SetBang(YSE::T_DSP);
        CHECK(TestHelpers::g_alloc_count.load() == 0);
      }
      r.p.FileIO()->WaitIdle();
      {
        TestHelpers::ProbeScope probe;
        r.p.Calculate(YSE::T_DSP);
        r.p.Calculate(YSE::T_DSP);
        CHECK(TestHelpers::g_alloc_count.load() == 0);
      }
      r.p.FileIO()->WaitIdle();
      r.p.Calculate(YSE::T_DSP); // hands the slots back
    }
    CHECK(r.table->Dropped() == 0);
    CHECK(r.p.FileIO()->Dropped() == 0);
  }

  TEST_CASE("~table renders while control threads edit, set and message it (#1148)") {
    // Buffers filled 1 and 2, resized, refilled and switched from control
    // threads while the audio thread reads them, linearly, at a ~phasor's
    // phase, and a third thread sends position messages and bangs. Every
    // value heard is 0, 1, 2 or 3, or a line between two of those. Under
    // ASan or TSan a freed storage or a broken handoff shows up here.
    auto ones = sampleBufferRegistry::acquire("race_1148_1");
    ones->setSize(64);
    ones->fill(1.f);
    auto twos = sampleBufferRegistry::acquire("race_1148_2");
    twos->setSize(200);
    twos->fill(2.f);
    TableRig r("race_1148_1 1 2 0 1", YSE::OBJ::D_PHASOR, "441");
    const std::string toOnes = "set race_1148_1";
    const std::string toTwos = "set race_1148_2";

    std::atomic<bool> stop{false};
    std::thread setter([&] {
      int n = 0;
      while (!stop.load()) {
        r.Message((n++ % 2) != 0 ? toOnes : toTwos, YSE::T_GUI);
        std::this_thread::yield();
      }
    });
    std::thread editor([&] {
      int n = 0;
      while (!stop.load()) {
        ones->setSize(64 + static_cast<UInt>(n % 7));
        ones->fill(n % 2 != 0 ? 1.f : 3.f);
        n++;
        std::this_thread::yield();
      }
    });
    std::thread sender([&] {
      int n = 0;
      while (!stop.load()) {
        r.Message(n % 3 == 0 ? "next" : (n % 3 == 1 ? "prev" : "goto 2"), YSE::T_GUI);
        r.table->GetInlet(0)->SetBang(YSE::T_GUI);
        n++;
        std::this_thread::yield();
      }
    });

    int bad = 0;
    for (int block = 0; block < 4000; ++block) {
      r.p.Calculate(YSE::T_DSP);
      for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
        const float v = Out(r.p, i);
        if (!(v >= 0.f && v <= 3.f)) bad++;
      }
    }
    stop.store(true);
    setter.join();
    editor.join();
    sender.join();
    CHECK(bad == 0);
  }

} // TEST_SUITE

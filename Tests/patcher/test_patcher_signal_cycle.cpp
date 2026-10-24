// Tests for the ~cycle patcher object (patcher/generatorObjects/dCycle),
// issue #1054. The oscillator itself is DSP::cycle's and is tested in
// Tests/dsp/test_cycle.cpp; these show that pins, messages, parameters and a
// named sample buffer reach it, alone and in a rendered patch.

#include <doctest/doctest.h>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "dsp/sampling/sampleBuffer.hpp"
#include "headers/constants.hpp"
#include "patcher/generatorObjects/dCycle.h"
#include "patcher/io/fileScheduler.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::sampleBuffer;
using YSE::DSP::sampleBufferRegistry;
using YSE::PATCHER::dCycle;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr double kTwoPi = 6.283185307179586;

  // 100 samples a cycle at whatever rate the test binary runs.
  float HundredSampleFrequency() {
    return static_cast<float>(YSE::SAMPLERATE) / 100.f;
  }

  bool Near(double a, double b, double tolerance = 1e-5) {
    return std::fabs(a - b) <= tolerance;
  }

  void Wire(dCycle& osc, BufferSink& sink) {
    osc.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(osc.GetOutlet(0), 0);
  }

  // A named buffer holding a ramp 0, 1, 2, ... in its first channel, so a
  // table read shows which sample it landed on.
  std::shared_ptr<sampleBuffer> RampBuffer(const std::string& name, UInt frames) {
    std::shared_ptr<sampleBuffer> buffer = sampleBufferRegistry::acquire(name);
    buffer->setSize(frames, 1);
    std::vector<float> ramp(frames);
    for (UInt i = 0; i < frames; ++i)
      ramp[i] = static_cast<float>(i);
    buffer->setSamples(0, 0, ramp.data(), frames);
    return buffer;
  }

  YSE::PATCHER::pObject* Object(patcherImplementation& p, YSE::pHandle* h) {
    return p.GetObjectFromID(h->GetID());
  }

  // A still ~cycle (frequency 0) at phase 0.5 into a dac, so output[0][0] is
  // the table's middle point, for the rebinding messages (#1192).
  struct RebindRig {
    patcherImplementation p{1, nullptr};
    YSE::pHandle* handle = nullptr;
    dCycle* osc = nullptr;

    explicit RebindRig(const std::string& args) {
      handle = p.CreateObject(YSE::OBJ::D_CYCLE, args);
      REQUIRE(handle != nullptr);
      YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      p.Connect(handle, 0, dac, 0);
      osc = static_cast<dCycle*>(Object(p, handle));
      osc->GetInlet(1)->SetFloat(0.5f, YSE::T_GUI);
    }

    void Message(const std::string& text, YSE::THREAD thread = YSE::T_GUI) {
      osc->GetInlet(0)->SetMessage(text, thread);
    }

    // The bind task runs on the background pool; the next block takes the
    // table, and the task that frees the old one runs after it.
    float Settle() {
      YSE::PATCHER::fileScheduler* io = p.FileIO();
      REQUIRE(io != nullptr);
      io->WaitIdle();
      p.Calculate(YSE::T_DSP);
      const float first = p.output[0].getPtr()[0];
      io->WaitIdle();
      return first;
    }
  };
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~cycle: registry and shape (#1054)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_CYCLE);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~cycle");
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::D_CYCLE));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::OSC);
    CHECK(obj->IsDSPObject());
    CHECK(obj->GetParamCount() == 4);
  }

  TEST_CASE("~cycle: the left float sets the frequency of a cosine (#1054)") {
    dCycle osc;
    BufferSink sink;
    Wire(osc, sink);
    CHECK_FALSE(osc.UsesBuffer());
    osc.GetInlet(0)->SetFloat(HundredSampleFrequency(), YSE::T_GUI);
    osc.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CHECK(Near(sink.received->getPtr()[0], 1.0));
    CHECK(Near(sink.received->getPtr()[25], 0.0));
    CHECK(Near(sink.received->getPtr()[50], -1.0));

    osc.GetInlet(0)->SetInt(0, YSE::T_GUI);
    osc.Calculate(YSE::T_DSP);
    CHECK(Near(sink.received->getPtr()[127], sink.received->getPtr()[0]));
  }

  TEST_CASE("~cycle: a connected frequency signal wins over the float (#1054, #1036)") {
    dCycle osc;
    BufferSink sink;
    Wire(osc, sink);
    osc.GetInlet(0)->SetFloat(0.f, YSE::T_GUI);
    YSE::DSP::buffer frequency(YSE::STANDARD_BUFFERSIZE);
    frequency = HundredSampleFrequency();
    osc.GetInlet(0)->SetBuffer(&frequency, YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CHECK(Near(sink.received->getPtr()[50], -1.0));
  }

  TEST_CASE("~cycle: the right inlet is a phase offset, as a number or a signal (#1054)") {
    dCycle osc;
    BufferSink sink;
    Wire(osc, sink);

    osc.GetInlet(1)->SetFloat(0.5f, YSE::T_GUI);
    osc.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CHECK(Near(sink.received->getPtr()[0], -1.0));
    osc.GetInlet(1)->SetInt(1, YSE::T_GUI); // a whole cycle
    osc.Calculate(YSE::T_DSP);
    CHECK(Near(sink.received->getPtr()[0], 1.0));

    // A phase signal: phase modulation of a still oscillator.
    YSE::DSP::buffer phase(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < phase.getLength(); ++i)
      phase.getPtr()[i] = static_cast<float>(i) / 128.f;
    osc.GetInlet(1)->SetBuffer(&phase, YSE::T_DSP);
    osc.Calculate(YSE::T_DSP);
    for (unsigned int i = 0; i < 128; i += 16) {
      CAPTURE(i);
      CHECK(Near(sink.received->getPtr()[i], std::cos(kTwoPi * i / 128.0)));
    }
  }

  TEST_CASE("~cycle: reset, frequency and phase messages (#1054)") {
    dCycle osc;
    BufferSink sink;
    Wire(osc, sink);
    osc.SetParams(std::to_string(HundredSampleFrequency()));
    osc.Calculate(YSE::T_DSP); // 128 samples on: phase 0.28

    osc.GetInlet(0)->SetMessage("reset 0.5", YSE::T_GUI);
    osc.Calculate(YSE::T_DSP);
    CHECK(Near(sink.received->getPtr()[0], -1.0));

    osc.GetInlet(0)->SetMessage("reset", YSE::T_GUI);
    osc.Calculate(YSE::T_DSP);
    CHECK(Near(sink.received->getPtr()[0], 1.0));

    osc.GetInlet(0)->SetMessage("frequency 0", YSE::T_GUI);
    osc.GetInlet(0)->SetMessage("phase 0.25", YSE::T_GUI);
    osc.GetInlet(0)->SetMessage("reset 0", YSE::T_GUI);
    osc.Calculate(YSE::T_DSP);
    CHECK(Near(sink.received->getPtr()[0], 0.0));
    CHECK(Near(sink.received->getPtr()[127], 0.0)); // frequency 0: still

    // Unknown words and missing numbers change nothing, and outside a
    // patcher neither does a rebind (#1192: it needs the file scheduler).
    osc.GetInlet(0)->SetMessage("frequency", YSE::T_GUI);
    osc.GetInlet(0)->SetMessage("set nothing", YSE::T_GUI);
    CHECK_FALSE(osc.UsesBuffer());
    osc.Calculate(YSE::T_DSP);
    CHECK(Near(sink.received->getPtr()[0], 0.0));
  }

  TEST_CASE("~cycle: a named buffer is the waveform, from an offset, for a size (#1054)") {
    std::shared_ptr<sampleBuffer> buffer = RampBuffer("cycle_1054_table", 1024);
    dCycle osc;
    BufferSink sink;
    Wire(osc, sink);
    osc.SetParams("0 cycle_1054_table");
    CHECK(osc.UsesBuffer());

    // Size 0: 512 samples from the offset. Phase 0.5 is point 256.
    osc.GetInlet(1)->SetFloat(0.5f, YSE::T_GUI);
    osc.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CHECK(sink.received->getPtr()[0] == doctest::Approx(256.f));

    osc.GetInlet(0)->SetMessage("buffer_offset 100", YSE::T_GUI);
    osc.Calculate(YSE::T_DSP);
    CHECK(sink.received->getPtr()[0] == doctest::Approx(356.f));

    osc.GetInlet(0)->SetMessage("buffer_sizeinsamps -1", YSE::T_GUI); // the rest: 924
    osc.Calculate(YSE::T_DSP);
    CHECK(sink.received->getPtr()[0] == doctest::Approx(100.f + 462.f));

    // The same through the arguments.
    dCycle fromArgs;
    BufferSink argSink;
    Wire(fromArgs, argSink);
    fromArgs.SetParams("0 cycle_1054_table 200 64");
    fromArgs.GetInlet(1)->SetFloat(0.5f, YSE::T_GUI);
    fromArgs.Calculate(YSE::T_DSP);
    REQUIRE(argSink.received != nullptr);
    CHECK(argSink.received->getPtr()[0] == doctest::Approx(232.f));
  }

  TEST_CASE("~cycle: a buffer filled or changed later is followed from the next block (#1054)") {
    dCycle osc;
    BufferSink sink;
    Wire(osc, sink);
    osc.SetParams("0 cycle_1054_later");
    osc.GetInlet(1)->SetFloat(0.25f, YSE::T_GUI);

    // The name is held now, but the buffer is empty: silence.
    osc.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CHECK(sink.received->getPtr()[0] == 0.f);

    std::shared_ptr<sampleBuffer> buffer = sampleBufferRegistry::find("cycle_1054_later");
    REQUIRE(buffer != nullptr);
    buffer->setSize(512, 1);
    buffer->fillSine(); // one cycle of a sine: 1 at phase 0.25
    osc.Calculate(YSE::T_DSP);
    CHECK(Near(sink.received->getPtr()[0], 1.0, 1e-4));

    buffer->applyGain(0.5f);
    osc.Calculate(YSE::T_DSP);
    CHECK(Near(sink.received->getPtr()[0], 0.5, 1e-4));
  }

  TEST_CASE("~cycle: in a rendered patch, a ~buffer table phase-modulated by ~sig (#1054)") {
    patcherImplementation p(2, nullptr);
    YSE::pHandle* table = p.CreateObject(YSE::OBJ::D_BUFFER, "cycle_1054_patch");
    YSE::pHandle* offset = p.CreateObject(YSE::OBJ::D_SIG, "0.25");
    YSE::pHandle* tableOsc = p.CreateObject(YSE::OBJ::D_CYCLE, "0 cycle_1054_patch");
    YSE::pHandle* cosine =
        p.CreateObject(YSE::OBJ::D_CYCLE, std::to_string(HundredSampleFrequency()));
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(table != nullptr);
    p.Connect(offset, 0, tableOsc, 1);
    p.Connect(tableOsc, 0, dac, 0);
    p.Connect(cosine, 0, dac, 1);

    // The host fills the buffer the patch names: one cycle of a sine.
    std::shared_ptr<sampleBuffer> buffer = sampleBufferRegistry::find("cycle_1054_patch");
    REQUIRE(buffer != nullptr);
    buffer->setSize(512, 1);
    buffer->fillSine();

    p.Calculate(YSE::T_DSP);
    // A still oscillator, phase 0.25 from ~sig: the top of the sine.
    for (unsigned int i = 0; i < YSE::STANDARD_BUFFERSIZE; i += 32)
      CHECK(Near(p.output[0].getPtr()[i], 1.0, 1e-4));
    // The cosine at 100 samples a cycle.
    CHECK(Near(p.output[1].getPtr()[0], 1.0));
    CHECK(Near(p.output[1].getPtr()[50], -1.0));

    // ~sig moves the phase: 0.75 is the bottom of the sine.
    offset->SetFloatData(0, 0.75f);
    p.Calculate(YSE::T_DSP);
    CHECK(Near(p.output[0].getPtr()[0], -1.0, 1e-4));

    // A message to ~cycle inside the patch.
    Object(p, cosine)->GetInlet(0)->SetMessage("reset 0.5", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    CHECK(Near(p.output[1].getPtr()[0], -1.0));
  }

  TEST_CASE("~cycle: parameters survive a DumpJSON / ParseJSON round trip (#1054)") {
    std::shared_ptr<sampleBuffer> buffer = RampBuffer("cycle_1054_json", 1024);
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* a = src.CreateObject(YSE::OBJ::D_CYCLE, "0 cycle_1054_json 300 -1");
      YSE::pHandle* b =
          src.CreateObject(YSE::OBJ::D_CYCLE, std::to_string(HundredSampleFrequency()));
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      src.Connect(a, 0, dac, 0);
      src.Connect(b, 0, dac, 1);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);
    dst.Calculate(YSE::T_DSP);
    // Still, at phase 0: the table's first point, sample 300.
    CHECK(dst.output[0].getPtr()[0] == doctest::Approx(300.f));
    CHECK(dst.output[0].getPtr()[127] == doctest::Approx(300.f));
    CHECK(Near(dst.output[1].getPtr()[50], -1.0));
  }

  TEST_CASE("~cycle: messages and blocks on the audio thread allocate nothing (#1054)") {
    std::shared_ptr<sampleBuffer> buffer = RampBuffer("cycle_1054_alloc", 2048);
    dCycle osc;
    osc.SetParams("440 cycle_1054_alloc");
    BufferSink sink;
    Wire(osc, sink);
    YSE::DSP::buffer phase(YSE::STANDARD_BUFFERSIZE);
    phase = 0.1f;
    // Built before the probe: the strings themselves allocate.
    const std::string reset = "reset 0.25";
    const std::string size = "buffer_sizeinsamps 1024";
    const std::string offset = "buffer_offset 10";
    const std::string frequency = "frequency 110";
    osc.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      osc.GetInlet(0)->SetFloat(220.f, YSE::T_DSP);
      osc.GetInlet(1)->SetFloat(0.5f, YSE::T_DSP);
      osc.GetInlet(0)->SetMessage(reset, YSE::T_DSP);
      osc.GetInlet(0)->SetMessage(size, YSE::T_DSP);
      osc.GetInlet(0)->SetMessage(offset, YSE::T_DSP);
      osc.GetInlet(0)->SetMessage(frequency, YSE::T_DSP);
      osc.Calculate(YSE::T_DSP);
      osc.GetInlet(1)->SetBuffer(&phase, YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  TEST_CASE(
      "~cycle: the buffer is edited on a control thread while the audio thread reads (#1054)") {
    // The table read goes through the buffer's hazard pin: every block reads
    // one whole version, never freed or half-made storage. Under ASan or TSan
    // a broken handoff shows up here.
    std::shared_ptr<sampleBuffer> buffer = sampleBufferRegistry::acquire("cycle_1054_race");
    buffer->setSize(512, 1);
    buffer->fill(1.f);
    // Each edit publishes one whole, constant version of another length.
    std::shared_ptr<sampleBuffer> shortOnes = sampleBuffer::create(1, 256);
    shortOnes->fill(1.f);
    std::shared_ptr<sampleBuffer> longTwos = sampleBuffer::create(1, 1024);
    longTwos->fill(2.f);
    dCycle osc;
    osc.SetParams(std::to_string(HundredSampleFrequency()) + " cycle_1054_race 0 -1");
    BufferSink sink;
    Wire(osc, sink);

    std::atomic<bool> stop{false};
    std::thread control([&] {
      int n = 0;
      while (!stop.load()) {
        const bool odd = (n++ % 2) != 0;
        buffer->copyFrom(odd ? *shortOnes : *longTwos);
        buffer->collect();
      }
    });

    bool whole = true;
    for (int block = 0; block < 2000; ++block) {
      osc.Calculate(YSE::T_DSP);
      // A constant table: every sample of a block is that version's value.
      const float v = sink.received->getPtr()[0];
      if (!(v == 1.f || v == 2.f)) whole = false;
      for (unsigned int i = 1; i < sink.received->getLength(); ++i)
        if (sink.received->getPtr()[i] != v) whole = false;
    }
    stop.store(true);
    control.join();
    CHECK(whole);
  }

  // ─── set / setall / buffer (#1192) ──────────────────────────────────────────

  TEST_CASE("~cycle: set, setall and buffer rebind the table (#1192)") {
    std::shared_ptr<sampleBuffer> a = RampBuffer("cycle_1192_a", 1024);
    std::shared_ptr<sampleBuffer> b = RampBuffer("cycle_1192_b", 1024);
    b->applyOffset(1000.f); // 1000, 1001, ...
    RebindRig r("0");
    r.p.Calculate(YSE::T_DSP);
    CHECK(Near(r.p.output[0].getPtr()[0], -1.0)); // the cosine at 0.5
    CHECK_FALSE(r.osc->UsesBuffer());

    // set <name> <offset>: 512 points from 100, the middle one is 356.
    r.Message("set cycle_1192_a 100");
    CHECK(r.Settle() == doctest::Approx(356.f));
    CHECK(r.osc->UsesBuffer());
    CHECK(r.osc->Buffer() == a);
    CHECK_FALSE(r.osc->ReleasePending()); // the cosine's table is freed

    // buffer <name>: the other buffer, offset and size as they were.
    r.Message("buffer cycle_1192_b");
    CHECK(r.Settle() == doctest::Approx(1356.f));
    CHECK(r.osc->Buffer() == b);

    // setall <name>: all 1024 points, from 0.
    r.Message("setall cycle_1192_a");
    CHECK(r.Settle() == doctest::Approx(512.f));
    // ...until buffer_sizeinsamps gives a fixed size back.
    r.Message("buffer_sizeinsamps 64");
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.p.output[0].getPtr()[0] == doctest::Approx(32.f));

    // set without an offset starts at 0; the size stays.
    r.Message("set cycle_1192_b");
    CHECK(r.Settle() == doctest::Approx(1032.f));

    // set with no name: back to the cosine.
    r.Message("set");
    CHECK(Near(r.Settle(), -1.0));
    CHECK_FALSE(r.osc->UsesBuffer());
    CHECK(r.osc->Buffer() == nullptr);
    CHECK_FALSE(r.osc->ReleasePending());
    CHECK(r.p.FileIO()->Dropped() == 0);
  }

  TEST_CASE("~cycle: of rebinds sent before a block the last wins, with their offsets (#1192)") {
    std::shared_ptr<sampleBuffer> a = RampBuffer("cycle_1192_last_a", 1024);
    std::shared_ptr<sampleBuffer> b = RampBuffer("cycle_1192_last_b", 1024);
    b->applyOffset(1000.f);
    RebindRig r("0");
    r.p.Calculate(YSE::T_DSP);
    // Both tasks run before the block: the second replaces the first's table
    // but keeps the offset it would have brought.
    r.Message("set cycle_1192_last_a 200");
    r.Message("buffer cycle_1192_last_b");
    CHECK(r.Settle() == doctest::Approx(1456.f));
    CHECK(r.osc->Buffer() == b);
  }

  TEST_CASE("~cycle: malformed rebinds change nothing (#1192)") {
    std::shared_ptr<sampleBuffer> a = RampBuffer("cycle_1192_bad", 1024);
    RebindRig r("0 cycle_1192_bad");
    CHECK(r.Settle() == doctest::Approx(256.f));
    r.Message("setall");
    r.Message("buffer");
    r.Message("set " + std::string(YSE::PATCHER::fileScheduler::PATH_CAPACITY, 'x'));
    CHECK(r.Settle() == doctest::Approx(256.f));
    CHECK(r.osc->Buffer() == a);
    CHECK(r.p.FileIO()->Dropped() == 0);
  }

  TEST_CASE("~cycle: a rebind is not saved by DumpJSON (#1192)") {
    std::shared_ptr<sampleBuffer> a = RampBuffer("cycle_1192_json_a", 1024);
    std::shared_ptr<sampleBuffer> b = RampBuffer("cycle_1192_json_b", 1024);
    b->applyOffset(1000.f);
    RebindRig r("0 cycle_1192_json_a 100");
    const std::string before = r.p.DumpJSON();
    r.Message("set cycle_1192_json_b 300");
    CHECK(r.Settle() == doctest::Approx(1556.f));
    // The creation arguments are what is saved, so a reload reads them again.
    CHECK(r.p.DumpJSON() == before);
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(before);
    dst.Calculate(YSE::T_DSP);
    CHECK(dst.output[0].getPtr()[0] == doctest::Approx(100.f)); // phase 0: point 100 of a
  }

  TEST_CASE("~cycle: a rebind in flight survives the object's deletion (#1192)") {
    std::shared_ptr<sampleBuffer> a = RampBuffer("cycle_1192_deleted", 64);
    const long held = a.use_count();
    {
      RebindRig r("0");
      r.Message("set cycle_1192_deleted");
      r.p.DeleteObject(r.handle);
      r.p.FileIO()->WaitIdle();
      r.p.Calculate(YSE::T_DSP);
      r.p.FileIO()->WaitIdle();
    }
    // Whatever the task bound was freed with the object: nothing still holds
    // the buffer.
    CHECK(a.use_count() == held);
  }

  TEST_CASE("~cycle: rebinding allocates nothing on the audio thread (#1192)") {
    std::shared_ptr<sampleBuffer> a = RampBuffer("cycle_1192_alloc_a", 1024);
    std::shared_ptr<sampleBuffer> b = RampBuffer("cycle_1192_alloc_b", 1024);
    b->applyOffset(1000.f);
    RebindRig r("0 cycle_1192_alloc_a");
    r.Settle();
    // Built before the probe: the strings themselves allocate.
    const std::vector<std::string> messages = {
        "set cycle_1192_alloc_b 100", "setall cycle_1192_alloc_a", "buffer cycle_1192_alloc_b"};
    for (const std::string& m : messages) {
      CAPTURE(m);
      {
        TestHelpers::ProbeScope probe;
        r.Message(m, YSE::T_DSP);
        CHECK(TestHelpers::g_alloc_count.load() == 0);
      }
      r.p.FileIO()->WaitIdle();
      {
        // The block that takes the new table and asks for the old one to go.
        TestHelpers::ProbeScope probe;
        r.p.Calculate(YSE::T_DSP);
        CHECK(TestHelpers::g_alloc_count.load() == 0);
      }
      r.p.FileIO()->WaitIdle();
      CHECK_FALSE(r.osc->ReleasePending());
    }
    CHECK(r.osc->Buffer() == b);
    CHECK(r.p.output[0].getPtr()[0] == doctest::Approx(1512.f)); // b, all of it
  }

  TEST_CASE("~cycle: a control thread rebinds while the audio thread renders (#1192)") {
    // Two constant tables of different lengths: every block reads one whole
    // table, never freed or half-made memory. Under ASan or TSan a broken
    // handoff shows up here.
    std::shared_ptr<sampleBuffer> ones = sampleBufferRegistry::acquire("cycle_1192_race_1");
    ones->setSize(256, 1);
    ones->fill(1.f);
    std::shared_ptr<sampleBuffer> twos = sampleBufferRegistry::acquire("cycle_1192_race_2");
    twos->setSize(1024, 1);
    twos->fill(2.f);
    RebindRig r(std::to_string(HundredSampleFrequency()) + " cycle_1192_race_1 0 -1");
    const std::string toOnes = "set cycle_1192_race_1";
    const std::string toTwos = "buffer cycle_1192_race_2";

    std::atomic<bool> stop{false};
    std::thread control([&] {
      int n = 0;
      while (!stop.load()) {
        r.Message((n++ % 2) != 0 ? toOnes : toTwos);
        std::this_thread::yield();
      }
    });

    bool whole = true;
    int switches = 0;
    float last = 1.f;
    for (int block = 0; block < 20000 && (block < 2000 || switches == 0); ++block) {
      r.p.Calculate(YSE::T_DSP);
      const float* out = r.p.output[0].getPtr();
      const float v = out[0];
      if (!(v == 1.f || v == 2.f)) whole = false;
      for (unsigned int i = 1; i < YSE::STANDARD_BUFFERSIZE; ++i)
        if (out[i] != v) whole = false;
      if (v != last) switches++;
      last = v;
    }
    stop.store(true);
    control.join();
    CHECK(whole);
    CHECK(switches > 0);

    // Quiet again, everything let go is freed.
    r.Settle();
    r.Settle();
    CHECK_FALSE(r.osc->ReleasePending());
  }

} // TEST_SUITE("patcher")

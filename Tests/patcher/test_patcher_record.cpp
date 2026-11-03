// Tests for the ~record patcher object (patcher/sampling/dRecord), issue
// #1143. The recording itself is DSP::bufferRecorder's
// (Tests/dsp/test_buffer_recorder.cpp) and the in-place writer is
// DSP::sampleBufferWriter's (test_sampleBuffer.cpp). These show that pins,
// messages and arguments reach them, in rendered patches heard through a
// ~dac: a ~count recorded into a ~buffer and read back by ~index, the switch,
// the loop points by inlet, message and argument, several channels, set
// (#1256), a round trip, allocation-free blocks and messages on the audio
// thread, and a recording read and edited from other threads while it runs.
// The buffers run at 1000 Hz, so a millisecond is a frame.

#include <doctest/doctest.h>
#include <atomic>
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
#include "patcher/sampling/dRecord.h"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

using YSE::STANDARD_BUFFERSIZE;
using YSE::DSP::sampleBuffer;
using YSE::DSP::sampleBufferRegistry;
using YSE::PATCHER::dRecord;
using YSE::PATCHER::patcherImplementation;

namespace {

  constexpr float RATE = 1000.f; // a frame per millisecond

  // A named buffer of `frames` frames at 1000 Hz, every sample `value`.
  std::shared_ptr<sampleBuffer> Filled(const std::string& name, UInt channels, UInt frames,
                                       float value) {
    std::shared_ptr<sampleBuffer> b = sampleBufferRegistry::acquire(name);
    b->setSize(frames, static_cast<Int>(channels));
    b->setSampleRate(RATE);
    b->fill(value);
    return b;
  }

  float At(const sampleBuffer& b, UInt c, UInt frame) {
    float out = 0.f;
    b.getSamples(c, frame, &out, 1);
    return out;
  }

  float Out(patcherImplementation& p, UInt i) {
    return p.output[0].getPtr()[i];
  }

  // [source] -> [~record args] -> [~dac], `source` made from `type` and
  // `sourceArgs`, or no source at all when `type` is empty.
  struct RecordRig {
    patcherImplementation p{1, nullptr};
    YSE::pHandle* source = nullptr;
    YSE::pHandle* handle = nullptr;
    YSE::pHandle* dac = nullptr;
    dRecord* record = nullptr;

    RecordRig(const std::string& args, const char* type, const std::string& sourceArgs) {
      handle = p.CreateObject(YSE::OBJ::D_RECORD, args);
      REQUIRE(handle != nullptr);
      dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(dac != nullptr);
      if (type != nullptr && *type != '\0') {
        source = p.CreateObject(type, sourceArgs);
        REQUIRE(source != nullptr);
        p.Connect(source, 0, handle, 0);
      }
      p.Connect(handle, 0, dac, 0);
      record = static_cast<dRecord*>(p.GetObjectFromID(handle->GetID()));
    }

    void Message(const std::string& text, YSE::THREAD thread = YSE::T_GUI) {
      record->GetInlet(0)->SetMessage(text, thread);
    }

    void Switch(int on, YSE::THREAD thread = YSE::T_GUI) {
      record->GetInlet(0)->SetInt(on, thread);
    }

    // The bind task runs on the background pool, the next block takes the
    // writer, and the task that frees the old one runs after it.
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

  TEST_CASE("~record: registry and shape (#1143)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_RECORD, "shape_1143");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~record");
    CHECK(h->GetInputs() == 3); // signal, start, end
    REQUIRE(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    // The channel count sets the signal inlets; the points stay rightmost.
    YSE::pHandle* three = p.CreateObject(YSE::OBJ::D_RECORD, "shape_1143 3");
    REQUIRE(three != nullptr);
    CHECK(three->GetInputs() == 5);

    bool listed = false;
    for (const auto& name : YSE::PATCHER::Register().AllNames())
      if (name == YSE::OBJ::D_RECORD) listed = true;
    CHECK(listed);

    dRecord standalone;
    CHECK(standalone.GetCategory() == YSE::PATCHER::pCategory::SAMPLING);
    CHECK(standalone.Channels() == 1);
    CHECK(standalone.Buffer() == nullptr);
    CHECK(standalone.GetInlet(0)->AcceptsDSP());
    standalone.SetParams("shape_1143 0");
    CHECK(standalone.Channels() == 1);
    standalone.SetParams("shape_1143 1000");
    CHECK(standalone.Channels() == 64);
    CHECK(standalone.GetInlet(63)->AcceptsDSP());
    CHECK_FALSE(standalone.GetInlet(64)->AcceptsDSP());
  }

  TEST_CASE("~record: a ~count recorded into a ~buffer, read back by ~index (#1143)") {
    // The patch a user builds: [~count] -> [~record tape], 1 into the left
    // inlet starts it, the sync signal ramps at the [~dac], and [~index tape]
    // in the same patch hears the recording.
    patcherImplementation p(2, nullptr);
    YSE::pHandle* buffer = p.CreateObject(YSE::OBJ::D_BUFFER, "tape_1143");
    YSE::pHandle* count = p.CreateObject(YSE::OBJ::D_COUNT, "0 0 1");
    YSE::pHandle* rec = p.CreateObject(YSE::OBJ::D_RECORD, "tape_1143");
    YSE::pHandle* readAt = p.CreateObject(YSE::OBJ::D_SIG, "5");
    YSE::pHandle* index = p.CreateObject(YSE::OBJ::D_INDEX, "tape_1143");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(buffer != nullptr);
    REQUIRE(rec != nullptr);
    REQUIRE(index != nullptr);
    p.Connect(count, 0, rec, 0);
    p.Connect(rec, 0, dac, 0);
    p.Connect(readAt, 0, index, 0);
    p.Connect(index, 0, dac, 1);

    std::shared_ptr<sampleBuffer> tape = sampleBufferRegistry::find("tape_1143");
    REQUIRE(tape != nullptr);
    const UInt frames = 2 * STANDARD_BUFFERSIZE + 10;
    tape->setSize(frames);
    tape->setSampleRate(RATE);
    tape->fill(-1.f);

    // Not started: nothing recorded, the sync is 0.
    p.Calculate(YSE::T_DSP);
    CHECK(Out(p, 0) == 0.f);
    CHECK(At(*tape, 0, 0) == -1.f);

    dRecord* object = static_cast<dRecord*>(p.GetObjectFromID(rec->GetID()));
    object->GetInlet(0)->SetInt(1, YSE::T_GUI);
    p.Calculate(YSE::T_DSP); // the count is at 128 .. 255 now
    for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
      CAPTURE(i);
      CHECK(Out(p, i) == doctest::Approx(static_cast<float>(i) / static_cast<float>(frames)));
      CHECK(At(*tape, 0, i) == static_cast<float>(STANDARD_BUFFERSIZE + i));
    }
    p.Calculate(YSE::T_DSP);
    // ~index hears frame 5 from the block after it was written, whichever of
    // the two ran first.
    CHECK(p.output[1].getPtr()[0] == static_cast<float>(STANDARD_BUFFERSIZE + 5));
    p.Calculate(YSE::T_DSP); // 10 more frames, then the end of the buffer
    CHECK(Out(p, 9) == doctest::Approx(static_cast<float>(frames - 1) / frames));
    CHECK(Out(p, 10) == 0.f);
    CHECK_FALSE(object->Recorder().recording());
    CHECK(At(*tape, 0, frames - 1) == static_cast<float>(STANDARD_BUFFERSIZE + frames - 1));
    p.Calculate(YSE::T_DSP);
    CHECK(Out(p, 0) == 0.f);
  }

  TEST_CASE("~record: 0 stops, a float switches, the last request in a block wins (#1143)") {
    auto b = Filled("switch_1143", 1, 1000, -1.f);
    RecordRig r("switch_1143", YSE::OBJ::D_SIG, "0.5");
    r.record->GetInlet(0)->SetFloat(0.25f, YSE::T_GUI); // not a value: starts
    r.p.Calculate(YSE::T_DSP);
    CHECK(At(*b, 0, 0) == 0.5f); // the ~sig, not the number
    CHECK(r.record->Recorder().position() == STANDARD_BUFFERSIZE);
    r.Switch(0);
    r.p.Calculate(YSE::T_DSP);
    CHECK(Out(r.p, 0) == 0.f);
    CHECK(At(*b, 0, STANDARD_BUFFERSIZE) == -1.f);
    // A start then a stop before the block: stopped.
    r.Switch(1);
    r.record->GetInlet(0)->SetFloat(0.f, YSE::T_GUI);
    r.p.Calculate(YSE::T_DSP);
    CHECK_FALSE(r.record->Recorder().recording());
    // A stop then a start: started, at the start point.
    r.Switch(0);
    r.Switch(1);
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.record->Recorder().recording());
    CHECK(r.record->Recorder().position() == STANDARD_BUFFERSIZE);
  }

  TEST_CASE("~record: an unconnected inlet records silence (#1143)") {
    auto b = Filled("silent_1143", 1, 500, -1.f);
    RecordRig r("silent_1143", "", "");
    r.Switch(1);
    r.p.Calculate(YSE::T_DSP);
    CHECK(At(*b, 0, 0) == 0.f);
    CHECK(At(*b, 0, STANDARD_BUFFERSIZE - 1) == 0.f);
    CHECK(At(*b, 0, STANDARD_BUFFERSIZE) == -1.f);
    CHECK(Out(r.p, 64) == doctest::Approx(64.f / 500.f));
  }

  TEST_CASE("~record: the loop points by inlet, by message and by argument (#1143)") {
    auto b = Filled("points_1143", 1, 1000, -1.f);
    RecordRig r("points_1143", YSE::OBJ::D_COUNT, "0 0 1");
    // The start and end inlets, in ms.
    r.record->GetInlet(1)->SetInt(100, YSE::T_GUI);
    r.record->GetInlet(2)->SetFloat(140.f, YSE::T_GUI);
    r.Switch(1);
    r.p.Calculate(YSE::T_DSP);
    CHECK(At(*b, 0, 99) == -1.f);
    CHECK(At(*b, 0, 100) == 0.f);
    CHECK(At(*b, 0, 139) == 39.f);
    CHECK(At(*b, 0, 140) == -1.f);
    CHECK(Out(r.p, 20) == doctest::Approx(0.5f));
    CHECK(Out(r.p, 40) == 0.f);
    CHECK_FALSE(r.record->Recorder().recording());

    // loop 1: the 40 frames over and over.
    r.Message("loop 1");
    r.Switch(1);
    r.p.Calculate(YSE::T_DSP); // the count is at 128 .. 255
    CHECK(r.record->Recorder().recording());
    CHECK(Out(r.p, 45) == doctest::Approx(5.f / 40.f));
    CHECK(At(*b, 0, 100) == 128.f + 120.f); // the eight after the last full pass
    CHECK(At(*b, 0, 107) == 128.f + 127.f);
    CHECK(At(*b, 0, 108) == 128.f + 88.f); // the last full pass

    // loopstart / loopend words, then reset: the whole buffer.
    r.Message("loopstart 500");
    r.Message("loopend 600");
    r.Switch(1);
    r.p.Calculate(YSE::T_DSP); // 256 .. 383 over 100 frames, still looping
    CHECK(At(*b, 0, 500) == 356.f);
    CHECK(At(*b, 0, 599) == 355.f);
    CHECK(At(*b, 0, 600) == -1.f);
    CHECK(Out(r.p, 50) == doctest::Approx(0.5f));
    r.Message("reset");
    r.Message("loop 0");
    r.Switch(1);
    r.p.Calculate(YSE::T_DSP);
    CHECK(At(*b, 0, 0) == 384.f);
    CHECK(Out(r.p, 100) == doctest::Approx(0.1f));
    // A malformed word is ignored.
    r.Message("loopstart");
    r.Message("loopend banana");
    r.p.Calculate(YSE::T_DSP);
    CHECK(At(*b, 0, STANDARD_BUFFERSIZE) == 512.f);

    // The arguments: name channels append loop loopstart loopend.
    auto c = Filled("args_1143", 1, 1000, -1.f);
    RecordRig a("args_1143 1 0 1 10 20", YSE::OBJ::D_COUNT, "0 0 1");
    CHECK(a.record->Recorder().loop() == false); // passed on at the next block
    a.Switch(1);
    a.p.Calculate(YSE::T_DSP);
    CHECK(a.record->Recorder().loop());
    CHECK(a.record->Recorder().loopStart() == 10.f);
    CHECK(a.record->Recorder().loopEnd() == 20.f);
    CHECK(At(*c, 0, 10) == 120.f);
    CHECK(At(*c, 0, 9) == -1.f);
    CHECK(At(*c, 0, 20) == -1.f);
  }

  TEST_CASE("~record: append resumes where the recording stopped (#1143)") {
    auto b = Filled("append_1143", 1, 1000, -1.f);
    RecordRig r("append_1143", YSE::OBJ::D_COUNT, "0 0 1");
    r.Message("append 1");
    r.Switch(1);
    r.p.Calculate(YSE::T_DSP);
    r.Switch(0);
    r.p.Calculate(YSE::T_DSP); // not recorded
    r.Switch(1);
    r.p.Calculate(YSE::T_DSP);
    CHECK(At(*b, 0, STANDARD_BUFFERSIZE - 1) == static_cast<float>(STANDARD_BUFFERSIZE - 1));
    CHECK(At(*b, 0, STANDARD_BUFFERSIZE) == static_cast<float>(2 * STANDARD_BUFFERSIZE));
    // append 0: a start begins at the start point again.
    r.Message("append 0");
    r.Switch(1);
    r.p.Calculate(YSE::T_DSP);
    CHECK(At(*b, 0, 0) == static_cast<float>(3 * STANDARD_BUFFERSIZE));
  }

  TEST_CASE("~record: several channels, each into its own; the points shift right "
            "(#1143)") {
    auto b = Filled("multi_1143", 3, 1000, -1.f);
    patcherImplementation p(1, nullptr);
    YSE::pHandle* rec = p.CreateObject(YSE::OBJ::D_RECORD, "multi_1143 2");
    YSE::pHandle* one = p.CreateObject(YSE::OBJ::D_SIG, "1");
    YSE::pHandle* two = p.CreateObject(YSE::OBJ::D_SIG, "2");
    YSE::pHandle* also = p.CreateObject(YSE::OBJ::D_SIG, "0.5");
    REQUIRE(rec != nullptr);
    REQUIRE(rec->GetInputs() == 4);
    p.Connect(one, 0, rec, 0);
    p.Connect(two, 0, rec, 1);
    p.Connect(also, 0, rec, 1); // two cords: summed (#1036)
    auto* object = static_cast<dRecord*>(p.GetObjectFromID(rec->GetID()));
    object->GetInlet(2)->SetInt(10, YSE::T_GUI);
    object->GetInlet(3)->SetInt(20, YSE::T_GUI);
    object->GetInlet(0)->SetInt(1, YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    CHECK(At(*b, 0, 10) == 1.f);
    CHECK(At(*b, 1, 19) == 2.5f);
    CHECK(At(*b, 2, 10) == -1.f); // no inlet for channel 3
    CHECK(At(*b, 0, 9) == -1.f);
    CHECK(At(*b, 1, 20) == -1.f);
  }

  TEST_CASE("~record: no name, or a buffer sized later (#1143)") {
    {
      RecordRig r("", YSE::OBJ::D_SIG, "1");
      r.Switch(1);
      r.p.Calculate(YSE::T_DSP);
      CHECK(Out(r.p, 0) == 0.f);
    }
    RecordRig r("later_1143", YSE::OBJ::D_SIG, "0.75");
    r.Switch(1);
    r.p.Calculate(YSE::T_DSP); // acquired, but empty: nothing to write
    CHECK(Out(r.p, 0) == 0.f);
    CHECK(r.record->Recorder().recording());
    auto b = Filled("later_1143", 1, 256, 0.f);
    r.p.Calculate(YSE::T_DSP);
    CHECK(At(*b, 0, 0) == 0.75f);
    CHECK(Out(r.p, 64) == doctest::Approx(0.25f));
  }

  TEST_CASE("~record: set records into another buffer from a later block (#1143)") {
    auto a = Filled("set_1143_a", 1, 1000, -1.f);
    auto b = Filled("set_1143_b", 1, 1000, -1.f);
    RecordRig r("set_1143_a", YSE::OBJ::D_SIG, "3");
    r.Switch(1);
    r.p.Calculate(YSE::T_DSP);
    CHECK(At(*a, 0, 0) == 3.f);

    r.Message("set set_1143_b");
    r.Settle(); // this block still went into a or into b, from frame 128 on
    CHECK(r.record->Buffer() == b);
    CHECK_FALSE(r.record->ReleasePending());
    r.p.Calculate(YSE::T_DSP);
    // The recording goes on into b from the frame it had reached.
    CHECK(At(*b, 0, 2 * STANDARD_BUFFERSIZE) == 3.f);
    CHECK(At(*b, 0, 0) == -1.f);

    // set with no name: unbound, nothing written.
    r.Message("set");
    r.Settle();
    CHECK(r.record->Buffer() == nullptr);
    r.p.Calculate(YSE::T_DSP);
    CHECK(Out(r.p, 0) == 0.f);
    CHECK(r.p.FileIO()->Dropped() == 0);

    // Outside a patcher a set does nothing.
    dRecord standalone;
    standalone.SetParams("set_1143_a");
    standalone.GetInlet(0)->SetMessage("set set_1143_b", YSE::T_GUI);
    CHECK(standalone.Buffer() == a);
  }

  TEST_CASE("~record: arguments survive a DumpJSON / ParseJSON round trip; messages are not "
            "saved (#1143)") {
    auto a = Filled("json_1143_a", 2, 1000, -1.f);
    auto b = Filled("json_1143_b", 2, 1000, -1.f);
    std::string dump;
    {
      RecordRig r("json_1143_a 2 1 1 10 20", YSE::OBJ::D_SIG, "6");
      dump = r.p.DumpJSON();
      r.Message("set json_1143_b");
      r.Message("loop 0");
      r.Message("loopstart 50");
      r.record->GetInlet(3)->SetInt(70, YSE::T_GUI);
      r.Settle();
      CHECK(r.record->Buffer() == b);
      CHECK(r.p.DumpJSON() == dump);
    }
    CHECK(dump.find("json_1143_a 2 1 1 10 20") != std::string::npos);
    CHECK(dump.find("json_1143_b") == std::string::npos);
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
    // The reloaded ~record has two signal inlets and the saved attributes.
    dRecord* object = nullptr;
    for (unsigned int i = 0; i < dst.Objects(); ++i) {
      YSE::pHandle* h = dst.GetHandleFromList(i);
      if (h != nullptr && std::string(h->Type()) == YSE::OBJ::D_RECORD)
        object = static_cast<dRecord*>(dst.GetObjectFromID(h->GetID()));
    }
    REQUIRE(object != nullptr);
    CHECK(object->Channels() == 2);
    object->GetInlet(0)->SetInt(1, YSE::T_GUI);
    dst.Calculate(YSE::T_DSP);
    CHECK(object->Recorder().loop());
    CHECK(object->Recorder().loopStart() == 10.f);
    CHECK(object->Recorder().loopEnd() == 20.f);
    CHECK(At(*a, 0, 10) == 6.f);
    CHECK(At(*a, 0, 20) == -1.f);
  }

  TEST_CASE("~record: blocks, numbers, words and set on the audio thread allocate nothing "
            "(#1143)") {
    auto a = Filled("rt_1143_a", 2, 1000, 0.f);
    auto b = Filled("rt_1143_b", 2, 500, 0.f);
    RecordRig r("rt_1143_a 2", YSE::OBJ::D_COUNT, "0 0 1");
    r.Settle();
    // Built before the probe: the strings themselves allocate.
    const std::string toB = "set rt_1143_b";
    const std::string toA = "set rt_1143_a";
    const std::string loopOn = "loop 1";
    const std::string appendOn = "append 1";
    const std::string start = "loopstart 10";
    const std::string end = "loopend 300";
    const std::string reset = "reset";
    for (const std::string* m : {&toB, &toA, &toB}) {
      CAPTURE(*m);
      {
        // Lock-free too: the request is a CAS into the scheduler's table.
        TestHelpers::ProbeScope probe;
        r.Message(*m, YSE::T_DSP);
        r.Message(loopOn, YSE::T_DSP);
        r.Message(appendOn, YSE::T_DSP);
        r.Message(start, YSE::T_DSP);
        r.Message(end, YSE::T_DSP);
        r.Message(reset, YSE::T_DSP);
        r.Switch(1, YSE::T_DSP);
        r.record->GetInlet(0)->SetFloat(1.f, YSE::T_DSP);
        r.record->GetInlet(2)->SetInt(5, YSE::T_DSP);
        r.record->GetInlet(2)->SetFloat(5.f, YSE::T_DSP);
        r.record->GetInlet(3)->SetInt(400, YSE::T_DSP);
        r.record->GetInlet(3)->SetFloat(400.f, YSE::T_DSP);
        CHECK(TestHelpers::g_alloc_count.load() == 0);
      }
      r.p.FileIO()->WaitIdle();
      {
        // The block that takes the new writer and asks for the old one to go.
        TestHelpers::ProbeScope probe;
        r.p.Calculate(YSE::T_DSP);
        r.p.Calculate(YSE::T_DSP);
        r.Switch(0, YSE::T_DSP);
        r.p.Calculate(YSE::T_DSP);
        CHECK(TestHelpers::g_alloc_count.load() == 0);
      }
      r.p.FileIO()->WaitIdle();
      CHECK_FALSE(r.record->ReleasePending());
      r.p.Calculate(YSE::T_DSP); // hands the slots back
    }
    CHECK(r.record->Buffer() == b);
    CHECK(r.p.FileIO()->Dropped() == 0);
  }

  TEST_CASE("~record records while other threads read, edit and set its buffers (#1143)") {
    // ~record loops a constant 1 or 2 into two buffers of different lengths,
    // switched from a control thread, while a reader on another thread reads
    // them and an editor resizes and fills one with 3. Every sample anyone
    // sees is whole: 0 (a resize's new frames), 1, 2 or 3. Under ASan or TSan
    // a freed storage, a torn write or a broken handoff shows up here.
    auto ones = Filled("race_1143_1", 1, STANDARD_BUFFERSIZE * 2, 0.f);
    auto twos = Filled("race_1143_2", 1, STANDARD_BUFFERSIZE * 5, 0.f);
    RecordRig r("race_1143_1 1 0 1", YSE::OBJ::D_SIG, "1");
    r.Switch(1);
    const std::string toOnes = "set race_1143_1";
    const std::string toTwos = "set race_1143_2";

    std::atomic<bool> stop{false};
    std::atomic<int> bad{0};
    std::atomic<int> reads{0};
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
        ones->setSize(STANDARD_BUFFERSIZE * 2 + static_cast<UInt>(n % 7));
        if (n % 3 == 0) ones->fill(3.f);
        n++;
        std::this_thread::yield();
      }
    });
    std::thread reader([&] {
      YSE::DSP::sampleBufferReader one(ones);
      YSE::DSP::sampleBufferReader two(twos);
      while (!stop.load()) {
        for (YSE::DSP::sampleBufferReader* rd : {&one, &two}) {
          YSE::DSP::sampleView view = rd->pin();
          for (UInt i = 0; i < view.frames(); ++i) {
            const float v = view.at(0, i);
            if (!(v == 0.f || v == 1.f || v == 2.f || v == 3.f)) bad.fetch_add(1);
          }
          reads.fetch_add(1);
        }
      }
    });

    int switches = 0;
    const sampleBuffer* last = r.record->Buffer().get();
    for (int block = 0; block < 40000 && (block < 5000 || switches == 0 || reads.load() < 10);
         ++block) {
      // The value follows the buffer the block will record into, roughly.
      r.source->SetFloatData(0, r.record->Buffer().get() == twos.get() ? 2.f : 1.f);
      r.p.Calculate(YSE::T_DSP);
      for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
        const float v = Out(r.p, i);
        if (!(v >= 0.f && v < 1.f)) bad.fetch_add(1); // the sync stays in 0 .. 1
      }
      const sampleBuffer* now = r.record->Buffer().get();
      if (now != last) switches++;
      last = now;
    }
    stop.store(true);
    setter.join();
    editor.join();
    reader.join();
    CHECK(bad.load() == 0);
    CHECK(switches > 0);
    CHECK(reads.load() > 0);
    CHECK(r.record->Recorder().recording()); // loop on: never stopped
  }

} // TEST_SUITE

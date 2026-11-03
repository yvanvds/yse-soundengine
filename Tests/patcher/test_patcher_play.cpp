// Tests for the ~play patcher object (patcher/sampling/dPlay), issue #1140.
// The playback itself is DSP::bufferPlayer's (Tests/dsp/test_buffer_player.cpp)
// and the reader is DSP::sampleBufferReader's (test_sampleBuffer.cpp). These
// show that pins, messages and arguments reach them, in rendered patches
// heard through a ~dac: a message box starting a ~buffer's playback with its
// done bang counted by a .counter, the switch, pause / resume / stop, a
// position signal from ~count, loop and loopinterp by word and argument,
// several channels, set (#1256), a round trip, allocation-free blocks,
// messages and flushes on the audio thread, and playback while other threads
// edit and rebind. The buffers hold a ramp (sample n is n) at 1000 Hz and the
// engine runs at 1000 Hz, so a millisecond is a frame and a sample.

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
#include "patcher/sampling/dPlay.h"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::STANDARD_BUFFERSIZE;
using YSE::DSP::sampleBuffer;
using YSE::DSP::sampleBufferRegistry;
using YSE::PATCHER::dPlay;
using YSE::PATCHER::patcherImplementation;

namespace {

  constexpr float RATE = 1000.f; // a frame per millisecond
  constexpr UInt N = STANDARD_BUFFERSIZE;

  // A named buffer of `frames` frames at 1000 Hz: channel c's sample n is
  // n + 1000 c.
  std::shared_ptr<sampleBuffer> Ramp(const std::string& name, UInt channels, UInt frames) {
    std::shared_ptr<sampleBuffer> b = sampleBufferRegistry::acquire(name);
    b->setSize(frames, static_cast<Int>(channels));
    b->setSampleRate(RATE);
    std::vector<float> values(frames);
    for (UInt c = 0; c < channels; ++c) {
      for (UInt n = 0; n < frames; ++n)
        values[n] = static_cast<float>(n) + 1000.f * static_cast<float>(c);
      b->setSamples(c, 0, values.data(), frames);
    }
    return b;
  }

  int GuiInt(YSE::pHandle* h) {
    return std::stoi(h->GetGuiValue());
  }

  // [source] -> [~play args] -> [~dac], with the done outlet into a
  // .counter; `source` made from `type` and `sourceArgs`, or none when
  // `type` is empty.
  struct PlayRig {
    ScopedSampleRate rate{1000};
    patcherImplementation p{2, nullptr};
    YSE::pHandle* source = nullptr;
    YSE::pHandle* handle = nullptr;
    YSE::pHandle* dac = nullptr;
    YSE::pHandle* counter = nullptr;
    dPlay* play = nullptr;

    PlayRig(const std::string& args, const char* type = "", const std::string& sourceArgs = "") {
      handle = p.CreateObject(YSE::OBJ::D_PLAY, args);
      REQUIRE(handle != nullptr);
      dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      counter = p.CreateObject(YSE::OBJ::G_COUNTER, "");
      REQUIRE(dac != nullptr);
      REQUIRE(counter != nullptr);
      if (type != nullptr && *type != '\0') {
        source = p.CreateObject(type, sourceArgs);
        REQUIRE(source != nullptr);
        p.Connect(source, 0, handle, 0);
      }
      play = static_cast<dPlay*>(p.GetObjectFromID(handle->GetID()));
      p.Connect(handle, 0, dac, 0);
      if (play->Channels() > 1) p.Connect(handle, 1, dac, 1);
      p.Connect(handle, static_cast<unsigned int>(play->Channels()), counter, 0);
    }

    void Message(const std::string& text, YSE::THREAD thread = YSE::T_GUI) {
      play->GetInlet(0)->SetMessage(text, thread);
    }

    void Switch(int on, YSE::THREAD thread = YSE::T_GUI) {
      play->GetInlet(0)->SetInt(on, thread);
    }

    float Out(UInt i, UInt channel = 0) {
      return p.output[channel].getPtr()[i];
    }

    bool Silent(UInt from = 0) {
      for (UInt i = from; i < N; ++i)
        if (Out(i) != 0.f) return false;
      return true;
    }

    int Dones() {
      return GuiInt(counter);
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

  TEST_CASE("~play: registry and shape (#1140)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_PLAY, "shape_1140");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~play");
    CHECK(h->GetInputs() == 1);
    REQUIRE(h->GetOutputs() == 2); // the signal, done
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::BANG);

    // The channel count sets the signal outlets; done stays rightmost.
    YSE::pHandle* three = p.CreateObject(YSE::OBJ::D_PLAY, "shape_1140 3");
    REQUIRE(three != nullptr);
    REQUIRE(three->GetOutputs() == 4);
    CHECK(three->OutputDataType(2) == YSE::OUT_TYPE::BUFFER);
    CHECK(three->OutputDataType(3) == YSE::OUT_TYPE::BANG);

    bool listed = false;
    for (const auto& name : YSE::PATCHER::Register().AllNames())
      if (name == YSE::OBJ::D_PLAY) listed = true;
    CHECK(listed);

    dPlay standalone;
    CHECK(standalone.GetCategory() == YSE::PATCHER::pCategory::SAMPLING);
    CHECK(standalone.Channels() == 1);
    CHECK(standalone.Buffer() == nullptr);
    CHECK(standalone.GetInlet(0)->AcceptsDSP());
    standalone.SetParams("shape_1140 0");
    CHECK(standalone.Channels() == 1);
    CHECK(standalone.Player().channels() == 1);
    standalone.SetParams("shape_1140 1000");
    CHECK(standalone.Channels() == 64);
    CHECK(standalone.Player().channels() == 64);
    CHECK(standalone.GetOutputType(64) == YSE::OUT_TYPE::BANG);
  }

  TEST_CASE("~play: a message box plays a ~buffer, and done bangs in the block it ends "
            "(#1140, #1037)") {
    // The patch a user builds: [start 0 300 150( -> [~play tape] -> [~dac],
    // the done outlet into a [.counter].
    ScopedSampleRate rate(1000);
    patcherImplementation p(1, nullptr);
    YSE::pHandle* buffer = p.CreateObject(YSE::OBJ::D_BUFFER, "tape_1140");
    YSE::pHandle* box = p.CreateObject(YSE::OBJ::G_MESSAGE, "start 0 300 150");
    YSE::pHandle* play = p.CreateObject(YSE::OBJ::D_PLAY, "tape_1140");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* count = p.CreateObject(YSE::OBJ::G_COUNTER, "");
    REQUIRE(buffer != nullptr);
    REQUIRE(box != nullptr);
    REQUIRE(play != nullptr);
    REQUIRE(count != nullptr);
    p.Connect(box, 0, play, 0);
    p.Connect(play, 0, dac, 0);
    p.Connect(play, 1, count, 0);
    auto tape = Ramp("tape_1140", 1, 1000);
    REQUIRE(sampleBufferRegistry::find("tape_1140") == tape);

    p.Calculate(YSE::T_DSP);
    CHECK(p.output[0].getPtr()[5] == 0.f); // not started
    CHECK(GuiInt(count) == 0);

    box->SetBang(0);
    // 300 ms of buffer over 150 ms: every second sample, 150 samples.
    p.Calculate(YSE::T_DSP);
    for (UInt i = 0; i < N; ++i) {
      CAPTURE(i);
      CHECK(p.output[0].getPtr()[i] == static_cast<float>(2 * i));
    }
    CHECK(GuiInt(count) == 0);
    p.Calculate(YSE::T_DSP);
    CHECK(p.output[0].getPtr()[21] == 298.f);
    CHECK(p.output[0].getPtr()[22] == 0.f);
    CHECK(GuiInt(count) == 1); // sent after the block it ended in
    p.Calculate(YSE::T_DSP);
    CHECK(GuiInt(count) == 1); // once
    CHECK(p.output[0].getPtr()[0] == 0.f);

    // Again: another bang.
    box->SetBang(0);
    p.Calculate(YSE::T_DSP);
    p.Calculate(YSE::T_DSP);
    CHECK(GuiInt(count) == 2);
  }

  TEST_CASE("~play: numbers switch, the words drive playback, the last request in a block "
            "wins (#1140)") {
    auto b = Ramp("switch_1140", 1, 1000);
    PlayRig r("switch_1140");
    r.play->GetInlet(0)->SetFloat(0.25f, YSE::T_GUI); // not a position: plays
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Out(7) == 7.f);
    r.Switch(0); // stop: not an end
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Silent());
    CHECK(r.Dones() == 0);
    CHECK_FALSE(r.play->Player().playing());

    // start <start> <end>: backward from 50 down to just above 40.
    r.Message("start 50 40");
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Out(0) == 50.f);
    CHECK(r.Out(9) == 41.f);
    CHECK(r.Silent(10));
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Dones() == 1);

    // pause holds the position, resume goes on, stop ends without a bang.
    r.Message("start 100");
    r.p.Calculate(YSE::T_DSP);
    r.Message("pause");
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Silent());
    CHECK(r.play->Player().paused());
    r.Message("resume");
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Out(0) == static_cast<float>(100 + N));
    r.Message("stop");
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Silent());
    CHECK(r.Dones() == 1);

    // A start then a stop before the block: stopped. A stop then a start:
    // playing, from the start's point.
    r.Message("start 10");
    r.Switch(0);
    r.p.Calculate(YSE::T_DSP);
    CHECK_FALSE(r.play->Player().playing());
    r.Message("stop");
    r.Message("start 20 30");
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Out(0) == 20.f);
    CHECK(r.Silent(10));
    // A malformed word changes nothing; a number after it ends the numbers.
    r.Message("start banana");
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Out(0) == 0.f); // start with no numbers: the whole buffer
    r.Message("loop");
    r.Message("interptime banana");
    r.Message("frobnicate 1");
    r.p.Calculate(YSE::T_DSP);
    CHECK_FALSE(r.play->Player().loop());
    CHECK(r.Out(0) == static_cast<float>(N));
  }

  TEST_CASE("~play: a connected signal is the position to play (#1140)") {
    auto b = Ramp("follow_1140", 1, 1000);
    PlayRig r("follow_1140", YSE::OBJ::D_COUNT, "0 0 1");
    r.p.Calculate(YSE::T_DSP);
    for (UInt i = 0; i < N; ++i) {
      CAPTURE(i);
      CHECK(r.Out(i) == static_cast<float>(i));
    }
    // A start changes nothing while a signal plays it; no done bang.
    r.Message("start 900 901");
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Out(3) == static_cast<float>(N + 3));
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Dones() == 0);

    // Positions past the end hold the last sample.
    for (int block = 0; block < 6; ++block)
      r.p.Calculate(YSE::T_DSP);
    CHECK(r.Out(0) == 999.f);
  }

  TEST_CASE("~play: loop, loopinterp and interptime by word and by argument (#1140)") {
    auto b = Ramp("loop_1140", 1, 1000);
    PlayRig r("loop_1140");
    r.Message("loop 1");
    r.Message("start 100 110");
    for (int block = 0; block < 3; ++block)
      r.p.Calculate(YSE::T_DSP);
    CHECK(r.Out(5) == static_cast<float>(100 + (2 * N + 5) % 10));
    CHECK(r.Dones() == 0); // a loop never ends

    r.Message("loopinterp 1");
    r.Message("interptime 4");
    r.Message("start 100 110");
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.play->Player().loopInterp());
    CHECK(r.play->Player().interpTime() == 4.f);
    CHECK(r.Out(6) == 106.f);
    CHECK(r.Out(8) == doctest::Approx(103.f)); // 108 halfway to 98
    CHECK(r.Out(10) == 100.f);
    r.Message("interptime -5");
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.play->Player().interpTime() == 0.f);

    // loop 0: ends at the next end point, with a bang.
    r.Message("loop 0");
    r.p.Calculate(YSE::T_DSP);
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Dones() == 1);

    // The arguments: name channels loop loopinterp interptime.
    auto c = Ramp("args_1140", 1, 1000);
    PlayRig a("args_1140 1 1 1 4");
    CHECK_FALSE(a.play->Player().loop()); // passed on at the next block
    a.Message("start 100 110");
    a.p.Calculate(YSE::T_DSP);
    CHECK(a.play->Player().loop());
    CHECK(a.play->Player().loopInterp());
    CHECK(a.play->Player().interpTime() == 4.f);
    CHECK(a.Out(8) == doctest::Approx(103.f));
    CHECK(a.Out(18) == doctest::Approx(103.f));
    // interptime defaults to 50.
    PlayRig d("args_1140");
    d.p.Calculate(YSE::T_DSP);
    CHECK(d.play->Player().interpTime() == 50.f);
  }

  TEST_CASE("~play: several channels, one outlet each; done is the rightmost (#1140)") {
    auto b = Ramp("multi_1140", 3, 1000);
    PlayRig r("multi_1140 2");
    REQUIRE(r.handle->GetOutputs() == 3);
    r.Message("start 0 40");
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Out(5, 0) == 5.f + 2005.f); // buffer channel 3 mixed into outlet 1
    CHECK(r.Out(5, 1) == 1005.f);
    CHECK(r.Out(40, 1) == 0.f);
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Dones() == 1);
  }

  TEST_CASE("~play: no name, or a buffer sized later (#1140)") {
    {
      PlayRig r("");
      r.Switch(1);
      r.p.Calculate(YSE::T_DSP);
      CHECK(r.Silent());
      CHECK(r.Dones() == 0);
    }
    std::shared_ptr<sampleBuffer> later = sampleBufferRegistry::acquire("later_1140");
    PlayRig r("later_1140");
    r.Message("start 3");
    r.p.Calculate(YSE::T_DSP); // acquired, but empty: waits
    CHECK(r.Silent());
    CHECK(r.play->Player().playing());
    auto b = Ramp("later_1140", 1, 256);
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Out(0) == 3.f);
  }

  TEST_CASE("~play: set plays another buffer from a later block (#1140)") {
    auto a = Ramp("set_1140_a", 1, 1000);
    auto b = Ramp("set_1140_b", 2, 1000);
    PlayRig r("set_1140_a");
    r.Switch(1);
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Out(1) == 1.f);

    r.Message("set set_1140_b");
    r.Settle();
    CHECK(r.play->Buffer() == b);
    CHECK_FALSE(r.play->ReleasePending());
    r.p.Calculate(YSE::T_DSP);
    // Playback goes on in b from where it had got to; its second channel
    // is mixed into the one outlet.
    CHECK(r.Out(0) == static_cast<float>(2 * N) * 2.f + 1000.f);

    // set with no name: silence.
    r.Message("set");
    r.Settle();
    CHECK(r.play->Buffer() == nullptr);
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Silent());
    CHECK(r.p.FileIO()->Dropped() == 0);

    // Outside a patcher a set does nothing.
    dPlay standalone;
    standalone.SetParams("set_1140_a");
    standalone.GetInlet(0)->SetMessage("set set_1140_b", YSE::T_GUI);
    CHECK(standalone.Buffer() == a);
  }

  TEST_CASE("~play: arguments survive a DumpJSON / ParseJSON round trip; messages are not "
            "saved (#1140)") {
    ScopedSampleRate rate(1000);
    auto a = Ramp("json_1140_a", 2, 1000);
    auto b = Ramp("json_1140_b", 2, 1000);
    std::string dump;
    {
      PlayRig r("json_1140_a 2 1 1 4");
      dump = r.p.DumpJSON();
      r.Message("set json_1140_b");
      r.Message("loop 0");
      r.Message("loopinterp 0");
      r.Message("interptime 9");
      r.Message("start 5");
      r.Settle();
      CHECK(r.play->Buffer() == b);
      CHECK(r.p.DumpJSON() == dump);
    }
    CHECK(dump.find("json_1140_a 2 1 1 4") != std::string::npos);
    CHECK(dump.find("json_1140_b") == std::string::npos);
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
    dPlay* object = nullptr;
    for (unsigned int i = 0; i < dst.Objects(); ++i) {
      YSE::pHandle* h = dst.GetHandleFromList(i);
      if (h != nullptr && std::string(h->Type()) == YSE::OBJ::D_PLAY)
        object = static_cast<dPlay*>(dst.GetObjectFromID(h->GetID()));
    }
    REQUIRE(object != nullptr);
    CHECK(object->Channels() == 2);
    CHECK(object->Buffer() == a);
    object->GetInlet(0)->SetMessage("start 100 110", YSE::T_GUI);
    dst.Calculate(YSE::T_DSP);
    CHECK(object->Player().loop());
    CHECK(object->Player().loopInterp());
    CHECK(object->Player().interpTime() == 4.f);
    CHECK(object->Player().playing());
    // The two buffer channels on the two ~dac channels.
    CHECK(dst.output[0].getPtr()[3] == 103.f);
    CHECK(dst.output[1].getPtr()[3] == 1103.f);
  }

  TEST_CASE("~play: blocks, numbers, words, set and the done flush on the audio thread "
            "allocate nothing (#1140)") {
    auto a = Ramp("rt_1140_a", 2, 1000);
    auto b = Ramp("rt_1140_b", 2, 300);
    PlayRig r("rt_1140_a 2");
    r.Settle();
    // Built before the probe: the strings themselves allocate.
    const std::string toB = "set rt_1140_b";
    const std::string toA = "set rt_1140_a";
    const std::vector<std::string> words = {
        "loop 1", "loopinterp 1", "interptime 5", "start 10 200 50",
        "pause",  "resume",       "loop 0",       "start 250 0",
        "stop",   "start"};
    int dones = 0;
    for (const std::string* m : {&toB, &toA, &toB}) {
      CAPTURE(*m);
      {
        // Lock-free too: the request is a CAS into the scheduler's table.
        TestHelpers::ProbeScope probe;
        r.Message(*m, YSE::T_DSP);
        for (const std::string& w : words)
          r.Message(w, YSE::T_DSP);
        r.Switch(1, YSE::T_DSP);
        r.play->GetInlet(0)->SetFloat(1.f, YSE::T_DSP);
        CHECK(TestHelpers::g_alloc_count.load() == 0);
      }
      r.p.FileIO()->WaitIdle();
      {
        // The blocks that take the new reader, play to the end and flush the
        // done bang into the .counter.
        TestHelpers::ProbeScope probe;
        for (int block = 0; block < 4; ++block)
          r.p.Calculate(YSE::T_DSP);
        r.Message("start 0 20", YSE::T_DSP);
        r.p.Calculate(YSE::T_DSP);
        CHECK(TestHelpers::g_alloc_count.load() == 0);
      }
      r.p.FileIO()->WaitIdle();
      CHECK_FALSE(r.play->ReleasePending());
      r.p.Calculate(YSE::T_DSP); // hands the slots back
      CHECK(r.Dones() > dones);
      dones = r.Dones();
    }
    CHECK(r.play->Buffer() == b);
    CHECK(r.p.FileIO()->Dropped() == 0);
  }

  TEST_CASE("~play plays while other threads edit and set its buffers (#1140)") {
    // ~play loops two buffers of different lengths, switched from a control
    // thread, while an editor resizes one and fills it with 3 and the other
    // with 2. Every sample heard comes from what the buffers held: 0 (a
    // resize's new frames), 1, 2 or 3, through the cubic kernel (which
    // overshoots a step a little) and the loop crossfade. Under ASan or TSan
    // a freed storage or a broken handoff shows up here.
    auto ones = sampleBufferRegistry::acquire("race_1140_1");
    ones->setSize(N * 2);
    ones->setSampleRate(RATE);
    ones->fill(1.f);
    auto twos = sampleBufferRegistry::acquire("race_1140_2");
    twos->setSize(N * 5);
    twos->setSampleRate(RATE);
    twos->fill(2.f);
    PlayRig r("race_1140_1 1 1 1 10");
    r.Switch(1);
    const std::string toOnes = "set race_1140_1";
    const std::string toTwos = "set race_1140_2";

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
        ones->setSize(N * 2 + static_cast<UInt>(n % 7));
        if (n % 3 == 0) ones->fill(3.f);
        if (n % 3 == 1) ones->fill(1.f);
        n++;
        std::this_thread::yield();
      }
    });

    int bad = 0;
    int switches = 0;
    const sampleBuffer* last = r.play->Buffer().get();
    for (int block = 0; block < 40000 && (block < 5000 || switches == 0); ++block) {
      r.p.Calculate(YSE::T_DSP);
      for (UInt i = 0; i < N; ++i) {
        // A step from 3 to 0 overshoots by under 0.2 either way.
        const float v = r.Out(i);
        if (!(v >= -0.5f && v <= 3.5f)) bad++;
      }
      const sampleBuffer* now = r.play->Buffer().get();
      if (now != last) switches++;
      last = now;
    }
    stop.store(true);
    setter.join();
    editor.join();
    CHECK(bad == 0);
    CHECK(switches > 0);
    CHECK(r.play->Player().playing()); // loop on: never ended
    CHECK(r.Dones() == 0);
  }

} // TEST_SUITE

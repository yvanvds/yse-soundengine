// Tests for the ~groove patcher object (patcher/sampling/dGroove), issue
// #1141. The playback itself is DSP::groovePlayer's
// (Tests/dsp/test_groove_player.cpp) and the reader is
// DSP::sampleBufferReader's (test_sampleBuffer.cpp). These show that pins,
// messages and arguments reach them, in rendered patches heard through a
// ~dac: a ~sig speed playing a ~buffer with the sync outlet on the second
// ~dac channel, numbers as positions, the words, the loop points by inlet,
// signal, word and argument, pendulum and crossfade, several channels, set
// (#1256), a round trip, allocation-free blocks and messages on the audio
// thread, and playback while other threads edit and rebind. The buffers hold
// a ramp (sample n is n) at 1000 Hz and the engine runs at 1000 Hz, so a
// millisecond is a frame and a sample.

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
#include "patcher/sampling/dGroove.h"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::STANDARD_BUFFERSIZE;
using YSE::DSP::GROOVE_LOOP;
using YSE::DSP::sampleBuffer;
using YSE::DSP::sampleBufferRegistry;
using YSE::PATCHER::dGroove;
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

  // [~sig speed] -> [~groove args] -> [~dac]: the first sample outlet on
  // ~dac channel 0, the sync outlet on channel 1 (or, with two channels,
  // the second sample outlet there).
  struct GrooveRig {
    ScopedSampleRate rate{1000};
    patcherImplementation p{2, nullptr};
    YSE::pHandle* speed = nullptr;
    YSE::pHandle* handle = nullptr;
    YSE::pHandle* dac = nullptr;
    dGroove* groove = nullptr;

    explicit GrooveRig(const std::string& args, const char* speedArgs = "1") {
      handle = p.CreateObject(YSE::OBJ::D_GROOVE, args);
      REQUIRE(handle != nullptr);
      dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(dac != nullptr);
      groove = static_cast<dGroove*>(p.GetObjectFromID(handle->GetID()));
      if (speedArgs != nullptr) {
        speed = p.CreateObject(YSE::OBJ::D_SIG, speedArgs);
        REQUIRE(speed != nullptr);
        p.Connect(speed, 0, handle, 0);
      }
      p.Connect(handle, 0, dac, 0);
      p.Connect(handle, groove->Channels() > 1 ? 1 : groove->Channels(), dac, 1);
    }

    void Message(const std::string& text, YSE::THREAD thread = YSE::T_GUI) {
      groove->GetInlet(0)->SetMessage(text, thread);
    }

    void Position(float ms, YSE::THREAD thread = YSE::T_GUI) {
      groove->GetInlet(0)->SetFloat(ms, thread);
    }

    void Speed(float value) {
      speed->SetFloatData(0, value);
    }

    float Out(UInt i, UInt channel = 0) {
      return p.output[channel].getPtr()[i];
    }

    float Sync(UInt i) {
      return Out(i, 1);
    }

    bool Silent(UInt from = 0) {
      for (UInt i = from; i < N; ++i)
        if (Out(i) != 0.f) return false;
      return true;
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

  TEST_CASE("~groove: registry and shape (#1141)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_GROOVE, "shape_1141");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~groove");
    CHECK(h->GetInputs() == 3); // speed, loop start, loop end
    REQUIRE(h->GetOutputs() == 2); // the signal, sync
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::BUFFER);

    // The channel count sets the sample outlets; sync stays rightmost.
    YSE::pHandle* three = p.CreateObject(YSE::OBJ::D_GROOVE, "shape_1141 3");
    REQUIRE(three != nullptr);
    CHECK(three->GetInputs() == 3);
    CHECK(three->GetOutputs() == 4);

    bool listed = false;
    for (const auto& name : YSE::PATCHER::Register().AllNames())
      if (name == YSE::OBJ::D_GROOVE) listed = true;
    CHECK(listed);

    dGroove standalone;
    CHECK(standalone.GetCategory() == YSE::PATCHER::pCategory::SAMPLING);
    CHECK(standalone.Channels() == 1);
    CHECK(standalone.Buffer() == nullptr);
    for (int i = 0; i < 3; ++i)
      CHECK(standalone.GetInlet(i)->AcceptsDSP());
    standalone.SetParams("shape_1141 0");
    CHECK(standalone.Channels() == 1);
    CHECK(standalone.Player().channels() == 1);
    standalone.SetParams("shape_1141 1000");
    CHECK(standalone.Channels() == 64);
    CHECK(standalone.Player().channels() == 64);
    CHECK(standalone.GetOutputType(64) == YSE::OUT_TYPE::BUFFER);
  }

  TEST_CASE("~groove: a ~sig speed plays a ~buffer in a loop set by a message box, with the "
            "sync on the second ~dac channel (#1141)") {
    // The patch a user builds: [~sig 1] -> [~groove tape] -> [~dac], with
    // [setloop 100 110( and [loop 1( into the left inlet.
    ScopedSampleRate rate(1000);
    patcherImplementation p(2, nullptr);
    YSE::pHandle* buffer = p.CreateObject(YSE::OBJ::D_BUFFER, "tape_1141");
    YSE::pHandle* sig = p.CreateObject(YSE::OBJ::D_SIG, "1");
    YSE::pHandle* setloop = p.CreateObject(YSE::OBJ::G_MESSAGE, "setloop 100 110");
    YSE::pHandle* loop = p.CreateObject(YSE::OBJ::G_MESSAGE, "loop 1");
    YSE::pHandle* startloop = p.CreateObject(YSE::OBJ::G_MESSAGE, "startloop");
    YSE::pHandle* groove = p.CreateObject(YSE::OBJ::D_GROOVE, "tape_1141");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(buffer != nullptr);
    REQUIRE(sig != nullptr);
    REQUIRE(setloop != nullptr);
    REQUIRE(loop != nullptr);
    REQUIRE(startloop != nullptr);
    REQUIRE(groove != nullptr);
    p.Connect(sig, 0, groove, 0);
    p.Connect(setloop, 0, groove, 0);
    p.Connect(loop, 0, groove, 0);
    p.Connect(startloop, 0, groove, 0);
    p.Connect(groove, 0, dac, 0);
    p.Connect(groove, 1, dac, 1);
    auto tape = Ramp("tape_1141", 1, 1000);
    REQUIRE(sampleBufferRegistry::find("tape_1141") == tape);

    // Playing from position 0 at once, as Max's groove~ does; the sync
    // ramps over the whole buffer, the default loop.
    p.Calculate(YSE::T_DSP);
    for (UInt i = 0; i < N; ++i) {
      CAPTURE(i);
      CHECK(p.output[0].getPtr()[i] == static_cast<float>(i));
      CHECK(p.output[1].getPtr()[i] == doctest::Approx(static_cast<float>(i) / 1000.f));
    }

    setloop->SetBang(0);
    loop->SetBang(0);
    startloop->SetBang(0);
    p.Calculate(YSE::T_DSP);
    for (UInt i = 0; i < N; ++i) {
      CAPTURE(i);
      CHECK(p.output[0].getPtr()[i] == static_cast<float>(100 + i % 10));
      CHECK(p.output[1].getPtr()[i] == doctest::Approx(static_cast<float>(i % 10) / 10.f));
    }

    // Backward at twice the speed: below the loop start, on from the end.
    sig->SetFloatData(0, -2.f);
    startloop->SetBang(0);
    p.Calculate(YSE::T_DSP);
    CHECK(p.output[0].getPtr()[0] == 100.f);
    CHECK(p.output[0].getPtr()[1] == 108.f);
  }

  TEST_CASE("~groove: numbers are positions; stop, startloop, endloop; the last request in a "
            "block wins (#1141)") {
    auto b = Ramp("words_1141", 1, 1000);
    GrooveRig r("words_1141");
    r.Position(250.f);
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Out(0) == 250.f);
    CHECK(r.Out(7) == 257.f);
    r.groove->GetInlet(0)->SetInt(40, YSE::T_GUI);
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Out(0) == 40.f);

    r.Message("stop");
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Silent());
    CHECK_FALSE(r.groove->Player().playing());
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Silent()); // until the next position
    r.Position(0.f);
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Out(5) == 5.f);

    // startloop and endloop jump to the loop points (no loop: the buffer).
    r.Message("loopstart 300");
    r.Message("loopend 400");
    r.Message("stop");
    r.Message("startloop");
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Out(0) == 300.f);
    r.Speed(-1.f);
    r.Message("endloop");
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Out(0) == 400.f);
    CHECK(r.Out(1) == 399.f);

    // Two requests before a block: the last counts.
    r.Position(10.f);
    r.Message("stop");
    r.p.Calculate(YSE::T_DSP);
    CHECK_FALSE(r.groove->Player().playing());
    r.Message("stop");
    r.Position(600.f);
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Out(0) == 600.f);

    // reset: the loop is the whole buffer again.
    r.Message("reset");
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.groove->Player().loopStart() == 0.f);
    CHECK(r.groove->Player().loopEnd() == 0.f);

    // Malformed words change nothing.
    r.Message("setloop 5");
    r.Message("loop");
    r.Message("interptime banana");
    r.Message("frobnicate 1");
    r.Message("");
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.groove->Player().loopStart() == 0.f);
    CHECK(r.groove->Player().loop() == GROOVE_LOOP::OFF);
    CHECK(r.groove->Player().interpTime() == 50.f);
  }

  TEST_CASE("~groove: an unconnected speed is 0: the position holds (#1141)") {
    auto b = Ramp("still_1141", 1, 1000);
    GrooveRig r("still_1141", nullptr);
    r.Position(77.f);
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Out(0) == 77.f);
    CHECK(r.Out(N - 1) == 77.f);
    CHECK(r.groove->Player().position() == doctest::Approx(77.0));
  }

  TEST_CASE("~groove: loop points by inlet, by signal, by word and by argument (#1141)") {
    auto b = Ramp("points_1141", 1, 1000);
    {
      GrooveRig r("points_1141 1 1");
      r.groove->GetInlet(1)->SetFloat(200.f, YSE::T_GUI);
      r.groove->GetInlet(2)->SetInt(210, YSE::T_GUI);
      r.Message("startloop");
      r.p.Calculate(YSE::T_DSP);
      CHECK(r.Out(9) == 209.f);
      CHECK(r.Out(10) == 200.f);
      CHECK(r.groove->Player().loop() == GROOVE_LOOP::ON);

      r.Message("setloop 500 505");
      r.Message("startloop");
      r.p.Calculate(YSE::T_DSP);
      CHECK(r.Out(5) == 500.f);

      // A connected signal on the loop start wins over its number; once per
      // block.
      YSE::pHandle* start = r.p.CreateObject(YSE::OBJ::D_SIG, "502");
      REQUIRE(start != nullptr);
      r.p.Connect(start, 0, r.handle, 1);
      r.groove->GetInlet(1)->SetFloat(0.f, YSE::T_GUI); // stored, not heard
      r.Message("startloop");
      r.p.Calculate(YSE::T_DSP);
      CHECK(r.groove->Player().loopStart() == 502.f);
      CHECK(r.Out(0) == 502.f);
      CHECK(r.Out(3) == 502.f);
    }
    // The arguments: name channels loop loopinterp interptime loopstart
    // loopend.
    GrooveRig a("points_1141 1 2 0 50 100 110");
    a.p.Calculate(YSE::T_DSP); // passed on at the block
    CHECK(a.groove->Player().loop() == GROOVE_LOOP::PENDULUM);
    CHECK(a.groove->Player().loopStart() == 100.f);
    CHECK(a.groove->Player().loopEnd() == 110.f);
    a.Message("startloop");
    a.p.Calculate(YSE::T_DSP);
    CHECK(a.Out(10) == 110.f); // turned at the end
    CHECK(a.Out(11) == 109.f);
    // A loop mode past 2 is a pendulum, below 1 off.
    a.Message("loop 7");
    a.p.Calculate(YSE::T_DSP);
    CHECK(a.groove->Player().loop() == GROOVE_LOOP::PENDULUM);
    a.Message("loop -1");
    a.p.Calculate(YSE::T_DSP);
    CHECK(a.groove->Player().loop() == GROOVE_LOOP::OFF);
  }

  TEST_CASE("~groove: loopinterp and interptime by word and by argument (#1141)") {
    auto b = Ramp("interp_1141", 1, 1000);
    GrooveRig r("interp_1141 1 1 1 4 100 110");
    r.Message("startloop");
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.groove->Player().loopInterp());
    CHECK(r.groove->Player().interpTime() == 4.f);
    CHECK(r.Out(6) == 106.f);
    CHECK(r.Out(8) == doctest::Approx(103.f)); // 108 halfway to 98
    CHECK(r.Out(10) == 100.f);

    r.Message("loopinterp 0");
    r.Message("startloop");
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Out(8) == 108.f);
    r.Message("loopinterp 1");
    r.Message("interptime 2");
    r.Message("startloop");
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Out(8) == 108.f);
    CHECK(r.Out(9) == doctest::Approx(104.f)); // halfway to 99
    r.Message("interptime -5");
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.groove->Player().interpTime() == 0.f);

    GrooveRig d("interp_1141");
    d.p.Calculate(YSE::T_DSP);
    CHECK(d.groove->Player().interpTime() == 50.f);
    CHECK_FALSE(d.groove->Player().loopInterp());
  }

  TEST_CASE("~groove: several channels, one outlet each; sync is the rightmost (#1141)") {
    auto b = Ramp("multi_1141", 3, 1000);
    GrooveRig r("multi_1141 2 1 0 50 0 100");
    REQUIRE(r.handle->GetOutputs() == 3);
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Out(5, 0) == 5.f + 2005.f); // buffer channel 3 mixed into outlet 1
    CHECK(r.Out(5, 1) == 1005.f);

    // The sync outlet, into a ~dac of its own.
    patcherImplementation q(1, nullptr);
    YSE::pHandle* sig = q.CreateObject(YSE::OBJ::D_SIG, "1");
    YSE::pHandle* g = q.CreateObject(YSE::OBJ::D_GROOVE, "multi_1141 2 1 0 50 0 100");
    YSE::pHandle* dac = q.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(g != nullptr);
    q.Connect(sig, 0, g, 0);
    q.Connect(g, 2, dac, 0);
    q.Calculate(YSE::T_DSP);
    CHECK(q.output[0].getPtr()[50] == doctest::Approx(0.5f));
    CHECK(q.output[0].getPtr()[100] == 0.f); // wrapped: the loop start
  }

  TEST_CASE("~groove: no name, or a buffer sized later (#1141)") {
    {
      GrooveRig r("");
      r.p.Calculate(YSE::T_DSP);
      CHECK(r.Silent());
    }
    std::shared_ptr<sampleBuffer> later = sampleBufferRegistry::acquire("later_1141");
    GrooveRig r("later_1141");
    r.Position(3.f);
    r.p.Calculate(YSE::T_DSP); // acquired, but empty: silence, the position stays
    CHECK(r.Silent());
    CHECK(r.groove->Player().playing());
    auto b = Ramp("later_1141", 1, 256);
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Out(0) == 3.f);
  }

  TEST_CASE("~groove: set plays another buffer from a later block (#1141)") {
    auto a = Ramp("set_1141_a", 1, 1000);
    auto b = Ramp("set_1141_b", 2, 1000);
    GrooveRig r("set_1141_a");
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Out(1) == 1.f);

    r.Message("set set_1141_b");
    r.Settle();
    CHECK(r.groove->Buffer() == b);
    CHECK_FALSE(r.groove->ReleasePending());
    r.p.Calculate(YSE::T_DSP);
    // Playback goes on in b from where it had got to; its second channel
    // is mixed into the one outlet.
    CHECK(r.Out(0) == static_cast<float>(2 * N) * 2.f + 1000.f);

    // set with no name: silence.
    r.Message("set");
    r.Settle();
    CHECK(r.groove->Buffer() == nullptr);
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Silent());
    CHECK(r.p.FileIO()->Dropped() == 0);

    // Outside a patcher a set does nothing.
    dGroove standalone;
    standalone.SetParams("set_1141_a");
    standalone.GetInlet(0)->SetMessage("set set_1141_b", YSE::T_GUI);
    CHECK(standalone.Buffer() == a);
  }

  TEST_CASE("~groove: arguments survive a DumpJSON / ParseJSON round trip; messages are not "
            "saved (#1141)") {
    ScopedSampleRate rate(1000);
    auto a = Ramp("json_1141_a", 2, 1000);
    auto b = Ramp("json_1141_b", 2, 1000);
    std::string dump;
    {
      GrooveRig r("json_1141_a 2 1 1 4 100 110");
      dump = r.p.DumpJSON();
      r.Message("set json_1141_b");
      r.Message("loop 0");
      r.Message("loopinterp 0");
      r.Message("interptime 9");
      r.Message("setloop 5 6");
      r.groove->GetInlet(1)->SetFloat(7.f, YSE::T_GUI);
      r.Position(5.f);
      r.Settle();
      CHECK(r.groove->Buffer() == b);
      CHECK(r.p.DumpJSON() == dump);
    }
    CHECK(dump.find("json_1141_a 2 1 1 4 100 110") != std::string::npos);
    CHECK(dump.find("json_1141_b") == std::string::npos);
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
    dGroove* object = nullptr;
    for (unsigned int i = 0; i < dst.Objects(); ++i) {
      YSE::pHandle* h = dst.GetHandleFromList(i);
      if (h != nullptr && std::string(h->Type()) == YSE::OBJ::D_GROOVE)
        object = static_cast<dGroove*>(dst.GetObjectFromID(h->GetID()));
    }
    REQUIRE(object != nullptr);
    CHECK(object->Channels() == 2);
    CHECK(object->Buffer() == a);
    dst.Calculate(YSE::T_DSP);
    CHECK(object->Player().loop() == GROOVE_LOOP::ON);
    CHECK(object->Player().loopInterp());
    CHECK(object->Player().interpTime() == 4.f);
    CHECK(object->Player().loopStart() == 100.f);
    CHECK(object->Player().loopEnd() == 110.f);
  }

  TEST_CASE("~groove: blocks, numbers, words and set on the audio thread allocate nothing "
            "(#1141)") {
    auto a = Ramp("rt_1141_a", 2, 1000);
    auto b = Ramp("rt_1141_b", 2, 300);
    GrooveRig r("rt_1141_a 2");
    r.Settle();
    // Built before the probe: the strings themselves allocate.
    const std::string toB = "set rt_1141_b";
    const std::string toA = "set rt_1141_a";
    const std::vector<std::string> words = {
        "loop 1", "loopinterp 1", "interptime 5", "setloop 10 200", "startloop", "endloop",
        "loop 2", "loopstart 20", "loopend 250",  "reset",          "loop 0",    "stop"};
    for (const std::string* m : {&toB, &toA, &toB}) {
      CAPTURE(*m);
      {
        // Lock-free too: the request is a CAS into the scheduler's table.
        TestHelpers::ProbeScope probe;
        r.Message(*m, YSE::T_DSP);
        for (const std::string& w : words)
          r.Message(w, YSE::T_DSP);
        r.Position(12.f, YSE::T_DSP);
        r.groove->GetInlet(0)->SetInt(3, YSE::T_DSP);
        r.groove->GetInlet(1)->SetFloat(5.f, YSE::T_DSP);
        r.groove->GetInlet(2)->SetInt(90, YSE::T_DSP);
        CHECK(TestHelpers::g_alloc_count.load() == 0);
      }
      r.p.FileIO()->WaitIdle();
      {
        // The blocks that take the new reader and play, loop and turn.
        TestHelpers::ProbeScope probe;
        for (int block = 0; block < 4; ++block)
          r.p.Calculate(YSE::T_DSP);
        r.Message("loop 2", YSE::T_DSP);
        r.Message("startloop", YSE::T_DSP);
        r.p.Calculate(YSE::T_DSP);
        CHECK(TestHelpers::g_alloc_count.load() == 0);
      }
      r.p.FileIO()->WaitIdle();
      CHECK_FALSE(r.groove->ReleasePending());
      r.p.Calculate(YSE::T_DSP); // hands the slots back
    }
    CHECK(r.groove->Buffer() == b);
    CHECK(r.p.FileIO()->Dropped() == 0);
  }

  TEST_CASE("~groove plays while other threads edit and set its buffers (#1141)") {
    // ~groove loops two buffers of different lengths at 1.5 times the speed,
    // switched from a control thread, while an editor resizes one and fills
    // it with 3 and the other with 2, and moves the loop points. Every
    // sample heard comes from what the buffers held: 0 (a resize's new
    // frames), 1, 2 or 3, through the cubic kernel (which overshoots a step
    // a little) and the loop crossfade. Under ASan or TSan a freed storage or
    // a broken handoff shows up here.
    auto ones = sampleBufferRegistry::acquire("race_1141_1");
    ones->setSize(N * 2);
    ones->setSampleRate(RATE);
    ones->fill(1.f);
    auto twos = sampleBufferRegistry::acquire("race_1141_2");
    twos->setSize(N * 5);
    twos->setSampleRate(RATE);
    twos->fill(2.f);
    GrooveRig r("race_1141_1 1 1 1 10", "1.5");
    const std::string toOnes = "set race_1141_1";
    const std::string toTwos = "set race_1141_2";

    std::atomic<bool> stop{false};
    std::thread setter([&] {
      int n = 0;
      while (!stop.load()) {
        r.Message((n % 2) != 0 ? toOnes : toTwos, YSE::T_GUI);
        r.groove->GetInlet(1)->SetFloat(static_cast<float>(n % 50), YSE::T_GUI);
        r.groove->GetInlet(2)->SetFloat(static_cast<float>(100 + n % 300), YSE::T_GUI);
        n++;
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
    const sampleBuffer* last = r.groove->Buffer().get();
    for (int block = 0; block < 40000 && (block < 5000 || switches == 0); ++block) {
      r.p.Calculate(YSE::T_DSP);
      for (UInt i = 0; i < N; ++i) {
        // A step from 3 to 0 overshoots by under 0.2 either way.
        const float v = r.Out(i);
        if (!(v >= -0.5f && v <= 3.5f)) bad++;
        const float s = r.Sync(i);
        if (!(s >= 0.f && s <= 1.f)) bad++;
      }
      const sampleBuffer* now = r.groove->Buffer().get();
      if (now != last) switches++;
      last = now;
    }
    stop.store(true);
    setter.join();
    editor.join();
    CHECK(bad == 0);
    CHECK(switches > 0);
    CHECK(r.groove->Player().playing()); // loop on: never ran off
  }

} // TEST_SUITE

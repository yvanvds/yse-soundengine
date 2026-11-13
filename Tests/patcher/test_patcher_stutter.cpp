// Tests for the ~stutter patcher object (patcher/sampling/dStutter), issue
// #1150. The sample math is DSP::stutter's (Tests/dsp/test_stutter.cpp).
// These show that pins, messages and arguments reach it, in rendered
// patches heard through a ~dac: a ~count recorded and a ~click's trigger
// replayed by a ~phasor, the number fallbacks and a signal winning, the
// size, maxsize, polarity, dropout, repeat, ampvar and clear messages,
// several outputs, a copy rate, setbuf into a ~buffer's buffer, a round
// trip, allocation-free blocks and messages on the audio thread, and
// messages sent while it renders.

#include <doctest/doctest.h>
#include <algorithm>
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
#include "patcher/sampling/dStutter.h"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

using YSE::STANDARD_BUFFERSIZE;
using YSE::DSP::sampleBuffer;
using YSE::DSP::sampleBufferRegistry;
using YSE::PATCHER::dStutter;
using YSE::PATCHER::patcherImplementation;

namespace {

  float Out(patcherImplementation& p, UInt i, UInt channel = 0) {
    return p.output[channel].getPtr()[i];
  }

  // [~count 0 0 1] -> [~stutter args] -> [~dac], the count heard on the
  // last ~dac channel too: input sample n (from the first block) is n. The
  // stutter's outlets go to the first channels.
  struct StutterRig {
    patcherImplementation p;
    YSE::pHandle* count = nullptr;
    YSE::pHandle* handle = nullptr;
    YSE::pHandle* dac = nullptr;
    dStutter* st = nullptr;
    UInt countChannel = 0;

    explicit StutterRig(const std::string& args, UInt outlets = 1, bool record = true)
      : p(outlets + 1, nullptr), countChannel(outlets) {
      handle = p.CreateObject(YSE::OBJ::D_STUTTER, args);
      REQUIRE(handle != nullptr);
      dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(dac != nullptr);
      count = p.CreateObject(YSE::OBJ::D_COUNT, "0 0 1");
      REQUIRE(count != nullptr);
      if (record) p.Connect(count, 0, handle, 0);
      p.Connect(count, 0, dac, static_cast<int>(outlets));
      for (UInt k = 0; k < outlets; ++k)
        p.Connect(handle, static_cast<int>(k), dac, static_cast<int>(k));
      st = static_cast<dStutter*>(p.GetObjectFromID(handle->GetID()));
    }

    void Message(const std::string& text, YSE::THREAD thread = YSE::T_GUI) {
      st->GetInlet(0)->SetMessage(text, thread);
    }

    // The next block; what output `k` gives at sample `i`.
    float Block(UInt i = STANDARD_BUFFERSIZE - 1, UInt k = 0) {
      p.Calculate(YSE::T_DSP);
      return Out(p, i, k);
    }

    // The count recorded at sample `i` of the last block.
    float Count(UInt i = 0) {
      return Out(p, i, countChannel);
    }

    // The setbuf's bind task runs on the background pool, the next block
    // takes the writer, and the task that frees the old one runs after it.
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

  TEST_CASE("~stutter: registry and shape, the phases and outputs from the arguments "
            "(#1150)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_STUTTER, "");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~stutter");
    CHECK(h->GetInputs() == 3);
    REQUIRE(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    YSE::pHandle* four = p.CreateObject(YSE::OBJ::D_STUTTER, "1000 10 1 0 4");
    REQUIRE(four != nullptr);
    CHECK(four->GetInputs() == 6);
    CHECK(four->GetOutputs() == 4);

    bool listed = false;
    for (const auto& name : YSE::PATCHER::Register().AllNames())
      if (name == YSE::OBJ::D_STUTTER) listed = true;
    CHECK(listed);

    dStutter standalone;
    CHECK(standalone.GetCategory() == YSE::PATCHER::pCategory::SAMPLING);
    CHECK(standalone.Stutter().capacity() == YSE::DSP::stutter::DEFAULT_CAPACITY);
    CHECK(standalone.CurrentSize() == static_cast<int>(YSE::DSP::stutter::DEFAULT_SIZE));
    CHECK(standalone.Stutter().polarity());
    CHECK(standalone.Stutter().outputs() == 1);
    standalone.SetParams("500 20 0 7 40");
    CHECK(standalone.Stutter().capacity() == 500);
    CHECK(standalone.CurrentMaxSize() == 500);
    CHECK(standalone.CurrentSize() == 20);
    CHECK(standalone.Stutter().outputs() == YSE::DSP::stutter::MAX_OUTPUTS);
    CHECK(standalone.NumInputs() == 2 + static_cast<int>(YSE::DSP::stutter::MAX_OUTPUTS));
  }

  TEST_CASE("~stutter: a ~click copies the recorded ~count, a ~phasor replays it (#1150)") {
    // The patch a user builds: [~count] -> [~stutter 4096 64], a ~click on
    // the trigger, a ~phasor at one fragment per cycle on the phase. The
    // phasor is heard on a channel of its own, so the output can be
    // checked against the fragment read at each of its values.
    constexpr UInt L = 64;
    patcherImplementation p(3, nullptr);
    YSE::pHandle* count = p.CreateObject(YSE::OBJ::D_COUNT, "0 0 1");
    YSE::pHandle* click = p.CreateObject(YSE::OBJ::D_CLICK, "");
    YSE::pHandle* phasor =
        p.CreateObject(YSE::OBJ::D_PHASOR, std::to_string(static_cast<float>(YSE::SAMPLERATE) / L));
    YSE::pHandle* st = p.CreateObject(YSE::OBJ::D_STUTTER, "4096 64");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(count != nullptr);
    REQUIRE(click != nullptr);
    REQUIRE(phasor != nullptr);
    REQUIRE(st != nullptr);
    REQUIRE(dac != nullptr);
    p.Connect(count, 0, st, 0);
    p.Connect(click, 0, st, 1);
    p.Connect(phasor, 0, st, 2);
    p.Connect(st, 0, dac, 0);
    p.Connect(count, 0, dac, 1);
    p.Connect(phasor, 0, dac, 2);

    // Nothing copied yet: silence.
    p.Calculate(YSE::T_DSP);
    p.Calculate(YSE::T_DSP);
    for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i)
      CHECK(Out(p, i) == 0.f);

    p.GetObjectFromID(click->GetID())->GetInlet(0)->SetBang(YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    // The click is the first sample of this block: the fragment is the 64
    // counts that end with it.
    const float last = Out(p, 0, 1);
    REQUIRE(last >= static_cast<float>(L));
    double loudest = 0.0;
    for (int block = 0; block < 4; ++block) {
      if (block > 0) p.Calculate(YSE::T_DSP);
      for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
        const double phase = Out(p, i, 2);
        const double position = (phase - std::floor(phase)) * L;
        const auto i0 = std::min(static_cast<UInt>(position), L - 1);
        const UInt i1 = (i0 + 1) % L;
        const double a = last - (L - 1) + i0;
        const double b = last - (L - 1) + i1;
        const double expected = a + (position - i0) * (b - a);
        CAPTURE(block);
        CAPTURE(i);
        CHECK(Out(p, i) == doctest::Approx(expected).epsilon(1e-4));
        loudest = std::max(loudest, static_cast<double>(Out(p, i)));
      }
    }
    // The count goes on, the output stays within the fragment.
    CHECK(Out(p, 0, 1) == last + static_cast<float>(3 * STANDARD_BUFFERSIZE));
    CHECK(loudest <= last);
  }

  TEST_CASE("~stutter: numbers while no signal is connected; a connected signal wins "
            "(#1150)") {
    StutterRig r("1000 4");
    CHECK(r.Block() == 0.f);
    // A number on the trigger: 0 to 1 is an edge, at the next block's first
    // sample. Phase 0 reads the first of the four.
    r.st->GetInlet(1)->SetFloat(1.f, YSE::T_GUI);
    r.Block();
    const float at = r.Count(0);
    CHECK(Out(r.p, 0) == at - 3.f);
    CHECK(Out(r.p, STANDARD_BUFFERSIZE - 1) == at - 3.f);
    // Held at 1: no new trigger.
    CHECK(r.Block() == at - 3.f);
    // The phase by number: 0.5 is the third sample, an int 1 wraps to 0.
    r.st->GetInlet(2)->SetFloat(0.5f, YSE::T_GUI);
    CHECK(r.Block() == at - 1.f);
    r.st->GetInlet(2)->SetInt(1, YSE::T_GUI);
    CHECK(r.Block() == at - 3.f);
    // 1 -> 0 -> 1 by int: a second trigger.
    r.st->GetInlet(1)->SetInt(0, YSE::T_GUI);
    r.Block();
    r.st->GetInlet(1)->SetInt(1, YSE::T_GUI);
    r.Block();
    CHECK(Out(r.p, 0) == r.Count(0) - 3.f);

    // A ~sig on the phase wins over the number; without it the number is
    // back.
    YSE::pHandle* sig = r.p.CreateObject(YSE::OBJ::D_SIG, "0.75");
    REQUIRE(sig != nullptr);
    r.p.Connect(sig, 0, r.handle, 2);
    const float now = Out(r.p, 0);
    CHECK(r.Block() == now + 3.f);
    r.p.Disconnect(sig, 0, r.handle, 2);
    CHECK(r.Block() == now);
  }

  TEST_CASE("~stutter: bang, size by number and argument, maxsize (#1150)") {
    StutterRig r("100 4");
    CHECK(r.st->CurrentSize() == 4);
    CHECK(r.st->CurrentMaxSize() == 100);
    r.Block();
    r.st->GetInlet(2)->SetFloat(0.99f, YSE::T_GUI); // the last sample, nearly
    // A bang copies at the next block's first sample.
    r.st->GetInlet(0)->SetBang(YSE::T_GUI);
    r.Block();
    CHECK(r.st->Stutter().length() == 4);
    CHECK(r.st->Stutter().fragment(3) == r.Count(0));
    // An int and a float (truncated) set the size of the next copy.
    r.st->GetInlet(0)->SetInt(10, YSE::T_GUI);
    CHECK(r.st->CurrentSize() == 10);
    r.st->GetInlet(0)->SetFloat(20.9f, YSE::T_GUI);
    CHECK(r.st->CurrentSize() == 20);
    CHECK(r.st->Stutter().length() == 4); // not copied yet
    r.st->GetInlet(0)->SetBang(YSE::T_GUI);
    r.Block();
    CHECK(r.st->Stutter().length() == 20);
    CHECK(r.st->Stutter().fragment(0) == r.Count(0) - 19.f);
    // maxsize holds the size; past the record buffer it is held there.
    r.Message("maxsize 8");
    r.st->GetInlet(0)->SetBang(YSE::T_GUI);
    r.Block();
    CHECK(r.st->Stutter().length() == 8);
    r.Message("maxsize 100000");
    r.st->GetInlet(0)->SetInt(500, YSE::T_GUI);
    r.st->GetInlet(0)->SetBang(YSE::T_GUI);
    r.Block();
    CHECK(r.st->Stutter().length() == 100);
    CHECK(r.st->Stutter().fragment(99) == r.Count(0));
    // Not a number, or no number: ignored.
    r.Message("maxsize none");
    r.Message("maxsize");
    CHECK(r.st->CurrentMaxSize() == 100000);
  }

  TEST_CASE("~stutter: polarity by argument and message (#1150)") {
    StutterRig r("1000 1 0");
    CHECK_FALSE(r.st->Stutter().polarity());
    r.Block();
    // A rise does not trigger a negative stutter; a fall below 0 does.
    r.st->GetInlet(1)->SetFloat(1.f, YSE::T_GUI);
    r.Block();
    CHECK(r.st->Stutter().length() == 0);
    r.st->GetInlet(1)->SetFloat(-1.f, YSE::T_GUI);
    r.Block();
    CHECK(r.st->Stutter().fragment(0) == r.Count(0));
    CHECK(r.Block(0) == r.st->Stutter().fragment(0));
    // polarity 1: rises from here.
    r.Message("polarity 1");
    r.st->GetInlet(1)->SetFloat(1.f, YSE::T_GUI);
    r.Block();
    CHECK(r.st->Stutter().fragment(0) == r.Count(0));
    CHECK(r.st->Stutter().polarity());
  }

  TEST_CASE("~stutter: dropout, repeat, ampvar and clear (#1150)") {
    StutterRig r("1000 1");
    r.Block();
    r.st->GetInlet(0)->SetBang(YSE::T_GUI);
    const float first = [&] {
      r.Block();
      return r.Count(0);
    }();
    CHECK(Out(r.p, 5) == first);

    // repeat 100: the old fragment plays on.
    r.Message("repeat 100");
    r.st->GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(r.Block(5) == first);
    r.Message("repeat 0");

    // dropout 100: silence until a trigger that does not drop out.
    r.Message("dropout 100");
    r.st->GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(r.Block(5) == 0.f);
    CHECK(r.st->Stutter().droppedOut());
    r.Message("dropout 0");
    r.st->GetInlet(0)->SetBang(YSE::T_GUI);
    const float back = r.Block(5);
    CHECK(back == r.Count(0));

    // ampvar 1: each trigger at a gain from 0 to 1.
    r.Message("ampvar 1");
    bool quieter = false;
    for (int n = 0; n < 20; ++n) {
      r.st->GetInlet(0)->SetBang(YSE::T_GUI);
      const float heard = r.Block(5);
      const float gain = r.st->Stutter().gain();
      CHECK(gain >= 0.f);
      CHECK(gain <= 1.f);
      CHECK(heard == doctest::Approx(gain * r.Count(0)));
      if (gain < 0.9f) quieter = true;
    }
    CHECK(quieter);
    r.Message("ampvar 0");

    // clear: silence, and the recording before it is gone.
    r.Message("clear");
    CHECK(r.Block() == 0.f);
    CHECK(r.st->Stutter().length() == 0);
  }

  TEST_CASE("~stutter: several outputs, each at its own phase (#1150)") {
    StutterRig r("1000 4 1 0 3", 3);
    REQUIRE(r.st->NumOutputs() == 3);
    r.st->GetInlet(2)->SetFloat(0.f, YSE::T_GUI);
    r.st->GetInlet(3)->SetFloat(0.25f, YSE::T_GUI);
    r.st->GetInlet(4)->SetFloat(0.75f, YSE::T_GUI);
    r.Block();
    r.st->GetInlet(0)->SetBang(YSE::T_GUI);
    r.Block();
    const float at = r.Count(0);
    CHECK(Out(r.p, 7, 0) == at - 3.f);
    CHECK(Out(r.p, 7, 1) == at - 2.f);
    CHECK(Out(r.p, 7, 2) == at);
  }

  TEST_CASE("~stutter: a copy rate by argument spreads the copy over blocks (#1150)") {
    StutterRig r("10000 1000 1 300");
    while (r.Count(0) < 2000.f)
      r.Block();
    r.st->GetInlet(0)->SetBang(YSE::T_GUI);
    r.Block();
    const float at = r.Count(0);
    CHECK(r.st->Stutter().copying());
    CHECK(r.st->Stutter().fragment(299) == at - 700.f);
    CHECK(r.st->Stutter().fragment(300) == 0.f);
    r.Block(); // 600
    r.Block(); // 900
    CHECK(r.st->Stutter().copying());
    r.Block();
    CHECK_FALSE(r.st->Stutter().copying());
    CHECK(r.st->Stutter().fragment(999) == at);
  }

  TEST_CASE("~stutter: setbuf writes the fragment into a ~buffer's buffer (#1150)") {
    StutterRig r("1000 4");
    YSE::pHandle* buf = r.p.CreateObject(YSE::OBJ::D_BUFFER, "setbuf_1150");
    REQUIRE(buf != nullptr);
    std::shared_ptr<sampleBuffer> b = sampleBufferRegistry::acquire("setbuf_1150");
    b->setSize(10, 2);
    r.Block();
    r.st->GetInlet(0)->SetBang(YSE::T_GUI);
    r.Block();
    const float at = r.Count(0);

    // Channel 2 from frame 3.
    r.Message("setbuf setbuf_1150 3 2");
    r.Settle();
    CHECK(r.st->SetbufBuffer() == b);
    std::vector<float> got(10);
    b->getSamples(1, 0, got.data(), 10);
    CHECK(got ==
          std::vector<float>{0.f, 0.f, 0.f, at - 3.f, at - 2.f, at - 1.f, at, 0.f, 0.f, 0.f});
    b->getSamples(0, 0, got.data(), 10);
    CHECK(got == std::vector<float>(10, 0.f));

    // The defaults: frame 0 of channel 1; cut at the buffer's end.
    r.Message("setbuf setbuf_1150");
    r.Settle();
    b->getSamples(0, 0, got.data(), 4);
    CHECK(got[0] == at - 3.f);
    CHECK(got[3] == at);
    r.Message("setbuf setbuf_1150 8");
    r.Settle();
    b->getSamples(0, 8, got.data(), 2);
    CHECK(got[0] == at - 3.f);
    CHECK(got[1] == at - 2.f);
    // No name: nothing.
    r.Message("setbuf");
    r.Settle();
    CHECK(r.st->SetbufBuffer() == b);
    CHECK(r.p.FileIO()->Dropped() == 0);

    // Outside a patcher a setbuf does nothing.
    dStutter standalone;
    standalone.GetInlet(0)->SetMessage("setbuf setbuf_1150", YSE::T_GUI);
    CHECK(standalone.SetbufBuffer() == nullptr);
  }

  TEST_CASE("~stutter: arguments survive a DumpJSON / ParseJSON round trip; messages are "
            "not saved (#1150)") {
    std::string dump;
    {
      StutterRig r("2000 16 0 32 2", 2);
      dump = r.p.DumpJSON();
      CHECK(dump.find("2000 16 0 32 2") != std::string::npos);
      r.Message("maxsize 4");
      r.Message("polarity 1");
      r.Message("dropout 50");
      r.Message("repeat 50");
      r.Message("ampvar 0.5");
      r.Message("clear");
      r.st->GetInlet(0)->SetInt(3, YSE::T_GUI);
      r.st->GetInlet(1)->SetFloat(1.f, YSE::T_GUI);
      r.st->GetInlet(2)->SetFloat(0.5f, YSE::T_GUI);
      CHECK(r.p.DumpJSON() == dump);
    }
    patcherImplementation dst(3, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
    dStutter* reloaded = nullptr;
    for (unsigned int n = 0; n < dst.Objects(); ++n) {
      YSE::pHandle* h = dst.GetHandleFromList(n);
      if (std::string(h->Type()) == YSE::OBJ::D_STUTTER)
        reloaded = static_cast<dStutter*>(dst.GetObjectFromID(h->GetID()));
    }
    REQUIRE(reloaded != nullptr);
    CHECK(reloaded->Stutter().capacity() == 2000);
    CHECK(reloaded->CurrentSize() == 16);
    CHECK(reloaded->CurrentMaxSize() == 2000);
    CHECK_FALSE(reloaded->Stutter().polarity());
    CHECK(reloaded->Stutter().outputs() == 2);
    CHECK(reloaded->NumOutputs() == 2);
    dst.Calculate(YSE::T_DSP);
    CHECK(reloaded->Stutter().copyRate() == 32);
    CHECK(reloaded->Stutter().dropoutChance() == 0.f);
  }

  TEST_CASE("~stutter: blocks, numbers and messages on the audio thread allocate nothing "
            "(#1150)") {
    std::shared_ptr<sampleBuffer> a = sampleBufferRegistry::acquire("rt_1150_a");
    a->setSize(5000, 2);
    std::shared_ptr<sampleBuffer> b = sampleBufferRegistry::acquire("rt_1150_b");
    b->setSize(500, 1);
    StutterRig r("48000 4000 1 512 2", 2);
    YSE::pHandle* phasor = r.p.CreateObject(YSE::OBJ::D_PHASOR, "11");
    REQUIRE(phasor != nullptr);
    r.p.Connect(phasor, 0, r.handle, 1);
    r.p.Connect(phasor, 0, r.handle, 2);
    // Built before the probe: the strings themselves allocate.
    const std::string toA = "setbuf rt_1150_a 10 2";
    const std::string toB = "setbuf rt_1150_b";
    const std::vector<std::string> messages = {"clear",         "maxsize 3000", "polarity 0",
                                               "ampvar 0.5",    "dropout 10",   "repeat 10",
                                               "maxsize 48000", "polarity 1"};
    for (const std::string* m : {&toA, &toB, &toA}) {
      CAPTURE(*m);
      {
        TestHelpers::ProbeScope probe;
        r.Message(*m, YSE::T_DSP);
        for (const std::string& text : messages)
          r.Message(text, YSE::T_DSP);
        r.st->GetInlet(0)->SetBang(YSE::T_DSP);
        r.st->GetInlet(0)->SetInt(2000, YSE::T_DSP);
        r.st->GetInlet(0)->SetFloat(3000.f, YSE::T_DSP);
        r.st->GetInlet(1)->SetFloat(1.f, YSE::T_DSP);
        r.st->GetInlet(3)->SetInt(0, YSE::T_DSP);
        r.st->GetInlet(3)->SetFloat(0.5f, YSE::T_DSP);
        CHECK(TestHelpers::g_alloc_count.load() == 0);
      }
      r.p.FileIO()->WaitIdle();
      {
        // The block that takes the new writer, writes and asks for the old
        // one to go.
        TestHelpers::ProbeScope probe;
        for (int block = 0; block < 4; ++block)
          r.p.Calculate(YSE::T_DSP);
        CHECK(TestHelpers::g_alloc_count.load() == 0);
      }
      r.p.FileIO()->WaitIdle();
      r.p.Calculate(YSE::T_DSP); // hands the slots back
    }
    CHECK(r.st->SetbufBuffer() == a);
    CHECK(r.p.FileIO()->Dropped() == 0);
  }

  TEST_CASE("~stutter renders while control threads send it messages (#1150)") {
    // A ~cycle recorded and replayed by a ~phasor while control threads
    // bang, resize, clear, setbuf and change its chances. Every output is a
    // recorded ~cycle sample, or a line between two, at a gain of at most 1:
    // within -1 .. 1. Under ASan or TSan a broken handoff shows up here.
    std::shared_ptr<sampleBuffer> target = sampleBufferRegistry::acquire("race_1150");
    target->setSize(3000, 1);
    StutterRig r("20000 2000 1 0 1", 1, false);
    YSE::pHandle* cycle = r.p.CreateObject(YSE::OBJ::D_CYCLE, "300");
    YSE::pHandle* phasor = r.p.CreateObject(YSE::OBJ::D_PHASOR, "40");
    REQUIRE(cycle != nullptr);
    REQUIRE(phasor != nullptr);
    r.p.Connect(cycle, 0, r.handle, 0);
    r.p.Connect(phasor, 0, r.handle, 2);
    const std::vector<std::string> messages = {
        "clear",     "maxsize 1000", "maxsize 20000", "ampvar 0.7",           "dropout 20",
        "repeat 30", "polarity 0",   "polarity 1",    "setbuf race_1150 5 1", "dropout 0"};

    std::atomic<bool> stop{false};
    std::thread sender([&] {
      std::size_t n = 0;
      while (!stop.load()) {
        r.Message(messages[n % messages.size()], YSE::T_GUI);
        r.st->GetInlet(0)->SetBang(YSE::T_GUI);
        r.st->GetInlet(0)->SetInt(static_cast<int>(100 + (n * 37) % 3000), YSE::T_GUI);
        n++;
        std::this_thread::yield();
      }
    });
    std::thread resizer([&] {
      int n = 0;
      while (!stop.load()) {
        target->setSize(2000 + static_cast<UInt>(n % 1500));
        n++;
        std::this_thread::yield();
      }
    });

    int bad = 0;
    int heard = 0;
    for (int block = 0; block < 3000; ++block) {
      r.p.Calculate(YSE::T_DSP);
      for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
        const float v = Out(r.p, i);
        if (!(v >= -1.001f && v <= 1.001f)) bad++;
        if (v != 0.f) heard++;
      }
    }
    stop.store(true);
    sender.join();
    resizer.join();
    CHECK(bad == 0);
    CHECK(heard > 0);
  }

} // TEST_SUITE

// Tests for the ~send and ~receive patcher objects (patcher/routing/dSend,
// dReceive), issue #1136: signals sent by name over DSP::signalBus, without
// a cord. They drive the objects as a patch does, on rendered patches:
//
//  - a ~send in one subpatcher heard by a ~receive in another in the same
//    block, whatever order the objects were created in;
//  - several ~send objects of one name summed;
//  - set re-targeting both ends, ordered again by the graph rebuild it asks
//    for, and not saved;
//  - a feedback loop through a name, which renders with one block of delay
//    (the pair that would close the loop is cut, the others are not) and
//    which clear empties;
//  - patchers with the same name meeting one block late, deterministically
//    whichever renders first, and a rename moving the scope;
//  - two patchers rendered on two threads at once while a third retargets
//    a receive (the TSan case), and allocation freedom on the audio thread;
//  - and, in the rendergolden process, two sounds' patchers rendered by the
//    engine itself, whose block clock makes them meet one block late.

#include <doctest/doctest.h>
#include <atomic>
#include <cmath>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/routing/dReceive.h"
#include "patcher/routing/dSend.h"
#include "dsp/routing/signalBus.hpp"
#include "dsp/buffer.hpp"
#include "headers/constants.hpp"
#include "implementations/logImplementation.h"
#include "internal/rtLogQueue.h"
#include "internal/global.h"
#include "sound/soundInterface.hpp"
#include "system.hpp"
#include "support/alloc_probe.hpp"
#include "support/null_device.hpp"
#include "support/timer_pacing.hpp"
#include "sinks.hpp"

using YSE::DSP::signalBus;
using YSE::PATCHER::dReceive;
using YSE::PATCHER::dSend;
using YSE::PATCHER::patcherImplementation;

namespace {

  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // The engine log, as a host sees it (cf. test_patcher_signal_loops).
  struct LogCapture : YSE::logHandler {
    LogCapture() {
      YSE::INTERNAL::RtLog().drain();
      YSE::Log().setHandler(this);
    }
    ~LogCapture() override {
      YSE::Log().setHandler(nullptr);
    }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;
    LogCapture(LogCapture&&) = delete;
    LogCapture& operator=(LogCapture&&) = delete;

    void AddMessage(const std::string& message) override {
      const std::lock_guard<std::mutex> lock(mutex);
      lines.push_back(message);
    }
    bool Saw(const std::string& needle) const {
      const std::lock_guard<std::mutex> lock(mutex);
      for (const std::string& line : lines) {
        if (line.find(needle) != std::string::npos) return true;
      }
      return false;
    }

    mutable std::mutex mutex;
    std::vector<std::string> lines;
  };

  bool OutputIs(patcherImplementation& p, float value, unsigned int channel = 0) {
    YSE::DSP::buffer& out = p.output[channel];
    if (out.getLength() == 0) return false;
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      if (std::fabs(out.getPtr()[i] - value) > 1e-6f) return false;
    }
    return true;
  }

  float First(patcherImplementation& p, unsigned int channel = 0) {
    return p.output[channel].getPtr()[0];
  }

  // A number into an object's left inlet, as a cord from a number box would.
  void Number(patcherImplementation& p, YSE::pHandle* h, float value,
              YSE::THREAD thread = YSE::T_GUI) {
    p.GetObjectFromID(h->GetID())->GetInlet(0)->SetFloat(value, thread);
  }

  void Message(patcherImplementation& p, YSE::pHandle* h, const std::string& text,
               YSE::THREAD thread = YSE::T_GUI) {
    p.GetObjectFromID(h->GetID())->GetInlet(0)->SetMessage(text, thread);
  }

  dSend* SendOf(patcherImplementation& p, YSE::pHandle* h) {
    return static_cast<dSend*>(p.GetObjectFromID(h->GetID()));
  }

  dReceive* ReceiveOf(patcherImplementation& p, YSE::pHandle* h) {
    return static_cast<dReceive*>(p.GetObjectFromID(h->GetID()));
  }

  // [~sig] -> [~send name] in one subpatcher, [~receive name] -> [~dac] in
  // another, built in one order or the other.
  struct TwoRooms {
    YSE::pHandle* source = nullptr;
    YSE::pHandle* send = nullptr;
    YSE::pHandle* receive = nullptr;
    YSE::pHandle* dac = nullptr;
  };

  TwoRooms BuildTwoRooms(patcherImplementation& p, const std::string& name, bool receiveFirst) {
    TwoRooms r;
    YSE::pHandle* roomA = p.CreateObject(YSE::OBJ::PATCHER, "");
    YSE::pHandle* roomB = p.CreateObject(YSE::OBJ::PATCHER, "");
    if (receiveFirst) {
      r.receive = p.CreateObject(YSE::OBJ::D_RECEIVE, name);
      r.dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      r.send = p.CreateObject(YSE::OBJ::D_SEND, name);
      r.source = p.CreateObject(YSE::OBJ::D_SIG, "1");
    } else {
      r.source = p.CreateObject(YSE::OBJ::D_SIG, "1");
      r.send = p.CreateObject(YSE::OBJ::D_SEND, name);
      r.receive = p.CreateObject(YSE::OBJ::D_RECEIVE, name);
      r.dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    }
    REQUIRE(roomA != nullptr);
    REQUIRE(roomB != nullptr);
    REQUIRE(r.source != nullptr);
    REQUIRE(r.send != nullptr);
    REQUIRE(r.receive != nullptr);
    REQUIRE(r.dac != nullptr);
    p.SetObjectContainer(r.source, roomA);
    p.SetObjectContainer(r.send, roomA);
    p.SetObjectContainer(r.receive, roomB);
    p.SetObjectContainer(r.dac, roomB);
    p.Connect(r.source, 0, r.send, 0);
    p.Connect(r.receive, 0, r.dac, 0);
    return r;
  }

  // The feedback patch:
  //
  //   [~receive fb] -> [~* 0.5] -> [~+ 1] -> [~send fb]
  //                                   |
  //                                [~dac]
  //
  // plus a second [~receive fb] -> dac channel 1, outside the loop. The
  // loop's receive hears the previous block, so the ~+ outputs
  // s[k] = 1 + 0.5 s[k-1] = 2 - 0.5^k; the other receive is in no loop and
  // hears this block's s[k].
  struct Feedback {
    YSE::pHandle* receive = nullptr;
    YSE::pHandle* gain = nullptr;
    YSE::pHandle* add = nullptr;
    YSE::pHandle* send = nullptr;
    YSE::pHandle* tap = nullptr;
    YSE::pHandle* dac = nullptr;
  };

  Feedback BuildFeedback(patcherImplementation& p, bool reversed) {
    Feedback f;
    if (!reversed) {
      f.receive = p.CreateObject(YSE::OBJ::D_RECEIVE, "fb");
      f.gain = p.CreateObject(YSE::OBJ::D_MULTIPLY, "0.5");
      f.add = p.CreateObject(YSE::OBJ::D_ADD, "1");
      f.send = p.CreateObject(YSE::OBJ::D_SEND, "fb");
      f.tap = p.CreateObject(YSE::OBJ::D_RECEIVE, "fb");
      f.dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    } else {
      f.dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      f.tap = p.CreateObject(YSE::OBJ::D_RECEIVE, "fb");
      f.send = p.CreateObject(YSE::OBJ::D_SEND, "fb");
      f.add = p.CreateObject(YSE::OBJ::D_ADD, "1");
      f.gain = p.CreateObject(YSE::OBJ::D_MULTIPLY, "0.5");
      f.receive = p.CreateObject(YSE::OBJ::D_RECEIVE, "fb");
    }
    p.Connect(f.receive, 0, f.gain, 0);
    p.Connect(f.gain, 0, f.add, 0);
    p.Connect(f.add, 0, f.send, 0);
    p.Connect(f.add, 0, f.dac, 0);
    p.Connect(f.tap, 0, f.dac, 1);
    return f;
  }

  double Loop(int k) {
    return 2.0 - std::pow(0.5, k);
  }

} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~send / ~receive: registry, shape and category (#1136)") {
    bool hasSend = false;
    bool hasReceive = false;
    for (const auto& name : YSE::PATCHER::Register().AllNames()) {
      if (name == YSE::OBJ::D_SEND) hasSend = true;
      if (name == YSE::OBJ::D_RECEIVE) hasReceive = true;
    }
    CHECK(hasSend);
    CHECK(hasReceive);

    YSE::patcher p;
    p.create(1);
    YSE::pHandle* s = p.CreateObject(YSE::OBJ::D_SEND, "x");
    YSE::pHandle* r = p.CreateObject(YSE::OBJ::D_RECEIVE, "x");
    REQUIRE(s != nullptr);
    REQUIRE(r != nullptr);
    CHECK(std::string(s->Type()) == "~send");
    CHECK(std::string(r->Type()) == "~receive");
    CHECK(s->GetInputs() == 1);
    CHECK(s->GetOutputs() == 0);
    CHECK(r->GetInputs() == 1);
    REQUIRE(r->GetOutputs() == 1);
    CHECK(r->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    dSend send;
    dReceive receive;
    CHECK(send.IsDSPObject());
    CHECK(receive.IsDSPObject());
    CHECK(send.GetCategory() == YSE::PATCHER::pCategory::ROUTING);
    CHECK(receive.GetCategory() == YSE::PATCHER::pCategory::ROUTING);
    CHECK((send.GetInlet(0)->GetAcceptedTypes() & YSE::PATCHER::IT_BUFFER) != 0u);
    CHECK((send.GetInlet(0)->GetAcceptedTypes() & YSE::PATCHER::IT_FLOAT) != 0u);
    CHECK((receive.GetInlet(0)->GetAcceptedTypes() & YSE::PATCHER::IT_BUFFER) == 0u);

    // Outside a patcher there is no scope: bound to nothing, and a set does
    // nothing.
    send.SetParams("x");
    CHECK(send.BusName() == "x");
    CHECK(send.Sender().bus().get() == nullptr);
    send.GetInlet(0)->SetMessage("set y", YSE::T_GUI);
    CHECK(send.BusName() == "x");
  }

  TEST_CASE("~send / ~receive: a ~send in one subpatcher is heard by a ~receive in another, "
            "in the same block (#1136)") {
    for (const bool receiveFirst : {false, true}) {
      CAPTURE(receiveFirst);
      patcherImplementation p(1, nullptr);
      TwoRooms r = BuildTwoRooms(p, "rooms", receiveFirst);
      // The scope is the patcher's: the bus is "patcher.<name>.rooms".
      REQUIRE(SendOf(p, r.send)->Sender().bus().get() != nullptr);
      CHECK(SendOf(p, r.send)->Sender().bus()->name() == p.ScopedAddress("rooms"));
      CHECK(ReceiveOf(p, r.receive)->Receiver().bus().get() ==
            SendOf(p, r.send)->Sender().bus().get());

      // Heard in the first block already, and every change in its own block.
      p.Calculate(YSE::T_DSP);
      CHECK(OutputIs(p, 1.f));
      for (int k = 2; k < 6; ++k) {
        Number(p, r.source, static_cast<float>(k));
        p.Calculate(YSE::T_DSP);
        CHECK(OutputIs(p, static_cast<float>(k)));
      }
    }
  }

  TEST_CASE("~send / ~receive: several ~send objects of one name sum (#1136)") {
    patcherImplementation p(2, nullptr);
    YSE::pHandle* a = p.CreateObject(YSE::OBJ::D_SIG, "0.25");
    YSE::pHandle* b = p.CreateObject(YSE::OBJ::D_SIG, "0.5");
    YSE::pHandle* sendAB = p.CreateObject(YSE::OBJ::D_SEND, "sum");
    YSE::pHandle* sendC = p.CreateObject(YSE::OBJ::D_SEND, "sum");
    YSE::pHandle* other = p.CreateObject(YSE::OBJ::D_SEND, "other");
    YSE::pHandle* receive = p.CreateObject(YSE::OBJ::D_RECEIVE, "sum");
    YSE::pHandle* receiveOther = p.CreateObject(YSE::OBJ::D_RECEIVE, "other");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    // Two cords into one ~send sum too (#1036); an unconnected ~send sends
    // the last number as a constant.
    p.Connect(a, 0, sendAB, 0);
    p.Connect(b, 0, sendAB, 0);
    p.Connect(receive, 0, dac, 0);
    p.Connect(receiveOther, 0, dac, 1);
    Number(p, sendC, 2.f);
    Number(p, other, 8.f);
    p.Calculate(YSE::T_DSP);
    CHECK(OutputIs(p, 2.75f, 0));
    CHECK(OutputIs(p, 8.f, 1));
    CHECK(SendOf(p, sendAB)->Sender().bus()->senders() == 2);

    // A ~send deleted leaves the sum at once.
    p.DeleteObject(sendC);
    p.Calculate(YSE::T_DSP);
    CHECK(OutputIs(p, 0.75f, 0));
  }

  TEST_CASE("~send / ~receive: set re-targets either end from a later block, and is not "
            "saved (#1136)") {
    // Built in both orders: before the set, nothing orders the receive
    // against b's ~send, so one of the two runs it first unless the set
    // rebuilds the graph.
    for (const bool receiveFirst : {false, true}) {
      CAPTURE(receiveFirst);
      patcherImplementation p(1, nullptr);
      YSE::pHandle* receive = receiveFirst ? p.CreateObject(YSE::OBJ::D_RECEIVE, "a") : nullptr;
      YSE::pHandle* one = p.CreateObject(YSE::OBJ::D_SIG, "1");
      YSE::pHandle* two = p.CreateObject(YSE::OBJ::D_SIG, "2");
      YSE::pHandle* sendA = p.CreateObject(YSE::OBJ::D_SEND, "a");
      YSE::pHandle* sendB = p.CreateObject(YSE::OBJ::D_SEND, "b");
      if (!receiveFirst) receive = p.CreateObject(YSE::OBJ::D_RECEIVE, "a");
      YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      p.Connect(one, 0, sendA, 0);
      p.Connect(two, 0, sendB, 0);
      p.Connect(receive, 0, dac, 0);
      const std::string dump = p.DumpJSON();
      p.Calculate(YSE::T_DSP);
      CHECK(OutputIs(p, 1.f));

      // The receive to b. The sync rebuilds the graph, so the new pair runs in
      // order: a change to b's source is heard in the block it is made.
      Message(p, receive, "set b");
      p.SettleSignalBus();
      CHECK(ReceiveOf(p, receive)->BusName() == "b");
      Number(p, two, 5.f);
      p.Calculate(YSE::T_DSP);
      CHECK(OutputIs(p, 5.f));
      Number(p, two, 6.f);
      p.Calculate(YSE::T_DSP);
      CHECK(OutputIs(p, 6.f));

      // The ~send of a to b as well: the two sum.
      Message(p, sendA, "set b");
      p.SettleSignalBus();
      CHECK(SendOf(p, sendA)->BusName() == "b");
      Number(p, one, 3.f);
      p.Calculate(YSE::T_DSP);
      CHECK(OutputIs(p, 9.f));

      // A set with no name: the receive hears nothing.
      Message(p, receive, "set");
      p.SettleSignalBus();
      p.Calculate(YSE::T_DSP);
      CHECK(OutputIs(p, 0.f));

      // A set arriving on the audio thread works the same way.
      Message(p, receive, "set b", YSE::T_DSP);
      p.SettleSignalBus();
      p.Calculate(YSE::T_DSP);
      CHECK(OutputIs(p, 9.f));

      // Too long a name is refused and the name stays.
      {
        LogCapture log;
        Message(p, receive, "set " + std::string(64, 'n'));
        p.SettleSignalBus();
        YSE::INTERNAL::RtLog().drain();
        CHECK(log.Saw("longer than 63"));
        CHECK(ReceiveOf(p, receive)->BusName() == "b");
      }

      // None of it is saved.
      CHECK(p.DumpJSON() == dump);
    }
  }

  TEST_CASE("~send / ~receive: a feedback loop through a name renders with one block of "
            "delay (#1136)") {
    for (const bool reversed : {false, true}) {
      CAPTURE(reversed);
      LogCapture log;
      patcherImplementation p(2, nullptr);
      Feedback f = BuildFeedback(p, reversed);
      CHECK_FALSE(log.Saw("connection refused"));
      for (int k = 0; k < 10; ++k) {
        p.Calculate(YSE::T_DSP);
        CAPTURE(k);
        CHECK(First(p, 0) == doctest::Approx(Loop(k)));
        CHECK(OutputIs(p, static_cast<float>(Loop(k)), 0));
        // The receive outside the loop hears this block's send.
        CHECK(OutputIs(p, static_cast<float>(Loop(k)), 1));
      }

      // clear: the next block sends silence, so the block after starts the
      // loop again from 1.
      Message(p, f.send, "clear");
      p.Calculate(YSE::T_DSP);
      CHECK(First(p, 0) == doctest::Approx(Loop(10)));
      CHECK(OutputIs(p, 0.f, 1));
      p.Calculate(YSE::T_DSP);
      CHECK(First(p, 0) == doctest::Approx(1.0));
      p.Calculate(YSE::T_DSP);
      CHECK(First(p, 0) == doctest::Approx(1.5));
    }
  }

  TEST_CASE("~send / ~receive: patchers with the same name meet one block late, whichever "
            "renders first (#1136)") {
    patcherImplementation a(1, nullptr);
    patcherImplementation b(2, nullptr);
    patcherImplementation c(1, nullptr);
    a.SetName("shared1136");
    b.SetName("shared1136");
    c.SetName("apart1136");
    YSE::pHandle* source = a.CreateObject(YSE::OBJ::D_SIG, "0");
    YSE::pHandle* send = a.CreateObject(YSE::OBJ::D_SEND, "x");
    a.Connect(source, 0, send, 0);
    // b hears a's x one block late, and its own x in the same block.
    YSE::pHandle* local = b.CreateObject(YSE::OBJ::D_SEND, "x");
    YSE::pHandle* receive = b.CreateObject(YSE::OBJ::D_RECEIVE, "x");
    YSE::pHandle* dacB = b.CreateObject(YSE::OBJ::D_DAC, "");
    b.Connect(receive, 0, dacB, 0);
    Number(b, local, 100.f);
    // c has another name: it never hears a.
    YSE::pHandle* receiveC = c.CreateObject(YSE::OBJ::D_RECEIVE, "x");
    YSE::pHandle* dacC = c.CreateObject(YSE::OBJ::D_DAC, "");
    c.Connect(receiveC, 0, dacC, 0);

    for (int k = 0; k < 8; ++k) {
      CAPTURE(k);
      Number(a, source, static_cast<float>(k + 1));
      // The order the engine's render workers would pick changes nothing.
      if (k % 2 == 0) {
        a.Calculate(YSE::T_DSP);
        b.Calculate(YSE::T_DSP);
      } else {
        b.Calculate(YSE::T_DSP);
        a.Calculate(YSE::T_DSP);
      }
      c.Calculate(YSE::T_DSP);
      CHECK(OutputIs(b, 100.f + static_cast<float>(k)));
      CHECK(OutputIs(c, 0.f));
      signalBus::advance();
    }

    // A rename moves the scope: b alone under a new name hears only itself,
    // and a joining it is heard again.
    b.SetName("moved1136");
    b.Calculate(YSE::T_DSP);
    a.Calculate(YSE::T_DSP);
    signalBus::advance();
    b.Calculate(YSE::T_DSP);
    CHECK(OutputIs(b, 100.f));
    a.SetName("moved1136");
    Number(a, source, 7.f);
    a.Calculate(YSE::T_DSP);
    signalBus::advance();
    b.Calculate(YSE::T_DSP);
    CHECK(OutputIs(b, 107.f));
  }

  TEST_CASE("~send / ~receive: the name survives a DumpJSON / ParseJSON round trip; too long "
            "a name is refused (#1136)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      BuildTwoRooms(src, "json1136", false);
      dump = src.DumpJSON();
    }
    CHECK(dump.find("json1136") != std::string::npos);
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
    dst.Calculate(YSE::T_DSP);
    CHECK(OutputIs(dst, 1.f));

    LogCapture log;
    patcherImplementation p(1, nullptr);
    YSE::pHandle* s = p.CreateObject(YSE::OBJ::D_SEND, std::string(64, 'n'));
    REQUIRE(s != nullptr);
    CHECK(log.Saw("longer than 63"));
    CHECK(SendOf(p, s)->BusName().empty());
    CHECK(SendOf(p, s)->Sender().bus().get() == nullptr);
  }

  TEST_CASE("~send / ~receive: rendering and set allocate nothing on the audio thread "
            "(#1136)") {
    patcherImplementation p(2, nullptr);
    Feedback f = BuildFeedback(p, false);
    TwoRooms r = BuildTwoRooms(p, "alloc1136", false);
    p.Calculate(YSE::T_DSP);
    // Built before the probe: the strings themselves allocate.
    const std::string toB = "set alloc1136b";
    const std::string toA = "set alloc1136";
    const std::string clear = "clear";
    {
      TestHelpers::ProbeScope probe;
      for (int k = 0; k < 8; ++k) {
        p.Calculate(YSE::T_DSP);
        Message(p, f.send, clear, YSE::T_DSP);
        Message(p, r.receive, (k % 2 == 0) ? toB : toA, YSE::T_DSP);
        Number(p, r.send, 0.5f, YSE::T_DSP);
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    p.SettleSignalBus();
  }

  TEST_CASE("~send / ~receive: two patchers rendered at once while a receive is retargeted "
            "(#1136)") {
    // Two render workers, each on a patcher, every block at the same time;
    // the clock advances between blocks, as the engine does. A control thread
    // retargets b's receive between two names all along, which rebinds it and
    // rebuilds b's graph while b renders. Under TSan nothing may race, and b
    // hears a's previous block, or silence while its receive is on the other
    // name.
    patcherImplementation a(1, nullptr);
    patcherImplementation b(1, nullptr);
    a.SetName("threads1136");
    b.SetName("threads1136");
    YSE::pHandle* source = a.CreateObject(YSE::OBJ::D_SIG, "0");
    YSE::pHandle* send = a.CreateObject(YSE::OBJ::D_SEND, "x");
    a.Connect(source, 0, send, 0);
    YSE::pHandle* receive = b.CreateObject(YSE::OBJ::D_RECEIVE, "x");
    YSE::pHandle* dac = b.CreateObject(YSE::OBJ::D_DAC, "");
    b.Connect(receive, 0, dac, 0);
    dReceive* r = ReceiveOf(b, receive);

    std::atomic<bool> stop{false};
    std::thread control([&] {
      bool flip = false;
      while (!stop.load(std::memory_order_acquire)) {
        r->GetInlet(0)->SetMessage(flip ? "set x" : "set elsewhere", YSE::T_GUI);
        flip = !flip;
        b.SettleSignalBus();
        signalBus::collect();
        std::this_thread::yield();
      }
      r->GetInlet(0)->SetMessage("set x", YSE::T_GUI);
      b.SettleSignalBus();
    });

    constexpr int blocks = 150;
    int wrong = 0;
    for (int k = 0; k < blocks; ++k) {
      const float value = static_cast<float>(k + 1);
      std::thread renderA([&] {
        Number(a, source, value);
        a.Calculate(YSE::T_DSP);
      });
      std::thread renderB([&] {
        b.Calculate(YSE::T_DSP);
        const float heard = First(b);
        if (k > 0 && heard != value - 1.f && heard != 0.f) wrong++;
      });
      renderA.join();
      renderB.join();
      signalBus::advance();
    }
    stop.store(true, std::memory_order_release);
    control.join();
    CHECK(wrong == 0);

    // Settled on x again: the previous block, exactly.
    Number(a, source, 1000.f);
    a.Calculate(YSE::T_DSP);
    signalBus::advance();
    b.Calculate(YSE::T_DSP);
    CHECK(OutputIs(b, 1000.f));
  }

} // TEST_SUITE("patcher")

// The engine's own render: in the rendergolden process, which is the sole
// driver of the mix (see test_render_golden.cpp), so the test thread can read
// the bus between two offline renders without racing a block.
TEST_SUITE("rendergolden") {

  TEST_CASE("rendergolden: a ~send in one sound's patcher reaches a ~receive in another's one "
            "block late, on 0 and 2 render workers (#1136)") {
    if (!TestHelpers::engineInit()) return;
    if (YSE::System().getActiveSampleRate() != 0.0) {
      MESSAGE("skipped: an audio stream is live in this process, so the test thread is not the "
              "only renderer.");
      return;
    }
    // a: [~sig 0.25] -> [~send x]. b: [~receive x] -> [~* 2] -> [~send y].
    // The test reads y from C++, after the render: y's previous block is b's
    // block before last, which heard a's block before that: 0.5, every time.
    // Without the engine advancing the bus clock, b would never hear a.
    YSE::patcher a;
    YSE::patcher b;
    a.create(1);
    b.create(1);
    a.name("engine1136");
    b.name("engine1136");
    YSE::pHandle* source = a.CreateObject(YSE::OBJ::D_SIG, "0.25");
    YSE::pHandle* send = a.CreateObject(YSE::OBJ::D_SEND, "x");
    a.Connect(source, 0, send, 0);
    YSE::pHandle* receive = b.CreateObject(YSE::OBJ::D_RECEIVE, "x");
    YSE::pHandle* gain = b.CreateObject(YSE::OBJ::D_MULTIPLY, "2");
    YSE::pHandle* resend = b.CreateObject(YSE::OBJ::D_SEND, "y");
    b.Connect(receive, 0, gain, 0);
    b.Connect(gain, 0, resend, 0);
    YSE::DSP::signalBusReceiver probe;
    probe.bind(signalBus::acquire("patcher.engine1136.y"));

    YSE::sound playA;
    YSE::sound playB;
    playA.create(a, nullptr, 0.f);
    playB.create(b, nullptr, 0.f);
    playA.play();
    playB.play();
    const bool playing = TestHelpers::pacedPump(
        3000, [&] { return playA.isPlaying() && playB.isPlaying(); },
        [] {
          YSE::System().update();
          YSE::System().renderOffline(1);
        },
        2);
    REQUIRE(playing);

    auto& renderer = YSE::INTERNAL::Global().renderer();
    renderer.setSerialGating(false);
    for (const int workers : {0, 2}) {
      CAPTURE(workers);
      YSE::INTERNAL::Global().setRenderWorkerCount(workers);
      const std::uint64_t before = signalBus::block();
      YSE::System().renderOffline(8);
      CHECK(signalBus::block() == before + 8);
      YSE::DSP::buffer& out = probe.read();
      bool all = true;
      for (unsigned int i = 0; i < out.getLength(); ++i)
        if (out.getPtr()[i] != 0.5f) all = false;
      CHECK(all);
    }
    renderer.setSerialGating(true);
    YSE::INTERNAL::Global().setRenderWorkerCount(-1);
    playA.stop();
    playB.stop();
    YSE::System().update();
  }

} // TEST_SUITE("rendergolden")

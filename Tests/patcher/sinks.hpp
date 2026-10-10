// Shared sink objects for patcher unit tests.
//
// A "sink" is a tiny pObject subclass with a single inlet that records the
// last value received from an upstream outlet, so a test can assert on what
// the object-under-test sent.  Each sink type matches one outlet data type
// (FLOAT, INT, BUFFER).
//
// Originally extracted from test_patcher_math.cpp:24-55.

#pragma once

#include <doctest/doctest.h>

#include "patcher/inlet.h"
#include "patcher/outlet.h"
#include "patcher/pObject.h"
#include "dsp/buffer.hpp"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

namespace TestHelpers {

  // Wire two standalone objects the way `patcherImplementation::ConnectUnlocked`
  // wires two objects in a real patch: **both ends, inlet first**.
  //
  // Registering only the outlet side is enough to make sends work, which is why
  // it is an easy thing to write and a hard thing to notice. It is also a bug,
  // and a documented one — `pObject::ConnectInlet` and `ConnectUnlocked` both
  // spell it out for issue #237: "a one-sided outlet->inlet edge survives
  // Disconnect/UnwireFromPeers (both clean up from the inlet's records)". The
  // teardown consequence is what issue #727 swept out of ~22 test files:
  // `~outlet` walks its `connections` and calls `inlet::Disconnect` on every
  // peer, and `~inlet` does the mirror image — so a *symmetric* edge is unwired
  // by whichever end dies first and destruction order stops mattering. A
  // one-sided one leaves the outlet holding an `inlet*` the inlet never knew
  // about, and destroying the receiver first makes `~outlet` read freed memory.
  //
  // Two habits go with it, and a standalone rig wants all three:
  //   * declare sinks **before** the object that sends to them, so the object
  //     dies first (it matters for anything holding a timer slot, which must be
  //     given back while its target is still alive);
  //   * never `sleep_for` to await a timer — `timerBridge::WaitIdle()` is the
  //     handshake that actually says the callback is done.
  inline void Wire(YSE::PATCHER::pObject& from, int outlet, YSE::PATCHER::pObject& to,
                   int inlet = 0) {
    // The inlet is asked first and the outlet only records the edge if it
    // accepted, exactly as ConnectUnlocked does.
    REQUIRE(to.ConnectInlet(from.GetOutlet(outlet), inlet));
    from.ConnectOutlet(to.GetInlet(inlet), outlet);
  }

  // The base every test-owned pObject derives from, instead of pObject itself
  // (issue #967). It adds one thing: its destructor fails the running test if
  // an object inside a patcher still feeds one of its inlets.
  //
  // A test-owned object wired to a patcher must be declared **before** the
  // patcherImplementation, so it is destroyed *after* it. The patcher's
  // teardown then unwires every cord while the sink is still alive. Declared
  // after, the sink dies first: its `~inlet` removes the edge from the live
  // wiring, but the published GraphState still holds the sink's `inlet*`, and
  // since #963 a control-thread send reads that snapshot. Any object that
  // sends from its Teardown (.makenote, .midiflush, .flush, .sustain, .poly,
  // .metro, .preset, when they hold pending state) then delivers into a
  // destroyed object: "pure virtual function called" natively, a
  // use-after-free under ASan. Most misordered tests never trip that, so the
  // bug would stay latent until someone adds pending state to one of them.
  //
  // A patcher cord still attached when the sink's destructor runs is exactly
  // that misordering, whether or not anything sends, so this makes it fail
  // every time instead of only the unlucky times. The message goes through
  // FAIL_CHECK, which never throws, so it is safe during unwinding.
  //
  // Only cords from a patcher's objects count — an outlet with a graph id.
  // A standalone object has no GraphState to go stale, and the symmetric
  // edge `Wire` makes is unwired by whichever end dies first; for those the
  // declare-first habit above is about timer slots, not this.
  //
  // A local test object belongs on this base too, not on pObject directly,
  // or the check never runs for it.
  struct SinkBase : YSE::PATCHER::pObject {
    explicit SinkBase(bool isDSPObject = false) : pObject(isDSPObject) {}
    SinkBase(const SinkBase&) = delete;
    SinkBase& operator=(const SinkBase&) = delete;
    SinkBase(SinkBase&&) = delete;
    SinkBase& operator=(SinkBase&&) = delete;
    ~SinkBase() override {
      // Type() is off limits here: the derived part is already gone, so the
      // call would itself be the pure virtual call this guards against.
      for (const auto& in : inputs) {
        if (FedByPatcher(in)) {
          FAIL_CHECK("a test sink was destroyed while a patcher object still feeds it: "
                     "declare it before the patcher (issue #967)");
          return;
        }
      }
    }

  private:
    static bool FedByPatcher(const YSE::PATCHER::inlet& in) {
      if (in.DspSource() != nullptr && in.DspSource()->GraphId() >= 0) return true;
      for (const YSE::PATCHER::outlet* source : in.Sources())
        if (source->GraphId() >= 0) return true;
      return false;
    }
  };

  struct FloatSink : SinkBase {
    float received = 0.f;
    bool gotFloat = false;
    FloatSink() {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterFloat([this](float v, int, YSE::THREAD) {
        received = v;
        gotFloat = true;
      });
    }
    const char* Type() const override {
      return "float_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  struct IntSink : SinkBase {
    int received = -999;
    bool gotInt = false;
    IntSink() {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) {
        received = v;
        gotInt = true;
      });
    }
    const char* Type() const override {
      return "int_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  struct BufferSink : SinkBase {
    YSE::DSP::buffer* received = nullptr;
    BufferSink() {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBuffer([this](YSE::DSP::buffer* b, int, YSE::THREAD) { received = b; });
    }
    const char* Type() const override {
      return "buffer_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  struct BangSink : SinkBase {
    int bangCount = 0;
    bool gotBang = false;
    BangSink() {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) {
        bangCount++;
        gotBang = true;
      });
    }
    const char* Type() const override {
      return "bang_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  struct ListSink : SinkBase {
    std::string received;
    bool gotList = false;
    ListSink() {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterList([this](const std::string& v, int, YSE::THREAD) {
        received = v;
        gotList = true;
      });
    }
    const char* Type() const override {
      return "list_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // Captures the inlet::SetMessage path (outlet::SendMessage -> obj->SetMessage).
  // Used to test gMessage which sends via SendMessage rather than SendList. It
  // declares a command channel (HandlesMessages) and no typed handlers, so every
  // text a message box sends lands in SetMessage (issue #933).
  struct MessageSink : SinkBase {
    std::string received;
    bool gotMessage = false;
    MessageSink() {
      inputs.emplace_back(this, true, 0);
    }
    const char* Type() const override {
      return "message_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string& message, float) override {
      received = message;
      gotMessage = true;
    }
    bool HandlesMessages() const override {
      return true;
    }
  };

  // Captures all four non-DSP message kinds.  Useful for verifying which path a
  // switching/routing object (gGate, gRoute, gSwitch) actually fires.
  struct MultiSink : SinkBase {
    bool gotBang = false;
    bool gotInt = false;
    bool gotFloat = false;
    bool gotList = false;
    int intValue = 0;
    float floatValue = 0.f;
    std::string listValue;

    MultiSink() {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { gotBang = true; });
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) {
        gotInt = true;
        intValue = v;
      });
      inputs.back().RegisterFloat([this](float v, int, YSE::THREAD) {
        gotFloat = true;
        floatValue = v;
      });
      inputs.back().RegisterList([this](const std::string& v, int, YSE::THREAD) {
        gotList = true;
        listValue = v;
      });
    }
    const char* Type() const override {
      return "multi_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    void reset() {
      gotBang = gotInt = gotFloat = gotList = false;
      intValue = 0;
      floatValue = 0.f;
      listValue.clear();
    }
  };

  // Records *when* it was hit as well as what arrived, so the firing order of a
  // multi-outlet object can be asserted rather than assumed.  Give each sink a
  // distinct `tag` and point every one of them at the same `log`: the log then
  // reads back as the exact sequence of sends, and a test that only counted
  // hits could not tell a right-to-left object from a left-to-right one.
  //
  // Extracted for .trigger (#466), whose right-to-left ordering guarantee *is*
  // the object.  Anything else that fires more than one outlet per input wants
  // the same rig — .bangbang (#467) is the degenerate all-bang case, and
  // .mean / .cartopol / .peak each grew a local copy of this before it was
  // shared.
  struct OrderSink : SinkBase {
    enum Kind { NONE, BANG, INT, FLOAT, LIST };

    std::vector<char>* log = nullptr;
    char tag = '?';

    Kind lastKind = NONE;
    int lastInt = 0;
    float lastFloat = 0.f;
    std::string lastList;
    int count = 0;

    OrderSink() {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { Record(BANG); });
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) {
        lastInt = v;
        Record(INT);
      });
      inputs.back().RegisterFloat([this](float v, int, YSE::THREAD) {
        lastFloat = v;
        Record(FLOAT);
      });
      inputs.back().RegisterList([this](const std::string& v, int, YSE::THREAD) {
        lastList = v;
        Record(LIST);
      });
    }
    const char* Type() const override {
      return "order_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

  private:
    void Record(Kind kind) {
      lastKind = kind;
      count++;
      if (log) log->push_back(tag);
    }
  };

  // Parks the thread that delivers into it, until the test lets it go — the
  // way to hold a control-thread send open at a known point while the test
  // thread edits the patch under it (issue #961). Only the first delivery
  // parks; later ones pass straight through. Every kind of message counts.
  struct GateSink : SinkBase {
    GateSink() {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { Hold(); });
      inputs.back().RegisterInt([this](int, int, YSE::THREAD) { Hold(); });
      inputs.back().RegisterFloat([this](float, int, YSE::THREAD) { Hold(); });
      inputs.back().RegisterList([this](const std::string&, int, YSE::THREAD) { Hold(); });
    }
    const char* Type() const override {
      return "gate_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    // True once a delivery is parked here, false after five seconds without.
    bool WaitEntered() {
      std::unique_lock<std::mutex> lock(mtx_);
      return cv_.wait_for(lock, std::chrono::seconds(5), [this] { return entered_; });
    }
    void Release() {
      {
        const std::lock_guard<std::mutex> lock(mtx_);
        released_ = true;
      }
      cv_.notify_all();
    }

  private:
    void Hold() {
      std::unique_lock<std::mutex> lock(mtx_);
      if (entered_) return;
      entered_ = true;
      cv_.notify_all();
      cv_.wait(lock, [this] { return released_; });
    }
    std::mutex mtx_;
    std::condition_variable cv_;
    bool entered_ = false;
    bool released_ = false;
  };

} // namespace TestHelpers

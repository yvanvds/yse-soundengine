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

#include "patcher/pObject.h"
#include "dsp/buffer.hpp"
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

  struct FloatSink : YSE::PATCHER::pObject {
    float received = 0.f;
    bool gotFloat = false;
    FloatSink() : pObject(false) {
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

  struct IntSink : YSE::PATCHER::pObject {
    int received = -999;
    bool gotInt = false;
    IntSink() : pObject(false) {
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

  struct BufferSink : YSE::PATCHER::pObject {
    YSE::DSP::buffer* received = nullptr;
    BufferSink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBuffer([this](YSE::DSP::buffer* b, int, YSE::THREAD) { received = b; });
    }
    const char* Type() const override {
      return "buffer_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  struct BangSink : YSE::PATCHER::pObject {
    int bangCount = 0;
    bool gotBang = false;
    BangSink() : pObject(false) {
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

  struct ListSink : YSE::PATCHER::pObject {
    std::string received;
    bool gotList = false;
    ListSink() : pObject(false) {
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
  // Used to test gMessage which sends via SendMessage rather than SendList.
  struct MessageSink : YSE::PATCHER::pObject {
    std::string received;
    bool gotMessage = false;
    MessageSink() : pObject(false) {
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
  };

  // Captures all four non-DSP message kinds.  Useful for verifying which path a
  // switching/routing object (gGate, gRoute, gSwitch) actually fires.
  struct MultiSink : YSE::PATCHER::pObject {
    bool gotBang = false;
    bool gotInt = false;
    bool gotFloat = false;
    bool gotList = false;
    int intValue = 0;
    float floatValue = 0.f;
    std::string listValue;

    MultiSink() : pObject(false) {
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
  struct OrderSink : YSE::PATCHER::pObject {
    enum Kind { NONE, BANG, INT, FLOAT, LIST };

    std::vector<char>* log = nullptr;
    char tag = '?';

    Kind lastKind = NONE;
    int lastInt = 0;
    float lastFloat = 0.f;
    std::string lastList;
    int count = 0;

    OrderSink() : pObject(false) {
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

} // namespace TestHelpers

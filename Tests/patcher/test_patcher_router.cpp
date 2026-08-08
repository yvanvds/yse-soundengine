// Tests for .router (issue #482) — a message crossbar whose connections are set
// by messages rather than by patch cords.
//
// Seven rules carry this file, and each is one a plausible implementation gets
// wrong:
//   - **the leftmost inlet routes nothing.** Max scopes every routed method to
//     "any but the leftmost inlet", so the object has one *more* inlet than its
//     argument asks for and the extra one takes the connection messages. An
//     implementation that made inlet 0 routable would still pass every routing
//     test, because the numbering would merely be shifted — so the shape, the
//     accepted types of inlet 0, and the fact that a routable inlet does *not*
//     interpret `connect` are all asserted directly.
//   - **the numbering counts routable inlets from 0.** `connect 0 0` addresses
//     the inlet immediately right of the control inlet, not the control inlet.
//     Off by one here is invisible in a symmetric test, so the tests use
//     asymmetric shapes and distinguishable sinks.
//   - **it is many-to-many.** One inlet to several outlets and several inlets to
//     one outlet, both at once. A selection-based implementation (`.gate` with a
//     different spelling) passes a one-cell test and fails this.
//   - **the outlets fire right to left.** Max's universal order. A test that only
//     read the final values could not tell it from its reverse, so the order is
//     logged.
//   - **the row is snapshotted before the first send.** Sends are synchronous, so
//     a sink that rewrites the routing re-enters *inside* the burst. Reading the
//     matrix per outlet would deliver one message under two routings; the
//     feedback case pins that it does not.
//   - **`patch` is a column operation.** It must disconnect the *other* inlets
//     from that outlet and leave every other outlet alone. A `clear` plus a
//     `connect` passes the end state and fails the "leave every other outlet
//     alone" half.
//   - **`dump` does not nest.** Its outlet is an ordinary outlet, so a patch can
//     wire it back to the control inlet. Without a guard the breadth multiplies
//     per level; the test counts the lines.
//
// No audio device required.

#include <doctest/doctest.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/pEnums.h"
#include "patcher/inlet.h"
#include "patcher/genericObjects/gRouter.h"
#include "patcher/sinks.hpp"

namespace {
  using TestHelpers::FloatSink;
  using TestHelpers::ListSink;
  using TestHelpers::OrderSink;
  using YSE::PATCHER::gRouter;

  // Records every list it is given, not just the last one. The dump outlet
  // emits one list per matrix cell, so "what did the whole dump say, and how
  // many lines was it" is the assertion — a sink that kept only the last value
  // could not tell a complete dump from a truncated or a nested one.
  struct DumpSink : YSE::PATCHER::pObject {
    std::vector<std::string> lines;

    DumpSink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { lines.push_back("<bang>"); });
      inputs.back().RegisterInt([this](int, int, YSE::THREAD) { lines.push_back("<int>"); });
      inputs.back().RegisterFloat([this](float, int, YSE::THREAD) { lines.push_back("<float>"); });
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { lines.push_back(v); });
    }
    const char* Type() const override {
      return "dump_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    std::string Joined() const {
      std::string out;
      for (std::size_t i = 0; i < lines.size(); i++) {
        if (i > 0) out += " | ";
        out += lines[i];
      }
      return out;
    }
  };

  // A sink that pushes a control message back into the router when it is hit, a
  // bounded number of times. A router whose outlet is wired to its own control
  // inlet is the re-entrancy case, and it needs a receiver that stops feeding
  // rather than a bare patch cord, which would not terminate.
  struct ControlFeedbackSink : YSE::PATCHER::pObject {
    gRouter* target = nullptr;
    std::string message;
    int budget = 0;

    std::vector<char>* log = nullptr;
    char tag = '?';
    int count = 0;

    ControlFeedbackSink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD t) { Hit(t); });
      inputs.back().RegisterInt([this](int, int, YSE::THREAD t) { Hit(t); });
      inputs.back().RegisterFloat([this](float, int, YSE::THREAD t) { Hit(t); });
      inputs.back().RegisterList([this](const std::string&, int, YSE::THREAD t) { Hit(t); });
    }
    const char* Type() const override {
      return "control_feedback_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

  private:
    void Hit(YSE::THREAD thread) {
      count++;
      if (log) log->push_back(tag);
      if (budget > 0 && target != nullptr) {
        budget--;
        target->GetInlet(0)->SetList(message, thread);
      }
    }
  };

  // One order-logging sink per routable outlet, all sharing one log, plus a
  // DumpSink on the rightmost outlet. Outlet i is tagged 'a' + i, so a single
  // message reads back as the firing order itself.
  struct Rig {
    gRouter op;
    std::vector<std::unique_ptr<OrderSink>> sinks;
    std::vector<char> order;
    DumpSink dumps;

    explicit Rig(const std::string& args = "") {
      if (!args.empty()) op.SetParams(args);
      for (int i = 0; i < op.OutletCount(); i++) {
        sinks.push_back(std::unique_ptr<OrderSink>(new OrderSink()));
        sinks.back()->tag = (char)('a' + (i % 26));
        sinks.back()->log = &order;
        op.ConnectOutlet(sinks.back()->GetInlet(0), i);
        sinks.back()->ConnectInlet(op.GetOutlet(i), 0);
      }
      op.ConnectOutlet(dumps.GetInlet(0), op.OutletCount());
      dumps.ConnectInlet(op.GetOutlet(op.OutletCount()), 0);
    }

    // A message on the control inlet — the connection messages.
    void Control(const std::string& text) {
      op.GetInlet(0)->SetList(text, YSE::T_GUI);
    }

    // `inlet` counts the routable inlets from 0, the way the connection
    // messages do; the physical inlet is one further right.
    void Int(int inlet, int value) {
      op.GetInlet(inlet + 1)->SetInt(value, YSE::T_GUI);
    }
    void Float(int inlet, float value) {
      op.GetInlet(inlet + 1)->SetFloat(value, YSE::T_GUI);
    }
    void Bang(int inlet) {
      op.GetInlet(inlet + 1)->SetBang(YSE::T_GUI);
    }
    void List(int inlet, const std::string& text) {
      op.GetInlet(inlet + 1)->SetList(text, YSE::T_GUI);
    }

    std::string Log() const {
      return std::string(order.begin(), order.end());
    }
    void Reset() {
      order.clear();
      dumps.lines.clear();
      for (auto& s : sinks)
        s->count = 0;
    }
    int Total() const {
      int t = 0;
      for (const auto& s : sinks)
        t += s->count;
      return t;
    }
  };
} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("router: creatable through the registry (#482)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ROUTER, "3 4");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".router"));
    // One more of each than the arguments ask for: the control inlet on the
    // left and the dump outlet on the right.
    CHECK(h->GetInputs() == 4);
    CHECK(h->GetOutputs() == 5);
  }

  TEST_CASE("router: listed by pRegistry::AllNames (#482)") {
    auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(YSE::OBJ::G_ROUTER)) != names.end());
  }

  TEST_CASE("router: two routable inlets and two routable outlets with no argument (#482)") {
    gRouter op;
    CHECK(op.InletCount() == 2);
    CHECK(op.OutletCount() == 2);
    CHECK(op.NumInputs() == 3);
    CHECK(op.NumOutputs() == 3);
    // And nothing is wired to begin with — Max's matrix starts empty.
    CHECK(op.ConnectionCount() == 0);
  }

  TEST_CASE(
      "router: a lone argument sets the inlets and leaves the outlets at the default (#482)") {
    gRouter op;
    op.SetParams("5");
    CHECK(op.InletCount() == 5);
    CHECK(op.OutletCount() == 2);
  }

  TEST_CASE("router: both counts are clamped to 1-256 independently (#482)") {
    gRouter low;
    low.SetParams("0 0");
    CHECK(low.InletCount() == 1);
    CHECK(low.OutletCount() == 1);

    gRouter negative;
    negative.SetParams("-7 -3");
    CHECK(negative.InletCount() == 1);
    CHECK(negative.OutletCount() == 1);

    gRouter high;
    high.SetParams("9999 9999");
    CHECK(high.InletCount() == gRouter::MAX_PORTS);
    CHECK(high.OutletCount() == gRouter::MAX_PORTS);

    // Clamped one at a time, not both to the same value.
    gRouter mixed;
    mixed.SetParams("9999 3");
    CHECK(mixed.InletCount() == gRouter::MAX_PORTS);
    CHECK(mixed.OutletCount() == 3);

    gRouter fractional;
    fractional.SetParams("3.9 2.9");
    CHECK(fractional.InletCount() == 3);
    CHECK(fractional.OutletCount() == 2);

    gRouter nonsense;
    nonsense.SetParams("wide tall");
    CHECK(nonsense.InletCount() == gRouter::DEFAULT_PORTS);
    CHECK(nonsense.OutletCount() == gRouter::DEFAULT_PORTS);
  }

  TEST_CASE("router: the matrix has one cell per inlet/outlet pair (#482)") {
    gRouter op;
    op.SetParams("3 4");
    for (int in = 0; in < 3; in++) {
      for (int out = 0; out < 4; out++)
        CHECK_FALSE(op.Connected(in, out));
    }
    // And no cell exists outside the shape, whichever axis is overrun.
    CHECK_FALSE(op.Connected(3, 0));
    CHECK_FALSE(op.Connected(0, 4));
    CHECK_FALSE(op.Connected(-1, 0));
    CHECK_FALSE(op.Connected(0, -1));
  }

  TEST_CASE("router: SetParams(\"\") returns the object to the no-argument shape (#482)") {
    gRouter op;
    op.SetParams("6 7");
    op.GetInlet(0)->SetList("connect 0 0", YSE::T_GUI);
    REQUIRE(op.InletCount() == 6);
    REQUIRE(op.Connected(0, 0));

    op.SetParams("");
    CHECK(op.InletCount() == gRouter::DEFAULT_PORTS);
    CHECK(op.OutletCount() == gRouter::DEFAULT_PORTS);
    CHECK(op.NumInputs() == gRouter::DEFAULT_PORTS + 1);
    CHECK(op.NumOutputs() == gRouter::DEFAULT_PORTS + 1);
    // The routing described ports that no longer exist, so it is forgotten with
    // them rather than reinterpreted onto the new shape.
    CHECK(op.ConnectionCount() == 0);
  }

  TEST_CASE("router: the routable outlets are ANY and the dump outlet is a list (#482)") {
    gRouter op;
    op.SetParams("2 3");
    for (int i = 0; i < op.OutletCount(); i++)
      CHECK(op.GetOutputType((unsigned int)i) == YSE::OUT_TYPE::ANY);
    CHECK(op.GetOutputType((unsigned int)op.OutletCount()) == YSE::OUT_TYPE::LIST);
  }

  TEST_CASE("router: the control inlet accepts a list and nothing else (#482)") {
    // Max scopes bang, int and float to "any but the leftmost inlet". Declined
    // outright rather than swallowed, so GetAcceptedTypes reports the real
    // contract.
    gRouter op;
    const unsigned int accepted = op.GetInlet(0)->GetAcceptedTypes();
    CHECK((accepted & YSE::PATCHER::IT_LIST) != 0u);
    CHECK((accepted & YSE::PATCHER::IT_INT) == 0u);
    CHECK((accepted & YSE::PATCHER::IT_FLOAT) == 0u);
    CHECK((accepted & YSE::PATCHER::IT_BANG) == 0u);
  }

  TEST_CASE("router: every routable inlet accepts all four message kinds (#482)") {
    gRouter op;
    op.SetParams("3 2");
    for (int i = 1; i <= op.InletCount(); i++) {
      const unsigned int accepted = op.GetInlet(i)->GetAcceptedTypes();
      CHECK((accepted & YSE::PATCHER::IT_BANG) != 0u);
      CHECK((accepted & YSE::PATCHER::IT_INT) != 0u);
      CHECK((accepted & YSE::PATCHER::IT_FLOAT) != 0u);
      CHECK((accepted & YSE::PATCHER::IT_LIST) != 0u);
    }
  }

  // ─── the crossbar ───────────────────────────────────────────────────────────

  TEST_CASE("router: nothing is routed until a connection is made (#482)") {
    Rig rig("2 3");
    rig.Int(0, 60);
    rig.Int(1, 61);
    rig.Bang(0);
    rig.List(0, "hello");
    CHECK(rig.Total() == 0);
  }

  TEST_CASE("router: 'connect' routes one inlet to one outlet (#482)") {
    Rig rig("2 3");
    rig.Control("connect 0 1");
    CHECK(rig.op.Connected(0, 1));
    CHECK(rig.op.ConnectionCount() == 1);
    // The connection message itself emits nothing.
    CHECK(rig.Total() == 0);

    rig.Int(0, 60);
    CHECK(rig.Log() == "b");
    CHECK(rig.sinks[1]->lastInt == 60);

    // And only that inlet: the crossbar is not a broadcast.
    rig.Reset();
    rig.Int(1, 61);
    CHECK(rig.Total() == 0);
  }

  TEST_CASE("router: 'connect 0 0' addresses the inlet right of the control inlet (#482)") {
    // The numbering counts the routable inlets from 0. Numbering from the
    // physical inlet 0 would make `connect 0 x` name the control inlet, which no
    // message can pass through — so this test is the one that fails on an off by
    // one, and an asymmetric shape keeps it from passing by luck.
    Rig rig("3 1");
    rig.Control("connect 0 0");
    rig.Int(0, 7);
    CHECK(rig.sinks[0]->lastInt == 7);
    CHECK(rig.Total() == 1);

    // The last routable inlet is InletCount()-1, and the one past it does not
    // exist even though a physical inlet with that index does not either.
    rig.Reset();
    rig.Control("connect 2 0");
    rig.Int(2, 9);
    CHECK(rig.sinks[0]->lastInt == 9);
    rig.Control("connect 3 0");
    CHECK(rig.op.ConnectionCount() == 2);
  }

  TEST_CASE("router: one inlet reaches several outlets, right to left (#482)") {
    Rig rig("2 3");
    rig.Control("connect 0 0");
    rig.Control("connect 0 2");
    rig.Int(0, 60);
    // Outlet 2 first, outlet 0 last — Max's universal order. Outlet 1 is not
    // connected and stays silent.
    CHECK(rig.Log() == "ca");
    CHECK(rig.sinks[0]->lastInt == 60);
    CHECK(rig.sinks[2]->lastInt == 60);
    CHECK(rig.sinks[1]->count == 0);

    rig.Reset();
    rig.Control("connect 0 1");
    rig.Int(0, 61);
    CHECK(rig.Log() == "cba");
  }

  TEST_CASE("router: several inlets reach the same outlet (#482)") {
    // The other half of many-to-many, and the half a selection-based
    // implementation cannot express at all.
    Rig rig("3 2");
    rig.Control("connect 0 1");
    rig.Control("connect 2 1");
    CHECK(rig.op.ConnectionCount() == 2);

    rig.Int(0, 10);
    CHECK(rig.sinks[1]->lastInt == 10);
    rig.Int(2, 30);
    CHECK(rig.sinks[1]->lastInt == 30);
    // Inlet 1 was never connected and the other two did not make it so.
    rig.Reset();
    rig.Int(1, 20);
    CHECK(rig.Total() == 0);
  }

  TEST_CASE("router: 'disconnect' removes exactly one cell (#482)") {
    Rig rig("2 2");
    rig.Control("connect 0 0");
    rig.Control("connect 0 1");
    rig.Control("connect 1 1");
    REQUIRE(rig.op.ConnectionCount() == 3);

    rig.Control("disconnect 0 1");
    CHECK(rig.op.Connected(0, 0));
    CHECK_FALSE(rig.op.Connected(0, 1));
    CHECK(rig.op.Connected(1, 1));
    CHECK(rig.op.ConnectionCount() == 2);

    // Disconnecting a cell that was already off is not an error, and is not a
    // toggle either.
    rig.Control("disconnect 0 1");
    CHECK_FALSE(rig.op.Connected(0, 1));
    CHECK(rig.op.ConnectionCount() == 2);
  }

  TEST_CASE(
      "router: the three-number list sets a connection, any non-zero state connecting (#482)") {
    Rig rig("2 2");
    rig.Control("0 1 1");
    CHECK(rig.op.Connected(0, 1));

    rig.Control("0 1 0");
    CHECK_FALSE(rig.op.Connected(0, 1));

    // Max says "a 0 or 1 specifying the state". Any non-zero connects, the
    // reading .decode takes of its own on/off inlets — a computed 2 meaning
    // "leave it alone" would surprise every patch.
    rig.Control("1 0 7");
    CHECK(rig.op.Connected(1, 0));
    rig.Control("1 0 -1");
    CHECK(rig.op.Connected(1, 0));
    rig.Control("1 0 0");
    CHECK_FALSE(rig.op.Connected(1, 0));

    // And it routes, so the list form is not merely bookkeeping.
    rig.Control("0 0 1");
    rig.Int(0, 42);
    CHECK(rig.sinks[0]->lastInt == 42);
  }

  TEST_CASE("router: a malformed or out-of-range connection message is ignored (#482)") {
    Rig rig("2 2");
    rig.Control("connect 0 0");
    REQUIRE(rig.op.ConnectionCount() == 1);

    // Too few, too many, and not numbers at all.
    rig.Control("connect 1");
    rig.Control("connect");
    rig.Control("connect 1 1 please");
    rig.Control("connect one two");
    rig.Control("disconnect 0");
    rig.Control("patch 1");

    // Two numbers is not the three-number list, and neither is four.
    rig.Control("1 1");
    rig.Control("1 1 1 1");

    // Indices the object has no port for, on either axis and either end.
    rig.Control("connect 9 0");
    rig.Control("connect 0 9");
    rig.Control("connect -1 0");
    rig.Control("connect 0 -1");
    rig.Control("9 0 1");

    // A message word this object does not have.
    rig.Control("shuffle 0 1");
    rig.Control("");

    // Nothing was added, nothing was removed, and nothing was emitted.
    CHECK(rig.op.ConnectionCount() == 1);
    CHECK(rig.op.Connected(0, 0));
    CHECK(rig.Total() == 0);
    CHECK(rig.dumps.lines.empty());
  }

  TEST_CASE("router: each message leaves as the kind it arrived as (#482)") {
    // The object switches; it does not convert. A crossbar that rewrote what
    // passed through it would not be transparent to swap in.
    Rig rig("1 1");
    rig.Control("connect 0 0");

    rig.Bang(0);
    CHECK(rig.sinks[0]->lastKind == OrderSink::BANG);

    rig.Int(0, 12);
    CHECK(rig.sinks[0]->lastKind == OrderSink::INT);
    CHECK(rig.sinks[0]->lastInt == 12);

    rig.Float(0, 2.5f);
    CHECK(rig.sinks[0]->lastKind == OrderSink::FLOAT);
    CHECK(rig.sinks[0]->lastFloat == doctest::Approx(2.5f));

    rig.List(0, "note 60 100");
    CHECK(rig.sinks[0]->lastKind == OrderSink::LIST);
    // Forwarded by text, character for character.
    CHECK(rig.sinks[0]->lastList == "note 60 100");

    CHECK(rig.Total() == 4);
  }

  TEST_CASE("router: message words are not interpreted on a routable inlet (#482)") {
    // The connection methods belong to the control inlet only. A message that
    // happens to read like one is data on any other inlet, which is what makes
    // the crossbar transparent to whatever passes through it.
    Rig rig("2 2");
    rig.Control("connect 0 0");

    rig.List(0, "connect 1 1");
    CHECK(rig.sinks[0]->lastList == "connect 1 1");
    CHECK_FALSE(rig.op.Connected(1, 1));

    rig.List(0, "clear");
    CHECK(rig.sinks[0]->lastList == "clear");
    CHECK(rig.op.Connected(0, 0));

    rig.List(0, "dump");
    CHECK(rig.sinks[0]->lastList == "dump");
    CHECK(rig.dumps.lines.empty());

    rig.List(0, "0 1 1");
    CHECK(rig.sinks[0]->lastList == "0 1 1");
    CHECK_FALSE(rig.op.Connected(0, 1));
  }

  // ─── patch ──────────────────────────────────────────────────────────────────

  TEST_CASE("router: 'patch' makes an outlet's source exclusive (#482)") {
    Rig rig("3 3");
    rig.Control("connect 0 1");
    rig.Control("connect 1 1");
    rig.Control("connect 2 1");
    REQUIRE(rig.op.ConnectionCount() == 3);

    rig.Control("patch 1 1");
    CHECK_FALSE(rig.op.Connected(0, 1));
    CHECK(rig.op.Connected(1, 1));
    CHECK_FALSE(rig.op.Connected(2, 1));
    CHECK(rig.op.ConnectionCount() == 1);
  }

  TEST_CASE("router: 'patch' leaves every other outlet alone (#482)") {
    // The half a `clear` followed by a `connect` gets wrong: it is a column
    // operation, not a reset.
    Rig rig("2 3");
    rig.Control("connect 0 0");
    rig.Control("connect 0 2");
    rig.Control("connect 1 0");
    REQUIRE(rig.op.ConnectionCount() == 3);

    rig.Control("patch 1 2");
    // Column 2 is now inlet 1's alone...
    CHECK_FALSE(rig.op.Connected(0, 2));
    CHECK(rig.op.Connected(1, 2));
    // ...and columns 0 and 1 are untouched.
    CHECK(rig.op.Connected(0, 0));
    CHECK(rig.op.Connected(1, 0));
    CHECK_FALSE(rig.op.Connected(0, 1));
    CHECK(rig.op.ConnectionCount() == 3);
  }

  TEST_CASE("router: 'patch' on an already exclusive outlet is idempotent (#482)") {
    Rig rig("2 2");
    rig.Control("patch 0 0");
    CHECK(rig.op.Connected(0, 0));
    rig.Control("patch 0 0");
    CHECK(rig.op.Connected(0, 0));
    CHECK(rig.op.ConnectionCount() == 1);
  }

  TEST_CASE("router: an out-of-range 'patch' changes nothing (#482)") {
    // In particular it must not clear the column: an index the object has no
    // port for is a message with nothing to address, not a request to blank
    // something.
    Rig rig("2 2");
    rig.Control("connect 0 0");
    rig.Control("connect 1 0");
    REQUIRE(rig.op.ConnectionCount() == 2);

    rig.Control("patch 9 0");
    rig.Control("patch -1 0");
    rig.Control("patch 0 9");
    CHECK(rig.op.Connected(0, 0));
    CHECK(rig.op.Connected(1, 0));
    CHECK(rig.op.ConnectionCount() == 2);
  }

  // ─── clear ──────────────────────────────────────────────────────────────────

  TEST_CASE("router: 'clear' disconnects everything (#482)") {
    Rig rig("3 3");
    rig.Control("connect 0 0");
    rig.Control("connect 1 1");
    rig.Control("connect 2 2");
    REQUIRE(rig.op.ConnectionCount() == 3);

    rig.Control("clear");
    CHECK(rig.op.ConnectionCount() == 0);
    rig.Int(0, 1);
    rig.Int(1, 2);
    rig.Int(2, 3);
    CHECK(rig.Total() == 0);

    // Max types `clear` as taking an argument list, so trailing text is still
    // the message.
    rig.Control("connect 0 0");
    rig.Control("clear all");
    CHECK(rig.op.ConnectionCount() == 0);
  }

  // ─── dump ───────────────────────────────────────────────────────────────────

  TEST_CASE("router: 'dump' sends every cell out the rightmost outlet (#482)") {
    Rig rig("2 2");
    rig.Control("connect 0 0");
    rig.Control("connect 1 0");

    rig.Control("dump");
    // Every cell, ascending by inlet then by outlet — so the whole matrix is
    // recoverable by a patch and not only readable by a person.
    CHECK(rig.dumps.lines.size() == 4);
    CHECK(rig.dumps.Joined() == "0 0 1 | 0 1 0 | 1 0 1 | 1 1 0");
    // And nothing went out a routable outlet.
    CHECK(rig.Total() == 0);
  }

  TEST_CASE("router: 'dump' reports an asymmetric matrix in full (#482)") {
    Rig rig("3 2");
    rig.Control("connect 2 1");
    rig.Control("dump");
    CHECK(rig.dumps.lines.size() == 6);
    CHECK(rig.dumps.Joined() == "0 0 0 | 0 1 0 | 1 0 0 | 1 1 0 | 2 0 0 | 2 1 1");
  }

  TEST_CASE("router: a dump fed back into the control inlet does not nest (#482)") {
    // The dump outlet is an ordinary outlet, so this is one patch cord away. The
    // send-depth guard bounds the depth of an unguarded nesting, but the breadth
    // is inlets * outlets per level and multiplies, so the count is the
    // assertion.
    gRouter op;
    op.SetParams("2 2");

    ControlFeedbackSink echo;
    echo.target = &op;
    echo.message = "dump";
    echo.budget = 64;

    const int dumpOutlet = op.OutletCount();
    op.ConnectOutlet(echo.GetInlet(0), dumpOutlet);
    echo.ConnectInlet(op.GetOutlet(dumpOutlet), 0);

    op.GetInlet(0)->SetList("connect 0 0", YSE::T_GUI);
    op.GetInlet(0)->SetList("dump", YSE::T_GUI);

    // Exactly one dump: four cells, four lines.
    CHECK(echo.count == 4);
    // The re-entrant dumps were ignored, not merely depth-limited, so the
    // budget is untouched beyond the four lines that were actually delivered.
    CHECK(echo.budget == 60);
  }

  TEST_CASE("router: 'print' is accepted and does nothing (#482)") {
    // Max prints to the Max Console. This patcher is headless and has none, and
    // a message handler must not log — so it is silent rather than routed as the
    // symbol it also is.
    Rig rig("2 2");
    rig.Control("connect 0 0");
    rig.Control("print");
    rig.Control("print matrix");
    CHECK(rig.op.ConnectionCount() == 1);
    CHECK(rig.Total() == 0);
    CHECK(rig.dumps.lines.empty());
  }

  // ─── re-entrancy ────────────────────────────────────────────────────────────

  TEST_CASE("router: a fan-out survives the routing being rewritten mid-send (#482)") {
    // Sends are synchronous, so the sink on the first outlet to fire re-enters
    // the control inlet *inside* the burst. Reading the matrix per outlet would
    // deliver this one message under two different routings; the row is
    // snapshotted before the first send, so it does not.
    gRouter op;
    op.SetParams("1 3");

    std::vector<char> order;

    ControlFeedbackSink first; // outlet 2, the first to fire
    first.tag = 'c';
    first.log = &order;
    first.target = &op;
    first.message = "clear";
    first.budget = 1;

    OrderSink second;
    second.tag = 'b';
    second.log = &order;

    OrderSink third;
    third.tag = 'a';
    third.log = &order;

    op.ConnectOutlet(third.GetInlet(0), 0);
    third.ConnectInlet(op.GetOutlet(0), 0);
    op.ConnectOutlet(second.GetInlet(0), 1);
    second.ConnectInlet(op.GetOutlet(1), 0);
    op.ConnectOutlet(first.GetInlet(0), 2);
    first.ConnectInlet(op.GetOutlet(2), 0);

    op.GetInlet(0)->SetList("connect 0 0", YSE::T_GUI);
    op.GetInlet(0)->SetList("connect 0 1", YSE::T_GUI);
    op.GetInlet(0)->SetList("connect 0 2", YSE::T_GUI);

    op.GetInlet(1)->SetInt(60, YSE::T_GUI);

    // All three fired, in Max's order, even though the first of them wiped the
    // matrix on its way.
    CHECK(std::string(order.begin(), order.end()) == "cba");
    CHECK(second.lastInt == 60);
    CHECK(third.lastInt == 60);
    // And the rewrite did happen — it just did not take effect until the next
    // message.
    CHECK(op.ConnectionCount() == 0);

    order.clear();
    op.GetInlet(1)->SetInt(61, YSE::T_GUI);
    CHECK(order.empty());
  }

  // ─── real-time / graph ──────────────────────────────────────────────────────

  TEST_CASE("router: Calculate() emits nothing (#482)") {
    // The object is driven by its inlets. An emitting Calculate() would route a
    // message no patch sent.
    Rig rig("2 2");
    rig.Control("connect 0 0");
    rig.Control("connect 1 1");
    rig.Reset();
    for (int i = 0; i < 8; i++)
      rig.op.Calculate(YSE::T_DSP);
    CHECK(rig.Total() == 0);
    CHECK(rig.dumps.lines.empty());
  }

  TEST_CASE("router: survives a DumpJSON / ParseJSON round trip (#482)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_ROUTER);
    REQUIRE(h != nullptr);
    h->SetParams("3 4");
    const std::string json = src.DumpJSON();
    CHECK(json.find(".router") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".router"));
    CHECK(copy->GetParams() == std::string("3 4"));
    // The shape has to come back with it, or the saved patch cords land on the
    // wrong ports.
    CHECK(copy->GetInputs() == 4);
    CHECK(copy->GetOutputs() == 5);

    // And the reloaded object is a working crossbar, not merely the right shape.
    ListSink sink;
    YSE::pHandle sinkHandle(&sink);
    loaded.Connect(copy, 2, &sinkHandle, 0);
    copy->SetListData(0, "connect 1 2");
    copy->SetListData(2, "note 60 100");
    CHECK(sink.gotList);
    CHECK(sink.received == "note 60 100");
  }

  TEST_CASE("router: the connections are run-time state, not parameters (#482)") {
    // The creation arguments are what a saved patch carries, exactly as
    // .spray's `offset` and .cycle's `thresh` are run-time state. `dump` is how
    // a patch reads the routing back out.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ROUTER, "2 2");
    REQUIRE(h != nullptr);
    h->SetListData(0, "connect 0 1");
    CHECK(h->GetParams() == std::string("2 2"));
  }

  // ─── end to end ─────────────────────────────────────────────────────────────

  TEST_CASE("router: rewires a live patch from a message, in a real patcher (#482)") {
    // The headline use, end to end through real objects: one source, two
    // destinations, and the destination changed by a message rather than by
    // re-patching. Each path adds a different constant, so a sink's value is
    // only right if the value went out the outlet it was routed to.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* router = p.CreateObject(YSE::OBJ::G_ROUTER, "2 2");
    YSE::pHandle* addHundred = p.CreateObject(YSE::OBJ::G_ADD, "100");
    YSE::pHandle* addThousand = p.CreateObject(YSE::OBJ::G_ADD, "1000");
    REQUIRE(router != nullptr);
    REQUIRE(addHundred != nullptr);
    REQUIRE(addThousand != nullptr);

    FloatSink one;
    FloatSink two;
    YSE::pHandle oneHandle(&one);
    YSE::pHandle twoHandle(&two);

    p.Connect(router, 0, addHundred, 0);
    p.Connect(router, 1, addThousand, 0);
    p.Connect(addHundred, 0, &oneHandle, 0);
    p.Connect(addThousand, 0, &twoHandle, 0);

    // Nothing is wired inside the crossbar yet, so the patch is inert.
    router->SetIntData(1, 5);
    CHECK_FALSE(one.gotFloat);
    CHECK_FALSE(two.gotFloat);

    // One message, and the source now reaches the first destination.
    router->SetListData(0, "connect 0 0");
    router->SetIntData(1, 5);
    CHECK(one.received == doctest::Approx(105.f));
    CHECK_FALSE(two.gotFloat);

    // One more message moves it — no cord touched, no object replaced.
    router->SetListData(0, "patch 0 1");
    router->SetListData(0, "disconnect 0 0");
    one.gotFloat = false;
    router->SetIntData(1, 5);
    CHECK(two.received == doctest::Approx(1005.f));
    CHECK_FALSE(one.gotFloat);

    // And both at once, which is the thing a .gate in the same place could not
    // do.
    router->SetListData(0, "connect 0 0");
    one.gotFloat = false;
    two.gotFloat = false;
    router->SetIntData(1, 7);
    CHECK(one.received == doctest::Approx(107.f));
    CHECK(two.received == doctest::Approx(1007.f));

    // The second source reaches the same destination as the first — the other
    // half of many-to-many, through real cords.
    router->SetListData(0, "connect 1 1");
    two.gotFloat = false;
    router->SetIntData(2, 9);
    CHECK(two.received == doctest::Approx(1009.f));
  }

} // TEST_SUITE("patcher")

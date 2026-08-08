// Tests for .matrix (issue #484) — a message crossbar whose cells carry a gain
// rather than a switch.
//
// .matrix shares .router's whole control protocol, so the tests that pin the
// *crossbar* — the leftmost inlet routing nothing, the numbering counting
// routable inlets from 0, many-to-many, right-to-left firing, the row
// snapshotted before the first send, the dump not nesting — are the same
// obligations and are asserted here again rather than assumed from #482: the
// two objects are separate code and a shared doc does not make a shared bug
// impossible.
//
// What is *only* here is the gain, and six rules carry it. Each is one a
// plausible implementation gets wrong:
//   - **connection and gain are one value.** Max: "a non-zero gain adds a
//     connection with the designated gain; a gain of 0 deletes the connection
//     if it exists." An implementation with a separate connected flag passes
//     every routing test and then disagrees with itself the first time a cell
//     is set to 0 — so `disconnect`, `connect x y 0` and the list `x y 0` are
//     all asserted to be the same operation.
//   - **unity is transparent.** A gain of exactly 1 must forward the message
//     untouched: an int stays an int, a bang stays a bang, a list arrives
//     character for character. An implementation that always multiplied would
//     turn every int into a float and every list into re-spelled floats, and
//     would still pass a test that only looked at numeric values.
//   - **any other gain sends a float, even from an int.** Rounding the product
//     back to an int would discard the scaling itself: 1 at a gain of 0.5 is 0,
//     and a modulation matrix whose fine settings all read zero looks like a
//     dead object rather than a wrong one.
//   - **a bang is forwarded at any gain.** It carries no value, so there is
//     nothing to scale and nothing that can make it not arrive.
//   - **a list is scaled element-wise only when every item is a number.**
//     `note 60 100` must keep its selector *and* its numbers — an implementation
//     that scaled the numeric tokens of a symbol-led message would silently
//     transpose every note it routed.
//   - **the outlets do not sum.** Max says so against matrix~. Two inlets
//     reaching one outlet produce two messages, not one carrying their total.
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
#include "patcher/genericObjects/gMatrix.h"
#include "patcher/sinks.hpp"

namespace {
  using TestHelpers::FloatSink;
  using TestHelpers::OrderSink;
  using YSE::PATCHER::gMatrix;

  // Records every list it is given, not just the last one. The dump outlet
  // emits one list per connected cell, so "what did the whole dump say, and how
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

  // A sink that pushes a control message back into the matrix when it is hit, a
  // bounded number of times. A crossbar whose outlet is wired to its own
  // control inlet is the re-entrancy case, and it needs a receiver that stops
  // feeding rather than a bare patch cord, which would not terminate.
  struct ControlFeedbackSink : YSE::PATCHER::pObject {
    gMatrix* target = nullptr;
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
    gMatrix op;
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

  TEST_CASE("matrix: creatable through the registry (#484)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_MATRIX, "3 4");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".matrix"));
    // One more of each than the arguments ask for: the control inlet on the
    // left and the dumpconnections outlet on the right.
    CHECK(h->GetInputs() == 4);
    CHECK(h->GetOutputs() == 5);
  }

  TEST_CASE("matrix: listed by pRegistry::AllNames (#484)") {
    auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(YSE::OBJ::G_MATRIX)) != names.end());
  }

  TEST_CASE("matrix: two of each and a unity default gain with no argument (#484)") {
    // Max states both counts outright — "if not present the default is 2" —
    // and states no default for the gain, where 1 is the only value that makes
    // a bare `connect` mean what the word says.
    gMatrix op;
    CHECK(op.InletCount() == 2);
    CHECK(op.OutletCount() == 2);
    CHECK(op.NumInputs() == 3);
    CHECK(op.NumOutputs() == 3);
    CHECK(op.DefaultGain() == doctest::Approx(1.f));
    // And nothing is wired to begin with.
    CHECK(op.ConnectionCount() == 0);
  }

  TEST_CASE("matrix: a lone argument sets the inputs and leaves the rest defaulted (#484)") {
    gMatrix op;
    op.SetParams("5");
    CHECK(op.InletCount() == 5);
    CHECK(op.OutletCount() == 2);
    CHECK(op.DefaultGain() == doctest::Approx(1.f));
  }

  TEST_CASE("matrix: the third argument is the gain a bare 'connect' uses (#484)") {
    gMatrix op;
    op.SetParams("2 2 0.25");
    CHECK(op.DefaultGain() == doctest::Approx(0.25f));

    op.GetInlet(0)->SetList("connect 0 1", YSE::T_GUI);
    CHECK(op.Gain(0, 1) == doctest::Approx(0.25f));

    // An explicit gain still wins over it — the default is only the fallback.
    op.GetInlet(0)->SetList("connect 1 0 4", YSE::T_GUI);
    CHECK(op.Gain(1, 0) == doctest::Approx(4.f));

    // The gain is a coefficient, not a count, so it is taken as written rather
    // than clamped: Max gives it no documented range.
    gMatrix negative;
    negative.SetParams("2 2 -1");
    CHECK(negative.DefaultGain() == doctest::Approx(-1.f));

    // And a third argument that is not a number leaves unity in place.
    gMatrix nonsense;
    nonsense.SetParams("2 2 loud");
    CHECK(nonsense.DefaultGain() == doctest::Approx(1.f));
  }

  TEST_CASE("matrix: both counts are clamped to 1-256 independently (#484)") {
    gMatrix low;
    low.SetParams("0 0");
    CHECK(low.InletCount() == 1);
    CHECK(low.OutletCount() == 1);

    gMatrix negative;
    negative.SetParams("-7 -3");
    CHECK(negative.InletCount() == 1);
    CHECK(negative.OutletCount() == 1);

    gMatrix high;
    high.SetParams("9999 9999");
    CHECK(high.InletCount() == gMatrix::MAX_PORTS);
    CHECK(high.OutletCount() == gMatrix::MAX_PORTS);

    // Clamped one at a time, not both to the same value.
    gMatrix mixed;
    mixed.SetParams("9999 3");
    CHECK(mixed.InletCount() == gMatrix::MAX_PORTS);
    CHECK(mixed.OutletCount() == 3);

    gMatrix fractional;
    fractional.SetParams("3.9 2.9");
    CHECK(fractional.InletCount() == 3);
    CHECK(fractional.OutletCount() == 2);

    gMatrix nonsense;
    nonsense.SetParams("wide tall");
    CHECK(nonsense.InletCount() == gMatrix::DEFAULT_PORTS);
    CHECK(nonsense.OutletCount() == gMatrix::DEFAULT_PORTS);
  }

  TEST_CASE("matrix: the table has one cell per inlet/outlet pair (#484)") {
    gMatrix op;
    op.SetParams("3 4");
    for (int in = 0; in < 3; in++) {
      for (int out = 0; out < 4; out++) {
        CHECK(op.Gain(in, out) == doctest::Approx(0.f));
        CHECK_FALSE(op.Connected(in, out));
      }
    }
    // And no cell exists outside the shape, whichever axis is overrun.
    CHECK_FALSE(op.Connected(3, 0));
    CHECK_FALSE(op.Connected(0, 4));
    CHECK_FALSE(op.Connected(-1, 0));
    CHECK_FALSE(op.Connected(0, -1));
  }

  TEST_CASE("matrix: SetParams(\"\") returns the object to the no-argument shape (#484)") {
    gMatrix op;
    op.SetParams("6 7 0.5");
    op.GetInlet(0)->SetList("connect 0 0", YSE::T_GUI);
    REQUIRE(op.InletCount() == 6);
    REQUIRE(op.Connected(0, 0));

    op.SetParams("");
    CHECK(op.InletCount() == gMatrix::DEFAULT_PORTS);
    CHECK(op.OutletCount() == gMatrix::DEFAULT_PORTS);
    CHECK(op.NumInputs() == gMatrix::DEFAULT_PORTS + 1);
    CHECK(op.NumOutputs() == gMatrix::DEFAULT_PORTS + 1);
    // The default gain comes back with the shape...
    CHECK(op.DefaultGain() == doctest::Approx(1.f));
    // ...and the routing described ports that no longer exist, so it is
    // forgotten with them rather than reinterpreted onto the new shape.
    CHECK(op.ConnectionCount() == 0);
  }

  TEST_CASE("matrix: the routable outlets are ANY and the dump outlet is a list (#484)") {
    gMatrix op;
    op.SetParams("2 3");
    for (int i = 0; i < op.OutletCount(); i++)
      CHECK(op.GetOutputType((unsigned int)i) == YSE::OUT_TYPE::ANY);
    CHECK(op.GetOutputType((unsigned int)op.OutletCount()) == YSE::OUT_TYPE::LIST);
  }

  TEST_CASE("matrix: the control inlet accepts a list and nothing else (#484)") {
    // Max scopes every connection message to "In left inlet" and routes nothing
    // from it. Declined outright rather than swallowed, so GetAcceptedTypes
    // reports the real contract.
    gMatrix op;
    const unsigned int accepted = op.GetInlet(0)->GetAcceptedTypes();
    CHECK((accepted & YSE::PATCHER::IT_LIST) != 0u);
    CHECK((accepted & YSE::PATCHER::IT_INT) == 0u);
    CHECK((accepted & YSE::PATCHER::IT_FLOAT) == 0u);
    CHECK((accepted & YSE::PATCHER::IT_BANG) == 0u);
  }

  TEST_CASE("matrix: every routable inlet accepts all four message kinds (#484)") {
    gMatrix op;
    op.SetParams("3 2");
    for (int i = 1; i <= op.InletCount(); i++) {
      const unsigned int accepted = op.GetInlet(i)->GetAcceptedTypes();
      CHECK((accepted & YSE::PATCHER::IT_BANG) != 0u);
      CHECK((accepted & YSE::PATCHER::IT_INT) != 0u);
      CHECK((accepted & YSE::PATCHER::IT_FLOAT) != 0u);
      CHECK((accepted & YSE::PATCHER::IT_LIST) != 0u);
    }
  }

  // ─── connection and gain are one value ──────────────────────────────────────

  TEST_CASE("matrix: nothing is routed until a connection is made (#484)") {
    Rig rig("2 3");
    rig.Int(0, 60);
    rig.Int(1, 61);
    rig.Bang(0);
    rig.List(0, "hello");
    CHECK(rig.Total() == 0);
  }

  TEST_CASE("matrix: a non-zero gain connects and a zero gain disconnects (#484)") {
    // Max: "A non-zero gain adds a connection with the designated gain; a gain
    // of 0 deletes the connection if it exists." So there is no
    // connected-but-unweighted state to disagree with the gain, and the three
    // spellings that reach zero are one operation.
    Rig rig("2 2");

    rig.Control("0 1 0.5");
    CHECK(rig.op.Gain(0, 1) == doctest::Approx(0.5f));
    CHECK(rig.op.Connected(0, 1));
    CHECK(rig.op.ConnectionCount() == 1);

    rig.Control("0 1 0");
    CHECK(rig.op.Gain(0, 1) == doctest::Approx(0.f));
    CHECK_FALSE(rig.op.Connected(0, 1));
    CHECK(rig.op.ConnectionCount() == 0);

    // `connect x y 0` reaches the same place — the gain is the connection, so a
    // connect at zero is a disconnect and not a connect at silence.
    rig.Control("connect 1 0 2");
    REQUIRE(rig.op.Connected(1, 0));
    rig.Control("connect 1 0 0");
    CHECK_FALSE(rig.op.Connected(1, 0));

    // And so does `disconnect`.
    rig.Control("connect 1 1 3");
    REQUIRE(rig.op.Gain(1, 1) == doctest::Approx(3.f));
    rig.Control("disconnect 1 1");
    CHECK(rig.op.Gain(1, 1) == doctest::Approx(0.f));

    // Disconnecting a cell that was already off is not an error, and is not a
    // toggle either.
    rig.Control("disconnect 1 1");
    CHECK_FALSE(rig.op.Connected(1, 1));
    CHECK(rig.op.ConnectionCount() == 0);
    // None of it emitted anything.
    CHECK(rig.Total() == 0);
  }

  TEST_CASE("matrix: 'connect 0 0' addresses the inlet right of the control inlet (#484)") {
    // Max numbers the routed inlets "starting at 0 for the object's second
    // inlet from left". Numbering from the physical inlet 0 would make
    // `connect 0 x` name the control inlet, which no message can pass through —
    // so this is the test that fails on an off by one, and the asymmetric shape
    // keeps it from passing by luck.
    Rig rig("3 1");
    rig.Control("connect 0 0");
    rig.Int(0, 7);
    CHECK(rig.sinks[0]->lastInt == 7);
    CHECK(rig.Total() == 1);

    rig.Reset();
    rig.Control("connect 2 0");
    rig.Int(2, 9);
    CHECK(rig.sinks[0]->lastInt == 9);
    // The inlet past the last routable one does not exist.
    rig.Control("connect 3 0");
    CHECK(rig.op.ConnectionCount() == 2);
  }

  TEST_CASE("matrix: a malformed or out-of-range connection message is ignored (#484)") {
    Rig rig("2 2");
    rig.Control("connect 0 0 2");
    REQUIRE(rig.op.ConnectionCount() == 1);

    // Too few, too many, and not numbers at all.
    rig.Control("connect 1");
    rig.Control("connect");
    rig.Control("connect 1 1 2 please");
    rig.Control("connect one two");
    rig.Control("disconnect 0");
    // Max documents no gain argument on `disconnect`, so one is not a message
    // this object has.
    rig.Control("disconnect 0 0 2");

    // Two numbers name a cell without saying what to do with it, and four is
    // not the message either.
    rig.Control("1 1");
    rig.Control("1 1 1 1");

    // Indices the object has no port for, on either axis and either end.
    rig.Control("connect 9 0");
    rig.Control("connect 0 9");
    rig.Control("connect -1 0");
    rig.Control("connect 0 -1");
    rig.Control("9 0 1");

    // A message word this object does not have, and Max's own `clear all`,
    // which it does not document either.
    rig.Control("shuffle 0 1");
    rig.Control("clear all");
    rig.Control("");

    // Nothing was added, nothing was removed, and nothing was emitted.
    CHECK(rig.op.ConnectionCount() == 1);
    CHECK(rig.op.Gain(0, 0) == doctest::Approx(2.f));
    CHECK(rig.Total() == 0);
    CHECK(rig.dumps.lines.empty());
  }

  TEST_CASE("matrix: 'clear' removes every connection (#484)") {
    Rig rig("3 3");
    rig.Control("connect 0 0 2");
    rig.Control("connect 1 1 0.5");
    rig.Control("connect 2 2");
    REQUIRE(rig.op.ConnectionCount() == 3);

    rig.Control("clear");
    CHECK(rig.op.ConnectionCount() == 0);
    rig.Int(0, 1);
    rig.Int(1, 2);
    rig.Int(2, 3);
    CHECK(rig.Total() == 0);
  }

  // ─── the gain ───────────────────────────────────────────────────────────────

  TEST_CASE("matrix: a unity gain forwards every kind untouched (#484)") {
    // The rule that makes .matrix at its default gain interchangeable with
    // .router, and the one a patch depends on wherever the value is a note
    // number rather than a quantity. An implementation that always multiplied
    // would turn every int into a float here.
    Rig rig("1 1");
    rig.Control("connect 0 0");
    REQUIRE(rig.op.Gain(0, 0) == doctest::Approx(1.f));

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

    // And a numeric list is not re-spelled either — `60 61`, not `60. 61.`.
    rig.List(0, "60 61");
    CHECK(rig.sinks[0]->lastList == "60 61");

    CHECK(rig.Total() == 5);
  }

  TEST_CASE("matrix: any other gain multiplies, and an int leaves as a float (#484)") {
    // Rounding the product back to an int would discard the scaling itself: 1
    // at a gain of 0.5 is 0, and a modulation matrix whose fine settings all
    // read zero looks like a dead object rather than a wrong one.
    Rig rig("1 1");
    rig.Control("connect 0 0 0.5");

    rig.Int(0, 7);
    CHECK(rig.sinks[0]->lastKind == OrderSink::FLOAT);
    CHECK(rig.sinks[0]->lastFloat == doctest::Approx(3.5f));

    // Including the case an int-preserving implementation would round to zero.
    rig.Int(0, 1);
    CHECK(rig.sinks[0]->lastKind == OrderSink::FLOAT);
    CHECK(rig.sinks[0]->lastFloat == doctest::Approx(0.5f));

    rig.Float(0, 3.f);
    CHECK(rig.sinks[0]->lastFloat == doctest::Approx(1.5f));

    // A gain above unity and a negative one are ordinary coefficients.
    rig.Control("connect 0 0 -2");
    rig.Int(0, 5);
    CHECK(rig.sinks[0]->lastFloat == doctest::Approx(-10.f));
  }

  TEST_CASE("matrix: a bang is forwarded at any gain (#484)") {
    // It carries no value, so there is nothing for a gain to act on and
    // nothing that can make it not arrive.
    Rig rig("1 1");

    rig.Control("connect 0 0 0.01");
    rig.Bang(0);
    CHECK(rig.sinks[0]->lastKind == OrderSink::BANG);
    CHECK(rig.sinks[0]->count == 1);

    rig.Control("connect 0 0 -5");
    rig.Bang(0);
    CHECK(rig.sinks[0]->lastKind == OrderSink::BANG);
    CHECK(rig.sinks[0]->count == 2);

    // A disconnected cell still stops it, since a gain of 0 is no connection.
    rig.Control("connect 0 0 0");
    rig.Bang(0);
    CHECK(rig.sinks[0]->count == 2);
  }

  TEST_CASE("matrix: a list of numbers is scaled element-wise (#484)") {
    Rig rig("1 1");
    rig.Control("connect 0 0 2");

    rig.List(0, "60 61");
    CHECK(rig.sinks[0]->lastKind == OrderSink::LIST);
    // Floats out, spelled the way the patcher spells floats.
    CHECK(rig.sinks[0]->lastList == "120. 122.");

    rig.Control("connect 0 0 0.5");
    rig.List(0, "60 61");
    CHECK(rig.sinks[0]->lastList == "30. 30.5");

    // A one-item list is still a list, not a number.
    rig.List(0, "8");
    CHECK(rig.sinks[0]->lastKind == OrderSink::LIST);
    CHECK(rig.sinks[0]->lastList == "4.");
  }

  TEST_CASE("matrix: a list that is not all numbers is forwarded unchanged (#484)") {
    // Max's scalemode rule read from its default position: scaling applies to
    // numbers, and scalemode exists to *extend* it to "numeric arguments to
    // messages that do not start with a number", off unless asked for. An
    // implementation that scaled the numeric tokens of a symbol-led message
    // would silently transpose every note it routed.
    Rig rig("1 1");
    rig.Control("connect 0 0 2");

    rig.List(0, "note 60 100");
    CHECK(rig.sinks[0]->lastList == "note 60 100");

    rig.List(0, "set 74");
    CHECK(rig.sinks[0]->lastList == "set 74");

    // Including a list that merely *starts* with a number but is not wholly
    // numbers — a partial rescale would be worse than none.
    rig.List(0, "60 loud");
    CHECK(rig.sinks[0]->lastList == "60 loud");
  }

  TEST_CASE("matrix: a list longer than the bound is forwarded unscaled, not truncated (#484)") {
    // The buffer a scaled list is built into is sized once on the control
    // thread, as .vexpr's is. A crossbar that shortened what passed through it
    // would be a worse failure than one that did not weight it.
    Rig rig("1 1");
    rig.Control("connect 0 0 2");

    std::string bound;
    for (int i = 0; i < gMatrix::MAX_LIST_VALUES; i++) {
      if (i > 0) bound += ' ';
      bound += '1';
    }
    rig.List(0, bound);
    // Exactly at the bound it is still scaled.
    CHECK(rig.sinks[0]->lastList.find("2.") == 0);

    std::string over = bound + " 1";
    rig.List(0, over);
    CHECK(rig.sinks[0]->lastList == over);
  }

  TEST_CASE("matrix: each cell scales independently (#484)") {
    // One message, two destinations, two different gains — the thing a crossbar
    // of switches with one multiplier behind it could not do in one object.
    Rig rig("1 3");
    rig.Control("connect 0 0 1");
    rig.Control("connect 0 1 0.5");
    rig.Control("connect 0 2 3");

    rig.Int(0, 10);
    // Right to left, Max's universal order.
    CHECK(rig.Log() == "cba");
    CHECK(rig.sinks[0]->lastKind == OrderSink::INT);
    CHECK(rig.sinks[0]->lastInt == 10);
    CHECK(rig.sinks[1]->lastFloat == doctest::Approx(5.f));
    CHECK(rig.sinks[2]->lastFloat == doctest::Approx(30.f));
  }

  TEST_CASE("matrix: the outlets do not add the values of multiple inputs (#484)") {
    // Max says so explicitly against matrix~. Two inlets reaching one outlet
    // produce two messages, one per event, not one carrying their total —
    // there is no moment at which two events are simultaneous.
    Rig rig("2 1");
    rig.Control("connect 0 0 1");
    rig.Control("connect 1 0 1");

    rig.Int(0, 10);
    CHECK(rig.sinks[0]->lastInt == 10);
    CHECK(rig.sinks[0]->count == 1);

    rig.Int(1, 5);
    CHECK(rig.sinks[0]->lastInt == 5);
    CHECK(rig.sinks[0]->count == 2);
  }

  TEST_CASE("matrix: message words are not interpreted on a routable inlet (#484)") {
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

    rig.List(0, "dumpconnections");
    CHECK(rig.sinks[0]->lastList == "dumpconnections");
    CHECK(rig.dumps.lines.empty());

    // And the bare list form is data too, so a routed `0 1 1` cannot rewire the
    // object it is passing through.
    rig.List(0, "0 1 1");
    CHECK(rig.sinks[0]->lastList == "0 1 1");
    CHECK_FALSE(rig.op.Connected(0, 1));
  }

  // ─── dumpconnections ────────────────────────────────────────────────────────

  TEST_CASE("matrix: 'dumpconnections' reports only the connected cells (#484)") {
    // Max: "outputs a ... message listing all current connections." *Current* —
    // which is where this parts company with .router, whose Max wording is "the
    // state of the switching matrix" and which therefore dumps the zeros too.
    // At 256 by 256 the full table would be 65536 lines.
    Rig rig("3 2");
    rig.Control("connect 0 1 0.5");
    rig.Control("connect 2 0 2");

    rig.Control("dumpconnections");
    CHECK(rig.dumps.lines.size() == 2);
    // Ascending by inlet then by outlet, so the order is a fact a patch can
    // rely on rather than an accident of iteration.
    CHECK(rig.dumps.Joined() == "0 1 0.5 | 2 0 2.");
    // And nothing went out a routable outlet.
    CHECK(rig.Total() == 0);

    // An empty matrix dumps nothing at all, rather than a table of zeros.
    rig.Reset();
    rig.Control("clear");
    rig.Control("dumpconnections");
    CHECK(rig.dumps.lines.empty());
  }

  TEST_CASE("matrix: a dump line re-creates the connection it describes (#484)") {
    // The line is the same three items in the same order that *set* a cell, so
    // storing a routing is storing the dump and restoring one is `clear`
    // followed by replaying it. That is the only way state living in messages
    // rather than in the saved file can be saved at all.
    Rig source("3 3");
    source.Control("connect 0 2 0.25");
    source.Control("connect 1 1");
    source.Control("connect 2 0 -3");
    source.Control("dumpconnections");
    REQUIRE(source.dumps.lines.size() == 3);

    Rig restored("3 3");
    for (const std::string& line : source.dumps.lines)
      restored.Control(line);

    CHECK(restored.op.ConnectionCount() == 3);
    CHECK(restored.op.Gain(0, 2) == doctest::Approx(0.25f));
    CHECK(restored.op.Gain(1, 1) == doctest::Approx(1.f));
    CHECK(restored.op.Gain(2, 0) == doctest::Approx(-3.f));

    // And the restored object dumps the same thing the original did.
    restored.Control("dumpconnections");
    CHECK(restored.dumps.Joined() == source.dumps.Joined());
  }

  TEST_CASE("matrix: a dump fed back into the control inlet does not nest (#484)") {
    // The dump outlet is an ordinary outlet, so this is one patch cord away.
    // The send-depth guard bounds the depth of an unguarded nesting, but the
    // breadth is the connection count per level and multiplies, so the count is
    // the assertion.
    gMatrix op;
    op.SetParams("2 2");

    ControlFeedbackSink echo;
    echo.target = &op;
    echo.message = "dumpconnections";
    echo.budget = 64;

    const int dumpOutlet = op.OutletCount();
    op.ConnectOutlet(echo.GetInlet(0), dumpOutlet);
    echo.ConnectInlet(op.GetOutlet(dumpOutlet), 0);

    op.GetInlet(0)->SetList("connect 0 0", YSE::T_GUI);
    op.GetInlet(0)->SetList("connect 1 1", YSE::T_GUI);
    op.GetInlet(0)->SetList("dumpconnections", YSE::T_GUI);

    // Exactly one dump: two connections, two lines.
    CHECK(echo.count == 2);
    // The re-entrant dumps were ignored, not merely depth-limited, so the
    // budget is untouched beyond the two lines actually delivered.
    CHECK(echo.budget == 62);
  }

  TEST_CASE("matrix: 'dictionary' is accepted and does nothing (#484)") {
    // Max replaces every connection with the ones in a named dictionary object.
    // This patcher has none and, being headless, is not going to grow them —
    // so it is silent rather than routed as the symbol it also is, and
    // `dumpconnections` is the form of the same conversation a patch can act
    // on.
    Rig rig("2 2");
    rig.Control("connect 0 0 2");
    rig.Control("dictionary routing");
    rig.Control("dictionary");
    CHECK(rig.op.ConnectionCount() == 1);
    CHECK(rig.op.Gain(0, 0) == doctest::Approx(2.f));
    CHECK(rig.Total() == 0);
    CHECK(rig.dumps.lines.empty());
  }

  // ─── re-entrancy ────────────────────────────────────────────────────────────

  TEST_CASE("matrix: a fan-out survives the gains being rewritten mid-send (#484)") {
    // Sends are synchronous, so the sink on the first outlet to fire re-enters
    // the control inlet *inside* the burst. Reading the table per outlet would
    // deliver this one message under two different sets of gains; the row is
    // snapshotted before the first send, so it does not.
    gMatrix op;
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

    op.GetInlet(0)->SetList("connect 0 0 2", YSE::T_GUI);
    op.GetInlet(0)->SetList("connect 0 1 3", YSE::T_GUI);
    op.GetInlet(0)->SetList("connect 0 2 4", YSE::T_GUI);

    op.GetInlet(1)->SetInt(10, YSE::T_GUI);

    // All three fired, in Max's order, and each under the gain it had when the
    // message arrived — even though the first of them wiped the table on its
    // way.
    CHECK(std::string(order.begin(), order.end()) == "cba");
    CHECK(second.lastFloat == doctest::Approx(30.f));
    CHECK(third.lastFloat == doctest::Approx(20.f));
    // And the rewrite did happen — it just did not take effect until the next
    // message.
    CHECK(op.ConnectionCount() == 0);

    order.clear();
    op.GetInlet(1)->SetInt(11, YSE::T_GUI);
    CHECK(order.empty());
  }

  // ─── real-time / graph ──────────────────────────────────────────────────────

  TEST_CASE("matrix: Calculate() emits nothing (#484)") {
    // The object is driven by its inlets. An emitting Calculate() would route a
    // message no patch sent.
    Rig rig("2 2");
    rig.Control("connect 0 0 2");
    rig.Control("connect 1 1");
    rig.Reset();
    for (int i = 0; i < 8; i++)
      rig.op.Calculate(YSE::T_DSP);
    CHECK(rig.Total() == 0);
    CHECK(rig.dumps.lines.empty());
  }

  TEST_CASE("matrix: survives a DumpJSON / ParseJSON round trip (#484)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_MATRIX);
    REQUIRE(h != nullptr);
    h->SetParams("3 4 0.5");
    const std::string json = src.DumpJSON();
    CHECK(json.find(".matrix") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".matrix"));
    CHECK(copy->GetParams() == std::string("3 4 0.5"));
    // The shape has to come back with it, or the saved patch cords land on the
    // wrong ports.
    CHECK(copy->GetInputs() == 4);
    CHECK(copy->GetOutputs() == 5);

    // And the reloaded object is a working weighted crossbar, not merely the
    // right shape: the default gain came back too, so a bare `connect` halves.
    FloatSink sink;
    YSE::pHandle sinkHandle(&sink);
    loaded.Connect(copy, 2, &sinkHandle, 0);
    copy->SetListData(0, "connect 1 2");
    copy->SetIntData(2, 10);
    CHECK(sink.gotFloat);
    CHECK(sink.received == doctest::Approx(5.f));
  }

  TEST_CASE("matrix: the connections are run-time state, not parameters (#484)") {
    // The creation arguments are what a saved patch carries, exactly as
    // .spray's `offset` and .router's routing are run-time state.
    // `dumpconnections` is how a patch reads the routing back out.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_MATRIX, "2 2");
    REQUIRE(h != nullptr);
    h->SetListData(0, "connect 0 1 0.5");
    CHECK(h->GetParams() == std::string("2 2"));
  }

  // ─── end to end ─────────────────────────────────────────────────────────────

  TEST_CASE("matrix: routes and weights modulation in a real patcher (#484)") {
    // The headline use, end to end through a real patcher: one modulation
    // source reaching two destinations at different depths, with both the
    // destination and the depth changed by a message rather than by
    // re-patching. This is the case a .router plus per-destination multipliers
    // could not express in one object, because the depths would live in
    // separate boxes that no single message can address.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* matrix = p.CreateObject(YSE::OBJ::G_MATRIX, "2 2");
    REQUIRE(matrix != nullptr);

    FloatSink cutoff;
    FloatSink amount;
    YSE::pHandle cutoffHandle(&cutoff);
    YSE::pHandle amountHandle(&amount);

    p.Connect(matrix, 0, &cutoffHandle, 0);
    p.Connect(matrix, 1, &amountHandle, 0);

    // Nothing is wired inside the crossbar yet, so the patch is inert.
    matrix->SetFloatData(1, 1.f);
    CHECK_FALSE(cutoff.gotFloat);
    CHECK_FALSE(amount.gotFloat);

    // One message, and the source reaches the first destination at a quarter.
    matrix->SetListData(0, "connect 0 0 0.25");
    matrix->SetFloatData(1, 1.f);
    CHECK(cutoff.received == doctest::Approx(0.25f));
    CHECK_FALSE(amount.gotFloat);

    // A second message adds the other destination at a different depth — one
    // event, two differently weighted arrivals.
    matrix->SetListData(0, "connect 0 1 2");
    cutoff.gotFloat = false;
    matrix->SetFloatData(1, 1.f);
    CHECK(cutoff.received == doctest::Approx(0.25f));
    CHECK(amount.received == doctest::Approx(2.f));

    // Changing a depth is a message, not a re-patch.
    matrix->SetListData(0, "0 0 -1");
    matrix->SetFloatData(1, 1.f);
    CHECK(cutoff.received == doctest::Approx(-1.f));

    // And a second source reaching the same destination arrives on its own
    // terms rather than being summed with the first.
    matrix->SetListData(0, "connect 1 1 0.5");
    amount.gotFloat = false;
    matrix->SetFloatData(2, 8.f);
    CHECK(amount.received == doctest::Approx(4.f));
    CHECK(amount.gotFloat);

    // Pulling a connection out is one message too.
    matrix->SetListData(0, "disconnect 0 1");
    amount.gotFloat = false;
    matrix->SetFloatData(1, 1.f);
    CHECK_FALSE(amount.gotFloat);
    CHECK(cutoff.received == doctest::Approx(-1.f));
  }

} // TEST_SUITE("patcher")

// Tests for .buddy (issue #475) — wait until every inlet has data, then release
// the whole set once.
//
// Five rules carry this file, and each of them is one a plausible but wrong
// implementation gets backwards:
//
//   - **nothing leaves until every inlet has something.** The headline rule,
//     and the one that separates this object from `.bondo` (#474), which looks
//     identical from the outside and releases on *any* input. A partial set is
//     silence, however many times the same inlet is fed.
//   - **a release consumes the set.** Max: "then waits until data has arrived
//     again in all inlets." So the second release needs a second full round —
//     an implementation that persists its slots (which is exactly what `.bondo`
//     must do) passes every single-release test and then fires on every
//     subsequent message forever.
//   - **a bang is the number 0, not a trigger.** Max says so once, and it is
//     the natural thing to get wrong on a rendezvous object, where "bang =
//     release now" is the obvious guess. A bang into a partial set releases
//     nothing; a bang into the last empty inlet releases the set with int 0 on
//     that outlet.
//   - **the release order is right to left**, asserted through sinks that all
//     log into one buffer so the log reads back as the exact sequence, plus a
//     nesting check that each send *completes* before the outlet to its left is
//     served, plus the idiom end to end in a real patcher.
//   - **the object is emptied before it sends.** Correctness, not tidiness: a
//     patch that loops an outlet back into an inlet re-enters inside the Send,
//     and an object emptied afterwards would find a complete set and re-release.
//     Pinned with a self-wired outlet, which recurses to the send-depth ceiling
//     if the emptying moves.
//
// The rest is the message grammar from the Max reference: `clear` on the left
// inlet only, a list stored whole rather than spread (the deliberate difference
// from `.bondo`), and the inlet count from the creation argument.
//
// The standalone rigs wire objects directly rather than through a patcher, as
// every sibling suite does. That exercises the same outlet::Send* loop the
// pinned-GraphState path of #226 runs: the snapshot changes *which* adjacency
// vector is walked, not that it is walked front to back.
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
#include "patcher/genericObjects/gBuddy.h"
#include "patcher/genericObjects/gNext.h"
#include "patcher/sinks.hpp"

namespace {

  using TestHelpers::IntSink;
  using TestHelpers::OrderSink;
  using YSE::PATCHER::gBuddy;
  using YSE::PATCHER::gNext;

  // One order-logging sink per outlet, all sharing one log, so "which outlet
  // released what, and in what order" is an assertion rather than an inference.
  struct Rig {
    std::unique_ptr<gBuddy> op;
    std::vector<std::unique_ptr<OrderSink>> sinks;
    std::vector<char> order;

    explicit Rig(const std::string& args = "") : op(new gBuddy()) {
      if (!args.empty()) op->SetParams(args);
      Wire();
    }

    // Rebuilt after a SetParams, since re-parsing replaces the ports.
    void Wire() {
      sinks.clear();
      order.clear();
      for (int i = 0; i < op->NumOutputs(); i++) {
        sinks.push_back(std::unique_ptr<OrderSink>(new OrderSink()));
        // 'a' for outlet 0, 'b' for outlet 1, ... so the log reads left to
        // right in *outlet* order and a right-to-left release shows up
        // reversed. Wrapped at 26, which only matters for the port-cap test.
        sinks.back()->tag = (char)('a' + (i % 26));
        sinks.back()->log = &order;
        op->ConnectOutlet(sinks.back()->GetInlet(0), i);
        sinks.back()->ConnectInlet(op->GetOutlet(i), 0);
      }
    }

    void Bang(int inlet = 0) {
      op->GetInlet(inlet)->SetBang(YSE::T_GUI);
    }
    void SendInt(int inlet, int v) {
      op->GetInlet(inlet)->SetInt(v, YSE::T_GUI);
    }
    void SendFloat(int inlet, float v) {
      op->GetInlet(inlet)->SetFloat(v, YSE::T_GUI);
    }
    void List(int inlet, const std::string& text) {
      op->GetInlet(inlet)->SetList(text, YSE::T_GUI);
    }

    // Feed every inlet an int so the set completes, values 100, 101, ...
    void FillAll(int base = 100) {
      for (int i = 0; i < op->NumInputs(); i++)
        SendInt(i, base + i);
    }

    int Ports() const {
      return op->NumOutputs();
    }
    const OrderSink& At(int outlet) const {
      return *sinks[(std::size_t)outlet];
    }
    std::string Log() const {
      return std::string(order.begin(), order.end());
    }
    void ClearLog() {
      order.clear();
    }
    int Total() const {
      int n = 0;
      for (const auto& s : sinks)
        n += s->count;
      return n;
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("buddy: creatable through the registry (#475)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_BUDDY, "4");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".buddy"));
    CHECK(h->GetInputs() == 4);
    CHECK(h->GetOutputs() == 4);
  }

  TEST_CASE("buddy: listed by pRegistry::AllNames (#475)") {
    auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(YSE::OBJ::G_BUDDY)) != names.end());
  }

  TEST_CASE("buddy: with no arguments there are two inlets and two outlets (#475)") {
    // Max: "If there is no argument, there are two inlets and two outlets."
    gBuddy op;
    CHECK(op.PortCount() == gBuddy::DEFAULT_PORTS);
    CHECK(op.NumInputs() == 2);
    CHECK(op.NumOutputs() == 2);
    CHECK(op.GetOutputType(0) == YSE::OUT_TYPE::ANY);
  }

  TEST_CASE("buddy: the numeric argument sets the port count (#475)") {
    Rig rig("5");
    CHECK(rig.op->PortCount() == 5);
    CHECK(rig.op->NumInputs() == 5);
    CHECK(rig.Ports() == 5);
  }

  TEST_CASE("buddy: the port count is clamped to 1-256 (#475)") {
    gBuddy tooFew;
    tooFew.SetParams("0");
    CHECK(tooFew.PortCount() == gBuddy::MIN_PORTS);
    CHECK(tooFew.NumInputs() == gBuddy::MIN_PORTS);

    gBuddy negative;
    negative.SetParams("-7");
    CHECK(negative.PortCount() == gBuddy::MIN_PORTS);

    gBuddy tooMany;
    tooMany.SetParams("1000");
    CHECK(tooMany.PortCount() == gBuddy::MAX_PORTS);
    CHECK(tooMany.NumInputs() == gBuddy::MAX_PORTS);
    CHECK(tooMany.NumOutputs() == gBuddy::MAX_PORTS);
  }

  TEST_CASE("buddy: a non-numeric argument leaves the default port count (#475)") {
    gBuddy op;
    op.SetParams("wide");
    CHECK(op.PortCount() == gBuddy::DEFAULT_PORTS);

    gBuddy overflow;
    overflow.SetParams("1e999");
    CHECK(overflow.PortCount() == gBuddy::DEFAULT_PORTS);
  }

  TEST_CASE("buddy: a float argument is truncated (#475)") {
    gBuddy op;
    op.SetParams("3.9");
    CHECK(op.PortCount() == 3);
  }

  // ─── nothing leaves until every inlet has something ─────────────────────────
  // The headline rule, and the whole difference from .bondo.

  TEST_CASE("buddy: a partial set releases nothing at all (#475)") {
    Rig rig("3");
    rig.SendInt(0, 1);
    CHECK(rig.Total() == 0);
    rig.SendInt(1, 2);
    CHECK(rig.Total() == 0);
    CHECK(rig.op->FilledCount() == 2);
  }

  TEST_CASE("buddy: feeding one inlet repeatedly never completes the set (#475)") {
    // The mistake `.bondo` would make here: every one of these is an input, and
    // on that object every input is a release.
    Rig rig("2");
    for (int i = 0; i < 10; i++)
      rig.SendInt(0, i);
    CHECK(rig.Total() == 0);
    CHECK(rig.op->FilledCount() == 1);
    // ...and the newest value is the one that will go out.
    CHECK(rig.op->HeldInt(0) == 9);
  }

  TEST_CASE("buddy: the last missing inlet releases the whole set (#475)") {
    Rig rig("3");
    rig.SendInt(0, 11);
    rig.SendInt(1, 22);
    REQUIRE(rig.Total() == 0);

    rig.SendInt(2, 33);
    CHECK(rig.Total() == 3);
    CHECK(rig.At(0).lastInt == 11);
    CHECK(rig.At(1).lastInt == 22);
    CHECK(rig.At(2).lastInt == 33);
  }

  TEST_CASE("buddy: the completing inlet may be any of them (#475)") {
    // A "the left inlet triggers" implementation passes every test that fills
    // left to right.
    for (int last = 0; last < 3; last++) {
      CAPTURE(last);
      Rig rig("3");
      for (int i = 0; i < 3; i++)
        if (i != last) rig.SendInt(i, 10 + i);
      REQUIRE(rig.Total() == 0);
      rig.SendInt(last, 10 + last);
      CHECK(rig.Total() == 3);
      for (int i = 0; i < 3; i++)
        CHECK(rig.At(i).lastInt == 10 + i);
    }
  }

  TEST_CASE("buddy: a one-inlet object releases on every message (#475)") {
    // The degenerate case of the same rule, not a special case in the code.
    Rig rig("1");
    REQUIRE(rig.Ports() == 1);
    rig.SendInt(0, 5);
    CHECK(rig.Total() == 1);
    rig.SendInt(0, 6);
    CHECK(rig.Total() == 2);
    CHECK(rig.At(0).lastInt == 6);
  }

  // ─── a release consumes the set ─────────────────────────────────────────────

  TEST_CASE("buddy: a release empties every inlet (#475)") {
    // Max: "then waits until data has arrived again in all inlets."
    Rig rig("3");
    rig.FillAll();
    REQUIRE(rig.Total() == 3);

    CHECK(rig.op->FilledCount() == 0);
    for (int i = 0; i < 3; i++) {
      CAPTURE(i);
      CHECK(rig.op->HeldKind(i) == gBuddy::Held::NONE);
    }
  }

  TEST_CASE("buddy: the second release needs a second full round (#475)") {
    // The test a persisting implementation fails — and persisting is exactly
    // what `.bondo` must do, which is why this object is not a mode of it.
    Rig rig("2");
    rig.FillAll();
    REQUIRE(rig.Total() == 2);
    rig.ClearLog();

    rig.SendInt(0, 7);
    CHECK(rig.Total() == 2); // still, nothing new
    rig.SendInt(1, 8);
    CHECK(rig.Total() == 4);
    CHECK(rig.At(0).lastInt == 7);
    CHECK(rig.At(1).lastInt == 8);
  }

  TEST_CASE("buddy: nothing is ever sent twice (#475)") {
    Rig rig("2");
    rig.SendInt(0, 1);
    rig.SendInt(1, 2);
    REQUIRE(rig.At(0).count == 1);

    // A long run of messages into one inlet only: the object stays silent
    // because the other inlet was consumed by the release above.
    for (int i = 0; i < 20; i++)
      rig.SendInt(0, i);
    CHECK(rig.At(0).count == 1);
    CHECK(rig.At(1).count == 1);
  }

  // ─── bang is the number 0 ───────────────────────────────────────────────────

  TEST_CASE("buddy: a bang into a partial set releases nothing (#475)") {
    // Max: "bang: In any inlet: Same as sending the number 0." Not a trigger,
    // which is the obvious wrong guess on a rendezvous object.
    Rig rig("3");
    rig.SendInt(0, 1);
    rig.Bang(1);
    CHECK(rig.Total() == 0);
    // ...but it did count as data arriving.
    CHECK(rig.op->FilledCount() == 2);
    CHECK(rig.op->HeldKind(1) == gBuddy::Held::INT);
    CHECK(rig.op->HeldInt(1) == 0);
  }

  TEST_CASE("buddy: a bang completing the set releases it, with 0 on that outlet (#475)") {
    Rig rig("2");
    rig.SendInt(0, 42);
    rig.Bang(1);
    CHECK(rig.Total() == 2);
    CHECK(rig.At(0).lastInt == 42);
    CHECK(rig.At(1).lastKind == OrderSink::INT);
    CHECK(rig.At(1).lastInt == 0);
  }

  TEST_CASE("buddy: a bang in any inlet is a value (#475)") {
    Rig rig("3");
    rig.Bang(0);
    rig.Bang(1);
    REQUIRE(rig.Total() == 0);
    rig.Bang(2);
    CHECK(rig.Total() == 3);
    for (int i = 0; i < 3; i++) {
      CAPTURE(i);
      CHECK(rig.At(i).lastKind == OrderSink::INT);
      CHECK(rig.At(i).lastInt == 0);
    }
  }

  // ─── replacement ────────────────────────────────────────────────────────────

  TEST_CASE("buddy: a second value replaces the one an inlet was holding (#475)") {
    Rig rig("2");
    rig.SendInt(0, 1);
    rig.SendInt(0, 2);
    rig.SendFloat(0, 3.5f);
    CHECK(rig.op->FilledCount() == 1);
    CHECK(rig.op->HeldKind(0) == gBuddy::Held::FLOAT);

    rig.SendInt(1, 9);
    CHECK(rig.At(0).lastKind == OrderSink::FLOAT);
    CHECK(rig.At(0).lastFloat == doctest::Approx(3.5f));
    CHECK(rig.Total() == 2);
  }

  // ─── what comes out is what went in ─────────────────────────────────────────

  TEST_CASE("buddy: each slot releases whichever kind last arrived (#475)") {
    Rig rig("3");
    rig.SendInt(0, 7);
    rig.SendFloat(1, 1.5f);
    rig.List(2, "some symbol");

    CHECK(rig.At(0).lastKind == OrderSink::INT);
    CHECK(rig.At(0).lastInt == 7);
    CHECK(rig.At(1).lastKind == OrderSink::FLOAT);
    CHECK(rig.At(1).lastFloat == doctest::Approx(1.5f));
    CHECK(rig.At(2).lastKind == OrderSink::LIST);
    CHECK(rig.At(2).lastList == "some symbol");
  }

  TEST_CASE("buddy: a list is stored whole, not spread across the outlets (#475)") {
    // The deliberate difference from `.bondo`, which spreads a list that leads
    // with a number across the slots to the right. Max documents spreading for
    // bondo and nothing of the kind here, where list and anything share one
    // sentence with int and float.
    Rig rig("3");
    rig.List(0, "10 20 30");
    CHECK(rig.op->FilledCount() == 1);
    CHECK(rig.op->HeldKind(0) == gBuddy::Held::LIST);
    CHECK(rig.op->HeldText(0) == "10 20 30");
    CHECK(rig.op->HeldKind(1) == gBuddy::Held::NONE);
    CHECK(rig.op->HeldKind(2) == gBuddy::Held::NONE);
    CHECK(rig.Total() == 0);

    rig.SendInt(1, 1);
    rig.SendInt(2, 2);
    CHECK(rig.At(0).lastKind == OrderSink::LIST);
    CHECK(rig.At(0).lastList == "10 20 30");
  }

  TEST_CASE("buddy: an empty list message is data (#475)") {
    Rig rig("2");
    rig.List(0, "");
    CHECK(rig.op->HeldKind(0) == gBuddy::Held::LIST);
    CHECK(rig.op->HeldText(0).empty());
    CHECK(rig.op->FilledCount() == 1);
    rig.SendInt(1, 1);
    CHECK(rig.Total() == 2);
    CHECK(rig.At(0).lastList.empty());
  }

  TEST_CASE("buddy: text longer than the reserved slot capacity is still released (#475)") {
    // Correctness never depends on the reserve; exceeding it costs one
    // reallocation, not a truncated value. Twice, because a release swaps the
    // slot's buffer with the outgoing one and a swap that lost the contents
    // would only show on the round after the first.
    const std::string wide(gBuddy::TEXT_CAPACITY * 2, 'x');
    Rig rig("2");
    rig.List(0, wide);
    rig.SendInt(1, 1);
    CHECK(rig.At(0).lastList == wide);

    const std::string other(gBuddy::TEXT_CAPACITY * 2, 'y');
    rig.List(0, other);
    rig.SendInt(1, 2);
    CHECK(rig.At(0).lastList == other);
  }

  // ─── clear ──────────────────────────────────────────────────────────────────

  TEST_CASE("buddy: 'clear' on the left inlet empties every inlet silently (#475)") {
    // Max: "clear: In left inlet: Deletes all values stored in the inlets."
    Rig rig("3");
    rig.SendInt(0, 1);
    rig.SendInt(1, 2);
    REQUIRE(rig.op->FilledCount() == 2);

    rig.List(0, "clear");
    CHECK(rig.Total() == 0);
    CHECK(rig.op->FilledCount() == 0);
    for (int i = 0; i < 3; i++) {
      CAPTURE(i);
      CHECK(rig.op->HeldKind(i) == gBuddy::Held::NONE);
    }

    // And the object still works afterwards: a full round is needed again.
    rig.SendInt(0, 5);
    rig.SendInt(1, 6);
    CHECK(rig.Total() == 0);
    rig.SendInt(2, 7);
    CHECK(rig.Total() == 3);
  }

  TEST_CASE("buddy: 'clear' on a data inlet is an ordinary symbol (#475)") {
    // Max scopes the message to the left inlet and that is kept literally: the
    // other inlets are fed by other objects' outlets, and a message whose text
    // happened to read `clear` wiping the object from a data inlet would be far
    // worse than storing it as the symbol it is.
    Rig rig("2");
    rig.SendInt(0, 1);
    rig.List(1, "clear");
    CHECK(rig.Total() == 2);
    CHECK(rig.At(1).lastKind == OrderSink::LIST);
    CHECK(rig.At(1).lastList == "clear");
  }

  TEST_CASE("buddy: 'clear' is matched bare (#475)") {
    // As `.uzi`'s `pause` and `.change`'s `mode` are: `clear 1` is a list, and
    // a list is data.
    Rig rig("2");
    rig.SendInt(0, 1);
    rig.List(0, "clear 1");
    CHECK(rig.op->FilledCount() == 1);
    CHECK(rig.op->HeldKind(0) == gBuddy::Held::LIST);
    CHECK(rig.op->HeldText(0) == "clear 1");
    CHECK(rig.Total() == 0);
  }

  // ─── release order ──────────────────────────────────────────────────────────

  TEST_CASE("buddy: outlets release right to left, not left to right (#475)") {
    // Max: "When a data has arrived in each inlet, it is sent out the outlets,
    // in order from right to left."
    Rig rig("4");
    rig.FillAll();
    // Sinks are tagged 'a' (outlet 0) through 'd' (outlet 3). Right to left is
    // the reverse of the outlet order.
    CHECK(rig.Log() == "dcba");
    CHECK(rig.Total() == 4);
  }

  TEST_CASE("buddy: the release order does not depend on the fill order (#475)") {
    Rig rig("3");
    rig.SendInt(2, 3);
    rig.SendInt(0, 1);
    rig.SendInt(1, 2);
    CHECK(rig.Log() == "cba");
  }

  TEST_CASE("buddy: the release order holds across mixed slot kinds (#475)") {
    // A switch arm that forgot the reverse walk cannot hide behind one kind.
    Rig rig("4");
    rig.SendInt(0, 1);
    rig.SendFloat(1, 2.5f);
    rig.List(2, "word");
    rig.Bang(3);
    CHECK(rig.Log() == "dcba");
    CHECK(rig.At(0).lastKind == OrderSink::INT);
    CHECK(rig.At(1).lastKind == OrderSink::FLOAT);
    CHECK(rig.At(2).lastKind == OrderSink::LIST);
    CHECK(rig.At(3).lastKind == OrderSink::INT);
  }

  TEST_CASE("buddy: the release order holds for many outlets (#475)") {
    // Not a small-n special case: twenty outlets come out in the reverse of the
    // alphabet, so an implementation that only got the two-port case right has
    // nowhere to hide.
    Rig rig("20");
    REQUIRE(rig.Ports() == 20);
    std::string expected;
    for (int i = 0; i < 20; i++)
      expected += (char)('a' + (19 - i));
    rig.FillAll();
    CHECK(rig.Log() == expected);
  }

  TEST_CASE("buddy: every outlet fires exactly once per release (#475)") {
    Rig rig("5");
    rig.FillAll();
    for (int i = 0; i < rig.Ports(); i++) {
      CAPTURE(i);
      CHECK(rig.At(i).count == 1);
    }
    rig.FillAll(200);
    for (int i = 0; i < rig.Ports(); i++) {
      CAPTURE(i);
      CHECK(rig.At(i).count == 2);
    }
  }

  TEST_CASE("buddy: each send completes before the outlet to its left fires (#475)") {
    // The half of the guarantee a bare sequence check cannot see: a send does
    // not merely *start* before the one to its left, it finishes — the whole
    // subgraph behind it runs first. A breadth-first fan-out would still log
    // 'b' before 'a' at the top level while interleaving everything below.
    std::vector<char> order;

    gBuddy outer; // two ports
    gBuddy inner; // two ports

    OrderSink innerLeft;
    OrderSink innerRight;
    OrderSink outerLeft;
    innerLeft.tag = 'x';
    innerRight.tag = 'y';
    outerLeft.tag = 'z';
    innerLeft.log = innerRight.log = outerLeft.log = &order;

    inner.ConnectOutlet(innerLeft.GetInlet(0), 0);
    innerLeft.ConnectInlet(inner.GetOutlet(0), 0);
    inner.ConnectOutlet(innerRight.GetInlet(0), 1);
    innerRight.ConnectInlet(inner.GetOutlet(1), 0);
    outer.ConnectOutlet(outerLeft.GetInlet(0), 0);
    outerLeft.ConnectInlet(outer.GetOutlet(0), 0);

    // The inner object is one message short of complete, so the single value
    // arriving from the outer object's right outlet (released first) makes it
    // release its whole pair before the outer object's left outlet is served.
    inner.GetInlet(0)->SetInt(1, YSE::T_GUI);
    outer.ConnectOutlet(inner.GetInlet(1), 1);
    inner.ConnectInlet(outer.GetOutlet(1), 1);

    outer.GetInlet(0)->SetInt(10, YSE::T_GUI);
    REQUIRE(order.empty()); // outer still incomplete
    outer.GetInlet(1)->SetInt(20, YSE::T_GUI);

    // The inner object's own right-to-left pair runs to completion ('y' then
    // 'x') before the outer object's left outlet is served ('z').
    CHECK(std::string(order.begin(), order.end()) == "yxz");
  }

  TEST_CASE("buddy: the left outlet acts on a set the right ones already placed (#475)") {
    // The idiom the guarantee exists for, end to end in a real patcher and
    // through real objects: the rightmost outlet stores into a cold inlet and
    // the leftmost one triggers the read. Under any other order the reader
    // fires against the *previous* set.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* buddy = p.CreateObject(YSE::OBJ::G_BUDDY, "2");
    YSE::pHandle* toBang = p.CreateObject(YSE::OBJ::G_TRIGGER, "b");
    YSE::pHandle* store = p.CreateObject(YSE::OBJ::G_INT);
    REQUIRE(buddy != nullptr);
    REQUIRE(toBang != nullptr);
    REQUIRE(store != nullptr);

    IntSink sink;
    YSE::pHandle sinkHandle(&sink);

    // Right outlet -> the silent inlet of .i; left outlet -> a bang -> its hot
    // inlet, which makes .i report whatever it is holding at that moment.
    p.Connect(buddy, 1, store, 1);
    p.Connect(buddy, 0, toBang, 0);
    p.Connect(toBang, 0, store, 0);
    p.Connect(store, 0, &sinkHandle, 0);

    buddy->SetIntData(1, 42);
    CHECK_FALSE(sink.gotInt); // the set is not complete yet
    buddy->SetIntData(0, 1);
    CHECK(sink.gotInt);
    CHECK(sink.received == 42);

    // Twice, with a different value: a left-to-right implementation would be
    // one release behind here (42 rather than 7) rather than wrong only once.
    buddy->SetIntData(1, 7);
    buddy->SetIntData(0, 1);
    CHECK(sink.received == 7);
  }

  // ─── emptied before it sends ────────────────────────────────────────────────

  TEST_CASE("buddy: a release empties the object before the first send (#475)") {
    // The re-entrancy rule, stated as a patch: outlet 1 is wired back into
    // inlet 1, so the release re-enters this object inside its own Send. With
    // the emptying after the send it would find a complete set and release
    // again, recursing until the kMaxSendDepth ceiling in outlet.cpp cut it
    // off; emptied first, the re-entrant value simply refills one slot.
    std::vector<char> order;
    gBuddy op; // two ports

    OrderSink left;
    left.tag = 'a';
    left.log = &order;
    op.ConnectOutlet(left.GetInlet(0), 0);
    left.ConnectInlet(op.GetOutlet(0), 0);

    // The loop: outlet 1 -> inlet 1, no object in between.
    op.ConnectOutlet(op.GetInlet(1), 1);
    op.ConnectInlet(op.GetOutlet(1), 1);

    op.GetInlet(1)->SetInt(5, YSE::T_GUI);
    op.GetInlet(0)->SetInt(9, YSE::T_GUI);

    // Exactly one release: the left outlet fired once.
    CHECK(left.count == 1);
    CHECK(left.lastInt == 9);
    CHECK(std::string(order.begin(), order.end()) == "a");
    // ...and the value that came back round is sitting in inlet 1, waiting for
    // the next round rather than having been released with it.
    CHECK(op.FilledCount() == 1);
    CHECK(op.HeldKind(1) == gBuddy::Held::INT);
    CHECK(op.HeldInt(1) == 5);
    CHECK(op.HeldKind(0) == gBuddy::Held::NONE);
  }

  // ─── one logical event ──────────────────────────────────────────────────────

  TEST_CASE("buddy: .next reads a whole release as one event (#475)") {
    // The real object, not the raw clock — a wrong clock and a right comparison
    // look identical from one outlet. 'S' = separated (a new event), 'C' =
    // continued.
    std::vector<char> order;
    gBuddy op;
    op.SetParams("4");
    gNext next;

    for (int outlet = 0; outlet < op.NumOutputs(); outlet++) {
      op.ConnectOutlet(next.GetInlet(0), outlet);
      next.ConnectInlet(op.GetOutlet(outlet), 0);
    }

    OrderSink separated;
    separated.tag = 'S';
    separated.log = &order;
    next.ConnectOutlet(separated.GetInlet(0), 0);
    separated.ConnectInlet(next.GetOutlet(0), 0);

    OrderSink continued;
    continued.tag = 'C';
    continued.log = &order;
    next.ConnectOutlet(continued.GetInlet(0), 1);
    continued.ConnectInlet(next.GetOutlet(1), 0);

    for (int i = 0; i < 4; i++)
      op.GetInlet(i)->SetInt(i, YSE::T_GUI);
    CHECK(std::string(order.begin(), order.end()) == "SCCC");

    // A second round is a second event, so exactly one more separated bang.
    order.clear();
    for (int i = 3; i >= 0; i--)
      op.GetInlet(i)->SetInt(i, YSE::T_GUI);
    CHECK(std::string(order.begin(), order.end()) == "SCCC");
    CHECK(separated.count == 2);
    CHECK(continued.count == 6);
  }

  // ─── Calculate ──────────────────────────────────────────────────────────────

  TEST_CASE("buddy: Calculate does not release anything (#475)") {
    // An emitting Calculate() would release on a DSP tick from a stimulus no
    // patch sent — the rule .sel, .trigger, .past and .bondo set. Checked with
    // a set that is one message short of complete, which is where a
    // Calculate-driven release would be most visible.
    Rig rig("3");
    rig.SendInt(0, 1);
    rig.SendInt(1, 2);
    for (int i = 0; i < 5; i++)
      rig.op->Calculate(YSE::T_DSP);
    CHECK(rig.Total() == 0);
    CHECK(rig.op->FilledCount() == 2);
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  TEST_CASE("buddy: re-parsing an empty parameter string returns to two ports (#475)") {
    gBuddy op;
    op.SetParams("6");
    REQUIRE(op.PortCount() == 6);

    op.SetParams("");
    CHECK(op.PortCount() == gBuddy::DEFAULT_PORTS);
    CHECK(op.NumInputs() == gBuddy::DEFAULT_PORTS);
    CHECK(op.NumOutputs() == gBuddy::DEFAULT_PORTS);
  }

  TEST_CASE("buddy: re-parsing rebuilds the ports and forgets the part-assembled set (#475)") {
    gBuddy op;
    op.GetInlet(0)->SetInt(5, YSE::T_GUI);
    REQUIRE(op.HeldInt(0) == 5);
    REQUIRE(op.FilledCount() == 1);

    op.SetParams("4");
    CHECK(op.PortCount() == 4);
    CHECK(op.FilledCount() == 0);
    // The slots the values belonged to no longer exist, so nothing is carried
    // over — a half-migrated set would be worse than a clean one.
    for (int i = 0; i < 4; i++) {
      CAPTURE(i);
      CHECK(op.HeldKind(i) == gBuddy::Held::NONE);
    }
  }

  TEST_CASE("buddy: survives a DumpJSON / ParseJSON round trip (#475)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_BUDDY);
    REQUIRE(h != nullptr);
    h->SetParams("5");
    const std::string json = src.DumpJSON();
    CHECK(json.find(".buddy") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".buddy"));
    CHECK(copy->GetParams() == std::string("5"));
    // The shape has to come back with it, or the saved patch cords land on the
    // wrong ports — on both sides, since this object's inlet count is a
    // creation argument too.
    CHECK(copy->GetInputs() == 5);
    CHECK(copy->GetOutputs() == 5);
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category, the port shape and the parameter name,
  // which is what a binding generator keys on. It also pins that the ports
  // built by the parse callback are documented — the coverage test only ever
  // sees a default-constructed object.

  TEST_CASE("buddy: documents itself as GENERIC with a labelled port set (#475)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_BUDDY));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    CHECK_FALSE(obj->GetDescription().empty());

    REQUIRE(obj->NumInputs() == 2);
    CHECK(obj->GetInlet(0)->GetDocLabel() == "in0");
    CHECK(obj->GetInlet(1)->GetDocLabel() == "in1");

    REQUIRE(obj->NumOutputs() == 2);
    CHECK(obj->GetOutlet(0)->GetDocLabel() == "out0");
    CHECK(obj->GetOutlet(1)->GetDocLabel() == "out1");

    REQUIRE(obj->GetParamDocs().size() == 1);
    CHECK(obj->GetParamDocs()[0].name == "inlets");
    CHECK(obj->GetParamDocs()[0].defaultValue == "2");
  }

  TEST_CASE("buddy: ports created by the parse callback are documented too (#475)") {
    gBuddy op;
    op.SetParams("7");
    REQUIRE(op.NumInputs() == 7);
    REQUIRE(op.NumOutputs() == 7);
    for (int i = 0; i < 7; i++) {
      CAPTURE(i);
      CHECK_FALSE(op.GetInlet(i)->GetDocLabel().empty());
      CHECK_FALSE(op.GetInlet(i)->GetDocDescription().empty());
      CHECK_FALSE(op.GetInlet(i)->GetRange().empty());
      CHECK_FALSE(op.GetOutlet(i)->GetDocLabel().empty());
      CHECK_FALSE(op.GetOutlet(i)->GetDocDescription().empty());
      CHECK_FALSE(op.GetOutlet(i)->GetRange().empty());
    }
    CHECK(op.GetInlet(6)->GetDocLabel() == "in6");
    CHECK(op.GetOutlet(6)->GetDocLabel() == "out6");
  }

  TEST_CASE("buddy: every inlet accepts bang, int, float and list (#475)") {
    // All four on all of them, because every inlet takes part in the
    // rendezvous — a cold inlet here would be visible as a missing handler.
    gBuddy op;
    op.SetParams("3");
    for (int i = 0; i < op.NumInputs(); i++) {
      CAPTURE(i);
      const unsigned int in = op.GetInlet(i)->GetAcceptedTypes();
      CHECK((in & YSE::PATCHER::IT_BANG) != 0);
      CHECK((in & YSE::PATCHER::IT_INT) != 0);
      CHECK((in & YSE::PATCHER::IT_FLOAT) != 0);
      CHECK((in & YSE::PATCHER::IT_LIST) != 0);
    }
  }

  // ─── capacity ───────────────────────────────────────────────────────────────

  TEST_CASE("buddy: at most 256 ports are built, and all of them release (#475)") {
    Rig rig("300");
    CHECK(rig.op->PortCount() == gBuddy::MAX_PORTS);
    CHECK(rig.op->NumInputs() == gBuddy::MAX_PORTS);
    CHECK(rig.Ports() == gBuddy::MAX_PORTS);

    // One short of complete: still silent with 255 of 256 inlets filled.
    for (int i = 0; i < gBuddy::MAX_PORTS - 1; i++)
      rig.SendInt(i, i);
    CHECK(rig.Total() == 0);

    rig.SendInt(gBuddy::MAX_PORTS - 1, 0);
    CHECK(rig.Total() == gBuddy::MAX_PORTS);
  }

  // ─── the contrast with .bondo, side by side ─────────────────────────────────

  TEST_CASE("buddy: releases where .bondo waits, and waits where .bondo releases (#475)") {
    // The two objects are each other's opposite and Max lists bondo first under
    // See Also, so the distinction is pinned against the real object rather
    // than described in a comment. One input: .bondo releases its whole set,
    // .buddy releases nothing.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* bondo = p.CreateObject(YSE::OBJ::G_BONDO, "2");
    YSE::pHandle* buddy = p.CreateObject(YSE::OBJ::G_BUDDY, "2");
    REQUIRE(bondo != nullptr);
    REQUIRE(buddy != nullptr);

    IntSink fromBondo;
    IntSink fromBuddy;
    YSE::pHandle bondoSink(&fromBondo);
    YSE::pHandle buddySink(&fromBuddy);
    p.Connect(bondo, 0, &bondoSink, 0);
    p.Connect(buddy, 0, &buddySink, 0);

    bondo->SetIntData(1, 5);
    buddy->SetIntData(1, 5);
    CHECK(fromBondo.gotInt); // released on any input
    CHECK_FALSE(fromBuddy.gotInt); // waits for every inlet
  }

} // TEST_SUITE("patcher")

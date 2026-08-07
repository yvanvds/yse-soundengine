// Tests for .bondo (issue #474) — hold one value per inlet and release the
// whole set together.
//
// Four things carry this file, and each of them is a rule that a plausible but
// wrong implementation gets backwards:
//
//   - **every inlet is hot.** Almost every other multi-inlet object in the
//     patcher has one acting inlet and cold ones behind it, so "inlet 1 stores
//     quietly" is the natural thing to write and the wrong thing for this
//     object. Max says "In any inlet" once per method, so there is a test per
//     inlet.
//   - **an unwritten inlet releases int 0, and stored values persist.** The
//     two halves of "a release is always a complete set": a fresh object bangs
//     out zeros rather than staying silent, and a release does not consume what
//     it sent, so the second bang of a pair is identical to the first. A
//     consuming implementation passes every single-release test and fails here.
//   - **the release order is right to left**, asserted through sinks that all
//     log into one buffer so the log reads back as the exact sequence, plus a
//     nesting check that each send *completes* before the outlet to its left is
//     served, plus the idiom end to end in a real patcher. Reverse the loop in
//     EmitAll and the named cases below fail.
//   - **a release is one logical event**, checked against the real `.next`
//     object (#471) rather than against `CurrentMessageEvent()` directly — a
//     wrong clock and a right comparison look identical from one outlet.
//
// The rest is the storage grammar taken from the Max reference: `set` stores
// without releasing, a bang releases without storing, a list whose first token
// is a number spreads across the outlets from the receiving one rightwards, and
// the `n` creation argument turns that off.
//
// The standalone rigs wire objects directly rather than through a patcher, as
// every sibling suite does. That exercises the same outlet::Send* loop the
// pinned-GraphState path of #226 runs: the snapshot changes *which* adjacency
// vector is walked, not that it is walked front to back.
//
// No audio device required.

#include <doctest/doctest.h>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/genericObjects/gBondo.h"
#include "patcher/genericObjects/gNext.h"
#include "patcher/sinks.hpp"

namespace {

  using TestHelpers::IntSink;
  using TestHelpers::OrderSink;
  using YSE::PATCHER::gBondo;
  using YSE::PATCHER::gNext;

  // One order-logging sink per outlet, all sharing one log, so "which outlet
  // released what, and in what order" is an assertion rather than an inference.
  struct Rig {
    std::unique_ptr<gBondo> op;
    std::vector<std::unique_ptr<OrderSink>> sinks;
    std::vector<char> order;

    explicit Rig(const std::string& args = "") : op(new gBondo()) {
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

  TEST_CASE("bondo: creatable through the registry (#474)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_BONDO, "4");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".bondo"));
    CHECK(h->GetInputs() == 4);
    CHECK(h->GetOutputs() == 4);
  }

  TEST_CASE("bondo: listed by pRegistry::AllNames (#474)") {
    auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(YSE::OBJ::G_BONDO)) != names.end());
  }

  TEST_CASE("bondo: with no arguments there are two inlets and two outlets (#474)") {
    // Max: "The default number of inlets and outlets is 2."
    gBondo op;
    CHECK(op.PortCount() == gBondo::DEFAULT_PORTS);
    CHECK(op.NumInputs() == 2);
    CHECK(op.NumOutputs() == 2);
    CHECK(op.GetOutputType(0) == YSE::OUT_TYPE::ANY);
  }

  TEST_CASE("bondo: the first numeric argument sets the port count (#474)") {
    Rig rig("5");
    CHECK(rig.op->PortCount() == 5);
    CHECK(rig.op->NumInputs() == 5);
    CHECK(rig.Ports() == 5);
  }

  TEST_CASE("bondo: the port count is clamped to 1-256 (#474)") {
    gBondo tooFew;
    tooFew.SetParams("0");
    CHECK(tooFew.PortCount() == gBondo::MIN_PORTS);
    CHECK(tooFew.NumInputs() == gBondo::MIN_PORTS);

    gBondo negative;
    negative.SetParams("-7");
    CHECK(negative.PortCount() == gBondo::MIN_PORTS);

    gBondo tooMany;
    tooMany.SetParams("1000");
    CHECK(tooMany.PortCount() == gBondo::MAX_PORTS);
    CHECK(tooMany.NumInputs() == gBondo::MAX_PORTS);
    CHECK(tooMany.NumOutputs() == gBondo::MAX_PORTS);
  }

  TEST_CASE("bondo: a non-numeric argument leaves the default port count (#474)") {
    gBondo op;
    op.SetParams("wide");
    CHECK(op.PortCount() == gBondo::DEFAULT_PORTS);
  }

  // ─── every inlet is hot ─────────────────────────────────────────────────────
  // The rule most likely to be got backwards, so it gets its own section. Max
  // states it once per method: "In any inlet: The input is stored in the
  // location corresponding to that inlet, and causes anything previously stored
  // to be sent out its corresponding outlet."

  TEST_CASE("bondo: an int in any inlet stores there and releases the set (#474)") {
    Rig rig("3");
    for (int inlet = 0; inlet < 3; inlet++) {
      CAPTURE(inlet);
      rig.ClearLog();
      rig.SendInt(inlet, 40 + inlet);
      // Every outlet fired, so the release was the whole set...
      CHECK(rig.Log().size() == 3);
      // ...and the value landed in the slot that matches the inlet.
      CHECK(rig.op->HeldKind(inlet) == gBondo::Held::INT);
      CHECK(rig.op->HeldInt(inlet) == 40 + inlet);
      CHECK(rig.At(inlet).lastInt == 40 + inlet);
    }
  }

  TEST_CASE("bondo: a float in any inlet stores there and releases the set (#474)") {
    Rig rig("3");
    rig.SendFloat(2, 1.5f);
    CHECK(rig.op->HeldKind(2) == gBondo::Held::FLOAT);
    CHECK(rig.op->HeldFloat(2) == doctest::Approx(1.5f));
    CHECK(rig.At(2).lastKind == OrderSink::FLOAT);
    CHECK(rig.At(2).lastFloat == doctest::Approx(1.5f));
    CHECK(rig.Total() == 3);
  }

  TEST_CASE("bondo: there are no cold inlets — the rightmost one releases too (#474)") {
    // The single most likely wrong implementation is "inlet 0 acts, the rest
    // store", which passes every test that only ever pokes inlet 0.
    Rig rig("4");
    rig.SendInt(3, 9);
    CHECK(rig.Total() == 4);
    for (int i = 0; i < 4; i++) {
      CAPTURE(i);
      CHECK(rig.At(i).count == 1);
    }
  }

  TEST_CASE("bondo: a bang in any inlet releases the set without storing (#474)") {
    // Max: "bang: Send all stored messages."
    Rig rig("3");
    rig.SendInt(0, 5);
    rig.ClearLog();

    rig.Bang(2);
    CHECK(rig.Log().size() == 3);
    // Nothing was stored by the bang itself: inlet 2 still holds nothing.
    CHECK(rig.op->HeldKind(2) == gBondo::Held::NONE);
    CHECK(rig.op->HeldKind(0) == gBondo::Held::INT);
    CHECK(rig.op->HeldInt(0) == 5);
  }

  // ─── the empty slot, and persistence ────────────────────────────────────────

  TEST_CASE("bondo: an inlet that has received nothing releases int 0 (#474)") {
    // Max: "If no message has yet been received in a particular inlet, 0 is
    // sent out of the corresponding outlet." Never silence — a reader wired to
    // every outlet gets a complete set from the very first release.
    Rig rig("3");
    rig.Bang();
    CHECK(rig.Total() == 3);
    for (int i = 0; i < 3; i++) {
      CAPTURE(i);
      CHECK(rig.op->HeldKind(i) == gBondo::Held::NONE);
      CHECK(rig.At(i).lastKind == OrderSink::INT);
      CHECK(rig.At(i).lastInt == 0);
    }
  }

  TEST_CASE("bondo: a partly-filled object still releases a complete set (#474)") {
    Rig rig("3");
    rig.SendInt(1, 7);
    CHECK(rig.At(0).lastInt == 0);
    CHECK(rig.At(1).lastInt == 7);
    CHECK(rig.At(2).lastInt == 0);
    CHECK(rig.Total() == 3);
  }

  TEST_CASE("bondo: stored values persist across releases rather than being consumed (#474)") {
    // The test a consuming implementation fails and every single-release test
    // misses. Max's wording is that an input "causes anything *previously
    // stored* to be sent out", and the 0 substitution is documented only for an
    // inlet that has received nothing yet.
    Rig rig("2");
    rig.SendInt(0, 11);
    rig.SendInt(1, 22);

    rig.Bang();
    CHECK(rig.At(0).lastInt == 11);
    CHECK(rig.At(1).lastInt == 22);

    // Again, unchanged.
    rig.Bang();
    CHECK(rig.At(0).lastInt == 11);
    CHECK(rig.At(1).lastInt == 22);
    CHECK(rig.op->HeldInt(0) == 11);
    CHECK(rig.op->HeldInt(1) == 22);
  }

  TEST_CASE("bondo: changing one inlet re-sends the others unchanged (#474)") {
    // The whole point of the object stated as a test: the set never goes out
    // half old and half new, and it never goes out incomplete.
    Rig rig("3");
    rig.SendInt(0, 1);
    rig.SendInt(1, 2);
    rig.SendInt(2, 3);

    rig.SendInt(1, 99);
    CHECK(rig.At(0).lastInt == 1);
    CHECK(rig.At(1).lastInt == 99);
    CHECK(rig.At(2).lastInt == 3);
  }

  // ─── set: store without releasing ───────────────────────────────────────────

  TEST_CASE("bondo: 'set' stores without releasing anything (#474)") {
    // Max: "The word set, followed by any message, stores the input in the
    // location corresponding to that inlet without triggering any output." The
    // only way to write an inlet quietly, since there are no cold inlets.
    Rig rig("3");
    rig.List(1, "set 42");
    CHECK(rig.Total() == 0);
    CHECK(rig.op->HeldKind(1) == gBondo::Held::INT);
    CHECK(rig.op->HeldInt(1) == 42);
  }

  TEST_CASE("bondo: 'set' then bang is the manual mode (#474)") {
    Rig rig("3");
    rig.List(0, "set 1");
    rig.List(1, "set 2");
    rig.List(2, "set 3");
    CHECK(rig.Total() == 0);

    rig.Bang();
    CHECK(rig.At(0).lastInt == 1);
    CHECK(rig.At(1).lastInt == 2);
    CHECK(rig.At(2).lastInt == 3);
    CHECK(rig.Total() == 3);
  }

  TEST_CASE("bondo: 'set' stores a float as a float and text as text (#474)") {
    Rig rig("3");
    rig.List(0, "set 2.5");
    rig.List(1, "set hello");
    CHECK(rig.op->HeldKind(0) == gBondo::Held::FLOAT);
    CHECK(rig.op->HeldFloat(0) == doctest::Approx(2.5f));
    CHECK(rig.op->HeldKind(1) == gBondo::Held::LIST);
    CHECK(rig.op->HeldText(1) == "hello");
    CHECK(rig.Total() == 0);
  }

  TEST_CASE("bondo: 'set' performs the same store the bare message would (#474)") {
    // One storage rule for both paths, so `set` can never drift from the
    // release path it is supposed to be the quiet half of.
    Rig quiet("4");
    quiet.List(1, "set 10 20 30");

    Rig loud("4");
    loud.List(1, "10 20 30");

    for (int i = 0; i < 4; i++) {
      CAPTURE(i);
      CHECK(quiet.op->HeldKind(i) == loud.op->HeldKind(i));
      CHECK(quiet.op->HeldInt(i) == loud.op->HeldInt(i));
    }
    CHECK(quiet.Total() == 0);
    CHECK(loud.Total() == 4);
  }

  TEST_CASE("bondo: a bare 'set' is a symbol, not the set message (#474)") {
    // MatchWord requires a separator after the word, which is the rule the rest
    // of the patcher's word-plus-argument messages already follow: a message
    // word with no argument is not that message.
    Rig rig("2");
    rig.List(0, "set");
    CHECK(rig.Total() == 2);
    CHECK(rig.op->HeldKind(0) == gBondo::Held::LIST);
    CHECK(rig.op->HeldText(0) == "set");
  }

  // ─── release order ──────────────────────────────────────────────────────────

  TEST_CASE("bondo: outlets release right to left, not left to right (#474)") {
    Rig rig("4");
    rig.Bang();
    // Sinks are tagged 'a' (outlet 0) through 'd' (outlet 3). Right to left is
    // the reverse of the outlet order.
    CHECK(rig.Log() == "dcba");
    CHECK(rig.Total() == 4);
  }

  TEST_CASE("bondo: the release order is the same whatever caused it (#474)") {
    Rig rig("3");
    rig.Bang();
    CHECK(rig.Log() == "cba");
    rig.SendInt(0, 1);
    CHECK(rig.Log() == "cbacba");
    rig.SendFloat(1, 1.5f);
    CHECK(rig.Log() == "cbacbacba");
    rig.List(2, "some symbol");
    CHECK(rig.Log() == "cbacbacbacba");
    // ...including when the stimulus arrives at the rightmost inlet, which is
    // where a "start from the receiving inlet" mistake would show.
    rig.ClearLog();
    rig.SendInt(2, 4);
    CHECK(rig.Log() == "cba");
  }

  TEST_CASE("bondo: the release order holds across mixed slot kinds (#474)") {
    // Not all-empty, so a switch arm that forgot the reverse walk cannot hide.
    Rig rig("4");
    rig.List(0, "set 1");
    rig.List(1, "set 2.5");
    rig.List(2, "set word");
    // Slot 3 stays empty, so its arm is the 0 substitution.
    rig.Bang();
    CHECK(rig.Log() == "dcba");
    CHECK(rig.At(0).lastKind == OrderSink::INT);
    CHECK(rig.At(1).lastKind == OrderSink::FLOAT);
    CHECK(rig.At(2).lastKind == OrderSink::LIST);
    CHECK(rig.At(3).lastKind == OrderSink::INT);
  }

  TEST_CASE("bondo: the release order holds for many outlets (#474)") {
    // Not a small-n special case: twenty outlets come out in the reverse of the
    // alphabet, so an implementation that only got the two-port case right has
    // nowhere to hide.
    Rig rig("20");
    REQUIRE(rig.Ports() == 20);
    std::string expected;
    for (int i = 0; i < 20; i++)
      expected += (char)('a' + (19 - i));
    rig.Bang();
    CHECK(rig.Log() == expected);
  }

  TEST_CASE("bondo: every outlet fires exactly once per release (#474)") {
    Rig rig("5");
    rig.SendInt(2, 1);
    for (int i = 0; i < rig.Ports(); i++) {
      CAPTURE(i);
      CHECK(rig.At(i).count == 1);
    }
    rig.Bang();
    for (int i = 0; i < rig.Ports(); i++) {
      CAPTURE(i);
      CHECK(rig.At(i).count == 2);
    }
  }

  TEST_CASE("bondo: each send completes before the outlet to its left fires (#474)") {
    // The half of the guarantee a bare sequence check cannot see: a send does
    // not merely *start* before the one to its left, it finishes — the whole
    // subgraph behind it runs first. A breadth-first fan-out would still log
    // 'b' before 'a' at the top level while interleaving everything below.
    std::vector<char> order;

    gBondo outer; // two ports
    gBondo inner;

    // outer outlet 1 (released first) drives the inner bondo.
    outer.ConnectOutlet(inner.GetInlet(0), 1);
    inner.ConnectInlet(outer.GetOutlet(1), 0);

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

    outer.GetInlet(0)->SetBang(YSE::T_GUI);

    // The inner object's own right-to-left pair runs to completion ('y' then
    // 'x') before the outer object's left outlet is served ('z').
    CHECK(std::string(order.begin(), order.end()) == "yxz");
  }

  TEST_CASE("bondo: the left outlet acts on a set the right ones already placed (#474)") {
    // The idiom the guarantee exists for, end to end in a real patcher and
    // through real objects: the rightmost outlet stores into a cold inlet and
    // the leftmost one triggers the read. Under any other order the reader
    // fires against the *previous* set, which is the bug one level down from
    // the one .bondo exists to prevent.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* bondo = p.CreateObject(YSE::OBJ::G_BONDO, "2");
    YSE::pHandle* toBang = p.CreateObject(YSE::OBJ::G_TRIGGER, "b");
    YSE::pHandle* store = p.CreateObject(YSE::OBJ::G_INT);
    REQUIRE(bondo != nullptr);
    REQUIRE(toBang != nullptr);
    REQUIRE(store != nullptr);

    IntSink sink;
    YSE::pHandle sinkHandle(&sink);

    // Right outlet -> the silent inlet of .i; left outlet -> a bang -> its hot
    // inlet, which makes .i report whatever it is holding at that moment.
    p.Connect(bondo, 1, store, 1);
    p.Connect(bondo, 0, toBang, 0);
    p.Connect(toBang, 0, store, 0);
    p.Connect(store, 0, &sinkHandle, 0);

    bondo->SetListData(1, "set 42");
    bondo->SetIntData(0, 1);
    CHECK(sink.gotInt);
    CHECK(sink.received == 42);

    // Twice, with a different value: a left-to-right implementation would be
    // one release behind here (0 then 42) rather than wrong only on the first.
    bondo->SetListData(1, "set 7");
    bondo->SetIntData(0, 1);
    CHECK(sink.received == 7);
  }

  // ─── one logical event ──────────────────────────────────────────────────────

  TEST_CASE("bondo: .next reads a whole release as one event (#474)") {
    // The real object, not the raw clock — a wrong clock and a right comparison
    // look identical from one outlet. 'S' = separated (a new event), 'C' =
    // continued.
    std::vector<char> order;
    gBondo op;
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

    op.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(std::string(order.begin(), order.end()) == "SCCC");

    // A second stimulus is a second event, so exactly one more separated bang —
    // and a release caused by a stored value behaves the same as one caused by
    // a bang.
    order.clear();
    op.GetInlet(2)->SetInt(5, YSE::T_GUI);
    CHECK(std::string(order.begin(), order.end()) == "SCCC");
    CHECK(separated.count == 2);
    CHECK(continued.count == 6);
  }

  // ─── lists and symbols ──────────────────────────────────────────────────────

  TEST_CASE("bondo: a list is spread from the receiving inlet rightwards (#474)") {
    // Max: "The first element in the list is sent out the outlet which
    // corresponds to the inlet which received the list and each subsequent
    // element in the list is sent out each subsequent outlet."
    Rig rig("4");
    rig.List(1, "10 20 30");
    CHECK(rig.op->HeldKind(0) == gBondo::Held::NONE);
    CHECK(rig.op->HeldInt(1) == 10);
    CHECK(rig.op->HeldInt(2) == 20);
    CHECK(rig.op->HeldInt(3) == 30);
    CHECK(rig.Total() == 4);
    CHECK(rig.At(0).lastInt == 0);
  }

  TEST_CASE("bondo: list elements past the last outlet are dropped (#474)") {
    Rig rig("3");
    rig.List(2, "1 2 3 4");
    CHECK(rig.op->HeldKind(0) == gBondo::Held::NONE);
    CHECK(rig.op->HeldKind(1) == gBondo::Held::NONE);
    CHECK(rig.op->HeldInt(2) == 1);
    // No crash, no wrap-around into slot 0.
    CHECK(rig.Total() == 3);
  }

  TEST_CASE("bondo: a spread list keeps the int / float spelling of each element (#474)") {
    Rig rig("3");
    rig.List(0, "1 2.5 3e2");
    CHECK(rig.op->HeldKind(0) == gBondo::Held::INT);
    CHECK(rig.op->HeldInt(0) == 1);
    CHECK(rig.op->HeldKind(1) == gBondo::Held::FLOAT);
    CHECK(rig.op->HeldFloat(1) == doctest::Approx(2.5f));
    CHECK(rig.op->HeldKind(2) == gBondo::Held::FLOAT);
    CHECK(rig.op->HeldFloat(2) == doctest::Approx(300.f));
  }

  TEST_CASE("bondo: a non-numeric element of a spread list is stored as text (#474)") {
    Rig rig("3");
    rig.List(0, "1 word 3");
    CHECK(rig.op->HeldInt(0) == 1);
    CHECK(rig.op->HeldKind(1) == gBondo::Held::LIST);
    CHECK(rig.op->HeldText(1) == "word");
    CHECK(rig.op->HeldInt(2) == 3);
  }

  TEST_CASE("bondo: a message that does not lead with a number is stored whole (#474)") {
    // Max's `anything`: "The input is stored in the location corresponding to
    // that inlet." The leading-token test is Max's own rule for which method a
    // message reaches, and it is what this patcher can use in place of the
    // symbol type it does not have.
    Rig rig("3");
    rig.List(0, "note 60 100");
    CHECK(rig.op->HeldKind(0) == gBondo::Held::LIST);
    CHECK(rig.op->HeldText(0) == "note 60 100");
    // Nothing leaked into the slots to its right.
    CHECK(rig.op->HeldKind(1) == gBondo::Held::NONE);
    CHECK(rig.op->HeldKind(2) == gBondo::Held::NONE);
    CHECK(rig.At(0).lastKind == OrderSink::LIST);
    CHECK(rig.At(0).lastList == "note 60 100");
  }

  TEST_CASE("bondo: the 'n' argument stores a whole list per outlet (#474)") {
    // Max: "Using the symbol 'n' as an argument, bondo is able to synchronize
    // lists which arrive in different inlets."
    Rig rig("3 0 n");
    CHECK(rig.op->WholeLists());
    rig.List(0, "10 20 30");
    CHECK(rig.op->HeldKind(0) == gBondo::Held::LIST);
    CHECK(rig.op->HeldText(0) == "10 20 30");
    CHECK(rig.op->HeldKind(1) == gBondo::Held::NONE);
    CHECK(rig.op->HeldKind(2) == gBondo::Held::NONE);
    CHECK(rig.At(0).lastList == "10 20 30");
  }

  TEST_CASE("bondo: 'n' needs no delay argument to be recognised (#474)") {
    gBondo op;
    op.SetParams("3 n");
    CHECK(op.WholeLists());
    CHECK(op.PortCount() == 3);
    CHECK(op.RequestedDelay() == 0);
  }

  TEST_CASE("bondo: without 'n' a list is spread, with it the same list is not (#474)") {
    Rig spread("3");
    spread.List(0, "1 2 3");
    Rig whole("3 0 n");
    whole.List(0, "1 2 3");

    CHECK(spread.op->HeldKind(2) == gBondo::Held::INT);
    CHECK(whole.op->HeldKind(2) == gBondo::Held::NONE);
  }

  TEST_CASE("bondo: an empty list message is stored as empty text (#474)") {
    Rig rig("2");
    rig.List(0, "");
    CHECK(rig.op->HeldKind(0) == gBondo::Held::LIST);
    CHECK(rig.op->HeldText(0).empty());
    CHECK(rig.Total() == 2);
  }

  TEST_CASE("bondo: text longer than the reserved slot capacity is still stored (#474)") {
    // Correctness never depends on the reserve; exceeding it costs one
    // reallocation, not a truncated value.
    const std::string wide(gBondo::TEXT_CAPACITY * 2, 'x');
    Rig rig("2");
    rig.List(0, wide);
    CHECK(rig.op->HeldText(0) == wide);
    CHECK(rig.At(0).lastList == wide);
  }

  // ─── the delay argument ─────────────────────────────────────────────────────

  TEST_CASE("bondo: standalone, the delay argument falls back to an immediate release (#628)") {
    // The delay is honoured through the owning patcher's deferred-message
    // scheduler (#628) — see test_message_scheduler.cpp for that path. A
    // standalone object has no patcher and therefore no dispatch to defer
    // into, so the argument is still read (RequestedDelay reports it, keeping
    // Max's argument positions) and the release is immediate.
    Rig rig("2 250");
    CHECK(rig.op->PortCount() == 2);
    CHECK(rig.op->RequestedDelay() == 250);

    rig.SendInt(0, 3);
    CHECK(rig.Total() == 2);
    CHECK(rig.At(0).lastInt == 3);
  }

  TEST_CASE("bondo: no delay argument reports zero (#474)") {
    gBondo op;
    op.SetParams("3");
    CHECK(op.RequestedDelay() == 0);
    CHECK_FALSE(op.WholeLists());
  }

  // ─── Calculate ──────────────────────────────────────────────────────────────

  TEST_CASE("bondo: Calculate does not release anything (#474)") {
    // An emitting Calculate() would re-release the whole set on every DSP tick
    // from a stimulus no patch sent — the rule .sel, .trigger and .past set.
    Rig rig("3");
    rig.SendInt(0, 1);
    const int before = rig.Total();
    for (int i = 0; i < 5; i++)
      rig.op->Calculate(YSE::T_DSP);
    CHECK(rig.Total() == before);
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  TEST_CASE("bondo: re-parsing an empty parameter string returns to two ports (#474)") {
    gBondo op;
    op.SetParams("6 100 n");
    REQUIRE(op.PortCount() == 6);
    CHECK(op.WholeLists());
    CHECK(op.RequestedDelay() == 100);

    op.SetParams("");
    CHECK(op.PortCount() == gBondo::DEFAULT_PORTS);
    CHECK(op.NumInputs() == gBondo::DEFAULT_PORTS);
    CHECK(op.NumOutputs() == gBondo::DEFAULT_PORTS);
    CHECK_FALSE(op.WholeLists());
    CHECK(op.RequestedDelay() == 0);
  }

  TEST_CASE("bondo: re-parsing rebuilds the ports and forgets the held set (#474)") {
    gBondo op;
    op.GetInlet(0)->SetInt(5, YSE::T_GUI);
    REQUIRE(op.HeldInt(0) == 5);

    op.SetParams("4");
    CHECK(op.PortCount() == 4);
    // The slots the values belonged to no longer exist, so nothing is carried
    // over — a half-migrated set would be worse than a clean one.
    for (int i = 0; i < 4; i++) {
      CAPTURE(i);
      CHECK(op.HeldKind(i) == gBondo::Held::NONE);
    }
  }

  TEST_CASE("bondo: survives a DumpJSON / ParseJSON round trip (#474)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_BONDO);
    REQUIRE(h != nullptr);
    h->SetParams("5 120 n");
    const std::string json = src.DumpJSON();
    CHECK(json.find(".bondo") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".bondo"));
    CHECK(copy->GetParams() == std::string("5 120 n"));
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

  TEST_CASE("bondo: documents itself as GENERIC with a labelled port set (#474)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_BONDO));
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
    CHECK(obj->GetParamDocs()[0].name == "ports");
    CHECK(obj->GetParamDocs()[0].defaultValue == "2");
  }

  TEST_CASE("bondo: ports created by the parse callback are documented too (#474)") {
    gBondo op;
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

  TEST_CASE("bondo: every inlet accepts bang, int, float and list (#474)") {
    // All four on all of them, because all of them are hot — a cold inlet here
    // would be visible as a missing bang handler.
    gBondo op;
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

  TEST_CASE("bondo: at most 256 ports are built, and all of them release (#474)") {
    Rig rig("300");
    CHECK(rig.op->PortCount() == gBondo::MAX_PORTS);
    CHECK(rig.op->NumInputs() == gBondo::MAX_PORTS);
    CHECK(rig.Ports() == gBondo::MAX_PORTS);

    rig.Bang();
    CHECK(rig.Total() == gBondo::MAX_PORTS);
  }

} // TEST_SUITE("patcher")

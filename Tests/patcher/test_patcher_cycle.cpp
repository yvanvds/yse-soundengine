// Tests for .cycle (issue #477) — deal successive messages to successive
// outlets.
//
// Five rules carry this file, and each is one a plausible implementation gets
// wrong:
//
//   - **the rotation wraps, and only one outlet fires per message.** Max: "Each
//     incoming number is sent to the next outlet, wrapping around to the first
//     outlet after the last has been reached." An implementation that fans out
//     (a .trigger) or that stops at the last outlet passes every value test.
//   - **the position is advanced before the send.** A self-wired outlet
//     re-enters inside the Send, and an object that advances afterwards hands
//     the whole loop the same outlet.
//   - **a number-leading list is dealt token by token; anything else is not.**
//     Max documents plural outlets for one list. The leading-token test is
//     .bondo's, and getting it wrong either shreds `note 60 100` across three
//     outlets or turns the object's headline "deal a stream" use into a
//     one-outlet forward.
//   - **dealt tokens keep their spelling.** An int token that leaves as a float
//     silently rewrites the type of everything going through the object.
//   - **event mode is logical, not temporal.** It compares the dispatch layer's
//     event ids, so a whole .trigger fan-out is one event and two host calls are
//     two — pinned against a real .trigger in a real patcher rather than against
//     a clock.
//
// The rest is the message grammar from the Max reference: `set` moves the
// rotation without emitting, `thresh` sets the output mode at run time, and the
// creation arguments are `<outlets> [<mode>]`.
//
// The standalone rigs wire objects directly, as every sibling suite does; the
// event-mode and end-to-end cases run through a real patcher so the guarantees
// are asserted where a patch can see them.
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
#include "patcher/inlet.h"
#include "patcher/genericObjects/gCycle.h"
#include "patcher/sinks.hpp"

namespace {

  using TestHelpers::FloatSink;
  using TestHelpers::OrderSink;
  using YSE::PATCHER::gCycle;

  // One order-logging sink per outlet, all sharing one log, so "which outlet
  // received what, and in what order" is an assertion rather than an inference.
  // Outlet i is tagged 'a' + i, so the log reads back as the rotation itself.
  struct Rig {
    gCycle op;
    std::vector<std::unique_ptr<OrderSink>> sinks;
    std::vector<char> order;

    explicit Rig(const std::string& args = "") {
      if (!args.empty()) op.SetParams(args);
      Wire();
    }

    void Wire() {
      for (int i = 0; i < op.OutletCount(); i++) {
        sinks.push_back(std::unique_ptr<OrderSink>(new OrderSink()));
        sinks.back()->tag = (char)('a' + (i % 26));
        sinks.back()->log = &order;
        op.ConnectOutlet(sinks.back()->GetInlet(0), i);
        sinks.back()->ConnectInlet(op.GetOutlet(i), 0);
      }
    }

    void Bang(int inlet = 0) {
      op.GetInlet(inlet)->SetBang(YSE::T_GUI);
    }
    void SendInt(int v, int inlet = 0) {
      op.GetInlet(inlet)->SetInt(v, YSE::T_GUI);
    }
    void SendFloat(float v, int inlet = 0) {
      op.GetInlet(inlet)->SetFloat(v, YSE::T_GUI);
    }
    void List(const std::string& text, int inlet = 0) {
      op.GetInlet(inlet)->SetList(text, YSE::T_GUI);
    }

    std::string Log() const {
      return std::string(order.begin(), order.end());
    }
    void ClearLog() {
      order.clear();
    }
    int Total() const {
      int total = 0;
      for (const auto& sink : sinks)
        total += sink->count;
      return total;
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("cycle: creatable through the registry (#477)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_CYCLE, "4");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".cycle"));
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 4);
  }

  TEST_CASE("cycle: listed by pRegistry::AllNames (#477)") {
    auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(YSE::OBJ::G_CYCLE)) != names.end());
  }

  TEST_CASE("cycle: one outlet with no argument, as Max documents (#477)") {
    // Max: "If there is no argument, there will be one outlet." Degenerate, but
    // it is the documented default and a patch that meant more says so.
    gCycle op;
    CHECK(op.OutletCount() == 1);
    CHECK(op.NextOutlet() == 0);
    CHECK_FALSE(op.EventMode());
  }

  TEST_CASE("cycle: the outlet count is clamped to 1-256 (#477)") {
    gCycle low;
    low.SetParams("0");
    CHECK(low.OutletCount() == 1);

    gCycle negative;
    negative.SetParams("-7");
    CHECK(negative.OutletCount() == 1);

    gCycle high;
    high.SetParams("9999");
    CHECK(high.OutletCount() == gCycle::MAX_PORTS);

    // A float argument is truncated, and a non-number leaves the default.
    gCycle fractional;
    fractional.SetParams("3.9");
    CHECK(fractional.OutletCount() == 3);

    gCycle nonsense;
    nonsense.SetParams("wide");
    CHECK(nonsense.OutletCount() == 1);
  }

  TEST_CASE("cycle: SetParams(\"\") returns the object to Max's default shape (#477)") {
    gCycle op;
    op.SetParams("5 1");
    REQUIRE(op.OutletCount() == 5);
    REQUIRE(op.EventMode());

    op.SetParams("");
    CHECK(op.OutletCount() == 1);
    CHECK_FALSE(op.EventMode());
  }

  TEST_CASE("cycle: every outlet is ANY, since whatever arrived leaves as itself (#477)") {
    gCycle op;
    op.SetParams("3");
    for (int i = 0; i < op.OutletCount(); i++)
      CHECK(op.GetOutputType(i) == YSE::OUT_TYPE::ANY);
  }

  // ─── the rotation ───────────────────────────────────────────────────────────

  TEST_CASE("cycle: successive ints land on successive outlets and wrap (#477)") {
    // The object. Max: "Each incoming number is sent to the next outlet,
    // wrapping around to the first outlet after the last has been reached."
    Rig rig("3");
    for (int i = 1; i <= 7; i++)
      rig.SendInt(i);

    CHECK(rig.Log() == "abcabca");
    CHECK(rig.sinks[0]->lastInt == 7);
    CHECK(rig.sinks[1]->lastInt == 5);
    CHECK(rig.sinks[2]->lastInt == 6);
    CHECK(rig.op.NextOutlet() == 1);
  }

  TEST_CASE("cycle: exactly one outlet fires per message (#477)") {
    // The line between this object and .trigger, which fans one message out
    // every outlet. An implementation that fanned out would pass a test that
    // only looked at outlet 0.
    Rig rig("4");
    rig.SendInt(9);
    CHECK(rig.Total() == 1);
    CHECK(rig.sinks[0]->count == 1);
    CHECK(rig.sinks[1]->count == 0);
    CHECK(rig.sinks[2]->count == 0);
    CHECK(rig.sinks[3]->count == 0);
  }

  TEST_CASE("cycle: a bang takes its turn like any other message (#477)") {
    // Max: "bang: Sends a bang to the next outlet." Not a trigger and not a
    // query — it occupies a slot in the rotation.
    Rig rig("2");
    rig.Bang();
    rig.Bang();
    rig.Bang();
    CHECK(rig.Log() == "aba");
    CHECK(rig.sinks[0]->lastKind == OrderSink::BANG);
    CHECK(rig.sinks[1]->lastKind == OrderSink::BANG);
  }

  TEST_CASE("cycle: every message kind shares one rotation and keeps its kind (#477)") {
    // The object forwards, it does not compute, so nothing is widened or
    // narrowed in transit — and all four kinds advance the same counter.
    Rig rig("4");
    rig.SendInt(3);
    rig.SendFloat(2.5f);
    rig.List("hello world");
    rig.Bang();

    CHECK(rig.Log() == "abcd");
    CHECK(rig.sinks[0]->lastKind == OrderSink::INT);
    CHECK(rig.sinks[0]->lastInt == 3);
    CHECK(rig.sinks[1]->lastKind == OrderSink::FLOAT);
    CHECK(rig.sinks[1]->lastFloat == doctest::Approx(2.5f));
    CHECK(rig.sinks[2]->lastKind == OrderSink::LIST);
    CHECK(rig.sinks[2]->lastList == "hello world");
    CHECK(rig.sinks[3]->lastKind == OrderSink::BANG);
  }

  TEST_CASE("cycle: a one-outlet object is a pass-through (#477)") {
    // The default shape has nowhere to rotate to, so every message leaves the
    // one outlet and the position never moves off 0.
    Rig rig;
    REQUIRE(rig.op.OutletCount() == 1);
    rig.SendInt(1);
    rig.SendInt(2);
    rig.SendInt(3);
    CHECK(rig.Log() == "aaa");
    CHECK(rig.op.NextOutlet() == 0);
  }

  // ─── set ────────────────────────────────────────────────────────────────────

  TEST_CASE("cycle: 'set' moves the rotation and emits nothing (#477)") {
    // Max: "The word set, followed by a number, specifies an outlet to which
    // the next input should be directed."
    Rig rig("4");
    rig.List("set 2");
    CHECK(rig.Total() == 0);
    CHECK(rig.op.NextOutlet() == 2);

    rig.SendInt(7);
    CHECK(rig.Log() == "c");
    CHECK(rig.sinks[2]->lastInt == 7);
  }

  TEST_CASE("cycle: the rotation carries on from where 'set' left it (#477)") {
    // `set` is a move, not a one-shot redirect: the object goes on rotating
    // from the new position and wraps as usual.
    Rig rig("3");
    rig.List("set 2");
    for (int i = 0; i < 4; i++)
      rig.SendInt(i);
    CHECK(rig.Log() == "cabc");
  }

  TEST_CASE("cycle: a 'set' index outside the outlet range is ignored (#477)") {
    // .gate's discipline. Wrapping would be defensible in an object that wraps
    // everything else, but a `set 4` on a three-outlet object is a miscount and
    // quietly dealing its stream to outlet 1 would hide it.
    Rig rig("3");
    rig.SendInt(0); // rotation now stands at outlet 1
    REQUIRE(rig.op.NextOutlet() == 1);

    rig.List("set 3");
    CHECK(rig.op.NextOutlet() == 1);
    rig.List("set -1");
    CHECK(rig.op.NextOutlet() == 1);
    CHECK(rig.Total() == 1); // neither one emitted

    rig.SendInt(1);
    CHECK(rig.Log() == "ab");
  }

  TEST_CASE("cycle: a bare 'set' is an ordinary message, not a move (#477)") {
    // The word has to end where it ends and be followed by an argument, the
    // rule MatchWord enforces for every `<word> <number>` message in the
    // patcher. A message word with no argument is not the same message.
    Rig rig("2");
    rig.List("set");
    CHECK(rig.Total() == 1);
    CHECK(rig.sinks[0]->lastKind == OrderSink::LIST);
    CHECK(rig.sinks[0]->lastList == "set");

    rig.List("settings 1");
    CHECK(rig.sinks[1]->lastList == "settings 1");
    CHECK(rig.op.NextOutlet() == 0);
  }

  // ─── lists ──────────────────────────────────────────────────────────────────

  TEST_CASE("cycle: a number-leading list is dealt one token per outlet (#477)") {
    // Max, for `list`: "The stream of ints, floats, or symbols to be directed to
    // successive outlets" — plural outlets for one message. This is the "deal a
    // stream into parallel paths" use, said in one message instead of N.
    Rig rig("3");
    rig.List("10 20 30");
    CHECK(rig.Log() == "abc");
    CHECK(rig.sinks[0]->lastInt == 10);
    CHECK(rig.sinks[1]->lastInt == 20);
    CHECK(rig.sinks[2]->lastInt == 30);
    CHECK(rig.op.NextOutlet() == 0);
  }

  TEST_CASE("cycle: a dealt list wraps round the outlets (#477)") {
    // Unlike .bondo, which drops the elements past its last outlet: there the
    // list is a set of slots, here it is a stream, and a stream wraps.
    Rig rig("2");
    rig.List("1 2 3 4 5");
    CHECK(rig.Log() == "ababa");
    CHECK(rig.sinks[0]->lastInt == 5);
    CHECK(rig.sinks[1]->lastInt == 4);
    CHECK(rig.op.NextOutlet() == 1);
  }

  TEST_CASE("cycle: dealt tokens keep their spelling (#477)") {
    // The int-atom / float-atom test .trigger classifies its constants with and
    // .bondo stores its spread elements by. An implementation that normalised
    // every token to float would pass every value check while rewriting the
    // type of everything downstream (.route 1, .sel, .i and .match all tell
    // them apart).
    Rig rig("3");
    rig.List("1 2. 3");
    CHECK(rig.sinks[0]->lastKind == OrderSink::INT);
    CHECK(rig.sinks[0]->lastInt == 1);
    CHECK(rig.sinks[1]->lastKind == OrderSink::FLOAT);
    CHECK(rig.sinks[1]->lastFloat == doctest::Approx(2.f));
    CHECK(rig.sinks[2]->lastKind == OrderSink::INT);
    CHECK(rig.sinks[2]->lastInt == 3);
  }

  TEST_CASE("cycle: a symbol-leading message is not split (#477)") {
    // Max's `anything`, told from `list` by the leading token the way Max's own
    // parser tells them apart and .bondo already does. Dealing `note 60 100`
    // across three outlets would arrive downstream as three unrelated fragments
    // and would hide the leading word from every object that matches on one.
    Rig rig("3");
    rig.List("note 60 100");
    CHECK(rig.Total() == 1);
    CHECK(rig.sinks[0]->lastKind == OrderSink::LIST);
    CHECK(rig.sinks[0]->lastList == "note 60 100");
    CHECK(rig.op.NextOutlet() == 1);
  }

  TEST_CASE("cycle: a symbol inside a numeric list is dealt as text (#477)") {
    // The message is a `list` — its first token is a number — so every token
    // takes its turn, and one that is not a number goes out as itself.
    Rig rig("3");
    rig.List("1 two 3");
    CHECK(rig.Log() == "abc");
    CHECK(rig.sinks[0]->lastInt == 1);
    CHECK(rig.sinks[1]->lastKind == OrderSink::LIST);
    CHECK(rig.sinks[1]->lastList == "two");
    CHECK(rig.sinks[2]->lastInt == 3);
  }

  TEST_CASE("cycle: dealing leaves the rotation where it stopped (#477)") {
    // A dealt list is a run of ordinary messages, not an event of its own: the
    // next message picks up immediately after the last token.
    Rig rig("4");
    rig.List("1 2 3");
    rig.SendInt(9);
    CHECK(rig.Log() == "abcd");
    CHECK(rig.sinks[3]->lastInt == 9);
  }

  // ─── event mode ─────────────────────────────────────────────────────────────

  TEST_CASE("cycle: event mode is off by default and separate calls keep rotating (#477)") {
    // Max's mode argument defaults to 0, "values cycle through all outlets
    // regardless of event boundaries". Each of these calls is its own logical
    // event, and the rotation ignores that.
    Rig rig("2");
    rig.SendInt(1);
    rig.SendInt(2);
    rig.SendInt(3);
    CHECK(rig.Log() == "aba");
  }

  TEST_CASE("cycle: the second creation argument turns event mode on (#477)") {
    gCycle op;
    op.SetParams("3 1");
    CHECK(op.OutletCount() == 3);
    CHECK(op.EventMode());

    // Explicitly zero is off, as is a missing argument.
    op.SetParams("3 0");
    CHECK_FALSE(op.EventMode());

    // Max: "If it is non-zero" — any non-zero value, not only 1.
    op.SetParams("3 -2");
    CHECK(op.EventMode());
  }

  TEST_CASE("cycle: in event mode each new event restarts at outlet 0 (#477)") {
    // Max: "cycle detects separate 'events' and restarts at the leftmost outlet
    // when a new event occurs." Every call below is a separate stimulus, so
    // every message goes out outlet 0 — the visible difference from the default
    // mode, which would read "abab".
    Rig rig("2 1");
    rig.SendInt(1);
    rig.SendInt(2);
    rig.SendInt(3);
    rig.SendInt(4);
    CHECK(rig.Log() == "aaaa");
    CHECK(rig.sinks[0]->count == 4);
    CHECK(rig.sinks[1]->count == 0);
  }

  TEST_CASE("cycle: in event mode one event's burst keeps rotating (#477)") {
    // The half that makes the mode logical rather than temporal, pinned through
    // a real .trigger in a real patcher: its whole right-to-left fan-out is one
    // logical event however many outlets it has, so the burst rotates and only
    // the *next* stimulus restarts. A clock-based implementation gets this
    // exactly backwards.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* trigger = p.CreateObject(YSE::OBJ::G_TRIGGER, "b b b");
    YSE::pHandle* cycle = p.CreateObject(YSE::OBJ::G_CYCLE, "2 1");
    REQUIRE(trigger != nullptr);
    REQUIRE(cycle != nullptr);
    REQUIRE(trigger->GetOutputs() == 3);

    std::vector<char> order;
    OrderSink left;
    OrderSink right;
    left.tag = 'a';
    right.tag = 'b';
    left.log = right.log = &order;
    YSE::pHandle leftHandle(&left);
    YSE::pHandle rightHandle(&right);

    for (int i = 0; i < 3; i++)
      p.Connect(trigger, i, cycle, 0);
    p.Connect(cycle, 0, &leftHandle, 0);
    p.Connect(cycle, 1, &rightHandle, 0);

    // One stimulus, three bangs: the rotation runs 0, 1, 0 within it.
    trigger->SetBang(0);
    CHECK(std::string(order.begin(), order.end()) == "aba");

    // A second stimulus is a second event, so it restarts rather than
    // continuing from outlet 1 — which is what "abaaba" says and "ababab"
    // (the event-blind reading) would not.
    trigger->SetBang(0);
    CHECK(std::string(order.begin(), order.end()) == "abaaba");
  }

  TEST_CASE("cycle: with event mode off a burst and the next event share one rotation (#477)") {
    // The same rig with the mode argument dropped, so the two runs differ only
    // in the mode: here the second stimulus carries on from outlet 1.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* trigger = p.CreateObject(YSE::OBJ::G_TRIGGER, "b b b");
    YSE::pHandle* cycle = p.CreateObject(YSE::OBJ::G_CYCLE, "2");
    REQUIRE(trigger != nullptr);
    REQUIRE(cycle != nullptr);

    std::vector<char> order;
    OrderSink left;
    OrderSink right;
    left.tag = 'a';
    right.tag = 'b';
    left.log = right.log = &order;
    YSE::pHandle leftHandle(&left);
    YSE::pHandle rightHandle(&right);

    for (int i = 0; i < 3; i++)
      p.Connect(trigger, i, cycle, 0);
    p.Connect(cycle, 0, &leftHandle, 0);
    p.Connect(cycle, 1, &rightHandle, 0);

    trigger->SetBang(0);
    trigger->SetBang(0);
    CHECK(std::string(order.begin(), order.end()) == "ababab");
  }

  TEST_CASE("cycle: 'thresh' sets the output mode at run time (#477)") {
    // Max: "The word thresh, followed by a number, sets the output mode."
    Rig rig("2");
    REQUIRE_FALSE(rig.op.EventMode());

    rig.List("thresh 1");
    CHECK(rig.op.EventMode());
    CHECK(rig.Total() == 0); // it emits nothing

    rig.SendInt(1);
    rig.SendInt(2);
    CHECK(rig.Log() == "aa"); // each call is its own event

    // The `thresh 0` message is itself a new event and so restarts the rotation
    // on the way in, while the mode it turns off applies from then on: the two
    // calls after it rotate instead of both landing on outlet 0, which is the
    // difference the mode makes.
    rig.List("thresh 0");
    CHECK_FALSE(rig.op.EventMode());
    CHECK(rig.op.NextOutlet() == 0);
    rig.ClearLog();
    rig.SendInt(3);
    rig.SendInt(4);
    CHECK(rig.Log() == "ab");
  }

  TEST_CASE("cycle: a bare 'thresh' is an ordinary message (#477)") {
    Rig rig("2");
    rig.List("thresh");
    CHECK_FALSE(rig.op.EventMode());
    CHECK(rig.sinks[0]->lastList == "thresh");
  }

  TEST_CASE("cycle: 'set' holds for the rest of its own event (#477)") {
    // Not a special case in the code and not one here either: the restart is
    // applied when a message arrives, so a `set` and the messages that share its
    // event see the position it asked for, and the next event restarts over the
    // top of it. Both halves are asserted, since an implementation that
    // restarted after the send would pass the first.
    Rig rig("3 1");
    {
      YSE::PATCHER::messageEventScope event;
      rig.List("set 2");
      rig.SendInt(1);
      rig.SendInt(2);
    }
    CHECK(rig.Log() == "ca");

    // A fresh event: back to outlet 0, `set` and all.
    rig.ClearLog();
    rig.SendInt(3);
    CHECK(rig.Log() == "a");
  }

  TEST_CASE("cycle: messages sharing one event keep rotating in event mode (#477)") {
    // The standalone counterpart of the .trigger case above, so the grouping is
    // pinned against the dispatch layer directly as well as through a patch.
    Rig rig("2 1");
    {
      YSE::PATCHER::messageEventScope event;
      rig.SendInt(1);
      rig.SendInt(2);
      rig.SendInt(3);
    }
    CHECK(rig.Log() == "aba");
  }

  // ─── re-entrancy ────────────────────────────────────────────────────────────

  TEST_CASE("cycle: a self-wired outlet does not re-use the outlet it is sending from (#477)") {
    // The advance happens before the send, so a message that loops back in
    // arrives to find the rotation already moved on. With the advance after the
    // send, the loop would be handed outlet 0 twice and the log would read "aa"
    // before the wrap put the object one step behind for good.
    gCycle op;
    op.SetParams("3");

    std::vector<char> order;
    OrderSink second;
    OrderSink third;
    second.tag = 'b';
    third.tag = 'c';
    second.log = third.log = &order;

    // Outlet 0 loops straight back into the inlet; outlets 1 and 2 are watched.
    op.ConnectOutlet(op.GetInlet(0), 0);
    op.ConnectInlet(op.GetOutlet(0), 0);
    op.ConnectOutlet(second.GetInlet(0), 1);
    second.ConnectInlet(op.GetOutlet(1), 0);
    op.ConnectOutlet(third.GetInlet(0), 2);
    third.ConnectInlet(op.GetOutlet(2), 0);

    // One int: outlet 0 takes it and feeds it back, the re-entrant delivery
    // finds the rotation at outlet 1, and that one is not wired back — so the
    // loop terminates after exactly one bounce.
    op.GetInlet(0)->SetInt(5, YSE::T_GUI);
    CHECK(std::string(order.begin(), order.end()) == "b");
    CHECK(second.lastInt == 5);
    CHECK(third.count == 0);
    CHECK(op.NextOutlet() == 2);
  }

  // ─── real-time / graph ──────────────────────────────────────────────────────

  TEST_CASE("cycle: Calculate() emits nothing (#477)") {
    // The object is driven by its inlet. An emitting Calculate() would deal one
    // message per DSP block from a stimulus no patch sent.
    Rig rig("3");
    for (int i = 0; i < 8; i++)
      rig.op.Calculate(YSE::T_DSP);
    CHECK(rig.Total() == 0);
    CHECK(rig.op.NextOutlet() == 0);
  }

  TEST_CASE("cycle: survives a DumpJSON / ParseJSON round trip (#477)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_CYCLE);
    REQUIRE(h != nullptr);
    h->SetParams("5 1");
    const std::string json = src.DumpJSON();
    CHECK(json.find(".cycle") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".cycle"));
    CHECK(copy->GetParams() == std::string("5 1"));
    // The shape has to come back with it, or the saved patch cords land on the
    // wrong outlets — and so does the mode, which is the difference between a
    // voice bank that stays aligned and one that drifts.
    CHECK(copy->GetOutputs() == 5);
  }

  TEST_CASE("cycle: 'thresh' is run-time state and does not rewrite the parameters (#477)") {
    // The creation argument is what a saved patch carries, exactly as .bondo's
    // `set` and .uzi's `pause` are run-time state rather than saved state.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_CYCLE, "3");
    REQUIRE(h != nullptr);
    h->SetListData(0, "thresh 1");
    CHECK(h->GetParams() == std::string("3"));
  }

  // ─── end to end ─────────────────────────────────────────────────────────────

  TEST_CASE("cycle: deals a stream across two chains in a real patcher (#477)") {
    // The headline use, end to end through real objects: one stream in, two
    // processing paths out, alternating. Each path adds a different constant, so
    // the sink's values are only right if the messages went where they should.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* cycle = p.CreateObject(YSE::OBJ::G_CYCLE, "2");
    YSE::pHandle* addTen = p.CreateObject(YSE::OBJ::G_ADD, "10");
    YSE::pHandle* addHundred = p.CreateObject(YSE::OBJ::G_ADD, "100");
    REQUIRE(cycle != nullptr);
    REQUIRE(addTen != nullptr);
    REQUIRE(addHundred != nullptr);

    FloatSink evens;
    FloatSink odds;
    YSE::pHandle evenHandle(&evens);
    YSE::pHandle oddHandle(&odds);

    p.Connect(cycle, 0, addTen, 0);
    p.Connect(cycle, 1, addHundred, 0);
    p.Connect(addTen, 0, &evenHandle, 0);
    p.Connect(addHundred, 0, &oddHandle, 0);

    cycle->SetIntData(0, 1);
    CHECK(evens.gotFloat);
    CHECK(evens.received == doctest::Approx(11.f));
    CHECK_FALSE(odds.gotFloat);

    cycle->SetIntData(0, 2);
    CHECK(odds.gotFloat);
    CHECK(odds.received == doctest::Approx(102.f));

    cycle->SetIntData(0, 3);
    CHECK(evens.received == doctest::Approx(13.f)); // wrapped back to the first path

    // And one list deals across both paths in a single message.
    cycle->SetListData(0, "5 6");
    CHECK(odds.received == doctest::Approx(105.f));
    CHECK(evens.received == doctest::Approx(16.f));
  }

} // TEST_SUITE("patcher")

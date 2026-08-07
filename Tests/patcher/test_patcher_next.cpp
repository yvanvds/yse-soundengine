// Tests for .next (issue #471) — tell the first message of a burst from the
// rest of it.
//
// This object is not what its title suggests, so the file starts by pinning
// what it is. Max's `next` "detects separation of messages", but the separation
// is **logical, not temporal**: it groups messages by the stimulus that caused
// them, measures no interval, and holds no clock. Max's own examples are the
// specification —
//
//   "if you click on a bang twice, the two bangs are not part of the same
//    logical event. But if you put `bang, bang` in a message box, or use the
//    `uzi` object to send out two bangs in a row, these bangs are part of the
//    same logical event."
//
// — so two stimuli are two events *however fast they arrive*, and one stimulus
// is one event *however many boxes it passes through*. A timer-based
// implementation gets both of those backwards, and the tests below fail it in
// both directions: BurstOfTwo() delivers its two messages back to back with no
// delay and requires them to read as one event, while TwoStimuli() delivers two
// with no delay either and requires them to read as two. Nothing here sleeps,
// and nothing here would behave differently if it did.
//
// The file is in three parts:
//
//   - **the event clock**, tested on its own through a probe that records
//     `CurrentMessageEvent()` wherever a message reaches it. That is the piece
//     #471 added to the dispatch layer, and testing it directly is what
//     separates "the object's comparison is right" from "the grouping it
//     compares is right" — a `.next` built on a broken clock passes every
//     single-outlet check.
//   - **the object**, over the four groupings that matter: the first message
//     ever, a second stimulus, a fan-out, and a deep chain.
//   - **the invariants**, asserted over whole streams rather than per message:
//     exactly one outlet per message, and the payload never changing the
//     answer.
//
// `.trigger b b` is this patcher's `bang, bang` message box — one input, two
// synchronous sends — so it is the burst generator throughout. OrderSink's
// counters are cumulative, so every sequencing assertion reads the shared
// order log instead.
//
// The rigs wire objects directly rather than through a patcher, as the sibling
// suites do; one test at the end runs the whole thing through a real patcher so
// the registry, the wiring API and the object agree.
//
// No audio device required.

#include <doctest/doctest.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/genericObjects/gNext.h"
#include "patcher/genericObjects/gTrigger.h"
#include "sinks.hpp"

namespace {

  using YSE::PATCHER::CurrentMessageEvent;
  using YSE::PATCHER::gNext;
  using YSE::PATCHER::gTrigger;

  // Records the logical event id in force wherever a message reaches it, for
  // every message kind. The clock is what #471 added below the object, so it
  // gets asserted on its own terms rather than only through .next's outlets.
  struct EventProbe : YSE::PATCHER::pObject {
    std::vector<std::uint64_t>* log = nullptr;
    std::uint64_t last = 0;
    int count = 0;

    EventProbe() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { Record(); });
      inputs.back().RegisterInt([this](int, int, YSE::THREAD) { Record(); });
      inputs.back().RegisterFloat([this](float, int, YSE::THREAD) { Record(); });
      inputs.back().RegisterList([this](const std::string&, int, YSE::THREAD) { Record(); });
    }
    const char* Type() const override {
      return "event_probe";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {
      Record();
    }

  private:
    void Record() {
      last = CurrentMessageEvent();
      count++;
      if (log) log->push_back(last);
    }
  };

  // Both outlets, each with its own tag, logging into one vector so a test can
  // read back the exact sequence of verdicts. 'S' = separated (outlet 0, a new
  // event), 'C' = continued (outlet 1, more of the event already running).
  struct Rig {
    std::unique_ptr<gNext> op;
    TestHelpers::OrderSink separated;
    TestHelpers::OrderSink same;
    std::vector<char> log;

    Rig() : op(new gNext()) {
      Wire(separated, 0, 'S');
      Wire(same, 1, 'C');
    }

    // ── one message, one stimulus ──
    void Bang() {
      op->GetInlet(0)->SetBang(YSE::T_GUI);
    }
    void SendInt(int v) {
      op->GetInlet(0)->SetInt(v, YSE::T_GUI);
    }
    void Send(float v) {
      op->GetInlet(0)->SetFloat(v, YSE::T_GUI);
    }
    void List(const std::string& text) {
      op->GetInlet(0)->SetList(text, YSE::T_GUI);
    }

    std::string Order() const {
      return std::string(log.begin(), log.end());
    }
    void Clear() {
      log.clear();
    }
    int Total() const {
      return separated.count + same.count;
    }

  private:
    void Wire(TestHelpers::OrderSink& sink, int outlet, char tag) {
      sink.log = &log;
      sink.tag = tag;
      op->ConnectOutlet(sink.GetInlet(0), outlet);
      sink.ConnectInlet(op->GetOutlet(outlet), 0);
    }
  };

  // A `.trigger` with `n` bang outlets, all wired into `target`'s inlet 0. One
  // message into the trigger therefore delivers `n` messages to the target
  // inside a single stimulus — this patcher's `bang, bang` message box, and the
  // `uzi` of Max's example.
  struct Burst {
    gTrigger trig;
    explicit Burst(int n, YSE::PATCHER::pObject& target) {
      std::string args;
      for (int i = 0; i < n; i++) {
        if (i > 0) args += " ";
        args += "b";
      }
      trig.SetParams(args);
      for (int i = 0; i < n; i++) {
        trig.ConnectOutlet(target.GetInlet(0), i);
        target.ConnectInlet(trig.GetOutlet(i), 0);
      }
    }
    // One stimulus, delivering every outlet synchronously.
    void Fire() {
      trig.GetInlet(0)->SetBang(YSE::T_GUI);
    }
  };

  // A sink that feeds what it receives straight back into the object's inlet,
  // from *inside* the send, so the re-entrant message is genuinely nested in
  // the same dispatch. Bounded by its own depth limit so a broken object fails
  // by assertion rather than by exhausting the stack.
  struct FeedbackSink : YSE::PATCHER::pObject {
    gNext* target = nullptr;
    std::vector<char>* log = nullptr;
    char tag = '?';
    int hits = 0;
    int depth = 0;
    static constexpr int DEPTH_LIMIT = 8;

    FeedbackSink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) {
        hits++;
        if (log) log->push_back(tag);
        depth++;
        if (depth < DEPTH_LIMIT && target != nullptr) {
          target->GetInlet(0)->SetBang(YSE::T_GUI);
        }
        depth--;
      });
    }
    const char* Type() const override {
      return "feedback_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── the event clock ────────────────────────────────────────────────────────
  // The piece #471 added underneath the object. Every grouping .next reports is
  // this clock's answer, so a wrong clock and a right comparison look exactly
  // like a correct object from the outlets alone.

  TEST_CASE("next: no event is in progress outside a dispatch (#471)") {
    // 0 is the documented "no dispatch" value, and .next relies on it: two
    // handlers called directly, outside any inlet, must not read as one event.
    CHECK(CurrentMessageEvent() == 0);

    EventProbe probe;
    probe.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(probe.last != 0);

    // ...and it is back to 0 once the dispatch has unwound, rather than left
    // holding the id of the event that just finished.
    CHECK(CurrentMessageEvent() == 0);
  }

  TEST_CASE("next: two separate stimuli are two events (#471)") {
    std::vector<std::uint64_t> ids;
    EventProbe probe;
    probe.log = &ids;

    probe.GetInlet(0)->SetBang(YSE::T_GUI);
    probe.GetInlet(0)->SetBang(YSE::T_GUI);

    REQUIRE(ids.size() == 2);
    CHECK(ids[0] != 0);
    CHECK(ids[1] != 0);
    // Delivered back to back with nothing in between, which is the case an
    // elapsed-time implementation would fold into one.
    CHECK(ids[0] != ids[1]);
  }

  TEST_CASE("next: every message a single stimulus causes shares one event (#471)") {
    std::vector<std::uint64_t> ids;
    EventProbe probe;
    probe.log = &ids;

    Burst burst(4, probe);
    burst.Fire();

    REQUIRE(ids.size() == 4);
    CHECK(ids[0] != 0);
    for (std::size_t i = 1; i < ids.size(); i++) {
      CAPTURE(i);
      CHECK(ids[i] == ids[0]);
    }
  }

  TEST_CASE("next: the event survives an arbitrarily deep chain of boxes (#471)") {
    // Nesting, not fan-out: outer -> inner -> probe, so the recorded ids come
    // from three different depths of the same dispatch.
    std::vector<std::uint64_t> ids;
    EventProbe probe;
    probe.log = &ids;

    gTrigger outer;
    outer.SetParams("b b");
    gTrigger inner;
    inner.SetParams("b b");

    // outer outlet 1 drives the inner trigger; outer outlet 0 goes straight to
    // the probe. The probe therefore sees inner's two outlets and then outer's
    // remaining one.
    outer.ConnectOutlet(inner.GetInlet(0), 1);
    inner.ConnectInlet(outer.GetOutlet(1), 0);
    outer.ConnectOutlet(probe.GetInlet(0), 0);
    probe.ConnectInlet(outer.GetOutlet(0), 0);
    inner.ConnectOutlet(probe.GetInlet(0), 0);
    probe.ConnectInlet(inner.GetOutlet(0), 0);
    inner.ConnectOutlet(probe.GetInlet(0), 1);
    probe.ConnectInlet(inner.GetOutlet(1), 0);

    outer.GetInlet(0)->SetBang(YSE::T_GUI);

    REQUIRE(ids.size() == 3);
    CHECK(ids[0] != 0);
    CHECK(ids[1] == ids[0]);
    CHECK(ids[2] == ids[0]);
  }

  TEST_CASE("next: event ids are never reused (#471)") {
    // .next only ever compares ids, so a recycled one would silently merge two
    // unrelated bursts.
    std::vector<std::uint64_t> ids;
    EventProbe probe;
    probe.log = &ids;

    for (int i = 0; i < 200; i++) {
      probe.GetInlet(0)->SetBang(YSE::T_GUI);
    }
    REQUIRE(ids.size() == 200);
    std::set<std::uint64_t> unique(ids.begin(), ids.end());
    CHECK(unique.size() == 200);
    CHECK(unique.find(0) == unique.end());
  }

  TEST_CASE("next: every message kind opens an event (#471)") {
    // The clock lives in five separate inlet setters; a scope missing from one
    // of them would leave that path reporting whatever ran last.
    EventProbe probe;
    std::vector<std::uint64_t> ids;
    probe.log = &ids;

    probe.GetInlet(0)->SetBang(YSE::T_GUI);
    probe.GetInlet(0)->SetInt(3, YSE::T_GUI);
    probe.GetInlet(0)->SetFloat(2.5f, YSE::T_GUI);
    probe.GetInlet(0)->SetList("a b", YSE::T_GUI);
    probe.GetInlet(0)->SetMessage("hello", YSE::T_GUI);

    REQUIRE(ids.size() == 5);
    std::set<std::uint64_t> unique(ids.begin(), ids.end());
    CHECK(unique.size() == 5);
    CHECK(unique.find(0) == unique.end());
  }

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("next: creatable through the registry with one inlet and two bang outlets (#471)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_NEXT);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".next"));
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 2);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BANG);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::BANG);
  }

  TEST_CASE("next: listed by pRegistry::AllNames (#471)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".next")) != names.end());
  }

  // ─── the groupings ──────────────────────────────────────────────────────────

  TEST_CASE("next: the first message ever is separated (#471)") {
    // There is no previous message for it to share an event with. This is what
    // lets a patch read outlet 0 as "a new burst starts here" and be right
    // about the first burst too.
    Rig rig;
    CHECK(rig.op->HasSeenMessage() == false);
    CHECK(rig.op->LastWasSeparated() == true);

    rig.Bang();
    CHECK(rig.Order() == "S");
    CHECK(rig.op->HasSeenMessage() == true);
    CHECK(rig.op->LastWasSeparated() == true);
  }

  TEST_CASE("next: separate stimuli are all separated, however fast they arrive (#471)") {
    // The "click on a bang twice" case, at machine speed. A threshold-based
    // implementation collapses this into "S" followed by five "C"s.
    Rig rig;
    for (int i = 0; i < 6; i++) {
      rig.Bang();
    }
    CHECK(rig.Order() == "SSSSSS");
  }

  TEST_CASE("next: a two-message burst is separated then continued (#471)") {
    // The `bang, bang` message box of Max's example, expressed with the box
    // this patcher has for it.
    Rig rig;
    Burst burst(2, *rig.op);
    burst.Fire();
    CHECK(rig.Order() == "SC");
  }

  TEST_CASE("next: only the first message of a long burst is separated (#471)") {
    Rig rig;
    Burst burst(12, *rig.op);
    burst.Fire();
    CHECK(rig.Order() == "SCCCCCCCCCCC");
    CHECK(rig.separated.count == 1);
    CHECK(rig.same.count == 11);
  }

  TEST_CASE("next: each burst starts a new event (#471)") {
    // The once-per-dump use, over three dumps: exactly one bang out of outlet 0
    // per stimulus, whatever the burst length.
    Rig rig;
    Burst burst(4, *rig.op);
    burst.Fire();
    burst.Fire();
    burst.Fire();
    CHECK(rig.Order() == "SCCCSCCCSCCC");
    CHECK(rig.separated.count == 3);
  }

  TEST_CASE("next: a burst reaching through a chain of boxes is still one event (#471)") {
    // Nested rather than fanned out, so the grouping cannot come from anything
    // as shallow as "which object sent it".
    Rig rig;

    gTrigger outer;
    outer.SetParams("b b");
    gTrigger inner;
    inner.SetParams("b b");

    outer.ConnectOutlet(inner.GetInlet(0), 1);
    inner.ConnectInlet(outer.GetOutlet(1), 0);
    outer.ConnectOutlet(rig.op->GetInlet(0), 0);
    rig.op->ConnectInlet(outer.GetOutlet(0), 0);
    inner.ConnectOutlet(rig.op->GetInlet(0), 0);
    rig.op->ConnectInlet(inner.GetOutlet(0), 0);
    inner.ConnectOutlet(rig.op->GetInlet(0), 1);
    rig.op->ConnectInlet(inner.GetOutlet(1), 0);

    outer.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Order() == "SCC");

    rig.Clear();
    outer.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Order() == "SCC");
  }

  TEST_CASE("next: a lone stimulus between two bursts is separated (#471)") {
    Rig rig;
    Burst burst(3, *rig.op);

    burst.Fire();
    rig.Bang();
    burst.Fire();
    CHECK(rig.Order() == "SCCSSCC");
  }

  // ─── payload independence ───────────────────────────────────────────────────

  TEST_CASE("next: every message kind is tested the same way (#471)") {
    // Max gives bang, int and float the description "Performs the same as
    // anything", so the payload is documented as irrelevant.
    Rig rig;
    rig.Bang();
    rig.SendInt(7);
    rig.Send(2.5f);
    rig.List("wobble bar");
    CHECK(rig.Order() == "SSSS");
  }

  TEST_CASE("next: a 0 separates like any other message (#471)") {
    // Worth stating because a 0 reads like a "don't" elsewhere in the family.
    // This is not .gate: no value means off here.
    Rig rig;
    rig.SendInt(0);
    CHECK(rig.Order() == "S");
    rig.Send(0.f);
    CHECK(rig.Order() == "SS");
  }

  TEST_CASE("next: an empty list is a message like any other (#471)") {
    Rig rig;
    rig.List("");
    CHECK(rig.Order() == "S");
    rig.List("");
    CHECK(rig.Order() == "SS");
  }

  TEST_CASE("next: no word is a message — 'reset', 'set' and 'stop' are ordinary input (#471)") {
    // Max lists no messages for this object, and here every symbol is an
    // ordinary tested message, so any word learned would be a message that
    // stopped being tested.
    Rig rig;
    rig.List("reset");
    rig.List("set 5");
    rig.List("stop");
    rig.List("clear");
    CHECK(rig.Order() == "SSSS");
    CHECK(rig.separated.count == 4);
    CHECK(rig.same.count == 0);
  }

  TEST_CASE("next: the payload never changes the verdict inside a burst (#471)") {
    // Same burst shape as the bang case above, but delivered as ints. `.trigger
    // i i i` sends the value out every outlet, so the grouping is the only
    // thing that could distinguish these messages — and it does not.
    Rig rig;
    gTrigger trig;
    trig.SetParams("i i i");
    for (int i = 0; i < 3; i++) {
      trig.ConnectOutlet(rig.op->GetInlet(0), i);
      rig.op->ConnectInlet(trig.GetOutlet(i), 0);
    }
    trig.GetInlet(0)->SetInt(42, YSE::T_GUI);
    CHECK(rig.Order() == "SCC");

    rig.Clear();
    trig.GetInlet(0)->SetInt(42, YSE::T_GUI);
    CHECK(rig.Order() == "SCC");
  }

  // ─── invariants ─────────────────────────────────────────────────────────────

  TEST_CASE("next: every message produces exactly one bang, on one outlet or the other (#471)") {
    // Max documents both outlets as the two halves of one test, so the pair is
    // a complete accounting of the inlet's traffic and nothing is swallowed.
    Rig rig;
    Burst burst(5, *rig.op);

    int delivered = 0;
    for (int round = 0; round < 4; round++) {
      burst.Fire();
      delivered += 5;
      rig.Bang();
      delivered += 1;
      CHECK(rig.Total() == delivered);
      CHECK(rig.separated.count + rig.same.count == delivered);
    }
    CHECK(delivered == 24);
    // One separated per stimulus: four bursts and four lone bangs.
    CHECK(rig.separated.count == 8);
    CHECK(rig.same.count == 16);
  }

  TEST_CASE("next: the reported state tracks the last verdict (#471)") {
    Rig rig;
    Burst burst(2, *rig.op);

    rig.Bang();
    CHECK(rig.op->LastWasSeparated() == true);
    CHECK(rig.op->GetGuiValue() == "1");

    burst.Fire(); // separated, then continued — the last verdict is "continued"
    CHECK(rig.op->LastWasSeparated() == false);
    CHECK(rig.op->GetGuiValue() == "0");

    rig.Bang();
    CHECK(rig.op->LastWasSeparated() == true);
    CHECK(rig.op->GetGuiValue() == "1");
  }

  // ─── dispatch corners ───────────────────────────────────────────────────────

  TEST_CASE("next: a handler called outside any inlet is its own stimulus (#471)") {
    // No dispatch is in progress, so CurrentMessageEvent() reports 0. Folding
    // two such calls together on that shared non-id would report a burst that
    // never happened, which is why the object refuses to treat 0 as an event.
    Rig rig;
    rig.op->SetBang(0, YSE::T_GUI);
    rig.op->SetBang(0, YSE::T_GUI);
    rig.op->SetInt(1, 0, YSE::T_GUI);
    CHECK(rig.Order() == "SSS");
  }

  TEST_CASE("next: a message looped back from an outlet is part of the same event (#471)") {
    // The send path is synchronous with no queue in between, so the loop-back
    // happens *inside* the SendBang and is genuinely nested in the same
    // dispatch. It must therefore report as continued — and because it does,
    // the loop terminates on its own instead of recursing until the send-depth
    // guard cuts it off.
    std::vector<char> order;
    gNext op;

    FeedbackSink loop;
    loop.target = &op;
    loop.tag = 'F';
    loop.log = &order;
    op.ConnectOutlet(loop.GetInlet(0), 0);
    loop.ConnectInlet(op.GetOutlet(0), 0);

    TestHelpers::OrderSink continued;
    continued.tag = 'C';
    continued.log = &order;
    op.ConnectOutlet(continued.GetInlet(0), 1);
    continued.ConnectInlet(op.GetOutlet(1), 0);

    op.GetInlet(0)->SetBang(YSE::T_GUI);

    // One trip through the separated outlet, one re-entry that reports as
    // continued, and there it stops.
    CHECK(std::string(order.begin(), order.end()) == "FC");
    CHECK(loop.hits == 1);
    CHECK(loop.depth == 0);
    CHECK(continued.count == 1);
  }

  TEST_CASE("next: the state is settled before the outlet fires (#471)") {
    // Anything reached from an outlet already sees the object describing the
    // message being reported, not the one before it. With the stores after the
    // send, HasSeenMessage() would still be false here on the first message.
    gNext op;

    struct Observer : YSE::PATCHER::pObject {
      gNext* watched = nullptr;
      bool seenDuringSend = false;
      bool separatedDuringSend = false;
      Observer() : pObject(false) {
        inputs.emplace_back(this, true, 0);
        inputs.back().RegisterBang([this](int, YSE::THREAD) {
          if (watched != nullptr) {
            seenDuringSend = watched->HasSeenMessage();
            separatedDuringSend = watched->LastWasSeparated();
          }
        });
      }
      const char* Type() const override {
        return "observer";
      }
      void Calculate(YSE::THREAD) override {}
      void SetMessage(const std::string&, float) override {}
    };

    Observer obs;
    obs.watched = &op;
    op.ConnectOutlet(obs.GetInlet(0), 0);
    obs.ConnectInlet(op.GetOutlet(0), 0);

    op.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(obs.seenDuringSend == true);
    CHECK(obs.separatedDuringSend == true);
  }

  TEST_CASE("next: Calculate does nothing (#471)") {
    // The object is driven by its inlet. An emitting Calculate() would fire it
    // once per DSP block from a stimulus no patch sent.
    Rig rig;
    rig.Bang();
    REQUIRE(rig.Total() == 1);

    for (int i = 0; i < 10; i++) {
      rig.op->Calculate(YSE::T_DSP);
    }
    CHECK(rig.Total() == 1);
    CHECK(rig.Order() == "S");
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  TEST_CASE("next: takes no creation arguments (#471)") {
    // Max: "Arguments: None." There is no state a creation argument could
    // pre-declare — the object's memory is the previous message's event, and an
    // event that has not happened cannot be named.
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_NEXT));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetParamDocs().empty());

    // A stray argument is accepted and ignored rather than changing anything.
    Rig rig;
    rig.op->SetParams("3 wobble");
    rig.Bang();
    rig.Bang();
    CHECK(rig.Order() == "SS");
    rig.op->SetParams("");
    rig.Clear();
    Burst burst(2, *rig.op);
    burst.Fire();
    CHECK(rig.Order() == "SC");
  }

  TEST_CASE("next: survives a DumpJSON / ParseJSON round trip (#471)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_NEXT) != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".next") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".next"));
    CHECK(copy->GetInputs() == 1);
    CHECK(copy->GetOutputs() == 2);
  }

  // ─── documentation ──────────────────────────────────────────────────────────

  TEST_CASE("next: documents itself as GENERIC with a labelled port set (#471)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_NEXT));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    CHECK_FALSE(obj->GetDescription().empty());

    REQUIRE(obj->NumInputs() == 1);
    CHECK(obj->GetInlet(0)->GetDocLabel() == "in");

    REQUIRE(obj->NumOutputs() == 2);
    CHECK(obj->GetOutlet(0)->GetDocLabel() == "separated");
    CHECK(obj->GetOutlet(1)->GetDocLabel() == "same");
  }

  TEST_CASE("next: the inlet accepts bang, int, float and list (#471)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_NEXT));
    REQUIRE(obj != nullptr);
    const unsigned int in = obj->GetInlet(0)->GetAcceptedTypes();
    CHECK((in & YSE::PATCHER::IT_BANG) != 0);
    CHECK((in & YSE::PATCHER::IT_INT) != 0);
    CHECK((in & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((in & YSE::PATCHER::IT_LIST) != 0);
    CHECK((in & YSE::PATCHER::IT_BUFFER) == 0);
  }

  // ─── end to end ─────────────────────────────────────────────────────────────

  TEST_CASE("next: once per burst in a real patcher (#471)") {
    // Registry, wiring API and object together: `.trigger b b b` into `.next`,
    // driven through the handle API the way a host drives it. One bang out of
    // the separated outlet per host call, three messages delivered per call.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* trig = p.CreateObject(YSE::OBJ::G_TRIGGER, "b b b");
    YSE::pHandle* nxt = p.CreateObject(YSE::OBJ::G_NEXT);
    REQUIRE(trig != nullptr);
    REQUIRE(nxt != nullptr);

    TestHelpers::BangSink first;
    TestHelpers::BangSink rest;
    YSE::pHandle firstHandle(&first);
    YSE::pHandle restHandle(&rest);

    p.Connect(trig, 0, nxt, 0);
    p.Connect(trig, 1, nxt, 0);
    p.Connect(trig, 2, nxt, 0);
    p.Connect(nxt, 0, &firstHandle, 0);
    p.Connect(nxt, 1, &restHandle, 0);

    trig->SetBang(0);
    CHECK(first.bangCount == 1);
    CHECK(rest.bangCount == 2);

    trig->SetBang(0);
    CHECK(first.bangCount == 2);
    CHECK(rest.bangCount == 4);

    // A message straight into the .next is its own stimulus, so it separates.
    nxt->SetBang(0);
    CHECK(first.bangCount == 3);
    CHECK(rest.bangCount == 4);
  }

} // TEST_SUITE("patcher")

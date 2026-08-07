// Tests for .uzi (issue #473) — emit N bangs immediately, with a running index.
//
// This is the patcher's loop, and it is the one object whose *failure* mode is
// a hang rather than a wrong number. The file is therefore organised around the
// three things that have to hold, in the order they matter:
//
//   - **the shape and the order**, because the order is the object. Per
//     iteration the index leaves outlet 2 before the bang leaves outlet 0
//     (Max's right-to-left rule, and the reason the index is usable at all),
//     and the carry leaves outlet 1 after the last bang — which Max states
//     explicitly rather than leaving to the rule. The assertions read the
//     shared OrderSink log, never the cumulative counters, so a test that
//     passes cannot be one that merely counted the right number of hits.
//
//   - **the bounding**, which is not Max's and is the whole risk. Three
//     independent runaways are pinned separately: a huge count (clamped, and
//     the clamp reported rather than silent), a start that arrives from inside
//     the burst (refused), and a count raised from the cold inlet during a run
//     (pinned, so it applies to the next run). The recursion tests carry their
//     own depth guard so a regression fails by assertion rather than by
//     exhausting the stack.
//
//   - **the logical event**, because Max's own `next` reference cites this
//     object as *the* example of many messages inside one event. The burst is
//     checked both against the raw `CurrentMessageEvent()` clock and against
//     the real `.next` object, since a wrong clock and a right comparison look
//     identical from one outlet.
//
// The rigs wire objects directly, as the sibling suites do; two tests at the
// end run the object through a real patcher so the registry, the wiring API and
// the object agree. No audio device required.

#include <doctest/doctest.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/genericObjects/gNext.h"
#include "patcher/genericObjects/gUzi.h"
#include "sinks.hpp"

namespace {

  using YSE::PATCHER::CurrentMessageEvent;
  using YSE::PATCHER::gNext;
  using YSE::PATCHER::gUzi;

  // Records every int it receives, in order. OrderSink keeps only the last one,
  // and the index sequence is half of what this object means.
  struct IntLog : YSE::PATCHER::pObject {
    std::vector<int>* log = nullptr;

    IntLog() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) {
        if (log) log->push_back(v);
      });
    }
    const char* Type() const override {
      return "int_log";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // Records the logical event id in force wherever a message reaches it, for
  // every message kind — the clock #471 put under the dispatch layer.
  struct EventProbe : YSE::PATCHER::pObject {
    std::vector<std::uint64_t>* log = nullptr;

    EventProbe() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { Record(); });
      inputs.back().RegisterInt([this](int, int, YSE::THREAD) { Record(); });
    }
    const char* Type() const override {
      return "event_probe";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

  private:
    void Record() {
      if (log) log->push_back(CurrentMessageEvent());
    }
  };

  // All three outlets, each with its own tag, logging into one vector so a test
  // reads back the exact sequence of sends. 'i' = index (outlet 2), 'b' = bang
  // (outlet 0), 'c' = carry (outlet 1). A fourth sink on the index outlet keeps
  // the values.
  struct Rig {
    std::unique_ptr<gUzi> op;
    TestHelpers::OrderSink bangs;
    TestHelpers::OrderSink carry;
    TestHelpers::OrderSink index;
    IntLog values;
    std::vector<char> log;
    std::vector<int> indices;

    explicit Rig(const std::string& args = "") : op(new gUzi()) {
      if (!args.empty()) op->SetParams(args);
      Wire(bangs, 0, 'b');
      Wire(carry, 1, 'c');
      Wire(index, 2, 'i');
      values.log = &indices;
      op->ConnectOutlet(values.GetInlet(0), 2);
      values.ConnectInlet(op->GetOutlet(2), 0);
    }

    // ── one stimulus each, through the inlet so the event clock runs ──
    void Bang() {
      op->GetInlet(0)->SetBang(YSE::T_GUI);
    }
    void Start(int n) {
      op->GetInlet(0)->SetInt(n, YSE::T_GUI);
    }
    void StartFloat(float n) {
      op->GetInlet(0)->SetFloat(n, YSE::T_GUI);
    }
    void SetCount(int n) {
      op->GetInlet(1)->SetInt(n, YSE::T_GUI);
    }
    void SetCountFloat(float n) {
      op->GetInlet(1)->SetFloat(n, YSE::T_GUI);
    }
    void Word(const std::string& text) {
      op->GetInlet(0)->SetList(text, YSE::T_GUI);
    }

    std::string Order() const {
      return std::string(log.begin(), log.end());
    }
    void Clear() {
      log.clear();
      indices.clear();
    }

  private:
    void Wire(TestHelpers::OrderSink& sink, int outlet, char tag) {
      sink.log = &log;
      sink.tag = tag;
      op->ConnectOutlet(sink.GetInlet(0), outlet);
      sink.ConnectInlet(op->GetOutlet(outlet), 0);
    }
  };

  // Pauses the object from inside its own burst, after the nth bang. Max is
  // explicit that this is the only way the message can arrive: "Since uzi sends
  // its output as fast as possible, this message must be triggered in some way
  // by the output of uzi itself."
  struct PauseAfter : YSE::PATCHER::pObject {
    gUzi* target = nullptr;
    int after = 0;
    int seen = 0;
    bool runningDuringSend = false;

    PauseAfter() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) {
        seen++;
        if (target == nullptr) return;
        if (target->IsRunning()) runningDuringSend = true;
        if (seen == after) target->GetInlet(0)->SetList("pause", YSE::T_GUI);
      });
    }
    const char* Type() const override {
      return "pause_after";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // Feeds every bang straight back into the object's start inlet, from *inside*
  // the send, so the re-entrant start is genuinely nested in the same dispatch.
  // Carries its own depth limit so a missing guard fails by assertion rather
  // than by blowing the stack.
  struct FeedbackSink : YSE::PATCHER::pObject {
    YSE::PATCHER::pObject* target = nullptr;
    int hits = 0;
    int depth = 0;
    int maxDepth = 0;
    static constexpr int DEPTH_LIMIT = 6;

    FeedbackSink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) {
        hits++;
        depth++;
        if (depth > maxDepth) maxDepth = depth;
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

  // Raises the count from the *cold* inlet on every bang — a start guard cannot
  // see this one, because setting the count is not a start.
  struct RaiseCount : YSE::PATCHER::pObject {
    gUzi* target = nullptr;
    int next = 0;
    int hits = 0;

    RaiseCount() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) {
        hits++;
        if (target != nullptr) target->GetInlet(1)->SetInt(++next, YSE::T_GUI);
      });
    }
    const char* Type() const override {
      return "raise_count";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("uzi: creatable through the registry with two inlets and three outlets (#473)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_UZI);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".uzi"));
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 3);
    // Max's order, left to right: bang, carry, index.
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BANG);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::BANG);
    CHECK(h->OutputDataType(2) == YSE::OUT_TYPE::INT);
  }

  TEST_CASE("uzi: listed by pRegistry::AllNames (#473)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".uzi")) != names.end());
  }

  // ─── the order, which is the object ─────────────────────────────────────────

  TEST_CASE("uzi: each iteration sends its index before its bang, and the carry last (#473)") {
    // The single assertion this object lives or dies by. An implementation that
    // sent the bang first would count the right number of hits and still be
    // useless, because the idiom reads the index from a box the bang triggers.
    Rig rig("3");
    rig.Bang();
    CHECK(rig.Order() == "ibibibc");
    REQUIRE(rig.indices.size() == 3);
    CHECK(rig.indices[0] == 1);
    CHECK(rig.indices[1] == 2);
    CHECK(rig.indices[2] == 3);
  }

  TEST_CASE("uzi: the index is 1-based by default (#473)") {
    // Max: "Numbering begins from 1 (or from the base value specified by the
    // optional second argument)."
    Rig rig("4");
    rig.Bang();
    REQUIRE(rig.indices.size() == 4);
    CHECK(rig.indices[0] == 1);
    CHECK(rig.indices[3] == 4);
  }

  TEST_CASE("uzi: the second argument moves the index base (#473)") {
    Rig zero("4 0");
    zero.Bang();
    REQUIRE(zero.indices.size() == 4);
    CHECK(zero.indices[0] == 0);
    CHECK(zero.indices[3] == 3);
    CHECK(zero.op->Base() == 0);

    Rig negative("3 -1");
    negative.Bang();
    REQUIRE(negative.indices.size() == 3);
    CHECK(negative.indices[0] == -1);
    CHECK(negative.indices[1] == 0);
    CHECK(negative.indices[2] == 1);
  }

  TEST_CASE("uzi: a bare .uzi sends one bang (#473)") {
    // Max: "If no argument is present, uzi is initially set to send out one
    // bang."
    Rig rig;
    CHECK(rig.op->Count() == 1);
    rig.Bang();
    CHECK(rig.Order() == "ibc");
    REQUIRE(rig.indices.size() == 1);
    CHECK(rig.indices[0] == 1);
  }

  TEST_CASE("uzi: every stimulus restarts the numbering (#473)") {
    // Max: numbering "begins from 1 [...] each time an int or bang is received
    // in the left inlet".
    Rig rig("2");
    rig.Bang();
    rig.Bang();
    CHECK(rig.Order() == "ibibcibibc");
    REQUIRE(rig.indices.size() == 4);
    CHECK(rig.indices[0] == 1);
    CHECK(rig.indices[1] == 2);
    CHECK(rig.indices[2] == 1);
    CHECK(rig.indices[3] == 2);
  }

  // ─── setting the count ──────────────────────────────────────────────────────

  TEST_CASE("uzi: an int on the start inlet sets the count and then runs (#473)") {
    Rig rig;
    rig.Start(3);
    CHECK(rig.op->Count() == 3);
    CHECK(rig.Order() == "ibibibc");

    // ...and it sticks, so a following bang uses it.
    rig.Clear();
    rig.Bang();
    CHECK(rig.Order() == "ibibibc");
  }

  TEST_CASE("uzi: an int on the count inlet sets the count without output (#473)") {
    // Max: "In right inlet: Sets the number of bang messages to send, without
    // causing output."
    Rig rig;
    rig.SetCount(4);
    CHECK(rig.op->Count() == 4);
    CHECK(rig.Order().empty());

    rig.Bang();
    CHECK(rig.Order() == "ibibibibc");
  }

  TEST_CASE("uzi: a float is truncated towards zero on both inlets (#473)") {
    Rig left;
    left.StartFloat(3.9f);
    CHECK(left.op->Count() == 3);
    CHECK(left.Order() == "ibibibc");

    Rig right;
    right.SetCountFloat(2.75f);
    CHECK(right.op->Count() == 2);
    CHECK(right.Order().empty());
    right.Bang();
    CHECK(right.Order() == "ibibc");
  }

  TEST_CASE("uzi: the count inlet takes no bang and no list (#473)") {
    // Max lists an int method for inlet 1 and nothing else, so neither is
    // registered — the object reports that honestly rather than accepting them
    // and doing nothing.
    Rig rig("2");
    rig.op->GetInlet(1)->SetBang(YSE::T_GUI);
    rig.op->GetInlet(1)->SetList("pause", YSE::T_GUI);
    CHECK(rig.Order().empty());
    CHECK(rig.op->Count() == 2);
  }

  TEST_CASE("uzi: a count of zero sends no bangs but still carries (#473)") {
    // A loop that does not run is a shape a patch computing its count from a
    // list length genuinely produces. The carry means "all the bangs have been
    // sent", which is vacuously true, and an after-the-loop branch that was
    // silently skipped here would be a nasty thing to debug.
    Rig rig("0");
    CHECK(rig.op->Count() == 0);
    rig.Bang();
    CHECK(rig.Order() == "c");
    CHECK(rig.indices.empty());
  }

  TEST_CASE("uzi: a negative count is clamped to zero (#473)") {
    Rig rig;
    rig.Start(-5);
    CHECK(rig.op->RequestedCount() == -5);
    CHECK(rig.op->Count() == 0);
    CHECK(rig.op->CountWasClamped());
    CHECK(rig.Order() == "c");
  }

  // ─── the cap, and the report ────────────────────────────────────────────────

  TEST_CASE("uzi: the count is clamped to MAX_COUNT (#473)") {
    // The send path is synchronous, so the count is the number of subgraph
    // traversals one message costs on whichever thread dispatched it. Max
    // documents no limit; this patcher cannot adopt that.
    Rig rig;
    rig.Start(gUzi::MAX_COUNT + 1000);
    CHECK(rig.op->Count() == gUzi::MAX_COUNT);
    CHECK(rig.op->RequestedCount() == gUzi::MAX_COUNT + 1000);
    CHECK(rig.op->CountWasClamped());
    CHECK(rig.bangs.count == gUzi::MAX_COUNT);
    CHECK(rig.carry.count == 1);
    REQUIRE(rig.indices.size() == static_cast<std::size_t>(gUzi::MAX_COUNT));
    CHECK(rig.indices.front() == 1);
    CHECK(rig.indices.back() == gUzi::MAX_COUNT);
  }

  TEST_CASE("uzi: a count exactly at the cap is not reported as clamped (#473)") {
    Rig rig;
    rig.SetCount(gUzi::MAX_COUNT);
    CHECK(rig.op->Count() == gUzi::MAX_COUNT);
    CHECK_FALSE(rig.op->CountWasClamped());
  }

  TEST_CASE("uzi: an over-large creation argument is clamped, not dropped (#473)") {
    Rig rig(std::string("99999"));
    CHECK(rig.op->Count() == gUzi::MAX_COUNT);
    CHECK(rig.op->RequestedCount() == 99999);
    CHECK(rig.op->CountWasClamped());
  }

  TEST_CASE("uzi: a huge float count lands at the ceiling, not at the floor (#473)") {
    // The ExprToInt trap: that helper answers *everything* outside the int
    // range with 0, which is right for an expression result and exactly wrong
    // here — 1e30 is a huge number of bangs and belongs at the cap. Reporting
    // it as "0 requested" would also make the clamp report a lie.
    Rig rig;
    rig.StartFloat(1e30f);
    CHECK(rig.op->Count() == gUzi::MAX_COUNT);
    CHECK(rig.op->RequestedCount() > gUzi::MAX_COUNT);
    CHECK(rig.op->CountWasClamped());
    CHECK(rig.bangs.count == gUzi::MAX_COUNT);
  }

  TEST_CASE("uzi: a NaN count is not a number and reads as zero (#473)") {
    Rig rig;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    rig.StartFloat(nan);
    CHECK(rig.op->Count() == 0);
    CHECK(rig.Order() == "c");
  }

  TEST_CASE("uzi: an argument that is not a whole finite number leaves the defaults (#473)") {
    // Strict, through the shared ReadNumericToken: ExprParseFloatList would
    // read "16abc" as 16 and fold "1e999" to 0.
    Rig text("wobble");
    CHECK(text.op->Count() == gUzi::DEFAULT_COUNT);
    CHECK(text.op->Base() == gUzi::DEFAULT_BASE);

    Rig partial("16abc");
    CHECK(partial.op->Count() == gUzi::DEFAULT_COUNT);

    Rig infinite("inf");
    CHECK(infinite.op->Count() == gUzi::DEFAULT_COUNT);

    Rig badBase("3 wobble");
    CHECK(badBase.op->Count() == 3);
    CHECK(badBase.op->Base() == gUzi::DEFAULT_BASE);
  }

  TEST_CASE("uzi: the index base is clamped so base + index cannot overflow (#473)") {
    Rig rig("2 999999999999");
    CHECK(rig.op->Base() == gUzi::BASE_LIMIT);
    rig.Bang();
    REQUIRE(rig.indices.size() == 2);
    CHECK(rig.indices[0] == gUzi::BASE_LIMIT);
    CHECK(rig.indices[1] == gUzi::BASE_LIMIT + 1);
  }

  // ─── offset ─────────────────────────────────────────────────────────────────

  TEST_CASE("uzi: offset skips the leading iterations (#473)") {
    // Max: "the number is subtracted from the previously assigned number of
    // bangs to equal the new total number of bangs".
    Rig rig("5");
    rig.Word("offset 2");
    CHECK(rig.op->Offset() == 2);
    rig.Bang();
    CHECK(rig.Order() == "ibibibc");
    REQUIRE(rig.indices.size() == 3);
    CHECK(rig.indices[0] == 3);
    CHECK(rig.indices[1] == 4);
    CHECK(rig.indices[2] == 5);
  }

  TEST_CASE("uzi: offset persists and offset 0 restores the whole range (#473)") {
    Rig rig("4");
    rig.Word("offset 3");
    rig.Bang();
    CHECK(rig.Order() == "ibc");
    rig.Clear();
    rig.Bang();
    CHECK(rig.Order() == "ibc"); // still offset

    rig.Word("offset 0");
    rig.Clear();
    rig.Bang();
    CHECK(rig.Order() == "ibibibibc");
  }

  TEST_CASE("uzi: an offset past the count is an empty run that still carries (#473)") {
    Rig rig("3");
    rig.Word("offset 10");
    rig.Bang();
    CHECK(rig.Order() == "c");
    CHECK(rig.indices.empty());
  }

  TEST_CASE("uzi: a negative offset is clamped and a malformed one is ignored (#473)") {
    Rig rig("3");
    rig.Word("offset -4");
    CHECK(rig.op->Offset() == 0);

    rig.Word("offset 1");
    CHECK(rig.op->Offset() == 1);
    // No number, and a word that is not a number: neither resets the offset,
    // because a message that could not be read is not a message.
    rig.Word("offset");
    CHECK(rig.op->Offset() == 1);
    rig.Word("offset wobble");
    CHECK(rig.op->Offset() == 1);
  }

  // ─── pause / resume ─────────────────────────────────────────────────────────

  TEST_CASE("uzi: pause stops a run from inside it, and resume finishes it (#473)") {
    // Max: "uzi keeps track of how many bang messages it has sent, and if it
    // receives the pause message before sending out all its bang messages, it
    // can then be caused to send out the rest [...] with a resume or continue
    // message", numbering "wherever it left off".
    Rig rig("5");
    PauseAfter pauser;
    pauser.target = rig.op.get();
    pauser.after = 2;
    rig.op->ConnectOutlet(pauser.GetInlet(0), 0);
    pauser.ConnectInlet(rig.op->GetOutlet(0), 0);

    rig.Bang();
    CHECK(pauser.runningDuringSend); // the message really did arrive mid-burst
    CHECK(rig.Order() == "ibib"); // no carry: the bangs have not all been sent
    CHECK(rig.op->IsPaused());
    CHECK_FALSE(rig.op->IsRunning());
    CHECK(rig.op->Progress() == 2);
    REQUIRE(rig.indices.size() == 2);
    CHECK(rig.indices[1] == 2);

    rig.Clear();
    rig.Word("resume");
    CHECK(rig.Order() == "ibibibc");
    CHECK_FALSE(rig.op->IsPaused());
    REQUIRE(rig.indices.size() == 3);
    CHECK(rig.indices[0] == 3);
    CHECK(rig.indices[2] == 5);
    // Five bangs across the two halves, exactly the count.
    CHECK(rig.bangs.count == 5);
    CHECK(rig.carry.count == 1);
  }

  TEST_CASE("uzi: continue resumes exactly as resume does (#473)") {
    // Max: "continue: Same as resume."
    Rig rig("4");
    PauseAfter pauser;
    pauser.target = rig.op.get();
    pauser.after = 1;
    rig.op->ConnectOutlet(pauser.GetInlet(0), 0);
    pauser.ConnectInlet(rig.op->GetOutlet(0), 0);

    rig.Bang();
    REQUIRE(rig.op->IsPaused());
    rig.Clear();
    rig.Word("continue");
    CHECK(rig.Order() == "ibibibc");
    CHECK(rig.bangs.count == 4);
  }

  TEST_CASE("uzi: break pauses exactly as pause does (#473)") {
    struct BreakAfter : YSE::PATCHER::pObject {
      gUzi* target = nullptr;
      int seen = 0;
      BreakAfter() : pObject(false) {
        inputs.emplace_back(this, true, 0);
        inputs.back().RegisterBang([this](int, YSE::THREAD) {
          seen++;
          if (seen == 2 && target != nullptr) {
            target->GetInlet(0)->SetList("break", YSE::T_GUI);
          }
        });
      }
      const char* Type() const override {
        return "break_after";
      }
      void Calculate(YSE::THREAD) override {}
      void SetMessage(const std::string&, float) override {}
    };

    Rig rig("5");
    BreakAfter breaker;
    breaker.target = rig.op.get();
    rig.op->ConnectOutlet(breaker.GetInlet(0), 0);
    breaker.ConnectInlet(rig.op->GetOutlet(0), 0);

    rig.Bang();
    CHECK(rig.Order() == "ibib");
    CHECK(rig.op->IsPaused());
  }

  TEST_CASE(
      "uzi: pause outside a run is a no-op, and so is a resume with nothing to resume (#473)") {
    // A stored pause would make the next resume fire a carry for a run nobody
    // started, and a resume on a finished object would fire a second carry.
    Rig rig("2");
    rig.Word("pause");
    CHECK_FALSE(rig.op->IsPaused());
    CHECK(rig.Order().empty());

    rig.Word("resume");
    CHECK(rig.Order().empty());

    rig.Bang();
    CHECK(rig.Order() == "ibibc");

    rig.Clear();
    rig.Word("resume"); // the run finished; there is nothing left of it
    CHECK(rig.Order().empty());
    CHECK(rig.carry.count == 1);
  }

  TEST_CASE("uzi: a start after a pause restarts rather than resuming (#473)") {
    Rig rig("4");
    PauseAfter pauser;
    pauser.target = rig.op.get();
    pauser.after = 1;
    rig.op->ConnectOutlet(pauser.GetInlet(0), 0);
    pauser.ConnectInlet(rig.op->GetOutlet(0), 0);

    rig.Bang();
    REQUIRE(rig.op->IsPaused());

    // Take the pauser out of the way and start again: Max restarts the
    // numbering on a bang in the left inlet, which is a different thing from
    // resuming.
    pauser.target = nullptr;
    rig.Clear();
    rig.Bang();
    CHECK(rig.Order() == "ibibibibc");
    REQUIRE(rig.indices.size() == 4);
    CHECK(rig.indices[0] == 1);
    CHECK_FALSE(rig.op->IsPaused());
  }

  TEST_CASE("uzi: pause never leaves an index without its bang (#473)") {
    // The flag is read at the top of the loop, so an iteration is atomic even
    // when the pause arrives from the *index* outlet, half way through one.
    struct PauseOnIndex : YSE::PATCHER::pObject {
      gUzi* target = nullptr;
      int at = 0;
      PauseOnIndex() : pObject(false) {
        inputs.emplace_back(this, true, 0);
        inputs.back().RegisterInt([this](int v, int, YSE::THREAD) {
          if (v == at && target != nullptr) target->GetInlet(0)->SetList("pause", YSE::T_GUI);
        });
      }
      const char* Type() const override {
        return "pause_on_index";
      }
      void Calculate(YSE::THREAD) override {}
      void SetMessage(const std::string&, float) override {}
    };

    Rig rig("5");
    PauseOnIndex pauser;
    pauser.target = rig.op.get();
    pauser.at = 3;
    rig.op->ConnectOutlet(pauser.GetInlet(0), 2);
    pauser.ConnectInlet(rig.op->GetOutlet(2), 0);

    rig.Bang();
    // Three complete iterations — the third one's bang still went out.
    CHECK(rig.Order() == "ibibib");
    CHECK(rig.bangs.count == 3);
    CHECK(rig.index.count == 3);
    CHECK(rig.op->Progress() == 3);
  }

  // ─── bounding: recursion, and a count that will not sit still ───────────────

  TEST_CASE("uzi: a start arriving from inside the burst is refused (#473)") {
    // The bang outlet wired back to the start inlet. Without the guard this is
    // not merely deep recursion but a tree with `count` children per level, so
    // the send-depth ceiling in outlet.cpp would only cut it off after
    // count^64 iterations. The sink's own depth limit makes a regression fail
    // by assertion instead of by exhausting the stack.
    Rig rig("3");
    FeedbackSink loop;
    loop.target = rig.op.get();
    rig.op->ConnectOutlet(loop.GetInlet(0), 0);
    loop.ConnectInlet(rig.op->GetOutlet(0), 0);

    rig.Bang();

    CHECK(rig.Order() == "ibibibc");
    CHECK(loop.hits == 3);
    CHECK(loop.depth == 0);
    // One level only: every re-entry was refused, so the loop never nested.
    CHECK(loop.maxDepth == 1);
    CHECK(loop.maxDepth < FeedbackSink::DEPTH_LIMIT);
    CHECK(rig.op->RefusedStarts() == 3);
  }

  TEST_CASE("uzi: two .uzi objects feeding each other still terminate (#473)") {
    // The guard is per object, so mutual recursion is stopped by whichever one
    // is already running.
    gUzi outer;
    outer.SetParams("3");
    gUzi inner;
    inner.SetParams("2");

    TestHelpers::BangSink innerBangs;
    outer.ConnectOutlet(inner.GetInlet(0), 0);
    inner.ConnectInlet(outer.GetOutlet(0), 0);
    inner.ConnectOutlet(outer.GetInlet(0), 0);
    outer.ConnectInlet(inner.GetOutlet(0), 0);
    inner.ConnectOutlet(innerBangs.GetInlet(0), 0);
    innerBangs.ConnectInlet(inner.GetOutlet(0), 0);

    outer.GetInlet(0)->SetBang(YSE::T_GUI);

    // Three outer bangs, each running the inner object's two — and every
    // inner bang's trip back into the outer object is refused.
    CHECK(innerBangs.bangCount == 6);
    CHECK(outer.RefusedStarts() == 6);
    CHECK(inner.RefusedStarts() == 0);
  }

  TEST_CASE("uzi: distinct .uzi objects nest normally (#473)") {
    // The guard must not break the legitimate nested loop, which is the whole
    // reason it is per object rather than global.
    Rig outerRig("3");
    gUzi inner;
    inner.SetParams("4");
    TestHelpers::BangSink innerBangs;
    outerRig.op->ConnectOutlet(inner.GetInlet(0), 0);
    inner.ConnectInlet(outerRig.op->GetOutlet(0), 0);
    inner.ConnectOutlet(innerBangs.GetInlet(0), 0);
    innerBangs.ConnectInlet(inner.GetOutlet(0), 0);

    outerRig.Bang();
    CHECK(innerBangs.bangCount == 12);
    CHECK(inner.RefusedStarts() == 0);
    CHECK(outerRig.op->RefusedStarts() == 0);
  }

  TEST_CASE("uzi: the loop bound is pinned when the run starts (#473)") {
    // The subtle runaway, and the one a start guard cannot catch: setting the
    // count is not a start, so a patch raising it from the cold inlet once per
    // iteration would extend the loop it is inside, forever.
    Rig rig("3");
    RaiseCount raiser;
    raiser.target = rig.op.get();
    raiser.next = 10; // every bang pushes the count higher
    rig.op->ConnectOutlet(raiser.GetInlet(0), 0);
    raiser.ConnectInlet(rig.op->GetOutlet(0), 0);

    rig.Bang();

    // Three bangs, the count the run began with.
    CHECK(rig.Order() == "ibibibc");
    CHECK(raiser.hits == 3);
    // ...and the writes were not lost, they just apply to the next run.
    CHECK(rig.op->Count() == 13);
  }

  TEST_CASE("uzi: an offset written during a run applies to the next one (#473)") {
    struct OffsetOnBang : YSE::PATCHER::pObject {
      gUzi* target = nullptr;
      OffsetOnBang() : pObject(false) {
        inputs.emplace_back(this, true, 0);
        inputs.back().RegisterBang([this](int, YSE::THREAD) {
          if (target != nullptr) target->GetInlet(0)->SetList("offset 2", YSE::T_GUI);
        });
      }
      const char* Type() const override {
        return "offset_on_bang";
      }
      void Calculate(YSE::THREAD) override {}
      void SetMessage(const std::string&, float) override {}
    };

    Rig rig("4");
    OffsetOnBang setter;
    setter.target = rig.op.get();
    rig.op->ConnectOutlet(setter.GetInlet(0), 0);
    setter.ConnectInlet(rig.op->GetOutlet(0), 0);

    rig.Bang();
    CHECK(rig.Order() == "ibibibibc");
    CHECK(rig.op->Offset() == 2);

    rig.Clear();
    rig.Bang();
    CHECK(rig.Order() == "ibibc");
  }

  TEST_CASE("uzi: a resume arriving from inside the burst is refused (#473)") {
    struct ResumeOnBang : YSE::PATCHER::pObject {
      gUzi* target = nullptr;
      ResumeOnBang() : pObject(false) {
        inputs.emplace_back(this, true, 0);
        inputs.back().RegisterBang([this](int, YSE::THREAD) {
          if (target != nullptr) target->GetInlet(0)->SetList("resume", YSE::T_GUI);
        });
      }
      const char* Type() const override {
        return "resume_on_bang";
      }
      void Calculate(YSE::THREAD) override {}
      void SetMessage(const std::string&, float) override {}
    };

    Rig rig("3");
    ResumeOnBang resumer;
    resumer.target = rig.op.get();
    rig.op->ConnectOutlet(resumer.GetInlet(0), 0);
    resumer.ConnectInlet(rig.op->GetOutlet(0), 0);

    rig.Bang();
    CHECK(rig.Order() == "ibibibc");
    CHECK(rig.op->RefusedStarts() == 3);
  }

  // ─── one logical event ──────────────────────────────────────────────────────

  TEST_CASE("uzi: the whole burst is a single logical event (#473)") {
    // Max's `next` reference names this object as the example: "if you put
    // bang, bang in a message box, or use the uzi object to send out two bangs
    // in a row, these bangs are part of the same logical event."
    std::vector<std::uint64_t> ids;
    EventProbe probe;
    probe.log = &ids;

    gUzi op;
    op.SetParams("4");
    for (int outlet = 0; outlet < 3; outlet++) {
      op.ConnectOutlet(probe.GetInlet(0), outlet);
      probe.ConnectInlet(op.GetOutlet(outlet), 0);
    }

    op.GetInlet(0)->SetBang(YSE::T_GUI);

    // Four indices, four bangs and one carry.
    REQUIRE(ids.size() == 9);
    CHECK(ids[0] != 0);
    for (std::size_t i = 1; i < ids.size(); i++) {
      CAPTURE(i);
      CHECK(ids[i] == ids[0]);
    }
  }

  TEST_CASE("uzi: two stimuli are two events, and a resume is a third (#473)") {
    std::vector<std::uint64_t> ids;
    EventProbe probe;
    probe.log = &ids;

    gUzi op;
    op.SetParams("1");
    op.ConnectOutlet(probe.GetInlet(0), 0);
    probe.ConnectInlet(op.GetOutlet(0), 0);

    op.GetInlet(0)->SetBang(YSE::T_GUI);
    op.GetInlet(0)->SetBang(YSE::T_GUI);

    REQUIRE(ids.size() == 2);
    CHECK(ids[0] != 0);
    CHECK(ids[1] != 0);
    CHECK(ids[0] != ids[1]);
  }

  TEST_CASE("uzi: .next reads a whole burst as one event (#473)") {
    // The real object, not the raw clock — a wrong clock and a right comparison
    // look identical from one outlet. 'S' = separated (a new event), 'C' =
    // continued.
    std::vector<char> order;
    gUzi op;
    op.SetParams("5");
    gNext next;

    op.ConnectOutlet(next.GetInlet(0), 0);
    next.ConnectInlet(op.GetOutlet(0), 0);

    TestHelpers::OrderSink separated;
    separated.tag = 'S';
    separated.log = &order;
    next.ConnectOutlet(separated.GetInlet(0), 0);
    separated.ConnectInlet(next.GetOutlet(0), 0);

    TestHelpers::OrderSink continued;
    continued.tag = 'C';
    continued.log = &order;
    next.ConnectOutlet(continued.GetInlet(0), 1);
    continued.ConnectInlet(next.GetOutlet(1), 0);

    op.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(std::string(order.begin(), order.end()) == "SCCCC");

    // A second stimulus is a second event, so exactly one more separated bang.
    order.clear();
    op.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(std::string(order.begin(), order.end()) == "SCCCC");
    CHECK(separated.count == 2);
    CHECK(continued.count == 8);
  }

  TEST_CASE("uzi: the carry belongs to the same event as the bangs it follows (#473)") {
    // Worth pinning separately: the carry is sent after the emit loop has
    // finished and `running` has been cleared, which is a plausible place for a
    // burst to accidentally leave its own dispatch.
    std::vector<char> order;
    gUzi op;
    op.SetParams("3");
    gNext next;

    op.ConnectOutlet(next.GetInlet(0), 0); // bangs
    next.ConnectInlet(op.GetOutlet(0), 0);
    op.ConnectOutlet(next.GetInlet(0), 1); // carry
    next.ConnectInlet(op.GetOutlet(1), 0);

    TestHelpers::OrderSink separated;
    separated.tag = 'S';
    separated.log = &order;
    next.ConnectOutlet(separated.GetInlet(0), 0);
    separated.ConnectInlet(next.GetOutlet(0), 0);

    TestHelpers::OrderSink continued;
    continued.tag = 'C';
    continued.log = &order;
    next.ConnectOutlet(continued.GetInlet(0), 1);
    continued.ConnectInlet(next.GetOutlet(1), 0);

    op.GetInlet(0)->SetBang(YSE::T_GUI);
    // Three bangs and the carry, one event between them.
    CHECK(std::string(order.begin(), order.end()) == "SCCC");
  }

  // ─── words that are not messages ────────────────────────────────────────────

  TEST_CASE("uzi: an unrecognised symbol does not start a run (#473)") {
    // Max lists no `anything` method for this object. Where a stray message
    // costs .bangbang one bang, here it would cost up to MAX_COUNT of them.
    Rig rig("3");
    rig.Word("reset");
    rig.Word("set 5");
    rig.Word("stop");
    rig.Word("");
    rig.Word("7");
    CHECK(rig.Order().empty());
    CHECK(rig.op->Count() == 3);
  }

  TEST_CASE("uzi: the message words are bare (#473)") {
    // `pause 1` is a list, not the message — the family's rule, and here it has
    // teeth, since the alternative is behaviour that depends on text after a
    // word the object does not read.
    Rig rig("3");
    PauseAfter pauser;
    pauser.target = rig.op.get();
    pauser.after = 1;
    rig.op->ConnectOutlet(pauser.GetInlet(0), 0);
    pauser.ConnectInlet(rig.op->GetOutlet(0), 0);

    rig.Bang();
    REQUIRE(rig.op->IsPaused());

    rig.Clear();
    rig.Word("resume 1"); // not `resume`
    CHECK(rig.Order().empty());
    CHECK(rig.op->IsPaused());

    rig.Word("resume");
    CHECK(rig.Order() == "ibibc");
  }

  // ─── Calculate ──────────────────────────────────────────────────────────────

  TEST_CASE("uzi: Calculate does nothing (#473)") {
    // The family's rule, and here the most expensive possible way to break it:
    // an emitting Calculate() would run the whole loop again every DSP block.
    Rig rig("4");
    rig.Bang();
    REQUIRE(rig.bangs.count == 4);

    for (int i = 0; i < 10; i++) {
      rig.op->Calculate(YSE::T_DSP);
    }
    CHECK(rig.bangs.count == 4);
    CHECK(rig.carry.count == 1);
    CHECK(rig.Order() == "ibibibibc");
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  TEST_CASE("uzi: SetParams(\"\") returns the object to Max's no-argument shape (#473)") {
    Rig rig("6 0");
    REQUIRE(rig.op->Count() == 6);
    REQUIRE(rig.op->Base() == 0);
    rig.op->GetInlet(0)->SetList("offset 3", YSE::T_GUI);
    REQUIRE(rig.op->Offset() == 3);

    rig.op->SetParams("");
    CHECK(rig.op->Count() == gUzi::DEFAULT_COUNT);
    CHECK(rig.op->Base() == gUzi::DEFAULT_BASE);
    CHECK(rig.op->Offset() == 0);

    rig.Bang();
    CHECK(rig.Order() == "ibc");
    REQUIRE(rig.indices.size() == 1);
    CHECK(rig.indices[0] == 1);
  }

  TEST_CASE("uzi: survives a DumpJSON / ParseJSON round trip (#473)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_UZI, "8 0") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".uzi") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".uzi"));
    CHECK(copy->GetInputs() == 2);
    CHECK(copy->GetOutputs() == 3);
    CHECK(copy->GetParams() == std::string("8 0"));
  }

  // ─── documentation ──────────────────────────────────────────────────────────

  TEST_CASE("uzi: documents itself as GENERIC with a labelled port set (#473)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_UZI));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    CHECK_FALSE(obj->GetDescription().empty());

    REQUIRE(obj->NumInputs() == 2);
    CHECK(obj->GetInlet(0)->GetDocLabel() == "start");
    CHECK(obj->GetInlet(1)->GetDocLabel() == "count");

    REQUIRE(obj->NumOutputs() == 3);
    CHECK(obj->GetOutlet(0)->GetDocLabel() == "bang");
    CHECK(obj->GetOutlet(1)->GetDocLabel() == "carry");
    CHECK(obj->GetOutlet(2)->GetDocLabel() == "index");

    const auto& params = obj->GetParamDocs();
    REQUIRE(params.size() == 2);
    CHECK(params[0].name == "count");
    CHECK(params[1].name == "base");
  }

  TEST_CASE("uzi: the inlets accept the message kinds Max gives them (#473)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_UZI));
    REQUIRE(obj != nullptr);

    const unsigned int start = obj->GetInlet(0)->GetAcceptedTypes();
    CHECK((start & YSE::PATCHER::IT_BANG) != 0);
    CHECK((start & YSE::PATCHER::IT_INT) != 0);
    CHECK((start & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((start & YSE::PATCHER::IT_LIST) != 0);
    CHECK((start & YSE::PATCHER::IT_BUFFER) == 0);

    // Max lists an int method for the right inlet and nothing else.
    const unsigned int cold = obj->GetInlet(1)->GetAcceptedTypes();
    CHECK((cold & YSE::PATCHER::IT_INT) != 0);
    CHECK((cold & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((cold & YSE::PATCHER::IT_BANG) == 0);
    CHECK((cold & YSE::PATCHER::IT_LIST) == 0);
    CHECK((cold & YSE::PATCHER::IT_BUFFER) == 0);
  }

  TEST_CASE("uzi: reports its effective count as its GUI value (#473)") {
    Rig rig("5");
    CHECK(rig.op->GetGuiValue() == "5");
    // The clamp is visible from outside without a C++ accessor.
    rig.SetCount(gUzi::MAX_COUNT * 2);
    CHECK(rig.op->GetGuiValue() == "4096");
  }

  // ─── end to end ─────────────────────────────────────────────────────────────

  TEST_CASE("uzi: a loop in a real patcher (#473)") {
    // Registry, wiring API and object together, driven through the handle API
    // the way a host drives it.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* uzi = p.CreateObject(YSE::OBJ::G_UZI, "6 0");
    REQUIRE(uzi != nullptr);

    TestHelpers::BangSink bangs;
    TestHelpers::BangSink done;
    TestHelpers::IntSink last;
    YSE::pHandle bangHandle(&bangs);
    YSE::pHandle doneHandle(&done);
    YSE::pHandle indexHandle(&last);

    p.Connect(uzi, 0, &bangHandle, 0);
    p.Connect(uzi, 1, &doneHandle, 0);
    p.Connect(uzi, 2, &indexHandle, 0);

    uzi->SetBang(0);
    CHECK(bangs.bangCount == 6);
    CHECK(done.bangCount == 1);
    CHECK(last.received == 5); // 0-based, so the last index is count-1

    // An int on the start inlet sets the count and runs it.
    uzi->SetIntData(0, 3);
    CHECK(bangs.bangCount == 9);
    CHECK(done.bangCount == 2);
    CHECK(last.received == 2);

    // An int on the count inlet is silent.
    uzi->SetIntData(1, 2);
    CHECK(bangs.bangCount == 9);
    CHECK(done.bangCount == 2);
  }

  TEST_CASE("uzi: driving .next from a real patcher gives one bang per burst (#473)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* uzi = p.CreateObject(YSE::OBJ::G_UZI, "4");
    YSE::pHandle* nxt = p.CreateObject(YSE::OBJ::G_NEXT);
    REQUIRE(uzi != nullptr);
    REQUIRE(nxt != nullptr);

    TestHelpers::BangSink first;
    TestHelpers::BangSink rest;
    YSE::pHandle firstHandle(&first);
    YSE::pHandle restHandle(&rest);

    p.Connect(uzi, 0, nxt, 0);
    p.Connect(nxt, 0, &firstHandle, 0);
    p.Connect(nxt, 1, &restHandle, 0);

    uzi->SetBang(0);
    CHECK(first.bangCount == 1);
    CHECK(rest.bangCount == 3);

    uzi->SetBang(0);
    CHECK(first.bangCount == 2);
    CHECK(rest.bangCount == 6);
  }

} // TEST_SUITE("patcher")

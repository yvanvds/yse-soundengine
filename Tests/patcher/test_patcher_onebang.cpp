// Tests for .onebang (issue #470) — let exactly one bang through per arming.
//
// The whole object is a one-bit state machine, so this file is organised around
// the state machine rather than around a happy path. A "arm, trigger, trigger"
// test passes against at least three different objects, only one of which is
// Max's, so the rules that separate them are each pinned on their own:
//
//   - **the arming inlet is idempotent.** Max: the left inlet passes "only if
//     it has received a bang in its right inlet *since* the last time it sent
//     out a bang" — a since, not a count. An object holding a credit counter
//     instead of a bit reads the description the same way and diverges the
//     moment a patch arms twice, which a burst of MIDI or a poll loop does
//     constantly. So: five arms then five triggers is one pass and four
//     rejects.
//   - **nothing is swallowed.** Every message the left inlet accepts produces
//     exactly one bang, on one outlet or the other — Max: "Otherwise, it sends
//     a bang out its right outlet." An implementation that simply dropped a
//     blocked bang passes every "did the right thing come out outlet 0" test
//     and silently loses the half of the object that reports being too early.
//     This is asserted as a running invariant over whole streams, not just per
//     message.
//   - **the gate closes before the bang is sent.** The patcher's send path
//     calls the target inlet directly with no queue in between, so a patch that
//     loops outlet 0 back into the left inlet re-enters the handler *inside*
//     its own SendBang. With the store after the send that re-entry finds the
//     gate still open, which for a one-shot is unbounded recursion rather than
//     a stale read.
//   - **everything is a bang**, including a 0. Max gives int, float and list
//     "Same as a bang" and anything "Converted to bang", so the payload is
//     documented as discarded — this is not .gate, and 0 does not mean off on
//     either inlet.
//   - **`stop`, and only `stop`.** Max's manual disarm on the left inlet,
//     silent, bare, and the only word the object knows — here a symbol is a
//     *trigger*, so any word invented on top of Max's would be a bang silently
//     no longer passed. On the *right* inlet `stop` arms like anything else,
//     which is the file's one documented reading of an ambiguous reference and
//     is pinned so it cannot drift.
//   - **where the object starts.** Disarmed, unless a non-zero creation
//     argument says otherwise (Max: "A non-zero argument sets onebang to permit
//     a bang to be sent out the left outlet the first time...").
//
// The middle of the file is an exhaustive matrix — both states x both inlets x
// every message kind — because "test the states exhaustively" is cheap for a
// machine this small and is the only way to be sure no corner was reasoned
// about rather than run.
//
// The rigs wire objects directly rather than through a patcher, as every
// sibling suite does: that exercises the same outlet::SendBang path the
// pinned-GraphState route of #226 runs.
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
#include "patcher/genericObjects/gOneBang.h"
#include "sinks.hpp"

namespace {

  using YSE::PATCHER::gOneBang;

  // Both outlets, each with its own tag, logging into one vector so a test can
  // read back the exact sequence. OrderSink::count is *cumulative*, so counters
  // alone cannot say which message a bang belonged to and cannot tell "pass,
  // reject" from "pass, pass".
  struct Rig {
    std::unique_ptr<gOneBang> op;
    TestHelpers::OrderSink pass; // outlet 0 — the gate was armed
    TestHelpers::OrderSink reject; // outlet 1 — it was not
    std::vector<char> log;

    explicit Rig(const std::string& args = "") : op(new gOneBang()) {
      if (!args.empty()) op->SetParams(args);
      Wire(pass, 0, 'L');
      Wire(reject, 1, 'R');
    }

    // ── the left (trigger) inlet ──
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

    // ── the right (arming) inlet ──
    void Arm() {
      op->GetInlet(1)->SetBang(YSE::T_GUI);
    }
    void ArmInt(int v) {
      op->GetInlet(1)->SetInt(v, YSE::T_GUI);
    }
    void ArmFloat(float v) {
      op->GetInlet(1)->SetFloat(v, YSE::T_GUI);
    }
    void ArmList(const std::string& text) {
      op->GetInlet(1)->SetList(text, YSE::T_GUI);
    }

    // Everything the object has sent, in order. 'L' = passed, 'R' = rejected.
    std::string Order() const {
      return std::string(log.begin(), log.end());
    }

    void Clear() {
      log.clear();
    }

  private:
    void Wire(TestHelpers::OrderSink& sink, int outlet, char tag) {
      sink.log = &log;
      sink.tag = tag;
      op->ConnectOutlet(sink.GetInlet(0), outlet);
      sink.ConnectInlet(op->GetOutlet(outlet), 0);
    }
  };

  // The message kinds an inlet accepts. Used by the exhaustive matrix so that
  // "every kind, on every inlet, in every state" is enumerated rather than
  // spot-checked.
  enum Kind { K_BANG, K_INT, K_FLOAT, K_LIST, K_ZERO_INT, K_ZERO_FLOAT, K_COUNT };

  const char* KindName(int kind) {
    switch (kind) {
    case K_BANG:
      return "bang";
    case K_INT:
      return "int 7";
    case K_FLOAT:
      return "float 2.5";
    case K_LIST:
      return "list 'wobble bar'";
    case K_ZERO_INT:
      return "int 0";
    default:
      return "float 0.0";
    }
  }

  void Deliver(Rig& rig, int kind, int inlet) {
    if (inlet == 0) {
      switch (kind) {
      case K_BANG:
        rig.Bang();
        return;
      case K_INT:
        rig.SendInt(7);
        return;
      case K_FLOAT:
        rig.Send(2.5f);
        return;
      case K_LIST:
        rig.List("wobble bar");
        return;
      case K_ZERO_INT:
        rig.SendInt(0);
        return;
      default:
        rig.Send(0.f);
        return;
      }
    }
    switch (kind) {
    case K_BANG:
      rig.Arm();
      return;
    case K_INT:
      rig.ArmInt(7);
      return;
    case K_FLOAT:
      rig.ArmFloat(2.5f);
      return;
    case K_LIST:
      rig.ArmList("wobble bar");
      return;
    case K_ZERO_INT:
      rig.ArmInt(0);
      return;
    default:
      rig.ArmFloat(0.f);
      return;
    }
  }

  // A sink that feeds what it receives straight back into the gate's trigger
  // inlet, from *inside* the send. The depth guard is what makes a broken
  // object fail by assertion instead of by exhausting the stack.
  struct FeedbackSink : YSE::PATCHER::pObject {
    gOneBang* target = nullptr;
    std::vector<char>* log = nullptr;
    int hits = 0;
    int depth = 0;
    int maxDepth = 0;
    static constexpr int DEPTH_LIMIT = 8;

    FeedbackSink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) {
        hits++;
        if (log) log->push_back('L');
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

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("onebang: creatable through the registry with two inlets and two bang outlets (#470)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ONEBANG);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".onebang"));
    CHECK(h->GetInputs() == 2);
    // Two outlets, which the object's name does not suggest and its summary
    // line does not mention. Max: "Otherwise, it sends a bang out its right
    // outlet" — a blocked bang is reported, not dropped.
    CHECK(h->GetOutputs() == 2);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BANG);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::BANG);
  }

  TEST_CASE("onebang: listed by pRegistry::AllNames (#470)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".onebang")) != names.end());
  }

  // ─── where the object starts ────────────────────────────────────────────────

  // Max's default. Also the safe one on its own terms: an object that started
  // armed would let a patch *load* fire one bang nobody armed it for.
  TEST_CASE("onebang: a fresh object is disarmed, so the first trigger is rejected (#470)") {
    Rig rig;
    CHECK_FALSE(rig.op->IsArmed());
    CHECK_FALSE(rig.op->InitiallyArmed());

    rig.Bang();
    CHECK(rig.Order() == "R");
    CHECK_FALSE(rig.op->IsArmed());
  }

  // Max: "A non-zero argument sets onebang to permit a bang to be sent out the
  // left outlet the first time a bang is received in the left inlet."
  TEST_CASE("onebang: a non-zero creation argument starts the gate open (#470)") {
    Rig rig("1");
    CHECK(rig.op->InitiallyArmed());
    CHECK(rig.op->IsArmed());

    rig.Bang();
    CHECK(rig.Order() == "L");
    // ...and it is still a one-shot: the argument grants one pass, not a mode.
    rig.Bang();
    CHECK(rig.Order() == "LR");
  }

  TEST_CASE("onebang: a zero creation argument starts the gate closed (#470)") {
    Rig rig("0");
    CHECK_FALSE(rig.op->InitiallyArmed());
    rig.Bang();
    CHECK(rig.Order() == "R");
  }

  // Any non-zero value, not just 1 — the argument is a flag and Max's wording
  // is "a non-zero argument".
  TEST_CASE("onebang: any non-zero creation argument arms, including a negative one (#470)") {
    for (const char* arg : {"1", "2", "-1", "-0.25", "42", "1e-9"}) {
      CAPTURE(arg);
      Rig rig(arg);
      CHECK(rig.op->InitiallyArmed());
      rig.Bang();
      CHECK(rig.Order() == "L");
    }
  }

  // The one knowing deviation from Max, and it is the family's: Max's int
  // argument would truncate 0.5 to a zero and start the object closed. This
  // patcher has one numeric type, and a flag that flipped on the third decimal
  // place of a number nobody meant as a value would be a trap.
  TEST_CASE("onebang: a fractional argument is read as non-zero, not truncated (#470)") {
    Rig rig("0.5");
    CHECK(rig.op->InitiallyArmed());
    rig.Bang();
    CHECK(rig.Order() == "L");
  }

  // Strict, through the shared ReadNumericToken: a token that is not wholly a
  // finite number is not an argument, so it leaves the documented default in
  // place rather than arming the object on a symbol. `inf` is the family's
  // corner — non-finite is "not a number" here, as `.change inf` starting at 0
  // already establishes.
  TEST_CASE(
      "onebang: an argument that is not a whole finite number leaves the gate closed (#470)") {
    for (const char* arg : {"wobble", "1abc", "inf", "-inf", "nan", "1e999", "+", "-"}) {
      CAPTURE(arg);
      Rig rig(arg);
      CHECK_FALSE(rig.op->InitiallyArmed());
      CHECK_FALSE(rig.op->IsArmed());
      rig.Bang();
      CHECK(rig.Order() == "R");
    }
  }

  // ClearParams() is the whole of SetParams(""), since Parameters::Set returns
  // without calling the parse callback for an empty argument.
  TEST_CASE("onebang: clearing the parameters returns the object to Max's default (#470)") {
    Rig rig("1");
    REQUIRE(rig.op->IsArmed());

    rig.op->SetParams("");
    CHECK(rig.Order().empty()); // silent
    CHECK_FALSE(rig.op->InitiallyArmed());
    CHECK_FALSE(rig.op->IsArmed());

    rig.Bang();
    CHECK(rig.Order() == "R");
  }

  // Re-declaring the argument re-applies it, which is what makes a saved
  // `.onebang 1` come back ready to pass rather than merely remembering a
  // string.
  TEST_CASE("onebang: re-parsing the argument restores the state it declares (#470)") {
    Rig rig;
    rig.Bang(); // consumes nothing; already closed
    rig.Clear();

    rig.op->SetParams("1");
    CHECK(rig.Order().empty());
    CHECK(rig.op->IsArmed());
    rig.Bang();
    CHECK(rig.Order() == "L");

    // And back the other way.
    rig.op->SetParams("0");
    CHECK_FALSE(rig.op->IsArmed());
  }

  TEST_CASE("onebang: documents its one creation parameter (#470)") {
    Rig rig;
    const auto& docs = rig.op->GetParamDocs();
    REQUIRE(docs.size() == 1);
    CHECK(docs[0].name == std::string("initial"));
    CHECK(docs[0].defaultValue == std::string("0"));
    CHECK_FALSE(docs[0].doc.empty());
  }

  // ─── the exhaustive state matrix ────────────────────────────────────────────
  //
  // Both states x both inlets x every message kind. Small enough to enumerate,
  // and enumerating it is the only way to be sure no corner was reasoned about
  // rather than run.

  TEST_CASE("onebang: every message kind on the trigger inlet passes when armed (#470)") {
    for (int kind = 0; kind < K_COUNT; kind++) {
      CAPTURE(KindName(kind));
      Rig rig;
      rig.Arm();
      REQUIRE(rig.op->IsArmed());
      rig.Clear();

      Deliver(rig, kind, 0);
      CHECK(rig.Order() == "L");
      // ...and the gate closed behind it.
      CHECK_FALSE(rig.op->IsArmed());
    }
  }

  TEST_CASE("onebang: every message kind on the trigger inlet is rejected when closed (#470)") {
    for (int kind = 0; kind < K_COUNT; kind++) {
      CAPTURE(KindName(kind));
      Rig rig;
      REQUIRE_FALSE(rig.op->IsArmed());

      Deliver(rig, kind, 0);
      CHECK(rig.Order() == "R");
      CHECK_FALSE(rig.op->IsArmed());
    }
  }

  TEST_CASE(
      "onebang: every message kind on the arming inlet opens a closed gate, silently (#470)") {
    for (int kind = 0; kind < K_COUNT; kind++) {
      CAPTURE(KindName(kind));
      Rig rig;
      REQUIRE_FALSE(rig.op->IsArmed());

      Deliver(rig, kind, 1);
      CHECK(rig.Order().empty()); // the cold inlet never emits
      CHECK(rig.op->IsArmed());

      // Observable through the outlet as well as the accessor, because
      // "nothing came out" alone cannot tell a store that happened from a
      // message dropped on the floor.
      rig.Bang();
      CHECK(rig.Order() == "L");
    }
  }

  TEST_CASE("onebang: every message kind on the arming inlet leaves an open gate open (#470)") {
    for (int kind = 0; kind < K_COUNT; kind++) {
      CAPTURE(KindName(kind));
      Rig rig("1");
      REQUIRE(rig.op->IsArmed());

      Deliver(rig, kind, 1);
      CHECK(rig.Order().empty());
      CHECK(rig.op->IsArmed());
    }
  }

  // ─── the arming inlet is idempotent ─────────────────────────────────────────
  //
  // Max: the left inlet passes "only if it has received a bang in its right
  // inlet *since* the last time it sent out a bang". A since, not a count. An
  // object holding a credit counter reads the description the same way and
  // behaves differently the moment a patch arms twice.

  TEST_CASE("onebang: arming five times still passes only one bang (#470)") {
    Rig rig;
    for (int i = 0; i < 5; i++)
      rig.Arm();
    CHECK(rig.Order().empty());

    for (int i = 0; i < 5; i++)
      rig.Bang();
    CHECK(rig.Order() == "LRRRR");
  }

  TEST_CASE("onebang: arming does not bank a pass for later (#470)") {
    Rig rig;
    // Arm, spend it, then arm three more times: still exactly one more pass.
    rig.Arm();
    rig.Bang();
    rig.Arm();
    rig.Arm();
    rig.Arm();
    rig.Bang();
    rig.Bang();
    CHECK(rig.Order() == "LLR");
  }

  // ─── the basic cycle ────────────────────────────────────────────────────────

  TEST_CASE("onebang: one arm grants exactly one pass, repeatedly (#470)") {
    Rig rig;
    for (int i = 0; i < 4; i++) {
      rig.Arm();
      rig.Bang();
    }
    CHECK(rig.Order() == "LLLL");
  }

  TEST_CASE("onebang: repeated triggers with no arming are all rejected (#470)") {
    Rig rig;
    for (int i = 0; i < 6; i++)
      rig.Bang();
    CHECK(rig.Order() == "RRRRRR");
    CHECK_FALSE(rig.op->IsArmed());
  }

  TEST_CASE("onebang: a burst is reduced to its first trigger (#470)") {
    // The object's headline use: fire once on the first trigger of a burst and
    // ignore the rest until something re-enables it.
    Rig rig("1");
    for (int i = 0; i < 10; i++)
      rig.Bang();
    CHECK(rig.Order() == "LRRRRRRRRR");

    rig.Clear();
    rig.Arm();
    for (int i = 0; i < 10; i++)
      rig.Bang();
    CHECK(rig.Order() == "LRRRRRRRRR");
  }

  // ─── nothing is swallowed ───────────────────────────────────────────────────
  //
  // Max: "Otherwise, it sends a bang out its right outlet." The object gates
  // *where* a bang goes, not *whether* one happens — an implementation that
  // simply dropped a blocked bang passes every "did the right thing come out
  // outlet 0" test and silently loses half the object.

  TEST_CASE("onebang: every trigger produces exactly one bang, on one outlet or the other (#470)") {
    Rig rig;
    // A deterministic mix of arms and triggers, arranged so both branches are
    // taken many times and neither the all-armed nor the all-closed case is
    // what is being measured.
    int triggers = 0;
    for (int i = 0; i < 60; i++) {
      if (i % 5 == 0 || i % 7 == 0) {
        rig.Arm();
      } else {
        rig.Bang();
        triggers++;
      }
    }

    const std::string order = rig.Order();
    CHECK((int)order.size() == triggers);
    CHECK(rig.pass.count + rig.reject.count == triggers);
    // Both branches genuinely exercised, or the count above proves nothing.
    CHECK(rig.pass.count > 0);
    CHECK(rig.reject.count > 0);
  }

  TEST_CASE("onebang: passes never outnumber armings over a stream (#470)") {
    Rig rig;
    int arms = 0;
    for (int i = 0; i < 50; i++) {
      if (i % 3 == 0) {
        rig.Arm();
        arms++;
      }
      rig.Bang();
      // The invariant that makes this a gate rather than a counter, checked
      // after every single message rather than once at the end.
      CHECK(rig.pass.count <= arms);
    }
    CHECK(rig.pass.count > 0);
  }

  // ─── everything is a bang ───────────────────────────────────────────────────

  // Worth its own case because the number reads like a "don't". This is not
  // .gate: 0 does not mean off anywhere in this object, on either inlet.
  TEST_CASE("onebang: a 0 on the trigger inlet triggers like anything else (#470)") {
    Rig rig;
    rig.Arm();
    rig.SendInt(0);
    CHECK(rig.Order() == "L");

    rig.Clear();
    rig.Arm();
    rig.Send(0.f);
    CHECK(rig.Order() == "L");
  }

  TEST_CASE("onebang: a 0 on the arming inlet arms like anything else (#470)") {
    Rig rig;
    rig.ArmInt(0);
    CHECK(rig.op->IsArmed());
    rig.Bang();
    CHECK(rig.Order() == "L");

    rig.Clear();
    rig.ArmFloat(0.f);
    CHECK(rig.op->IsArmed());
  }

  // Max: int, float and list are all "Same as a bang", anything is "Converted
  // to bang". The payload is documented as discarded, so mixing the kinds
  // cannot change the sequence.
  TEST_CASE("onebang: the message kind never changes the outcome (#470)") {
    Rig rig;
    rig.Arm();
    rig.SendInt(-3);
    rig.Send(99.5f);
    rig.List("anything at all");
    rig.Bang();
    CHECK(rig.Order() == "LRRR");
  }

  TEST_CASE("onebang: a list of any text triggers the gate (#470)") {
    for (const char* text :
         {"5", "5 6", "hello", "hello world", "", "   ", "reset", "clear", "set"}) {
      CAPTURE(text);
      Rig rig;
      rig.Arm();
      rig.List(text);
      CHECK(rig.Order() == "L");
    }
  }

  // The family's words are deliberately *not* claimed here — see "stop, and why
  // it is the only word" in the header. Everywhere else a symbol is a message
  // the object would ignore anyway, so `reset` costs nothing; here a symbol is
  // a trigger, so every word learned is a bang silently no longer passed.
  TEST_CASE("onebang: reset and clear are ordinary triggers, not messages (#470)") {
    for (const char* text : {"reset", "clear", "set 1", "bang"}) {
      CAPTURE(text);
      Rig rig;
      rig.List(text);
      CHECK(rig.Order() == "R"); // triggered, not silently obeyed
      CHECK_FALSE(rig.op->IsArmed()); // and it did not re-arm anything
    }
  }

  // ─── stop ───────────────────────────────────────────────────────────────────

  // Max: "In left inlet: Undoes the effect of a bang in the right inlet."
  // Silent — the reference lists only `bang` under Output.
  TEST_CASE("onebang: stop closes an open gate, silently (#470)") {
    Rig rig;
    rig.Arm();
    REQUIRE(rig.op->IsArmed());

    rig.List("stop");
    CHECK(rig.Order().empty());
    CHECK_FALSE(rig.op->IsArmed());

    rig.Bang();
    CHECK(rig.Order() == "R");
  }

  TEST_CASE("onebang: stop on an already closed gate is a silent no-op (#470)") {
    Rig rig;
    REQUIRE_FALSE(rig.op->IsArmed());
    rig.List("stop");
    rig.List("stop");
    CHECK(rig.Order().empty());
    CHECK_FALSE(rig.op->IsArmed());
  }

  TEST_CASE("onebang: stop undoes a creation argument's arming too (#470)") {
    Rig rig("1");
    REQUIRE(rig.op->IsArmed());
    rig.List("stop");
    CHECK(rig.Order().empty());
    rig.Bang();
    CHECK(rig.Order() == "R");
  }

  TEST_CASE("onebang: arming after a stop opens the gate again (#470)") {
    Rig rig;
    rig.Arm();
    rig.List("stop");
    rig.Arm();
    CHECK(rig.Order().empty());
    rig.Bang();
    CHECK(rig.Order() == "L");
  }

  // Bare, as .change's `mode` and .past's `clear` are: a message word with an
  // argument is a different message. Here that rule has teeth — anything that
  // is not exactly `stop` is a list, and a list is a bang.
  TEST_CASE("onebang: only the bare word stop is the stop message (#470)") {
    for (const char* text : {"stop 1", "stopped", "stop stop", " stop", "stop ", "Stop", "STOP"}) {
      CAPTURE(text);
      Rig rig;
      rig.Arm();
      REQUIRE(rig.op->IsArmed());

      rig.List(text);
      // Converted to a bang, so it *passed* rather than disarming silently.
      CHECK(rig.Order() == "L");
      CHECK_FALSE(rig.op->IsArmed());
    }
  }

  // The file's one documented reading of an ambiguous reference. Max scopes
  // `stop` to the left inlet while marking int, float, list and anything "in
  // either inlet", and describes the arming inlet with no conditions at all —
  // so a patch is entitled to treat a cord into it as "the gate is now open",
  // which would hold only for inspected message text if one string out of all
  // possible strings closed it instead.
  TEST_CASE("onebang: stop on the arming inlet arms rather than disarming (#470)") {
    Rig rig;
    rig.ArmList("stop");
    CHECK(rig.Order().empty());
    CHECK(rig.op->IsArmed());

    rig.Bang();
    CHECK(rig.Order() == "L");
  }

  TEST_CASE("onebang: stop on the arming inlet cannot close an open gate (#470)") {
    Rig rig("1");
    REQUIRE(rig.op->IsArmed());
    rig.ArmList("stop");
    CHECK(rig.op->IsArmed());
    rig.Bang();
    CHECK(rig.Order() == "L");
  }

  // ─── the gate closes before the bang is sent ────────────────────────────────

  // Not decoration: the patcher's send path calls the target inlet directly
  // with no queue in between, so a patch that loops outlet 0 back into the
  // trigger inlet re-enters the handler *inside* its own SendBang. With the
  // store after the send, the re-entry finds the gate still open and passes
  // again — unbounded recursion for a one-shot, not merely a stale read.
  TEST_CASE("onebang: a re-entrant trigger from outlet 0 finds the gate already closed (#470)") {
    std::vector<char> log;
    std::unique_ptr<gOneBang> op(new gOneBang());
    FeedbackSink feedback;
    TestHelpers::OrderSink reject;

    feedback.target = op.get();
    feedback.log = &log;
    op->ConnectOutlet(feedback.GetInlet(0), 0);
    feedback.ConnectInlet(op->GetOutlet(0), 0);

    reject.log = &log;
    reject.tag = 'R';
    op->ConnectOutlet(reject.GetInlet(0), 1);
    reject.ConnectInlet(op->GetOutlet(1), 0);

    op->GetInlet(1)->SetBang(YSE::T_GUI); // arm
    op->GetInlet(0)->SetBang(YSE::T_GUI); // trigger

    // One pass, then the re-entry is rejected, and the recursion stops there.
    CHECK(feedback.hits == 1);
    CHECK(feedback.maxDepth == 1);
    CHECK(std::string(log.begin(), log.end()) == "LR");
    CHECK_FALSE(op->IsArmed());
  }

  // ─── the DSP tick ───────────────────────────────────────────────────────────

  // A hot inlet fires CalculateIfReady() after every message it accepts, so a
  // Calculate() that emitted would make `stop` emit too and would re-fire the
  // gate on every block — for a one-shot, the exact failure the object exists
  // to prevent.
  TEST_CASE("onebang: Calculate does nothing (#470)") {
    Rig rig("1");
    for (int i = 0; i < 10; i++)
      rig.op->Calculate(YSE::T_GUI);

    CHECK(rig.Order().empty());
    CHECK(rig.op->IsArmed()); // the pass was not consumed either

    rig.Bang();
    CHECK(rig.Order() == "L");
    rig.Clear();

    for (int i = 0; i < 10; i++)
      rig.op->Calculate(YSE::T_GUI);
    CHECK(rig.Order().empty());
    CHECK_FALSE(rig.op->IsArmed());
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  TEST_CASE("onebang: survives a DumpJSON / ParseJSON round trip (#470)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_ONEBANG, "1");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".onebang") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".onebang"));
    CHECK(copy->GetParams() == std::string("1"));
    CHECK(copy->GetInputs() == 2);
    CHECK(copy->GetOutputs() == 2);
    // The argument has to come back as *state*, not merely as a stored string,
    // or a saved one-shot silently loses its first pass on reload.
    CHECK(copy->GetGuiValue() == std::string("1"));
  }

  TEST_CASE("onebang: an argument-free object round trips too (#470)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_ONEBANG) != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(copy->GetParams().empty());
    CHECK(copy->GetGuiValue() == std::string("0"));
  }

  TEST_CASE("onebang: the GUI value reports whether the next trigger will pass (#470)") {
    Rig rig;
    CHECK(rig.op->GetGuiValue() == std::string("0"));
    rig.Arm();
    CHECK(rig.op->GetGuiValue() == std::string("1"));
    rig.Bang();
    CHECK(rig.op->GetGuiValue() == std::string("0"));
    rig.Arm();
    rig.List("stop");
    CHECK(rig.op->GetGuiValue() == std::string("0"));
  }

} // TEST_SUITE

// Tests for .bucket (issue #478) — pass numbers from outlet to outlet, shifting
// on each input.
//
// Six rules carry this file, and each is one a plausible implementation gets
// wrong:
//
//   - **the register is one step behind by default.** Max: "The numbers
//     currently stored in bucket are sent out, then each number is moved one
//     outlet to the right and the new number is stored to be sent out the left
//     outlet the next time a number is received." An implementation that sends
//     after shifting passes every "outlet 1 is the previous value" check while
//     being off by one everywhere, and is silently the *other* documented mode.
//   - **the second creation argument is that other mode.** Max's "echo to
//     output": same register, the shift and the send swapped.
//   - **every outlet fires, right to left.** This is what separates it from
//     .cycle, which fires exactly one. A test that only reads outlet 0 cannot
//     tell them apart, and one that only counts hits cannot tell the firing
//     order from its reverse.
//   - **the burst is captured before the state changes, and the state settles
//     before the first send.** A patch that re-enters mid-burst must find a
//     register that has already shifted, and the interrupted burst must go on
//     emitting the values it started with — which a shared member buffer would
//     not.
//   - **stored values keep their spelling.** An int that comes back out as a
//     float rewrites the type of everything passing through.
//   - **freeze gags the output without stopping the register.** Max: "suspends
//     the bucket output, but new incoming numbers continue to shift the stored
//     values internally." An implementation that stops shifting loses the
//     history that thawing is supposed to reveal.
//
// The rest is the message grammar from the Max reference: `set`, `clear`,
// `freeze` / `thaw`, `L2R` / `R2L` and `roll`.
//
// The standalone rigs wire objects directly, as every sibling suite does; the
// round-trip and end-to-end cases run through a real patcher so the guarantees
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
#include "patcher/genericObjects/gBucket.h"
#include "patcher/sinks.hpp"

namespace {

  using TestHelpers::FloatSink;
  using TestHelpers::OrderSink;
  using YSE::PATCHER::gBucket;

  // One order-logging sink per outlet, all sharing one log, so "which outlet
  // received what, and in what order" is an assertion rather than an inference.
  // Outlet i is tagged 'a' + i, so a single input reads back as the firing
  // order itself.
  struct Rig {
    gBucket op;
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
    // Log *and* hit counts, for the cases that assert nothing came out at all
    // after the register has already been fed.
    void Reset() {
      order.clear();
      for (auto& sink : sinks)
        sink->count = 0;
    }
    int Total() const {
      int total = 0;
      for (const auto& sink : sinks)
        total += sink->count;
      return total;
    }
  };

  // A sink that pushes a value back into the object under test, a bounded
  // number of times. A .bucket wired straight back into its own inlet never
  // terminates — every input produces an output on every outlet, so the loop
  // has no exit — which is exactly why the re-entrancy case needs a receiver
  // that stops feeding rather than a bare patch cord.
  struct FeedbackSink : YSE::PATCHER::pObject {
    gBucket* target = nullptr;
    int fireValue = 0;
    int budget = 0;

    std::vector<char>* log = nullptr;
    char tag = '?';
    int lastInt = 0;
    int count = 0;

    FeedbackSink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD thread) {
        lastInt = v;
        count++;
        if (log) log->push_back(tag);
        if (budget > 0 && target != nullptr) {
          budget--;
          target->GetInlet(0)->SetInt(fireValue, thread);
        }
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

  TEST_CASE("bucket: creatable through the registry (#478)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_BUCKET, "4");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".bucket"));
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 4);
  }

  TEST_CASE("bucket: listed by pRegistry::AllNames (#478)") {
    auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(YSE::OBJ::G_BUCKET)) != names.end());
  }

  TEST_CASE("bucket: one outlet with no argument, as Max documents (#478)") {
    // Max: "Sets the number of outlets. If there is no argument, there will be
    // one outlet."
    gBucket op;
    CHECK(op.OutletCount() == 1);
    CHECK_FALSE(op.EchoToOutput());
    CHECK_FALSE(op.ShiftsRightToLeft());
    CHECK_FALSE(op.Frozen());
  }

  TEST_CASE("bucket: a fresh register holds int zeroes (#478)") {
    gBucket op;
    op.SetParams("3");
    for (int i = 0; i < op.OutletCount(); i++) {
      CHECK(op.StoredValue(i) == doctest::Approx(0.f));
      CHECK_FALSE(op.StoredIsFloat(i));
    }
  }

  TEST_CASE("bucket: the outlet count is clamped to 1-256 (#478)") {
    gBucket low;
    low.SetParams("0");
    CHECK(low.OutletCount() == 1);

    gBucket negative;
    negative.SetParams("-7");
    CHECK(negative.OutletCount() == 1);

    gBucket high;
    high.SetParams("9999");
    CHECK(high.OutletCount() == gBucket::MAX_PORTS);

    // A float argument is truncated, and a non-number leaves the default.
    gBucket fractional;
    fractional.SetParams("3.9");
    CHECK(fractional.OutletCount() == 3);

    gBucket nonsense;
    nonsense.SetParams("deep");
    CHECK(nonsense.OutletCount() == 1);
  }

  TEST_CASE("bucket: SetParams(\"\") returns the object to Max's default shape (#478)") {
    gBucket op;
    op.SetParams("5 1");
    REQUIRE(op.OutletCount() == 5);
    REQUIRE(op.EchoToOutput());

    op.SetParams("");
    CHECK(op.OutletCount() == 1);
    CHECK_FALSE(op.EchoToOutput());
  }

  TEST_CASE("bucket: every outlet is ANY, since a stage returns the kind it was given (#478)") {
    gBucket op;
    op.SetParams("3");
    for (int i = 0; i < op.OutletCount(); i++)
      CHECK(op.GetOutputType(i) == YSE::OUT_TYPE::ANY);
  }

  // ─── the shift ──────────────────────────────────────────────────────────────

  TEST_CASE("bucket: values shift one outlet along per input (#478)") {
    // The object. Max: "each number is moved one outlet to the right and the new
    // number is stored to be sent out the left outlet the next time a number is
    // received."
    Rig rig("3");
    for (int i = 1; i <= 4; i++)
      rig.SendInt(i);

    // The fourth input hands out the register as it stood after the third.
    CHECK(rig.sinks[0]->lastInt == 3);
    CHECK(rig.sinks[1]->lastInt == 2);
    CHECK(rig.sinks[2]->lastInt == 1);

    // ...and the value that just arrived is the one the *next* input will show.
    CHECK(rig.op.StoredValue(0) == doctest::Approx(4.f));
    CHECK(rig.op.StoredValue(1) == doctest::Approx(3.f));
    CHECK(rig.op.StoredValue(2) == doctest::Approx(2.f));
  }

  TEST_CASE("bucket: the default mode is one step behind the input (#478)") {
    // The first input hands out the empty register, not itself — Max's
    // documented order, and the difference between the default and "echo to
    // output" mode. An implementation that shifted first would report 1 here.
    Rig rig("3");
    rig.SendInt(1);
    CHECK(rig.sinks[0]->lastInt == 0);
    CHECK(rig.sinks[1]->lastInt == 0);
    CHECK(rig.sinks[2]->lastInt == 0);
    CHECK(rig.op.StoredValue(0) == doctest::Approx(1.f));
  }

  TEST_CASE("bucket: every outlet fires on every input (#478)") {
    // The line between this object and .cycle, which fires exactly one outlet
    // per message. An implementation that rotated would pass every "outlet 0
    // holds the newest value" check.
    Rig rig("4");
    rig.SendInt(9);
    CHECK(rig.Total() == 4);
    for (const auto& sink : rig.sinks)
      CHECK(sink->count == 1);
  }

  TEST_CASE("bucket: outlets fire right to left (#478)") {
    // Max's universal order, and a guarantee a patch can build on: a downstream
    // collector sees the oldest value before the newest. A left-to-right
    // implementation passes every value check and reverses this log.
    Rig rig("3");
    rig.SendInt(1);
    CHECK(rig.Log() == "cba");
    rig.SendInt(2);
    CHECK(rig.Log() == "cbacba");
  }

  TEST_CASE("bucket: the second creation argument echoes the input to outlet 0 (#478)") {
    // Max: "A second non-zero argument sets the bucket object to 'echo to
    // output' mode, whereby the number received in the inlet is stored and sent
    // out the left outlet when it is received."
    Rig rig("3 1");
    REQUIRE(rig.op.EchoToOutput());
    rig.SendInt(1);
    CHECK(rig.sinks[0]->lastInt == 1); // itself, not the zero before it
    CHECK(rig.sinks[1]->lastInt == 0);

    rig.SendInt(2);
    CHECK(rig.sinks[0]->lastInt == 2);
    CHECK(rig.sinks[1]->lastInt == 1);
    CHECK(rig.sinks[2]->lastInt == 0);

    // The firing order is the mode's business either way.
    CHECK(rig.Log() == "cbacba");
  }

  TEST_CASE("bucket: an explicit zero echo flag is the default mode (#478)") {
    gBucket op;
    op.SetParams("3 0");
    CHECK_FALSE(op.EchoToOutput());

    // Max: "a second non-zero argument" — any non-zero value, not only 1.
    op.SetParams("3 -2");
    CHECK(op.EchoToOutput());
  }

  TEST_CASE("bucket: a one-outlet object is a one-step delay (#478)") {
    // The default shape. Degenerate but documented, and it is still the
    // register: what comes out is what went in last time.
    Rig rig;
    REQUIRE(rig.op.OutletCount() == 1);
    rig.SendInt(7);
    CHECK(rig.sinks[0]->lastInt == 0);
    rig.SendInt(8);
    CHECK(rig.sinks[0]->lastInt == 7);
    rig.SendInt(9);
    CHECK(rig.sinks[0]->lastInt == 8);
  }

  TEST_CASE("bucket: stored values keep their spelling (#478)") {
    // The int-atom / float-atom test .trigger classifies its constants with and
    // .cycle deals its tokens by. An implementation that normalised the register
    // to float would pass every value check while rewriting the type of
    // everything downstream (.route 1, .sel, .i and .match all tell them apart).
    Rig rig("2");
    rig.SendFloat(2.5f);
    rig.SendInt(7);

    CHECK(rig.sinks[0]->lastKind == OrderSink::FLOAT);
    CHECK(rig.sinks[0]->lastFloat == doctest::Approx(2.5f));
    CHECK(rig.sinks[1]->lastKind == OrderSink::INT);
    CHECK(rig.sinks[1]->lastInt == 0);

    CHECK(rig.op.StoredIsFloat(1));
    CHECK_FALSE(rig.op.StoredIsFloat(0));
  }

  // ─── bang ───────────────────────────────────────────────────────────────────

  TEST_CASE("bucket: a bang reports the register without shifting it (#478)") {
    // Max: "bang — all stored values are sent out, but their position is not
    // shifted." A read, not a value: this is where .bucket differs from .buddy
    // and .bondo, whose bang is Max's zero.
    Rig rig("3");
    rig.SendInt(1);
    rig.SendInt(2);
    rig.SendInt(3);
    rig.ClearLog();

    rig.Bang();
    CHECK(rig.Log() == "cba");
    CHECK(rig.sinks[0]->lastInt == 3);
    CHECK(rig.sinks[1]->lastInt == 2);
    CHECK(rig.sinks[2]->lastInt == 1);

    // Nothing moved, so a second bang says the same thing.
    rig.Bang();
    CHECK(rig.sinks[0]->lastInt == 3);
    CHECK(rig.op.StoredValue(0) == doctest::Approx(3.f));
  }

  // ─── direction ──────────────────────────────────────────────────────────────

  TEST_CASE("bucket: 'R2L' puts new values at the far end and 'L2R' undoes it (#478)") {
    // Max: "R2L — sets bucket to shift its stored values from right to left ...,
    // placing the incoming number in the rightmost outlet"; "L2R — ... from left
    // to right (the default)".
    Rig rig("3");
    rig.List("R2L");
    CHECK(rig.op.ShiftsRightToLeft());
    CHECK(rig.Total() == 0); // the word itself emits nothing

    rig.SendInt(1);
    rig.SendInt(2);
    rig.SendInt(3);
    CHECK(rig.op.StoredValue(0) == doctest::Approx(1.f));
    CHECK(rig.op.StoredValue(1) == doctest::Approx(2.f));
    CHECK(rig.op.StoredValue(2) == doctest::Approx(3.f));

    // The outlets still fire right to left: the firing order is about outlets,
    // the direction is about storage.
    rig.ClearLog();
    rig.Bang();
    CHECK(rig.Log() == "cba");

    rig.List("L2R");
    CHECK_FALSE(rig.op.ShiftsRightToLeft());
    rig.SendInt(4);
    CHECK(rig.op.StoredValue(0) == doctest::Approx(4.f));
    CHECK(rig.op.StoredValue(1) == doctest::Approx(1.f));
  }

  TEST_CASE("bucket: the same input fills the register the other way round (#478)") {
    // The two directions, side by side, so the difference is the assertion
    // rather than an inference from one of them.
    Rig forward("3");
    Rig backward("3");
    backward.List("R2L");
    for (int i = 1; i <= 3; i++) {
      forward.SendInt(i);
      backward.SendInt(i);
    }
    CHECK(forward.op.StoredValue(0) == doctest::Approx(3.f));
    CHECK(forward.op.StoredValue(2) == doctest::Approx(1.f));
    CHECK(backward.op.StoredValue(0) == doctest::Approx(1.f));
    CHECK(backward.op.StoredValue(2) == doctest::Approx(3.f));
  }

  // ─── set / clear ────────────────────────────────────────────────────────────

  TEST_CASE("bucket: 'set' fills every stage and sends the value out every outlet (#478)") {
    // Max: "The word set, followed by a number, sends that number out each
    // outlet, and stores the number as the next value to be sent out each of its
    // outlets." It *emits*, unlike .cycle's and .past's silent sets, because it
    // changes the whole visible contents of the register rather than a cursor.
    Rig rig("3");
    rig.SendInt(1);
    rig.ClearLog();

    rig.List("set 5");
    CHECK(rig.Log() == "cba");
    for (const auto& sink : rig.sinks)
      CHECK(sink->lastInt == 5);
    for (int i = 0; i < rig.op.OutletCount(); i++)
      CHECK(rig.op.StoredValue(i) == doctest::Approx(5.f));

    // And the filled register is what the next input hands out.
    rig.ClearLog();
    rig.SendInt(9);
    CHECK(rig.sinks[2]->lastInt == 5);
    CHECK(rig.op.StoredValue(0) == doctest::Approx(9.f));
  }

  TEST_CASE("bucket: 'set' keeps the spelling of its argument (#478)") {
    Rig rig("2");
    rig.List("set 2.5");
    CHECK(rig.sinks[0]->lastKind == OrderSink::FLOAT);
    CHECK(rig.sinks[0]->lastFloat == doctest::Approx(2.5f));
    CHECK(rig.op.StoredIsFloat(1));
  }

  TEST_CASE("bucket: a bare 'set' and a non-numeric one are ignored (#478)") {
    // The word has to end where it ends and be followed by a number, the rule
    // MatchWord enforces for every `<word> <number>` message in the patcher.
    Rig rig("2");
    rig.List("set");
    rig.List("set later");
    rig.List("settings 1");
    CHECK(rig.Total() == 0);
    CHECK(rig.op.StoredValue(0) == doctest::Approx(0.f));
  }

  TEST_CASE("bucket: 'clear' empties the register silently (#478)") {
    // Max: "The clear message resets the internal values of bucket without
    // causing any output." The silent one, where `set` is not.
    Rig rig("3");
    rig.SendFloat(1.5f);
    rig.SendInt(2);
    rig.Reset();

    rig.List("clear");
    CHECK(rig.Total() == 0);
    for (int i = 0; i < rig.op.OutletCount(); i++) {
      CHECK(rig.op.StoredValue(i) == doctest::Approx(0.f));
      CHECK_FALSE(rig.op.StoredIsFloat(i)); // back to int zeroes, as at creation
    }

    rig.Bang();
    CHECK(rig.sinks[0]->lastKind == OrderSink::INT);
    CHECK(rig.sinks[0]->lastInt == 0);
  }

  // ─── freeze / thaw ──────────────────────────────────────────────────────────

  TEST_CASE("bucket: 'freeze' gags the outlets while the register goes on shifting (#478)") {
    // Max: "freeze — suspends the bucket output, but new incoming numbers
    // continue to shift the stored values internally"; "thaw — resumes bucket
    // output." An implementation that stopped shifting would lose exactly the
    // history the thaw is supposed to reveal.
    Rig rig("3");
    rig.List("freeze");
    CHECK(rig.op.Frozen());

    rig.SendInt(1);
    rig.SendInt(2);
    rig.SendInt(3);
    CHECK(rig.Total() == 0); // nothing came out

    // ...but the register recorded it all.
    CHECK(rig.op.StoredValue(0) == doctest::Approx(3.f));
    CHECK(rig.op.StoredValue(1) == doctest::Approx(2.f));
    CHECK(rig.op.StoredValue(2) == doctest::Approx(1.f));

    rig.List("thaw");
    CHECK_FALSE(rig.op.Frozen());
    rig.Bang();
    CHECK(rig.Log() == "cba");
    CHECK(rig.sinks[0]->lastInt == 3);
    CHECK(rig.sinks[2]->lastInt == 1);
  }

  TEST_CASE("bucket: a freeze covers bang, 'set' and 'roll' as well (#478)") {
    // "The bucket output" is the object's output, not one message's.
    Rig rig("3");
    rig.SendInt(1);
    rig.Reset();

    rig.List("freeze");
    rig.Bang();
    rig.List("set 4");
    rig.List("roll");
    CHECK(rig.Total() == 0);

    // The state changes still happened: `set` filled the register, and `roll`
    // then rotated it.
    CHECK(rig.op.StoredValue(0) == doctest::Approx(4.f));
  }

  // ─── roll ───────────────────────────────────────────────────────────────────

  TEST_CASE("bucket: 'roll' rotates the register without adding anything (#478)") {
    // Max: "The word roll ... causes bucket to use the value stored in its
    // rightmost outlet as input; thus, it sends its output, shifts all stored
    // values to the right, then stores the value which had been in the rightmost
    // outlet in the leftmost outlet." Nothing enters and nothing is lost, so a
    // full turn returns the register to where it started — which an
    // implementation that dropped the far value instead would fail.
    Rig rig("3");
    rig.SendInt(1);
    rig.SendInt(2);
    rig.SendInt(3);
    REQUIRE(rig.op.StoredValue(0) == doctest::Approx(3.f));
    rig.ClearLog();

    rig.List("roll");
    CHECK(rig.Log() == "cba"); // it sends, like any other input
    CHECK(rig.op.StoredValue(0) == doctest::Approx(1.f));
    CHECK(rig.op.StoredValue(1) == doctest::Approx(3.f));
    CHECK(rig.op.StoredValue(2) == doctest::Approx(2.f));

    rig.List("roll");
    rig.List("roll");
    CHECK(rig.op.StoredValue(0) == doctest::Approx(3.f));
    CHECK(rig.op.StoredValue(1) == doctest::Approx(2.f));
    CHECK(rig.op.StoredValue(2) == doctest::Approx(1.f));
  }

  TEST_CASE("bucket: 'roll' takes an argument Max does not read (#478)") {
    // Max: "The word roll, followed by any number" — *any* number, so the value
    // is not read and the bare word is the same message.
    Rig withArgument("3");
    Rig bare("3");
    for (int i = 1; i <= 3; i++) {
      withArgument.SendInt(i);
      bare.SendInt(i);
    }
    withArgument.List("roll 99");
    bare.List("roll");
    for (int i = 0; i < 3; i++)
      CHECK(withArgument.op.StoredValue(i) == doctest::Approx(bare.op.StoredValue(i)));
    CHECK(withArgument.op.StoredValue(0) == doctest::Approx(1.f));
  }

  TEST_CASE("bucket: 'roll' follows the direction the register is shifting in (#478)") {
    // Max documents the rightmost outlet because it documents the default
    // direction. The value taken is the one the next input would push off, so a
    // rolling register keeps rotating rather than duplicating a value once R2L
    // is set.
    Rig rig("3");
    rig.List("R2L");
    rig.SendInt(1);
    rig.SendInt(2);
    rig.SendInt(3);
    REQUIRE(rig.op.StoredValue(0) == doctest::Approx(1.f));

    rig.List("roll");
    // The far end going right-to-left is outlet 0, so the 1 comes off the front
    // and lands at the back.
    CHECK(rig.op.StoredValue(0) == doctest::Approx(2.f));
    CHECK(rig.op.StoredValue(1) == doctest::Approx(3.f));
    CHECK(rig.op.StoredValue(2) == doctest::Approx(1.f));

    // Still a rotation: three of them come back to the start.
    rig.List("roll");
    rig.List("roll");
    CHECK(rig.op.StoredValue(0) == doctest::Approx(1.f));
    CHECK(rig.op.StoredValue(2) == doctest::Approx(3.f));
  }

  // ─── lists and unknown messages ─────────────────────────────────────────────

  TEST_CASE("bucket: a number-leading message is fed in token by token (#478)") {
    // Max documents no `list` method, so a multi-number message would reach the
    // `int` method with the extra atoms dropped. A shift register is a thing you
    // push a sequence through, so each numeric token is one full input here —
    // and a one-element list still behaves exactly as Max's `int`.
    Rig rig("3");
    rig.List("1 2 3");
    CHECK(rig.op.StoredValue(0) == doctest::Approx(3.f));
    CHECK(rig.op.StoredValue(1) == doctest::Approx(2.f));
    CHECK(rig.op.StoredValue(2) == doctest::Approx(1.f));

    // Three inputs, so three full bursts of three outlets.
    CHECK(rig.Total() == 9);
    CHECK(rig.Log() == "cbacbacba");
  }

  TEST_CASE("bucket: a one-element list is exactly an int (#478)") {
    Rig fromList("2");
    Rig fromInt("2");
    fromList.List("42");
    fromInt.SendInt(42);
    CHECK(fromList.op.StoredValue(0) == doctest::Approx(fromInt.op.StoredValue(0)));
    CHECK(fromList.Total() == fromInt.Total());
  }

  TEST_CASE("bucket: non-numeric tokens inside a numeric message are skipped (#478)") {
    // The register stores numbers, and there is no sensible stage value for a
    // word.
    Rig rig("3");
    rig.List("1 two 3");
    CHECK(rig.op.StoredValue(0) == doctest::Approx(3.f));
    CHECK(rig.op.StoredValue(1) == doctest::Approx(1.f));
    CHECK(rig.Total() == 6); // two inputs, not three
  }

  TEST_CASE("bucket: a symbol-leading message is ignored (#478)") {
    // Max's `anything`, which bucket does not understand. Forwarding a word
    // through a register of numbers would hand downstream objects a value the
    // object cannot hold.
    Rig rig("3");
    rig.SendInt(5);
    rig.Reset();

    rig.List("note 60 100");
    rig.List("bang please");
    CHECK(rig.Total() == 0);
    CHECK(rig.op.StoredValue(0) == doctest::Approx(5.f));
  }

  // ─── re-entrancy ────────────────────────────────────────────────────────────

  TEST_CASE("bucket: a burst survives a message looping back into the inlet (#478)") {
    // Two guarantees at once, and each has its own mutation.
    //
    //   - the register has already shifted when the loop arrives, so the
    //     re-entrant input builds on the new contents rather than repeating the
    //     step (checked through the final stored values);
    //   - the interrupted burst goes on emitting the values it *started* with,
    //     which a shared member capture buffer would not — the inner burst would
    //     overwrite it and the outer outlets would report the loop's register.
    gBucket op;
    op.SetParams("3");

    std::vector<char> order;
    OrderSink first; // outlet 0
    OrderSink second; // outlet 1
    FeedbackSink third;
    first.tag = 'a';
    second.tag = 'b';
    third.tag = 'c';
    first.log = second.log = third.log = &order;
    third.target = &op;
    third.fireValue = 99;

    op.ConnectOutlet(first.GetInlet(0), 0);
    first.ConnectInlet(op.GetOutlet(0), 0);
    op.ConnectOutlet(second.GetInlet(0), 1);
    second.ConnectInlet(op.GetOutlet(1), 0);
    op.ConnectOutlet(third.GetInlet(0), 2);
    third.ConnectInlet(op.GetOutlet(2), 0);

    // Fill the register: 3 2 1.
    for (int i = 1; i <= 3; i++)
      op.GetInlet(0)->SetInt(i, YSE::T_GUI);
    order.clear();

    // Armed only now, so the fill above is three plain inputs.
    third.budget = 1; // exactly one trip round the loop

    op.GetInlet(0)->SetInt(4, YSE::T_GUI);

    // Outlet 2 fires first and re-enters; the inner burst runs to completion
    // (its outlet 2 finds the feedback budget spent), then the outer burst
    // finishes with the values it captured.
    CHECK(std::string(order.begin(), order.end()) == "ccbaba");

    // The inner burst carried the shifted register 4 3 2; the outer finished
    // with the 3 2 1 it started with, so these are the outer values.
    CHECK(second.lastInt == 2);
    CHECK(first.lastInt == 3);

    // And the loop built on the shifted register rather than repeating the step:
    // 3 2 1 -> 4 3 2 -> 99 4 3.
    CHECK(op.StoredValue(0) == doctest::Approx(99.f));
    CHECK(op.StoredValue(1) == doctest::Approx(4.f));
    CHECK(op.StoredValue(2) == doctest::Approx(3.f));
  }

  // ─── real-time / graph ──────────────────────────────────────────────────────

  TEST_CASE("bucket: Calculate() emits nothing (#478)") {
    // The object is driven by its inlet. An emitting Calculate() would push the
    // register one step per DSP block from a stimulus no patch sent.
    Rig rig("3");
    rig.SendInt(1);
    rig.Reset();
    for (int i = 0; i < 8; i++)
      rig.op.Calculate(YSE::T_DSP);
    CHECK(rig.Total() == 0);
    CHECK(rig.op.StoredValue(0) == doctest::Approx(1.f));
  }

  TEST_CASE("bucket: survives a DumpJSON / ParseJSON round trip (#478)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_BUCKET);
    REQUIRE(h != nullptr);
    h->SetParams("5 1");
    const std::string json = src.DumpJSON();
    CHECK(json.find(".bucket") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".bucket"));
    CHECK(copy->GetParams() == std::string("5 1"));
    // The shape has to come back with it, or the saved patch cords land on the
    // wrong stages — and so does the echo flag, which is the difference between
    // a register that shows the value that arrived and one that shows the
    // previous one.
    CHECK(copy->GetOutputs() == 5);
  }

  TEST_CASE("bucket: 'R2L' and 'freeze' are run-time state, not parameters (#478)") {
    // The creation arguments are what a saved patch carries, exactly as
    // .cycle's `thresh` and .uzi's `pause` are run-time state rather than saved
    // state.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_BUCKET, "3");
    REQUIRE(h != nullptr);
    h->SetListData(0, "R2L");
    h->SetListData(0, "freeze");
    CHECK(h->GetParams() == std::string("3"));
  }

  // ─── end to end ─────────────────────────────────────────────────────────────

  TEST_CASE("bucket: echoes a stream one step later in a real patcher (#478)") {
    // The headline use, end to end through real objects: a value and the value
    // before it, side by side, which is the canon / arpeggio-echo shape. Each
    // path adds a different constant so the sinks' values are only right if the
    // stages held what they should.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* bucket = p.CreateObject(YSE::OBJ::G_BUCKET, "2 1");
    YSE::pHandle* addTen = p.CreateObject(YSE::OBJ::G_ADD, "10");
    YSE::pHandle* addHundred = p.CreateObject(YSE::OBJ::G_ADD, "100");
    REQUIRE(bucket != nullptr);
    REQUIRE(addTen != nullptr);
    REQUIRE(addHundred != nullptr);

    FloatSink now;
    FloatSink before;
    YSE::pHandle nowHandle(&now);
    YSE::pHandle beforeHandle(&before);

    p.Connect(bucket, 0, addTen, 0);
    p.Connect(bucket, 1, addHundred, 0);
    p.Connect(addTen, 0, &nowHandle, 0);
    p.Connect(addHundred, 0, &beforeHandle, 0);

    // Echo mode, so outlet 0 is the value that just arrived and outlet 1 is the
    // one before it.
    bucket->SetIntData(0, 1);
    CHECK(now.received == doctest::Approx(11.f));
    CHECK(before.received == doctest::Approx(100.f)); // nothing before it yet

    bucket->SetIntData(0, 2);
    CHECK(now.received == doctest::Approx(12.f));
    CHECK(before.received == doctest::Approx(101.f));

    bucket->SetIntData(0, 5);
    CHECK(now.received == doctest::Approx(15.f));
    CHECK(before.received == doctest::Approx(102.f));

    // And one list pushes the whole phrase through in a single message, leaving
    // the last two values of it in the register.
    bucket->SetListData(0, "7 8");
    CHECK(now.received == doctest::Approx(18.f));
    CHECK(before.received == doctest::Approx(107.f));
  }

} // TEST_SUITE("patcher")

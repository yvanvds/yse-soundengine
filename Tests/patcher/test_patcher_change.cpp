// Tests for .change (issue #468) — pass a number on only when it differs.
//
// The object's summary is one line and its behaviour is not, so this file is
// organised around the five questions "emit when it differs" leaves open. Each
// gets a section, and each rule that a plausible implementation would get wrong
// gets its own case:
//
//   - **where the stored value starts.** Max: "If there is no argument, the
//     initial value is 0", so the *first* message is compared against 0 rather
//     than passed through for want of a previous value. A bare .change sent 0
//     first emits nothing; sent 5 first it emits 5. The "first value always
//     passes" reading is the natural one and it is wrong, and it would make the
//     creation argument — the whole point of .change 5 — decoration.
//   - **equality is exact.** No tolerance, because a suppressor with a
//     tolerance swallows values that genuinely differ, and swallowing is
//     invisible. Pinned with two numbers a float can tell apart and nothing
//     coarser would.
//   - **a non-finite input is ignored**, not read as 0 (which would emit a zero
//     the patch never sent and then swallow the next real one) and not stored
//     raw (a NaN differs from everything including itself, so a NaN stream
//     would emit on every message — the exact flood the object prevents). The
//     invariant the tests hold the object to is that the stored value is
//     *always finite*.
//   - **`set` does not emit.** If it did it would be a spelling of sending the
//     number at the inlet and would have no reason to exist. Asserted both
//     ways: nothing comes out, and the store actually happened.
//   - **symbols and lists.** Max's change has no symbol method and no list
//     method; a list is Max's single-inlet distribution (first element, rest
//     dropped) and a symbol is an ignored message.
//
// Plus the two outlets the one-line summary does not mention — Max's
// zero/non-zero edge reports, which are .togedge (#469) built in — their
// right-to-left firing order, the modes, and the JSON round trip.
//
// No audio device required.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/gChange.h"
#include "sinks.hpp"

namespace {

  using YSE::PATCHER::gChange;
  using Mode = gChange::Mode;

  constexpr float INF = std::numeric_limits<float>::infinity();
  const float NOT_A_NUMBER = std::numeric_limits<float>::quiet_NaN();

  // All three outlets, each with its own tag, all logging into one vector so a
  // test can read back the exact firing sequence rather than a set of counts.
  // The right-to-left guarantee is only visible that way.
  struct Rig {
    std::unique_ptr<gChange> op;
    TestHelpers::OrderSink value; // outlet 0 — the number
    TestHelpers::OrderSink rising; // outlet 1 — stored was 0, input is not
    TestHelpers::OrderSink falling; // outlet 2 — stored was not 0, input is
    std::vector<char> log;

    Rig() : op(new gChange()) {
      Wire(value, 0, 'v');
      Wire(rising, 1, 'r');
      Wire(falling, 2, 'f');
    }

    // Creation argument, applied the way a saved patch applies it.
    explicit Rig(const std::string& args) : Rig() {
      op->SetParams(args);
    }

    void Send(float v) {
      op->GetInlet(0)->SetFloat(v, YSE::T_GUI);
    }

    void SendInt(int v) {
      op->GetInlet(0)->SetInt(v, YSE::T_GUI);
    }

    void List(const std::string& text) {
      op->GetInlet(0)->SetList(text, YSE::T_GUI);
    }

    int Values() const {
      return value.count;
    }

    int Risings() const {
      return rising.count;
    }

    int Fallings() const {
      return falling.count;
    }

    // Everything the object has sent, in order. 'v' / 'r' / 'f' per outlet.
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

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("change: creatable through the registry with one inlet and three outlets (#468)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_CHANGE);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".change"));
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 3);
    // Outlet 0 stays a float in every mode: an outlet's type is part of the
    // object's shape and cannot depend on a `mode` message arriving later.
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::INT);
    CHECK(h->OutputDataType(2) == YSE::OUT_TYPE::INT);
  }

  TEST_CASE("change: listed by pRegistry::AllNames (#468)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".change")) != names.end());
  }

  // ─── where the stored value starts ──────────────────────────────────────────
  //
  // The single most surprising thing about the object, and the one a "there is
  // no previous value, so let the first one through" implementation gets wrong.

  // Max: "If there is no argument, the initial value is 0." So the first
  // message is compared against a real 0, not against "nothing".
  TEST_CASE("change: a bare object starts holding 0, so its first 0 is swallowed (#468)") {
    Rig rig;
    CHECK(rig.op->Stored() == 0.f);

    rig.Send(0.f);
    CHECK(rig.Values() == 0);
    // Not a rising edge and not a falling one either: 0 after 0 is neither.
    CHECK(rig.Risings() == 0);
    CHECK(rig.Fallings() == 0);
  }

  TEST_CASE("change: a bare object passes its first non-zero value (#468)") {
    Rig rig;
    rig.Send(5.f);
    REQUIRE(rig.Values() == 1);
    CHECK(rig.value.lastFloat == 5.f);
    CHECK(rig.op->Stored() == 5.f);
  }

  // The mirror image, and what the creation argument is *for*: .change 5
  // declares what the patch already believes the value to be, so loading a
  // patch fires nothing until the value actually moves.
  TEST_CASE("change: the creation argument seeds the stored value (#468)") {
    Rig rig("5");
    CHECK(rig.op->Stored() == 5.f);
    CHECK(rig.op->Initial() == 5.f);

    rig.Send(5.f);
    CHECK(rig.Values() == 0);

    rig.Send(0.f);
    REQUIRE(rig.Values() == 1);
    CHECK(rig.value.lastFloat == 0.f);
  }

  // A creation argument that is not a whole finite number is not a value. This
  // is what keeps the stored value finite without a substitution that would
  // invent a 0 nobody wrote.
  TEST_CASE("change: a non-finite or partial creation argument leaves the object at 0 (#468)") {
    for (const char* args : {"inf", "-inf", "nan", "5abc", "wobble", "1e999"}) {
      CAPTURE(args);
      Rig rig(args);
      CHECK(rig.op->Initial() == 0.f);
      CHECK(rig.op->Stored() == 0.f);
      CHECK(std::isfinite(rig.op->Stored()));
    }
  }

  TEST_CASE("change: a negative creation argument is a value, not a mode flag (#468)") {
    Rig rig("-5");
    CHECK(rig.op->Initial() == -5.f);
    CHECK(rig.op->InitialMode() == Mode::DIFFERENT);
  }

  // ─── the suppression itself ─────────────────────────────────────────────────

  TEST_CASE("change: repeats are swallowed and changes pass (#468)") {
    Rig rig;
    rig.Send(5.f);
    rig.Send(5.f);
    rig.Send(5.f);
    CHECK(rig.Values() == 1);

    rig.Send(6.f);
    CHECK(rig.Values() == 2);
    CHECK(rig.value.lastFloat == 6.f);

    rig.Send(6.f);
    CHECK(rig.Values() == 2);
  }

  // The stored value follows every input, so a value that returns to one it
  // held earlier is still a change.
  TEST_CASE("change: an alternating stream passes every message (#468)") {
    Rig rig;
    rig.Send(1.f);
    rig.Send(2.f);
    rig.Send(1.f);
    rig.Send(2.f);
    CHECK(rig.Values() == 4);
  }

  TEST_CASE("change: ints and floats share one stored value (#468)") {
    Rig rig;
    rig.SendInt(5);
    CHECK(rig.Values() == 1);
    // '.change 5' and '.change 5.0' are the same parameter string here, so the
    // int 5 and the float 5.0 have to be the same number to the object too.
    rig.Send(5.f);
    CHECK(rig.Values() == 1);
    rig.SendInt(5);
    CHECK(rig.Values() == 1);
  }

  // ─── exact equality ─────────────────────────────────────────────────────────

  // A tolerance would make this object swallow a value that genuinely differs,
  // and swallowing is invisible — the failure has no symptom to trace.
  TEST_CASE("change: the comparison is exact, with no tolerance (#468)") {
    Rig rig;
    rig.Send(1.f);
    CHECK(rig.Values() == 1);

    // One ULP-ish apart at this magnitude, and far below any tolerance an
    // object would plausibly pick.
    rig.Send(1.0000001f);
    if (1.0000001f != 1.f) {
      CHECK(rig.Values() == 2);
    }

    rig.Send(1.001f);
    CHECK(rig.value.lastFloat == 1.001f);
  }

  // -0.f == 0.f in IEEE-754, and they are the same number. This is also the
  // corner the shared ZeroToNonZero / NonZeroToZero predicates exist to keep
  // .change and .togedge (#469) agreeing about.
  TEST_CASE("change: negative zero is not a change from zero (#468)") {
    Rig rig;
    rig.Send(5.f);
    rig.Send(0.f);
    REQUIRE(rig.Values() == 2);

    // Cleared so the log below covers this message alone — the counters are
    // cumulative and the 0 -> 5 above legitimately raised them.
    rig.Clear();
    rig.Send(-0.f);
    // Not a change, and not an edge either: -0.f is zero on both sides of the
    // comparison, so nothing fires at all.
    CHECK(rig.Order().empty());
    CHECK(rig.Values() == 2);
  }

  // ─── non-finite input ───────────────────────────────────────────────────────
  //
  // The family's usual "read a non-finite as 0" rule is wrong for a comparator,
  // exactly as it is for .sel — and here both of the obvious answers are wrong,
  // so the object refuses the message instead.

  TEST_CASE("change: a non-finite input emits nothing and leaves the stored value alone (#468)") {
    for (float bad : {NOT_A_NUMBER, INF, -INF}) {
      CAPTURE(bad);
      Rig rig;
      rig.Send(7.f);
      REQUIRE(rig.Values() == 1);
      // The 0 -> 7 above is a real rising edge, so the log rather than the
      // cumulative counters is what says "this message fired nothing".
      rig.Clear();

      rig.Send(bad);
      CHECK(rig.Order().empty());
      CHECK(rig.Values() == 1);
      CHECK(rig.op->Stored() == 7.f);
      CHECK(std::isfinite(rig.op->Stored()));
    }
  }

  // If a NaN were read as 0, this would emit a 0 the patch never sent and then
  // swallow the next real one. Both halves are checked, because only the second
  // distinguishes "ignored" from "read as 0 but coincidentally silent".
  TEST_CASE("change: a NaN is not read as 0, so the next real 0 still passes (#468)") {
    Rig rig;
    rig.Send(3.f);
    rig.Clear();

    rig.Send(NOT_A_NUMBER);
    CHECK(rig.Order().empty());

    rig.Send(0.f);
    CHECK(rig.value.lastFloat == 0.f);
    CHECK(rig.Order() == "fv");
  }

  // A stored NaN differs from everything including itself, so storing one raw
  // would turn a repeated NaN into an emission per message — the exact
  // repetition flood the object exists to stop.
  TEST_CASE("change: a stream of NaNs does not flood the outlet (#468)") {
    Rig rig;
    for (int i = 0; i < 20; i++)
      rig.Send(NOT_A_NUMBER);
    CHECK(rig.Values() == 0);
    CHECK(std::isfinite(rig.op->Stored()));
  }

  // ─── the zero / non-zero edge outlets ───────────────────────────────────────
  //
  // Max's middle and right outlets, which are .togedge (#469) built into the
  // object and which the one-line summary does not mention at all.

  TEST_CASE("change: outlet 1 reports the rising zero crossing (#468)") {
    Rig rig;
    rig.Send(0.f);
    CHECK(rig.Risings() == 0);

    rig.Send(4.f);
    REQUIRE(rig.Risings() == 1);
    CHECK(rig.rising.lastInt == 1);
    CHECK(rig.Fallings() == 0);

    // Still non-zero: not another crossing.
    rig.Send(9.f);
    CHECK(rig.Risings() == 1);
  }

  TEST_CASE("change: outlet 2 reports the falling zero crossing (#468)") {
    Rig rig;
    rig.Send(4.f);
    CHECK(rig.Fallings() == 0);

    rig.Send(0.f);
    REQUIRE(rig.Fallings() == 1);
    CHECK(rig.falling.lastInt == 1);
    CHECK(rig.Risings() == 1); // 0 -> 4 earlier

    rig.Send(0.f);
    CHECK(rig.Fallings() == 1);
  }

  // Both are evaluated against the value held *before* this input — Max: "If
  // the stored value is 0 and the input is not 0" — so the store has to happen
  // after they are computed.
  TEST_CASE("change: an edge outlet never fires on a repetition (#468)") {
    Rig rig;
    rig.Send(5.f);
    rig.Send(5.f);
    rig.Send(5.f);
    CHECK(rig.Risings() == 1); // only the first, 0 -> 5
    CHECK(rig.Fallings() == 0);

    rig.Send(0.f);
    rig.Send(0.f);
    CHECK(rig.Fallings() == 1);
  }

  // The order .mean, .cartopol and .peak already send in, so whatever the value
  // on outlet 0 triggers downstream already sees the matching edge report
  // rather than the previous message's.
  TEST_CASE("change: the three outlets fire right to left (#468)") {
    Rig rig;
    rig.Send(4.f); // 0 -> 4: rising, then the value
    CHECK(rig.Order() == "rv");

    rig.Clear();
    rig.Send(0.f); // 4 -> 0: falling, then the value
    CHECK(rig.Order() == "fv");

    rig.Clear();
    rig.Send(0.f); // nothing at all
    CHECK(rig.Order().empty());

    rig.Clear();
    rig.Send(1.f);
    rig.Send(2.f); // non-zero to non-zero: the value alone
    CHECK(rig.Order() == "rvv");
  }

  // ─── set ────────────────────────────────────────────────────────────────────

  // The whole reason the message exists: if it emitted it would be a spelling
  // of sending the number at the inlet.
  TEST_CASE("change: set stores without emitting anything (#468)") {
    Rig rig;
    rig.List("set 5");
    CHECK(rig.Order().empty());
    CHECK(rig.op->Stored() == 5.f);
  }

  // Asserted through the outlet as well as the accessor, because "nothing came
  // out" alone cannot tell a store that happened from a message that was
  // dropped on the floor.
  TEST_CASE("change: a value set silently is then treated as a repetition (#468)") {
    Rig rig;
    rig.List("set 5");
    rig.Send(5.f);
    CHECK(rig.Values() == 0);

    rig.Send(6.f);
    REQUIRE(rig.Values() == 1);
    CHECK(rig.value.lastFloat == 6.f);
  }

  TEST_CASE("change: set moves the object across a zero crossing silently (#468)") {
    Rig rig;
    rig.List("set 5");
    CHECK(rig.Order().empty());
    // The next 0 is a falling edge because the *stored* value moved, even
    // though nothing was ever emitted for the move itself.
    rig.Send(0.f);
    CHECK(rig.Order() == "fv");
  }

  // MatchWord requires the separator, so a longer word starting with "set" is
  // not this message.
  TEST_CASE("change: a malformed set keeps the value it had (#468)") {
    for (const char* message :
         {"set", "set wobble", "set 5abc", "set nan", "set inf", "settle 3"}) {
      CAPTURE(message);
      Rig rig("9");
      rig.List(message);
      CHECK(rig.op->Stored() == 9.f);
      CHECK(rig.Order().empty());
    }
  }

  // ─── the modes ──────────────────────────────────────────────────────────────

  TEST_CASE("change: mode + sends 1 only when the number is greater (#468)") {
    Rig rig;
    rig.List("mode +");
    CHECK(rig.op->CurrentMode() == Mode::GREATER);

    rig.Send(5.f);
    REQUIRE(rig.Values() == 1);
    CHECK(rig.value.lastFloat == 1.f);

    rig.Send(5.f);
    CHECK(rig.Values() == 1);

    rig.Send(3.f);
    CHECK(rig.Values() == 1);

    rig.Send(4.f);
    CHECK(rig.Values() == 2);
    CHECK(rig.value.lastFloat == 1.f);
  }

  TEST_CASE("change: mode - sends -1 only when the number is less (#468)") {
    Rig rig("10");
    rig.List("mode -");
    CHECK(rig.op->CurrentMode() == Mode::LESS);

    rig.Send(12.f);
    CHECK(rig.Values() == 0);

    rig.Send(8.f);
    REQUIRE(rig.Values() == 1);
    CHECK(rig.value.lastFloat == -1.f);
  }

  // "the previously received number", not "the last number emitted" — the
  // store is unconditional in every mode. 5, 3, 4 therefore emits on the 4,
  // because 4 beats the 3 the object actually received.
  TEST_CASE("change: every mode stores every number it receives (#468)") {
    Rig rig;
    rig.List("mode +");
    rig.Send(5.f);
    rig.Clear();

    rig.Send(3.f);
    CHECK(rig.Values() == 1); // nothing new: only the 5 above
    CHECK(rig.op->Stored() == 3.f);

    rig.Send(4.f);
    CHECK(rig.Values() == 2);
  }

  TEST_CASE("change: a bare mode message returns to the default (#468)") {
    Rig rig;
    rig.List("mode +");
    REQUIRE(rig.op->CurrentMode() == Mode::GREATER);

    rig.List("mode");
    CHECK(rig.op->CurrentMode() == Mode::DIFFERENT);

    rig.Send(5.f);
    rig.Send(3.f); // lower, so '+' would have stayed silent
    CHECK(rig.Values() == 2);
    CHECK(rig.value.lastFloat == 3.f);
  }

  // A message that cannot be understood should not silently reconfigure the
  // object — the answer .peak gives a malformed `set`.
  TEST_CASE("change: an unknown mode flag leaves the mode alone (#468)") {
    for (const char* message : {"mode x", "mode ++", "mode 5", "modest +"}) {
      CAPTURE(message);
      Rig rig;
      rig.List("mode +");
      rig.List(message);
      CHECK(rig.op->CurrentMode() == Mode::GREATER);
    }
  }

  TEST_CASE("change: the mode is settable from the creation argument (#468)") {
    Rig plus("5 +");
    CHECK(plus.op->Initial() == 5.f);
    CHECK(plus.op->InitialMode() == Mode::GREATER);
    CHECK(plus.op->CurrentMode() == Mode::GREATER);

    Rig minus("5 -");
    CHECK(minus.op->InitialMode() == Mode::LESS);
  }

  // `.change +` is a legal Max object whose only argument is the mode, which is
  // why the tokens are scanned rather than taken positionally — and why the
  // creation arguments do not go through Parameters' float slot, whose
  // std::stof would throw on this one.
  TEST_CASE("change: a mode-only creation argument works and leaves the value at 0 (#468)") {
    Rig rig("+");
    CHECK(rig.op->Initial() == 0.f);
    CHECK(rig.op->InitialMode() == Mode::GREATER);

    rig.Send(3.f);
    REQUIRE(rig.Values() == 1);
    CHECK(rig.value.lastFloat == 1.f);
  }

  // Max's Output section is not mode-qualified, and the zero-crossing question
  // is orthogonal to which comparison outlet 0 is making.
  TEST_CASE("change: the edge outlets still fire in the + and - modes (#468)") {
    Rig rig;
    rig.List("mode -");
    rig.Send(4.f); // not less than 0, so outlet 0 is silent
    CHECK(rig.Values() == 0);
    // ...but 0 -> 4 is still a rising zero crossing.
    CHECK(rig.Risings() == 1);

    rig.Send(0.f); // less than 4, so outlet 0 speaks, and it is a falling edge
    CHECK(rig.Values() == 1);
    CHECK(rig.Fallings() == 1);
  }

  // ─── reset ──────────────────────────────────────────────────────────────────

  TEST_CASE("change: reset returns to the creation value and mode, silently (#468)") {
    Rig rig("7 +");
    rig.List("set 100");
    rig.List("mode -");
    REQUIRE(rig.op->Stored() == 100.f);
    REQUIRE(rig.op->CurrentMode() == Mode::LESS);

    rig.List("reset");
    CHECK(rig.op->Stored() == 7.f);
    CHECK(rig.op->CurrentMode() == Mode::GREATER);
    CHECK(rig.Order().empty());
  }

  // ─── symbols and lists ──────────────────────────────────────────────────────

  // Max's change has no symbol or `anything` method. Two of its three outlets
  // are numeric predicates and two of its three modes are orderings, none of
  // which a symbol has, so a message that is neither a word this object knows
  // nor a list beginning with a number is ignored rather than guessed at.
  TEST_CASE("change: a symbol is ignored rather than compared or read as 0 (#468)") {
    for (const char* message : {"hello", "wobble bar", "", "   ", "+", "-"}) {
      CAPTURE(message);
      Rig rig("3");
      rig.List(message);
      CHECK(rig.Order().empty());
      CHECK(rig.op->Stored() == 3.f);
    }
  }

  TEST_CASE("change: repeated symbols do not emit even once (#468)") {
    Rig rig;
    rig.List("hello");
    rig.List("hello");
    rig.List("world");
    CHECK(rig.Order().empty());
  }

  // Max's single-inlet distribution: the first element goes to the int / float
  // method and the rest is dropped.
  TEST_CASE("change: a list is reduced to its first number (#468)") {
    Rig rig;
    rig.List("5 6");
    REQUIRE(rig.Values() == 1);
    CHECK(rig.value.lastFloat == 5.f);
    CHECK(rig.op->Stored() == 5.f);

    // The 6 was dropped, so the same list again is a repetition.
    rig.List("5 6");
    CHECK(rig.Values() == 1);

    // ...and one with a different *first* element is not.
    rig.List("6 5");
    CHECK(rig.Values() == 2);
    CHECK(rig.value.lastFloat == 6.f);
  }

  TEST_CASE("change: a list whose first element is not a number is ignored (#468)") {
    Rig rig("3");
    rig.List("wobble 5");
    CHECK(rig.Order().empty());
    CHECK(rig.op->Stored() == 3.f);
  }

  TEST_CASE("change: a list whose first element is non-finite is ignored (#468)") {
    for (const char* message : {"nan 5", "inf", "-inf 2"}) {
      CAPTURE(message);
      Rig rig("3");
      rig.List(message);
      CHECK(rig.Order().empty());
      CHECK(rig.op->Stored() == 3.f);
      CHECK(std::isfinite(rig.op->Stored()));
    }
  }

  // A bang carries no number, so there is no honest answer: outlet 0 means
  // "this number differs from the last one".
  TEST_CASE("change: a bang is not accepted (#468)") {
    Rig rig("3");
    rig.op->GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Order().empty());
    CHECK(rig.op->Stored() == 3.f);
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  TEST_CASE("change: survives a DumpJSON / ParseJSON round trip (#468)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_CHANGE);
    REQUIRE(h != nullptr);
    h->SetParams("12.5 -");
    const std::string json = src.DumpJSON();
    CHECK(json.find(".change") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".change"));
    CHECK(copy->GetInputs() == 1);
    CHECK(copy->GetOutputs() == 3);
    CHECK(copy->GetParams() == std::string("12.5 -"));
  }

  // SetParams("") takes the clear callback and never reaches the parse one, so
  // the clear path has to leave a usable object rather than one still holding
  // the previous creation argument.
  TEST_CASE("change: clearing the parameters returns the object to a bare .change (#468)") {
    Rig rig("42 +");
    REQUIRE(rig.op->Initial() == 42.f);

    rig.op->SetParams("");
    CHECK(rig.op->Initial() == 0.f);
    CHECK(rig.op->Stored() == 0.f);
    CHECK(rig.op->CurrentMode() == Mode::DIFFERENT);
    CHECK(rig.op->InitialMode() == Mode::DIFFERENT);
  }

  TEST_CASE("change: the GUI value reports the stored value (#468)") {
    Rig rig;
    rig.Send(5.f);
    CHECK(rig.op->GetGuiValue().find('5') != std::string::npos);
    rig.List("set 12");
    CHECK(rig.op->GetGuiValue().find("12") != std::string::npos);
  }

  // ─── the DSP tick ───────────────────────────────────────────────────────────

  // A hot inlet fires CalculateIfReady() after every message it accepts, so a
  // Calculate() that emitted would re-send the last value on every tick —
  // precisely the repetition flood this object exists to prevent — and would
  // make `set`, `mode` and `reset` emit too.
  TEST_CASE("change: Calculate does nothing (#468)") {
    Rig rig;
    rig.Send(5.f);
    rig.Clear();

    for (int i = 0; i < 10; i++)
      rig.op->Calculate(YSE::T_GUI);

    CHECK(rig.Order().empty());
    CHECK(rig.op->Stored() == 5.f);
  }

} // TEST_SUITE

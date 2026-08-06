// Tests for .togedge (issue #469) — bang on a zero crossing, in either
// direction.
//
// The object reports *transitions*, never levels, and almost every way of
// getting it wrong produces an object that still looks right on a 0, 5, 0
// stream. So this file is organised around the rules that separate the correct
// object from the plausible ones:
//
//   - **only zero crossings fire.** 5 then 9 is not an event and 0 then 0 is
//     not an event — Max: "Otherwise, togedge sends no output". An
//     implementation that reported "the value changed" rather than "the value
//     crossed zero" passes a 0, 5, 0 test and bangs on every message of a
//     rising envelope.
//   - **the outlets strictly alternate.** They are mutually exclusive, so at
//     most one fires per message and never the same one twice running. This is
//     the property a downstream note-on / note-off pairing depends on — two
//     note-ons with no note-off between them is a stuck note — so it is
//     asserted over whole streams and not just message by message.
//   - **where the object starts.** Holding 0, with no creation argument (Max:
//     "Arguments: None"), so a first non-zero number is a rising edge and a
//     first 0 is silent. .change's `initial` argument deliberately does not
//     transfer.
//   - **a bang toggles and reports.** Max's most surprising message: it
//     switches the stored value and bangs the outlet for the transition it just
//     made itself, so bangs alternate the outlets. It shares the stored value
//     with the numeric path rather than living beside it.
//   - **a non-finite input is ignored**, not read as 0 — which would bang the
//     falling outlet for a transition the patch never sent, i.e. a note-off for
//     a note still held — and not stored raw, which would leave the object in a
//     state neither predicate could fire from again. The invariant is that the
//     stored value is *always finite*.
//   - **`reset` is silent**, which is its whole reason for existing: a patch
//     that wants the gate re-armed cannot just send 0.
//   - **symbols and lists**, as .change handles them.
//
// Plus the payload difference from .change (bangs here, int 1 there — one Max
// reference page each), the shared negative-zero corner, and the JSON round
// trip.
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
#include "patcher/math/gTogEdge.h"
#include "sinks.hpp"

namespace {

  using YSE::PATCHER::gTogEdge;

  constexpr float INF = std::numeric_limits<float>::infinity();
  const float NOT_A_NUMBER = std::numeric_limits<float>::quiet_NaN();

  // Both outlets, each with its own tag, logging into one vector so a test can
  // read back the exact firing sequence. The strict alternation is only visible
  // that way: OrderSink::count is *cumulative*, so counters alone cannot say
  // which message a bang belonged to, and cannot distinguish "left, right" from
  // "left, left".
  struct Rig {
    std::unique_ptr<gTogEdge> op;
    TestHelpers::OrderSink rising; // outlet 0 — stored was 0, input is not
    TestHelpers::OrderSink falling; // outlet 1 — stored was not 0, input is
    std::vector<char> log;

    Rig() : op(new gTogEdge()) {
      Wire(rising, 0, 'r');
      Wire(falling, 1, 'f');
    }

    void Send(float v) {
      op->GetInlet(0)->SetFloat(v, YSE::T_GUI);
    }

    void SendInt(int v) {
      op->GetInlet(0)->SetInt(v, YSE::T_GUI);
    }

    void Bang() {
      op->GetInlet(0)->SetBang(YSE::T_GUI);
    }

    void List(const std::string& text) {
      op->GetInlet(0)->SetList(text, YSE::T_GUI);
    }

    // Everything the object has sent, in order. 'r' / 'f' per outlet.
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

  // True when no outlet fired twice in a row — the invariant a note-on /
  // note-off pairing depends on. Also rejects a message that fired both.
  bool Alternates(const std::string& order) {
    for (std::size_t i = 1; i < order.size(); i++) {
      if (order[i] == order[i - 1]) return false;
    }
    return true;
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("togedge: creatable through the registry with one inlet and two bang outlets (#469)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_TOGEDGE);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".togedge"));
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 2);
    // Bangs, not .change's int 1. One Max reference page each, and the header
    // sets out why the difference is deliberate: there is no value outlet here
    // for an int to be distinguished from, and a stray 1 would be a value
    // nobody asked for on an outlet meant to drive a .trigger or a message box.
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BANG);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::BANG);
  }

  TEST_CASE("togedge: listed by pRegistry::AllNames (#469)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".togedge")) != names.end());
  }

  // ─── where the object starts ────────────────────────────────────────────────

  // Max: "Arguments: None." The object starts holding 0, so a first 0 is a
  // repetition rather than a falling edge.
  TEST_CASE("togedge: a fresh object holds 0, so its first 0 is silent (#469)") {
    Rig rig;
    CHECK(rig.op->Stored() == 0.f);
    CHECK_FALSE(rig.op->IsHigh());

    rig.Send(0.f);
    CHECK(rig.Order().empty());
    CHECK_FALSE(rig.op->IsHigh());
  }

  TEST_CASE("togedge: the first non-zero value is a rising edge (#469)") {
    Rig rig;
    rig.Send(5.f);
    CHECK(rig.Order() == "r");
    CHECK(rig.rising.lastKind == TestHelpers::OrderSink::BANG);
    CHECK(rig.op->Stored() == 5.f);
    CHECK(rig.op->IsHigh());
  }

  // .change's `initial` creation argument exists to stop a patch *load* from
  // firing a value outlet for a parameter that has not moved. This object has
  // no value outlet, so Max gives it no argument ("Arguments: None") and
  // neither do we. Asserted through the documented parameter surface, which is
  // what a binding or an editor reads to decide the object takes no argument.
  TEST_CASE("togedge: documents no creation parameter at all (#469)") {
    Rig rig;
    CHECK(rig.op->GetParamDocs().empty());
    CHECK(rig.op->GetParams().empty());
  }

  // Clearing the parameters is the one argument string this object can be
  // handed today, and it must leave a usable object rather than disturbing the
  // state.
  //
  // The mirror case — a *non-empty* stray argument, which should be ignored —
  // is deliberately not asserted here: it currently segfaults in
  // Parameters::Set, which reads `parms.back()` on an empty vector for any
  // object that registers no parameters. That is a pre-existing engine defect
  // rather than anything about this object (`.mean` and the whole trigonometric
  // family crash the same way, and it takes down `patcher::ParseJSON` for a
  // patch file carrying such an argument), so it is filed as #627 and left to
  // be pinned by the test that fixes it.
  TEST_CASE("togedge: clearing the parameters leaves a usable object (#469)") {
    Rig rig;
    rig.Send(5.f);
    rig.Clear();

    rig.op->SetParams("");
    CHECK(rig.Order().empty());

    // Still fully functional, and still on the side it was left on: there is no
    // creation argument for a re-parse to restore it to.
    rig.Send(0.f);
    CHECK(rig.Order() == "f");
  }

  // ─── only zero crossings fire ───────────────────────────────────────────────
  //
  // The rule that separates this object from "report when the value changed",
  // which passes a naive 0, 5, 0 test and then bangs on every message of a
  // rising envelope.

  TEST_CASE("togedge: a change between two non-zero values is not an event (#469)") {
    Rig rig;
    rig.Send(5.f);
    REQUIRE(rig.Order() == "r");
    rig.Clear();

    rig.Send(9.f);
    rig.Send(1.f);
    rig.Send(-4.f);
    rig.Send(0.001f);
    CHECK(rig.Order().empty());
    // The stored value still follows every input, even though none of them was
    // an event.
    CHECK(rig.op->Stored() == 0.001f);
  }

  TEST_CASE("togedge: a repeated zero is not an event (#469)") {
    Rig rig;
    rig.Send(5.f);
    rig.Send(0.f);
    REQUIRE(rig.Order() == "rf");
    rig.Clear();

    rig.Send(0.f);
    rig.Send(0.f);
    CHECK(rig.Order().empty());
  }

  TEST_CASE("togedge: a repeated non-zero value is not an event (#469)") {
    Rig rig;
    rig.Send(5.f);
    rig.Clear();
    rig.Send(5.f);
    rig.Send(5.f);
    CHECK(rig.Order().empty());
  }

  TEST_CASE("togedge: the falling edge bangs the right outlet (#469)") {
    Rig rig;
    rig.Send(4.f);
    rig.Clear();

    rig.Send(0.f);
    CHECK(rig.Order() == "f");
    CHECK(rig.falling.lastKind == TestHelpers::OrderSink::BANG);
    CHECK_FALSE(rig.op->IsHigh());
  }

  // A negative number is non-zero, so it opens the gate exactly as a positive
  // one does. The object is about zero-ness, not about sign.
  TEST_CASE("togedge: a negative value is non-zero and opens the gate (#469)") {
    Rig rig;
    rig.Send(-3.f);
    CHECK(rig.Order() == "r");
    rig.Clear();
    rig.Send(-7.f); // still non-zero
    CHECK(rig.Order().empty());
    rig.Send(0.f);
    CHECK(rig.Order() == "f");
  }

  // ─── strict alternation ─────────────────────────────────────────────────────

  TEST_CASE("togedge: exactly one outlet fires per message, or neither (#469)") {
    Rig rig;
    for (float v : {5.f, 9.f, 0.f, 0.f, 1.f, 2.f, 0.f, -1.f, 0.f}) {
      CAPTURE(v);
      rig.Clear();
      rig.Send(v);
      CHECK(rig.log.size() <= 1);
    }
  }

  // The invariant a note-on / note-off pairing depends on, over a whole stream
  // rather than message by message.
  TEST_CASE("togedge: the two outlets alternate over an arbitrary stream (#469)") {
    Rig rig;
    const float stream[] = {0.f, 1.f, 2.f, 0.f, 0.f, -5.f, 3.f, 0.f, 1.f, 1.f, 0.f, 0.f, 8.f};
    for (float v : stream)
      rig.Send(v);

    CHECK(Alternates(rig.Order()));
    // ...and it really did fire, so the check above is not vacuous.
    CHECK(rig.Order() == "rfrfrfr");
  }

  // An envelope: rises through many values, sits at the top, falls back. Two
  // events for the whole ramp, which is the reason the object exists.
  TEST_CASE("togedge: an envelope produces exactly two events (#469)") {
    Rig rig;
    for (int i = 1; i <= 50; i++)
      rig.Send(static_cast<float>(i) / 50.f);
    for (int i = 50; i >= 1; i--)
      rig.Send(static_cast<float>(i) / 50.f);
    rig.Send(0.f);

    CHECK(rig.Order() == "rf");
  }

  // ─── the shared negative-zero corner ────────────────────────────────────────

  // -0.f == 0.f in IEEE-754, so a negative zero is zero on both sides. This is
  // the corner the shared ZeroToNonZero / NonZeroToZero predicates in gChange.h
  // exist to keep .change and .togedge agreeing about.
  TEST_CASE("togedge: negative zero counts as zero on the falling side (#469)") {
    Rig rig;
    rig.Send(5.f);
    rig.Clear();

    rig.Send(-0.f);
    CHECK(rig.Order() == "f");
    CHECK_FALSE(rig.op->IsHigh());
  }

  TEST_CASE("togedge: negative zero counts as zero on the rising side too (#469)") {
    Rig rig;
    rig.Send(-0.f); // still zero, so not an event at all
    CHECK(rig.Order().empty());

    rig.Send(2.f); // -0 -> 2 is a rising edge
    CHECK(rig.Order() == "r");
  }

  // ─── ints and floats ────────────────────────────────────────────────────────

  TEST_CASE("togedge: ints and floats share one stored value (#469)") {
    Rig rig;
    rig.SendInt(1);
    CHECK(rig.Order() == "r");
    rig.Clear();

    rig.Send(1.f); // same side
    CHECK(rig.Order().empty());

    rig.SendInt(0);
    CHECK(rig.Order() == "f");
  }

  // Max documents an int method only, so a float would be truncated there and
  // 0.5 would be a *zero*. Not reproduced: that would make an envelope between
  // 0 and 1 read as permanently zero except at full scale, and the gate would
  // never open — the object's headline use case, broken outright.
  TEST_CASE("togedge: a fractional value below 1 is non-zero, not truncated (#469)") {
    for (float v : {0.5f, 0.001f, -0.25f}) {
      CAPTURE(v);
      Rig rig;
      rig.Send(v);
      CHECK(rig.Order() == "r");
      CHECK(rig.op->IsHigh());
    }
  }

  // ─── the bang inlet ─────────────────────────────────────────────────────────
  //
  // Max: "Switches the value stored in togedge from 0 to non-zero, or vice
  // versa, and reports the change by sending a bang out one of the outlets."

  TEST_CASE("togedge: a bang from the low state toggles high and bangs left (#469)") {
    Rig rig;
    rig.Bang();
    CHECK(rig.Order() == "r");
    CHECK(rig.op->IsHigh());
  }

  TEST_CASE("togedge: a bang from the high state toggles low and bangs right (#469)") {
    Rig rig;
    rig.Send(5.f);
    rig.Clear();

    rig.Bang();
    CHECK(rig.Order() == "f");
    CHECK_FALSE(rig.op->IsHigh());
    CHECK(rig.op->Stored() == 0.f);
  }

  // "Outlets alternate output when bangs are received" — the description's own
  // words, and the reason a single button can drive a note-on / note-off pair.
  TEST_CASE("togedge: a stream of bangs alternates the outlets (#469)") {
    Rig rig;
    for (int i = 0; i < 6; i++)
      rig.Bang();
    CHECK(rig.Order() == "rfrfrf");
    CHECK(Alternates(rig.Order()));
  }

  // A bang is never silent — the one member of the family for which that is
  // true, because it reports the transition it made itself.
  TEST_CASE("togedge: every bang emits exactly one message (#469)") {
    Rig rig;
    for (int i = 0; i < 10; i++) {
      CAPTURE(i);
      rig.Clear();
      rig.Bang();
      CHECK(rig.log.size() == 1);
    }
  }

  // The toggle goes through the same stored value as the numeric path rather
  // than living beside it, so the two compose.
  TEST_CASE("togedge: bangs and numbers share one stored value (#469)") {
    Rig rig;
    rig.Bang(); // 0 -> 1, rising
    rig.Clear();

    // Already high, so a number on the same side is silent.
    rig.Send(7.f);
    CHECK(rig.Order().empty());

    // ...and the bang now falls from 7 rather than from the 1 it stored itself.
    rig.Bang();
    CHECK(rig.Order() == "f");
    CHECK(rig.op->Stored() == 0.f);
  }

  TEST_CASE("togedge: a bang after a number continues the alternation (#469)") {
    Rig rig;
    rig.Send(3.f); // r
    rig.Bang(); // f
    rig.Bang(); // r
    rig.Send(0.f); // f
    rig.Send(0.f); // nothing
    rig.Bang(); // r
    CHECK(rig.Order() == "rfrfr");
    CHECK(Alternates(rig.Order()));
  }

  // ─── non-finite input ───────────────────────────────────────────────────────
  //
  // The family's "read a non-finite as 0" rule is wrong here in a way that is
  // worse than usual: a NaN read as 0 while the object is high would report a
  // falling edge, i.e. a note-off for a note still being held.

  TEST_CASE("togedge: a non-finite input emits nothing and leaves the value alone (#469)") {
    for (float bad : {NOT_A_NUMBER, INF, -INF}) {
      CAPTURE(bad);
      Rig rig;
      rig.Send(7.f);
      REQUIRE(rig.Order() == "r");
      rig.Clear();

      rig.Send(bad);
      CHECK(rig.Order().empty());
      CHECK(rig.op->Stored() == 7.f);
      CHECK(rig.op->IsHigh());
      CHECK(std::isfinite(rig.op->Stored()));
    }
  }

  // Both halves matter: only the second distinguishes "ignored" from "read as 0
  // but coincidentally silent". If the NaN had been read as 0, the falling edge
  // would already have fired and the genuine 0 below would then be silent.
  TEST_CASE("togedge: a NaN is not read as 0, so the next real 0 still falls (#469)") {
    Rig rig;
    rig.Send(3.f);
    rig.Clear();

    rig.Send(NOT_A_NUMBER);
    CHECK(rig.Order().empty());

    rig.Send(0.f);
    CHECK(rig.Order() == "f");
  }

  // A non-finite input while the object is *low* must not fabricate a rising
  // edge either — the mirror of the case above.
  TEST_CASE("togedge: a non-finite input from the low state does not open the gate (#469)") {
    for (float bad : {NOT_A_NUMBER, INF, -INF}) {
      CAPTURE(bad);
      Rig rig;
      rig.Send(bad);
      CHECK(rig.Order().empty());
      CHECK_FALSE(rig.op->IsHigh());

      // ...and the object is still armed, so a real number still rises.
      rig.Send(1.f);
      CHECK(rig.Order() == "r");
    }
  }

  // Storing a NaN raw would leave the object in a state from which neither
  // predicate could ever fire again: a NaN is neither == 0 nor usefully != 0.
  TEST_CASE("togedge: a stream of NaNs neither floods nor wedges the object (#469)") {
    Rig rig;
    for (int i = 0; i < 20; i++)
      rig.Send(NOT_A_NUMBER);
    CHECK(rig.Order().empty());
    CHECK(std::isfinite(rig.op->Stored()));

    // Still fully functional afterwards.
    rig.Send(1.f);
    rig.Send(0.f);
    CHECK(rig.Order() == "rf");
  }

  // ─── reset ──────────────────────────────────────────────────────────────────

  // The silence is the whole reason the message exists: a patch that wants the
  // gate re-armed cannot just send 0, because sending 0 bangs the falling
  // outlet and delivers a note-off the patch did not mean.
  TEST_CASE("togedge: reset returns to the low state silently (#469)") {
    Rig rig;
    rig.Send(5.f);
    rig.Clear();

    rig.List("reset");
    CHECK(rig.Order().empty());
    CHECK(rig.op->Stored() == 0.f);
    CHECK_FALSE(rig.op->IsHigh());
  }

  // Asserted through the outlet as well as the accessor, because "nothing came
  // out" alone cannot tell a store that happened from a message dropped on the
  // floor.
  TEST_CASE("togedge: after reset the next non-zero value is a rising edge again (#469)") {
    Rig rig;
    rig.Send(5.f);
    rig.List("reset");
    rig.Clear();

    rig.Send(5.f);
    CHECK(rig.Order() == "r");
  }

  TEST_CASE("togedge: reset from the low state is a no-op and still silent (#469)") {
    Rig rig;
    rig.List("reset");
    CHECK(rig.Order().empty());
    CHECK_FALSE(rig.op->IsHigh());
  }

  // Max documents no `set`, so it is not invented — the word is just a symbol
  // and a symbol is ignored.
  TEST_CASE("togedge: there is no set message, and it is ignored rather than guessed at (#469)") {
    for (const char* message : {"set 5", "set 0", "set", "reset 5", "resetting"}) {
      CAPTURE(message);
      Rig rig;
      rig.Send(9.f);
      rig.Clear();
      rig.List(message);
      CHECK(rig.Order().empty());
      // Unchanged: none of these is a message this object knows, and `reset 5`
      // is not a bare `reset`.
      CHECK(rig.op->Stored() == 9.f);
    }
  }

  // ─── symbols and lists ──────────────────────────────────────────────────────

  // Zero-ness is a numeric predicate and a symbol is neither zero nor non-zero,
  // so there is no honest edge to report.
  TEST_CASE("togedge: a symbol is ignored rather than read as 0 (#469)") {
    for (const char* message : {"hello", "wobble bar", "", "   ", "+", "-"}) {
      CAPTURE(message);
      Rig rig;
      rig.Send(3.f);
      rig.Clear();
      rig.List(message);
      CHECK(rig.Order().empty());
      CHECK(rig.op->Stored() == 3.f);
    }
  }

  TEST_CASE("togedge: repeated symbols do not emit even once (#469)") {
    Rig rig;
    rig.List("hello");
    rig.List("hello");
    rig.List("world");
    CHECK(rig.Order().empty());
  }

  // Max's single-inlet distribution: the first element goes to the int method
  // and the rest is dropped.
  TEST_CASE("togedge: a list is reduced to its first number (#469)") {
    Rig rig;
    rig.List("5 6");
    CHECK(rig.Order() == "r");
    CHECK(rig.op->Stored() == 5.f);
    rig.Clear();

    // The 6 was dropped, so this is still the same side.
    rig.List("7 0");
    CHECK(rig.Order().empty());

    // ...and a leading 0 falls, whatever follows it.
    rig.List("0 9");
    CHECK(rig.Order() == "f");
  }

  TEST_CASE("togedge: a list whose first element is not a number is ignored (#469)") {
    Rig rig;
    rig.Send(3.f);
    rig.Clear();
    rig.List("wobble 5");
    CHECK(rig.Order().empty());
    CHECK(rig.op->Stored() == 3.f);
  }

  TEST_CASE("togedge: a list whose first element is non-finite is ignored (#469)") {
    for (const char* message : {"nan 5", "inf", "-inf 2"}) {
      CAPTURE(message);
      Rig rig;
      rig.Send(3.f);
      rig.Clear();
      rig.List(message);
      CHECK(rig.Order().empty());
      CHECK(rig.op->Stored() == 3.f);
      CHECK(std::isfinite(rig.op->Stored()));
    }
  }

  TEST_CASE("togedge: a list is read through the same path as a bare number (#469)") {
    Rig rig;
    rig.List("2");
    CHECK(rig.Order() == "r");
    rig.Clear();
    rig.List("0");
    CHECK(rig.Order() == "f");
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  TEST_CASE("togedge: survives a DumpJSON / ParseJSON round trip (#469)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_TOGEDGE);
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".togedge") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".togedge"));
    CHECK(copy->GetInputs() == 1);
    CHECK(copy->GetOutputs() == 2);
    // No creation argument, as in Max.
    CHECK(copy->GetParams().empty());
  }

  TEST_CASE("togedge: the GUI value reports the zero/non-zero state (#469)") {
    Rig rig;
    CHECK(rig.op->GetGuiValue() == std::string("0"));
    rig.Send(5.f);
    CHECK(rig.op->GetGuiValue() == std::string("1"));
    rig.Send(0.f);
    CHECK(rig.op->GetGuiValue() == std::string("0"));
    rig.Bang();
    CHECK(rig.op->GetGuiValue() == std::string("1"));
  }

  // ─── the DSP tick ───────────────────────────────────────────────────────────

  // A hot inlet fires CalculateIfReady() after every message it accepts, so a
  // Calculate() that emitted would make `reset` emit too and would re-bang on
  // every tick, breaking the strict alternation that is the object's whole
  // contract.
  TEST_CASE("togedge: Calculate does nothing (#469)") {
    Rig rig;
    rig.Send(5.f);
    rig.Clear();

    for (int i = 0; i < 10; i++)
      rig.op->Calculate(YSE::T_GUI);

    CHECK(rig.Order().empty());
    CHECK(rig.op->Stored() == 5.f);
    CHECK(rig.op->IsHigh());
  }

} // TEST_SUITE

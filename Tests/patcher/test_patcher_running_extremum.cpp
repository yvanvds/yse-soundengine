// Tests for the running-extreme pair (issue #463): .peak and .trough
//
// Two inlets and three outlets each: the extreme on outlet 0, Max's
// "was this a new one" flag on outlet 1 and its inverse on outlet 2.
//
// The pair is a mirror image, so everything here is asserted for *both*
// objects rather than for one with the other assumed. Where the assertion is
// direction-independent (a rejection is silent on the value outlet; the flags
// fire either way; `reset` returns to the creation argument) the test loops
// over the two through the shared gRunningExtremumBase; where the numbers
// differ the two cases are written out, since a test that computed its own
// expectation from the same comparator it is testing would pass against a
// comparator that is simply wrong.
//
// The decisions this file exists to pin, none of which the Max page states
// outright:
//
//   - **the object stores what it emits.** This is the entire difference from
//     .maximum / .minimum (#462), whose comparand is compared and forgotten.
//     Confusing the two turns a peak-hold into a clamp, or a clamp into a latch
//     that can never come back down, and both failures are silent.
//   - **a rejected number produces nothing on the value outlet** but still
//     reports on both flag outlets. "Emit only on change" is what the object is
//     for; the flags are how a patch learns about the messages it did not get.
//   - **what a bang reports before any input** — the creation argument, which
//     is 0 for .peak and Max's 128 for .trough, and not silence.
//   - **reset / set are silent, the cold inlet is not.** Max gives a running
//     extreme no way back that does not announce itself; the two messages are
//     the way back, and their being silent is the whole reason they exist next
//     to inlet 1.
//   - **a list is Max's two-element idiom**, not a reduction — the opposite of
//     what the same-looking list does to a .maximum.
//   - **non-finite input reads as 0**, which matters more here than in #462
//     because a stored NaN or infinity would never be displaced.
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
#include "patcher/math/gRunningExtremum.h"

namespace {

  using YSE::PATCHER::gPeak;
  using YSE::PATCHER::gRunningExtremumBase;
  using YSE::PATCHER::gTrough;

  constexpr float INF = std::numeric_limits<float>::infinity();
  const float NOT_A_NUMBER = std::numeric_limits<float>::quiet_NaN();

  // Records *when* it was hit as well as what it received, so both "this
  // message is silent" and the right-to-left outlet order can be asserted
  // rather than assumed. One type serves all three outlets: the extreme
  // arrives as a float, the two flags as ints.
  struct OrderSink : YSE::PATCHER::pObject {
    std::vector<char>* log = nullptr;
    char tag = '?';
    float lastFloat = 0.f;
    int lastInt = -1;
    int hits = 0;

    OrderSink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterFloat([this](float v, int, YSE::THREAD) {
        lastFloat = v;
        hits++;
        if (log) log->push_back(tag);
      });
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) {
        lastInt = v;
        hits++;
        if (log) log->push_back(tag);
      });
    }
    const char* Type() const override {
      return "running_extremum_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // One object wired to all three outlets, plus the ways a patch drives it.
  // Holds the object through the shared base so a single rig serves both
  // directions.
  struct Rig {
    std::unique_ptr<gRunningExtremumBase> op;
    OrderSink value; // outlet 0 — the extreme
    OrderSink isNew; // outlet 1 — 1 when the number was a new extreme
    OrderSink isSame; // outlet 2 — the inverse
    std::vector<char> order;

    explicit Rig(gRunningExtremumBase* object) : op(object) {
      value.tag = 'v';
      isNew.tag = 'n';
      isSame.tag = 's';
      value.log = &order;
      isNew.log = &order;
      isSame.log = &order;

      op->ConnectOutlet(value.GetInlet(0), 0);
      value.ConnectInlet(op->GetOutlet(0), 0);
      op->ConnectOutlet(isNew.GetInlet(0), 1);
      isNew.ConnectInlet(op->GetOutlet(1), 0);
      op->ConnectOutlet(isSame.GetInlet(0), 2);
      isSame.ConnectInlet(op->GetOutlet(2), 0);
    }

    // Creation argument, applied the way a saved patch applies it.
    Rig(gRunningExtremumBase* object, const std::string& args) : Rig(object) {
      op->SetParams(args);
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

    void Seed(float v) {
      op->GetInlet(1)->SetFloat(v, YSE::T_GUI);
    }

    void SeedInt(int v) {
      op->GetInlet(1)->SetInt(v, YSE::T_GUI);
    }

    // How many numbers the *value* outlet has carried. The count that matters:
    // "emit only when the record moves" is the object.
    int Emitted() const {
      return value.hits;
    }

    std::string Order() const {
      return std::string(order.begin(), order.end());
    }

    void Forget() {
      order.clear();
    }
  };

  std::vector<const char*> BothNames() {
    return {YSE::OBJ::G_PEAK, YSE::OBJ::G_TROUGH};
  }

  gRunningExtremumBase* Make(const char* name) {
    if (std::string(name) == YSE::OBJ::G_PEAK) return new gPeak();
    return new gTrough();
  }

  // Where each direction starts when it is created without an argument: Max's
  // documented defaults, and deliberately not the same number.
  float DefaultStart(const char* name) {
    return std::string(name) == YSE::OBJ::G_PEAK ? 0.f : 128.f;
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("running extremum: both objects are creatable through the registry (#463)") {
    YSE::patcher p;
    p.create(2);
    for (const char* name : BothNames()) {
      CAPTURE(name);
      YSE::pHandle* h = p.CreateObject(name);
      REQUIRE(h != nullptr);
      CHECK(std::string(h->Type()) == std::string(name));
      CHECK(h->GetInputs() == 2);
      CHECK(h->GetOutputs() == 3);
      CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);
      // The two flags carry nothing but 0 and 1, so they are int outlets.
      CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::INT);
      CHECK(h->OutputDataType(2) == YSE::OUT_TYPE::INT);
    }
  }

  TEST_CASE("running extremum: both objects are listed by pRegistry::AllNames (#463)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".peak")) != names.end());
    CHECK(std::find(names.begin(), names.end(), std::string(".trough")) != names.end());
  }

  // ─── the running comparison ─────────────────────────────────────────────────

  TEST_CASE("peak: only numbers greater than the stored peak come out (#463)") {
    Rig rig(new gPeak());
    // Max: "Compares a number to a previous peak-value and, if larger, it is
    // sent out the output while the new peak-value is set to that number."
    rig.Send(5.f);
    CHECK(rig.value.lastFloat == doctest::Approx(5.f));
    CHECK(rig.Emitted() == 1);

    rig.Send(3.f); // loses
    CHECK(rig.Emitted() == 1);
    CHECK(rig.value.lastFloat == doctest::Approx(5.f)); // outlet did not move

    rig.Send(9.f);
    CHECK(rig.Emitted() == 2);
    CHECK(rig.value.lastFloat == doctest::Approx(9.f));
    CHECK(rig.op->Extreme() == doctest::Approx(9.f));
  }

  TEST_CASE("trough: only numbers less than the stored minimum come out (#463)") {
    Rig rig(new gTrough(), "10");
    rig.Send(5.f);
    CHECK(rig.value.lastFloat == doctest::Approx(5.f));
    CHECK(rig.Emitted() == 1);

    rig.Send(7.f); // loses
    CHECK(rig.Emitted() == 1);
    CHECK(rig.value.lastFloat == doctest::Approx(5.f));

    rig.Send(-2.f);
    CHECK(rig.Emitted() == 2);
    CHECK(rig.value.lastFloat == doctest::Approx(-2.f));
    CHECK(rig.op->Extreme() == doctest::Approx(-2.f));
  }

  // The distinction that decides what the object *is*. .maximum compares its
  // input against a sticky comparand and forgets it; .peak keeps it. If this
  // regressed to .maximum's behaviour the second Send below would emit again,
  // and a peak-hold display would flicker instead of holding.
  TEST_CASE("running extremum: the object stores what it emits (#463)") {
    Rig hi(new gPeak());
    hi.Send(100.f);
    CHECK(hi.op->Extreme() == doctest::Approx(100.f)); // not left behind
    hi.Send(50.f);
    CHECK(hi.Emitted() == 1); // 50 is not a new peak, so nothing came out
    hi.Send(100.f);
    CHECK(hi.Emitted() == 1); // and neither is a tie with the record

    Rig lo(new gTrough());
    lo.Send(-100.f);
    CHECK(lo.op->Extreme() == doctest::Approx(-100.f));
    lo.Send(-50.f);
    CHECK(lo.Emitted() == 1);
    lo.Send(-100.f);
    CHECK(lo.Emitted() == 1);
  }

  TEST_CASE("running extremum: a tie is not a new extreme (#463)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name), "5");
      rig.Send(5.f);
      // The outlet would carry the number it already carried, and the flag
      // would call an unchanged record a new one.
      CHECK(rig.Emitted() == 0);
      CHECK(rig.isNew.lastInt == 0);
      CHECK(rig.op->Extreme() == doctest::Approx(5.f));
    }
  }

  TEST_CASE("running extremum: an int on inlet 0 behaves exactly like the float (#463)") {
    Rig hi(new gPeak());
    hi.SendInt(9);
    CHECK(hi.value.lastFloat == doctest::Approx(9.f));
    hi.SendInt(1);
    CHECK(hi.Emitted() == 1);

    Rig lo(new gTrough());
    lo.SendInt(9);
    CHECK(lo.value.lastFloat == doctest::Approx(9.f));
    lo.SendInt(100);
    CHECK(lo.Emitted() == 1);
  }

  TEST_CASE("running extremum: negative and fractional numbers compare as numbers (#463)") {
    Rig hi(new gPeak(), "-5");
    hi.Send(-7.25f);
    CHECK(hi.Emitted() == 0);
    hi.Send(-4.75f);
    CHECK(hi.value.lastFloat == doctest::Approx(-4.75f));

    Rig lo(new gTrough(), "-2.5");
    lo.Send(-1.25f);
    CHECK(lo.Emitted() == 0);
    lo.Send(-2.75f);
    CHECK(lo.value.lastFloat == doctest::Approx(-2.75f));
  }

  // ─── the two flag outlets ───────────────────────────────────────────────────

  TEST_CASE("running extremum: the flags report every number, new or not (#463)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name), "0");
      // Whatever the direction, one of these two is a record and the other is
      // not, so both branches are exercised for both objects.
      rig.Send(-1.f);
      rig.Send(1.f);

      CHECK(rig.isNew.hits == 2);
      CHECK(rig.isSame.hits == 2);
      // Exactly one of the two numbers moved the record.
      CHECK(rig.Emitted() == 1);
      // And the two flags never agree.
      CHECK(rig.isNew.lastInt != rig.isSame.lastInt);
    }
  }

  TEST_CASE("running extremum: the flags carry 1/0 for a record and 0/1 for a rejection (#463)") {
    Rig rig(new gPeak());
    rig.Send(5.f);
    CHECK(rig.isNew.lastInt == 1);
    CHECK(rig.isSame.lastInt == 0);

    rig.Send(1.f);
    CHECK(rig.isNew.lastInt == 0);
    CHECK(rig.isSame.lastInt == 1);
  }

  // Right to left, as .mean and .cartopol already send, so anything the value
  // outlet triggers already holds the matching flags rather than the previous
  // message's.
  TEST_CASE("running extremum: the outlets are sent right to left (#463)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name), "0");

      rig.Send(-1.f);
      const std::string first = rig.Order();
      rig.Forget();
      rig.Send(1.f);
      const std::string second = rig.Order();

      // One of the two was a record — that one sent all three, flags first;
      // the other sent only the flags, in the same order.
      const std::string record = (first.size() == 3) ? first : second;
      const std::string reject = (first.size() == 3) ? second : first;
      CHECK(record == "snv");
      CHECK(reject == "sn");
    }
  }

  // ─── bang ───────────────────────────────────────────────────────────────────

  TEST_CASE("running extremum: a bang sends the stored extreme and nothing else (#463)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name), "4");
      rig.Forget();

      rig.Bang();
      // Max: "Sends the currently stored peak value out the left outlet." A
      // bang receives no number, so the flags have nothing to answer.
      CHECK(rig.Order() == "v");
      CHECK(rig.value.lastFloat == doctest::Approx(4.f));
      CHECK(rig.isNew.hits == 0);
      CHECK(rig.isSame.hits == 0);

      // A report, not a comparison: it does not move the record.
      CHECK(rig.op->Extreme() == doctest::Approx(4.f));
    }
  }

  TEST_CASE("running extremum: a bang before any input reports where the object started (#463)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name));
      rig.Bang();
      // Not silence: a patch that bangs its objects once on load has to get an
      // answer. Max's defaults, and deliberately different per direction.
      CHECK(rig.Emitted() == 1);
      CHECK(rig.value.lastFloat == doctest::Approx(DefaultStart(name)));
      CHECK(std::isfinite(rig.value.lastFloat));
    }
  }

  // The asymmetry is Max's and is not an oversight: a trough starting at 0
  // could never be beaten by a positive number, so it would never emit at all.
  TEST_CASE("running extremum: peak starts at 0 and trough at 128 (#463)") {
    Rig hi(new gPeak());
    CHECK(hi.op->Extreme() == doctest::Approx(0.f));
    CHECK(hi.op->Initial() == doctest::Approx(0.f));

    Rig lo(new gTrough());
    CHECK(lo.op->Extreme() == doctest::Approx(128.f));
    CHECK(lo.op->Initial() == doctest::Approx(128.f));
    // Which is what makes a bare .trough usable: an ordinary MIDI-range or
    // 0-to-1 value beats it.
    lo.Send(64.f);
    CHECK(lo.Emitted() == 1);
    CHECK(lo.value.lastFloat == doctest::Approx(64.f));
  }

  // ─── the creation argument ──────────────────────────────────────────────────

  TEST_CASE("peak: the initial argument is where the peak starts (#463)") {
    Rig rig(new gPeak(), "10");
    CHECK(rig.op->Extreme() == doctest::Approx(10.f));
    rig.Send(4.f);
    CHECK(rig.Emitted() == 0);
    rig.Send(40.f);
    CHECK(rig.value.lastFloat == doctest::Approx(40.f));
  }

  TEST_CASE("trough: the initial argument is where the minimum starts (#463)") {
    Rig rig(new gTrough(), "10");
    CHECK(rig.op->Extreme() == doctest::Approx(10.f));
    rig.Send(40.f);
    CHECK(rig.Emitted() == 0);
    rig.Send(4.f);
    CHECK(rig.value.lastFloat == doctest::Approx(4.f));
  }

  TEST_CASE("running extremum: a fractional argument is kept as a float (#463)") {
    Rig rig(new gPeak(), "2.5");
    CHECK(rig.op->Extreme() == doctest::Approx(2.5f));
    rig.Send(2.25f);
    CHECK(rig.Emitted() == 0);
    rig.Send(2.75f);
    CHECK(rig.value.lastFloat == doctest::Approx(2.75f));
  }

  // Max's int variant would round every comparison; this port deliberately does
  // not, since `.peak 5` and `.peak 5.0` are the same parameter string here.
  TEST_CASE("running extremum: an integral argument does not make the object integral (#463)") {
    Rig hi(new gPeak(), "5");
    hi.Send(5.5f);
    CHECK(hi.value.lastFloat == doctest::Approx(5.5f)); // not 5, and not 6

    Rig lo(new gTrough(), "5");
    lo.Send(4.5f);
    CHECK(lo.value.lastFloat == doctest::Approx(4.5f));
  }

  // ─── inlet 1: reseed *and* announce ─────────────────────────────────────────

  TEST_CASE("running extremum: inlet 1 stores the extreme and sends it out (#463)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name), "0");
      rig.Forget();

      // Max: "The number is stored in peak as the new peak value, and is sent
      // out", with "(A number received in the right inlet is always the new
      // peak value.)" settling the flags. Note this is the opposite of
      // .maximum's cold inlet, which is silent.
      rig.Seed(42.f);
      CHECK(rig.op->Extreme() == doctest::Approx(42.f));
      CHECK(rig.Emitted() == 1);
      CHECK(rig.value.lastFloat == doctest::Approx(42.f));
      CHECK(rig.isNew.lastInt == 1);
      CHECK(rig.isSame.lastInt == 0);
      CHECK(rig.Order() == "snv");
    }
  }

  // "Always the new peak value" means always — even when the number would have
  // lost on inlet 0. That is what makes inlet 1 a reseed rather than a second
  // comparison.
  TEST_CASE("running extremum: inlet 1 reseeds even backwards (#463)") {
    Rig hi(new gPeak());
    hi.Send(100.f);
    hi.Seed(1.f); // would have lost on inlet 0
    CHECK(hi.op->Extreme() == doctest::Approx(1.f));
    CHECK(hi.value.lastFloat == doctest::Approx(1.f));
    // And the record really did come down, so 50 is a peak again.
    hi.Send(50.f);
    CHECK(hi.value.lastFloat == doctest::Approx(50.f));

    Rig lo(new gTrough());
    lo.Send(-100.f);
    lo.Seed(1.f);
    CHECK(lo.op->Extreme() == doctest::Approx(1.f));
    lo.Send(-50.f);
    CHECK(lo.value.lastFloat == doctest::Approx(-50.f));
  }

  TEST_CASE("running extremum: an int on inlet 1 behaves like the float (#463)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name));
      rig.SeedInt(7);
      CHECK(rig.op->Extreme() == doctest::Approx(7.f));
      CHECK(rig.value.lastFloat == doctest::Approx(7.f));
    }
  }

  TEST_CASE("running extremum: inlet 1 ignores bangs and lists (#463)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name), "4");
      rig.Forget();

      // Only inlet 0 registers them, so this asserts the registration as much
      // as the handler guard; either way nothing may change or come out.
      rig.op->GetInlet(1)->SetBang(YSE::T_GUI);
      rig.op->GetInlet(1)->SetList("1 2 3", YSE::T_GUI);
      CHECK(rig.Order().empty());
      CHECK(rig.op->Extreme() == doctest::Approx(4.f));
    }
  }

  // ─── reset and set: the silent way back ─────────────────────────────────────

  TEST_CASE("running extremum: reset returns to the creation argument, silently (#463)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name), "6");
      rig.Seed(-3.f);
      rig.Forget();

      // Silent is the point: the cold inlet already covers "reseed and
      // announce", so the message worth having is the one that lets the *next*
      // number be the next event.
      rig.List("reset");
      CHECK(rig.Order().empty());
      CHECK(rig.op->Extreme() == doctest::Approx(6.f));
      CHECK(rig.op->Initial() == doctest::Approx(6.f));
    }
  }

  TEST_CASE("running extremum: after a reset the next number is a new extreme again (#463)") {
    Rig hi(new gPeak(), "0");
    hi.Send(100.f);
    hi.Send(50.f);
    CHECK(hi.Emitted() == 1);
    hi.List("reset");
    hi.Send(50.f); // beats 0 again
    CHECK(hi.Emitted() == 2);
    CHECK(hi.value.lastFloat == doctest::Approx(50.f));

    Rig lo(new gTrough(), "100");
    lo.Send(1.f);
    lo.Send(50.f);
    CHECK(lo.Emitted() == 1);
    lo.List("reset");
    lo.Send(50.f);
    CHECK(lo.Emitted() == 2);
    CHECK(lo.value.lastFloat == doctest::Approx(50.f));
  }

  TEST_CASE("running extremum: 'set <n>' stores without emitting (#463)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name), "0");
      rig.Forget();

      rig.List("set 12.5");
      CHECK(rig.Order().empty());
      CHECK(rig.op->Extreme() == doctest::Approx(12.5f));
      // The stored value is real, not just recorded: a bang reports it.
      rig.Bang();
      CHECK(rig.value.lastFloat == doctest::Approx(12.5f));
    }
  }

  TEST_CASE("running extremum: a malformed 'set' is ignored rather than storing 0 (#463)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name), "6");
      rig.Forget();

      rig.List("set");
      rig.List("set wobble");
      CHECK(rig.Order().empty());
      CHECK(rig.op->Extreme() == doctest::Approx(6.f));
    }
  }

  // The word has to end where it ends. A bare prefix test would read this as
  // `set 3` and store 3 silently; instead the shared list reader steps over the
  // word it cannot parse and the 3 is offered like any other number.
  TEST_CASE("running extremum: 'settle 3' is not 'set 3' (#463)") {
    Rig rig(new gPeak(), "0");
    rig.List("settle 3");
    CHECK(rig.Emitted() == 1); // offered and won, rather than stored silently
    CHECK(rig.value.lastFloat == doctest::Approx(3.f));
  }

  // Deliberately absent, for .accum's reason: on a `.peak 5` the word could
  // mean either 0 or 5, and `reset` already says which one it means.
  TEST_CASE("running extremum: 'clear' is not a message this object knows (#463)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name), "6");
      rig.Seed(-3.f);
      rig.Forget();

      rig.List("clear");
      CHECK(rig.Order().empty());
      CHECK(rig.op->Extreme() == doctest::Approx(-3.f)); // untouched
    }
  }

  // ─── lists ──────────────────────────────────────────────────────────────────

  TEST_CASE("running extremum: a two-element list reseeds then offers (#463)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name), "0");
      rig.Forget();

      // Max: "The second number is stored as the new peak value and is sent
      // out, then the first number is received in the left inlet." Max's
      // standard right-to-left inlet fill, written out because this patcher
      // does not distribute lists across inlets on its own.
      rig.List("50 100");
      // The reseed came out first — flags then value, a full "snv" — and only
      // then was the offered number judged against it, which is why exactly two
      // numbers were acted on. (For .peak the 50 then loses and for .trough it
      // wins, so where the extreme ends up is asserted per direction below.)
      CHECK(rig.Order().substr(0, 3) == "snv");
      CHECK(rig.isNew.hits == 2);
      // Either way the extreme came from the list rather than staying at the 0
      // the object started from.
      CHECK(rig.op->Extreme() != doctest::Approx(0.f));
    }
  }

  TEST_CASE("peak: a two-element list emits the reseed and rejects a smaller first number (#463)") {
    Rig rig(new gPeak());
    rig.List("50 100");
    CHECK(rig.Emitted() == 1); // only the reseed
    CHECK(rig.value.lastFloat == doctest::Approx(100.f));
    CHECK(rig.op->Extreme() == doctest::Approx(100.f));
  }

  TEST_CASE(
      "trough: a two-element list emits the reseed and then the smaller first number (#463)") {
    Rig rig(new gTrough());
    rig.List("50 100");
    CHECK(rig.Emitted() == 2); // the reseed to 100, then 50 beating it
    CHECK(rig.value.lastFloat == doctest::Approx(50.f));
    CHECK(rig.op->Extreme() == doctest::Approx(50.f));
  }

  // This is NOT .maximum's list, which reduces the whole list against itself
  // and stores the runner-up. Same message text, entirely different object.
  TEST_CASE("running extremum: a list is not a reduction (#463)") {
    Rig rig(new gPeak());
    rig.List("3 9");
    // A reduction would leave 9 with 3 as a runner-up; this leaves 9 because 9
    // was the *second* item, not because it was the largest.
    CHECK(rig.op->Extreme() == doctest::Approx(9.f));

    Rig other(new gPeak());
    other.List("9 3");
    // Reversed, a reduction would still say 9. The two-element idiom says 3 was
    // stored first and then 9 beat it.
    CHECK(other.op->Extreme() == doctest::Approx(9.f));
    CHECK(other.Emitted() == 2); // the reseed to 3, then 9 as a new peak
  }

  TEST_CASE("running extremum: numbers past the second in a list are ignored (#463)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name), "0");
      // Max documents exactly two arguments. A 9999 that reached the object
      // would be a record for .peak and a -9999 one for .trough.
      rig.List("1 2 9999 -9999");
      CHECK(rig.op->Extreme() != doctest::Approx(9999.f));
      CHECK(rig.op->Extreme() != doctest::Approx(-9999.f));
      CHECK(rig.isNew.hits == 2); // exactly two numbers were acted on
    }
  }

  TEST_CASE("running extremum: a one-element list is a plain number (#463)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      // Max routes it to the int / float method, so it is offered rather than
      // stored.
      Rig scalar(Make(name), "5");
      scalar.Send(8.f);

      Rig listed(Make(name), "5");
      listed.List("8");
      CHECK(listed.Emitted() == scalar.Emitted());
      CHECK(listed.op->Extreme() == doctest::Approx(scalar.op->Extreme()));
      CHECK(listed.Order() == scalar.Order());
    }
  }

  TEST_CASE("running extremum: a message with no numbers in it is ignored (#463)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name), "6");
      rig.Forget();

      rig.List("wobble");
      rig.List("");
      rig.List("   ");
      CHECK(rig.Order().empty());
      CHECK(rig.op->Extreme() == doctest::Approx(6.f));
    }
  }

  // ─── non-finite input ───────────────────────────────────────────────────────
  // A NaN compares false against everything, so a stored one would lose every
  // later comparison and be reported forever; a stored infinity would either
  // win every comparison (silencing the outlet for good) or lose every one. All
  // read as 0 instead — the convention ./ , .sqrt, .zmap, .clip, .slide, .mean,
  // .accum and .maximum already use. It matters more here than in #462 because
  // this object *keeps* what it accepts.

  TEST_CASE("running extremum: a non-finite number on inlet 0 reads as 0 (#463)") {
    Rig hi(new gPeak(), "-5");
    hi.Send(NOT_A_NUMBER);
    CHECK(hi.value.lastFloat == doctest::Approx(0.f));
    CHECK(hi.op->Extreme() == doctest::Approx(0.f));
    hi.Send(INF);
    CHECK(hi.op->Extreme() == doctest::Approx(0.f)); // a tie with the 0, not a record
    hi.Send(-INF);
    CHECK(std::isfinite(hi.op->Extreme()));

    Rig lo(new gTrough(), "5");
    lo.Send(NOT_A_NUMBER);
    CHECK(lo.value.lastFloat == doctest::Approx(0.f));
    CHECK(lo.op->Extreme() == doctest::Approx(0.f));
    lo.Send(-INF);
    CHECK(lo.op->Extreme() == doctest::Approx(0.f));
    CHECK(std::isfinite(lo.op->Extreme()));
  }

  TEST_CASE("running extremum: a non-finite number on inlet 1 reads as 0 (#463)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name));
      rig.Seed(INF);
      CHECK(rig.op->Extreme() == 0.f);
      rig.Seed(NOT_A_NUMBER);
      CHECK(rig.op->Extreme() == 0.f);
      // And the object still compares, which a stored infinity would have
      // permanently prevented in one direction or the other.
      rig.Send(-1.f);
      rig.Send(1.f);
      CHECK(rig.Emitted() >= 2); // the two seeds; at least one number also won
      CHECK(std::isfinite(rig.op->Extreme()));
    }
  }

  TEST_CASE("running extremum: a non-finite creation argument reads as 0 (#463)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name), "inf");
      CHECK(std::isfinite(rig.op->Extreme()));
      CHECK(rig.op->Extreme() == 0.f);
      // Which is also what `reset` has to return to — an infinite starting
      // point would be unrecoverable rather than merely wrong.
      CHECK(rig.op->Initial() == 0.f);
      rig.List("reset");
      CHECK(rig.op->Extreme() == 0.f);
    }
  }

  TEST_CASE("running extremum: nothing the outlets ever carry is non-finite (#463)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      Rig rig(Make(name), "nan");
      rig.Seed(INF);
      rig.Send(-INF);
      rig.List("inf nan");
      rig.List("set inf");
      rig.Bang();
      CHECK(std::isfinite(rig.value.lastFloat));
      CHECK(std::isfinite(rig.op->Extreme()));
      CHECK(std::isfinite(rig.op->Initial()));
    }
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  TEST_CASE("running extremum: survives a DumpJSON / ParseJSON round trip (#463)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      YSE::patcher src;
      src.create(2);
      YSE::pHandle* h = src.CreateObject(name);
      REQUIRE(h != nullptr);
      h->SetParams("12.5");
      const std::string json = src.DumpJSON();
      CHECK(json.find(name) != std::string::npos);

      YSE::patcher loaded;
      loaded.create(2);
      loaded.ParseJSON(json);
      REQUIRE(loaded.Objects() == 1);

      YSE::pHandle* copy = loaded.GetHandleFromList(0);
      REQUIRE(copy != nullptr);
      CHECK(std::string(copy->Type()) == std::string(name));
      CHECK(copy->GetInputs() == 2);
      CHECK(copy->GetOutputs() == 3);
      CHECK(copy->GetParams() == std::string("12.5"));
    }
  }

  TEST_CASE("running extremum: the GUI value reports the stored extreme (#463)") {
    Rig rig(new gPeak(), "3");
    CHECK(std::stod(rig.op->GetGuiValue()) == doctest::Approx(3.0));
    rig.Send(9.f);
    CHECK(std::stod(rig.op->GetGuiValue()) == doctest::Approx(9.0));
    rig.Send(1.f);
    CHECK(std::stod(rig.op->GetGuiValue()) == doctest::Approx(9.0)); // still the record
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category, the port shape and the two different
  // defaults, which is what a binding generator keys on.

  TEST_CASE("running extremum: both document themselves as MATH with 2 inlets, 3 outlets (#463)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(name));
      REQUIRE(obj != nullptr);
      CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MATH);
      CHECK_FALSE(obj->GetDescription().empty());
      REQUIRE(obj->NumInputs() == 2);
      CHECK(obj->GetInlet(0)->GetDocLabel() == "in");
      CHECK(obj->GetInlet(1)->GetDocLabel() == "seed");
      REQUIRE(obj->NumOutputs() == 3);
      CHECK(obj->GetOutlet(0)->GetDocLabel() == "out");
      CHECK(obj->GetOutlet(1)->GetDocLabel() == "new");
      CHECK(obj->GetOutlet(2)->GetDocLabel() == "same");
      CHECK(obj->GetOutputType(0) == YSE::OUT_TYPE::FLOAT);
      CHECK(obj->GetOutputType(1) == YSE::OUT_TYPE::INT);
      CHECK(obj->GetOutputType(2) == YSE::OUT_TYPE::INT);
      REQUIRE(obj->GetParamDocs().size() == 1);
      CHECK(obj->GetParamDocs()[0].name == "initial");
    }
  }

  TEST_CASE("running extremum: the documented defaults match where each object starts (#463)") {
    // The one place a copy-paste between the mirror halves would be invisible
    // in behaviour but wrong in the published docs.
    std::unique_ptr<YSE::PATCHER::pObject> hi(YSE::PATCHER::Register().Get(YSE::OBJ::G_PEAK));
    std::unique_ptr<YSE::PATCHER::pObject> lo(YSE::PATCHER::Register().Get(YSE::OBJ::G_TROUGH));
    REQUIRE(hi != nullptr);
    REQUIRE(lo != nullptr);
    CHECK(hi->GetParamDocs()[0].defaultValue == "0");
    CHECK(lo->GetParamDocs()[0].defaultValue == "128");
  }

  TEST_CASE("running extremum: the two descriptions name their own direction (#463)") {
    // The pair is built from one body, so a copy-paste in the doc strings is
    // the one mistake the shared implementation makes easy.
    std::unique_ptr<YSE::PATCHER::pObject> hi(YSE::PATCHER::Register().Get(YSE::OBJ::G_PEAK));
    std::unique_ptr<YSE::PATCHER::pObject> lo(YSE::PATCHER::Register().Get(YSE::OBJ::G_TROUGH));
    REQUIRE(hi != nullptr);
    REQUIRE(lo != nullptr);
    CHECK(hi->GetDescription() != lo->GetDescription());
    CHECK(hi->GetDescription().find("largest") != std::string::npos);
    CHECK(lo->GetDescription().find("smallest") != std::string::npos);
    CHECK(hi->GetOutlet(0)->GetDocDescription() != lo->GetOutlet(0)->GetDocDescription());
  }

  TEST_CASE("running extremum: inlet 0 accepts float, int, bang and list (#463)") {
    for (const char* name : BothNames()) {
      CAPTURE(name);
      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(name));
      REQUIRE(obj != nullptr);
      const unsigned int hot = obj->GetInlet(0)->GetAcceptedTypes();
      CHECK((hot & YSE::PATCHER::IT_FLOAT) != 0);
      CHECK((hot & YSE::PATCHER::IT_INT) != 0);
      CHECK((hot & YSE::PATCHER::IT_BANG) != 0);
      CHECK((hot & YSE::PATCHER::IT_LIST) != 0);

      // The cold inlet takes numbers only — it has one job.
      const unsigned int cold = obj->GetInlet(1)->GetAcceptedTypes();
      CHECK((cold & YSE::PATCHER::IT_FLOAT) != 0);
      CHECK((cold & YSE::PATCHER::IT_INT) != 0);
      CHECK((cold & YSE::PATCHER::IT_BANG) == 0);
      CHECK((cold & YSE::PATCHER::IT_LIST) == 0);
    }
  }

} // TEST_SUITE("patcher")

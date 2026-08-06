// Tests for the accumulator (issue #461): .accum
//
// Three inlets and one float outlet, in Max's layout: inlet 0 replaces the
// stored value and emits it, inlet 1 adds silently, inlet 2 multiplies
// silently. The same three operations are also reachable as messages on
// inlet 0 (`set`, `add` / `ft1`, `mult`), and `reset` returns to the creation
// argument.
//
// The weight of this file is on the two places where an accumulator is a
// design decision rather than an arithmetic operation:
//
//   - **the limits.** A multiply-accumulate diverges fast — 128 doublings
//     reach the top of the float range — so the ceiling is a case a patch
//     meets in normal use, not an exotic one. The register saturates at
//     +/-FLT_MAX; it must never become infinite (which is absorbing, so one
//     overflow would make the register permanently useless) and never a NaN
//     (which would poison every object downstream). The tests drive it there
//     through the inlets, not just through the clamp function, and then check
//     that the object still works afterwards.
//   - **what does not emit.** Exactly two things emit: a number on inlet 0 and
//     a bang. Everything else — both cold inlets and all four messages — is
//     silent, and a regression there would be invisible in a patch until
//     something downstream fired twice.
//
// Also pinned: what a bang reports before any operation, that `reset` goes
// back to the creation argument rather than to zero, how a non-finite number
// is neutralised per operation, and the double-vs-float precision the register
// is kept at.
//
// No audio device required.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/gAccum.h"

namespace {

  using YSE::PATCHER::gAccum;

  // Counts what came out as well as recording it, so "this message is silent"
  // can be asserted rather than assumed.
  struct Sink : YSE::PATCHER::pObject {
    float last = 0.f;
    int hits = 0;

    Sink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterFloat([this](float v, int, YSE::THREAD) {
        last = v;
        hits++;
      });
    }
    const char* Type() const override {
      return "accum_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // An .accum wired to its outlet, plus the ways a patch drives it.
  struct AccumRig {
    gAccum op;
    Sink sink;

    AccumRig() {
      op.ConnectOutlet(sink.GetInlet(0), 0);
      sink.ConnectInlet(op.GetOutlet(0), 0);
    }

    // Creation argument, applied the way a saved patch applies it.
    explicit AccumRig(const std::string& args) : AccumRig() {
      op.SetParams(args);
    }

    // Inlet 0: replaces and emits.
    float Send(float value) {
      op.GetInlet(0)->SetFloat(value, YSE::T_GUI);
      return sink.last;
    }

    float SendInt(int value) {
      op.GetInlet(0)->SetInt(value, YSE::T_GUI);
      return sink.last;
    }

    float Bang() {
      op.GetInlet(0)->SetBang(YSE::T_GUI);
      return sink.last;
    }

    void Message(const std::string& text) {
      op.GetInlet(0)->SetList(text, YSE::T_GUI);
    }

    // Inlet 1 adds, inlet 2 multiplies. Neither emits.
    void AddIn(float value) {
      op.GetInlet(1)->SetFloat(value, YSE::T_GUI);
    }

    void MultIn(float value) {
      op.GetInlet(2)->SetFloat(value, YSE::T_GUI);
    }

    int Emitted() const {
      return sink.hits;
    }
  };

  constexpr double LIMIT = gAccum::ACCUM_LIMIT;

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("accum: the object is creatable through the registry (#461)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ACCUM);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".accum"));
    CHECK(h->GetInputs() == 3);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);
  }

  TEST_CASE("accum: the object is listed by pRegistry::AllNames (#461)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".accum")) != names.end());
  }

  // ─── inlet 0: replace and emit ──────────────────────────────────────────────

  TEST_CASE("accum: a number on inlet 0 replaces the stored value and emits it (#461)") {
    AccumRig rig;
    // Max: "Replaces the value stored in accum, and sends the new value out the
    // outlet." Not an add — that is the middle inlet's job.
    CHECK(rig.Send(5.f) == doctest::Approx(5.f));
    CHECK(rig.Send(2.f) == doctest::Approx(2.f)); // 2, not 7
    CHECK(rig.Emitted() == 2);
    CHECK(rig.op.Value() == doctest::Approx(2.0));
  }

  TEST_CASE("accum: an int on inlet 0 behaves exactly like the float (#461)") {
    AccumRig rig;
    CHECK(rig.SendInt(9) == doctest::Approx(9.f));
    CHECK(rig.op.Value() == doctest::Approx(9.0));
  }

  TEST_CASE("accum: a bang sends the stored value without changing it (#461)") {
    AccumRig rig;
    rig.Send(4.f);
    for (int i = 0; i < 5; i++) {
      CAPTURE(i);
      CHECK(rig.Bang() == doctest::Approx(4.f));
    }
    CHECK(rig.op.Value() == doctest::Approx(4.0));
  }

  // ─── the two cold inlets ────────────────────────────────────────────────────

  TEST_CASE("accum: inlet 1 adds to the stored value without emitting (#461)") {
    AccumRig rig;
    rig.Send(10.f);
    const int before = rig.Emitted();

    rig.AddIn(5.f);
    rig.AddIn(-2.f);
    CHECK(rig.Emitted() == before); // "without triggering output"
    CHECK(rig.op.Value() == doctest::Approx(13.0));
    CHECK(rig.Bang() == doctest::Approx(13.f));
  }

  TEST_CASE("accum: inlet 2 multiplies the stored value without emitting (#461)") {
    AccumRig rig;
    rig.Send(3.f);
    const int before = rig.Emitted();

    rig.MultIn(4.f);
    rig.MultIn(0.5f);
    CHECK(rig.Emitted() == before);
    CHECK(rig.op.Value() == doctest::Approx(6.0));
    CHECK(rig.Bang() == doctest::Approx(6.f));
  }

  TEST_CASE("accum: an int on a cold inlet does the same as the float (#461)") {
    AccumRig rig;
    rig.Send(2.f);
    rig.op.GetInlet(1)->SetInt(3, YSE::T_GUI);
    rig.op.GetInlet(2)->SetInt(4, YSE::T_GUI);
    CHECK(rig.op.Value() == doctest::Approx(20.0)); // (2 + 3) * 4
  }

  // The object's reason for existing: accumulate over many messages with no
  // feedback cord, which the patcher's loop guard (#236) would refuse anyway.
  TEST_CASE("accum: a running total accumulates across messages (#461)") {
    AccumRig rig;
    for (int i = 1; i <= 100; i++)
      rig.AddIn(static_cast<float>(i));
    CHECK(rig.Bang() == doctest::Approx(5050.f)); // 100 * 101 / 2
  }

  TEST_CASE("accum: adds and multiplies interleave in the order received (#461)") {
    AccumRig rig;
    rig.Send(1.f);
    rig.AddIn(2.f); // 3
    rig.MultIn(10.f); // 30
    rig.AddIn(5.f); // 35
    rig.MultIn(2.f); // 70
    CHECK(rig.Bang() == doctest::Approx(70.f));
  }

  // ─── messages ───────────────────────────────────────────────────────────────

  TEST_CASE("accum: 'set <n>' stores without emitting (#461)") {
    AccumRig rig;
    rig.Send(1.f);
    const int before = rig.Emitted();

    // Max: "The word set, followed by a number, sets the stored value to that
    // number, without triggering output."
    rig.Message("set 42");
    CHECK(rig.Emitted() == before);
    CHECK(rig.op.Value() == doctest::Approx(42.0));
    CHECK(rig.Bang() == doctest::Approx(42.f));
  }

  TEST_CASE("accum: 'add <n>' does what inlet 1 does, silently (#461)") {
    AccumRig rig;
    rig.Message("set 10");
    const int before = rig.Emitted();

    rig.Message("add 5");
    rig.Message("add -3");
    CHECK(rig.Emitted() == before);
    CHECK(rig.op.Value() == doctest::Approx(12.0));
  }

  TEST_CASE("accum: 'ft1 <n>' is Max's spelling of the same add (#461)") {
    AccumRig rig;
    rig.Message("set 1");
    rig.Message("ft1 6");
    CHECK(rig.op.Value() == doctest::Approx(7.0));
  }

  TEST_CASE("accum: 'mult <n>' does what inlet 2 does, silently (#461)") {
    AccumRig rig;
    rig.Message("set 3");
    const int before = rig.Emitted();

    rig.Message("mult 4");
    rig.Message("mult -0.5");
    CHECK(rig.Emitted() == before);
    CHECK(rig.op.Value() == doctest::Approx(-6.0));
  }

  TEST_CASE("accum: the messages accept fractions and negatives (#461)") {
    AccumRig rig;
    rig.Message("set -1.5");
    rig.Message("add 0.25");
    CHECK(rig.op.Value() == doctest::Approx(-1.25));
    rig.Message("mult -2.5");
    CHECK(rig.op.Value() == doctest::Approx(3.125));
  }

  // ─── reset ──────────────────────────────────────────────────────────────────

  TEST_CASE("accum: 'reset' returns to the initial argument, silently (#461)") {
    AccumRig rig("7");
    REQUIRE(rig.op.Initial() == doctest::Approx(7.f));
    rig.Send(100.f);
    const int before = rig.Emitted();

    rig.Message("reset");
    CHECK(rig.Emitted() == before);
    // Back to 7, not to 0: this is what makes `reset` a different message from
    // `set 0`, and it matches .counter's reset returning to startValue.
    CHECK(rig.op.Value() == doctest::Approx(7.0));
    CHECK(rig.Bang() == doctest::Approx(7.f));
  }

  TEST_CASE("accum: without an argument 'reset' returns to 0 (#461)") {
    AccumRig rig;
    rig.Send(100.f);
    rig.Message("reset");
    CHECK(rig.op.Value() == doctest::Approx(0.0));
  }

  TEST_CASE("accum: 'clear' is not a message this object knows (#461)") {
    AccumRig rig("7");
    rig.Send(100.f);
    // Deliberately unimplemented: on an object with an argument it would have
    // to mean either 0 or 7 and could not be told which. An unknown message is
    // ignored, so the register keeps its value.
    rig.Message("clear");
    CHECK(rig.op.Value() == doctest::Approx(100.0));
  }

  // ─── the creation argument ──────────────────────────────────────────────────

  TEST_CASE("accum: a bang before any operation reports the initial argument (#461)") {
    AccumRig rig("5");
    // Not silence, and not the constructor's 0: the argument is applied to the
    // register through the parse callback precisely so the first bang is right.
    CHECK(rig.Bang() == doctest::Approx(5.f));
    CHECK(rig.Emitted() == 1);
  }

  TEST_CASE("accum: a bang before any operation reports 0 without an argument (#461)") {
    AccumRig rig;
    CHECK(rig.Bang() == 0.f);
    CHECK(std::isfinite(rig.sink.last));
    CHECK(rig.Emitted() == 1);
  }

  TEST_CASE("accum: the initial argument is where the accumulation starts (#461)") {
    AccumRig rig("100");
    rig.AddIn(1.f);
    CHECK(rig.Bang() == doctest::Approx(101.f));
  }

  TEST_CASE("accum: a fractional argument is kept as a float (#461)") {
    AccumRig rig("2.5");
    CHECK(rig.Bang() == doctest::Approx(2.5f));
    rig.MultIn(2.f);
    CHECK(rig.Bang() == doctest::Approx(5.f));
  }

  // Max's int variant would round every product back to an integer; this port
  // deliberately does not, since `.accum 5` and `.accum 5.0` are the same
  // parameter string here and guessing wrong destroys a scaling chain.
  TEST_CASE("accum: an integral argument does not make the object integral (#461)") {
    AccumRig rig("5");
    rig.MultIn(0.5f);
    rig.MultIn(0.5f);
    CHECK(rig.Bang() == doctest::Approx(1.25f)); // not 1, and certainly not 0
  }

  // ─── the limits: saturation instead of infinity ─────────────────────────────

  TEST_CASE("accum: Clamp saturates at +/-FLT_MAX and neutralises a NaN (#461)") {
    CHECK(gAccum::Clamp(0.0) == 0.0);
    CHECK(gAccum::Clamp(1.5) == 1.5);
    CHECK(gAccum::Clamp(LIMIT) == LIMIT);
    CHECK(gAccum::Clamp(-LIMIT) == -LIMIT);
    CHECK(gAccum::Clamp(LIMIT * 2.0) == LIMIT);
    CHECK(gAccum::Clamp(-LIMIT * 2.0) == -LIMIT);
    CHECK(gAccum::Clamp(1e300) == LIMIT);
    CHECK(gAccum::Clamp(std::numeric_limits<double>::infinity()) == LIMIT);
    CHECK(gAccum::Clamp(-std::numeric_limits<double>::infinity()) == -LIMIT);
    CHECK(gAccum::Clamp(std::numeric_limits<double>::quiet_NaN()) == 0.0);
  }

  TEST_CASE("accum: repeated doubling saturates instead of becoming infinite (#461)") {
    AccumRig rig;
    rig.Send(1.f);
    // 128 doublings put 1.0 past FLT_MAX. Two hundred is comfortably beyond,
    // and in a patch this is one second of a .metro at 200 ms.
    for (int i = 0; i < 200; i++)
      rig.MultIn(2.f);

    const float out = rig.Bang();
    CHECK(std::isfinite(out));
    CHECK(out == std::numeric_limits<float>::max());
    CHECK(rig.op.Value() == LIMIT);
  }

  TEST_CASE("accum: a negative runaway saturates with its sign intact (#461)") {
    AccumRig rig;
    rig.Send(-1.f);
    for (int i = 0; i < 200; i++)
      rig.MultIn(10.f);

    CHECK(std::isfinite(rig.Bang()));
    CHECK(rig.op.Value() == -LIMIT);
  }

  // The point of saturating rather than overflowing: a saturated register is
  // still a number, so the patch recovers. An infinity would not — inf * 0.5
  // is inf — and that is the failure this test forbids.
  TEST_CASE("accum: a saturated register is brought back by the next multiply (#461)") {
    AccumRig rig;
    rig.Send(1.f);
    for (int i = 0; i < 200; i++)
      rig.MultIn(10.f);
    REQUIRE(rig.op.Value() == LIMIT);

    rig.MultIn(1e-30f);
    CHECK(std::isfinite(rig.op.Value()));
    CHECK(rig.op.Value() == doctest::Approx(LIMIT * 1e-30).epsilon(1e-6));
    CHECK(rig.op.Value() < 1e10); // genuinely back in a usable range
  }

  // The second half of that argument: an infinity that reached the register
  // would meet its opposite sooner or later and leave a NaN behind, which no
  // later message could clear and which every downstream object would inherit.
  TEST_CASE("accum: saturating both ways never produces a NaN (#461)") {
    AccumRig rig;
    rig.Send(1.f);
    for (int i = 0; i < 200; i++)
      rig.MultIn(10.f);
    REQUIRE(rig.op.Value() == LIMIT);

    // A true infinity here would give inf + (-inf) = NaN.
    rig.AddIn(-static_cast<float>(LIMIT));
    CHECK(std::isfinite(rig.op.Value()));
    CHECK(rig.op.Value() == doctest::Approx(0.0));

    // And the object still works.
    CHECK(rig.Send(3.f) == doctest::Approx(3.f));
  }

  TEST_CASE("accum: adding at the ceiling saturates rather than overflowing (#461)") {
    AccumRig rig;
    rig.Send(std::numeric_limits<float>::max());
    rig.AddIn(std::numeric_limits<float>::max());
    CHECK(rig.op.Value() == LIMIT);
    CHECK(std::isfinite(rig.Bang()));
  }

  TEST_CASE("accum: an infinite input on inlet 0 stores 0, not an infinity (#461)") {
    AccumRig rig;
    // The family convention for a value that becomes the stored one.
    CHECK(rig.Send(std::numeric_limits<float>::infinity()) == 0.f);
    CHECK(rig.op.Value() == 0.0);
    CHECK(rig.Send(-std::numeric_limits<float>::infinity()) == 0.f);
    CHECK(rig.Send(std::numeric_limits<float>::quiet_NaN()) == 0.f);
    CHECK(std::isfinite(rig.op.Value()));
  }

  // ─── the bottom of the range is not floored ─────────────────────────────────

  TEST_CASE("accum: a value below float resolution survives in the register (#461)") {
    AccumRig rig;
    rig.Send(1.f);
    rig.MultIn(1e-30f);
    rig.MultIn(1e-30f); // 1e-60: zero as a float, fine as a double

    CHECK(rig.Bang() == 0.f); // the outlet says 0 ...
    CHECK(rig.op.Value() > 0.0); // ... but the register has not lost it

    rig.MultIn(1e30f);
    rig.MultIn(1e30f);
    // Round trip recovered, which a float register could not have done.
    CHECK(rig.Bang() == doctest::Approx(1.f));
  }

  TEST_CASE("accum: multiplying by 0 zeroes the register, as arithmetic says (#461)") {
    AccumRig rig;
    rig.Send(1000.f);
    rig.MultIn(0.f);
    CHECK(rig.op.Value() == 0.0);
    // Nothing recovers a zeroed register but set / reset / a number on inlet 0,
    // which is exactly why a non-finite multiplier is *not* folded in as 0.
    rig.MultIn(1e9f);
    CHECK(rig.op.Value() == 0.0);
    rig.Message("reset");
    CHECK(rig.op.Value() == 0.0);
    CHECK(rig.Send(4.f) == doctest::Approx(4.f));
  }

  // ─── non-finite operands ────────────────────────────────────────────────────

  TEST_CASE("accum: a non-finite addend is neutralised as 0 (#461)") {
    AccumRig rig;
    rig.Send(6.f);
    rig.AddIn(std::numeric_limits<float>::quiet_NaN());
    rig.AddIn(std::numeric_limits<float>::infinity());
    rig.AddIn(-std::numeric_limits<float>::infinity());
    CHECK(rig.op.Value() == doctest::Approx(6.0));
    CHECK(std::isfinite(rig.Bang()));
  }

  // The one place the family's "read a non-finite as 0" convention has to be
  // refined: folding a NaN into a *multiply* as 0 would zero the register, and
  // nothing a patch sends afterwards could multiply it back.
  TEST_CASE("accum: a non-finite multiplier is neutralised as 1, not 0 (#461)") {
    AccumRig rig;
    rig.Send(6.f);
    rig.MultIn(std::numeric_limits<float>::quiet_NaN());
    CHECK(rig.op.Value() == doctest::Approx(6.0)); // untouched, not zeroed
    rig.MultIn(std::numeric_limits<float>::infinity());
    CHECK(rig.op.Value() == doctest::Approx(6.0));
    rig.MultIn(-std::numeric_limits<float>::infinity());
    CHECK(rig.op.Value() == doctest::Approx(6.0));

    // And the register is still multiplicable, which folding in a 0 would have
    // permanently prevented.
    rig.MultIn(2.f);
    CHECK(rig.op.Value() == doctest::Approx(12.0));
  }

  TEST_CASE("accum: 'set' with a non-finite number stores 0 (#461)") {
    AccumRig rig;
    rig.Message("set inf");
    CHECK(std::isfinite(rig.op.Value()));
    CHECK(rig.op.Value() == doctest::Approx(0.0));
  }

  // ─── precision ──────────────────────────────────────────────────────────────

  TEST_CASE("accum: the register keeps more precision than the outlet (#461)") {
    AccumRig rig;
    rig.Send(1.f);
    // 1 + 1e-10 is exactly 1 in float; in the double register it is not, and
    // ten thousand of them add up to something the outlet can finally see.
    for (int i = 0; i < 10000; i++)
      rig.AddIn(1e-10f);

    // A float register would still hold exactly 1.0 here, having rounded every
    // one of the ten thousand additions away; the tolerance is three decades
    // tighter than the 1e-6 the additions are worth, so this asserts the
    // double rather than merely the sign of the difference.
    CHECK(rig.op.Value() > 1.0);
    CHECK(rig.op.Value() == doctest::Approx(1.0 + 1e-6).epsilon(1e-9));
  }

  TEST_CASE("accum: a long multiply chain does not drift (#461)") {
    AccumRig rig;
    rig.Send(1.f);
    // A thousand multiplies by a factor that is exact in float, so the only
    // error in play is the register's own. Against the same product computed
    // in double: a float register accumulates about 1e-5 of relative error
    // over this chain and a double one about 1e-13, so the tolerance below
    // separates them by four decades rather than by luck.
    const float factor = 1.0009765625f; // 1 + 2^-10, exactly representable
    for (int i = 0; i < 1000; i++)
      rig.MultIn(factor);

    const double exact = std::pow(static_cast<double>(factor), 1000.0);
    CAPTURE(exact);
    CHECK(rig.op.Value() == doctest::Approx(exact).epsilon(1e-9));
  }

  // ─── unknown / malformed messages ───────────────────────────────────────────

  TEST_CASE("accum: an unknown message is ignored (#461)") {
    AccumRig rig;
    rig.Send(5.f);
    const int before = rig.Emitted();

    rig.Message("wobble");
    rig.Message("");
    rig.Message("   ");
    rig.Message("42"); // a bare number is not a message either
    CHECK(rig.Emitted() == before);
    CHECK(rig.op.Value() == doctest::Approx(5.0));
  }

  TEST_CASE("accum: a message word without a number is ignored (#461)") {
    AccumRig rig;
    rig.Send(5.f);
    rig.Message("set");
    rig.Message("add");
    rig.Message("mult");
    rig.Message("set ");
    CHECK(rig.op.Value() == doctest::Approx(5.0));
  }

  // The word has to end where it ends, or the shared list reader — which steps
  // over tokens it cannot parse — would find the 5 in `address 5`.
  TEST_CASE("accum: a longer word starting with a message word is ignored (#461)") {
    AccumRig rig;
    rig.Send(5.f);
    rig.Message("address 9");
    rig.Message("settle 9");
    rig.Message("multiverse 9");
    rig.Message("resetting");
    CHECK(rig.op.Value() == doctest::Approx(5.0));
  }

  TEST_CASE("accum: the cold inlets ignore bangs and lists (#461)") {
    AccumRig rig;
    rig.Send(5.f);
    const int before = rig.Emitted();
    // Only inlet 0 registers them, so this asserts the registration as much as
    // the handler guard; either way nothing may change or come out.
    rig.op.GetInlet(1)->SetBang(YSE::T_GUI);
    rig.op.GetInlet(2)->SetBang(YSE::T_GUI);
    rig.op.GetInlet(1)->SetList("set 99", YSE::T_GUI);
    rig.op.GetInlet(2)->SetList("mult 99", YSE::T_GUI);
    CHECK(rig.Emitted() == before);
    CHECK(rig.op.Value() == doctest::Approx(5.0));
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  TEST_CASE("accum: survives a DumpJSON / ParseJSON round trip (#461)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_ACCUM);
    REQUIRE(h != nullptr);
    h->SetParams("12.5");
    const std::string json = src.DumpJSON();
    CHECK(json.find(".accum") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".accum"));
    CHECK(copy->GetInputs() == 3);
    CHECK(copy->GetOutputs() == 1);
    CHECK(copy->GetParams() == std::string("12.5"));
  }

  TEST_CASE("accum: the GUI value reports the stored value (#461)") {
    AccumRig rig;
    CHECK(std::stod(rig.op.GetGuiValue()) == doctest::Approx(0.0));
    rig.Send(2.f);
    rig.AddIn(1.5f);
    CHECK(std::stod(rig.op.GetGuiValue()) == doctest::Approx(3.5));
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the port shape, which is what a binding
  // generator keys on.

  TEST_CASE("accum: documents itself as MATH with three inlets and one outlet (#461)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_ACCUM));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MATH);
    CHECK_FALSE(obj->GetDescription().empty());
    REQUIRE(obj->NumInputs() == 3);
    CHECK(obj->GetInlet(0)->GetDocLabel() == "value");
    CHECK(obj->GetInlet(1)->GetDocLabel() == "add");
    CHECK(obj->GetInlet(2)->GetDocLabel() == "mult");
    REQUIRE(obj->NumOutputs() == 1);
    CHECK(obj->GetOutlet(0)->GetDocLabel() == "out");
    CHECK(obj->GetOutputType(0) == YSE::OUT_TYPE::FLOAT);
    REQUIRE(obj->GetParamDocs().size() == 1);
    CHECK(obj->GetParamDocs()[0].name == "initial");
    CHECK(obj->GetParamDocs()[0].defaultValue == "0");
  }

  TEST_CASE("accum: inlet 0 accepts float, int, bang and list (#461)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_ACCUM));
    REQUIRE(obj != nullptr);
    const unsigned int hot = obj->GetInlet(0)->GetAcceptedTypes();
    CHECK((hot & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((hot & YSE::PATCHER::IT_INT) != 0);
    CHECK((hot & YSE::PATCHER::IT_BANG) != 0);
    CHECK((hot & YSE::PATCHER::IT_LIST) != 0);

    // The cold inlets take numbers only — they have one job each.
    for (int i = 1; i <= 2; i++) {
      CAPTURE(i);
      const unsigned int cold = obj->GetInlet(i)->GetAcceptedTypes();
      CHECK((cold & YSE::PATCHER::IT_FLOAT) != 0);
      CHECK((cold & YSE::PATCHER::IT_INT) != 0);
      CHECK((cold & YSE::PATCHER::IT_BANG) == 0);
      CHECK((cold & YSE::PATCHER::IT_LIST) == 0);
    }
  }

} // TEST_SUITE("patcher")

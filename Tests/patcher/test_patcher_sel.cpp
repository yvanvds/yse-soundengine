// Tests for .sel (issue #465) — bang the outlet whose selector the input
// matches.
//
// The object is Max's `select`, and almost all of it is shape rather than
// arithmetic, so this file pins the shape:
//
//   - **N selectors, N+1 outlets**, and a bare `.sel` is the no-argument case
//     Max defines as one match outlet for the number 0 plus the pass-through.
//   - **exactly one outlet fires.** The object looks like it might fan out and
//     does not; Max settles the only ambiguous case — "If an int is listed
//     multiple times as an argument, a bang message will be sent out the
//     leftmost outlet only" — which is also why there is no right-to-left
//     firing order to respect here, unlike .mean or .peak.
//   - **the reject outlet preserves the type**, so a chain of .sel objects can
//     be strung together the way .split's out-of-range branch chains.
//   - **numbers and symbols never match each other**, and a list is matched on
//     its first element alone with the rest dropped (that is .route's job).
//   - **the right inlet exists only for a single numeric selector**, which is
//     Max's rule with `numeric` in place of `int` — the one deliberate
//     deviation, since this patcher has a single numeric type.
//   - **the comparison is exact**: Max's matchfloat / fuzzy attributes are not
//     ported, and a NaN therefore matches nothing rather than being read as 0.
//
// No audio device required.

#include <doctest/doctest.h>
#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/genericObjects/gSel.h"

namespace {

  using YSE::PATCHER::gSel;

  constexpr float INF = std::numeric_limits<float>::infinity();
  const float NOT_A_NUMBER = std::numeric_limits<float>::quiet_NaN();

  // Records everything that arrives, tagged with the type it arrived as. The
  // reject outlet's whole contract is "unchanged and in its own type", so a
  // sink that only counted would not be able to see it kept.
  struct Sink : YSE::PATCHER::pObject {
    enum Kind { BANG, INT, FLOAT, LIST };

    struct Event {
      Kind kind;
      int i = 0;
      float f = 0.f;
      std::string text;
    };

    std::vector<Event> events;

    Sink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { events.push_back({BANG}); });
      inputs.back().RegisterInt(
          [this](int v, int, YSE::THREAD) { events.push_back({INT, v, 0.f, ""}); });
      inputs.back().RegisterFloat(
          [this](float v, int, YSE::THREAD) { events.push_back({FLOAT, 0, v, ""}); });
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { events.push_back({LIST, 0, 0.f, v}); });
    }
    const char* Type() const override {
      return "sel_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    std::size_t Count() const {
      return events.size();
    }
  };

  // One sink per outlet, so "which outlet fired" is an assertion rather than an
  // inference. Every test below is really about that.
  struct Rig {
    std::unique_ptr<gSel> op;
    std::vector<std::unique_ptr<Sink>> sinks;

    explicit Rig(const std::string& args = "") : op(new gSel()) {
      if (!args.empty()) op->SetParams(args);
      Wire();
    }

    void Wire() {
      sinks.clear();
      for (int i = 0; i < op->NumOutputs(); i++) {
        sinks.push_back(std::unique_ptr<Sink>(new Sink()));
        op->ConnectOutlet(sinks.back()->GetInlet(0), i);
        sinks.back()->ConnectInlet(op->GetOutlet(i), 0);
      }
    }

    void Bang() {
      op->GetInlet(0)->SetBang(YSE::T_GUI);
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
    void SetValue(int v) {
      op->GetInlet(1)->SetInt(v, YSE::T_GUI);
    }
    void SetValueF(float v) {
      op->GetInlet(1)->SetFloat(v, YSE::T_GUI);
    }

    int Outlets() const {
      return op->NumOutputs();
    }
    int Reject() const {
      return op->NumOutputs() - 1;
    }
    std::size_t Hits(int outlet) const {
      return sinks[outlet]->Count();
    }

    // Total across every outlet — the "exactly one fired" assertion.
    std::size_t Total() const {
      std::size_t n = 0;
      for (const auto& s : sinks)
        n += s->Count();
      return n;
    }

    const Sink::Event& Last(int outlet) const {
      return sinks[outlet]->events.back();
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("sel: creatable through the registry (#465)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_SEL, "1 2 3");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".sel"));
    // Max: "The number of arguments determines the number of outlets in
    // addition to the rightmost outlet."
    CHECK(h->GetOutputs() == 4);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BANG);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::BANG);
    CHECK(h->OutputDataType(2) == YSE::OUT_TYPE::BANG);
    CHECK(h->OutputDataType(3) == YSE::OUT_TYPE::ANY);
    // More than one argument, so no right inlet.
    CHECK(h->GetInputs() == 1);
  }

  TEST_CASE("sel: listed by pRegistry::AllNames (#465)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".sel")) != names.end());
  }

  // Max: "If there is no argument, there is only one other outlet, which is
  // assigned the integer number 0."
  TEST_CASE("sel: a bare .sel has two outlets and matches the number 0 (#465)") {
    Rig rig;
    CHECK(rig.Outlets() == 2);
    CHECK(rig.op->SelectorCount() == 1);
    CHECK(rig.op->SelectorIsNumber(0));
    CHECK(rig.op->SelectorValue(0) == doctest::Approx(0.f));

    rig.Send(0.f);
    CHECK(rig.Hits(0) == 1);
    CHECK(rig.Hits(1) == 0);

    rig.Send(1.f);
    CHECK(rig.Hits(0) == 1);
    CHECK(rig.Hits(1) == 1);
  }

  TEST_CASE("sel: one outlet per selector plus the pass-through (#465)") {
    Rig one("5");
    CHECK(one.Outlets() == 2);

    Rig five("1 2 3 4 5");
    CHECK(five.Outlets() == 6);
    CHECK(five.op->SelectorCount() == 5);
  }

  // ─── matching numbers ───────────────────────────────────────────────────────

  TEST_CASE("sel: a matching number bangs its own outlet and nothing else (#465)") {
    Rig rig("1 2 3");
    rig.SendInt(2);
    CHECK(rig.Total() == 1);
    CHECK(rig.Hits(1) == 1);
    CHECK(rig.Last(1).kind == Sink::BANG);
  }

  TEST_CASE("sel: each selector gets its own outlet, in argument order (#465)") {
    Rig rig("10 20 30");
    rig.SendInt(30);
    rig.SendInt(10);
    rig.SendInt(20);
    CHECK(rig.Hits(0) == 1);
    CHECK(rig.Hits(1) == 1);
    CHECK(rig.Hits(2) == 1);
    CHECK(rig.Hits(3) == 0);
  }

  // The patcher has a single numeric type, so `.sel 5` and `.sel 5.0` are the
  // same parameter string and both have to answer both spellings of 5.
  TEST_CASE("sel: an int and a float of the same value both match (#465)") {
    Rig fromInt("5");
    fromInt.SendInt(5);
    fromInt.Send(5.f);
    CHECK(fromInt.Hits(0) == 2);
    CHECK(fromInt.Hits(1) == 0);

    Rig fromFloat("5.0");
    fromFloat.SendInt(5);
    fromFloat.Send(5.f);
    CHECK(fromFloat.Hits(0) == 2);
    CHECK(fromFloat.Hits(1) == 0);
  }

  TEST_CASE("sel: fractional and negative selectors compare as numbers (#465)") {
    Rig rig("-2.5 0.25");
    rig.Send(-2.5f);
    CHECK(rig.Hits(0) == 1);
    rig.Send(0.25f);
    CHECK(rig.Hits(1) == 1);
    rig.Send(2.5f);
    CHECK(rig.Hits(2) == 1);
  }

  // ─── exactly one outlet fires ───────────────────────────────────────────────

  // Max: "If an int is listed multiple times as an argument, a bang message
  // will be sent out the leftmost outlet only." This is what makes the object
  // free of the right-to-left firing order .mean and .peak have to respect.
  TEST_CASE("sel: a repeated selector bangs the leftmost outlet only (#465)") {
    Rig rig("5 5 5");
    rig.SendInt(5);
    CHECK(rig.Total() == 1);
    CHECK(rig.Hits(0) == 1);
    CHECK(rig.Hits(1) == 0);
    CHECK(rig.Hits(2) == 0);
    CHECK(rig.Hits(3) == 0);
  }

  TEST_CASE("sel: every input produces exactly one outlet event (#465)") {
    Rig rig("1 hello");
    rig.SendInt(1); // match, outlet 0
    rig.List("hello"); // match, outlet 1
    rig.SendInt(9); // reject
    rig.Bang(); // reject
    rig.List("nope 1 2"); // reject
    rig.Send(0.5f); // reject
    CHECK(rig.Total() == 6);
    CHECK(rig.Hits(0) == 1);
    CHECK(rig.Hits(1) == 1);
    CHECK(rig.Hits(2) == 4);
  }

  // ─── the reject outlet ──────────────────────────────────────────────────────

  // "will output non-matching messages out its right-most outlet" — unchanged,
  // and in the type it arrived as, which is what lets a chain of .sel objects
  // be strung together through this outlet.
  TEST_CASE("sel: an unmatched value leaves the rightmost outlet in its own type (#465)") {
    Rig rig("1");

    rig.SendInt(7);
    REQUIRE(rig.Hits(1) == 1);
    CHECK(rig.Last(1).kind == Sink::INT);
    CHECK(rig.Last(1).i == 7);

    rig.Send(2.5f);
    REQUIRE(rig.Hits(1) == 2);
    CHECK(rig.Last(1).kind == Sink::FLOAT);
    CHECK(rig.Last(1).f == doctest::Approx(2.5f));

    rig.List("wobble 3 4");
    REQUIRE(rig.Hits(1) == 3);
    CHECK(rig.Last(1).kind == Sink::LIST);
    CHECK(rig.Last(1).text == "wobble 3 4");

    rig.Bang();
    REQUIRE(rig.Hits(1) == 4);
    CHECK(rig.Last(1).kind == Sink::BANG);

    // And nothing ever reached the match outlet.
    CHECK(rig.Hits(0) == 0);
  }

  TEST_CASE("sel: a match sends a bang, never the value (#465)") {
    Rig rig("42");
    rig.SendInt(42);
    REQUIRE(rig.Hits(0) == 1);
    CHECK(rig.Last(0).kind == Sink::BANG);
  }

  // ─── symbols ────────────────────────────────────────────────────────────────

  TEST_CASE("sel: a symbolic selector matches by exact text (#465)") {
    Rig rig("start stop");
    rig.List("stop");
    CHECK(rig.Hits(1) == 1);
    rig.List("start");
    CHECK(rig.Hits(0) == 1);
    CHECK(rig.Hits(2) == 0);
  }

  TEST_CASE("sel: a symbol match is exact, not a prefix (#465)") {
    Rig rig("stop");
    rig.List("sto");
    rig.List("stopp");
    rig.List("Stop");
    CHECK(rig.Hits(0) == 0);
    CHECK(rig.Hits(1) == 3);

    rig.List("stop");
    CHECK(rig.Hits(0) == 1);
  }

  // A number and a symbol are different kinds of selector and never match each
  // other, in either direction.
  TEST_CASE("sel: numbers and symbols do not match each other (#465)") {
    Rig numeric("5");
    numeric.List("five");
    CHECK(numeric.Hits(0) == 0);
    CHECK(numeric.Hits(1) == 1);

    Rig symbolic("five");
    symbolic.SendInt(5);
    symbolic.Send(5.f);
    CHECK(symbolic.Hits(0) == 0);
    CHECK(symbolic.Hits(1) == 2);
  }

  TEST_CASE("sel: mixed numeric and symbolic selectors coexist (#465)") {
    Rig rig("1 note 2.5");
    rig.SendInt(1);
    rig.List("note");
    rig.Send(2.5f);
    rig.List("other");
    CHECK(rig.Hits(0) == 1);
    CHECK(rig.Hits(1) == 1);
    CHECK(rig.Hits(2) == 1);
    CHECK(rig.Hits(3) == 1);
  }

  // ─── bang ───────────────────────────────────────────────────────────────────

  // Max: "The bang message matches a 'bang' symbol in the arguments."
  TEST_CASE("sel: a bang matches a selector spelled 'bang' (#465)") {
    Rig rig("1 bang");
    rig.Bang();
    CHECK(rig.Total() == 1);
    CHECK(rig.Hits(1) == 1);
    CHECK(rig.Last(1).kind == Sink::BANG);
  }

  TEST_CASE("sel: without a 'bang' selector a bang passes through as a bang (#465)") {
    Rig rig("1 2");
    rig.Bang();
    REQUIRE(rig.Hits(2) == 1);
    CHECK(rig.Last(2).kind == Sink::BANG);
    CHECK(rig.Hits(0) == 0);
    CHECK(rig.Hits(1) == 0);
  }

  // The list "bang" is the symbol bang, so it takes the same outlet a bang
  // message does — the two spellings of the same thing must not disagree.
  TEST_CASE("sel: the symbol 'bang' and a bang message take the same outlet (#465)") {
    Rig rig("bang");
    rig.Bang();
    rig.List("bang");
    CHECK(rig.Hits(0) == 2);
    CHECK(rig.Hits(1) == 0);
  }

  // ─── lists ──────────────────────────────────────────────────────────────────

  // Max: "a bang from one of its corresponding outlets if the first element in
  // the list matches the object argument(s)". Only the first element, and on a
  // match the rest is dropped — .route is the object that keeps the remainder.
  TEST_CASE("sel: a list is matched on its first element and the rest is dropped (#465)") {
    Rig rig("5");
    rig.List("5 6 7");
    CHECK(rig.Total() == 1);
    REQUIRE(rig.Hits(0) == 1);
    CHECK(rig.Last(0).kind == Sink::BANG); // not the remainder "6 7"
  }

  TEST_CASE("sel: an unmatched list leaves the reject outlet whole (#465)") {
    Rig rig("5");
    rig.List("9 6 7");
    REQUIRE(rig.Hits(1) == 1);
    CHECK(rig.Last(1).text == "9 6 7");
  }

  TEST_CASE("sel: a numeric first element of a list matches a numeric selector (#465)") {
    // In Max the first element of the list `5 6` *is* an int, and this patcher
    // carries lists as text; a text-only reading would send this out the right.
    Rig rig("5");
    rig.List("5 6");
    CHECK(rig.Hits(0) == 1);
  }

  TEST_CASE("sel: a symbolic first element of a list matches a symbolic selector (#465)") {
    Rig rig("note");
    rig.List("note 60 100");
    CHECK(rig.Hits(0) == 1);
  }

  TEST_CASE("sel: leading whitespace does not hide the first element (#465)") {
    Rig rig("5");
    rig.List("  5 6");
    CHECK(rig.Hits(0) == 1);
  }

  TEST_CASE("sel: an empty message matches nothing and passes through (#465)") {
    Rig rig("5");
    rig.List("");
    rig.List("   ");
    CHECK(rig.Hits(0) == 0);
    CHECK(rig.Hits(1) == 2);
  }

  // ─── exactness ──────────────────────────────────────────────────────────────

  // Max's `fuzzy` attribute is deliberately not ported: a tolerance makes .sel
  // match values it was not given, and two selectors closer together than the
  // tolerance would both be right. A .round upstream is the visible fix.
  TEST_CASE("sel: the comparison is exact, with no tolerance band (#465)") {
    Rig rig("0.5");
    rig.Send(0.5f);
    CHECK(rig.Hits(0) == 1);

    rig.Send(0.49999f);
    rig.Send(0.50001f);
    CHECK(rig.Hits(0) == 1);
    CHECK(rig.Hits(1) == 2);
  }

  // The patcher's usual "read a non-finite as 0" substitution is not applied
  // here: a stray NaN must not bang the outlet a patch wired for a real 0.
  TEST_CASE("sel: a NaN matches nothing, including a selector of 0 (#465)") {
    Rig zero("0");
    zero.Send(NOT_A_NUMBER);
    CHECK(zero.Hits(0) == 0);
    CHECK(zero.Hits(1) == 1);

    zero.Send(0.f); // and a real 0 still matches
    CHECK(zero.Hits(0) == 1);
  }

  TEST_CASE("sel: an infinity matches nothing and passes through (#465)") {
    Rig rig("0");
    rig.Send(INF);
    rig.Send(-INF);
    CHECK(rig.Hits(0) == 0);
    CHECK(rig.Hits(1) == 2);
  }

  // ─── how an argument becomes a number ───────────────────────────────────────

  // The whole token has to be the number. `strtof` alone stops at the first
  // character it cannot use, which would turn the symbol `5abc` into the number
  // 5 and leave a patch wired for it permanently unanswered.
  TEST_CASE("sel: a token that is only partly a number is a symbol (#465)") {
    Rig rig("5abc");
    CHECK_FALSE(rig.op->SelectorIsNumber(0));
    CHECK(rig.op->SelectorText(0) == "5abc");

    rig.SendInt(5);
    CHECK(rig.Hits(0) == 0);
    CHECK(rig.Hits(1) == 1);

    rig.List("5abc");
    CHECK(rig.Hits(0) == 1);
  }

  // A non-finite selector could never usefully match, and the shared
  // ExprParseFloatList would have folded both of these to 0 — which would make
  // `.sel 1e999` an object that bangs for every plain 0 a patch sends.
  TEST_CASE("sel: 'inf', 'nan' and an overflowing literal stay symbols (#465)") {
    Rig infinite("inf");
    CHECK_FALSE(infinite.op->SelectorIsNumber(0));
    infinite.Send(0.f);
    CHECK(infinite.Hits(0) == 0);
    infinite.Send(INF);
    CHECK(infinite.Hits(0) == 0);
    infinite.List("inf");
    CHECK(infinite.Hits(0) == 1);

    Rig notNumber("nan");
    CHECK_FALSE(notNumber.op->SelectorIsNumber(0));
    notNumber.Send(0.f);
    CHECK(notNumber.Hits(0) == 0);

    Rig overflow("1e999");
    CHECK_FALSE(overflow.op->SelectorIsNumber(0));
    overflow.Send(0.f);
    CHECK(overflow.Hits(0) == 0);
    CHECK(overflow.Hits(1) == 1);
  }

  TEST_CASE("sel: exponent notation is a number (#465)") {
    Rig rig("1e3");
    CHECK(rig.op->SelectorIsNumber(0));
    CHECK(rig.op->SelectorValue(0) == doctest::Approx(1000.f));
    rig.Send(1000.f);
    CHECK(rig.Hits(0) == 1);
  }

  // ─── the right inlet ────────────────────────────────────────────────────────

  // Max: "If there is a single int argument (or if there are no arguments) a
  // second inlet is created on the right." Reproduced with `numeric` in place
  // of `int` — see the header for why the int-ness cannot survive this
  // patcher's parameter model.
  TEST_CASE("sel: a single numeric selector gets the right inlet (#465)") {
    Rig one("5");
    CHECK(one.op->HasValueInlet());
    CHECK(one.op->NumInputs() == 2);

    Rig bare;
    CHECK(bare.op->HasValueInlet());

    Rig fractional("2.5");
    CHECK(fractional.op->HasValueInlet());
  }

  TEST_CASE("sel: more than one selector, or a symbolic one, has no right inlet (#465)") {
    Rig two("1 2");
    CHECK_FALSE(two.op->HasValueInlet());
    CHECK(two.op->NumInputs() == 1);

    Rig symbolic("hello");
    CHECK_FALSE(symbolic.op->HasValueInlet());
    CHECK(symbolic.op->NumInputs() == 1);
  }

  // "Numbers received in that inlet are stored in place of the argument."
  TEST_CASE("sel: the right inlet replaces the selector, silently (#465)") {
    Rig rig("5");
    rig.SetValue(9);
    CHECK(rig.Total() == 0); // silent
    CHECK(rig.op->SelectorValue(0) == doctest::Approx(9.f));

    rig.SendInt(5); // the old value no longer matches
    CHECK(rig.Hits(0) == 0);
    CHECK(rig.Hits(1) == 1);

    rig.SendInt(9);
    CHECK(rig.Hits(0) == 1);
  }

  TEST_CASE("sel: the right inlet takes a float as well as an int (#465)") {
    Rig rig("5");
    rig.SetValueF(-1.25f);
    CHECK(rig.op->SelectorValue(0) == doctest::Approx(-1.25f));
    rig.Send(-1.25f);
    CHECK(rig.Hits(0) == 1);
  }

  TEST_CASE("sel: the right inlet does not change the outlet count (#465)") {
    Rig rig("5");
    const int before = rig.Outlets();
    rig.SetValue(9);
    CHECK(rig.Outlets() == before);
  }

  // ─── parameter edges ────────────────────────────────────────────────────────

  TEST_CASE("sel: a run of spaces in the argument does not create empty selectors (#465)") {
    Rig rig("1  2");
    CHECK(rig.op->SelectorCount() == 2);
    CHECK(rig.Outlets() == 3);
    CHECK(rig.op->SelectorValue(0) == doctest::Approx(1.f));
    CHECK(rig.op->SelectorValue(1) == doctest::Approx(2.f));
  }

  TEST_CASE("sel: re-parsing an empty parameter string returns to a bare .sel (#465)") {
    Rig rig("1 2 3");
    REQUIRE(rig.op->SelectorCount() == 3);
    REQUIRE(rig.Outlets() == 4);
    REQUIRE_FALSE(rig.op->HasValueInlet());

    rig.op->SetParams("");
    CHECK(rig.op->SelectorCount() == 1);
    CHECK(rig.op->SelectorIsNumber(0));
    CHECK(rig.op->SelectorValue(0) == doctest::Approx(0.f));
    CHECK(rig.Outlets() == 2);
    // The bare object is the no-argument case, so the right inlet comes back.
    CHECK(rig.op->HasValueInlet());

    rig.Wire();
    rig.Send(0.f);
    CHECK(rig.Hits(0) == 1);
  }

  TEST_CASE("sel: re-parsing rebuilds the outlets and the right inlet (#465)") {
    Rig rig("5");
    REQUIRE(rig.op->HasValueInlet());

    rig.op->SetParams("1 2 3");
    CHECK(rig.Outlets() == 4);
    CHECK_FALSE(rig.op->HasValueInlet());

    rig.op->SetParams("7");
    CHECK(rig.Outlets() == 2);
    CHECK(rig.op->HasValueInlet());
    CHECK(rig.op->SelectorValue(0) == doctest::Approx(7.f));
  }

  TEST_CASE("sel: at most 256 selectors are held (#465)") {
    std::string args;
    for (int i = 0; i < 300; i++) {
      if (i > 0) args += " ";
      args += std::to_string(i);
    }
    Rig rig(args);
    CHECK(rig.op->SelectorCount() == gSel::MAX_SELECTORS);
    CHECK(rig.Outlets() == gSel::MAX_SELECTORS + 1);

    rig.SendInt(255);
    CHECK(rig.Hits(255) == 1);
    rig.SendInt(256); // dropped, so it is a reject
    CHECK(rig.Hits(gSel::MAX_SELECTORS) == 1);
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  TEST_CASE("sel: survives a DumpJSON / ParseJSON round trip (#465)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_SEL);
    REQUIRE(h != nullptr);
    h->SetParams("1 note 2.5");
    const std::string json = src.DumpJSON();
    CHECK(json.find(".sel") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".sel"));
    CHECK(copy->GetParams() == std::string("1 note 2.5"));
    // The shape has to come back with it, or the saved patch cords land on the
    // wrong outlets.
    CHECK(copy->GetOutputs() == 4);
    CHECK(copy->GetInputs() == 1);
  }

  TEST_CASE("sel: a single-selector round trip keeps the right inlet (#465)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_SEL, "5");
    REQUIRE(h != nullptr);
    REQUIRE(h->GetInputs() == 2);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(src.DumpJSON());
    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(copy->GetInputs() == 2);
    CHECK(copy->GetOutputs() == 2);
  }

  // ─── chaining ───────────────────────────────────────────────────────────────

  // The reject outlet preserving the type is what makes this work, and it is
  // the idiom .split establishes for its out-of-range branch.
  TEST_CASE("sel: reject outlets chain into the next .sel (#465)") {
    std::unique_ptr<gSel> first(new gSel());
    first->SetParams("1");
    std::unique_ptr<gSel> second(new gSel());
    second->SetParams("2");

    first->ConnectOutlet(second->GetInlet(0), 1);
    second->ConnectInlet(first->GetOutlet(1), 0);

    Sink hitFirst;
    Sink hitSecond;
    Sink rest;
    first->ConnectOutlet(hitFirst.GetInlet(0), 0);
    hitFirst.ConnectInlet(first->GetOutlet(0), 0);
    second->ConnectOutlet(hitSecond.GetInlet(0), 0);
    hitSecond.ConnectInlet(second->GetOutlet(0), 0);
    second->ConnectOutlet(rest.GetInlet(0), 1);
    rest.ConnectInlet(second->GetOutlet(1), 0);

    first->GetInlet(0)->SetInt(1, YSE::T_GUI);
    CHECK(hitFirst.Count() == 1);
    CHECK(hitSecond.Count() == 0);
    CHECK(rest.Count() == 0);

    first->GetInlet(0)->SetInt(2, YSE::T_GUI);
    CHECK(hitFirst.Count() == 1);
    CHECK(hitSecond.Count() == 1);
    CHECK(rest.Count() == 0);

    first->GetInlet(0)->SetInt(3, YSE::T_GUI);
    CHECK(rest.Count() == 1);
    CHECK(rest.events.back().kind == Sink::INT);
    CHECK(rest.events.back().i == 3);
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category, the port shape and the parameter name,
  // which is what a binding generator keys on. It also pins that the outlets
  // built by the parse callback are documented too — the coverage test only
  // ever sees a default-constructed object.

  TEST_CASE("sel: documents itself as GENERIC with a labelled port set (#465)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_SEL));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    CHECK_FALSE(obj->GetDescription().empty());

    REQUIRE(obj->NumInputs() == 2);
    CHECK(obj->GetInlet(0)->GetDocLabel() == "in");
    CHECK(obj->GetInlet(1)->GetDocLabel() == "value");

    REQUIRE(obj->NumOutputs() == 2);
    CHECK(obj->GetOutlet(0)->GetDocLabel() == "match0");
    CHECK(obj->GetOutlet(1)->GetDocLabel() == "rest");
    CHECK(obj->GetOutputType(0) == YSE::OUT_TYPE::BANG);
    CHECK(obj->GetOutputType(1) == YSE::OUT_TYPE::ANY);

    REQUIRE(obj->GetParamDocs().size() == 1);
    CHECK(obj->GetParamDocs()[0].name == "selectors");
    CHECK(obj->GetParamDocs()[0].defaultValue == "0");
  }

  TEST_CASE("sel: outlets created by the parse callback are documented too (#465)") {
    gSel op;
    op.SetParams("1 note");
    REQUIRE(op.NumOutputs() == 3);
    for (int i = 0; i < op.NumOutputs(); i++) {
      CAPTURE(i);
      CHECK_FALSE(op.GetOutlet(i)->GetDocLabel().empty());
      CHECK_FALSE(op.GetOutlet(i)->GetDocDescription().empty());
    }
    CHECK(op.GetOutlet(0)->GetDocLabel() == "match0");
    CHECK(op.GetOutlet(1)->GetDocLabel() == "match1");
    CHECK(op.GetOutlet(2)->GetDocLabel() == "rest");
    // The range field carries the selector the outlet answers to.
    CHECK(op.GetOutlet(1)->GetRange() == "note");
  }

  TEST_CASE("sel: the hot inlet accepts bang, int, float and list (#465)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_SEL));
    REQUIRE(obj != nullptr);
    const unsigned int hot = obj->GetInlet(0)->GetAcceptedTypes();
    CHECK((hot & YSE::PATCHER::IT_BANG) != 0);
    CHECK((hot & YSE::PATCHER::IT_INT) != 0);
    CHECK((hot & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((hot & YSE::PATCHER::IT_LIST) != 0);

    // The cold inlet stores a number and nothing else — Max's is an [int]
    // inlet, and a symbolic selector never has the inlet at all.
    REQUIRE(obj->NumInputs() == 2);
    const unsigned int cold = obj->GetInlet(1)->GetAcceptedTypes();
    CHECK((cold & YSE::PATCHER::IT_INT) != 0);
    CHECK((cold & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((cold & YSE::PATCHER::IT_BANG) == 0);
    CHECK((cold & YSE::PATCHER::IT_LIST) == 0);
  }

} // TEST_SUITE("patcher")

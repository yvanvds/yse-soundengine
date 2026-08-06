// Tests for .trigger (issue #466) — send one input to many outlets in a
// defined right-to-left order.
//
// The ordering guarantee *is* the object, so it carries the weight of this
// file, and it is asserted in three ways that a "did every outlet fire?" test
// could not distinguish from a broken one:
//
//   - **the sequence itself**, through sinks that all log into one buffer, so
//     the log reads back as the exact order the outlets were served. Reverse
//     the loop in EmitAll and these fail; leave the order unspecified and they
//     fail too.
//   - **each send completes before the next starts**, checked by hanging a
//     second .trigger off an outlet and requiring its whole subtree in the log
//     before the outlet to its left appears. Right-to-left with breadth-first
//     sends would pass the first check and fail this one.
//   - **the idiom the guarantee exists for**, end to end in a real patcher:
//     `.trigger b i` into an `.i`, where the bang must fire the value that the
//     same input just stored. Under any other order it fires the *previous*
//     value, which is the bug this object is here to prevent.
//
// The rest is the argument grammar — five format letters, everything else a
// constant — and the per-outlet type conversions, both taken from the Max
// reference rather than guessed.
//
// The standalone rigs wire objects directly rather than through a patcher, as
// every sibling suite does. That exercises the same outlet::Send* loop the
// pinned-GraphState path of #226 runs: the snapshot changes *which* adjacency
// vector is walked, not that it is walked front to back, so the ordering
// guarantee has one implementation, not two.
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
#include "patcher/genericObjects/gTrigger.h"
#include "patcher/sinks.hpp"

namespace {

  using TestHelpers::IntSink;
  using TestHelpers::OrderSink;
  using YSE::PATCHER::gTrigger;

  constexpr float INF = std::numeric_limits<float>::infinity();
  const float NOT_A_NUMBER = std::numeric_limits<float>::quiet_NaN();

  // One order-logging sink per outlet, all sharing one log. "Which outlet
  // fired, with what, and in what order" is then an assertion rather than an
  // inference — which is the whole subject of this object.
  struct Rig {
    std::unique_ptr<gTrigger> op;
    std::vector<std::unique_ptr<OrderSink>> sinks;
    std::vector<char> order;

    explicit Rig(const std::string& args = "") : op(new gTrigger()) {
      if (!args.empty()) op->SetParams(args);
      Wire();
    }

    // Rebuilt after a SetParams, since re-parsing replaces the outlets.
    void Wire() {
      sinks.clear();
      order.clear();
      for (int i = 0; i < op->NumOutputs(); i++) {
        sinks.push_back(std::unique_ptr<OrderSink>(new OrderSink()));
        // 'a' for outlet 0, 'b' for outlet 1, ... so the log reads left to
        // right in *outlet* order and a right-to-left firing shows up
        // reversed. Wrapped at 26, which only matters for the outlet-cap test
        // below — that one asserts the count, not the sequence.
        sinks.back()->tag = (char)('a' + (i % 26));
        sinks.back()->log = &order;
        op->ConnectOutlet(sinks.back()->GetInlet(0), i);
        sinks.back()->ConnectInlet(op->GetOutlet(i), 0);
      }
    }

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

    int Outlets() const {
      return op->NumOutputs();
    }
    const OrderSink& At(int outlet) const {
      return *sinks[(std::size_t)outlet];
    }
    std::string Log() const {
      return std::string(order.begin(), order.end());
    }
    int Total() const {
      int n = 0;
      for (const auto& s : sinks)
        n += s->count;
      return n;
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("trigger: creatable through the registry (#466)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_TRIGGER, "b i f l s");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".trigger"));
    // "The number of arguments determines the number of outlets."
    CHECK(h->GetOutputs() == 5);
    CHECK(h->GetInputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BANG);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::INT);
    CHECK(h->OutputDataType(2) == YSE::OUT_TYPE::FLOAT);
    // A symbol travels as a list message in this patcher, so `s` and `l` are
    // the same outlet type — see the header for the deviation.
    CHECK(h->OutputDataType(3) == YSE::OUT_TYPE::LIST);
    CHECK(h->OutputDataType(4) == YSE::OUT_TYPE::LIST);
  }

  TEST_CASE("trigger: listed by pRegistry::AllNames (#466)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".trigger")) != names.end());
  }

  // Max: "If there are no arguments, there are two outlets, both of which send
  // an int."
  TEST_CASE("trigger: with no arguments there are two int outlets (#466)") {
    Rig rig;
    REQUIRE(rig.Outlets() == 2);
    CHECK(rig.op->SlotKind(0) == gTrigger::Kind::INT);
    CHECK(rig.op->SlotKind(1) == gTrigger::Kind::INT);
    CHECK(rig.op->GetOutputType(0) == YSE::OUT_TYPE::INT);
    CHECK(rig.op->GetOutputType(1) == YSE::OUT_TYPE::INT);

    rig.SendInt(9);
    CHECK(rig.At(0).lastKind == OrderSink::INT);
    CHECK(rig.At(0).lastInt == 9);
    CHECK(rig.At(1).lastInt == 9);
  }

  TEST_CASE("trigger: one outlet per argument, in argument order (#466)") {
    Rig one("b");
    CHECK(one.Outlets() == 1);

    Rig six("b b b b b b");
    CHECK(six.Outlets() == 6);
    CHECK(six.op->SlotCount() == 6);
  }

  // ─── the ordering guarantee ─────────────────────────────────────────────────
  // The reason the object exists. Every test in this block would pass an
  // implementation that fires every outlet, and fail one that fires them in any
  // order but right to left.

  TEST_CASE("trigger: outlets fire right to left, not left to right (#466)") {
    Rig rig("b b b b");
    rig.Bang();
    // Sinks are tagged 'a' (outlet 0) through 'd' (outlet 3). Right to left is
    // the reverse of the outlet order.
    CHECK(rig.Log() == "dcba");
    CHECK(rig.Total() == 4);
  }

  TEST_CASE("trigger: the order is the same for every input type (#466)") {
    Rig rig("b b b");
    rig.Bang();
    CHECK(rig.Log() == "cba");
    rig.SendInt(1);
    CHECK(rig.Log() == "cbacba");
    rig.Send(1.5f);
    CHECK(rig.Log() == "cbacbacba");
    rig.List("some list");
    CHECK(rig.Log() == "cbacbacbacba");
  }

  TEST_CASE("trigger: the order holds across mixed outlet kinds (#466)") {
    // Not all-bang, so a switch arm that forgot the reverse walk cannot hide.
    Rig rig("i f b l s 7");
    rig.SendInt(3);
    CHECK(rig.Log() == "fedcba");
  }

  // Not a small-n special case: twenty outlets come out in the reverse of the
  // alphabet, so an implementation that only got the two- or three-outlet case
  // right has nowhere to hide.
  TEST_CASE("trigger: the order holds for many outlets (#466)") {
    std::string args;
    std::string expected;
    for (int i = 0; i < 20; i++) {
      if (i > 0) args += " ";
      args += "b";
      expected += (char)('a' + (19 - i));
    }
    Rig rig(args);
    REQUIRE(rig.Outlets() == 20);
    rig.SendInt(1);
    CHECK(rig.Log() == expected);
  }

  TEST_CASE("trigger: every outlet fires exactly once per input (#466)") {
    Rig rig("i f b l s");
    rig.SendInt(1);
    for (int i = 0; i < rig.Outlets(); i++) {
      CAPTURE(i);
      CHECK(rig.At(i).count == 1);
    }
    rig.Bang();
    for (int i = 0; i < rig.Outlets(); i++) {
      CAPTURE(i);
      CHECK(rig.At(i).count == 2);
    }
  }

  // The half of the guarantee a bare sequence check cannot see: a send does not
  // merely *start* before the one to its left, it finishes — the whole subgraph
  // behind it runs first. A breadth-first fan-out would still log "b" before
  // "a" at the top level while interleaving everything below.
  TEST_CASE("trigger: each send completes before the outlet to its left fires (#466)") {
    std::vector<char> order;

    gTrigger outer;
    outer.SetParams("b b");
    gTrigger inner;
    inner.SetParams("b b");

    // outer outlet 1 (fired first) drives the inner trigger.
    outer.ConnectOutlet(inner.GetInlet(0), 1);
    inner.ConnectInlet(outer.GetOutlet(1), 0);

    OrderSink innerLeft;
    OrderSink innerRight;
    OrderSink outerLeft;
    innerLeft.tag = 'x';
    innerRight.tag = 'y';
    outerLeft.tag = 'z';
    innerLeft.log = innerRight.log = outerLeft.log = &order;

    inner.ConnectOutlet(innerLeft.GetInlet(0), 0);
    innerLeft.ConnectInlet(inner.GetOutlet(0), 0);
    inner.ConnectOutlet(innerRight.GetInlet(0), 1);
    innerRight.ConnectInlet(inner.GetOutlet(1), 0);
    outer.ConnectOutlet(outerLeft.GetInlet(0), 0);
    outerLeft.ConnectInlet(outer.GetOutlet(0), 0);

    outer.GetInlet(0)->SetBang(YSE::T_GUI);

    // The inner trigger's own right-to-left pair runs to completion ('y' then
    // 'x') before the outer object's left outlet is served ('z').
    CHECK(std::string(order.begin(), order.end()) == "yxz");
  }

  // The idiom the guarantee is for, in a real patcher with real objects:
  // `.trigger b i` stores the value through the right outlet, then bangs it out
  // through the left one. Fire left to right and the bang emits the *previous*
  // value — which is exactly the class of bug .trigger exists to make
  // impossible, so the check is that the reported value tracks the input.
  TEST_CASE("trigger: 'b i' bangs the value it just stored, in a patcher (#466)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* trig = p.CreateObject(YSE::OBJ::G_TRIGGER, "b i");
    YSE::pHandle* store = p.CreateObject(YSE::OBJ::G_INT);
    REQUIRE(trig != nullptr);
    REQUIRE(store != nullptr);

    IntSink sink;
    YSE::pHandle sinkHandle(&sink);

    // Right outlet -> the silent inlet; left outlet -> the bang inlet.
    p.Connect(trig, 1, store, 1);
    p.Connect(trig, 0, store, 0);
    p.Connect(store, 0, &sinkHandle, 0);

    trig->SetIntData(0, 42);
    CHECK(sink.gotInt);
    CHECK(sink.received == 42);

    // Twice, with a different value: a left-to-right implementation would be
    // one message behind here (0 then 42) rather than wrong only on the first.
    trig->SetIntData(0, 7);
    CHECK(sink.received == 7);
  }

  // ─── format letters: what each outlet does with each input ──────────────────

  // "A number received in the inlet is sent out each outlet... The number will
  // be converted to int, float, list, symbol, or bang."
  TEST_CASE("trigger: an 'i' outlet sends ints and truncates floats (#466)") {
    Rig rig("i");
    rig.SendInt(-5);
    CHECK(rig.At(0).lastKind == OrderSink::INT);
    CHECK(rig.At(0).lastInt == -5);

    rig.Send(2.75f);
    CHECK(rig.At(0).lastInt == 2);
    // Towards zero, as C truncates — not floored.
    rig.Send(-2.75f);
    CHECK(rig.At(0).lastInt == -2);
  }

  // "A symbol, list, or bang received in the inlet will be converted to integer
  // 0 by an i outlet, and to float 0. by an f argument."
  TEST_CASE("trigger: a list or a bang reaches an 'i' outlet as 0 (#466)") {
    Rig rig("i");
    rig.SendInt(11);
    REQUIRE(rig.At(0).lastInt == 11);

    rig.Bang();
    CHECK(rig.At(0).lastKind == OrderSink::INT);
    CHECK(rig.At(0).lastInt == 0);

    rig.SendInt(11);
    // .trigger is not a list reader: the list "5 6" is 0 here, not 5. .route
    // and .sel are the objects that look inside a list.
    rig.List("5 6");
    CHECK(rig.At(0).lastInt == 0);
  }

  TEST_CASE("trigger: an 'f' outlet sends floats and widens ints (#466)") {
    Rig rig("f");
    rig.Send(2.5f);
    CHECK(rig.At(0).lastKind == OrderSink::FLOAT);
    CHECK(rig.At(0).lastFloat == doctest::Approx(2.5f));

    rig.SendInt(-3);
    CHECK(rig.At(0).lastFloat == doctest::Approx(-3.f));
  }

  TEST_CASE("trigger: a list or a bang reaches an 'f' outlet as 0. (#466)") {
    Rig rig("f");
    rig.Send(2.5f);
    REQUIRE(rig.At(0).lastFloat == doctest::Approx(2.5f));

    rig.Bang();
    CHECK(rig.At(0).lastFloat == doctest::Approx(0.f));

    rig.Send(2.5f);
    rig.List("text");
    CHECK(rig.At(0).lastFloat == doctest::Approx(0.f));
  }

  // C leaves a float-to-int conversion undefined outside the int range, and for
  // a NaN or an infinity. The shared ExprToInt answers all of them with 0.
  TEST_CASE("trigger: a non-finite or out-of-range float reaches an 'i' outlet as 0 (#466)") {
    Rig rig("i");
    rig.Send(NOT_A_NUMBER);
    CHECK(rig.At(0).lastInt == 0);
    rig.Send(INF);
    CHECK(rig.At(0).lastInt == 0);
    rig.Send(-INF);
    CHECK(rig.At(0).lastInt == 0);
    rig.Send(1e30f);
    CHECK(rig.At(0).lastInt == 0);

    // An 'f' outlet passes the same values on untouched: nothing is computed,
    // so nothing is substituted.
    Rig asFloat("f");
    asFloat.Send(INF);
    CHECK(std::isinf(asFloat.At(0).lastFloat));
    asFloat.Send(NOT_A_NUMBER);
    CHECK(std::isnan(asFloat.At(0).lastFloat));
  }

  // "Anything received in the inlet will be converted to bang before being sent
  // out a b outlet."
  TEST_CASE("trigger: a 'b' outlet sends a bang whatever arrived (#466)") {
    Rig rig("b");
    rig.SendInt(5);
    CHECK(rig.At(0).lastKind == OrderSink::BANG);
    rig.Send(0.5f);
    CHECK(rig.At(0).lastKind == OrderSink::BANG);
    rig.List("a b c");
    CHECK(rig.At(0).lastKind == OrderSink::BANG);
    rig.Bang();
    CHECK(rig.At(0).lastKind == OrderSink::BANG);
    CHECK(rig.At(0).count == 4);
  }

  // "A list received in the inlet will be sent out unchanged by an l outlet.
  // Anything else will be converted to the single-item list 0."
  TEST_CASE("trigger: an 'l' outlet passes a list through and substitutes 0 (#466)") {
    Rig rig("l");
    rig.List("60 100 note");
    CHECK(rig.At(0).lastKind == OrderSink::LIST);
    CHECK(rig.At(0).lastList == "60 100 note");

    rig.SendInt(5);
    CHECK(rig.At(0).lastList == "0");
    rig.Send(2.5f);
    CHECK(rig.At(0).lastList == "0");
    rig.Bang();
    CHECK(rig.At(0).lastList == "0");
  }

  // "A symbol received in the inlet will be sent out unchanged by an s outlet.
  // Anything else will be converted to the null symbol."
  TEST_CASE("trigger: an 's' outlet passes text through and substitutes the empty symbol (#466)") {
    Rig rig("s");
    rig.List("hello");
    CHECK(rig.At(0).lastKind == OrderSink::LIST);
    CHECK(rig.At(0).lastList == "hello");

    rig.SendInt(5);
    CHECK(rig.At(0).lastList.empty());
    rig.Send(2.5f);
    CHECK(rig.At(0).lastList.empty());
    rig.Bang();
    CHECK(rig.At(0).lastList.empty());
  }

  // The deviation, pinned so it cannot drift silently: this patcher has no
  // symbol message, so a list message satisfies both outlets. What survives of
  // Max's distinction is the substitution, and that is asserted above.
  TEST_CASE("trigger: 'l' and 's' both pass a list through unchanged (#466)") {
    Rig rig("l s");
    rig.List("alpha beta");
    CHECK(rig.At(0).lastList == "alpha beta");
    CHECK(rig.At(1).lastList == "alpha beta");

    // ...and differ on everything else.
    rig.Bang();
    CHECK(rig.At(0).lastList == "0");
    CHECK(rig.At(1).lastList.empty());
  }

  TEST_CASE("trigger: an empty list still reaches 'l' and 's' as itself (#466)") {
    Rig rig("l s i");
    rig.List("");
    CHECK(rig.At(0).lastKind == OrderSink::LIST);
    CHECK(rig.At(0).lastList.empty());
    CHECK(rig.At(1).lastList.empty());
    CHECK(rig.At(2).lastInt == 0);
  }

  // ─── constants ──────────────────────────────────────────────────────────────

  // Max: "When an int, float, or symbol is specified, the value is output as a
  // constant."
  TEST_CASE("trigger: an integer argument is an int constant (#466)") {
    Rig rig("5");
    CHECK(rig.op->SlotKind(0) == gTrigger::Kind::CONST_INT);
    CHECK(rig.op->SlotInt(0) == 5);
    CHECK(rig.op->GetOutputType(0) == YSE::OUT_TYPE::INT);

    rig.Bang();
    CHECK(rig.At(0).lastKind == OrderSink::INT);
    CHECK(rig.At(0).lastInt == 5);
  }

  // Spelling is what separates Max's int atom from its float atom, and the
  // parameter string round trips verbatim, so it survives a save/load.
  TEST_CASE("trigger: a '.' or an exponent makes a numeric argument a float constant (#466)") {
    Rig dot("5.");
    CHECK(dot.op->SlotKind(0) == gTrigger::Kind::CONST_FLOAT);
    CHECK(dot.op->GetOutputType(0) == YSE::OUT_TYPE::FLOAT);
    dot.Bang();
    CHECK(dot.At(0).lastKind == OrderSink::FLOAT);
    CHECK(dot.At(0).lastFloat == doctest::Approx(5.f));

    Rig fraction("-0.25");
    CHECK(fraction.op->SlotKind(0) == gTrigger::Kind::CONST_FLOAT);
    fraction.Bang();
    CHECK(fraction.At(0).lastFloat == doctest::Approx(-0.25f));

    Rig exponent("1e3");
    CHECK(exponent.op->SlotKind(0) == gTrigger::Kind::CONST_FLOAT);
    exponent.Bang();
    CHECK(exponent.At(0).lastFloat == doctest::Approx(1000.f));

    // And a plainly-spelled integer stays an int, including a negative one.
    Rig negative("-12");
    CHECK(negative.op->SlotKind(0) == gTrigger::Kind::CONST_INT);
    CHECK(negative.op->SlotInt(0) == -12);
  }

  TEST_CASE("trigger: a non-numeric argument is a symbol constant (#466)") {
    Rig rig("hello");
    CHECK(rig.op->SlotKind(0) == gTrigger::Kind::CONST_SYMBOL);
    CHECK(rig.op->SlotText(0) == "hello");
    CHECK(rig.op->GetOutputType(0) == YSE::OUT_TYPE::LIST);

    rig.SendInt(1);
    CHECK(rig.At(0).lastKind == OrderSink::LIST);
    CHECK(rig.At(0).lastList == "hello");
  }

  TEST_CASE("trigger: a constant ignores the input entirely (#466)") {
    Rig rig("5 2.5 tag");
    rig.Bang();
    rig.SendInt(99);
    rig.Send(-1.f);
    rig.List("anything at all");
    CHECK(rig.At(0).lastInt == 5);
    CHECK(rig.At(1).lastFloat == doctest::Approx(2.5f));
    CHECK(rig.At(2).lastList == "tag");
    CHECK(rig.At(0).count == 4);
    CHECK(rig.At(1).count == 4);
    CHECK(rig.At(2).count == 4);
  }

  // The format letters are single lowercase characters, as in Max. Anything
  // longer or differently cased is a symbol the user meant literally — Max has
  // no long spelling for these.
  TEST_CASE("trigger: only a lone lowercase letter is a format (#466)") {
    Rig upper("I F B L S");
    for (int i = 0; i < upper.Outlets(); i++) {
      CAPTURE(i);
      CHECK(upper.op->SlotKind(i) == gTrigger::Kind::CONST_SYMBOL);
    }
    upper.Bang();
    CHECK(upper.At(0).lastList == "I");

    Rig word("bang int float list");
    for (int i = 0; i < word.Outlets(); i++) {
      CAPTURE(i);
      CHECK(word.op->SlotKind(i) == gTrigger::Kind::CONST_SYMBOL);
    }
    word.SendInt(3);
    CHECK(word.At(0).lastList == "bang");
    CHECK(word.At(3).lastList == "list");
  }

  // The strict token reader shared with .sel: a token only counts as a number
  // when the *whole* of it does, and a non-finite reading is not a number at
  // all. Without both, `.trigger 5abc` would emit the int 5 and `.trigger inf`
  // an outlet whose "constant" is whatever strtof overflowed to.
  TEST_CASE("trigger: a partly-numeric or non-finite argument is a symbol constant (#466)") {
    Rig partial("5abc");
    CHECK(partial.op->SlotKind(0) == gTrigger::Kind::CONST_SYMBOL);
    partial.Bang();
    CHECK(partial.At(0).lastList == "5abc");

    Rig nonFinite("inf nan 1e999");
    CHECK(nonFinite.op->SlotKind(0) == gTrigger::Kind::CONST_SYMBOL);
    CHECK(nonFinite.op->SlotKind(1) == gTrigger::Kind::CONST_SYMBOL);
    CHECK(nonFinite.op->SlotKind(2) == gTrigger::Kind::CONST_SYMBOL);
    nonFinite.Bang();
    CHECK(nonFinite.At(0).lastList == "inf");
    CHECK(nonFinite.At(2).lastList == "1e999");
  }

  // An integer literal wider than an int would be undefined as a plain cast.
  TEST_CASE("trigger: an integer constant too wide for an int becomes 0 (#466)") {
    Rig rig("99999999999999999999");
    CHECK(rig.op->SlotKind(0) == gTrigger::Kind::CONST_INT);
    CHECK(rig.op->SlotInt(0) == 0);
    rig.Bang();
    CHECK(rig.At(0).lastInt == 0);
  }

  TEST_CASE("trigger: formats and constants mix in one argument list (#466)") {
    Rig rig("b i 5 note f");
    REQUIRE(rig.Outlets() == 5);
    CHECK(rig.op->SlotKind(0) == gTrigger::Kind::BANG);
    CHECK(rig.op->SlotKind(1) == gTrigger::Kind::INT);
    CHECK(rig.op->SlotKind(2) == gTrigger::Kind::CONST_INT);
    CHECK(rig.op->SlotKind(3) == gTrigger::Kind::CONST_SYMBOL);
    CHECK(rig.op->SlotKind(4) == gTrigger::Kind::FLOAT);

    rig.Send(7.5f);
    CHECK(rig.Log() == "edcba");
    CHECK(rig.At(0).lastKind == OrderSink::BANG);
    CHECK(rig.At(1).lastInt == 7);
    CHECK(rig.At(2).lastInt == 5);
    CHECK(rig.At(3).lastList == "note");
    CHECK(rig.At(4).lastFloat == doctest::Approx(7.5f));
  }

  // ─── parameter edges ────────────────────────────────────────────────────────

  TEST_CASE("trigger: a run of spaces in the argument does not create empty outlets (#466)") {
    Rig rig("b  i");
    CHECK(rig.op->SlotCount() == 2);
    CHECK(rig.Outlets() == 2);
    CHECK(rig.op->SlotKind(0) == gTrigger::Kind::BANG);
    CHECK(rig.op->SlotKind(1) == gTrigger::Kind::INT);
  }

  TEST_CASE("trigger: re-parsing an empty parameter string returns to two int outlets (#466)") {
    Rig rig("b l s");
    REQUIRE(rig.Outlets() == 3);

    rig.op->SetParams("");
    CHECK(rig.Outlets() == 2);
    CHECK(rig.op->SlotKind(0) == gTrigger::Kind::INT);
    CHECK(rig.op->SlotKind(1) == gTrigger::Kind::INT);

    rig.Wire();
    rig.SendInt(4);
    CHECK(rig.Log() == "ba");
    CHECK(rig.At(0).lastInt == 4);
  }

  TEST_CASE("trigger: re-parsing rebuilds the outlets and their types (#466)") {
    Rig rig("b");
    REQUIRE(rig.Outlets() == 1);

    rig.op->SetParams("i f l");
    CHECK(rig.Outlets() == 3);
    CHECK(rig.op->GetOutputType(0) == YSE::OUT_TYPE::INT);
    CHECK(rig.op->GetOutputType(1) == YSE::OUT_TYPE::FLOAT);
    CHECK(rig.op->GetOutputType(2) == YSE::OUT_TYPE::LIST);

    rig.Wire();
    rig.SendInt(2);
    CHECK(rig.Log() == "cba");
  }

  TEST_CASE("trigger: at most 256 outlets are built (#466)") {
    std::string args;
    for (int i = 0; i < 300; i++) {
      if (i > 0) args += " ";
      args += "b";
    }
    Rig rig(args);
    CHECK(rig.op->SlotCount() == gTrigger::MAX_OUTLETS);
    CHECK(rig.Outlets() == gTrigger::MAX_OUTLETS);

    rig.Bang();
    // Every one of them fires, exactly once.
    CHECK(rig.Total() == gTrigger::MAX_OUTLETS);
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  TEST_CASE("trigger: survives a DumpJSON / ParseJSON round trip (#466)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_TRIGGER);
    REQUIRE(h != nullptr);
    h->SetParams("b i 5 5. note");
    const std::string json = src.DumpJSON();
    CHECK(json.find(".trigger") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".trigger"));
    CHECK(copy->GetParams() == std::string("b i 5 5. note"));
    // The shape has to come back with it, or the saved patch cords land on the
    // wrong outlets...
    CHECK(copy->GetOutputs() == 5);
    // ...and so do the types, which is what pins that the int/float spelling of
    // a constant survives the trip.
    CHECK(copy->OutputDataType(0) == YSE::OUT_TYPE::BANG);
    CHECK(copy->OutputDataType(1) == YSE::OUT_TYPE::INT);
    CHECK(copy->OutputDataType(2) == YSE::OUT_TYPE::INT);
    CHECK(copy->OutputDataType(3) == YSE::OUT_TYPE::FLOAT);
    CHECK(copy->OutputDataType(4) == YSE::OUT_TYPE::LIST);
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category, the port shape and the parameter name,
  // which is what a binding generator keys on. It also pins that the outlets
  // built by the parse callback are documented — the coverage test only ever
  // sees a default-constructed object.

  TEST_CASE("trigger: documents itself as GENERIC with a labelled port set (#466)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_TRIGGER));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    CHECK_FALSE(obj->GetDescription().empty());

    REQUIRE(obj->NumInputs() == 1);
    CHECK(obj->GetInlet(0)->GetDocLabel() == "in");

    REQUIRE(obj->NumOutputs() == 2);
    CHECK(obj->GetOutlet(0)->GetDocLabel() == "out0");
    CHECK(obj->GetOutlet(1)->GetDocLabel() == "out1");

    REQUIRE(obj->GetParamDocs().size() == 1);
    CHECK(obj->GetParamDocs()[0].name == "formats");
    CHECK(obj->GetParamDocs()[0].defaultValue == "i i");
  }

  TEST_CASE("trigger: outlets created by the parse callback are documented too (#466)") {
    gTrigger op;
    op.SetParams("b i f l s 5 tag");
    REQUIRE(op.NumOutputs() == 7);
    for (int i = 0; i < op.NumOutputs(); i++) {
      CAPTURE(i);
      CHECK_FALSE(op.GetOutlet(i)->GetDocLabel().empty());
      CHECK_FALSE(op.GetOutlet(i)->GetDocDescription().empty());
      CHECK_FALSE(op.GetOutlet(i)->GetRange().empty());
    }
    CHECK(op.GetOutlet(6)->GetDocLabel() == "out6");
  }

  TEST_CASE("trigger: the inlet accepts bang, int, float and list (#466)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_TRIGGER));
    REQUIRE(obj != nullptr);
    const unsigned int in = obj->GetInlet(0)->GetAcceptedTypes();
    CHECK((in & YSE::PATCHER::IT_BANG) != 0);
    CHECK((in & YSE::PATCHER::IT_INT) != 0);
    CHECK((in & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((in & YSE::PATCHER::IT_LIST) != 0);
  }

} // TEST_SUITE("patcher")

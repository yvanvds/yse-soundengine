// Tests for .decode (issue #481) — send 1 out a selected outlet and 0 out every
// other one.
//
// Six rules carry this file, and each is one a plausible implementation gets
// wrong:
//
//   - **every outlet fires, every time.** The object emits a *state*, not a
//     value: an implementation that only touched the newly selected outlet
//     would pass every "outlet 2 got 1" test while leaving the outlet that was
//     selected a moment ago still reading 1, which is the one job .sel cannot
//     do and this object exists for.
//   - **the previously selected outlet goes to 0.** Same rule from the other
//     end, and the one that makes the bank mutually exclusive.
//   - **the hierarchy: right beats middle beats left.** Max's "the right inlet
//     overrides the middle inlet, and the middle inlet overrides numbers sent
//     to the left inlet". Any two of the three agree in most orders; only a
//     test that holds all three at once separates a real hierarchy from a
//     last-writer-wins.
//   - **the inlets are state, so the overrides are non-destructive.** A 1 and
//     then a 0 in the right inlet must leave the bank exactly as it was. An
//     implementation that cleared the selection on mute would look correct
//     until the unmute.
//   - **outlets fire right to left,** Max's universal order, so a downstream
//     collector sees the last outlet before outlet 0.
//   - **an index with no outlet is ignored,** .gate's, .cycle's and .spray's
//     discipline — neither wrapped nor allowed to blank the bank.
//
// The rest is the message grammar from the Max reference: the outlet-count
// creation argument, bang, the initially enabled left outlet, and the types the
// object does and does not accept.
//
// The standalone rigs wire objects directly, as every sibling suite does; the
// end-to-end cases run through a real patcher, including against a real bank of
// .gate objects — "provides hierarchical switching" is a claim about what the
// object does to a patch, not about what leaves its outlets.
//
// No audio device required.

#include <doctest/doctest.h>
#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/pEnums.h"
#include "patcher/inlet.h"
#include "patcher/genericObjects/gDecode.h"
#include "patcher/sinks.hpp"

namespace {

  using TestHelpers::FloatSink;
  using TestHelpers::IntSink;
  using YSE::PATCHER::gDecode;

  // Records (outlet, value) for every int the object sends, in send order, into
  // a log shared by the whole bank. Both halves of the contract are order
  // sensitive — "every outlet fires" is a count, "right to left" is a sequence —
  // so one sink per outlet writing into one log is the only rig that can see
  // both.
  struct OrderSink : YSE::PATCHER::pObject {
    std::vector<std::pair<int, int>>* log;
    int id;

    OrderSink(std::vector<std::pair<int, int>>* target, int outlet)
      : pObject(false), log(target), id(outlet) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt(
          [this](int v, int, YSE::THREAD) { log->emplace_back(this->id, v); });
    }
    const char* Type() const override {
      return "order_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  struct Rig {
    gDecode op;
    std::vector<std::pair<int, int>> log;
    std::vector<std::unique_ptr<OrderSink>> sinks;

    explicit Rig(const std::string& args = "") {
      if (!args.empty()) op.SetParams(args);
      for (int i = 0; i < op.OutletCount(); i++) {
        sinks.push_back(std::make_unique<OrderSink>(&log, i));
        op.ConnectOutlet(sinks.back()->GetInlet(0), i);
        sinks.back()->ConnectInlet(op.GetOutlet(i), 0);
      }
    }

    void Int(int value, int inlet = 0) {
      op.GetInlet(inlet)->SetInt(value, YSE::T_GUI);
    }
    void Bang(int inlet = 0) {
      op.GetInlet(inlet)->SetBang(YSE::T_GUI);
    }

    int Count() const {
      return (int)log.size();
    }
    void Reset() {
      log.clear();
    }

    // What the last burst put on each outlet, indexed by outlet. -1 marks an
    // outlet the burst did not reach at all, which is what separates "every
    // outlet fires" from "the two that changed fire".
    std::vector<int> Burst() const {
      std::vector<int> state((std::size_t)op.OutletCount(), -1);
      for (const std::pair<int, int>& entry : log) {
        if (entry.first >= 0 && entry.first < (int)state.size())
          state[(std::size_t)entry.first] = entry.second;
      }
      return state;
    }

    // The outlets in the order they fired.
    std::vector<int> Order() const {
      std::vector<int> order;
      order.reserve(log.size());
      for (const std::pair<int, int>& entry : log)
        order.push_back(entry.first);
      return order;
    }
  };

  // The one-hot vector a decoder with `count` outlets selecting `on` should
  // produce, so the expectation is written once rather than in every case.
  std::vector<int> OneHot(int count, int on) {
    std::vector<int> state((std::size_t)count, 0);
    if (on >= 0 && on < count) state[(std::size_t)on] = 1;
    return state;
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("decode: creatable through the registry (#481)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DECODE, "4");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".decode"));
    CHECK(h->GetInputs() == 3);
    CHECK(h->GetOutputs() == 4);
  }

  TEST_CASE("decode: listed by pRegistry::AllNames (#481)") {
    const std::vector<std::string> names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".decode")) != names.end());
  }

  TEST_CASE("decode: one outlet with no argument, as Max documents (#481)") {
    // Max: "Sets the number of outlets. The default is one outlet."
    gDecode op;
    CHECK(op.OutletCount() == gDecode::DEFAULT_PORTS);
    CHECK(op.OutletCount() == 1);
    CHECK(op.NumOutputs() == 1);
    // The three inlets are fixed: only the outlet count is an argument.
    CHECK(op.NumInputs() == 3);
  }

  TEST_CASE("decode: the outlet count is clamped to 1-256 (#481)") {
    {
      gDecode op;
      op.SetParams("0");
      CHECK(op.OutletCount() == gDecode::MIN_PORTS);
    }
    {
      gDecode op;
      op.SetParams("-5");
      CHECK(op.OutletCount() == gDecode::MIN_PORTS);
    }
    {
      gDecode op;
      op.SetParams("1000");
      CHECK(op.OutletCount() == gDecode::MAX_PORTS);
    }
    {
      // Max's argument list gives a float variant "converted to int".
      gDecode op;
      op.SetParams("4.9");
      CHECK(op.OutletCount() == 4);
    }
    {
      // Not a number at all: the default stands rather than a zero-outlet object.
      gDecode op;
      op.SetParams("banana");
      CHECK(op.OutletCount() == gDecode::DEFAULT_PORTS);
    }
    // The inlet count never moves whatever the argument says.
    gDecode op;
    op.SetParams("300");
    CHECK(op.NumInputs() == 3);
  }

  TEST_CASE("decode: SetParams(\"\") returns the object to Max's default shape (#481)") {
    gDecode op;
    op.SetParams("6");
    REQUIRE(op.OutletCount() == 6);
    op.SetParams("");
    CHECK(op.OutletCount() == gDecode::DEFAULT_PORTS);
    CHECK(op.NumOutputs() == gDecode::DEFAULT_PORTS);
  }

  TEST_CASE("decode: a re-parse that shrinks the bank moves the selection back (#481)") {
    // The selection has to name an outlet that exists, or the object would be
    // pointing past its own bank and no outlet would ever read 1.
    Rig rig("8");
    rig.Int(6);
    REQUIRE(rig.op.Index() == 6);
    rig.op.SetParams("2");
    CHECK(rig.op.OutletCount() == 2);
    CHECK(rig.op.Index() == 0);
    CHECK(rig.op.StateAt(0));
    CHECK_FALSE(rig.op.StateAt(1));
  }

  TEST_CASE("decode: every outlet is an int outlet (#481)") {
    // Each carries a 1 or a 0 the object computed, never a value it forwards.
    gDecode op;
    op.SetParams("3");
    for (int i = 0; i < op.NumOutputs(); i++) {
      CAPTURE(i);
      CHECK(op.GetOutputType(i) == YSE::OUT_TYPE::INT);
    }
  }

  TEST_CASE("decode: the left inlet takes bang and int; the other two take int (#481)") {
    // Max documents `bang` and `int` for this object and nothing else, and
    // scopes bang to the leftmost inlet. float, list and anything are declined
    // outright rather than swallowed by a handler that cannot log — .spray's
    // reading of the same situation.
    gDecode op;
    op.SetParams("3");
    REQUIRE(op.NumInputs() == 3);

    const unsigned int left = op.GetInlet(0)->GetAcceptedTypes();
    CHECK((left & YSE::PATCHER::IT_BANG) != 0);
    CHECK((left & YSE::PATCHER::IT_INT) != 0);
    CHECK((left & YSE::PATCHER::IT_FLOAT) == 0);
    CHECK((left & YSE::PATCHER::IT_LIST) == 0);

    for (int i = 1; i < 3; i++) {
      CAPTURE(i);
      const unsigned int in = op.GetInlet(i)->GetAcceptedTypes();
      CHECK((in & YSE::PATCHER::IT_INT) != 0);
      CHECK((in & YSE::PATCHER::IT_BANG) == 0);
      CHECK((in & YSE::PATCHER::IT_FLOAT) == 0);
      CHECK((in & YSE::PATCHER::IT_LIST) == 0);
    }
  }

  // ─── the decode ─────────────────────────────────────────────────────────────

  TEST_CASE("decode: an index puts 1 on that outlet and 0 on every other (#481)") {
    // The object.
    Rig rig("4");

    rig.Int(2);
    CHECK(rig.Burst() == OneHot(4, 2));
    // Every outlet, not just the one that changed: the state is the output.
    CHECK(rig.Count() == 4);
  }

  TEST_CASE("decode: the previously selected outlet is turned off (#481)") {
    // The whole difference from .sel, which reports the arrival of a state and
    // never its departure. An implementation that only touched the new outlet
    // would leave outlet 1 reading 1 here.
    Rig rig("4");

    rig.Int(1);
    REQUIRE(rig.Burst() == OneHot(4, 1));

    rig.Reset();
    rig.Int(3);
    const std::vector<int> state = rig.Burst();
    CHECK(state == OneHot(4, 3));
    CHECK(state[1] == 0);
    CHECK(rig.Count() == 4);
  }

  TEST_CASE("decode: re-selecting the same outlet restates the whole bank (#481)") {
    // Idempotent: a downstream bank always agrees with the decoder, whatever it
    // missed earlier.
    Rig rig("3");
    rig.Int(1);
    rig.Reset();
    rig.Int(1);
    CHECK(rig.Burst() == OneHot(3, 1));
    CHECK(rig.Count() == 3);
  }

  TEST_CASE("decode: outlets fire right to left (#481)") {
    // Max's universal order.
    Rig rig("4");
    rig.Int(1);
    CHECK(rig.Order() == std::vector<int>{3, 2, 1, 0});
  }

  TEST_CASE("decode: the left outlet is initially enabled (#481)") {
    // Max's words. The object starts in a state rather than in none, so a bank
    // wired to a fresh .decode is never ambiguous.
    gDecode op;
    op.SetParams("4");
    CHECK(op.Index() == 0);
    CHECK(op.StateAt(0));
    CHECK_FALSE(op.StateAt(1));
    CHECK_FALSE(op.StateAt(3));
    // And an outlet it does not have is not in any state.
    CHECK_FALSE(op.StateAt(4));
    CHECK_FALSE(op.StateAt(-1));
  }

  TEST_CASE("decode: an index with no outlet is ignored (#481)") {
    // .gate's, .cycle's and .spray's discipline: an out-of-range index is a
    // miscount in the patch, and wrapping it — or blanking the bank — would hide
    // it at the object best placed to expose it.
    Rig rig("4");
    rig.Int(2);
    rig.Reset();

    rig.Int(4);
    CHECK(rig.Count() == 0);
    CHECK(rig.op.Index() == 2);

    rig.Int(9999);
    CHECK(rig.Count() == 0);

    rig.Int(-1);
    CHECK(rig.Count() == 0);
    CHECK(rig.op.Index() == 2);

    // And the selection that was there is still the one a bang reports.
    rig.Bang();
    CHECK(rig.Burst() == OneHot(4, 2));
  }

  // ─── bang ───────────────────────────────────────────────────────────────────

  TEST_CASE("decode: bang outputs the current state and changes nothing (#481)") {
    // Max: "The message bang causes decode to output its current state."
    Rig rig("3");
    rig.Int(2);
    rig.Reset();

    rig.Bang();
    CHECK(rig.Burst() == OneHot(3, 2));
    CHECK(rig.Count() == 3);
    CHECK(rig.op.Index() == 2);

    rig.Reset();
    rig.Bang();
    CHECK(rig.Burst() == OneHot(3, 2));
  }

  TEST_CASE("decode: bang before anything else reports the left outlet (#481)") {
    Rig rig("3");
    rig.Bang();
    CHECK(rig.Burst() == OneHot(3, 0));
  }

  TEST_CASE("decode: bang is scoped to the left inlet (#481)") {
    // The middle and right inlets document an int method and nothing else.
    Rig rig("3");
    rig.Bang(1);
    rig.Bang(2);
    CHECK(rig.Count() == 0);
  }

  // ─── the middle inlet: all on ───────────────────────────────────────────────

  TEST_CASE("decode: a number greater than 0 in the middle inlet turns every outlet on (#481)") {
    // Max: "a number greater than 0 received in the middle inlet sends a 1 out
    // all outlets."
    Rig rig("4");
    rig.Int(2);
    rig.Reset();

    rig.Int(1, 1);
    CHECK(rig.Burst() == std::vector<int>{1, 1, 1, 1});
    CHECK(rig.Count() == 4);

    // Any number greater than 0, not just 1.
    rig.Reset();
    rig.Int(7, 1);
    CHECK(rig.Burst() == std::vector<int>{1, 1, 1, 1});
  }

  TEST_CASE("decode: 0 in the middle inlet hands control back to the selection (#481)") {
    // Max: "If 0 is received in the middle inlet, decode sends a 1 out the last
    // outlet decoded by a number received in the left inlet, and 0 out all other
    // outlets." The selection it hands back to is the one from before the
    // override, which is what makes the override non-destructive.
    Rig rig("4");
    rig.Int(2);
    rig.Int(1, 1);
    rig.Reset();

    rig.Int(0, 1);
    CHECK(rig.Burst() == OneHot(4, 2));
    CHECK(rig.op.Index() == 2);
  }

  TEST_CASE("decode: a number below 0 in the middle inlet is not 'all on' (#481)") {
    // Max words it as "greater than 0", strictly, so a negative falls through to
    // the selection exactly as 0 does.
    Rig rig("3");
    rig.Int(1);
    rig.Reset();
    rig.Int(-4, 1);
    CHECK(rig.Burst() == OneHot(3, 1));
  }

  TEST_CASE("decode: the middle inlet overrides the left one (#481)") {
    // Max: "the middle inlet overrides numbers sent to the left inlet that turn
    // individual outlets on or off." The selection is still recorded — it is
    // just not what the outlets are showing.
    Rig rig("4");
    rig.Int(5, 1); // all on
    rig.Reset();

    rig.Int(3);
    CHECK(rig.Burst() == std::vector<int>{1, 1, 1, 1});
    CHECK(rig.op.Index() == 3);

    // And when the override lifts, the selection made underneath it is the one
    // that appears.
    rig.Reset();
    rig.Int(0, 1);
    CHECK(rig.Burst() == OneHot(4, 3));
  }

  // ─── the right inlet: master mute ───────────────────────────────────────────

  TEST_CASE("decode: a non-zero number in the right inlet turns every outlet off (#481)") {
    // Max: "Any positive number other than 0 sends a 0 out all outlets."
    Rig rig("4");
    rig.Int(2);
    rig.Reset();

    rig.Int(1, 2);
    CHECK(rig.Burst() == std::vector<int>{0, 0, 0, 0});
    CHECK(rig.Count() == 4);

    // Whichever sign it has: -1 meaning the opposite of 1 in an inlet whose
    // whole vocabulary is on and off would surprise every patch.
    rig.Reset();
    rig.Int(0, 2);
    rig.Reset();
    rig.Int(-1, 2);
    CHECK(rig.Burst() == std::vector<int>{0, 0, 0, 0});
  }

  TEST_CASE("decode: the mute is non-destructive (#481)") {
    // Max: "When decode receives a 0 in its right inlet, it outputs 0 or 1 out
    // its outlets based on the values last received in the middle and left
    // inlets." The inlets are state, so a 1 and then a 0 leave the bank exactly
    // as it was — without the patch re-sending the index.
    Rig rig("5");
    rig.Int(3);
    rig.Int(1, 2);
    rig.Reset();

    rig.Int(0, 2);
    CHECK(rig.Burst() == OneHot(5, 3));
    CHECK(rig.op.Index() == 3);
  }

  TEST_CASE("decode: the right inlet overrides the middle one (#481)") {
    // Max: "The right inlet overrides the middle inlet", and "If a 1 was last
    // received in the right inlet, any number received in the middle inlet will
    // send a 0 out all outlets."
    Rig rig("3");
    rig.Int(1, 2); // muted
    rig.Reset();

    rig.Int(1, 1); // "all on", but the mute is above it
    CHECK(rig.Burst() == std::vector<int>{0, 0, 0});

    rig.Reset();
    rig.Int(0, 1);
    CHECK(rig.Burst() == std::vector<int>{0, 0, 0});

    // And the middle inlet's value was recorded all along: lifting the mute
    // shows what it had been saying.
    rig.Reset();
    rig.Int(5, 1);
    rig.Reset();
    rig.Int(0, 2);
    CHECK(rig.Burst() == std::vector<int>{1, 1, 1});
  }

  TEST_CASE("decode: the left inlet still fires while muted, and still records (#481)") {
    // Every accepted message restates the bank, so the burst is real — it just
    // says what the hierarchy says. The index underneath survives the mute.
    Rig rig("4");
    rig.Int(1, 2);
    rig.Reset();

    rig.Int(2);
    CHECK(rig.Burst() == std::vector<int>{0, 0, 0, 0});
    CHECK(rig.Count() == 4);
    CHECK(rig.op.Index() == 2);

    rig.Reset();
    rig.Int(0, 2);
    CHECK(rig.Burst() == OneHot(4, 2));
  }

  TEST_CASE("decode: all three inlets held at once resolve in Max's order (#481)") {
    // The case that separates a real hierarchy from a last-writer-wins: the
    // three values are all set, and only the ordering of the rules decides what
    // comes out.
    Rig rig("3");
    rig.Int(2); // left: outlet 2
    rig.Int(1, 1); // middle: all on
    rig.Int(1, 2); // right: all off

    CHECK(rig.op.Index() == 2);
    CHECK(rig.op.Secondary() == 1);
    CHECK(rig.op.Primary() == 1);

    rig.Reset();
    rig.Bang();
    CHECK(rig.Burst() == std::vector<int>{0, 0, 0}); // right wins

    rig.Int(0, 2);
    rig.Reset();
    rig.Bang();
    CHECK(rig.Burst() == std::vector<int>{1, 1, 1}); // middle wins

    rig.Int(0, 1);
    rig.Reset();
    rig.Bang();
    CHECK(rig.Burst() == OneHot(3, 2)); // left, at last
  }

  // ─── real-time behaviour ────────────────────────────────────────────────────

  TEST_CASE("decode: Calculate() emits nothing (#481)") {
    // The object is driven by its inlets. An emitting Calculate() would restate
    // the bank from a stimulus no patch sent.
    Rig rig("3");
    rig.Int(1);
    rig.Reset();
    for (int i = 0; i < 8; i++)
      rig.op.Calculate(YSE::T_DSP);
    CHECK(rig.Count() == 0);
  }

  // ─── persistence ────────────────────────────────────────────────────────────

  TEST_CASE("decode: survives a DumpJSON / ParseJSON round trip (#481)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_DECODE);
    REQUIRE(h != nullptr);
    h->SetParams("5");
    const std::string json = src.DumpJSON();
    CHECK(json.find(".decode") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".decode"));
    CHECK(copy->GetParams() == std::string("5"));
    // The shape has to come back with it, or the saved patch cords land on the
    // wrong outlets.
    CHECK(copy->GetInputs() == 3);
    CHECK(copy->GetOutputs() == 5);

    // And the restored object decodes, which the parameter string alone does not
    // prove: outlet 4 has to be reachable.
    IntSink last;
    YSE::pHandle lastHandle(&last);
    loaded.Connect(copy, 4, &lastHandle, 0);
    copy->SetIntData(0, 4);
    CHECK(last.gotInt);
    CHECK(last.received == 1);
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category, the port shape and the parameter name,
  // which is what a binding generator keys on. It also pins that the outlets
  // built by the parse callback are documented — the coverage test only ever
  // sees a default-constructed object.

  TEST_CASE("decode: documents itself as GENERIC with a labelled port set (#481)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_DECODE));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    CHECK_FALSE(obj->GetDescription().empty());

    REQUIRE(obj->NumInputs() == 3);
    CHECK(obj->GetInlet(0)->GetDocLabel() == "select");
    CHECK(obj->GetInlet(1)->GetDocLabel() == "secondary");
    CHECK(obj->GetInlet(2)->GetDocLabel() == "primary");

    REQUIRE(obj->NumOutputs() == 1);
    CHECK(obj->GetOutlet(0)->GetDocLabel() == "out0");

    REQUIRE(obj->GetParamDocs().size() == 1);
    CHECK(obj->GetParamDocs()[0].name == "outlets");
    CHECK(obj->GetParamDocs()[0].defaultValue == "1");
  }

  TEST_CASE("decode: outlets created by the parse callback are documented too (#481)") {
    gDecode op;
    op.SetParams("7");
    REQUIRE(op.NumOutputs() == 7);
    for (int i = 0; i < 7; i++) {
      CAPTURE(i);
      CHECK_FALSE(op.GetOutlet(i)->GetDocLabel().empty());
      CHECK_FALSE(op.GetOutlet(i)->GetDocDescription().empty());
      CHECK_FALSE(op.GetOutlet(i)->GetRange().empty());
    }
    CHECK(op.GetOutlet(6)->GetDocLabel() == "out6");
    for (int i = 0; i < 3; i++) {
      CAPTURE(i);
      CHECK_FALSE(op.GetInlet(i)->GetDocDescription().empty());
      CHECK_FALSE(op.GetInlet(i)->GetRange().empty());
    }
  }

  // ─── capacity ───────────────────────────────────────────────────────────────

  TEST_CASE("decode: at most 256 outlets are built, and the last one decodes (#481)") {
    Rig rig("300");
    REQUIRE(rig.op.OutletCount() == gDecode::MAX_PORTS);
    REQUIRE(rig.op.NumOutputs() == gDecode::MAX_PORTS);

    rig.Int(gDecode::MAX_PORTS - 1);
    CHECK(rig.Count() == gDecode::MAX_PORTS);
    CHECK(rig.Burst() == OneHot(gDecode::MAX_PORTS, gDecode::MAX_PORTS - 1));
    // One past the last is still out of range, at the boundary as anywhere else.
    rig.Reset();
    rig.Int(gDecode::MAX_PORTS);
    CHECK(rig.Count() == 0);
  }

  // ─── end to end ─────────────────────────────────────────────────────────────

  TEST_CASE("decode: one-hot across a real patcher's cords (#481)") {
    // The state as a patch sees it: four sinks on four cords, exactly one of
    // them reading 1 at any moment, and the one that was reading 1 dropping to 0
    // by itself.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* decode = p.CreateObject(YSE::OBJ::G_DECODE, "4");
    REQUIRE(decode != nullptr);
    REQUIRE(decode->GetOutputs() == 4);

    IntSink a, b, c, d;
    YSE::pHandle aH(&a), bH(&b), cH(&c), dH(&d);
    p.Connect(decode, 0, &aH, 0);
    p.Connect(decode, 1, &bH, 0);
    p.Connect(decode, 2, &cH, 0);
    p.Connect(decode, 3, &dH, 0);

    decode->SetIntData(0, 2);
    CHECK(a.received == 0);
    CHECK(b.received == 0);
    CHECK(c.received == 1);
    CHECK(d.received == 0);

    decode->SetIntData(0, 0);
    CHECK(a.received == 1);
    CHECK(c.received == 0); // the one that was on turned itself off
    CHECK(b.received == 0);
    CHECK(d.received == 0);
  }

  TEST_CASE("decode drives a bank of gates as Max's hierarchical switch (#481)") {
    // The headline use, end to end through real objects: a .decode into the
    // select inlets of three .gate objects is the "hierarchical switching" Max's
    // description names, and it is a claim about a patch rather than about what
    // leaves one object's outlets. A .gate passes when its select reads 1 and
    // mutes when it reads 0, so this asserts the decoder against a consumer that
    // was written without it in mind.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* decode = p.CreateObject(YSE::OBJ::G_DECODE, "3");
    YSE::pHandle* gateA = p.CreateObject(YSE::OBJ::G_GATE);
    YSE::pHandle* gateB = p.CreateObject(YSE::OBJ::G_GATE);
    YSE::pHandle* gateC = p.CreateObject(YSE::OBJ::G_GATE);
    REQUIRE(decode != nullptr);
    REQUIRE(gateA != nullptr);
    REQUIRE(gateB != nullptr);
    REQUIRE(gateC != nullptr);

    FloatSink a, b, c;
    YSE::pHandle aH(&a), bH(&b), cH(&c);

    p.Connect(decode, 0, gateA, 0); // decode outlet -> gate select inlet
    p.Connect(decode, 1, gateB, 0);
    p.Connect(decode, 2, gateC, 0);
    p.Connect(gateA, 0, &aH, 0);
    p.Connect(gateB, 0, &bH, 0);
    p.Connect(gateC, 0, &cH, 0);

    // Fires all three gates' value inlets with the same number, the way a patch
    // fans one stream across a bank.
    auto feed = [&](float value) {
      gateA->SetFloatData(1, value);
      gateB->SetFloatData(1, value);
      gateC->SetFloatData(1, value);
    };

    // Select the middle member of the bank: only it passes.
    decode->SetIntData(0, 1);
    feed(60.f);
    CHECK(b.gotFloat);
    CHECK(b.received == doctest::Approx(60.f));
    CHECK_FALSE(a.gotFloat);
    CHECK_FALSE(c.gotFloat);

    // Move the selection: the old member closes without the patch closing it.
    decode->SetIntData(0, 2);
    feed(61.f);
    CHECK(c.received == doctest::Approx(61.f));
    CHECK(b.received == doctest::Approx(60.f)); // b stayed shut
    CHECK_FALSE(a.gotFloat);

    // The right inlet mutes the whole bank.
    decode->SetIntData(2, 1);
    feed(62.f);
    CHECK(c.received == doctest::Approx(61.f));
    CHECK_FALSE(a.gotFloat);

    // And lifting it restores the selection that was underneath, untouched.
    decode->SetIntData(2, 0);
    feed(63.f);
    CHECK(c.received == doctest::Approx(63.f));
    CHECK_FALSE(a.gotFloat);

    // The middle inlet opens every member at once.
    decode->SetIntData(1, 1);
    feed(64.f);
    CHECK(a.received == doctest::Approx(64.f));
    CHECK(b.received == doctest::Approx(64.f));
    CHECK(c.received == doctest::Approx(64.f));
  }

} // TEST_SUITE("patcher")

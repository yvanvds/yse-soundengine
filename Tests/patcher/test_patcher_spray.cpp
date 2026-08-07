// Tests for .spray (issue #479) — distribute the values of a list to numbered
// outlets.
//
// Six rules carry this file, and each is one a plausible implementation gets
// wrong:
//
//   - **the index and the values arrive in one message.** That is the whole
//     reason the object exists rather than being a .gate: an implementation
//     that latched the index from one message and applied it to the next would
//     pass every single-list test while being a different object.
//   - **the elements spread rightwards from the index.** Max: "If there are
//     additional elements in the list, they are sent out the subsequent outlets
//     to the right of the one specified by the first number in the list."
//   - **the outlets fire right to left.** Max's universal order, stated
//     outright in this object's own description. A test that only reads the
//     final values cannot tell it from its reverse, so the order is logged.
//   - **the burst is captured before the first send.** The send path is
//     synchronous, so a patch looping an outlet back into the inlet re-enters
//     mid-burst, and the interrupted burst must go on emitting the values it
//     started with — which a shared member buffer would not.
//   - **-1 is tested before the offset is applied.** Otherwise an `offset -1`
//     silently turns every broadcast into an ordinary index.
//   - **a symbol is ignored in place.** Max: "The list may contain only ints or
//     floats; symbols will be ignored." Closing the gap instead would move every
//     later element one outlet to the left in an object whose contract is
//     positional.
//
// The rest is the message grammar from the Max reference: the outlet count,
// offset and list-mode creation arguments, the `offset` message, and the
// messages spray has no method for.
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
#include "patcher/pEnums.h"
#include "patcher/inlet.h"
#include "patcher/genericObjects/gSpray.h"
#include "patcher/sinks.hpp"

namespace {

  using TestHelpers::FloatSink;
  using TestHelpers::ListSink;
  using TestHelpers::OrderSink;
  using YSE::PATCHER::gSpray;

  // One order-logging sink per outlet, all sharing one log, so "which outlet
  // received what, and in what order" is an assertion rather than an inference.
  // Outlet i is tagged 'a' + i, so a single input reads back as the firing
  // order itself.
  struct Rig {
    gSpray op;
    std::vector<std::unique_ptr<OrderSink>> sinks;
    std::vector<char> order;

    explicit Rig(const std::string& args = "") {
      if (!args.empty()) op.SetParams(args);
      for (int i = 0; i < op.OutletCount(); i++) {
        sinks.push_back(std::unique_ptr<OrderSink>(new OrderSink()));
        sinks.back()->tag = (char)('a' + (i % 26));
        sinks.back()->log = &order;
        op.ConnectOutlet(sinks.back()->GetInlet(0), i);
        sinks.back()->ConnectInlet(op.GetOutlet(i), 0);
      }
    }

    void List(const std::string& text, int inlet = 0) {
      op.GetInlet(inlet)->SetList(text, YSE::T_GUI);
    }

    std::string Log() const {
      return std::string(order.begin(), order.end());
    }
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

  // A sink that pushes a list back into the object under test, a bounded number
  // of times. A .spray wired straight back into its own inlet would not
  // terminate, which is exactly why the re-entrancy case needs a receiver that
  // stops feeding rather than a bare patch cord.
  struct FeedbackSink : YSE::PATCHER::pObject {
    gSpray* target = nullptr;
    std::string fireList;
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
          target->GetInlet(0)->SetList(fireList, thread);
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

  TEST_CASE("spray: creatable through the registry (#479)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_SPRAY, "4");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".spray"));
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 4);
  }

  TEST_CASE("spray: listed by pRegistry::AllNames (#479)") {
    auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(YSE::OBJ::G_SPRAY)) != names.end());
  }

  TEST_CASE("spray: two outlets with no argument, as Max documents (#479)") {
    // Max: "If there is no argument present, the object has two outlets." The
    // odd one out in this family — .cycle and .bucket default to one — because a
    // one-outlet spray cannot distribute anything.
    gSpray op;
    CHECK(op.OutletCount() == 2);
    CHECK(op.Offset() == 0);
    CHECK_FALSE(op.ListMode());
  }

  TEST_CASE("spray: the outlet count is clamped to 1-256 (#479)") {
    gSpray low;
    low.SetParams("0");
    CHECK(low.OutletCount() == 1);

    gSpray negative;
    negative.SetParams("-7");
    CHECK(negative.OutletCount() == 1);

    gSpray high;
    high.SetParams("9999");
    CHECK(high.OutletCount() == gSpray::MAX_PORTS);

    // A float argument is truncated, and a non-number leaves the default.
    gSpray fractional;
    fractional.SetParams("3.9");
    CHECK(fractional.OutletCount() == 3);

    gSpray nonsense;
    nonsense.SetParams("wide");
    CHECK(nonsense.OutletCount() == gSpray::DEFAULT_PORTS);
  }

  TEST_CASE("spray: the second and third arguments are offset and list mode (#479)") {
    gSpray op;
    op.SetParams("4 3 1");
    CHECK(op.OutletCount() == 4);
    CHECK(op.Offset() == 3);
    CHECK(op.ListMode());

    // Any non-zero third argument means list mode, as .cycle and .bucket read
    // their second.
    gSpray other;
    other.SetParams("4 -2 7");
    CHECK(other.Offset() == -2);
    CHECK(other.ListMode());

    gSpray plain;
    plain.SetParams("4 3 0");
    CHECK_FALSE(plain.ListMode());
  }

  TEST_CASE("spray: SetParams(\"\") returns the object to Max's default shape (#479)") {
    gSpray op;
    op.SetParams("5 2 1");
    REQUIRE(op.OutletCount() == 5);
    REQUIRE(op.Offset() == 2);
    REQUIRE(op.ListMode());

    op.SetParams("");
    CHECK(op.OutletCount() == gSpray::DEFAULT_PORTS);
    CHECK(op.Offset() == 0);
    CHECK_FALSE(op.ListMode());
  }

  TEST_CASE("spray: every outlet is ANY, since an element returns its own kind (#479)") {
    gSpray op;
    op.SetParams("3");
    for (int i = 0; i < op.OutletCount(); i++)
      CHECK(op.GetOutputType(i) == YSE::OUT_TYPE::ANY);
  }

  TEST_CASE("spray: the inlet accepts a list and nothing else (#479)") {
    // Max's int method exists only to post "spray requires a list", and there is
    // no float or bang method at all. The patcher has no per-message console and
    // a message handler must not log, so the inlet declines the types outright
    // and GetAcceptedTypes reports the object's real contract.
    gSpray op;
    const unsigned int accepted = op.GetInlet(0)->GetAcceptedTypes();
    CHECK((accepted & YSE::PATCHER::IT_LIST) != 0u);
    CHECK((accepted & YSE::PATCHER::IT_INT) == 0u);
    CHECK((accepted & YSE::PATCHER::IT_FLOAT) == 0u);
    CHECK((accepted & YSE::PATCHER::IT_BANG) == 0u);
  }

  // ─── the distribution ───────────────────────────────────────────────────────

  TEST_CASE("spray: the first number picks the outlet, the second is the value (#479)") {
    // The object. Max: "The first number in the list is a number that specifies
    // the outlet number starting at 0 for the leftmost outlet; the second is an
    // int or float value to send out that outlet."
    Rig rig("4");

    rig.List("2 60");
    CHECK(rig.Log() == "c");
    CHECK(rig.sinks[2]->lastInt == 60);
    CHECK(rig.Total() == 1);

    // And the destination is a property of the *message*, not of the object:
    // the very next list goes somewhere else with nothing set in between, which
    // is what separates this from .gate.
    rig.Reset();
    rig.List("0 61");
    CHECK(rig.Log() == "a");
    CHECK(rig.sinks[0]->lastInt == 61);
    CHECK(rig.Total() == 1);
  }

  TEST_CASE("spray: extra elements spread to the outlets on the right (#479)") {
    // Max: "If there are additional elements in the list, they are sent out the
    // subsequent outlets to the right of the one specified by the first number."
    Rig rig("4");
    rig.List("1 10 20 30");

    CHECK(rig.sinks[1]->lastInt == 10);
    CHECK(rig.sinks[2]->lastInt == 20);
    CHECK(rig.sinks[3]->lastInt == 30);
    CHECK(rig.sinks[0]->count == 0);
    CHECK(rig.Total() == 3);
  }

  TEST_CASE("spray: the addressed outlets fire right to left (#479)") {
    // Max's universal order, and this object's description says it outright:
    // "sent out that outlet and those to its right, in right-to-left order". A
    // test that only read the final values could not tell this from its reverse.
    Rig rig("5");
    rig.List("1 10 20 30");
    CHECK(rig.Log() == "dcb");

    // Including the whole-object case, so the order is not an artefact of
    // starting part-way along.
    rig.Reset();
    rig.List("0 1 2 3 4 5");
    CHECK(rig.Log() == "edcba");
  }

  TEST_CASE("spray: elements with no outlet are dropped, not wrapped (#479)") {
    // An index the object has no outlet for is a miscount in the patch, and
    // folding it back onto a real outlet would hide it — .gate's and .cycle's
    // discipline for the same situation.
    Rig rig("3");

    rig.List("1 10 20 30 40");
    CHECK(rig.sinks[1]->lastInt == 10);
    CHECK(rig.sinks[2]->lastInt == 20);
    CHECK(rig.Total() == 2); // 30 and 40 had nowhere to go

    // An index past the last outlet sends nothing at all.
    rig.Reset();
    rig.List("3 99");
    CHECK(rig.Total() == 0);
    rig.List("40 99");
    CHECK(rig.Total() == 0);
  }

  TEST_CASE("spray: each element leaves as the kind it was spelled as (#479)") {
    // The discipline .trigger, .bondo, .cycle and .bucket share: an object that
    // forwards must not rewrite the type of what passes through it, or 0 1 2
    // arrives downstream as 1. 2.
    Rig rig("4");
    rig.List("0 1 2.5 3e2 4");

    CHECK(rig.sinks[0]->lastKind == OrderSink::INT);
    CHECK(rig.sinks[0]->lastInt == 1);
    CHECK(rig.sinks[1]->lastKind == OrderSink::FLOAT);
    CHECK(rig.sinks[1]->lastFloat == doctest::Approx(2.5f));
    // Max reads an exponent as a float too, which is what TokenLooksLikeFloat
    // answers.
    CHECK(rig.sinks[2]->lastKind == OrderSink::FLOAT);
    CHECK(rig.sinks[2]->lastFloat == doctest::Approx(300.f));
    CHECK(rig.sinks[3]->lastKind == OrderSink::INT);
    CHECK(rig.sinks[3]->lastInt == 4);
  }

  TEST_CASE("spray: a symbol is ignored in place, keeping later elements aligned (#479)") {
    // Max: "The list may contain only ints or floats; symbols will be ignored."
    // The outlet is spent and the elements after it keep their positions.
    // Closing the gap would put 30 on outlet 1 — a stray symbol should cost one
    // destination, not silently rewrite the destination of all the rest.
    Rig rig("4");
    rig.List("0 10 hello 30");

    CHECK(rig.sinks[0]->lastInt == 10);
    CHECK(rig.sinks[1]->count == 0);
    CHECK(rig.sinks[2]->lastInt == 30);
    CHECK(rig.sinks[3]->count == 0);
    CHECK(rig.Total() == 2);
    CHECK(rig.Log() == "ca");

    // A list of nothing but symbols sends nothing.
    rig.Reset();
    rig.List("0 one two");
    CHECK(rig.Total() == 0);
  }

  TEST_CASE("spray: a fractional index is truncated (#479)") {
    Rig rig("4");
    rig.List("2.7 60");
    CHECK(rig.sinks[2]->lastInt == 60);
    CHECK(rig.Total() == 1);
  }

  // ─── -1, the broadcast ──────────────────────────────────────────────────────

  TEST_CASE("spray: an index of -1 repeats the values to every outlet (#479)") {
    // Max: "If the first number is -1, the remaining elements of the list will
    // be repeated to all outlets."
    Rig rig("4");
    rig.List("-1 60");

    CHECK(rig.Log() == "dcba");
    for (int i = 0; i < 4; i++) {
      CHECK(rig.sinks[i]->lastInt == 60);
      CHECK(rig.sinks[i]->count == 1);
    }
  }

  TEST_CASE("spray: a multi-value broadcast repeats around the outlets (#479)") {
    // "Repeated" taken as repeated *around*: element i % count goes out outlet
    // i, so -1 0 1 sets alternate outlets on a bank and a single value still
    // reaches all of them.
    Rig rig("5");
    rig.List("-1 7 8");

    CHECK(rig.Log() == "edcba");
    CHECK(rig.sinks[0]->lastInt == 7);
    CHECK(rig.sinks[1]->lastInt == 8);
    CHECK(rig.sinks[2]->lastInt == 7);
    CHECK(rig.sinks[3]->lastInt == 8);
    CHECK(rig.sinks[4]->lastInt == 7);
  }

  TEST_CASE("spray: a broadcast keeps each value's spelling (#479)") {
    Rig rig("2");
    rig.List("-1 1.5");
    CHECK(rig.sinks[0]->lastKind == OrderSink::FLOAT);
    CHECK(rig.sinks[1]->lastKind == OrderSink::FLOAT);
    CHECK(rig.sinks[0]->lastFloat == doctest::Approx(1.5f));
  }

  TEST_CASE("spray: a broadcast with nothing numeric to repeat sends nothing (#479)") {
    Rig rig("3");
    rig.List("-1 nope");
    CHECK(rig.Total() == 0);
  }

  // ─── the offset ─────────────────────────────────────────────────────────────

  TEST_CASE("spray: the second creation argument offsets the outlet numbering (#479)") {
    // Max: "The second argument sets an offset for the numbering of the outlets.
    // If the second argument is not present, the outlets are numbered beginning
    // with 0." So with an offset of 1 the index 1 reaches outlet 0 — the shape
    // of a patch spraying MIDI channels, which start at 1.
    Rig rig("4 1");
    REQUIRE(rig.op.Offset() == 1);

    rig.List("1 60");
    CHECK(rig.sinks[0]->lastInt == 60);
    CHECK(rig.Total() == 1);

    // And the index the un-offset object would have used now falls off the left.
    rig.Reset();
    rig.List("0 60");
    CHECK(rig.Total() == 0);
  }

  TEST_CASE("spray: elements that fall off the left are dropped, the rest still land (#479)") {
    // The offset can put the start of a burst before outlet 0. The elements that
    // fell off are dropped and the ones that reach the object keep the outlets
    // their positions give them, exactly as the elements past the right end do.
    Rig rig("3 2");
    rig.List("0 10 20 30 40");

    CHECK(rig.sinks[0]->lastInt == 30);
    CHECK(rig.sinks[1]->lastInt == 40);
    CHECK(rig.Total() == 2);
    CHECK(rig.Log() == "ba");
  }

  TEST_CASE("spray: the 'offset' message shifts the numbering at run time (#479)") {
    // Max: "The word offset followed by a number will offset the output of the
    // object by the number of outlets given shifted to the left (a negative
    // number will specify the number of outlets offset to the right)."
    Rig rig("4");

    rig.List("offset 2");
    CHECK(rig.op.Offset() == 2);
    CHECK(rig.Total() == 0); // it sets, it does not emit

    rig.List("2 60");
    CHECK(rig.sinks[0]->lastInt == 60);

    // A negative offset shifts the other way.
    rig.Reset();
    rig.List("offset -1");
    CHECK(rig.op.Offset() == -1);
    rig.List("0 61");
    CHECK(rig.sinks[1]->lastInt == 61);
    CHECK(rig.Total() == 1);

    // It also overrides the creation argument rather than adding to it.
    rig.Reset();
    rig.List("offset 0");
    rig.List("3 62");
    CHECK(rig.sinks[3]->lastInt == 62);
  }

  TEST_CASE("spray: a malformed or bare 'offset' leaves the offset alone (#479)") {
    Rig rig("4 1");
    rig.List("offset");
    CHECK(rig.op.Offset() == 1);
    rig.List("offset later");
    CHECK(rig.op.Offset() == 1);
    CHECK(rig.Total() == 0);
  }

  TEST_CASE("spray: -1 is a broadcast even when an offset is in force (#479)") {
    // Tested on the number as *written*, before the offset is applied. An
    // implementation that offset first would turn `-1` into outlet -3 here and
    // send nothing, losing every broadcast in a patch without a word.
    Rig rig("4 2");
    rig.List("-1 60");

    CHECK(rig.Log() == "dcba");
    for (int i = 0; i < 4; i++)
      CHECK(rig.sinks[i]->lastInt == 60);

    // And with a negative offset, which would otherwise make -1 a *real* index.
    Rig other("4 -1");
    other.List("-1 60");
    CHECK(other.Log() == "dcba");
  }

  // ─── list mode ──────────────────────────────────────────────────────────────

  TEST_CASE("spray: list mode sends the whole remainder out one outlet (#479)") {
    // Max: "In 'list mode,' an entire list is output through the indicated
    // outlet ..., instead of unpacking the list and sending the individual
    // elements out sequential outlets." What a patch wants when the elements
    // belong together — a note and its velocity are one message.
    Rig rig("4 0 1");
    REQUIRE(rig.op.ListMode());

    rig.List("2 60 100");
    CHECK(rig.Log() == "c");
    CHECK(rig.sinks[2]->lastKind == OrderSink::LIST);
    CHECK(rig.sinks[2]->lastList == "60 100");
    CHECK(rig.Total() == 1);
  }

  TEST_CASE("spray: list mode honours the offset (#479)") {
    // Max: "with the optional offset provided by the second object argument".
    Rig rig("4 1 1");
    rig.List("3 60 100");
    CHECK(rig.sinks[2]->lastList == "60 100");
    CHECK(rig.Total() == 1);

    // Out of range is ignored here too.
    rig.Reset();
    rig.List("0 60 100");
    CHECK(rig.Total() == 0);
  }

  TEST_CASE("spray: list mode broadcasts the whole list on -1 (#479)") {
    Rig rig("3 0 1");
    rig.List("-1 5 6");

    CHECK(rig.Log() == "cba");
    for (int i = 0; i < 3; i++) {
      CHECK(rig.sinks[i]->lastKind == OrderSink::LIST);
      CHECK(rig.sinks[i]->lastList == "5 6");
    }
  }

  TEST_CASE("spray: list mode forwards a single element as a list too (#479)") {
    // The remainder is what it is: list mode does not re-classify a one-element
    // remainder into an int, because the object was told these elements travel
    // together and a downstream object matching on a list would stop seeing it.
    Rig rig("2 0 1");
    rig.List("1 60");
    CHECK(rig.sinks[1]->lastKind == OrderSink::LIST);
    CHECK(rig.sinks[1]->lastList == "60");
  }

  // ─── what spray has no method for ───────────────────────────────────────────

  TEST_CASE("spray: a bare int, float or bang is not accepted (#479)") {
    // Max's int method exists only to post "spray requires a list", and a bare
    // number really is meaningless — an outlet with nothing to put in it, or a
    // value with nowhere to go.
    Rig rig("3");
    rig.op.GetInlet(0)->SetInt(1, YSE::T_GUI);
    rig.op.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    rig.op.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Total() == 0);
  }

  TEST_CASE("spray: a one-element list and a symbol-leading message are ignored (#479)") {
    Rig rig("3");

    rig.List("1"); // an outlet with nothing to put in it
    CHECK(rig.Total() == 0);

    // Max's `anything`, which spray has no method for. The leading-token rule
    // .bondo, .cycle and .bucket apply.
    rig.List("note 60 100");
    rig.List("");
    rig.List("   ");
    CHECK(rig.Total() == 0);
  }

  // ─── re-entrancy ────────────────────────────────────────────────────────────

  TEST_CASE("spray: a burst survives a message looping back into the inlet (#479)") {
    // The interrupted burst must go on emitting the values it *started* with,
    // which a shared member capture buffer would not — the inner burst would
    // overwrite it and the outer outlets would report the loop's values.
    gSpray op;
    op.SetParams("3");

    std::vector<char> order;
    OrderSink first; // outlet 0
    OrderSink second; // outlet 1
    FeedbackSink third; // outlet 2, which fires first
    first.tag = 'a';
    second.tag = 'b';
    third.tag = 'c';
    first.log = second.log = third.log = &order;
    third.target = &op;
    third.fireList = "0 7 8 9";

    op.ConnectOutlet(first.GetInlet(0), 0);
    first.ConnectInlet(op.GetOutlet(0), 0);
    op.ConnectOutlet(second.GetInlet(0), 1);
    second.ConnectInlet(op.GetOutlet(1), 0);
    op.ConnectOutlet(third.GetInlet(0), 2);
    third.ConnectInlet(op.GetOutlet(2), 0);

    third.budget = 1; // exactly one trip round the loop
    op.GetInlet(0)->SetList("0 1 2 3", YSE::T_GUI);

    // Outlet 2 fires first and re-enters; the inner burst runs to completion
    // (its outlet 2 finds the feedback budget spent), then the outer burst
    // finishes with the values it captured.
    CHECK(std::string(order.begin(), order.end()) == "ccbaba");

    // The inner burst carried 7 8 9; the outer finished with the 1 2 3 it
    // started with, so these are the outer values. A member capture buffer would
    // report 8 and 7 here.
    CHECK(second.lastInt == 2);
    CHECK(first.lastInt == 1);
  }

  // ─── real-time / graph ──────────────────────────────────────────────────────

  TEST_CASE("spray: Calculate() emits nothing (#479)") {
    // The object is driven by its inlet. An emitting Calculate() would spray from
    // a stimulus no patch sent.
    Rig rig("3");
    rig.List("0 1 2 3");
    rig.Reset();
    for (int i = 0; i < 8; i++)
      rig.op.Calculate(YSE::T_DSP);
    CHECK(rig.Total() == 0);
  }

  TEST_CASE("spray: survives a DumpJSON / ParseJSON round trip (#479)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_SPRAY);
    REQUIRE(h != nullptr);
    h->SetParams("4 1 1");
    const std::string json = src.DumpJSON();
    CHECK(json.find(".spray") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".spray"));
    CHECK(copy->GetParams() == std::string("4 1 1"));
    // The shape has to come back with it, or the saved patch cords land on the
    // wrong outlets.
    CHECK(copy->GetOutputs() == 4);

    // And so do the offset and the list-mode flag, which the parameter string
    // alone does not prove: index 1 has to reach outlet 0, carrying a list.
    ListSink sink;
    YSE::pHandle sinkHandle(&sink);
    loaded.Connect(copy, 0, &sinkHandle, 0);
    copy->SetListData(0, "1 60 100");
    CHECK(sink.gotList);
    CHECK(sink.received == "60 100");
  }

  TEST_CASE("spray: 'offset' is run-time state, not a parameter (#479)") {
    // The creation arguments are what a saved patch carries, exactly as
    // .cycle's `thresh` and .bucket's `R2L` are run-time state rather than saved
    // state.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_SPRAY, "3 1");
    REQUIRE(h != nullptr);
    h->SetListData(0, "offset 2");
    CHECK(h->GetParams() == std::string("3 1"));
  }

  // ─── end to end ─────────────────────────────────────────────────────────────

  TEST_CASE("spray: addresses a bank of destinations from one cord in a real patcher (#479)") {
    // The headline use, end to end through real objects: one stream, three
    // destinations, and the destination named by the message rather than by the
    // patching. Each path adds a different constant so a sink's value is only
    // right if the value went out the outlet it was addressed to.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* spray = p.CreateObject(YSE::OBJ::G_SPRAY, "3");
    YSE::pHandle* addTen = p.CreateObject(YSE::OBJ::G_ADD, "10");
    YSE::pHandle* addHundred = p.CreateObject(YSE::OBJ::G_ADD, "100");
    YSE::pHandle* addThousand = p.CreateObject(YSE::OBJ::G_ADD, "1000");
    REQUIRE(spray != nullptr);
    REQUIRE(addTen != nullptr);
    REQUIRE(addHundred != nullptr);
    REQUIRE(addThousand != nullptr);

    FloatSink one;
    FloatSink two;
    FloatSink three;
    YSE::pHandle oneHandle(&one);
    YSE::pHandle twoHandle(&two);
    YSE::pHandle threeHandle(&three);

    p.Connect(spray, 0, addTen, 0);
    p.Connect(spray, 1, addHundred, 0);
    p.Connect(spray, 2, addThousand, 0);
    p.Connect(addTen, 0, &oneHandle, 0);
    p.Connect(addHundred, 0, &twoHandle, 0);
    p.Connect(addThousand, 0, &threeHandle, 0);

    // One value, addressed at the middle destination — and nothing else moves.
    spray->SetListData(0, "1 5");
    CHECK(two.received == doctest::Approx(105.f));
    CHECK_FALSE(one.gotFloat);
    CHECK_FALSE(three.gotFloat);

    // The next message addresses a different one, with nothing set in between.
    spray->SetListData(0, "2 5");
    CHECK(three.received == doctest::Approx(1005.f));
    CHECK(two.received == doctest::Approx(105.f));

    // A wider list fills the rest of the bank from where it was aimed.
    spray->SetListData(0, "0 1 2");
    CHECK(one.received == doctest::Approx(11.f));
    CHECK(two.received == doctest::Approx(102.f));
    CHECK(three.received == doctest::Approx(1005.f)); // untouched: the list ran out

    // And -1 reaches the whole bank at once.
    spray->SetListData(0, "-1 9");
    CHECK(one.received == doctest::Approx(19.f));
    CHECK(two.received == doctest::Approx(109.f));
    CHECK(three.received == doctest::Approx(1009.f));
  }

} // TEST_SUITE("patcher")

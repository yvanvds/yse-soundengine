// Tests for .funnel (issue #480) — tag incoming data with its inlet number and
// merge it to one outlet.
//
// Six rules carry this file, and each is one a plausible implementation gets
// wrong:
//
//   - **the tag is the inlet the message arrived at.** The whole object. An
//     implementation that stamped a counter, or the inlet of the *last*
//     message, would pass any single-inlet test while being a different object.
//   - **the offset is added.** .spray's offset subtracts, and getting this one
//     backwards would still pass every offset-0 test while breaking the
//     .funnel -> .spray bus at any other offset — which is the pairing both
//     objects exist for.
//   - **the store is a number, and a list does not write it.** Max calls it
//     "the stored (most recently received) number" and calls bang's reply a
//     "two-item list", so a stored list would make bang emit three items out of
//     a method documented to emit two.
//   - **`set` addresses every inlet at once and emits nothing.** Max: "a list
//     of numbers which correspond with the number of inlets". An implementation
//     that stored into the inlet the message arrived at would look right in a
//     one-element test and be wrong everywhere else.
//   - **spelling survives.** Max says it outright — "in a list floats are not
//     converted to ints" — and it applies to the bare int and float methods and
//     to bang's replay too.
//   - **`set` is positional.** A non-number element spends its place rather
//     than closing the gap, .spray's discipline, or every later value lands in
//     the wrong inlet.
//
// The rest is the message grammar from the Max reference: the inlet count and
// offset creation arguments, the `offset` message, bang, and the bare message
// words.
//
// The standalone rigs wire objects directly, as every sibling suite does; the
// round-trip and end-to-end cases run through a real patcher so the guarantees
// are asserted where a patch can see them — including against a real .spray,
// since "the exact inverse of .spray" is a claim about two objects and not one.
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
#include "patcher/genericObjects/gFunnel.h"
#include "patcher/sinks.hpp"

namespace {

  using TestHelpers::FloatSink;
  using TestHelpers::ListSink;
  using YSE::PATCHER::gFunnel;

  // Records every list the object sends, in order, so a test can assert both
  // "what came out" and "how many times" — the object merges a stream, so the
  // count is as much of the contract as the content.
  struct LogSink : YSE::PATCHER::pObject {
    std::vector<std::string> lists;
    LogSink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { lists.push_back(v); });
    }
    const char* Type() const override {
      return "log_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  struct Rig {
    gFunnel op;
    LogSink sink;

    explicit Rig(const std::string& args = "") {
      if (!args.empty()) op.SetParams(args);
      op.ConnectOutlet(sink.GetInlet(0), 0);
      sink.ConnectInlet(op.GetOutlet(0), 0);
    }

    void Int(int value, int inlet = 0) {
      op.GetInlet(inlet)->SetInt(value, YSE::T_GUI);
    }
    void Float(float value, int inlet = 0) {
      op.GetInlet(inlet)->SetFloat(value, YSE::T_GUI);
    }
    void List(const std::string& text, int inlet = 0) {
      op.GetInlet(inlet)->SetList(text, YSE::T_GUI);
    }
    void Bang(int inlet = 0) {
      op.GetInlet(inlet)->SetBang(YSE::T_GUI);
    }

    int Count() const {
      return (int)sink.lists.size();
    }
    std::string Last() const {
      return sink.lists.empty() ? std::string() : sink.lists.back();
    }
    void Reset() {
      sink.lists.clear();
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("funnel: creatable through the registry (#480)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_FUNNEL, "4");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".funnel"));
    CHECK(h->GetInputs() == 4);
    // One outlet, always — the object merges rather than distributes.
    CHECK(h->GetOutputs() == 1);
  }

  TEST_CASE("funnel: listed by pRegistry::AllNames (#480)") {
    auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(YSE::OBJ::G_FUNNEL)) != names.end());
  }

  TEST_CASE("funnel: two inlets with no argument, as Max documents (#480)") {
    // Max: "If there is no argument there will be two inlets." The same default
    // .spray has for its outlets, so the no-argument pair is already a bus.
    gFunnel op;
    CHECK(op.InletCount() == 2);
    CHECK(op.NumOutputs() == 1);
    CHECK(op.Offset() == 0);
  }

  TEST_CASE("funnel: the inlet count is clamped to 1-256 (#480)") {
    gFunnel low;
    low.SetParams("0");
    CHECK(low.InletCount() == 1);

    gFunnel negative;
    negative.SetParams("-7");
    CHECK(negative.InletCount() == 1);

    gFunnel high;
    high.SetParams("9999");
    CHECK(high.InletCount() == gFunnel::MAX_PORTS);

    // A float argument is truncated, and a non-number leaves the default.
    gFunnel fractional;
    fractional.SetParams("3.9");
    CHECK(fractional.InletCount() == 3);

    gFunnel nonsense;
    nonsense.SetParams("wide");
    CHECK(nonsense.InletCount() == gFunnel::DEFAULT_PORTS);
  }

  TEST_CASE("funnel: the second argument is the offset (#480)") {
    gFunnel op;
    op.SetParams("4 3");
    CHECK(op.InletCount() == 4);
    CHECK(op.Offset() == 3);
    // Max: "an offset for the first inlet number", so inlet 0 stamps it.
    CHECK(op.TagFor(0) == 3);
    CHECK(op.TagFor(3) == 6);

    gFunnel negative;
    negative.SetParams("4 -2");
    CHECK(negative.Offset() == -2);
    CHECK(negative.TagFor(0) == -2);
  }

  TEST_CASE("funnel: SetParams(\"\") returns the object to Max's default shape (#480)") {
    gFunnel op;
    op.SetParams("5 2");
    REQUIRE(op.InletCount() == 5);
    REQUIRE(op.Offset() == 2);

    op.SetParams("");
    CHECK(op.InletCount() == gFunnel::DEFAULT_PORTS);
    CHECK(op.Offset() == 0);
  }

  TEST_CASE("funnel: the outlet is a list outlet (#480)") {
    // Max: "funnel outputs a list consisting of the inlet number followed the
    // input" — always a list, so it is typed as one rather than as ANY.
    gFunnel op;
    CHECK(op.GetOutputType(0) == YSE::OUT_TYPE::LIST);
  }

  TEST_CASE("funnel: every inlet accepts bang, int, float and list (#480)") {
    // Max says "In any inlet" on every method this object has, so a cold inlet
    // here would be visible as a missing handler.
    gFunnel op;
    op.SetParams("3");
    for (int i = 0; i < op.NumInputs(); i++) {
      CAPTURE(i);
      const unsigned int in = op.GetInlet(i)->GetAcceptedTypes();
      CHECK((in & YSE::PATCHER::IT_BANG) != 0);
      CHECK((in & YSE::PATCHER::IT_INT) != 0);
      CHECK((in & YSE::PATCHER::IT_FLOAT) != 0);
      CHECK((in & YSE::PATCHER::IT_LIST) != 0);
    }
  }

  // ─── the tag ────────────────────────────────────────────────────────────────

  TEST_CASE("funnel: an int leaves tagged with the inlet it arrived at (#480)") {
    // The object. Max: "The number of the inlet and the received number are sent
    // out as a list."
    Rig rig("4");

    rig.Int(60, 0);
    CHECK(rig.Count() == 1);
    CHECK(rig.Last() == "0 60");

    rig.Int(60, 2);
    CHECK(rig.Count() == 2);
    CHECK(rig.Last() == "2 60");

    rig.Int(-7, 3);
    CHECK(rig.Last() == "3 -7");
  }

  TEST_CASE("funnel: the tag is the arriving inlet, not a counter or the last one (#480)") {
    // The mistake worth pinning: two values at the *same* inlet must both carry
    // that inlet's number, and a value at a new inlet must not carry the old
    // one's.
    Rig rig("4");

    rig.Int(1, 2);
    rig.Int(2, 2);
    rig.Int(3, 0);
    REQUIRE(rig.Count() == 3);
    CHECK(rig.sink.lists[0] == "2 1");
    CHECK(rig.sink.lists[1] == "2 2");
    CHECK(rig.sink.lists[2] == "0 3");
  }

  TEST_CASE("funnel: a float keeps its spelling and an int keeps its own (#480)") {
    // The discipline .trigger, .bondo, .cycle, .bucket and .spray share: a
    // forwarding object must not rewrite the type of what passes through it.
    Rig rig("2");

    rig.Int(1, 0);
    CHECK(rig.Last() == "0 1");

    rig.Float(1.5f, 1);
    CHECK(rig.Last() == "1 1.5");

    // A float that happens to be whole is still a float.
    rig.Float(2.f, 0);
    CHECK(rig.Last() == "0 2.");
  }

  TEST_CASE("funnel: a list is prepended with the inlet number, verbatim (#480)") {
    // Max: "The number of the inlet is prepended to the list, and the new list
    // is sent out. In a list floats are not converted to ints."
    Rig rig("3");

    rig.List("60 100", 1);
    CHECK(rig.Last() == "1 60 100");

    // The clause Max states outright: the 100.5 comes back as 100.5.
    rig.List("60 100.5", 2);
    CHECK(rig.Last() == "2 60 100.5");
  }

  TEST_CASE("funnel: anything is tagged the same as a list (#480)") {
    // Max: "anything: Functions the same as list."
    Rig rig("3");
    rig.List("note 60 100", 2);
    CHECK(rig.Last() == "2 note 60 100");
  }

  TEST_CASE("funnel: surrounding whitespace is not carried into the tagged list (#480)") {
    Rig rig("2");
    rig.List("  60 100  ", 1);
    CHECK(rig.Last() == "1 60 100");

    // Nothing to tag: a bare tag would claim an arrival that carried nothing.
    rig.Reset();
    rig.List("", 0);
    rig.List("   ", 0);
    CHECK(rig.Count() == 0);
  }

  // ─── the offset ─────────────────────────────────────────────────────────────

  TEST_CASE("funnel: the offset is ADDED to the inlet number (#480)") {
    // The direction is the thing. .spray's offset subtracts; this one adds, and
    // that is exactly what makes the two mirror images at any offset.
    Rig rig("3 10");

    rig.Int(60, 0);
    CHECK(rig.Last() == "10 60");
    rig.Int(60, 2);
    CHECK(rig.Last() == "12 60");
  }

  TEST_CASE("funnel: the 'offset' message changes the tag, in any inlet (#480)") {
    // Max: "The word offset followed by a number will offset the numbering of
    // inlets by the number given." No inlet scoping in the reference.
    Rig rig("3");

    rig.List("offset 5", 0);
    CHECK(rig.Count() == 0); // the method emits nothing
    CHECK(rig.op.Offset() == 5);
    rig.Int(60, 1);
    CHECK(rig.Last() == "6 60");

    // And from a different inlet.
    rig.List("offset -1", 2);
    CHECK(rig.op.Offset() == -1);
    rig.Int(60, 1);
    CHECK(rig.Last() == "0 60");
  }

  TEST_CASE("funnel: a bare message word is the method with nothing to do (#480)") {
    // Not forwarded as the symbol it also is — answering a method call with
    // "0 set" is not something a patch means. A malformed argument is the same
    // case.
    Rig rig("2");
    rig.List("offset", 0);
    rig.List("set", 0);
    rig.List("offset zzz", 0);
    CHECK(rig.Count() == 0);
    CHECK(rig.op.Offset() == 0);
  }

  // ─── bang and the store ─────────────────────────────────────────────────────

  TEST_CASE("funnel: bang replays the stored number, tagged (#480)") {
    // Max: "The number of the inlet and the stored (most recently received)
    // number in that inlet are sent out as a two-item list." A poll, which is
    // what lets a patch ask what a source is saying without it speaking again.
    Rig rig("3");

    rig.Int(60, 1);
    rig.Reset();

    rig.Bang(1);
    CHECK(rig.Count() == 1);
    CHECK(rig.Last() == "1 60");

    // Replayed as the kind it was stored as.
    rig.Float(1.5f, 2);
    rig.Reset();
    rig.Bang(2);
    CHECK(rig.Last() == "2 1.5");
  }

  TEST_CASE("funnel: bang before anything has arrived replays 0 (#480)") {
    // The store starts at 0, which makes the object's output shape independent
    // of its history — a bang always answers, and always with two items.
    Rig rig("3 4");
    rig.Bang(2);
    CHECK(rig.Count() == 1);
    CHECK(rig.Last() == "6 0");
  }

  TEST_CASE("funnel: a list does not become the stored number (#480)") {
    // Max calls the store a *number* and bang's reply a *two-item* list, so a
    // stored list would make bang emit three items out of a method documented
    // to emit two.
    Rig rig("2");

    rig.Int(60, 0);
    rig.List("7 8 9", 0);
    REQUIRE(rig.Last() == "0 7 8 9");

    rig.Reset();
    rig.Bang(0);
    CHECK(rig.Last() == "0 60");
    CHECK(rig.op.StoredInt(0) == 60);
  }

  TEST_CASE("funnel: each inlet has its own store (#480)") {
    Rig rig("3");
    rig.Int(10, 0);
    rig.Int(20, 1);
    rig.Int(30, 2);
    rig.Reset();

    rig.Bang(0);
    rig.Bang(1);
    rig.Bang(2);
    REQUIRE(rig.Count() == 3);
    CHECK(rig.sink.lists[0] == "0 10");
    CHECK(rig.sink.lists[1] == "1 20");
    CHECK(rig.sink.lists[2] == "2 30");
  }

  // ─── set ────────────────────────────────────────────────────────────────────

  TEST_CASE("funnel: 'set' writes every inlet's store and sends nothing (#480)") {
    // Max: "The word set followed by a list of numbers which correspond with the
    // number of inlets, will set the input list of numbers without sending them
    // through the outputs." Element k is inlet k's — not the inlet the message
    // arrived at.
    Rig rig("3");

    rig.List("set 10 20 30", 2);
    CHECK(rig.Count() == 0);
    CHECK(rig.op.StoredInt(0) == 10);
    CHECK(rig.op.StoredInt(1) == 20);
    CHECK(rig.op.StoredInt(2) == 30);

    rig.Bang(0);
    rig.Bang(2);
    REQUIRE(rig.Count() == 2);
    CHECK(rig.sink.lists[0] == "0 10");
    CHECK(rig.sink.lists[1] == "2 30");
  }

  TEST_CASE("funnel: 'set' keeps each element's spelling (#480)") {
    Rig rig("2");
    rig.List("set 1 2.5", 0);
    CHECK_FALSE(rig.op.StoredIsFloat(0));
    CHECK(rig.op.StoredIsFloat(1));

    rig.Bang(0);
    CHECK(rig.Last() == "0 1");
    rig.Bang(1);
    CHECK(rig.Last() == "1 2.5");
  }

  TEST_CASE("funnel: a short 'set' leaves the rest alone and a long one is cut (#480)") {
    Rig rig("3");
    rig.List("set 10 20 30", 0);
    REQUIRE(rig.op.StoredInt(2) == 30);

    rig.List("set 1", 0);
    CHECK(rig.op.StoredInt(0) == 1);
    CHECK(rig.op.StoredInt(1) == 20);
    CHECK(rig.op.StoredInt(2) == 30);

    // Past the last inlet is dropped rather than wrapped.
    rig.List("set 4 5 6 7 8", 0);
    CHECK(rig.op.StoredInt(0) == 4);
    CHECK(rig.op.StoredInt(2) == 6);
  }

  TEST_CASE("funnel: 'set' is positional — a symbol spends its place (#480)") {
    // .spray's discipline for the same situation: closing the gap would write
    // every later value into the wrong inlet.
    Rig rig("3");
    rig.List("set 10 20 30", 0);

    rig.List("set 1 wat 3", 0);
    CHECK(rig.op.StoredInt(0) == 1);
    CHECK(rig.op.StoredInt(1) == 20); // untouched, not overwritten by the 3
    CHECK(rig.op.StoredInt(2) == 3);
  }

  // ─── real-time / graph ──────────────────────────────────────────────────────

  TEST_CASE("funnel: Calculate() emits nothing (#480)") {
    // The object is driven by its inlets. An emitting Calculate() would tag a
    // value from a stimulus no patch sent.
    Rig rig("3");
    rig.Int(60, 1);
    rig.Reset();
    for (int i = 0; i < 8; i++)
      rig.op.Calculate(YSE::T_DSP);
    CHECK(rig.Count() == 0);
  }

  TEST_CASE("funnel: survives a DumpJSON / ParseJSON round trip (#480)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_FUNNEL);
    REQUIRE(h != nullptr);
    h->SetParams("4 10");
    const std::string json = src.DumpJSON();
    CHECK(json.find(".funnel") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".funnel"));
    CHECK(copy->GetParams() == std::string("4 10"));
    // The shape has to come back with it, or the saved patch cords land on the
    // wrong inlets.
    CHECK(copy->GetInputs() == 4);
    CHECK(copy->GetOutputs() == 1);

    // And so does the offset, which the parameter string alone does not prove:
    // inlet 3 has to stamp 13.
    ListSink sink;
    YSE::pHandle sinkHandle(&sink);
    loaded.Connect(copy, 0, &sinkHandle, 0);
    copy->SetIntData(3, 60);
    CHECK(sink.gotList);
    CHECK(sink.received == "13 60");
  }

  TEST_CASE("funnel: 'offset' is run-time state, not a parameter (#480)") {
    // The creation arguments are what a saved patch carries, exactly as
    // .spray's offset and .cycle's `thresh` are run-time state.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_FUNNEL, "3 1");
    REQUIRE(h != nullptr);
    h->SetListData(0, "offset 2");
    CHECK(h->GetParams() == std::string("3 1"));
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category, the port shape and the parameter name,
  // which is what a binding generator keys on. It also pins that the inlets
  // built by the parse callback are documented — the coverage test only ever
  // sees a default-constructed object.

  TEST_CASE("funnel: documents itself as GENERIC with a labelled port set (#480)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_FUNNEL));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    CHECK_FALSE(obj->GetDescription().empty());

    REQUIRE(obj->NumInputs() == 2);
    CHECK(obj->GetInlet(0)->GetDocLabel() == "in0");
    CHECK(obj->GetInlet(1)->GetDocLabel() == "in1");

    REQUIRE(obj->NumOutputs() == 1);
    CHECK(obj->GetOutlet(0)->GetDocLabel() == "out");

    REQUIRE(obj->GetParamDocs().size() == 1);
    CHECK(obj->GetParamDocs()[0].name == "inlets");
    CHECK(obj->GetParamDocs()[0].defaultValue == "2");
  }

  TEST_CASE("funnel: inlets created by the parse callback are documented too (#480)") {
    gFunnel op;
    op.SetParams("7");
    REQUIRE(op.NumInputs() == 7);
    for (int i = 0; i < 7; i++) {
      CAPTURE(i);
      CHECK_FALSE(op.GetInlet(i)->GetDocLabel().empty());
      CHECK_FALSE(op.GetInlet(i)->GetDocDescription().empty());
      CHECK_FALSE(op.GetInlet(i)->GetRange().empty());
    }
    CHECK(op.GetInlet(6)->GetDocLabel() == "in6");
    CHECK_FALSE(op.GetOutlet(0)->GetDocDescription().empty());
    CHECK_FALSE(op.GetOutlet(0)->GetRange().empty());
  }

  // ─── capacity ───────────────────────────────────────────────────────────────

  TEST_CASE("funnel: at most 256 inlets are built, and the last one tags (#480)") {
    Rig rig("300");
    REQUIRE(rig.op.InletCount() == gFunnel::MAX_PORTS);
    REQUIRE(rig.op.NumInputs() == gFunnel::MAX_PORTS);

    rig.Int(9, gFunnel::MAX_PORTS - 1);
    CHECK(rig.Last() == "255 9");
  }

  // ─── end to end ─────────────────────────────────────────────────────────────

  TEST_CASE("funnel: merges a bank of sources onto one cord in a real patcher (#480)") {
    // The headline use, end to end through real objects: three sources, one
    // cord, and the destination still able to tell them apart. .route is the
    // object that reads the tag back, so the pair is asserted rather than
    // described — a value only reaches the right sink if the tag survived the
    // merge intact.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* funnel = p.CreateObject(YSE::OBJ::G_FUNNEL, "3");
    YSE::pHandle* route = p.CreateObject(YSE::OBJ::G_ROUTE, "0 1 2");
    REQUIRE(funnel != nullptr);
    REQUIRE(route != nullptr);
    REQUIRE(route->GetOutputs() == 4); // one per token plus the fall-through

    ListSink one;
    ListSink two;
    ListSink three;
    ListSink rejected;
    YSE::pHandle oneHandle(&one);
    YSE::pHandle twoHandle(&two);
    YSE::pHandle threeHandle(&three);
    YSE::pHandle rejectedHandle(&rejected);

    p.Connect(funnel, 0, route, 0); // three sources, one cord
    p.Connect(route, 0, &oneHandle, 0);
    p.Connect(route, 1, &twoHandle, 0);
    p.Connect(route, 2, &threeHandle, 0);
    p.Connect(route, 3, &rejectedHandle, 0);

    // A value at the middle inlet, and nothing else moves.
    funnel->SetIntData(1, 5);
    CHECK(two.gotList);
    CHECK(two.received == "1 5");
    CHECK_FALSE(one.gotList);
    CHECK_FALSE(three.gotList);
    CHECK_FALSE(rejected.gotList);

    // A list at another inlet keeps its elements and picks up that inlet's tag.
    funnel->SetListData(2, "60 100.5");
    CHECK(three.received == "2 60 100.5");

    funnel->SetIntData(0, 7);
    CHECK(one.received == "0 7");
    CHECK_FALSE(rejected.gotList);
  }

  TEST_CASE("funnel into .spray is an n-way bus over one cord (#480)") {
    // The claim the two objects are built on, asserted against the real .spray
    // rather than described in a comment: what enters inlet i leaves outlet i,
    // and it holds at a non-zero offset only because this object *adds* where
    // .spray subtracts.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* funnel = p.CreateObject(YSE::OBJ::G_FUNNEL, "3 7");
    YSE::pHandle* spray = p.CreateObject(YSE::OBJ::G_SPRAY, "3 7");
    REQUIRE(funnel != nullptr);
    REQUIRE(spray != nullptr);

    FloatSink one;
    FloatSink two;
    FloatSink three;
    YSE::pHandle oneHandle(&one);
    YSE::pHandle twoHandle(&two);
    YSE::pHandle threeHandle(&three);

    p.Connect(funnel, 0, spray, 0); // the whole bus, one cord
    p.Connect(spray, 0, &oneHandle, 0);
    p.Connect(spray, 1, &twoHandle, 0);
    p.Connect(spray, 2, &threeHandle, 0);

    funnel->SetFloatData(1, 60.f);
    CHECK(two.received == doctest::Approx(60.f));
    CHECK_FALSE(one.gotFloat);
    CHECK_FALSE(three.gotFloat);

    funnel->SetFloatData(0, 61.f);
    CHECK(one.received == doctest::Approx(61.f));
    funnel->SetFloatData(2, 62.f);
    CHECK(three.received == doctest::Approx(62.f));
  }

} // TEST_SUITE("patcher")

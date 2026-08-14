// Tests for .array.indexof / .array.index (issue #786) — Max's array.indexof
// ("output the position of an array element... output -1 if the element
// cannot be found") and the membership test #786 assigns the array.index
// name, on the name-addressed value model .array settled (#548).
//
// What has to be proven, and what every case below is one of:
//
//   - **the array is bound from the creation argument.** An array never
//     travels down a cord, so ".array.indexof <name> [<value>]" resolves the
//     name once, on the control thread, and an `array <name>` message is
//     honoured only when it names the array already bound —
//     ArrayReferenceNames' bounded compare, never a registry lookup on a
//     message path.
//   - **both are one ArrayFind, differing only in reporting.** A value
//     searches for the element that spells it and stores the value; a bang
//     re-searches with the stored one, seeded by the second creation
//     argument. .array.indexof answers in-band — the first matching
//     position, or -1 on a miss, Max's own answer — where .array.index
//     splits the miss onto an outlet, so membership is a cord choice.
//   - **equality is the spelling.** An element is one atom and equals the
//     value that spells it: an int 7 finds "7" and not "7.", a float 7.
//     finds "7." and not "7" — the same fact getvalue spells them
//     differently. A multi-atom list is refused whole: only one atom can be
//     held, and a sub-array match is not offered.
//   - **the search crosses the control/audio boundary and nothing
//     allocates.** In-patcher delivery dispatches on T_DSP, so "the audio
//     thread asks where a value is" is the ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names an array uses names of its own — one case's contents
// must not be visible to the next.

#include <doctest/doctest.h>
#include <cstdint>
#include <string>

#include "patcher/genericObjects/gArray.h"
#include "patcher/genericObjects/gArrayFind.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using TestHelpers::BangSink;
using TestHelpers::MultiSink;
using TestHelpers::Wire;
using YSE::PATCHER::gArray;
using YSE::PATCHER::gArrayIndex;
using YSE::PATCHER::gArrayIndexOf;

namespace {

  // An .array, an .array.indexof and an .array.index on one name, sharing
  // one patcherImplementation so the name actually binds
  // ("<patcherName>.<name>" needs a patcher to prefix with — a parentless
  // object stays private). The sinks are declared before the objects so they
  // are torn down last, while the outlets wired to them still exist (see
  // sinks.hpp on why that matters).
  struct Rig {
    MultiSink position; // indexof's one outlet: the position, or -1
    MultiSink held; // index's position outlet: the position, on a hit
    BangSink miss; // index's miss outlet, banged when the value is not held
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    gArray array;
    gArrayIndexOf indexOf;
    gArrayIndex index;

    Rig(const std::string& patcherName, const std::string& name,
        const std::string& findArgs = std::string()) {
      p.SetName(patcherName);
      array.SetParent(&p);
      array.SetParams(name);
      indexOf.SetParent(&p);
      indexOf.SetParams(findArgs.empty() ? name : findArgs);
      index.SetParent(&p);
      index.SetParams(findArgs.empty() ? name : findArgs);
      Wire(indexOf, 0, position);
      Wire(index, 0, held);
      Wire(index, 1, miss);
    }

    void Store(const std::string& message) {
      array.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void Reset() {
      position.reset();
      held.reset();
      miss.bangCount = 0;
      miss.gotBang = false;
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("array.indexof/.index: registered, two inlets, their outlets (#786)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* indexOf = p.CreateObject(YSE::OBJ::G_ARRAY_INDEXOF);
    REQUIRE(indexOf != nullptr);
    CHECK(std::string(indexOf->Type()) == ".array.indexof");
    CHECK(indexOf->GetInputs() == 2);
    // One outlet: the miss travels in-band as -1.
    CHECK(indexOf->GetOutputs() == 1);

    YSE::pHandle* index = p.CreateObject(YSE::OBJ::G_ARRAY_INDEX);
    REQUIRE(index != nullptr);
    CHECK(std::string(index->Type()) == ".array.index");
    CHECK(index->GetInputs() == 2);
    // Two: the family's split — the position on a hit, a bang on a miss.
    CHECK(index->GetOutputs() == 2);
  }

  TEST_CASE("array.indexof/.index: appear in the registry's name list (#786)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool foundIndexOf = false;
    bool foundIndex = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_ARRAY_INDEXOF)) foundIndexOf = true;
      if (name == std::string(YSE::OBJ::G_ARRAY_INDEX)) foundIndex = true;
    }
    CHECK(foundIndexOf);
    CHECK(foundIndex);
  }

  TEST_CASE("array.indexof: the value inlet takes everything, the reference inlet only lists "
            "(#786)") {
    // Inlet 0 is the value — int, float, bang and list all mean a search.
    // Inlet 1 only ever carries the "array <name>" reference, which is list
    // text; a bare number there names nothing.
    gArrayIndexOf g;
    const unsigned int valueIn = g.GetInlet(0)->GetAcceptedTypes();
    CHECK((valueIn & YSE::PATCHER::IT_INT) != 0);
    CHECK((valueIn & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((valueIn & YSE::PATCHER::IT_BANG) != 0);
    CHECK((valueIn & YSE::PATCHER::IT_LIST) != 0);
    const unsigned int refIn = g.GetInlet(1)->GetAcceptedTypes();
    CHECK((refIn & YSE::PATCHER::IT_LIST) != 0);
    CHECK((refIn & YSE::PATCHER::IT_INT) == 0);
    CHECK((refIn & YSE::PATCHER::IT_FLOAT) == 0);
    CHECK((refIn & YSE::PATCHER::IT_BANG) == 0);

    // The same base builds .array.index's inlets, but a shared base is an
    // implementation fact, not a contract — assert the shape on both.
    gArrayIndex g2;
    CHECK(g2.GetInlet(0)->GetAcceptedTypes() == valueIn);
    CHECK(g2.GetInlet(1)->GetAcceptedTypes() == refIn);
  }

  // ─── the search ─────────────────────────────────────────────────────────────

  TEST_CASE("array.indexof: a value searches by its spelling, first position wins (#786)") {
    // The object's whole point: the value arrives on a cord and out comes a
    // position .array.at can consume. First position only — the decision
    // #786 asks for, written down on gArrayFindBase — so the repeated "10"
    // answers 0, not 3.
    Rig rig("afd786a", "a786a");
    rig.Store("append 10 c4 7.5 10");

    rig.indexOf.GetInlet(0)->SetInt(10, YSE::T_GUI);
    CHECK(rig.position.gotInt);
    CHECK(rig.position.intValue == 0);

    rig.Reset();
    const std::string symbol = "c4";
    rig.indexOf.GetInlet(0)->SetList(symbol, YSE::T_GUI);
    CHECK(rig.position.gotInt);
    CHECK(rig.position.intValue == 1);

    rig.Reset();
    rig.indexOf.GetInlet(0)->SetFloat(7.5f, YSE::T_GUI);
    CHECK(rig.position.gotInt);
    CHECK(rig.position.intValue == 2);

    // A negative number is a value here, not an index — nothing to refuse.
    rig.Reset();
    rig.Store("append -3");
    rig.indexOf.GetInlet(0)->SetInt(-3, YSE::T_GUI);
    CHECK(rig.position.gotInt);
    CHECK(rig.position.intValue == 4);
    CHECK(rig.indexOf.Dropped() == 0);
  }

  TEST_CASE("array.indexof: a miss answers -1, in-band (#786)") {
    // Max's own answer, and the one a patch can test with .sel -1: every
    // answered search emits exactly one int, so a miss is data, not
    // silence. An unnamed (private) array misses every search the same way.
    Rig rig("afd786b", "a786b");
    rig.Store("append 10 4 7");

    rig.indexOf.GetInlet(0)->SetInt(99, YSE::T_GUI);
    CHECK(rig.position.gotInt);
    CHECK(rig.position.intValue == -1);
    CHECK(rig.indexOf.Dropped() == 0);

    MultiSink sink;
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("afd786b2");
    gArrayIndexOf unnamed;
    unnamed.SetParent(&p);
    Wire(unnamed, 0, sink);
    CHECK(unnamed.Address().empty());
    const std::string value = "anything";
    unnamed.GetInlet(0)->SetList(value, YSE::T_GUI);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == -1);
    CHECK(unnamed.Dropped() == 0);
  }

  TEST_CASE("array.indexof: equality is the spelling — 7 and 7. are different elements (#786)") {
    // An element equals the value that spells it, the same fact getvalue
    // spells "7" and "7." differently: what a float push stored is what a
    // float search finds, and an int search does not blur into it.
    Rig rig("afd786c", "a786c");
    rig.Store("append 7.");

    rig.indexOf.GetInlet(0)->SetFloat(7.f, YSE::T_GUI);
    CHECK(rig.position.gotInt);
    CHECK(rig.position.intValue == 0);

    rig.Reset();
    rig.indexOf.GetInlet(0)->SetInt(7, YSE::T_GUI);
    CHECK(rig.position.gotInt);
    CHECK(rig.position.intValue == -1);
    CHECK(rig.indexOf.Dropped() == 0);
  }

  TEST_CASE("array.indexof: a bang re-searches with the stored value, seeded by the argument "
            "(#786)") {
    // ".array.indexof <name> c4" starts with a value, so a bang before any
    // value has arrived already searches — and a value moves the stored one,
    // so the next bang follows it. A miss is a property of the array at that
    // moment, not of the request: the same bang finds the element once the
    // array has grown it.
    Rig rig("afd786d", "a786d", "a786d c4");
    rig.Store("append 10 c4 7");
    CHECK(rig.indexOf.Value() == "c4");

    rig.indexOf.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.position.gotInt);
    CHECK(rig.position.intValue == 1);

    rig.Reset();
    rig.indexOf.GetInlet(0)->SetInt(99, YSE::T_GUI);
    CHECK(rig.position.gotInt);
    CHECK(rig.position.intValue == -1);
    CHECK(rig.indexOf.Value() == "99");

    rig.Reset();
    rig.Store("append 99");
    rig.indexOf.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.position.gotInt);
    CHECK(rig.position.intValue == 3);
    CHECK(rig.indexOf.Dropped() == 0);
  }

  TEST_CASE("array.indexof: a bang before any value is refused, not a miss (#786)") {
    // An absent value is malformed where a miss is a well-formed value the
    // array happens not to hold — so nothing leaves, where a miss would
    // answer -1, and the refusal is counted.
    Rig rig("afd786e", "a786e");
    rig.Store("append 10 4 7");
    CHECK(rig.indexOf.Value().empty());

    rig.indexOf.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(rig.position.gotInt);
    CHECK(rig.indexOf.Dropped() == 1);

    rig.index.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(rig.held.gotInt);
    CHECK(rig.miss.bangCount == 0);
    CHECK(rig.index.Dropped() == 1);
  }

  TEST_CASE("array.indexof: a multi-atom list is refused whole, and moves nothing (#786)") {
    // An element is one atom, so only one atom can be held — a sub-array
    // match is not offered, refused rather than guessed. The stored value
    // does not move either, so a bang after the refusal searches for what it
    // would have searched for before it. An atom past ELEMENT_CAPACITY can
    // never match and is refused as malformed too.
    Rig rig("afd786f", "a786f", "a786f c4");
    rig.Store("append 10 c4 7");

    const std::string listValue = "10 c4";
    rig.indexOf.GetInlet(0)->SetList(listValue, YSE::T_GUI);
    CHECK_FALSE(rig.position.gotInt);
    CHECK(rig.indexOf.Dropped() == 1);
    CHECK(rig.indexOf.Value() == "c4");

    const std::string overlong(YSE::PATCHER::arrayStore::ELEMENT_CAPACITY + 1, 'x');
    rig.indexOf.GetInlet(0)->SetList(overlong, YSE::T_GUI);
    CHECK_FALSE(rig.position.gotInt);
    CHECK(rig.indexOf.Dropped() == 2);
    CHECK(rig.indexOf.Value() == "c4");

    rig.indexOf.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.position.gotInt);
    CHECK(rig.position.intValue == 1);
  }

  TEST_CASE("array.indexof: an over-long seed argument plants no value (#786)") {
    // A value past ELEMENT_CAPACITY cannot be an element at all, so the
    // argument becomes "no value" — counted — and a bang then refuses
    // exactly as it does before any value has arrived.
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("afd786g");
    MultiSink sink;
    gArrayIndexOf g;
    g.SetParent(&p);
    const std::string overlong(YSE::PATCHER::arrayStore::ELEMENT_CAPACITY + 1, 'y');
    g.SetParams("a786g " + overlong);
    Wire(g, 0, sink);

    CHECK(g.Value().empty());
    const std::uint64_t before = g.Dropped();
    CHECK(before >= 1);
    g.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(sink.gotInt);
    CHECK(g.Dropped() == before + 1);
  }

  // ─── the split — .array.index ───────────────────────────────────────────────

  TEST_CASE("array.index: a hit answers the position, a miss bangs the miss outlet (#786)") {
    // The same search, reported out-of-band — the family's split, so
    // "whether" is which outlet fired and "where" is the int, and a patch
    // routes on membership without comparing against -1.
    Rig rig("afd786h", "a786h");
    rig.Store("append 10 c4 7.5");

    rig.index.GetInlet(0)->SetInt(10, YSE::T_GUI);
    CHECK(rig.held.gotInt);
    CHECK(rig.held.intValue == 0);
    CHECK(rig.miss.bangCount == 0);

    rig.Reset();
    const std::string symbol = "c4";
    rig.index.GetInlet(0)->SetList(symbol, YSE::T_GUI);
    CHECK(rig.held.gotInt);
    CHECK(rig.held.intValue == 1);
    CHECK(rig.miss.bangCount == 0);

    rig.Reset();
    rig.index.GetInlet(0)->SetInt(99, YSE::T_GUI);
    CHECK_FALSE(rig.held.gotInt);
    CHECK(rig.miss.bangCount == 1);
    CHECK(rig.index.Dropped() == 0);
  }

  TEST_CASE("array.index: an empty or unnamed array misses every search (#786)") {
    // "Is it held" over an array that holds nothing answers no, on the miss
    // outlet — a miss, not a refusal, because the question was well-formed.
    Rig rig("afd786i", "a786i");
    rig.index.GetInlet(0)->SetInt(1, YSE::T_GUI);
    CHECK_FALSE(rig.held.gotInt);
    CHECK(rig.miss.bangCount == 1);
    CHECK(rig.index.Dropped() == 0);

    BangSink missSink;
    MultiSink heldSink;
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("afd786i2");
    gArrayIndex unnamed;
    unnamed.SetParent(&p);
    Wire(unnamed, 0, heldSink);
    Wire(unnamed, 1, missSink);
    CHECK(unnamed.Address().empty());
    unnamed.GetInlet(0)->SetInt(1, YSE::T_GUI);
    CHECK_FALSE(heldSink.gotInt);
    CHECK(missSink.bangCount == 1);
    CHECK(unnamed.Dropped() == 0);
  }

  // ─── the reference ──────────────────────────────────────────────────────────

  TEST_CASE("array.indexof: the reference searches on the value inlet, anything else refused "
            "(#786)") {
    // "array <name>" is the message an .array's reference outlet emits on a
    // bang; on the value inlet it searches with the stored value — the
    // family gesture: bang the array, out comes the stored value's
    // position. No ambiguity with data: a search value is one atom and a
    // reference is two. A reference to an array this object is not bound to
    // is refused, never resolved: a registry lookup is a mutex, and this
    // may be the audio thread.
    Rig rig("afd786j", "a786j", "a786j 4");
    rig.Store("append 10 4 7");

    const std::uint64_t before = rig.indexOf.Dropped();
    rig.indexOf.GetInlet(0)->SetList("array a786j", YSE::T_GUI);
    CHECK(rig.position.gotInt);
    CHECK(rig.position.intValue == 1);
    CHECK(rig.indexOf.Dropped() == before);

    rig.Reset();
    rig.indexOf.GetInlet(0)->SetList("array somewhere_else", YSE::T_GUI);
    CHECK_FALSE(rig.position.gotInt);
    CHECK(rig.indexOf.Dropped() == before + 1);
  }

  TEST_CASE("array.indexof: the reference inlet acknowledges its own array and nothing more "
            "(#786)") {
    // Wiring the array's reference outlet across keeps the patch readable,
    // and the acknowledgement is silent — the binding is the creation
    // argument, so there is nothing to set. Anything else there is refused,
    // which is what makes a mis-wired cord visible in Dropped().
    Rig rig("afd786k", "a786k", "a786k 4");
    rig.Store("append 10 4 7");

    const std::uint64_t before = rig.indexOf.Dropped();
    rig.indexOf.GetInlet(1)->SetList("array a786k", YSE::T_GUI);
    CHECK_FALSE(rig.position.gotInt);
    CHECK(rig.indexOf.Dropped() == before);

    rig.indexOf.GetInlet(1)->SetList("array somewhere_else", YSE::T_GUI);
    rig.indexOf.GetInlet(1)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK(rig.indexOf.Dropped() == before + 2);
  }

  TEST_CASE("array.indexof: wired from the array's reference outlet, banging the array searches "
            "(#786)") {
    // The flow a patch actually wires, end to end through the public patcher
    // API: the .array's reference outlet into the value inlet gives the
    // family gesture — bang the array, out comes the stored value's
    // position.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::patcher p;
    p.create(2);
    p.name("afd786l");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "a786l");
    YSE::pHandle* find = p.CreateObject(YSE::OBJ::G_ARRAY_INDEXOF, "a786l 64");
    REQUIRE(array != nullptr);
    REQUIRE(find != nullptr);
    p.Connect(array, 1, find, 0);
    p.Connect(find, 0, &sinkHandle, 0);

    array->SetListData(0, "append 60 64 67");
    array->SetBang(0);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 1);

    sink.reset();
    array->SetListData(0, "delete 0");
    array->SetBang(0);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 0);
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("array.indexof: the address form is the patcher's, and RefreshBinding follows it "
            "(#786)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("afd786m_before");

    gArrayIndexOf g;
    g.SetParams("a786m");
    g.SetParent(&p);
    CHECK(g.ArrayName() == "a786m");
    CHECK(g.Address() == "afd786m_before.a786m");

    // Idempotent: a rebind to the address it already has keeps the store.
    g.RefreshBinding();
    CHECK(g.Address() == "afd786m_before.a786m");

    p.SetName("afd786m_after");
    g.RefreshBinding();
    CHECK(g.Address() == "afd786m_after.a786m");
  }

  TEST_CASE("array.indexof/.index: patcherImplementation::SetName re-anchors them (#786)") {
    // The rename dispatch itself, which the standalone case above cannot
    // reach: an object created *inside* a patcher must be re-anchored by the
    // patcher, without anybody calling RefreshBinding by hand. The keeper
    // holds the old-address store; after the rename both objects read a
    // fresh empty array under the new prefix, so the search misses — while
    // the stored value survives, being the object's own state rather than
    // anything derived from the name.
    MultiSink positionSink;
    MultiSink heldSink;
    BangSink missSink;
    YSE::pHandle positionHandle(&positionSink);
    YSE::pHandle heldHandle(&heldSink);
    YSE::pHandle missHandle(&missSink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("afd786n_before");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a786n");
    keeper.GetInlet(0)->SetList("append 10 4 7", YSE::T_GUI);

    YSE::pHandle* indexOf = p.CreateObject(YSE::OBJ::G_ARRAY_INDEXOF, "a786n 4");
    YSE::pHandle* index = p.CreateObject(YSE::OBJ::G_ARRAY_INDEX, "a786n 4");
    REQUIRE(indexOf != nullptr);
    REQUIRE(index != nullptr);
    p.Connect(indexOf, 0, &positionHandle, 0);
    p.Connect(index, 0, &heldHandle, 0);
    p.Connect(index, 1, &missHandle, 0);

    indexOf->SetBang(0);
    REQUIRE(positionSink.gotInt);
    CHECK(positionSink.intValue == 1);
    index->SetBang(0);
    REQUIRE(heldSink.gotInt);
    CHECK(heldSink.intValue == 1);
    CHECK(missSink.bangCount == 0);

    p.SetName("afd786n_after");
    positionSink.reset();
    heldSink.reset();
    indexOf->SetBang(0);
    REQUIRE(positionSink.gotInt);
    CHECK(positionSink.intValue == -1);
    index->SetBang(0);
    CHECK_FALSE(heldSink.gotInt);
    CHECK(missSink.bangCount == 1);
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("array.indexof: a search asked for over in-patcher delivery lands on T_DSP (#786)") {
    // A .r feeding the value inlet dispatches on T_DSP when the block drains
    // it (issue #225) — "the audio thread asks where a value is" is the
    // ordinary case, and the whole path is one guard hold, one bounded scan
    // and a send of one int.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("afd786o");

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go786o");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "a786o");
    YSE::pHandle* find = p.CreateObject(YSE::OBJ::G_ARRAY_INDEXOF, "a786o 4");
    REQUIRE(recv != nullptr);
    REQUIRE(array != nullptr);
    REQUIRE(find != nullptr);
    p.Connect(recv, 0, find, 0);
    p.Connect(find, 0, &sinkHandle, 0);

    array->SetListData(0, "append 10 4 7");

    p.PassData(std::string("array a786o"), "go786o", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 1);
  }

  TEST_CASE("array.indexof/.index: no message path allocates (#786)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path of both objects: the bang, the int, the float, the
    // symbol search, the reference gesture, the wrong-name refusals on both
    // inlets, the multi-atom refusal, and both of .array.index's reports —
    // the hit and the miss.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string reference = "array probeAF786";
    const std::string wrongName = "array somewhere_else_long";
    const std::string symbol = "c4";
    const std::string absent = "not_in_this_array";
    const std::string listValue = "10 c4";

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("afd786p");
    MultiSink positionSink;
    MultiSink heldSink;
    BangSink missSink;
    gArray array;
    gArrayIndexOf indexOf;
    gArrayIndex index;
    array.SetParent(&p);
    array.SetParams("probeAF786");
    indexOf.SetParent(&p);
    indexOf.SetParams("probeAF786");
    index.SetParent(&p);
    index.SetParams("probeAF786");
    Wire(indexOf, 0, positionSink);
    Wire(index, 0, heldSink);
    Wire(index, 1, missSink);

    array.GetInlet(0)->SetList("append 10 c4 7.5", YSE::T_GUI);

    // Warm every path — including the sinks' assignments — so first-call
    // machinery is not what the probe catches.
    indexOf.GetInlet(0)->SetInt(10, YSE::T_GUI);
    indexOf.GetInlet(0)->SetFloat(7.5f, YSE::T_GUI);
    indexOf.GetInlet(0)->SetList(symbol, YSE::T_GUI);
    indexOf.GetInlet(0)->SetBang(YSE::T_GUI);
    indexOf.GetInlet(0)->SetList(reference, YSE::T_GUI);
    indexOf.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    indexOf.GetInlet(0)->SetList(listValue, YSE::T_GUI);
    indexOf.GetInlet(1)->SetList(reference, YSE::T_GUI);
    indexOf.GetInlet(1)->SetList(wrongName, YSE::T_GUI);
    index.GetInlet(0)->SetList(symbol, YSE::T_GUI);
    index.GetInlet(0)->SetList(absent, YSE::T_GUI);
    const std::uint64_t indexOfBefore = indexOf.Dropped();
    const std::uint64_t indexBefore = index.Dropped();

    positionSink.reset();
    heldSink.reset();
    missSink.bangCount = 0;
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      indexOf.GetInlet(0)->SetInt(10, YSE::T_DSP);
      indexOf.GetInlet(0)->SetFloat(7.5f, YSE::T_DSP);
      indexOf.GetInlet(0)->SetList(symbol, YSE::T_DSP);
      indexOf.GetInlet(0)->SetBang(YSE::T_DSP);
      indexOf.GetInlet(0)->SetList(reference, YSE::T_DSP);
      indexOf.GetInlet(0)->SetList(wrongName, YSE::T_DSP);
      indexOf.GetInlet(0)->SetList(listValue, YSE::T_DSP);
      indexOf.GetInlet(1)->SetList(reference, YSE::T_DSP);
      indexOf.GetInlet(1)->SetList(wrongName, YSE::T_DSP);
      index.GetInlet(0)->SetList(symbol, YSE::T_DSP);
      index.GetInlet(0)->SetList(absent, YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing. The searches answered, both wrong-name
    // refusals and the multi-atom refusal were counted on .array.indexof,
    // and .array.index reported both a hit and a miss.
    CHECK(positionSink.gotInt);
    CHECK(positionSink.intValue == 1);
    CHECK(indexOf.Dropped() == indexOfBefore + 3);
    CHECK(heldSink.gotInt);
    CHECK(heldSink.intValue == 1);
    CHECK(missSink.bangCount == 1);
    CHECK(index.Dropped() == indexBefore);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("array.indexof/.index: params survive a DumpJSON / ParseJSON round trip (#786)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* indexOf = src.CreateObject(YSE::OBJ::G_ARRAY_INDEXOF, "notes786 c4");
    YSE::pHandle* index = src.CreateObject(YSE::OBJ::G_ARRAY_INDEX, "notes786 60");
    REQUIRE(indexOf != nullptr);
    REQUIRE(index != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 2);

    bool sawIndexOf = false;
    bool sawIndex = false;
    for (unsigned int i = 0; i < loaded.Objects(); i++) {
      YSE::pHandle* copy = loaded.GetHandleFromList(i);
      REQUIRE(copy != nullptr);
      if (std::string(copy->Type()) == std::string(".array.indexof")) {
        sawIndexOf = true;
        CHECK(copy->GetParams() == std::string("notes786 c4"));
        CHECK(copy->GetInputs() == 2);
        CHECK(copy->GetOutputs() == 1);
      } else if (std::string(copy->Type()) == std::string(".array.index")) {
        sawIndex = true;
        CHECK(copy->GetParams() == std::string("notes786 60"));
        CHECK(copy->GetInputs() == 2);
        CHECK(copy->GetOutputs() == 2);
      }
    }
    CHECK(sawIndexOf);
    CHECK(sawIndex);
  }

  TEST_CASE("array.indexof/.index: carry complete documentation metadata (#786)") {
    gArrayIndexOf indexOf;
    CHECK_FALSE(indexOf.GetDescription().empty());
    CHECK(indexOf.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    const auto& indexOfDocs = indexOf.GetParamDocs();
    REQUIRE(indexOfDocs.size() == 2);
    CHECK(indexOfDocs[0].name == "name");
    CHECK(indexOfDocs[1].name == "value");

    gArrayIndex index;
    CHECK_FALSE(index.GetDescription().empty());
    CHECK(index.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    const auto& indexDocs = index.GetParamDocs();
    REQUIRE(indexDocs.size() == 2);
    CHECK(indexDocs[0].name == "name");
    CHECK(indexDocs[1].name == "value");
  }
}

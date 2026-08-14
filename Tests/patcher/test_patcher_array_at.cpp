// Tests for .array.at (issue #782) — Max's array.at ("output the element at
// an index") on the name-addressed value model .array settled (#548).
//
// What has to be proven, and what every case below is one of:
//
//   - **the array is bound from the creation argument.** An array never
//     travels down a cord, so ".array.at <name>" resolves the name once, on
//     the control thread, and an `array <name>` message is honoured only when
//     it names the array already bound — ArrayReferenceNames' bounded
//     compare, never a registry lookup on a message path.
//   - **an index arriving on a cord fetches.** An int fetches that position
//     and stores it, a bang re-fetches at the stored index, a float
//     truncates to an int first (Max's float method), and the element leaves
//     typed the way the patcher spells it.
//   - **an index is a position.** Out of range is a miss on the miss outlet
//     — .array's own miss rule — and negative is refused and counted, never
//     wrapped or clamped: the family rule gArray.h decides once.
//   - **a list of indices is honoured, and answered whole.** One list in the
//     order asked; any position missing is one miss bang and no partial
//     reply, because a reply shorter than the request misaligns every
//     position after the cut. A list never moves the stored index.
//   - **a fetch is atomic** — the concurrent-write decision #782 asks for:
//     every position is read under one hold of the store's guard and the
//     send happens after it is released, so a write from the fetch's own
//     downstream subgraph lands in the store and never in the reply in
//     flight.
//   - **the fetch crosses the control/audio boundary and nothing allocates.**
//     In-patcher delivery dispatches on T_DSP, so "the audio thread asks for
//     an element" is the ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names an array uses names of its own — one case's contents
// must not be visible to the next.

#include <doctest/doctest.h>
#include <cstdint>
#include <string>

#include "patcher/genericObjects/gArray.h"
#include "patcher/genericObjects/gArrayAt.h"
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
using YSE::PATCHER::gArrayAt;

namespace {

  // An .array and an .array.at on one name, sharing one
  // patcherImplementation so the name actually binds ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // sinks are declared before the objects so they are torn down last, while
  // the outlets wired to them still exist (see sinks.hpp on why that
  // matters).
  struct Rig {
    MultiSink element;
    BangSink miss;
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    gArray array;
    gArrayAt at;

    Rig(const std::string& patcherName, const std::string& name,
        const std::string& atArgs = std::string()) {
      p.SetName(patcherName);
      array.SetParent(&p);
      array.SetParams(name);
      at.SetParent(&p);
      at.SetParams(atArgs.empty() ? name : atArgs);
      Wire(at, 0, element);
      Wire(at, 1, miss);
    }

    void Store(const std::string& message) {
      array.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void Reset() {
      element.reset();
      miss.bangCount = 0;
      miss.gotBang = false;
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("array.at: registered, two inlets, two outlets (#782)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY_AT);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".array.at");
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 2);
  }

  TEST_CASE("array.at: appears in the registry's name list (#782)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_ARRAY_AT)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("array.at: the index inlet takes everything, the reference inlet only lists (#782)") {
    // Inlet 0 is the index — int, float, bang and list all mean a fetch.
    // Inlet 1 only ever carries the "array <name>" reference, which is list
    // text; a bare number there names nothing.
    gArrayAt g;
    const unsigned int indexIn = g.GetInlet(0)->GetAcceptedTypes();
    CHECK((indexIn & YSE::PATCHER::IT_INT) != 0);
    CHECK((indexIn & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((indexIn & YSE::PATCHER::IT_BANG) != 0);
    CHECK((indexIn & YSE::PATCHER::IT_LIST) != 0);
    const unsigned int refIn = g.GetInlet(1)->GetAcceptedTypes();
    CHECK((refIn & YSE::PATCHER::IT_LIST) != 0);
    CHECK((refIn & YSE::PATCHER::IT_INT) == 0);
    CHECK((refIn & YSE::PATCHER::IT_FLOAT) == 0);
    CHECK((refIn & YSE::PATCHER::IT_BANG) == 0);
  }

  // ─── the fetch ──────────────────────────────────────────────────────────────

  TEST_CASE("array.at: an int fetches, and the element is typed by its spelling (#782)") {
    // The object's whole point: the index arrives on a cord, and what comes
    // out is usable — an int-spelled element leaves as an int, a
    // float-spelled one as a float, anything else as a symbol. SendAtom's
    // rule, the same one .array's own "get" follows.
    Rig rig("aat782a", "a782a");
    rig.Store("append 10 c4 7.5");

    rig.at.GetInlet(0)->SetInt(0, YSE::T_GUI);
    CHECK(rig.element.gotInt);
    CHECK(rig.element.intValue == 10);

    rig.Reset();
    rig.at.GetInlet(0)->SetInt(1, YSE::T_GUI);
    CHECK(rig.element.gotList);
    CHECK(rig.element.listValue == "c4");

    rig.Reset();
    rig.at.GetInlet(0)->SetInt(2, YSE::T_GUI);
    CHECK(rig.element.gotFloat);
    CHECK(rig.element.floatValue == doctest::Approx(7.5f));

    CHECK(rig.miss.bangCount == 0);
    CHECK(rig.at.Dropped() == 0);
  }

  TEST_CASE("array.at: a bang re-fetches at the stored index, seeded by the argument (#782)") {
    // ".array.at <name> 2" starts with index 2, so a bang before any int has
    // arrived already names a position — and an int moves the index, so the
    // next bang follows it.
    Rig rig("aat782b", "a782b", "a782b 2");
    rig.Store("append 10 4 7");
    CHECK(rig.at.Index() == 2);

    rig.at.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.element.gotInt);
    CHECK(rig.element.intValue == 7);

    rig.Reset();
    rig.at.GetInlet(0)->SetInt(0, YSE::T_GUI);
    CHECK(rig.at.Index() == 0);
    rig.Reset();
    rig.at.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.element.gotInt);
    CHECK(rig.element.intValue == 10);
    CHECK(rig.at.Dropped() == 0);
  }

  TEST_CASE("array.at: a float truncates to an int — Max's float method (#782)") {
    Rig rig("aat782c", "a782c");
    rig.Store("append 10 4 7");

    rig.at.GetInlet(0)->SetFloat(1.9f, YSE::T_GUI);
    CHECK(rig.element.gotInt);
    CHECK(rig.element.intValue == 4);
    CHECK(rig.at.Index() == 1);
    CHECK(rig.at.Dropped() == 0);

    // Negative is refused, not truncated toward a position — including the
    // -0.5 that truncation alone would fold onto element 0.
    rig.Reset();
    rig.at.GetInlet(0)->SetFloat(-0.5f, YSE::T_GUI);
    CHECK_FALSE(rig.element.gotInt);
    CHECK(rig.miss.bangCount == 0);
    CHECK(rig.at.Index() == 1);
    CHECK(rig.at.Dropped() == 1);
  }

  TEST_CASE("array.at: a negative index is refused and moves nothing (#782)") {
    // Never wrapped, never clamped, never counted from the end — the family
    // rule gArray.h decides once. The stored index stays where it was, so a
    // bang after the refusal fetches what it would have fetched before it.
    Rig rig("aat782d", "a782d", "a782d 1");
    rig.Store("append 10 4 7");

    rig.at.GetInlet(0)->SetInt(-1, YSE::T_GUI);
    CHECK_FALSE(rig.element.gotInt);
    CHECK(rig.miss.bangCount == 0);
    CHECK(rig.at.Dropped() == 1);
    CHECK(rig.at.Index() == 1);

    rig.at.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.element.gotInt);
    CHECK(rig.element.intValue == 4);
  }

  TEST_CASE("array.at: out of range is a miss, and the index still moves (#782)") {
    // The miss leaves the miss outlet, not the element one — .array's own
    // split, so a patch can tell "no such element" from an element it
    // received. The index stores anyway: a miss is a property of the array
    // at that moment, not of the request, so a bang after the array has
    // grown finds the element the earlier fetch could not.
    Rig rig("aat782e", "a782e");
    rig.Store("append 10 4 7");

    rig.at.GetInlet(0)->SetInt(5, YSE::T_GUI);
    CHECK_FALSE(rig.element.gotInt);
    CHECK(rig.miss.bangCount == 1);
    CHECK(rig.at.Dropped() == 0);
    CHECK(rig.at.Index() == 5);

    rig.Store("append 9 11 13");
    rig.Reset();
    rig.at.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.element.gotInt);
    CHECK(rig.element.intValue == 13);
    CHECK(rig.miss.bangCount == 0);
  }

  // ─── the list fetch ─────────────────────────────────────────────────────────

  TEST_CASE("array.at: a list of indices is answered whole, in the order asked (#782)") {
    // Max's array.at takes several indices; here the reply is one list, the
    // positions in the order the request named them — every one read under a
    // single hold of the store's guard, so the reply is the array as it
    // stood at the trigger. A one-index list leaves as the element itself,
    // the patcher's transport rule for a list of one.
    Rig rig("aat782f", "a782f", "a782f 2");
    rig.Store("append 10 4 7");

    rig.at.GetInlet(0)->SetList("2 0 1", YSE::T_GUI);
    CHECK(rig.element.gotList);
    CHECK(rig.element.listValue == "7 10 4");

    // A list never moves the stored index: it is a compound fetch, not a
    // cursor move, and a bang that replayed the last list would be hidden
    // state no patch can see.
    CHECK(rig.at.Index() == 2);

    rig.Reset();
    rig.at.GetInlet(0)->SetList("1", YSE::T_GUI);
    CHECK(rig.element.gotInt);
    CHECK(rig.element.intValue == 4);
    CHECK(rig.miss.bangCount == 0);
    CHECK(rig.at.Dropped() == 0);
  }

  TEST_CASE("array.at: a list with any position missing is one miss and no partial reply (#782)") {
    // A reply shorter than the request would misalign every position after
    // the cut — truncation by another name, and the family refuses rather
    // than truncates.
    Rig rig("aat782g", "a782g");
    rig.Store("append 10 4 7");

    rig.at.GetInlet(0)->SetList("1 9", YSE::T_GUI);
    CHECK_FALSE(rig.element.gotList);
    CHECK_FALSE(rig.element.gotInt);
    CHECK(rig.miss.bangCount == 1);
    CHECK(rig.at.Dropped() == 0);
  }

  TEST_CASE("array.at: a malformed index list is refused whole and counted (#782)") {
    // A symbol, a negative, or a float-spelled token names no position, and
    // a request that is partly malformed is refused whole rather than the
    // readable half being answered — counted, not logged, since the inlet
    // may be the audio thread.
    Rig rig("aat782h", "a782h");
    rig.Store("append 10 4 7");

    const std::uint64_t before = rig.at.Dropped();
    rig.at.GetInlet(0)->SetList("1 x", YSE::T_GUI);
    rig.at.GetInlet(0)->SetList("1 -2", YSE::T_GUI);
    rig.at.GetInlet(0)->SetList("2.5", YSE::T_GUI);
    CHECK_FALSE(rig.element.gotList);
    CHECK_FALSE(rig.element.gotInt);
    CHECK(rig.miss.bangCount == 0);
    CHECK(rig.at.Dropped() == before + 3);
  }

  // ─── the reference ──────────────────────────────────────────────────────────

  TEST_CASE("array.at: the reference fetches on the index inlet, anything else refused (#782)") {
    // "array <name>" is the message an .array's reference outlet emits on a
    // bang; on the index inlet it fetches at the stored index — the family
    // gesture: bang the array, out comes the current element. A reference to
    // an array this object is not bound to is refused, never resolved: a
    // registry lookup is a mutex, and this may be the audio thread.
    Rig rig("aat782i", "a782i", "a782i 1");
    rig.Store("append 10 4 7");

    const std::uint64_t before = rig.at.Dropped();
    rig.at.GetInlet(0)->SetList("array a782i", YSE::T_GUI);
    CHECK(rig.element.gotInt);
    CHECK(rig.element.intValue == 4);
    CHECK(rig.at.Dropped() == before);

    rig.Reset();
    rig.at.GetInlet(0)->SetList("array somewhere_else", YSE::T_GUI);
    CHECK_FALSE(rig.element.gotInt);
    CHECK(rig.at.Dropped() == before + 1);
  }

  TEST_CASE("array.at: the reference inlet acknowledges its own array and nothing more (#782)") {
    // Wiring the array's reference outlet across keeps the patch readable,
    // and the acknowledgement is silent — the binding is the creation
    // argument, so there is nothing to set. Anything else there is refused,
    // which is what makes a mis-wired cord visible in Dropped().
    Rig rig("aat782j", "a782j");
    rig.Store("append 10 4 7");

    const std::uint64_t before = rig.at.Dropped();
    rig.at.GetInlet(1)->SetList("array a782j", YSE::T_GUI);
    CHECK_FALSE(rig.element.gotInt);
    CHECK_FALSE(rig.element.gotList);
    CHECK(rig.miss.bangCount == 0);
    CHECK(rig.at.Dropped() == before);

    rig.at.GetInlet(1)->SetList("array somewhere_else", YSE::T_GUI);
    rig.at.GetInlet(1)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK(rig.at.Dropped() == before + 2);
  }

  TEST_CASE("array.at: wired from the array's reference outlet, banging the array fetches (#782)") {
    // The flow a patch actually wires, end to end through the public patcher
    // API: an int into the index inlet fetches, and the .array's reference
    // outlet into the same inlet gives the family gesture — bang the array,
    // out comes the current element.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    BangSink miss;
    YSE::pHandle missHandle(&miss);
    YSE::patcher p;
    p.create(2);
    p.name("aat782k");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "a782k");
    YSE::pHandle* at = p.CreateObject(YSE::OBJ::G_ARRAY_AT, "a782k");
    REQUIRE(array != nullptr);
    REQUIRE(at != nullptr);
    p.Connect(array, 1, at, 0);
    p.Connect(at, 0, &sinkHandle, 0);
    p.Connect(at, 1, &missHandle, 0);

    array->SetListData(0, "append 60 64 67");
    at->SetIntData(0, 1);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 64);

    sink.reset();
    array->SetBang(0);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 64);
    CHECK_FALSE(miss.gotBang);
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("array.at: the address form is the patcher's, and RefreshBinding follows it (#782)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aat782l_before");

    gArrayAt g;
    g.SetParams("a782l");
    g.SetParent(&p);
    CHECK(g.ArrayName() == "a782l");
    CHECK(g.Address() == "aat782l_before.a782l");

    // Idempotent: a rebind to the address it already has keeps the store.
    g.RefreshBinding();
    CHECK(g.Address() == "aat782l_before.a782l");

    p.SetName("aat782l_after");
    g.RefreshBinding();
    CHECK(g.Address() == "aat782l_after.a782l");
  }

  TEST_CASE("array.at: patcherImplementation::SetName re-anchors it (#782)") {
    // The rename dispatch itself, which the standalone case above cannot
    // reach: an object created *inside* a patcher must be re-anchored by the
    // patcher, without anybody calling RefreshBinding by hand. The keeper
    // holds the old-address store (it is not in the patcher's object map, so
    // the rename does not touch it): before the rename a fetch finds the
    // keeper's element; after it the fetch reads a fresh empty array under
    // the new prefix, so the same index is a miss.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    BangSink miss;
    YSE::pHandle missHandle(&miss);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aat782m_before");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a782m");
    keeper.GetInlet(0)->SetList("append 10 4 7", YSE::T_GUI);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY_AT, "a782m");
    REQUIRE(h != nullptr);
    p.Connect(h, 0, &sinkHandle, 0);
    p.Connect(h, 1, &missHandle, 0);

    h->SetIntData(0, 2);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 7);
    CHECK_FALSE(miss.gotBang);

    p.SetName("aat782m_after");
    sink.reset();
    h->SetIntData(0, 2);
    CHECK_FALSE(sink.gotInt);
    CHECK(miss.gotBang);
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("array.at: a fetch asked for over in-patcher delivery lands on T_DSP (#782)") {
    // A .r feeding the index inlet dispatches on T_DSP when the block drains
    // it (issue #225) — "the audio thread asks for an element" is the
    // ordinary case, and the whole path is one guard hold, a bounded copy
    // and a send of storage the object already owns.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aat782n");

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go782n");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "a782n");
    YSE::pHandle* at = p.CreateObject(YSE::OBJ::G_ARRAY_AT, "a782n");
    REQUIRE(recv != nullptr);
    REQUIRE(array != nullptr);
    REQUIRE(at != nullptr);
    p.Connect(recv, 0, at, 0);
    p.Connect(at, 0, &sinkHandle, 0);

    array->SetListData(0, "append 10 4 7");

    p.PassData(1, "go782n", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 4);
  }

  TEST_CASE("array.at: no message path allocates (#782)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path: the int, float, bang and list fetches, the hit,
    // the miss, the reference on both inlets, the wrong-name refusal and
    // the malformed list.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string indices = "2 0 1";
    const std::string reference = "array probeA782";
    const std::string wrongName = "array somewhere_else_long";
    const std::string malformed = "1 not_an_index_at_all";

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aat782o");
    MultiSink element;
    BangSink miss;
    gArray array;
    gArrayAt at;
    array.SetParent(&p);
    array.SetParams("probeA782");
    at.SetParent(&p);
    at.SetParams("probeA782");
    Wire(at, 0, element);
    Wire(at, 1, miss);

    array.GetInlet(0)->SetList("append 10 a-symbol-past-every-small-string-buffer 7.5", YSE::T_GUI);

    // Warm every path — including the sink's list assignment — so
    // first-call machinery is not what the probe catches.
    at.GetInlet(0)->SetInt(1, YSE::T_GUI);
    at.GetInlet(0)->SetFloat(0.f, YSE::T_GUI);
    at.GetInlet(0)->SetBang(YSE::T_GUI);
    at.GetInlet(0)->SetList(indices, YSE::T_GUI);
    at.GetInlet(0)->SetList(reference, YSE::T_GUI);
    at.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    at.GetInlet(0)->SetList(malformed, YSE::T_GUI);
    at.GetInlet(0)->SetInt(9, YSE::T_GUI);
    at.GetInlet(1)->SetList(reference, YSE::T_GUI);
    at.GetInlet(1)->SetList(wrongName, YSE::T_GUI);
    const std::uint64_t before = at.Dropped();
    const int misses = miss.bangCount;

    element.reset();
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      at.GetInlet(0)->SetInt(1, YSE::T_DSP);
      at.GetInlet(0)->SetFloat(0.f, YSE::T_DSP);
      at.GetInlet(0)->SetBang(YSE::T_DSP);
      at.GetInlet(0)->SetList(indices, YSE::T_DSP);
      at.GetInlet(0)->SetList(reference, YSE::T_DSP);
      at.GetInlet(0)->SetList(wrongName, YSE::T_DSP);
      at.GetInlet(0)->SetList(malformed, YSE::T_DSP);
      at.GetInlet(0)->SetInt(9, YSE::T_DSP);
      at.GetInlet(1)->SetList(reference, YSE::T_DSP);
      at.GetInlet(1)->SetList(wrongName, YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing. The list fetch answered, the out-of-range
    // index missed, and all three refusals (the wrong name on each inlet,
    // the malformed list) were counted.
    CHECK(element.gotList);
    CHECK(element.listValue == "7.5 10 a-symbol-past-every-small-string-buffer");
    CHECK(miss.bangCount == misses + 1);
    CHECK(at.Dropped() == before + 3);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("array.at: params survive a DumpJSON / ParseJSON round trip (#782)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_ARRAY_AT, "notes782 3");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".array.at"));
    CHECK(copy->GetParams() == std::string("notes782 3"));
    CHECK(copy->GetInputs() == 2);
    CHECK(copy->GetOutputs() == 2);
  }

  TEST_CASE("array.at: carries complete documentation metadata (#782)") {
    gArrayAt g;
    CHECK_FALSE(g.GetDescription().empty());
    CHECK(g.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    const auto& docs = g.GetParamDocs();
    REQUIRE(docs.size() == 2);
    CHECK(docs[0].name == "name");
    CHECK(docs[1].name == "index");
  }
}

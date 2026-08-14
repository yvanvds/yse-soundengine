// Tests for .array.sort (issue #789) — Max's array.sort on the name-addressed
// value model .array settled (#548).
//
// What has to be proven, and what every case below is one of:
//
//   - **the array is bound from the creation argument.** An array never
//     travels down a cord, so ".array.sort <name>" resolves the name once, on
//     the control thread, and an `array <name>` message is honoured only when
//     it names the array already bound — ArrayReferenceNames' bounded
//     compare, never a registry lookup on a message path.
//   - **the comparison is .zl sort's, over the store.** Numbers before
//     symbols in both directions, numbers by value whatever their spelling,
//     symbols by their characters with the shorter first when one is a
//     prefix — and the sort is stable, so equal elements keep arrival order,
//     which is what makes the published order one a patch can reason about.
//   - **the direction is .zl sort's argument.** Negative descends, anything
//     else ascends, seeded by the second creation argument, moved silently
//     by the cold inlet, and applied-not-stored when it arrives inline on
//     the trigger.
//   - **a sort that lands publishes the applied order before the
//     reference** — zero-based, .zl sort's index map, the idiom that puts a
//     parallel array into the same new order through .array.indexmap.
//   - **the whole sort is one guard hold, and nothing allocates.**
//     In-patcher delivery dispatches on T_DSP, so "the audio thread sorts an
//     array" is the ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names an array uses names of its own — one case's contents
// must not be visible to the next.

#include <doctest/doctest.h>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "patcher/genericObjects/gArray.h"
#include "patcher/genericObjects/gArraySort.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using TestHelpers::MultiSink;
using TestHelpers::OrderSink;
using TestHelpers::Wire;
using YSE::PATCHER::gArray;
using YSE::PATCHER::gArraySort;

namespace {

  // An .array and one .array.sort on one name, sharing one
  // patcherImplementation so the name actually binds ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // sink is declared before the objects so it is torn down last, while the
  // outlet wired to it still exists (see sinks.hpp on why that matters).
  struct Rig {
    MultiSink reference; // outlet 0: the reference after a landed sort
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    gArray array;
    gArraySort object;

    Rig(const std::string& patcherName, const std::string& name,
        const std::string& params = std::string()) {
      p.SetName(patcherName);
      array.SetParent(&p);
      array.SetParams(name);
      object.SetParent(&p);
      object.SetParams(params.empty() ? name : params);
      Wire(object, 0, reference);
    }

    void Store(const std::string& message) {
      array.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    // The array's contents as one space-joined string — what the sort is
    // asserted against. Control thread only (ElementAt allocates).
    std::string Contents() {
      std::string out;
      for (std::size_t i = 0; i < array.Count(); i++) {
        if (i != 0) out += ' ';
        out += array.ElementAt(i);
      }
      return out;
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("array.sort: registered, with .array.rotate's inlets over the pair's outlets "
            "(#789)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* sort = p.CreateObject(YSE::OBJ::G_ARRAY_SORT);
    REQUIRE(sort != nullptr);
    CHECK(std::string(sort->Type()) == ".array.sort");
    CHECK(sort->GetInputs() == 3);
    CHECK(sort->GetOutputs() == 2);

    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_ARRAY_SORT)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("array.sort: the trigger takes what the ask needs, the cold inlets only their "
            "configuration (#789)") {
    // The trigger takes the inline direction too — rotate's trigger; the
    // direction inlet is a number and nothing else, and the reference inlet
    // is list text only.
    gArraySort sort;
    const unsigned int trigger = sort.GetInlet(0)->GetAcceptedTypes();
    CHECK((trigger & YSE::PATCHER::IT_BANG) != 0);
    CHECK((trigger & YSE::PATCHER::IT_INT) != 0);
    CHECK((trigger & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((trigger & YSE::PATCHER::IT_LIST) != 0);
    const unsigned int direction = sort.GetInlet(1)->GetAcceptedTypes();
    CHECK((direction & YSE::PATCHER::IT_INT) != 0);
    CHECK((direction & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((direction & YSE::PATCHER::IT_LIST) == 0);
    CHECK((direction & YSE::PATCHER::IT_BANG) == 0);
    const unsigned int ref = sort.GetInlet(2)->GetAcceptedTypes();
    CHECK((ref & YSE::PATCHER::IT_LIST) != 0);
    CHECK((ref & YSE::PATCHER::IT_BANG) == 0);
  }

  // ─── the comparison ─────────────────────────────────────────────────────────

  TEST_CASE("array.sort: numbers by value before symbols by characters, stably (#789)") {
    Rig rig("aso789a", "a789a");

    // Empty: the ask is well-formed and the answer is what it says — the
    // sort of nothing lands, and the reference leaves.
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.array.Count() == 0);
    CHECK(rig.reference.gotList);
    CHECK(rig.reference.listValue == "array a789a");

    // Numbers before symbols; numbers by the value they spell, so 7. and 7
    // are the same number and — the sort being stable — keep the order they
    // arrived in; symbols by their characters, the shorter first when one
    // is a prefix of the other.
    rig.Store("append 20 7. c4 7 10 c 100");
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Contents() == "7. 7 10 20 100 c c4");

    // Sorting the sorted array is the identity — one more stability fact:
    // nothing equal trades places on a second pass.
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Contents() == "7. 7 10 20 100 c c4");
    CHECK(rig.object.Dropped() == 0);
  }

  TEST_CASE("array.sort: descending reverses within each type, never across the split (#789)") {
    // Numbers before symbols in *both* directions — which of the two an
    // element is, is a type ordering rather than a value one, so ascending
    // and descending stay one question asked two ways. Equal numbers keep
    // arrival order in both directions too.
    Rig rig("aso789b", "a789b");
    rig.Store("append 20 7. c4 7 10 c");

    rig.object.GetInlet(0)->SetInt(-1, YSE::T_GUI);
    CHECK(rig.Contents() == "20 10 7. 7 c4 c");
    CHECK(rig.reference.gotList);

    // Anything non-negative ascends — 0 included, .zl sort's unset
    // argument.
    rig.object.GetInlet(0)->SetInt(0, YSE::T_GUI);
    CHECK(rig.Contents() == "7. 7 10 20 c c4");
    CHECK(rig.object.Dropped() == 0);
  }

  // ─── the direction ──────────────────────────────────────────────────────────

  TEST_CASE("array.sort: a bang sorts by the stored direction — seeded by the argument, moved "
            "silently by the direction inlet, inline directions stored nowhere (#789)") {
    // ".array.sort <name> -1" starts descending, so a bang already sorts
    // downwards. The direction inlet is the cold half of the Max idiom.
    Rig rig("aso789c", "a789c", "a789c -1");
    rig.Store("append 10 30 20");
    CHECK(rig.object.Direction() == -1);

    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Contents() == "30 20 10");
    CHECK(rig.reference.gotList);

    // An inline direction is applied at the moment it arrives and stores
    // nothing — a bang after it sorts by the stored direction, not the
    // inline one. gArrayIndexMap's trigger rule.
    rig.object.GetInlet(0)->SetInt(1, YSE::T_GUI);
    CHECK(rig.Contents() == "10 20 30");
    CHECK(rig.object.Direction() == -1);
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Contents() == "30 20 10");

    // The direction inlet stores silently — nothing emitted, nothing
    // sorted — and a float truncates first; a non-finite one is refused
    // rather than quietly becoming ascending.
    rig.reference.reset();
    rig.object.GetInlet(1)->SetInt(2, YSE::T_GUI);
    CHECK_FALSE(rig.reference.gotList);
    CHECK(rig.Contents() == "30 20 10");
    CHECK(rig.object.Direction() == 2);
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Contents() == "10 20 30");

    rig.object.GetInlet(1)->SetFloat(-1.7f, YSE::T_GUI);
    CHECK(rig.object.Direction() == -1);
    rig.object.GetInlet(1)->SetFloat(std::numeric_limits<float>::quiet_NaN(), YSE::T_GUI);
    CHECK(rig.object.Direction() == -1);
    CHECK(rig.object.Dropped() == 1);

    // A float on the trigger truncates too, and a list spelling exactly one
    // signed int is the direction it spells; a list of more is refused.
    rig.object.GetInlet(0)->SetFloat(1.7f, YSE::T_GUI);
    CHECK(rig.Contents() == "10 20 30");
    rig.object.GetInlet(0)->SetList("-1", YSE::T_GUI);
    CHECK(rig.Contents() == "30 20 10");
    rig.object.GetInlet(0)->SetList("1 2", YSE::T_GUI);
    CHECK(rig.object.Dropped() == 2);

    // A re-parse must not leave half of the previous configuration
    // standing: SetParams("") drops the direction back to 0 along with the
    // name.
    rig.object.SetParams("");
    CHECK(rig.object.Direction() == 0);
  }

  TEST_CASE("array.sort: the reference sorts on the trigger, acknowledges on its own inlet, "
            "and anything else is refused (#789)") {
    Rig rig("aso789d", "a789d");
    rig.Store("append 30 10 20");

    rig.object.GetInlet(0)->SetList("array a789d", YSE::T_GUI);
    CHECK(rig.Contents() == "10 20 30");
    CHECK(rig.reference.gotList);
    CHECK(rig.object.Dropped() == 0);

    // A reference naming an array this object is not bound to is refused
    // everywhere, never resolved: a registry lookup is a mutex, and this may
    // be the audio thread.
    rig.reference.reset();
    rig.object.GetInlet(0)->SetList("array somewhere_else", YSE::T_GUI);
    CHECK_FALSE(rig.reference.gotList);
    CHECK(rig.object.Dropped() == 1);
    rig.object.GetInlet(0)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK(rig.object.Dropped() == 2);

    rig.object.GetInlet(2)->SetList("array a789d", YSE::T_GUI);
    CHECK(rig.object.Dropped() == 2);
    rig.object.GetInlet(2)->SetList("array somewhere_else", YSE::T_GUI);
    CHECK(rig.object.Dropped() == 3);
    CHECK_FALSE(rig.reference.gotList);
  }

  // ─── the order outlet ───────────────────────────────────────────────────────

  TEST_CASE("array.sort: the order outlet publishes the applied picks, before the reference "
            "(#789)") {
    // .zl sort's index map, zero-based because the family's positions are
    // arrayStore's — and sent right before left so a second
    // .array.indexmap's map is in place before the reference sets anything
    // running. The picks are deterministic here, unlike the shuffle's, so
    // the map itself is asserted — including the stable placement of the
    // equal pair.
    std::vector<char> log;
    OrderSink orderOut;
    OrderSink referenceOut;
    orderOut.log = &log;
    orderOut.tag = 'O';
    referenceOut.log = &log;
    referenceOut.tag = 'R';

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aso789e");
    gArray array;
    gArraySort sort;
    array.SetParent(&p);
    array.SetParams("a789e");
    sort.SetParent(&p);
    sort.SetParams("a789e");
    Wire(sort, 0, referenceOut);
    Wire(sort, 1, orderOut);

    // Ascending over "30 7. 7 c 10": 7. (1) and 7 (2) keep arrival order,
    // then 10 (4) and 30 (0), then the symbol (3).
    array.GetInlet(0)->SetList("append 30 7. 7 c 10", YSE::T_GUI);
    sort.GetInlet(0)->SetBang(YSE::T_GUI);

    REQUIRE(log.size() == 2);
    CHECK(log[0] == 'O');
    CHECK(log[1] == 'R');
    CHECK(referenceOut.lastKind == OrderSink::LIST);
    CHECK(referenceOut.lastList == "array a789e");
    REQUIRE(orderOut.lastKind == OrderSink::LIST);
    CHECK(orderOut.lastList == "1 2 4 0 3");

    // An empty array publishes no order — there are no picks to speak of —
    // but the ask still lands and the reference still leaves.
    array.GetInlet(0)->SetList("clear", YSE::T_GUI);
    log.clear();
    sort.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(log.size() == 1);
    CHECK(log[0] == 'R');
  }

  TEST_CASE("array.sort: the order chains into .array.indexmap, putting a parallel array into "
            "the same new order (#789)") {
    // The idiom the order outlet exists for — and the reason #789's
    // second-outlet question is answered yes — run end to end through the
    // public patcher API: sort one array, feed the published order to an
    // .array.indexmap bound to a parallel array, and the two land in the
    // same arrangement. .zl sort's parallel-list gesture on the value
    // model, deterministic where the shuffle's was not.
    MultiSink notesOut;
    MultiSink namesOut;
    YSE::pHandle notesHandle(&notesOut);
    YSE::pHandle namesHandle(&namesOut);
    YSE::patcher p;
    p.create(2);
    p.name("aso789f");
    YSE::pHandle* notes = p.CreateObject(YSE::OBJ::G_ARRAY, "notes789f");
    YSE::pHandle* names = p.CreateObject(YSE::OBJ::G_ARRAY, "names789f");
    YSE::pHandle* sort = p.CreateObject(YSE::OBJ::G_ARRAY_SORT, "notes789f");
    YSE::pHandle* map = p.CreateObject(YSE::OBJ::G_ARRAY_INDEXMAP, "names789f");
    REQUIRE(notes != nullptr);
    REQUIRE(names != nullptr);
    REQUIRE(sort != nullptr);
    REQUIRE(map != nullptr);
    // The order into the map inlet — configuration before the ask, which the
    // right-before-left send order guarantees even when both come from the
    // same trigger.
    p.Connect(sort, 1, map, 1);
    p.Connect(notes, 0, &notesHandle, 0);
    p.Connect(names, 0, &namesHandle, 0);

    notes->SetListData(0, "append 64 60 67 62");
    names->SetListData(0, "append e c g d");

    sort->SetBang(0);
    map->SetBang(0);

    notes->SetListData(0, "getvalue");
    names->SetListData(0, "getvalue");
    REQUIRE(notesOut.gotList);
    REQUIRE(namesOut.gotList);
    CHECK(notesOut.listValue == "60 62 64 67");
    CHECK(namesOut.listValue == "c d e g");
  }

  TEST_CASE("array.sort: wired from the array's reference outlet, banging the array sorts "
            "(#789)") {
    // The flow a patch actually wires, end to end through the public patcher
    // API: the .array's reference outlet into the trigger gives the family
    // gesture — bang the array, out comes the sorted array's reference —
    // and the contents are read back through the array's own "getvalue", so
    // the whole loop runs on cords.
    MultiSink chained;
    MultiSink contents;
    YSE::pHandle chainedHandle(&chained);
    YSE::pHandle contentsHandle(&contents);
    YSE::patcher p;
    p.create(2);
    p.name("aso789g");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "a789g");
    YSE::pHandle* sort = p.CreateObject(YSE::OBJ::G_ARRAY_SORT, "a789g");
    REQUIRE(array != nullptr);
    REQUIRE(sort != nullptr);
    p.Connect(array, 1, sort, 0);
    p.Connect(sort, 0, &chainedHandle, 0);
    p.Connect(array, 0, &contentsHandle, 0);

    array->SetListData(0, "append 67 60 64");
    array->SetBang(0);
    REQUIRE(chained.gotList);
    CHECK(chained.listValue == "array a789g");

    array->SetListData(0, "getvalue");
    REQUIRE(contents.gotList);
    CHECK(contents.listValue == "60 64 67");
  }

  TEST_CASE("array.sort: an unnamed object sorts a private array, silently (#789)") {
    // No name, no shared store, no reference to pass on — the ask lands
    // (over an empty private array), the announcement is simply empty, and
    // nothing is a refusal: the ask was well-formed.
    MultiSink sink;
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aso789h");
    gArraySort sort;
    sort.SetParent(&p);
    Wire(sort, 0, sink);
    CHECK(sort.Address().empty());

    sort.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(sink.gotList);
    CHECK(sort.Dropped() == 0);
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("array.sort: patcherImplementation::SetName re-anchors the shared base (#789)") {
    // The rename dispatch itself: an object created *inside* a patcher must
    // be re-anchored by the patcher, without anybody calling RefreshBinding
    // by hand. The keeper holds the old-address store; after the rename the
    // object acts on a fresh empty array under the new prefix, so the
    // keeper's contents stop moving — while the stored direction survives,
    // being the object's own state rather than anything derived from the
    // name.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aso789i_before");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a789i");
    keeper.GetInlet(0)->SetList("append 30 10 20", YSE::T_GUI);

    YSE::pHandle* sort = p.CreateObject(YSE::OBJ::G_ARRAY_SORT, "a789i -1");
    REQUIRE(sort != nullptr);
    p.Connect(sort, 0, &sinkHandle, 0);

    sort->SetBang(0);
    REQUIRE(sink.gotList);
    CHECK(keeper.Count() == 3);
    CHECK(keeper.ElementAt(0) == "30");
    CHECK(keeper.ElementAt(2) == "10");

    p.SetName("aso789i_after");
    sink.reset();
    sort->SetBang(0);
    // The sort landed — on the new, empty array — and the keeper's contents
    // were not touched.
    CHECK(sink.gotList);
    CHECK(keeper.Count() == 3);
    CHECK(keeper.ElementAt(0) == "30");
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("array.sort: a direction arriving over in-patcher delivery lands on T_DSP (#789)") {
    // A .r feeding the trigger dispatches on T_DSP when the block drains it
    // (issue #225) — "the audio thread sorts an array" is the ordinary
    // case, and the whole path is one bounded parse, one guard hold and a
    // send of a string the object already owns.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aso789j");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a789j");
    keeper.GetInlet(0)->SetList("append 30 10 c 20", YSE::T_GUI);

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go789j");
    YSE::pHandle* sort = p.CreateObject(YSE::OBJ::G_ARRAY_SORT, "a789j");
    REQUIRE(recv != nullptr);
    REQUIRE(sort != nullptr);
    p.Connect(recv, 0, sort, 0);
    p.Connect(sort, 0, &sinkHandle, 0);

    p.PassData(std::string("-1"), "go789j", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "array a789j");
    CHECK(keeper.Count() == 4);
    CHECK(keeper.ElementAt(0) == "30");
    CHECK(keeper.ElementAt(1) == "20");
    CHECK(keeper.ElementAt(2) == "10");
    CHECK(keeper.ElementAt(3) == "c");
  }

  TEST_CASE("array.sort: no message path allocates (#789)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path — the bang, the inline and stored directions, the
    // reference gesture, the refusals, and the reference-inlet
    // acknowledgement — on T_DSP, in-patcher delivery's thread.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string reference = "array probeASO789";
    const std::string wrongName = "array somewhere_else_long";
    const std::string inlineDirection = "-1";
    const std::string malformed = "1 2";

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aso789k");
    MultiSink referenceSink;
    MultiSink orderSink;
    gArray array;
    gArraySort sort;
    array.SetParent(&p);
    array.SetParams("probeASO789");
    sort.SetParent(&p);
    sort.SetParams("probeASO789 -1");
    Wire(sort, 0, referenceSink);
    Wire(sort, 1, orderSink);

    array.GetInlet(0)->SetList("append 30 7. c4 7 10", YSE::T_GUI);

    // Warm every path — including the sinks' assignments — so first-call
    // machinery is not what the probe catches.
    sort.GetInlet(0)->SetBang(YSE::T_GUI);
    sort.GetInlet(0)->SetInt(1, YSE::T_GUI);
    sort.GetInlet(0)->SetFloat(-1.5f, YSE::T_GUI);
    sort.GetInlet(0)->SetList(inlineDirection, YSE::T_GUI);
    sort.GetInlet(0)->SetList(reference, YSE::T_GUI);
    sort.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    sort.GetInlet(0)->SetList(malformed, YSE::T_GUI);
    sort.GetInlet(1)->SetInt(1, YSE::T_GUI);
    sort.GetInlet(1)->SetFloat(-2.5f, YSE::T_GUI);
    sort.GetInlet(2)->SetList(reference, YSE::T_GUI);

    // Reset the contents, then run the same sequence on T_DSP under the
    // probe.
    array.GetInlet(0)->SetList("clear", YSE::T_GUI);
    array.GetInlet(0)->SetList("append 30 7. c4 7 10", YSE::T_GUI);
    const std::uint64_t drops = sort.Dropped();
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      sort.GetInlet(0)->SetBang(YSE::T_DSP);
      sort.GetInlet(0)->SetInt(1, YSE::T_DSP);
      sort.GetInlet(0)->SetFloat(-1.5f, YSE::T_DSP);
      sort.GetInlet(0)->SetList(inlineDirection, YSE::T_DSP);
      sort.GetInlet(0)->SetList(reference, YSE::T_DSP);
      sort.GetInlet(0)->SetList(wrongName, YSE::T_DSP); // refused
      sort.GetInlet(0)->SetList(malformed, YSE::T_DSP); // refused
      sort.GetInlet(1)->SetInt(2, YSE::T_DSP); // stored, silent
      sort.GetInlet(1)->SetFloat(-3.5f, YSE::T_DSP); // stored, silent
      sort.GetInlet(2)->SetList(reference, YSE::T_DSP); // acknowledged
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing. The last sort under the probe was the
    // reference gesture, descending by the direction the warm-up's cold
    // float stored (-2); the direction inlet moved the stored direction
    // afterwards, so it ends at the final float's truncation; and exactly
    // the two refusals were counted.
    CHECK(array.Count() == 5);
    CHECK(referenceSink.gotList);
    CHECK(orderSink.gotList);
    CHECK(sort.Direction() == -3);
    CHECK(sort.Dropped() == drops + 2);
    std::string contents;
    for (std::size_t i = 0; i < array.Count(); i++) {
      if (i != 0) contents += ' ';
      contents += array.ElementAt(i);
    }
    CHECK(contents == "30 10 7. 7 c4");
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("array.sort: params survive a DumpJSON / ParseJSON round trip (#789)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* sort = src.CreateObject(YSE::OBJ::G_ARRAY_SORT, "notes789l -1");
    REQUIRE(sort != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    // Found by type rather than by list position: the loaded patcher's
    // enumeration order is not the creation order.
    YSE::pHandle* copy = nullptr;
    for (unsigned int i = 0; i < loaded.Objects(); i++) {
      YSE::pHandle* handle = loaded.GetHandleFromList(static_cast<int>(i));
      REQUIRE(handle != nullptr);
      if (std::string(handle->Type()) == ".array.sort") copy = handle;
    }

    REQUIRE(copy != nullptr);
    // The analyzer cannot see that a failed REQUIRE aborts the case (doctest's
    // failure path is a runtime jump), so it assumes `copy` may be null here.
    // NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage)
    CHECK(copy->GetParams() == std::string("notes789l -1"));
    CHECK(copy->GetInputs() == 3);
    CHECK(copy->GetOutputs() == 2);
  }

  TEST_CASE("array.sort: carries complete documentation metadata (#789)") {
    gArraySort sort;
    CHECK_FALSE(sort.GetDescription().empty());
    CHECK(sort.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    REQUIRE(sort.GetParamDocs().size() == 2);
    CHECK(sort.GetParamDocs()[0].name == "name");
    CHECK(sort.GetParamDocs()[1].name == "direction");
  }
}

// Tests for .array.indexmap (issue #787) — Max's array.indexmap ("reorder an
// array using a list of indices") on the name-addressed value model .array
// settled (#548).
//
// What has to be proven, and what every case below is one of:
//
//   - **the array is bound from the creation argument.** An array never
//     travels down a cord, so ".array.indexmap <name> [<indices...>]"
//     resolves the name once, on the control thread, and an `array <name>`
//     message is honoured only when it names the array already bound —
//     ArrayReferenceNames' bounded compare, never a registry lookup on a
//     message path.
//   - **the map is a list of independent picks, applied in map order.**
//     Entries may repeat, an index naming no element contributes nothing —
//     AssignOrder's rule, #787's spec — so the result is as long as what
//     landed; a negative index refuses the whole map, the family's split
//     between a miss and a refusal.
//   - **the stored map is the object's configuration.** The trailing
//     creation arguments seed it, a list on the map inlet replaces it, a
//     bang — or the bound array's reference — applies it; a list on the
//     trigger is applied at the moment it arrives and stores nothing.
//   - **the whole reorder is one guard hold, and nothing allocates.**
//     In-patcher delivery dispatches on T_DSP, so "the audio thread reorders
//     an array" is the ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names an array uses names of its own — one case's contents
// must not be visible to the next.

#include <doctest/doctest.h>
#include <cstdint>
#include <string>

#include "patcher/genericObjects/gArray.h"
#include "patcher/genericObjects/gArrayIndexMap.h"
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
using TestHelpers::Wire;
using YSE::PATCHER::gArray;
using YSE::PATCHER::gArrayIndexMap;

namespace {

  // An .array and an .array.indexmap on one name, sharing one
  // patcherImplementation so the name actually binds ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // sink is declared before the objects so it is torn down last, while the
  // outlet wired to it still exists (see sinks.hpp on why that matters).
  struct Rig {
    MultiSink reference; // indexmap's one outlet: the reference after a landed reorder
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    gArray array;
    gArrayIndexMap map;

    Rig(const std::string& patcherName, const std::string& name,
        const std::string& mapArgs = std::string()) {
      p.SetName(patcherName);
      array.SetParent(&p);
      array.SetParams(name);
      map.SetParent(&p);
      map.SetParams(mapArgs.empty() ? name : mapArgs);
      Wire(map, 0, reference);
    }

    void Store(const std::string& message) {
      array.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    // The array's contents as one space-joined string — what the reorder is
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

  TEST_CASE("array.indexmap: registered, three inlets, one outlet (#787)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* map = p.CreateObject(YSE::OBJ::G_ARRAY_INDEXMAP);
    REQUIRE(map != nullptr);
    CHECK(std::string(map->Type()) == ".array.indexmap");
    // The trigger, the map and the reference — .array.insert's arrangement.
    CHECK(map->GetInputs() == 3);
    // One outlet: the reference after a reorder that landed.
    CHECK(map->GetOutputs() == 1);

    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_ARRAY_INDEXMAP)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("array.indexmap: the trigger takes everything, the map no bang, the reference only "
            "lists (#787)") {
    // Inlet 0 is the ask — bang, int, float and list all mean a reorder.
    // Inlet 1 is configuration — a map can arrive as a list or as one
    // number, but there is nothing a bang there could mean. Inlet 2 only
    // ever carries the "array <name>" reference, which is list text.
    gArrayIndexMap g;
    const unsigned int triggerIn = g.GetInlet(0)->GetAcceptedTypes();
    CHECK((triggerIn & YSE::PATCHER::IT_BANG) != 0);
    CHECK((triggerIn & YSE::PATCHER::IT_INT) != 0);
    CHECK((triggerIn & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((triggerIn & YSE::PATCHER::IT_LIST) != 0);
    const unsigned int mapIn = g.GetInlet(1)->GetAcceptedTypes();
    CHECK((mapIn & YSE::PATCHER::IT_BANG) == 0);
    CHECK((mapIn & YSE::PATCHER::IT_INT) != 0);
    CHECK((mapIn & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((mapIn & YSE::PATCHER::IT_LIST) != 0);
    const unsigned int refIn = g.GetInlet(2)->GetAcceptedTypes();
    CHECK((refIn & YSE::PATCHER::IT_LIST) != 0);
    CHECK((refIn & YSE::PATCHER::IT_INT) == 0);
    CHECK((refIn & YSE::PATCHER::IT_FLOAT) == 0);
    CHECK((refIn & YSE::PATCHER::IT_BANG) == 0);
  }

  // ─── the reorder ────────────────────────────────────────────────────────────

  TEST_CASE("array.indexmap: a list of indices reorders, in map order, and stores nothing "
            "(#787)") {
    // The object's whole point: the map arrives on a cord and the array is
    // re-picked in the order it names — a permutation here, so the reverse
    // comes back reversed. The list is applied at the moment it arrives and
    // does not move the stored map: a bang that replayed the last list
    // would be hidden state no patch can see, gArrayAt's rule.
    Rig rig("aim787a", "a787a", "a787a 0 1");
    rig.Store("append 10 20 c4 7.5");
    REQUIRE(rig.map.MapSize() == 2);

    rig.map.GetInlet(0)->SetList("3 2 1 0", YSE::T_GUI);
    CHECK(rig.Contents() == "7.5 c4 20 10");
    // The reorder landed, so the reference left — the way the family chains.
    CHECK(rig.reference.gotList);
    CHECK(rig.reference.listValue == "array a787a");
    // The stored map is still the seed's.
    CHECK(rig.map.MapSize() == 2);
    CHECK(rig.map.MapAt(0) == 0);
    CHECK(rig.map.MapAt(1) == 1);
    CHECK(rig.map.Dropped() == 0);
  }

  TEST_CASE("array.indexmap: entries may repeat, and the result is as long as the map (#787)") {
    // The map is a list of independent picks, not a permutation table — a
    // map naming the same element twice produces it twice, and a shorter
    // map shrinks the array to what it named. AssignOrder's rule, applied
    // to the store.
    Rig rig("aim787b", "a787b");
    rig.Store("append 10 20 30");

    rig.map.GetInlet(0)->SetList("0 0 2 2", YSE::T_GUI);
    CHECK(rig.Contents() == "10 10 30 30");
    CHECK(rig.array.Count() == 4);

    rig.map.GetInlet(0)->SetList("3 0", YSE::T_GUI);
    CHECK(rig.Contents() == "30 10");
    CHECK(rig.map.Dropped() == 0);
  }

  TEST_CASE("array.indexmap: an index naming no element contributes nothing (#787)") {
    // AssignOrder's established rule, and #787's spec: a well-formed index
    // past the end drops its own pick and the rest still land — never a
    // placeholder, never a refusal of the whole map. A map whose every
    // index misses lands as an empty array, which is what those picks say.
    Rig rig("aim787c", "a787c");
    rig.Store("append 10 20 30");

    rig.map.GetInlet(0)->SetList("2 99 0", YSE::T_GUI);
    CHECK(rig.Contents() == "30 10");
    CHECK(rig.reference.gotList);
    CHECK(rig.map.Dropped() == 0);

    rig.reference.reset();
    rig.map.GetInlet(0)->SetList("7 8", YSE::T_GUI);
    CHECK(rig.array.Count() == 0);
    // The reorder landed — every pick contributed nothing, which is what
    // the map said — so the reference still leaves.
    CHECK(rig.reference.gotList);
    CHECK(rig.map.Dropped() == 0);
  }

  TEST_CASE("array.indexmap: a negative or malformed map is refused whole, nothing changed "
            "(#787)") {
    // Negative is malformed, not a miss — the family's indexing rule,
    // decided once on arrayStore — and a compound that cannot be taken
    // entirely is refused entirely: no partial reorder, no reference, and
    // the stored map does not move either.
    Rig rig("aim787d", "a787d", "a787d 2 1 0");
    rig.Store("append 10 20 30");

    rig.map.GetInlet(0)->SetList("1 -2", YSE::T_GUI);
    CHECK(rig.Contents() == "10 20 30");
    CHECK_FALSE(rig.reference.gotList);
    CHECK(rig.map.Dropped() == 1);

    rig.map.GetInlet(0)->SetList("1 x", YSE::T_GUI);
    CHECK(rig.Contents() == "10 20 30");
    CHECK(rig.map.Dropped() == 2);

    rig.map.GetInlet(0)->SetInt(-1, YSE::T_GUI);
    CHECK(rig.Contents() == "10 20 30");
    CHECK(rig.map.Dropped() == 3);

    // A non-finite float would fold to index 0 through ExprToInt — refused
    // instead, so a NaN cannot quietly shrink the array to its first
    // element.
    rig.map.GetInlet(0)->SetFloat(-1.5f, YSE::T_GUI);
    CHECK(rig.map.Dropped() == 4);

    // The map inlet refuses the same shapes, and the stored map stays the
    // seed's.
    rig.map.GetInlet(1)->SetList("0 -1", YSE::T_GUI);
    rig.map.GetInlet(1)->SetInt(-3, YSE::T_GUI);
    CHECK(rig.map.Dropped() == 6);
    CHECK(rig.map.MapSize() == 3);
    CHECK(rig.map.MapAt(0) == 2);
  }

  // ─── the stored map ─────────────────────────────────────────────────────────

  TEST_CASE("array.indexmap: a bang applies the stored map, seeded by the arguments (#787)") {
    // ".array.indexmap <name> 2 1 0" starts with a map, so a bang already
    // reorders — and a single int on the trigger is the one-entry map it
    // spells, applied at once, a float truncating first: Max's float
    // method, kept equivalent to the one-atom list so a map-producing
    // outlet that sends its single entry as an int still lands.
    Rig rig("aim787e", "a787e", "a787e 2 1 0");
    rig.Store("append 10 20 30");
    REQUIRE(rig.map.MapSize() == 3);

    rig.map.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Contents() == "30 20 10");
    CHECK(rig.reference.gotList);
    CHECK(rig.reference.listValue == "array a787e");

    rig.reference.reset();
    rig.map.GetInlet(0)->SetInt(1, YSE::T_GUI);
    CHECK(rig.Contents() == "20");
    CHECK(rig.reference.gotList);

    rig.map.GetInlet(0)->SetFloat(0.7f, YSE::T_GUI);
    CHECK(rig.Contents() == "20");
    CHECK(rig.map.Dropped() == 0);
  }

  TEST_CASE("array.indexmap: the map inlet replaces the stored map, silently (#787)") {
    // The cold half of the Max idiom — .zl indexmap's right inlet:
    // storing emits nothing, and the next bang applies the new map. A
    // single int stores a one-entry map; a float truncates first.
    Rig rig("aim787f", "a787f", "a787f 0 1 2");
    rig.Store("append 10 20 30");

    rig.map.GetInlet(1)->SetList("2 0", YSE::T_GUI);
    CHECK_FALSE(rig.reference.gotList);
    CHECK(rig.Contents() == "10 20 30");
    REQUIRE(rig.map.MapSize() == 2);
    CHECK(rig.map.MapAt(0) == 2);
    CHECK(rig.map.MapAt(1) == 0);

    rig.map.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Contents() == "30 10");
    CHECK(rig.reference.gotList);

    rig.map.GetInlet(1)->SetInt(1, YSE::T_GUI);
    REQUIRE(rig.map.MapSize() == 1);
    CHECK(rig.map.MapAt(0) == 1);

    rig.map.GetInlet(1)->SetFloat(0.9f, YSE::T_GUI);
    REQUIRE(rig.map.MapSize() == 1);
    CHECK(rig.map.MapAt(0) == 0);
    CHECK(rig.map.Dropped() == 0);
  }

  TEST_CASE("array.indexmap: a bang before any map is refused, and SetParams clears it (#787)") {
    // An absent map is malformed, not a reorder to nothing — .array's own
    // "clear" is the object that empties on purpose — and a re-parse must
    // not leave half of the previous configuration standing: SetParams("")
    // drops the map with the name.
    Rig rig("aim787g", "a787g");
    rig.Store("append 10 20 30");
    CHECK(rig.map.MapSize() == 0);

    rig.map.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Contents() == "10 20 30");
    CHECK_FALSE(rig.reference.gotList);
    CHECK(rig.map.Dropped() == 1);

    rig.map.SetParams("a787g 1 0");
    CHECK(rig.map.MapSize() == 2);
    rig.map.SetParams("");
    CHECK(rig.map.MapSize() == 0);
    rig.map.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.map.Dropped() == 2);
  }

  TEST_CASE("array.indexmap: a malformed seed plants no map, counted (#787)") {
    // A token that is not a non-negative integer cannot be an index at all,
    // so the whole argument becomes "no map" — counted, where the absent
    // case is simply the object's initial state — and a bang then refuses
    // exactly as it does before any map has arrived.
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aim787h");
    MultiSink sink;
    gArrayIndexMap g;
    g.SetParent(&p);
    g.SetParams("a787h 2 x 0");
    Wire(g, 0, sink);

    CHECK(g.MapSize() == 0);
    const std::uint64_t before = g.Dropped();
    CHECK(before >= 1);
    g.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(sink.gotList);
    CHECK(g.Dropped() == before + 1);
  }

  // ─── the reference ──────────────────────────────────────────────────────────

  TEST_CASE("array.indexmap: the reference applies on the trigger, is data nowhere, and "
            "acknowledges on its own inlet (#787)") {
    // "array <name>" is the message an .array's reference outlet emits on a
    // bang. On the trigger it applies the stored map — the family gesture:
    // bang the array, out comes the reordered array's reference. On the map
    // inlet it is an identity mis-wired into data, refused. On the
    // reference inlet it is acknowledged silently — the binding is the
    // creation argument, so there is nothing to set — and anything else
    // there is refused, which is what makes a mis-wired cord visible in
    // Dropped(). A reference naming an array this object is not bound to is
    // refused everywhere, never resolved: a registry lookup is a mutex, and
    // this may be the audio thread.
    Rig rig("aim787i", "a787i", "a787i 2 1 0");
    rig.Store("append 10 20 30");

    const std::uint64_t before = rig.map.Dropped();
    rig.map.GetInlet(0)->SetList("array a787i", YSE::T_GUI);
    CHECK(rig.Contents() == "30 20 10");
    CHECK(rig.reference.gotList);
    CHECK(rig.map.Dropped() == before);

    rig.reference.reset();
    rig.map.GetInlet(0)->SetList("array somewhere_else", YSE::T_GUI);
    CHECK(rig.Contents() == "30 20 10");
    CHECK_FALSE(rig.reference.gotList);
    CHECK(rig.map.Dropped() == before + 1);

    rig.map.GetInlet(1)->SetList("array a787i", YSE::T_GUI);
    CHECK(rig.map.Dropped() == before + 2);
    CHECK(rig.map.MapSize() == 3);

    rig.map.GetInlet(2)->SetList("array a787i", YSE::T_GUI);
    CHECK(rig.map.Dropped() == before + 2);
    rig.map.GetInlet(2)->SetList("array somewhere_else", YSE::T_GUI);
    rig.map.GetInlet(2)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK(rig.map.Dropped() == before + 4);
    CHECK_FALSE(rig.reference.gotList);
  }

  TEST_CASE("array.indexmap: an unnamed object reorders a private array, silently (#787)") {
    // No name, no shared store, no reference to pass on — the reorder
    // happens (over an empty private array, so every pick contributes
    // nothing), the announcement is simply empty, and nothing is a
    // refusal: the ask was well-formed.
    MultiSink sink;
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aim787j");
    gArrayIndexMap unnamed;
    unnamed.SetParent(&p);
    Wire(unnamed, 0, sink);
    CHECK(unnamed.Address().empty());

    unnamed.GetInlet(0)->SetList("0 1 2", YSE::T_GUI);
    CHECK_FALSE(sink.gotList);
    CHECK(unnamed.Dropped() == 0);
  }

  TEST_CASE("array.indexmap: wired from the array's reference outlet, banging the array reorders "
            "(#787)") {
    // The flow a patch actually wires, end to end through the public
    // patcher API: the .array's reference outlet into the trigger gives the
    // family gesture — bang the array, the stored map is applied, and the
    // emitted reference chains onward. The contents are read back through
    // the array's own "getvalue", so the whole loop runs on cords.
    MultiSink chained;
    MultiSink contents;
    YSE::pHandle chainedHandle(&chained);
    YSE::pHandle contentsHandle(&contents);
    YSE::patcher p;
    p.create(2);
    p.name("aim787k");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "a787k");
    YSE::pHandle* map = p.CreateObject(YSE::OBJ::G_ARRAY_INDEXMAP, "a787k 2 1 0");
    REQUIRE(array != nullptr);
    REQUIRE(map != nullptr);
    p.Connect(array, 1, map, 0);
    p.Connect(map, 0, &chainedHandle, 0);
    p.Connect(array, 0, &contentsHandle, 0);

    array->SetListData(0, "append 60 64 67");
    array->SetBang(0);
    REQUIRE(chained.gotList);
    CHECK(chained.listValue == "array a787k");

    array->SetListData(0, "getvalue");
    REQUIRE(contents.gotList);
    CHECK(contents.listValue == "67 64 60");
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("array.indexmap: the address form is the patcher's, and RefreshBinding follows it "
            "(#787)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aim787l_before");

    gArrayIndexMap g;
    g.SetParams("a787l");
    g.SetParent(&p);
    CHECK(g.ArrayName() == "a787l");
    CHECK(g.Address() == "aim787l_before.a787l");

    // Idempotent: a rebind to the address it already has keeps the store.
    g.RefreshBinding();
    CHECK(g.Address() == "aim787l_before.a787l");

    p.SetName("aim787l_after");
    g.RefreshBinding();
    CHECK(g.Address() == "aim787l_after.a787l");
  }

  TEST_CASE("array.indexmap: patcherImplementation::SetName re-anchors it (#787)") {
    // The rename dispatch itself, which the standalone case above cannot
    // reach: an object created *inside* a patcher must be re-anchored by
    // the patcher, without anybody calling RefreshBinding by hand. The
    // keeper holds the old-address store; after the rename the object acts
    // on a fresh empty array under the new prefix, so the keeper's contents
    // stop moving — while the stored map survives, being the object's own
    // state rather than anything derived from the name.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aim787m_before");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a787m");
    keeper.GetInlet(0)->SetList("append 10 20 30", YSE::T_GUI);

    YSE::pHandle* map = p.CreateObject(YSE::OBJ::G_ARRAY_INDEXMAP, "a787m 2 1 0");
    REQUIRE(map != nullptr);
    p.Connect(map, 0, &sinkHandle, 0);

    map->SetBang(0);
    REQUIRE(sink.gotList);
    CHECK(keeper.Count() == 3);
    CHECK(keeper.ElementAt(0) == "30");

    p.SetName("aim787m_after");
    sink.reset();
    map->SetBang(0);
    // The reorder landed — on the new, empty array — and the keeper's
    // contents were not touched.
    CHECK(sink.gotList);
    CHECK(keeper.Count() == 3);
    CHECK(keeper.ElementAt(0) == "30");
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("array.indexmap: a map arriving over in-patcher delivery lands on T_DSP (#787)") {
    // A .r feeding the trigger dispatches on T_DSP when the block drains it
    // (issue #225) — "the audio thread reorders an array" is the ordinary
    // case, and the whole path is one parse into fixed storage, one guard
    // hold and a send of a string the object already owns.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aim787n");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a787n");
    keeper.GetInlet(0)->SetList("append 10 20 30", YSE::T_GUI);

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go787n");
    YSE::pHandle* map = p.CreateObject(YSE::OBJ::G_ARRAY_INDEXMAP, "a787n");
    REQUIRE(recv != nullptr);
    REQUIRE(map != nullptr);
    p.Connect(recv, 0, map, 0);
    p.Connect(map, 0, &sinkHandle, 0);

    p.PassData(std::string("2 1 0"), "go787n", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "array a787n");
    CHECK(keeper.Count() == 3);
    CHECK(keeper.ElementAt(0) == "30");
    CHECK(keeper.ElementAt(2) == "10");
  }

  TEST_CASE("array.indexmap: no message path allocates (#787)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path: the bang, the int, the float, the list map on
    // both inlets, the reference gesture, the wrong-name and malformed
    // refusals, and the reference-inlet acknowledgement.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string reference = "array probeAIM787";
    const std::string wrongName = "array somewhere_else_long";
    const std::string reverse = "2 1 0";
    const std::string storeMap = "0 5";
    const std::string malformed = "1 -2";

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aim787o");
    MultiSink sink;
    gArray array;
    gArrayIndexMap map;
    array.SetParent(&p);
    array.SetParams("probeAIM787");
    map.SetParent(&p);
    map.SetParams("probeAIM787 0 1 2");
    Wire(map, 0, sink);

    array.GetInlet(0)->SetList("append 10 20 30", YSE::T_GUI);

    // Warm every path — including the sink's assignments — so first-call
    // machinery is not what the probe catches.
    map.GetInlet(0)->SetList(reverse, YSE::T_GUI);
    map.GetInlet(0)->SetBang(YSE::T_GUI);
    map.GetInlet(0)->SetList(reference, YSE::T_GUI);
    map.GetInlet(0)->SetFloat(0.7f, YSE::T_GUI);
    map.GetInlet(0)->SetInt(0, YSE::T_GUI);
    map.GetInlet(1)->SetList(storeMap, YSE::T_GUI);
    map.GetInlet(1)->SetInt(2, YSE::T_GUI);
    map.GetInlet(1)->SetFloat(1.4f, YSE::T_GUI);
    map.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    map.GetInlet(0)->SetList(malformed, YSE::T_GUI);
    map.GetInlet(1)->SetList(reference, YSE::T_GUI);
    map.GetInlet(2)->SetList(reference, YSE::T_GUI);
    map.GetInlet(2)->SetList(wrongName, YSE::T_GUI);

    // Reset the contents, then run the same sequence on T_DSP under the
    // probe.
    array.GetInlet(0)->SetList("clear", YSE::T_GUI);
    array.GetInlet(0)->SetList("append 10 20 30", YSE::T_GUI);
    const std::uint64_t before = map.Dropped();
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      map.GetInlet(0)->SetList(reverse, YSE::T_DSP); // 30 20 10
      map.GetInlet(0)->SetBang(YSE::T_DSP); // stored [1] from the warm-up: 20
      map.GetInlet(0)->SetList(reference, YSE::T_DSP); // stored [1]: misses, empty
      map.GetInlet(1)->SetList(storeMap, YSE::T_DSP); // stores [0, 5]
      map.GetInlet(1)->SetInt(2, YSE::T_DSP); // stores [2]
      map.GetInlet(1)->SetFloat(1.4f, YSE::T_DSP); // stores [1]
      map.GetInlet(0)->SetInt(0, YSE::T_DSP); // [0]: misses, stays empty
      map.GetInlet(0)->SetFloat(0.7f, YSE::T_DSP); // [0]: misses, stays empty
      map.GetInlet(0)->SetList(wrongName, YSE::T_DSP); // refused
      map.GetInlet(0)->SetList(malformed, YSE::T_DSP); // refused
      map.GetInlet(1)->SetList(reference, YSE::T_DSP); // refused: not a map
      map.GetInlet(2)->SetList(reference, YSE::T_DSP); // acknowledged
      map.GetInlet(2)->SetList(wrongName, YSE::T_DSP); // refused
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing. The reorders landed (the array ended empty,
    // every late pick missing), the stored map is the float's truncation,
    // and exactly the four refusals were counted.
    CHECK(array.Count() == 0);
    CHECK(sink.gotList);
    CHECK(sink.listValue == "array probeAIM787");
    REQUIRE(map.MapSize() == 1);
    CHECK(map.MapAt(0) == 1);
    CHECK(map.Dropped() == before + 4);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("array.indexmap: params survive a DumpJSON / ParseJSON round trip (#787)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* map = src.CreateObject(YSE::OBJ::G_ARRAY_INDEXMAP, "notes787 2 0 1");
    REQUIRE(map != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".array.indexmap"));
    CHECK(copy->GetParams() == std::string("notes787 2 0 1"));
    CHECK(copy->GetInputs() == 3);
    CHECK(copy->GetOutputs() == 1);
  }

  TEST_CASE("array.indexmap: carries complete documentation metadata (#787)") {
    gArrayIndexMap map;
    CHECK_FALSE(map.GetDescription().empty());
    CHECK(map.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    const auto& docs = map.GetParamDocs();
    REQUIRE(docs.size() == 2);
    CHECK(docs[0].name == "name");
    CHECK(docs[1].name == "map");
  }
}

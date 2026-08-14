// Tests for .array.flatten (issue #795) — Max's array.flatten on the
// name-addressed value model .array settled (#548).
//
// What has to be proven, and what every case below is one of:
//
//   - **every array is bound from a creation argument.** An element is one
//     atom, so an array cannot hold an array — what stands where Max's
//     nesting stood is a group of *named* arrays, and a name resolves only
//     on the control thread. ".array.flatten <first> ... <last>" binds every
//     name once, and an `array <name>` message is honoured only against a
//     name already bound on that inlet — ArrayReferenceNames' bounded
//     compare, never a registry lookup on a message path.
//   - **a flatten keeps everything** — the named arrays' elements in
//     argument order, repeats included, order preserved — leaving as list
//     text, never as a new named array, and never writing to any store.
//   - **one guard at a time, in sequence** — each source is read under its
//     own guard alone, so ".array.flatten seq seq" answers the sequence
//     doubled instead of tripping over its own try-lock.
//   - **refusal, never truncation** — a result that outruns what a cord
//     carries, a lost guard mid-walk, and a creation line spelling more
//     than MAX_SOURCES arrays are each refused whole and counted.
//   - **the ask crosses the control/audio boundary and nothing allocates.**
//     In-patcher delivery dispatches on T_DSP, so "the audio thread builds
//     the flattened sequence" is the ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names an array uses names of its own — one case's contents
// must not be visible to the next.

#include <doctest/doctest.h>
#include <cstdint>
#include <memory>
#include <string>

#include "patcher/genericObjects/gArray.h"
#include "patcher/genericObjects/gArrayFlatten.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObject.h"
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
using YSE::PATCHER::gArrayFlatten;

namespace {

  // Three .arrays and one flatten on their names, sharing one
  // patcherImplementation so the names actually bind ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // sinks are declared before the objects so they are torn down last, while
  // the outlets wired to them still exist (see sinks.hpp on why that
  // matters).
  struct FlattenRig {
    MultiSink out;
    BangSink empty;
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    gArray first;
    gArray second;
    gArray third;
    gArrayFlatten op;

    FlattenRig(const std::string& patcherName, const std::string& firstName,
               const std::string& secondName, const std::string& thirdName) {
      p.SetName(patcherName);
      first.SetParent(&p);
      first.SetParams(firstName);
      second.SetParent(&p);
      second.SetParams(secondName);
      third.SetParent(&p);
      third.SetParams(thirdName);
      op.SetParent(&p);
      op.SetParams(firstName + " " + secondName + " " + thirdName);
      Wire(op, 0, out);
      Wire(op, 1, empty);
    }

    void StoreFirst(const std::string& message) {
      first.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void StoreSecond(const std::string& message) {
      second.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void StoreThird(const std::string& message) {
      third.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void Ask() {
      op.GetInlet(0)->SetBang(YSE::T_GUI);
    }
    void Reset() {
      out.reset();
      empty.gotBang = false;
      empty.bangCount = 0;
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("array.flatten: registered, with its inlets and outlets (#795)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY_FLATTEN);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(YSE::OBJ::G_ARRAY_FLATTEN));
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 2);

    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_ARRAY_FLATTEN)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("array.flatten: the trigger takes the ask, the sources inlet only lists (#795)") {
    // Inlet 0 is the ask — a bang, or the "array <name>" reference, which is
    // list text; a result is asked for, never addressed, so no int lands
    // anywhere. Inlet 1 acknowledges references only.
    std::unique_ptr<YSE::PATCHER::pObject> obj(
        YSE::PATCHER::Register().Get(YSE::OBJ::G_ARRAY_FLATTEN));
    REQUIRE(obj != nullptr);

    const unsigned int triggerIn = obj->GetInlet(0)->GetAcceptedTypes();
    CHECK((triggerIn & YSE::PATCHER::IT_BANG) != 0);
    CHECK((triggerIn & YSE::PATCHER::IT_LIST) != 0);
    CHECK((triggerIn & YSE::PATCHER::IT_INT) == 0);
    CHECK((triggerIn & YSE::PATCHER::IT_FLOAT) == 0);

    const unsigned int refIn = obj->GetInlet(1)->GetAcceptedTypes();
    CHECK((refIn & YSE::PATCHER::IT_LIST) != 0);
    CHECK((refIn & YSE::PATCHER::IT_BANG) == 0);
    CHECK((refIn & YSE::PATCHER::IT_INT) == 0);
    CHECK((refIn & YSE::PATCHER::IT_FLOAT) == 0);
  }

  // ─── the flatten ────────────────────────────────────────────────────────────

  TEST_CASE("array.flatten: every array's elements in argument order, everything kept (#795)") {
    FlattenRig rig("af795a", "a795a", "b795a", "c795a");
    rig.StoreFirst("append 10 20");
    rig.StoreSecond("append 20 30");
    rig.StoreThird("append 10");

    // Not a set operation: repeats survive, inside each array and across
    // them, in argument order — where union would thin this to "10 20 30".
    rig.Ask();
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "10 20 20 30 10");
    CHECK_FALSE(rig.empty.gotBang);
    CHECK(rig.op.Dropped() == 0);

    // Mixed types pass through as the atoms they are — 7 and 7. stay two
    // different elements, a symbol stays a symbol.
    FlattenRig mixed("af795a2", "a795a2", "b795a2", "c795a2");
    mixed.StoreFirst("append 7 c4");
    mixed.StoreSecond("append 7.");
    mixed.StoreThird("append c4");
    mixed.Ask();
    CHECK(mixed.out.gotList);
    CHECK(mixed.out.listValue == "7 c4 7. c4");
  }

  TEST_CASE("array.flatten: an empty source contributes nothing, all empty bangs (#795)") {
    // A hole in the middle of the group leaves the others joined around it.
    FlattenRig rig("af795b", "a795b", "b795b", "c795b");
    rig.StoreFirst("append 60 64");
    rig.StoreThird("append 67");
    rig.Ask();
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "60 64 67");

    // One element in the whole group leaves typed — SendAtoms' rule.
    FlattenRig single("af795b2", "a795b2", "b795b2", "c795b2");
    single.StoreSecond("append 60");
    single.Ask();
    CHECK(single.out.gotInt);
    CHECK(single.out.intValue == 60);
    CHECK_FALSE(single.out.gotList);

    // Every source empty bangs the empty outlet — "no data" is a state a
    // patch must be able to route on, not an error.
    FlattenRig hollow("af795b3", "a795b3", "b795b3", "c795b3");
    hollow.Ask();
    CHECK(hollow.empty.gotBang);
    CHECK_FALSE(hollow.out.gotList);
    CHECK(hollow.op.Dropped() == 0);
  }

  TEST_CASE("array.flatten: an unnamed object reads a private, empty array (#795)") {
    // Not "shares the empty name" — gArray's rule, inherited on every
    // binding. With no creation arguments there is one private source,
    // empty, so every ask bangs empty and counts nothing.
    MultiSink out;
    BangSink empty;
    YSE::pHandle outHandle(&out);
    YSE::pHandle emptyHandle(&empty);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("af795c");
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY_FLATTEN, "");
    REQUIRE(h != nullptr);
    p.Connect(h, 0, &outHandle, 0);
    p.Connect(h, 1, &emptyHandle, 0);

    h->SetBang(0);
    CHECK(empty.gotBang);
    CHECK_FALSE(out.gotList);
    CHECK_FALSE(out.gotInt);
  }

  TEST_CASE("array.flatten: one array named twice answers it doubled (#795)") {
    // ".array.flatten seq seq" — the case that proves no two guards are
    // ever held at once: each source is read under its own guard alone, so
    // the same store's guard is taken twice *sequentially* and the answer
    // is the sequence twice over. Three times over proves the walk keeps
    // going.
    MultiSink out;
    BangSink empty;
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    p.SetName("af795d");
    gArray seq;
    seq.SetParent(&p);
    seq.SetParams("s795d");
    seq.GetInlet(0)->SetList("append 60 64", YSE::T_GUI);
    gArrayFlatten op;
    op.SetParent(&p);
    op.SetParams("s795d s795d s795d");
    Wire(op, 0, out);
    Wire(op, 1, empty);

    op.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(out.gotList);
    CHECK(out.listValue == "60 64 60 64 60 64");
    CHECK(op.Dropped() == 0);
  }

  TEST_CASE("array.flatten: a flatten never writes to any store (#795)") {
    // Read-only, Max's contract kept: the result is a list the object owns;
    // .array itself is where a patch writes it back.
    FlattenRig rig("af795e", "a795e", "b795e", "c795e");
    rig.StoreFirst("append 10 20");
    rig.StoreSecond("append 30");
    rig.Ask();
    CHECK(rig.out.gotList);
    CHECK(rig.first.Count() == 2);
    CHECK(rig.first.ElementAt(0) == "10");
    CHECK(rig.first.ElementAt(1) == "20");
    CHECK(rig.second.Count() == 1);
    CHECK(rig.second.ElementAt(0) == "30");
    CHECK(rig.third.Count() == 0);
  }

  TEST_CASE("array.flatten: a result that outruns a cord is refused whole (#795)") {
    // 150 + 150 elements is a well-formed flatten of 300 — more atoms than
    // a list carries (AtomList::MAX_ATOMS is 256), so the ask is refused
    // whole and counted: a partial flatten would be truncation by another
    // name, .array.at's whole-reply rule.
    FlattenRig rig("af795f", "a795f", "b795f", "c795f");
    std::string message = "append";
    for (int i = 0; i < 150; i++)
      message += " " + std::to_string(1000 + i);
    rig.StoreFirst(message);
    message = "append";
    for (int i = 0; i < 150; i++)
      message += " " + std::to_string(2000 + i);
    rig.StoreSecond(message);
    CHECK(rig.first.Count() == 150);
    CHECK(rig.second.Count() == 150);

    const std::uint64_t before = rig.op.Dropped();
    rig.Ask();
    CHECK(rig.op.Dropped() == before + 1);
    CHECK_FALSE(rig.out.gotList);
    CHECK_FALSE(rig.empty.gotBang);
  }

  // ─── the slot table's bound ─────────────────────────────────────────────────

  TEST_CASE("array.flatten: sixteen arrays bind, a seventeenth refuses every ask (#795)") {
    // MAX_SOURCES arrays in all — the bound that keeps the slot table fixed
    // at construction. Sixteen names bind and answer.
    MultiSink out;
    BangSink empty;
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    p.SetName("af795g");
    gArray seed;
    seed.SetParent(&p);
    seed.SetParams("cap795x0");
    seed.GetInlet(0)->SetList("append 7", YSE::T_GUI);

    std::string params = "cap795x0";
    for (int i = 1; i < 16; i++)
      params += " cap795x" + std::to_string(i);
    gArrayFlatten op;
    op.SetParent(&p);
    op.SetParams(params);
    Wire(op, 0, out);
    Wire(op, 1, empty);
    CHECK_FALSE(op.Overflowed());
    CHECK(op.ExtraSources() == 15u);
    CHECK(op.ExtraName(0) == "cap795x1");
    CHECK(op.ExtraAddress(0) == "af795g.cap795x1");
    CHECK(op.ExtraAddress(14) == "af795g.cap795x15");

    op.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(out.gotInt);
    CHECK(out.intValue == 7);
    CHECK(op.Dropped() == 0);

    // A seventeenth name is the configuration refused whole: nothing beyond
    // the first binds, and every ask is refused and counted — a flatten
    // that silently dropped its seventeenth array would be truncation by
    // another name.
    out.reset();
    empty.gotBang = false;
    op.SetParams(params + " cap795x16");
    CHECK(op.Overflowed());
    CHECK(op.ExtraSources() == 0u);

    const std::uint64_t before = op.Dropped();
    op.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(op.Dropped() == before + 1);
    CHECK_FALSE(out.gotInt);
    CHECK_FALSE(out.gotList);
    CHECK_FALSE(empty.gotBang);

    // A re-parse that fits again recovers — the refusal is the
    // configuration's, not the object's.
    op.SetParams("cap795x0 cap795x1");
    CHECK_FALSE(op.Overflowed());
    CHECK(op.ExtraSources() == 1u);
    op.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(out.gotInt);
    CHECK(out.intValue == 7);
  }

  // ─── the references ─────────────────────────────────────────────────────────

  TEST_CASE(
      "array.flatten: the first reference asks on the trigger, anything else refused (#795)") {
    // "array <name>" is the message an .array's reference outlet emits on a
    // bang; on the trigger inlet it asks when it names the bound (first)
    // array — the family gesture. Any other name there is a mis-wired cord,
    // refused, never resolved: a registry lookup is a mutex, and this may
    // be the audio thread.
    FlattenRig rig("af795h", "a795h", "b795h", "c795h");
    rig.StoreFirst("append 10");
    rig.StoreSecond("append 20");

    const std::uint64_t before = rig.op.Dropped();
    rig.op.GetInlet(0)->SetList("array a795h", YSE::T_GUI);
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "10 20");
    CHECK(rig.op.Dropped() == before);

    rig.Reset();
    rig.op.GetInlet(0)->SetList("array b795h", YSE::T_GUI);
    rig.op.GetInlet(0)->SetList("array somewhere_else", YSE::T_GUI);
    rig.op.GetInlet(0)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotList);
    CHECK_FALSE(rig.empty.gotBang);
    CHECK(rig.op.Dropped() == before + 3);
  }

  TEST_CASE("array.flatten: the sources inlet acknowledges the trailing names only (#795)") {
    // Inlet 1 answers to every *trailing* name — a patch may wire each
    // source's reference outlet across, as it would in Max — and to nothing
    // else, the first name included: each inlet is bound to its own side,
    // .array.concat's rule.
    FlattenRig rig("af795i", "a795i", "b795i", "c795i");
    rig.StoreFirst("append 1");

    const std::uint64_t before = rig.op.Dropped();
    rig.op.GetInlet(1)->SetList("array b795i", YSE::T_GUI);
    rig.op.GetInlet(1)->SetList("array c795i", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotList);
    CHECK_FALSE(rig.out.gotInt);
    CHECK(rig.op.Dropped() == before);

    rig.op.GetInlet(1)->SetList("array a795i", YSE::T_GUI);
    rig.op.GetInlet(1)->SetList("array somewhere_else", YSE::T_GUI);
    rig.op.GetInlet(1)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK(rig.op.Dropped() == before + 3);
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("array.flatten: patcherImplementation::SetName re-anchors every binding (#795)") {
    // The rename dispatch itself: an object created *inside* a patcher must
    // be re-anchored by the patcher, without anybody calling RefreshBinding
    // by hand — and the dispatch must reach gArrayFlatten's RefreshBinding,
    // or only the first side would move. The keepers hold the old-address
    // stores (they are not in the patcher's object map, so the rename does
    // not touch them): before the rename the flatten answers from every
    // side; after it every side reads a fresh empty array under the new
    // prefix, so the ask bangs empty.
    MultiSink out;
    BangSink empty;
    YSE::pHandle outHandle(&out);
    YSE::pHandle emptyHandle(&empty);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("af795j_before");

    gArray keepFirst;
    keepFirst.SetParent(&p);
    keepFirst.SetParams("a795j");
    keepFirst.GetInlet(0)->SetList("append 10", YSE::T_GUI);
    gArray keepSecond;
    keepSecond.SetParent(&p);
    keepSecond.SetParams("b795j");
    keepSecond.GetInlet(0)->SetList("append 20", YSE::T_GUI);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY_FLATTEN, "a795j b795j");
    REQUIRE(h != nullptr);
    p.Connect(h, 0, &outHandle, 0);
    p.Connect(h, 1, &emptyHandle, 0);

    h->SetBang(0);
    CHECK(out.gotList);
    CHECK(out.listValue == "10 20");
    CHECK_FALSE(empty.gotBang);

    p.SetName("af795j_after");
    out.reset();
    h->SetBang(0);
    CHECK_FALSE(out.gotInt);
    CHECK_FALSE(out.gotList);
    CHECK(empty.gotBang);
  }

  // ─── the family, chained through the public API ─────────────────────────────

  TEST_CASE(
      "array.flatten: wired from the array's reference outlet, banging the array asks (#795)") {
    // The flow a patch actually wires, end to end through the public patcher
    // API: the first .array's reference outlet into the trigger inlet gives
    // the family gesture — bang the array, out comes the group as one
    // sequence — and a write between two bangs changes what the next ask
    // sees.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::patcher p;
    p.create(2);
    p.name("af795k");
    YSE::pHandle* firstArray = p.CreateObject(YSE::OBJ::G_ARRAY, "a795k");
    YSE::pHandle* secondArray = p.CreateObject(YSE::OBJ::G_ARRAY, "b795k");
    YSE::pHandle* thirdArray = p.CreateObject(YSE::OBJ::G_ARRAY, "c795k");
    YSE::pHandle* flat = p.CreateObject(YSE::OBJ::G_ARRAY_FLATTEN, "a795k b795k c795k");
    REQUIRE(firstArray != nullptr);
    REQUIRE(secondArray != nullptr);
    REQUIRE(thirdArray != nullptr);
    REQUIRE(flat != nullptr);
    p.Connect(firstArray, 1, flat, 0);
    p.Connect(flat, 0, &sinkHandle, 0);

    firstArray->SetListData(0, "append 60 64");
    secondArray->SetListData(0, "append 67");
    firstArray->SetBang(0);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "60 64 67");

    sink.reset();
    thirdArray->SetListData(0, "append 71");
    firstArray->SetBang(0);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "60 64 67 71");
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("array.flatten: an ask over in-patcher delivery lands on T_DSP (#795)") {
    // A .r feeding the trigger inlet dispatches on T_DSP when the block
    // drains it (issue #225) — "the audio thread builds the flattened
    // sequence" is the ordinary case.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("af795l");

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go795l");
    YSE::pHandle* firstArray = p.CreateObject(YSE::OBJ::G_ARRAY, "a795l");
    YSE::pHandle* secondArray = p.CreateObject(YSE::OBJ::G_ARRAY, "b795l");
    YSE::pHandle* flat = p.CreateObject(YSE::OBJ::G_ARRAY_FLATTEN, "a795l b795l");
    REQUIRE(recv != nullptr);
    REQUIRE(firstArray != nullptr);
    REQUIRE(secondArray != nullptr);
    REQUIRE(flat != nullptr);
    p.Connect(recv, 0, flat, 0);
    p.Connect(flat, 0, &sinkHandle, 0);

    firstArray->SetListData(0, "append 10");
    secondArray->SetListData(0, "append 20 30");

    p.PassData(std::string("array a795l"), "go795l", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "10 20 30");
  }

  TEST_CASE("array.flatten: no message path allocates (#795)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every path: the multi-source walk, the list-rendered send, the empty
    // outlet, the reference ask, the refusal and acknowledgement paths, and
    // the overflowed configuration's refusal — all on T_DSP.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string firstReference = "array probeFa795";
    const std::string secondReference = "array probeFb795";
    const std::string wrongName = "array somewhere_else_long";
    const std::string clearMessage = "clear";

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("af795m");
    MultiSink sink;
    BangSink empty;
    gArray first;
    gArray second;
    gArray third;
    gArrayFlatten flat;
    gArrayFlatten flatOver;

    first.SetParent(&p);
    first.SetParams("probeFa795");
    second.SetParent(&p);
    second.SetParams("probeFb795");
    third.SetParent(&p);
    third.SetParams("probeFc795");
    flat.SetParams("probeFa795 probeFb795 probeFc795");
    flat.SetParent(&p);
    Wire(flat, 0, sink);
    Wire(flat, 1, empty);

    // The overflowed configuration — seventeen names — whose every ask is a
    // counted refusal, on the message path too.
    std::string overflowParams = "probeFa795";
    for (int i = 0; i < 16; i++)
      overflowParams += " probeOver795x" + std::to_string(i);
    flatOver.SetParams(overflowParams);
    flatOver.SetParent(&p);
    REQUIRE(flatOver.Overflowed());

    // Mixed populations, so the result carries ints, a float and a symbol —
    // the list-rendered send path runs — and the arrays are emptied inside
    // the scope so the empty outlet runs on T_DSP too.
    first.GetInlet(0)->SetList("append 10 c4 7.5", YSE::T_GUI);
    second.GetInlet(0)->SetList("append -3", YSE::T_GUI);
    third.GetInlet(0)->SetList("append 5 8", YSE::T_GUI);

    // Warm every path — including the sinks' assignments — so first-call
    // machinery is not what the probe catches.
    flat.GetInlet(0)->SetBang(YSE::T_GUI);
    flat.GetInlet(0)->SetList(firstReference, YSE::T_GUI);
    flat.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    flat.GetInlet(1)->SetList(secondReference, YSE::T_GUI);
    flat.GetInlet(1)->SetList(wrongName, YSE::T_GUI);
    flatOver.GetInlet(0)->SetBang(YSE::T_GUI);
    const std::uint64_t flatDroppedBefore = flat.Dropped();
    const std::uint64_t overDroppedBefore = flatOver.Dropped();

    sink.reset();
    empty.gotBang = false;
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      flat.GetInlet(0)->SetBang(YSE::T_DSP);
      flat.GetInlet(0)->SetList(firstReference, YSE::T_DSP);
      flat.GetInlet(0)->SetList(wrongName, YSE::T_DSP);
      flat.GetInlet(1)->SetList(secondReference, YSE::T_DSP);
      flat.GetInlet(1)->SetList(wrongName, YSE::T_DSP);
      flatOver.GetInlet(0)->SetBang(YSE::T_DSP);
      // The empty outlet on T_DSP too: clear every source through the
      // .array's own message path, then ask again.
      first.GetInlet(0)->SetList(clearMessage, YSE::T_DSP);
      second.GetInlet(0)->SetList(clearMessage, YSE::T_DSP);
      third.GetInlet(0)->SetList(clearMessage, YSE::T_DSP);
      flat.GetInlet(0)->SetBang(YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing.
    CHECK(sink.gotList);
    CHECK(sink.listValue == "10 c4 7.5 -3 5 8");
    CHECK(empty.gotBang);
    CHECK(flat.Dropped() == flatDroppedBefore + 2);
    CHECK(flatOver.Dropped() == overDroppedBefore + 1);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("array.flatten: params survive a DumpJSON / ParseJSON round trip (#795)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* f = src.CreateObject(YSE::OBJ::G_ARRAY_FLATTEN, "notes795 more795 rest795");
    REQUIRE(f != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);
    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(copy->GetParams() == "notes795 more795 rest795");
    CHECK(copy->GetInputs() == 2);
    CHECK(copy->GetOutputs() == 2);

    // A flatten of one name round-trips bare — the trailing names are
    // genuinely optional.
    YSE::patcher bare;
    bare.create(2);
    YSE::pHandle* b = bare.CreateObject(YSE::OBJ::G_ARRAY_FLATTEN, "bare795");
    REQUIRE(b != nullptr);
    YSE::patcher bareLoaded;
    bareLoaded.create(2);
    bareLoaded.ParseJSON(bare.DumpJSON());
    REQUIRE(bareLoaded.Objects() == 1);
    YSE::pHandle* bareCopy = bareLoaded.GetHandleFromList(0);
    REQUIRE(bareCopy != nullptr);
    CHECK(bareCopy->GetParams() == "bare795");
  }

  TEST_CASE("array.flatten: carries complete documentation metadata (#795)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(
        YSE::PATCHER::Register().Get(YSE::OBJ::G_ARRAY_FLATTEN));
    REQUIRE(obj != nullptr);
    CHECK_FALSE(obj->GetDescription().empty());
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 2u);
    CHECK(docs[0].name == "first");
    CHECK(docs[1].name == "others");
  }

  TEST_CASE("array.flatten: every binding resets with the params (#795)") {
    // The bound addresses are visible for diagnostics; SetParams("") is a
    // real reset on every side — back to one private array and no trailing
    // sources at all.
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("af795n");
    std::unique_ptr<YSE::PATCHER::pObject> obj(
        YSE::PATCHER::Register().Get(YSE::OBJ::G_ARRAY_FLATTEN));
    REQUIRE(obj != nullptr);
    auto* flat = static_cast<gArrayFlatten*>(obj.get());
    flat->SetParams("one795 two795 three795");
    CHECK(flat->Address().empty());
    CHECK(flat->ExtraAddress(0).empty());
    flat->SetParent(&p);
    CHECK(flat->ArrayName() == "one795");
    CHECK(flat->ExtraSources() == 2u);
    CHECK(flat->ExtraName(0) == "two795");
    CHECK(flat->ExtraName(1) == "three795");
    CHECK(flat->Address() == "af795n.one795");
    CHECK(flat->ExtraAddress(0) == "af795n.two795");
    CHECK(flat->ExtraAddress(1) == "af795n.three795");

    // A shorter line drops the slots past it.
    flat->SetParams("one795 two795");
    CHECK(flat->ExtraSources() == 1u);
    CHECK(flat->ExtraAddress(0) == "af795n.two795");
    CHECK(flat->ExtraName(1).empty());
    CHECK(flat->ExtraAddress(1).empty());

    flat->SetParams("");
    CHECK(flat->ArrayName().empty());
    CHECK(flat->Address().empty());
    CHECK(flat->ExtraSources() == 0u);
    CHECK(flat->ExtraAddress(0).empty());
    CHECK_FALSE(flat->Overflowed());
  }
}

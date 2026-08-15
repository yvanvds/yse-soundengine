// Tests for .array.thin (issue #807) — the neighbour thin on the
// name-addressed value model .array settled (#548).
//
// What has to be proven, and what every case below is one of:
//
//   - **the array is bound from the creation argument.** An array never
//     travels down a cord, so ".array.thin <name> [<tolerance>]" resolves
//     the name once, on the control thread, and an `array <name>` message is
//     honoured only when it names the array already bound —
//     ArrayReferenceNames' bounded compare, never a registry lookup on a
//     message path.
//   - **which thin this is — #807's decision.** Tolerance 0, the default,
//     is the exact thin: the family's byte compare, .array.unique
//     restricted to neighbours, so symbols thin too and 7 and 7. stay
//     distinct. A positive tolerance is a numeric distance for the pairs
//     that can carry one, and byte-equality still drops what it always
//     dropped.
//   - **the baseline is the last survivor.** A drift climbing in steps
//     inside the tolerance is kept, not erased — the property that makes a
//     decimated control stream usable.
//   - **the removed count leaves before the reference — right to left** —
//     so a patch can tell thinned-nothing (count 0, landed) from a refused
//     thin, which emits nothing on either outlet.
//   - **nothing on a message path allocates.** In-patcher delivery
//     dispatches on T_DSP, so "the audio thread thins an array" is the
//     ordinary case.
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
#include "patcher/genericObjects/gArrayThin.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using TestHelpers::IntSink;
using TestHelpers::MultiSink;
using TestHelpers::OrderSink;
using TestHelpers::Wire;
using YSE::PATCHER::gArray;
using YSE::PATCHER::gArrayThin;

namespace {

  // An .array and one .array.thin on one name, sharing one
  // patcherImplementation so the name actually binds ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // keeper is also the witness: its Count()/ElementAt() read back what the
  // thin left in the shared store. The sinks are declared before the objects
  // so they are torn down last, while the outlets wired to them still exist
  // (see sinks.hpp on why that matters).
  struct Rig {
    MultiSink ref; // outlet 0: the reference, after a thin that landed
    IntSink removed; // outlet 1: how many elements went
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    gArray keeper;
    gArrayThin object;

    Rig(const std::string& patcherName, const std::string& name,
        const std::string& params = std::string()) {
      p.SetName(patcherName);
      keeper.SetParent(&p);
      keeper.SetParams(name);
      object.SetParent(&p);
      object.SetParams(params.empty() ? name : params);
      Wire(object, 0, ref);
      Wire(object, 1, removed);
    }

    void seed(const std::string& elements) {
      keeper.GetInlet(0)->SetList("append " + elements, YSE::T_GUI);
    }

    void reset() {
      ref.reset();
      removed.gotInt = false;
      removed.received = -999;
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("array.thin: registered, with its inlets and outlets (#807)") {
    YSE::patcher p;
    p.create(2);

    // Trigger, tolerance, reference — the remover's trigger with the
    // family's cold configuration; the reference and the removed count
    // leave.
    YSE::pHandle* thin = p.CreateObject(YSE::OBJ::G_ARRAY_THIN);
    REQUIRE(thin != nullptr);
    CHECK(std::string(thin->Type()) == ".array.thin");
    CHECK(thin->GetInputs() == 3);
    CHECK(thin->GetOutputs() == 2);

    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_ARRAY_THIN)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("array.thin: a thin is asked for with a bang — no number method on the trigger, "
            "none but numbers on the tolerance (#807)") {
    gArrayThin thin;
    const unsigned int trigger = thin.GetInlet(0)->GetAcceptedTypes();
    CHECK((trigger & YSE::PATCHER::IT_BANG) != 0);
    CHECK((trigger & YSE::PATCHER::IT_LIST) != 0);
    CHECK((trigger & YSE::PATCHER::IT_INT) == 0);
    CHECK((trigger & YSE::PATCHER::IT_FLOAT) == 0);
    const unsigned int tolerance = thin.GetInlet(1)->GetAcceptedTypes();
    CHECK((tolerance & YSE::PATCHER::IT_INT) != 0);
    CHECK((tolerance & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((tolerance & YSE::PATCHER::IT_LIST) == 0);
    CHECK((tolerance & YSE::PATCHER::IT_BANG) == 0);
    const unsigned int reference = thin.GetInlet(2)->GetAcceptedTypes();
    CHECK((reference & YSE::PATCHER::IT_LIST) != 0);
    CHECK((reference & YSE::PATCHER::IT_BANG) == 0);
  }

  // ─── the exact thin — tolerance 0, the default ──────────────────────────────

  TEST_CASE("array.thin: the default is the exact neighbour thin — repeats go, recurrences "
            "stay (#807)") {
    // .array.unique restricted to neighbours: consecutive duplicates
    // removed, the same value recurring later kept — the whole difference
    // from the wholesale dedupe.
    Rig rig("apt807a", "t807a");
    CHECK(rig.object.StoredTolerance() == 0.f);
    rig.seed("10 10 10 20 10 c c 20");

    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.removed.gotInt);
    CHECK(rig.removed.received == 3);
    CHECK(rig.ref.gotList);
    CHECK(rig.ref.listValue == "array t807a");
    REQUIRE(rig.keeper.Count() == 5);
    CHECK(rig.keeper.ElementAt(0) == "10");
    CHECK(rig.keeper.ElementAt(1) == "20");
    CHECK(rig.keeper.ElementAt(2) == "10");
    CHECK(rig.keeper.ElementAt(3) == "c");
    CHECK(rig.keeper.ElementAt(4) == "20");
    CHECK(rig.object.Dropped() == 0);

    // Idempotent: a second bang finds nothing near, still lands — count 0
    // is an answer, not a refusal — and the array does not move.
    rig.reset();
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.removed.received == 0);
    CHECK(rig.ref.gotList);
    CHECK(rig.keeper.Count() == 5);
  }

  TEST_CASE("array.thin: at tolerance 0 equality is the spelling — 7 and 7. stay distinct "
            "(#807)") {
    // The family's byte compare: a different spelling is a different atom
    // downstream, so the exact thin keeps it — where any positive tolerance
    // would measure the distance 0 and drop it.
    Rig rig("apt807b", "t807b");
    rig.seed("7 7. 7 7");

    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.removed.received == 1);
    REQUIRE(rig.keeper.Count() == 3);
    CHECK(rig.keeper.ElementAt(0) == "7");
    CHECK(rig.keeper.ElementAt(1) == "7.");
    CHECK(rig.keeper.ElementAt(2) == "7");
  }

  TEST_CASE("array.thin: an empty or one-element array thins to itself — landed, count 0 "
            "(#807)") {
    Rig rig("apt807c", "t807c");
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.removed.received == 0);
    CHECK(rig.ref.gotList);

    rig.reset();
    rig.seed("42");
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.removed.received == 0);
    CHECK(rig.keeper.Count() == 1);
    CHECK(rig.object.Dropped() == 0);
  }

  // ─── the tolerance thin ─────────────────────────────────────────────────────

  TEST_CASE("array.thin: a positive tolerance is measured against the last survivor — a "
            "drift is kept, not erased (#807)") {
    // 0 0.1 0.2 0.3 0.4 at tolerance 0.15: against the ORIGINAL
    // predecessor every step is inside the tolerance and the whole drift
    // would collapse to "0" — misstating the stream by an unbounded amount.
    // Against the survivor: 0.1 goes (near 0), 0.2 stays (0.2 from 0), 0.3
    // goes (near 0.2), 0.4 stays (0.2 from 0.2) — the thinned array still
    // spans the drift, off by at most the tolerance.
    Rig rig("apt807d", "t807d", "t807d 0.15");
    CHECK(rig.object.StoredTolerance() == doctest::Approx(0.15f));
    rig.seed("0. 0.1 0.2 0.3 0.4");

    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.removed.received == 2);
    REQUIRE(rig.keeper.Count() == 3);
    CHECK(rig.keeper.ElementAt(0) == "0.");
    CHECK(rig.keeper.ElementAt(1) == "0.2");
    CHECK(rig.keeper.ElementAt(2) == "0.4");
    CHECK(rig.object.Dropped() == 0);
  }

  TEST_CASE("array.thin: under a tolerance a pair that is not wholly numeric falls back to "
            "the byte compare (#807)") {
    // 5.05 is inside the tolerance of 5 and goes; c against 5.2 has no
    // distance to measure and stays; c against c is byte-equal — an exact
    // duplicate is the nearest duplicate there is — and goes at any
    // tolerance; 5.02 is inside the tolerance of the surviving 5 and goes.
    Rig rig("apt807e", "t807e", "t807e 0.1");
    rig.seed("5 5.05 5.2 c c 5 5.02");

    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.removed.received == 3);
    REQUIRE(rig.keeper.Count() == 4);
    CHECK(rig.keeper.ElementAt(0) == "5");
    CHECK(rig.keeper.ElementAt(1) == "5.2");
    CHECK(rig.keeper.ElementAt(2) == "c");
    CHECK(rig.keeper.ElementAt(3) == "5");
  }

  // ─── the tolerance inlet ────────────────────────────────────────────────────

  TEST_CASE("array.thin: the tolerance inlet stores silently and refuses what is not a "
            "distance — negative, non-finite — never clamping (#807)") {
    Rig rig("apt807f", "t807f");
    rig.seed("10 10.5 12");

    // Stored silently — nothing leaves, nothing is thinned until the
    // trigger asks. An int is a distance as readily as a float.
    rig.object.GetInlet(1)->SetInt(1, YSE::T_GUI);
    CHECK(rig.object.StoredTolerance() == 1.f);
    CHECK_FALSE(rig.removed.gotInt);
    CHECK(rig.keeper.Count() == 3);

    // Negative and non-finite are refused before they are stored, and the
    // stored tolerance does not move.
    rig.object.GetInlet(1)->SetFloat(-0.5f, YSE::T_GUI);
    CHECK(rig.object.Dropped() == 1);
    CHECK(rig.object.StoredTolerance() == 1.f);
    rig.object.GetInlet(1)->SetFloat(std::numeric_limits<float>::infinity(), YSE::T_GUI);
    CHECK(rig.object.Dropped() == 2);
    rig.object.GetInlet(1)->SetFloat(std::numeric_limits<float>::quiet_NaN(), YSE::T_GUI);
    CHECK(rig.object.Dropped() == 3);
    CHECK(rig.object.StoredTolerance() == 1.f);

    // The stored tolerance is what the next trigger thins to: 10.5 is
    // inside 1 of 10 and goes, 12 is 2 from 10 and stays.
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.removed.received == 1);
    REQUIRE(rig.keeper.Count() == 2);
    CHECK(rig.keeper.ElementAt(0) == "10");
    CHECK(rig.keeper.ElementAt(1) == "12");

    // 0 stores — it is the exact thin, not a refusal — and restores the
    // byte compare: nothing here spells the same, so nothing goes.
    rig.reset();
    rig.object.GetInlet(1)->SetFloat(0.f, YSE::T_GUI);
    CHECK(rig.object.StoredTolerance() == 0.f);
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.removed.received == 0);
    CHECK(rig.keeper.Count() == 2);
    CHECK(rig.object.Dropped() == 3);
  }

  TEST_CASE("array.thin: a negative creation argument is malformed — every trigger refuses, "
            "counted, until the inlet stores a real tolerance (#807)") {
    // A seed only the creation string can plant: the inlet refuses one
    // before storing it, so the trigger is where it surfaces — refused
    // whole, nothing thinned, nothing announced. Malformed, not a miss,
    // and never clamped. gArrayStream's rule.
    Rig rig("apt807g", "t807g", "t807g -1");
    rig.seed("10 10 20");

    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.object.Dropped() == 1);
    CHECK_FALSE(rig.removed.gotInt);
    CHECK_FALSE(rig.ref.gotList);
    CHECK(rig.keeper.Count() == 3);

    // A real tolerance un-wedges it.
    rig.object.GetInlet(1)->SetFloat(0.f, YSE::T_GUI);
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.removed.received == 1);
    CHECK(rig.keeper.Count() == 2);
    CHECK(rig.object.Dropped() == 1);
  }

  // ─── the announcement ───────────────────────────────────────────────────────

  TEST_CASE("array.thin: the removed count leaves before the reference — right to left "
            "(#807)") {
    // OrderSink's log is the proof a counter could not give: the count has
    // arrived wherever it is wired by the time the reference triggers the
    // family downstream.
    std::vector<char> log;
    OrderSink refOrder;
    OrderSink countOrder;
    refOrder.log = &log;
    refOrder.tag = 'r';
    countOrder.log = &log;
    countOrder.tag = 'c';
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("apt807h");
    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("t807h");
    keeper.GetInlet(0)->SetList("append 1 1 2", YSE::T_GUI);
    gArrayThin thin;
    thin.SetParent(&p);
    thin.SetParams("t807h");
    Wire(thin, 0, refOrder);
    Wire(thin, 1, countOrder);

    thin.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(log.size() == 2);
    CHECK(log[0] == 'c');
    CHECK(log[1] == 'r');
    CHECK(countOrder.lastInt == 1);
    CHECK(refOrder.lastList == "array t807h");
    CHECK(keeper.Count() == 2);
  }

  // ─── the reference gesture, and refusals ────────────────────────────────────

  TEST_CASE("array.thin: the bound reference thins, any other list is refused (#807)") {
    Rig rig("apt807i", "t807i");
    rig.seed("10 10 20");
    rig.reset();

    // The family's gesture: the message an .array's reference outlet emits
    // on a bang thins — bang the array, out comes the thinned depth.
    rig.object.GetInlet(0)->SetList("array t807i", YSE::T_GUI);
    CHECK(rig.removed.received == 1);
    CHECK(rig.ref.gotList);
    CHECK(rig.keeper.Count() == 2);
    CHECK(rig.object.Dropped() == 0);

    // Only the bound name can be recognised at all — resolving an
    // unrecognised name means the registry's mutex — and a thin takes no
    // other message, so anything else on the trigger is refused and
    // counted. gArrayEndsRemover's rule.
    rig.reset();
    rig.object.GetInlet(0)->SetList("array elsewhere", YSE::T_GUI);
    CHECK(rig.object.Dropped() == 1);
    CHECK_FALSE(rig.removed.gotInt);
    CHECK_FALSE(rig.ref.gotList);
    CHECK(rig.keeper.Count() == 2);

    // The reference inlet acknowledges the bound name silently and refuses
    // anything else — gDictSlice's inlet rule.
    rig.object.GetInlet(2)->SetList("array t807i", YSE::T_GUI);
    CHECK(rig.object.Dropped() == 1);
    rig.object.GetInlet(2)->SetList("array elsewhere", YSE::T_GUI);
    CHECK(rig.object.Dropped() == 2);
    CHECK_FALSE(rig.removed.gotInt);
  }

  TEST_CASE("array.thin: an unnamed object thins a private array — the count leaves, the "
            "reference stays silent (#807)") {
    // No name, no shared store, no reference to pass on — but an answer is
    // a value, not an identity, so the removed count still leaves.
    MultiSink ref;
    IntSink removed;
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("apt807j");
    gArrayThin thin;
    thin.SetParent(&p);
    Wire(thin, 0, ref);
    Wire(thin, 1, removed);
    CHECK(thin.Address().empty());

    thin.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(removed.gotInt);
    CHECK(removed.received == 0);
    CHECK_FALSE(ref.gotList);
    CHECK(thin.Dropped() == 0);
  }

  TEST_CASE("array.thin: wired into the family, the thin drives .array.length through the "
            "public patcher API (#807)") {
    // The flow a patch actually wires, end to end: contents into the
    // .array, a bang on the thin, its reference into .array.length — which
    // reports the thinned depth, because the reference leaves after the
    // count with the array already compacted.
    IntSink length;
    YSE::pHandle lengthHandle(&length);
    YSE::patcher p;
    p.create(2);
    p.name("apt807k");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "t807k");
    YSE::pHandle* thin = p.CreateObject(YSE::OBJ::G_ARRAY_THIN, "t807k");
    YSE::pHandle* len = p.CreateObject(YSE::OBJ::G_ARRAY_LENGTH, "t807k");
    REQUIRE(array != nullptr);
    REQUIRE(thin != nullptr);
    REQUIRE(len != nullptr);
    p.Connect(thin, 0, len, 0);
    p.Connect(len, 0, &lengthHandle, 0);

    array->SetListData(0, "append 5 5 5 9 9 2");
    thin->SetBang(0);
    CHECK(length.gotInt);
    CHECK(length.received == 3);
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("array.thin: patcherImplementation::SetName re-anchors the shared base (#807)") {
    // The rename dispatch itself: an object created *inside* a patcher must
    // be re-anchored by the patcher, without anybody calling RefreshBinding
    // by hand. The keeper holds the old-address store; after the rename the
    // thin lands on a fresh empty array under the new prefix — count 0 —
    // while the keeper's contents stay put, duplicates and all.
    IntSink removed;
    YSE::pHandle removedHandle(&removed);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("apt807l_before");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("t807l");
    keeper.GetInlet(0)->SetList("append 10 10 20", YSE::T_GUI);

    YSE::pHandle* thin = p.CreateObject(YSE::OBJ::G_ARRAY_THIN, "t807l");
    REQUIRE(thin != nullptr);
    p.Connect(thin, 1, &removedHandle, 0);

    thin->SetBang(0);
    CHECK(removed.received == 1);
    CHECK(keeper.Count() == 2);

    p.SetName("apt807l_after");
    keeper.GetInlet(0)->SetList("append 30 30", YSE::T_GUI);
    thin->SetBang(0);
    // The thin landed — on the new, empty array — and the keeper's
    // duplicates were not touched.
    CHECK(removed.received == 0);
    REQUIRE(keeper.Count() == 4);
    CHECK(keeper.ElementAt(2) == "30");
    CHECK(keeper.ElementAt(3) == "30");
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("array.thin: a trigger arriving over in-patcher delivery lands on T_DSP (#807)") {
    // A .r feeding the trigger dispatches on T_DSP when the block drains it
    // (issue #225) — "the audio thread thins an array" is the ordinary
    // case, and the whole path is one guard hold, one bounded scan and two
    // sends.
    IntSink removed;
    YSE::pHandle removedHandle(&removed);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("apt807m");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("t807m");
    keeper.GetInlet(0)->SetList("append 10 10 10 20", YSE::T_GUI);

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go807m");
    YSE::pHandle* thin = p.CreateObject(YSE::OBJ::G_ARRAY_THIN, "t807m");
    REQUIRE(recv != nullptr);
    REQUIRE(thin != nullptr);
    p.Connect(recv, 0, thin, 0);
    p.Connect(thin, 1, &removedHandle, 0);

    p.PassData(std::string("array t807m"), "go807m", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    CHECK(removed.gotInt);
    CHECK(removed.received == 2);
    REQUIRE(keeper.Count() == 2);
    CHECK(keeper.ElementAt(0) == "10");
    CHECK(keeper.ElementAt(1) == "20");
  }

  TEST_CASE("array.thin: no message path allocates (#807)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path — the thin that removes (the compaction's moves
    // included), the thin that finds nothing, the reference gesture, the
    // foreign-list refusal, the tolerance stores and refusals, the
    // reference-inlet acknowledgement, the malformed-seed refusal and the
    // unnamed object's private thin — on T_DSP, in-patcher delivery's
    // thread.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string reference = "array probeAT807";
    const std::string foreign = "array somewhere_else_long";
    const std::string refill = "append 5 5.05 5.2 c c 9";

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("apt807n");
    MultiSink ref;
    IntSink removed;
    MultiSink privateRef;
    IntSink privateRemoved;
    gArray keeper;
    gArrayThin thin;
    gArrayThin unnamed;
    gArrayThin misSeeded;
    keeper.SetParent(&p);
    keeper.SetParams("probeAT807");
    thin.SetParent(&p);
    thin.SetParams("probeAT807 0.1");
    unnamed.SetParent(&p);
    misSeeded.SetParent(&p);
    misSeeded.SetParams("probeAT807bad -1");
    Wire(thin, 0, ref);
    Wire(thin, 1, removed);
    Wire(unnamed, 0, privateRef);
    Wire(unnamed, 1, privateRemoved);

    // Warm every path — including the sinks' assignments — so first-call
    // machinery is not what the probe catches.
    keeper.GetInlet(0)->SetList(refill, YSE::T_GUI);
    thin.GetInlet(0)->SetBang(YSE::T_GUI);
    thin.GetInlet(0)->SetList(reference, YSE::T_GUI);
    thin.GetInlet(0)->SetList(foreign, YSE::T_GUI); // refused
    thin.GetInlet(1)->SetInt(1, YSE::T_GUI);
    thin.GetInlet(1)->SetFloat(0.1f, YSE::T_GUI);
    thin.GetInlet(1)->SetFloat(-1.f, YSE::T_GUI); // refused
    thin.GetInlet(2)->SetList(reference, YSE::T_GUI);
    thin.GetInlet(2)->SetList(foreign, YSE::T_GUI); // refused
    unnamed.GetInlet(0)->SetBang(YSE::T_GUI);
    misSeeded.GetInlet(0)->SetBang(YSE::T_GUI); // refused: malformed seed

    // Refill outside the scope — gArray's own append, already proven
    // allocation-free in its tests — so the probe's first bang does real
    // removals and real compaction moves, not a scan of survivors.
    keeper.GetInlet(0)->SetList(refill, YSE::T_GUI);

    const std::uint64_t drops = thin.Dropped();
    const std::uint64_t misSeededDrops = misSeeded.Dropped();
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      thin.GetInlet(0)->SetBang(YSE::T_DSP); // removes, compacting
      thin.GetInlet(0)->SetBang(YSE::T_DSP); // removes nothing, still lands
      thin.GetInlet(0)->SetList(reference, YSE::T_DSP); // gesture: thins
      thin.GetInlet(0)->SetList(foreign, YSE::T_DSP); // refused
      thin.GetInlet(1)->SetInt(1, YSE::T_DSP); // stored, silent
      thin.GetInlet(1)->SetFloat(0.1f, YSE::T_DSP); // stored, silent
      thin.GetInlet(1)->SetFloat(-1.f, YSE::T_DSP); // refused
      thin.GetInlet(2)->SetList(reference, YSE::T_DSP); // acknowledged
      thin.GetInlet(2)->SetList(foreign, YSE::T_DSP); // refused
      unnamed.GetInlet(0)->SetBang(YSE::T_DSP); // private thin
      misSeeded.GetInlet(0)->SetBang(YSE::T_DSP); // refused: malformed seed
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing. The first bang thinned the refill — 5.05 and
    // the second c went, the warm pass having left only distinct survivors
    // ahead of it — the count and reference left, the private thin counted,
    // and exactly the three refusals were counted.
    CHECK(ref.gotList);
    CHECK(ref.listValue == reference);
    CHECK(removed.gotInt);
    CHECK(privateRemoved.gotInt);
    CHECK_FALSE(privateRef.gotList);
    CHECK(thin.Dropped() == drops + 3);
    CHECK(misSeeded.Dropped() == misSeededDrops + 1);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("array.thin: params survive a DumpJSON / ParseJSON round trip (#807)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* thin = src.CreateObject(YSE::OBJ::G_ARRAY_THIN, "notes807 0.5");
    REQUIRE(thin != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);
    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".array.thin");
    CHECK(copy->GetParams() == std::string("notes807 0.5"));
    CHECK(copy->GetInputs() == 3);
    CHECK(copy->GetOutputs() == 2);
  }

  TEST_CASE("array.thin: a re-parse resets the tolerance along with the name (#807)") {
    // SetParams("") must not keep thinning to the distance the previous
    // arguments configured — gArrayStream's rule, and the reset drops the
    // binding back to a private array too. Tolerance 0 is a valid
    // configuration, so the reset object still thins — its own empty array,
    // exactly.
    Rig rig("apt807o", "t807o", "t807o 0.5");
    CHECK(rig.object.StoredTolerance() == doctest::Approx(0.5f));
    rig.seed("10 10 20");

    rig.object.SetParams("");
    CHECK(rig.object.Address().empty());
    CHECK(rig.object.StoredTolerance() == 0.f);
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.removed.received == 0);
    CHECK_FALSE(rig.ref.gotList);
    CHECK(rig.keeper.Count() == 3);
    CHECK(rig.object.Dropped() == 0);
  }

  TEST_CASE("array.thin: carries complete documentation metadata (#807)") {
    gArrayThin thin;
    CHECK_FALSE(thin.GetDescription().empty());
    CHECK(thin.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    REQUIRE(thin.GetParamDocs().size() == 2);
    CHECK(thin.GetParamDocs()[0].name == "name");
    CHECK(thin.GetParamDocs()[1].name == "tolerance");
  }
}

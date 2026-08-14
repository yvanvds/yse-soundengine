// Tests for .array.change / .array.compare (issue #800) — Max's array
// comparison pair on the name-addressed value model .array settled (#548).
//
// What has to be proven, and what every case below is one of:
//
//   - **every array is bound from a creation argument.** An array never
//     travels down a cord, so ".array.change <name>" and ".array.compare
//     <left> <right>" resolve their names once, on the control thread, and
//     an `array <name>` message is honoured only against the name already
//     bound on that inlet — ArrayReferenceNames' bounded compare, never a
//     registry lookup on a message path.
//   - **both are an element-by-element comparison** — count and spelling,
//     in order (Max's "order or value" / "value and order"): 7 and 7. are
//     different elements, and 10 20 is not 20 10.
//   - **.array.change compares against a baseline it keeps**, which starts
//     as the empty array (the scalar .change's creation-argument rule: an
//     array that already has contents is news on the first poll, an empty
//     one is not), is replaced under the same guard hold that read the
//     difference, and resets whenever the binding is re-read. The verdict
//     leaves on every poll, the reference only on a change — the verdict
//     first, the scalar .change's right-to-left order.
//   - **.array.compare never holds two guards at once** — the left array is
//     snapshotted under its guard, the verdict decided against the right
//     under that guard alone, which is what makes ".array.compare seq seq"
//     answer 1 instead of tripping over its own try-lock.
//   - **neither object ever writes to a shared store.**
//   - **the ask crosses the control/audio boundary and nothing allocates.**
//     In-patcher delivery dispatches on T_DSP, so "the audio thread asks
//     whether anything changed" is the ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names an array uses names of its own — one case's contents
// must not be visible to the next.

#include <doctest/doctest.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "patcher/genericObjects/gArray.h"
#include "patcher/genericObjects/gArrayCompare.h"
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

using TestHelpers::MultiSink;
using TestHelpers::OrderSink;
using TestHelpers::Wire;
using YSE::PATCHER::gArray;
using YSE::PATCHER::gArrayChange;
using YSE::PATCHER::gArrayCompare;

namespace {

  // The pair, for the cases that loop over the whole file.
  const char* const kPairTypes[] = {
      YSE::OBJ::G_ARRAY_CHANGE,
      YSE::OBJ::G_ARRAY_COMPARE,
  };

  // One .array and one .array.change on its name, sharing one
  // patcherImplementation so the name actually binds ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // sinks are declared before the objects so they are torn down last, while
  // the outlets wired to them still exist (see sinks.hpp on why that
  // matters).
  struct ChangeRig {
    MultiSink ref;
    MultiSink flag;
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    gArray array;
    gArrayChange op;

    ChangeRig(const std::string& patcherName, const std::string& name) {
      p.SetName(patcherName);
      array.SetParent(&p);
      array.SetParams(name);
      op.SetParent(&p);
      op.SetParams(name);
      Wire(op, 0, ref);
      Wire(op, 1, flag);
    }

    void Store(const std::string& message) {
      array.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void Poll() {
      op.GetInlet(0)->SetBang(YSE::T_GUI);
    }
    void Reset() {
      ref.reset();
      flag.reset();
    }
  };

  // Two .arrays and one .array.compare on their names.
  struct CompareRig {
    MultiSink out;
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    gArray left;
    gArray right;
    gArrayCompare op;

    CompareRig(const std::string& patcherName, const std::string& leftName,
               const std::string& rightName) {
      p.SetName(patcherName);
      left.SetParent(&p);
      left.SetParams(leftName);
      right.SetParent(&p);
      right.SetParams(rightName);
      op.SetParent(&p);
      op.SetParams(leftName + " " + rightName);
      Wire(op, 0, out);
    }

    void StoreLeft(const std::string& message) {
      left.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void StoreRight(const std::string& message) {
      right.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void Ask() {
      op.GetInlet(0)->SetBang(YSE::T_GUI);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("array.change/compare: both registered, with their inlets and outlets (#800)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* change = p.CreateObject(YSE::OBJ::G_ARRAY_CHANGE);
    REQUIRE(change != nullptr);
    CHECK(std::string(change->Type()) == std::string(YSE::OBJ::G_ARRAY_CHANGE));
    CHECK(change->GetInputs() == 2);
    CHECK(change->GetOutputs() == 2);

    YSE::pHandle* compare = p.CreateObject(YSE::OBJ::G_ARRAY_COMPARE);
    REQUIRE(compare != nullptr);
    CHECK(std::string(compare->Type()) == std::string(YSE::OBJ::G_ARRAY_COMPARE));
    CHECK(compare->GetInputs() == 2);
    CHECK(compare->GetOutputs() == 1);

    auto names = YSE::PATCHER::Register().AllNames();
    for (const char* type : kPairTypes) {
      CAPTURE(type);
      bool found = false;
      for (const auto& name : names) {
        if (name == std::string(type)) found = true;
      }
      CHECK(found);
    }
  }

  TEST_CASE("array.change/compare: the trigger asks, no number lands anywhere (#800)") {
    // Inlet 0 is the ask — a bang, or the "array <name>" reference, which is
    // list text; a verdict is asked for, never addressed. Inlet 1 takes list
    // text only: change's baseline is set by the bound reference, compare's
    // right side only acknowledges its own.
    for (const char* type : kPairTypes) {
      CAPTURE(type);
      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(type));
      REQUIRE(obj != nullptr);

      const unsigned int triggerIn = obj->GetInlet(0)->GetAcceptedTypes();
      CHECK((triggerIn & YSE::PATCHER::IT_BANG) != 0);
      CHECK((triggerIn & YSE::PATCHER::IT_LIST) != 0);
      CHECK((triggerIn & YSE::PATCHER::IT_INT) == 0);
      CHECK((triggerIn & YSE::PATCHER::IT_FLOAT) == 0);

      const unsigned int coldIn = obj->GetInlet(1)->GetAcceptedTypes();
      CHECK((coldIn & YSE::PATCHER::IT_LIST) != 0);
      CHECK((coldIn & YSE::PATCHER::IT_BANG) == 0);
      CHECK((coldIn & YSE::PATCHER::IT_INT) == 0);
      CHECK((coldIn & YSE::PATCHER::IT_FLOAT) == 0);
    }
  }

  // ─── .array.change: the baseline ────────────────────────────────────────────

  TEST_CASE(
      "array.change: the baseline starts empty — contents are news, emptiness is not (#800)") {
    // The scalar .change starts its stored value at 0, so a first 0 is
    // swallowed and a first 5 emits; the array analogue of 0 is the empty
    // array.
    ChangeRig loaded("ac800a", "a800a");
    loaded.Store("append 60 64 67");
    loaded.Poll();
    CHECK(loaded.flag.gotInt);
    CHECK(loaded.flag.intValue == 1);
    CHECK(loaded.ref.gotList);
    CHECK(loaded.ref.listValue == "array a800a");
    CHECK(loaded.op.Dropped() == 0);

    ChangeRig hollow("ac800a2", "a800a2");
    hollow.Poll();
    CHECK(hollow.flag.gotInt);
    CHECK(hollow.flag.intValue == 0);
    CHECK_FALSE(hollow.ref.gotList);
    CHECK(hollow.op.Dropped() == 0);
  }

  TEST_CASE("array.change: a repetition is swallowed, a change passes (#800)") {
    ChangeRig rig("ac800b", "a800b");
    rig.Store("append 10 20 30");

    // First poll: the contents are news.
    rig.Poll();
    CHECK(rig.flag.intValue == 1);
    CHECK(rig.ref.gotList);

    // Second poll, nothing moved: the verdict still leaves — Max's right
    // outlet reports on every input — but the reference stays silent, which
    // is the object's whole point.
    rig.Reset();
    rig.Poll();
    CHECK(rig.flag.gotInt);
    CHECK(rig.flag.intValue == 0);
    CHECK_FALSE(rig.ref.gotList);

    // A write, then a poll: news again — and the baseline was replaced, so
    // the poll after that is silent again.
    rig.Reset();
    rig.Store("append 40");
    rig.Poll();
    CHECK(rig.flag.intValue == 1);
    CHECK(rig.ref.gotList);
    CHECK(rig.ref.listValue == "array a800b");

    rig.Reset();
    rig.Poll();
    CHECK(rig.flag.intValue == 0);
    CHECK_FALSE(rig.ref.gotList);
    CHECK(rig.op.Dropped() == 0);
  }

  TEST_CASE("array.change: order and spelling are part of the value (#800)") {
    // Max: "if the order or value of the elements is different". The same
    // multiset in a different order is a change, and equality is the
    // spelling — the family's byte compare — so 7 becoming 7. is a change
    // too.
    ChangeRig rig("ac800c", "a800c");
    rig.Store("append 10 20");
    rig.Poll();
    CHECK(rig.flag.intValue == 1);

    rig.Reset();
    rig.Store("clear");
    rig.Store("append 20 10");
    rig.Poll();
    CHECK(rig.flag.intValue == 1);
    CHECK(rig.ref.gotList);

    ChangeRig spelled("ac800c2", "a800c2");
    spelled.Store("append 7");
    spelled.Poll();
    CHECK(spelled.flag.intValue == 1);
    spelled.Reset();
    spelled.Store("clear");
    spelled.Store("append 7.");
    spelled.Poll();
    CHECK(spelled.flag.intValue == 1);

    // And a shorter array is a change even when it is a prefix.
    ChangeRig shorter("ac800c3", "a800c3");
    shorter.Store("append 1 2 3");
    shorter.Poll();
    shorter.Reset();
    shorter.Store("delete 2");
    shorter.Poll();
    CHECK(shorter.flag.intValue == 1);
  }

  TEST_CASE("array.change: the baseline inlet re-baselines silently (#800)") {
    // Max's right inlet stores without generating output — the scalar
    // .change's set, whose whole value is moving the object's idea of
    // "current" without telling anybody.
    ChangeRig rig("ac800d", "a800d");
    rig.Store("append 60 64");

    const std::string ownReference = "array a800d";
    rig.op.GetInlet(1)->SetList(ownReference, YSE::T_GUI);
    CHECK_FALSE(rig.flag.gotInt);
    CHECK_FALSE(rig.ref.gotList);
    CHECK(rig.op.Dropped() == 0);

    // The next poll answers 0: the contents were made the baseline by the
    // re-baseline, not by a report.
    rig.Poll();
    CHECK(rig.flag.gotInt);
    CHECK(rig.flag.intValue == 0);
    CHECK_FALSE(rig.ref.gotList);

    // Anything else on that inlet is refused and counted — another name
    // cannot be resolved on a message path.
    const std::uint64_t before = rig.op.Dropped();
    rig.op.GetInlet(1)->SetList("array somewhere_else", YSE::T_GUI);
    rig.op.GetInlet(1)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK(rig.op.Dropped() == before + 2);
  }

  TEST_CASE("array.change: the trigger honours the bound reference only (#800)") {
    // "array <name>" is the message an .array's reference outlet emits on a
    // bang; on the trigger inlet it polls when it names the bound array —
    // the family gesture. Anything else is refused, never resolved.
    ChangeRig rig("ac800e", "a800e");
    rig.Store("append 5");

    rig.op.GetInlet(0)->SetList("array a800e", YSE::T_GUI);
    CHECK(rig.flag.gotInt);
    CHECK(rig.flag.intValue == 1);
    CHECK(rig.ref.gotList);
    CHECK(rig.op.Dropped() == 0);

    rig.Reset();
    const std::uint64_t before = rig.op.Dropped();
    rig.op.GetInlet(0)->SetList("array somewhere_else", YSE::T_GUI);
    rig.op.GetInlet(0)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK_FALSE(rig.flag.gotInt);
    CHECK_FALSE(rig.ref.gotList);
    CHECK(rig.op.Dropped() == before + 2);
  }

  TEST_CASE("array.change: an unnamed object watches a private, empty array (#800)") {
    // Not "shares the empty name" — gArray's rule. A private array never
    // changes, so every poll reports 0; and there is no name to pass on, so
    // the reference outlet never fires.
    MultiSink ref;
    MultiSink flag;
    YSE::pHandle refHandle(&ref);
    YSE::pHandle flagHandle(&flag);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ac800f");
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY_CHANGE, "");
    REQUIRE(h != nullptr);
    p.Connect(h, 0, &refHandle, 0);
    p.Connect(h, 1, &flagHandle, 0);

    h->SetBang(0);
    CHECK(flag.gotInt);
    CHECK(flag.intValue == 0);
    CHECK_FALSE(ref.gotList);
  }

  TEST_CASE("array.change: the verdict fires before the reference (#800)") {
    // Right to left, the scalar .change's outlet order: whatever the
    // reference triggers downstream must already see the matching report.
    std::vector<char> log;
    OrderSink refSink;
    refSink.log = &log;
    refSink.tag = 'r';
    OrderSink flagSink;
    flagSink.log = &log;
    flagSink.tag = 'c';
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ac800g");
    gArray array;
    array.SetParent(&p);
    array.SetParams("a800g");
    gArrayChange op;
    op.SetParent(&p);
    op.SetParams("a800g");
    Wire(op, 0, refSink);
    Wire(op, 1, flagSink);

    array.GetInlet(0)->SetList("append 1 2", YSE::T_GUI);
    op.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(log.size() == 2);
    CHECK(log[0] == 'c');
    CHECK(log[1] == 'r');
    CHECK(flagSink.lastInt == 1);
    CHECK(refSink.lastList == "array a800g");
  }

  TEST_CASE("array.change: a re-parse and a rename reset the baseline (#800)") {
    // The object then watches a different binding, and whatever that array
    // holds is news — while what the *old* array held must not leak into a
    // verdict about the new one.
    ChangeRig rig("ac800h", "a800h");
    rig.Store("append 10 20");
    rig.Poll();
    CHECK(rig.flag.intValue == 1);
    CHECK(rig.op.BaselineCount() == 2);

    // SetParams — even to the same name — is a reconfiguration: the baseline
    // is the empty array again, so the unchanged contents are news once
    // more.
    rig.Reset();
    rig.op.SetParams("a800h");
    CHECK(rig.op.BaselineCount() == 0);
    rig.Poll();
    CHECK(rig.flag.intValue == 1);
    CHECK(rig.ref.gotList);

    // A rename moves the binding to a fresh, empty array under the new
    // prefix. Had the baseline survived as [10 20], empty-vs-baseline would
    // report a change; the reset makes the truthful answer: nothing this
    // object has not reported.
    rig.Reset();
    rig.p.SetName("ac800h_after");
    rig.Poll();
    CHECK(rig.flag.gotInt);
    CHECK(rig.flag.intValue == 0);
    CHECK_FALSE(rig.ref.gotList);
  }

  // ─── .array.compare ─────────────────────────────────────────────────────────

  TEST_CASE("array.compare: equal means count and spelling, in order (#800)") {
    CompareRig same("ac800i", "l800i", "r800i");
    same.StoreLeft("append 60 64 67");
    same.StoreRight("append 60 64 67");
    same.Ask();
    CHECK(same.out.gotInt);
    CHECK(same.out.intValue == 1);
    CHECK(same.op.Dropped() == 0);

    // The same multiset in a different order is not equal — an array is a
    // sequence, Max's "value and order".
    CompareRig ordered("ac800i2", "l800i2", "r800i2");
    ordered.StoreLeft("append 60 64");
    ordered.StoreRight("append 64 60");
    ordered.Ask();
    CHECK(ordered.out.intValue == 0);

    // Equality is the spelling: 7 and 7. are different elements.
    CompareRig spelled("ac800i3", "l800i3", "r800i3");
    spelled.StoreLeft("append 7");
    spelled.StoreRight("append 7.");
    spelled.Ask();
    CHECK(spelled.out.intValue == 0);

    // A prefix is not the whole.
    CompareRig counted("ac800i4", "l800i4", "r800i4");
    counted.StoreLeft("append 10 20");
    counted.StoreRight("append 10 20 30");
    counted.Ask();
    CHECK(counted.out.intValue == 0);

    // Two empty arrays hold the same nothing.
    CompareRig hollow("ac800i5", "l800i5", "r800i5");
    hollow.Ask();
    CHECK(hollow.out.gotInt);
    CHECK(hollow.out.intValue == 1);
  }

  TEST_CASE("array.compare: both names binding one array answers 1, never self-blocks (#800)") {
    // ".array.compare seq seq" — the case that proves no two guards are ever
    // held at once: the left is snapshotted under its guard, the verdict
    // decided under the right's own hold, sequentially.
    CompareRig rig("ac800j", "s800j", "s800j");
    rig.StoreLeft("append 60 64 60 67");
    rig.Ask();
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 1);
    CHECK(rig.op.Dropped() == 0);
  }

  TEST_CASE("array.compare: an unnamed side is a private, empty array (#800)") {
    // Fully unnamed: two private empty sides are equal.
    MultiSink out;
    YSE::pHandle outHandle(&out);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ac800k");
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY_COMPARE, "");
    REQUIRE(h != nullptr);
    p.Connect(h, 0, &outHandle, 0);
    h->SetBang(0);
    CHECK(out.gotInt);
    CHECK(out.intValue == 1);

    // One bound side with contents against a missing second argument: the
    // private empty right is a different array.
    CompareRig lone("ac800k2", "l800k2", "unused800k2");
    lone.op.SetParams("l800k2");
    lone.StoreLeft("append 5");
    lone.Ask();
    CHECK(lone.out.gotInt);
    CHECK(lone.out.intValue == 0);
  }

  TEST_CASE("array.compare: the left reference asks, the right inlet acknowledges its own (#800)") {
    CompareRig rig("ac800l", "l800l", "r800l");
    rig.StoreLeft("append 10");
    rig.StoreRight("append 10");

    // The left array's reference on the compare inlet is the family gesture.
    rig.op.GetInlet(0)->SetList("array l800l", YSE::T_GUI);
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 1);
    CHECK(rig.op.Dropped() == 0);

    // The right array's name there is a mis-wired cord; so is anything the
    // object is not bound to. Refused, never resolved.
    rig.out.reset();
    const std::uint64_t before = rig.op.Dropped();
    rig.op.GetInlet(0)->SetList("array r800l", YSE::T_GUI);
    rig.op.GetInlet(0)->SetList("array somewhere_else", YSE::T_GUI);
    rig.op.GetInlet(0)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotInt);
    CHECK(rig.op.Dropped() == before + 3);

    // Inlet 1 answers to the right name — silently, setting nothing — and to
    // nothing else, the left name included.
    rig.op.GetInlet(1)->SetList("array r800l", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotInt);
    CHECK(rig.op.Dropped() == before + 3);
    rig.op.GetInlet(1)->SetList("array l800l", YSE::T_GUI);
    rig.op.GetInlet(1)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK(rig.op.Dropped() == before + 5);
  }

  TEST_CASE("array.change/compare: neither object writes to a shared store (#800)") {
    ChangeRig change("ac800m", "a800m");
    change.Store("append 10 10 20");
    change.Poll();
    change.op.GetInlet(1)->SetList("array a800m", YSE::T_GUI);
    CHECK(change.array.Count() == 3);
    CHECK(change.array.ElementAt(0) == "10");
    CHECK(change.array.ElementAt(2) == "20");

    CompareRig compare("ac800m2", "l800m2", "r800m2");
    compare.StoreLeft("append 1 2");
    compare.StoreRight("append 1 2 3");
    compare.Ask();
    CHECK(compare.left.Count() == 2);
    CHECK(compare.right.Count() == 3);
    CHECK(compare.right.ElementAt(2) == "3");
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("array.compare: patcherImplementation::SetName re-anchors both bindings (#800)") {
    // The rename dispatch itself: an object created *inside* a patcher must
    // be re-anchored by the patcher, and the dispatch must reach the
    // override, or only the left side would move. The keepers hold the
    // old-address stores; before the rename the unequal sides answer 0,
    // after it both sides read fresh empty arrays under the new prefix — so
    // a verdict of 1 proves *both* moved (a surviving right binding would
    // still answer 0).
    MultiSink out;
    YSE::pHandle outHandle(&out);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ac800n_before");

    gArray keepLeft;
    keepLeft.SetParent(&p);
    keepLeft.SetParams("l800n");
    keepLeft.GetInlet(0)->SetList("append 10", YSE::T_GUI);
    gArray keepRight;
    keepRight.SetParent(&p);
    keepRight.SetParams("r800n");
    keepRight.GetInlet(0)->SetList("append 10 20", YSE::T_GUI);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY_COMPARE, "l800n r800n");
    REQUIRE(h != nullptr);
    p.Connect(h, 0, &outHandle, 0);

    h->SetBang(0);
    CHECK(out.gotInt);
    CHECK(out.intValue == 0);

    p.SetName("ac800n_after");
    out.reset();
    h->SetBang(0);
    CHECK(out.gotInt);
    CHECK(out.intValue == 1);
  }

  TEST_CASE("array.change: patcherImplementation::SetName re-anchors and resets (#800)") {
    // The same dispatch for change: after the rename the object watches a
    // fresh empty array, and its baseline — which held the old contents —
    // must have been reset along with the binding, or empty-vs-baseline
    // would fabricate a change.
    MultiSink ref;
    MultiSink flag;
    YSE::pHandle refHandle(&ref);
    YSE::pHandle flagHandle(&flag);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ac800o_before");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a800o");
    keeper.GetInlet(0)->SetList("append 10 20", YSE::T_GUI);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY_CHANGE, "a800o");
    REQUIRE(h != nullptr);
    p.Connect(h, 0, &refHandle, 0);
    p.Connect(h, 1, &flagHandle, 0);

    h->SetBang(0);
    CHECK(flag.intValue == 1);
    CHECK(ref.gotList);

    p.SetName("ac800o_after");
    ref.reset();
    flag.reset();
    h->SetBang(0);
    CHECK(flag.gotInt);
    CHECK(flag.intValue == 0);
    CHECK_FALSE(ref.gotList);
  }

  TEST_CASE("array.compare: exposes both bound addresses (#800)") {
    // The two-name binding, visible: the left address is gArrayEndsBase's,
    // the right one this object's own mirror — and a parentless object holds
    // no address at all.
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ac800p");
    std::unique_ptr<YSE::PATCHER::pObject> obj(
        YSE::PATCHER::Register().Get(YSE::OBJ::G_ARRAY_COMPARE));
    REQUIRE(obj != nullptr);
    auto* op = static_cast<gArrayCompare*>(obj.get());
    op->SetParams("lft800 rgt800");
    CHECK(op->Address().empty());
    CHECK(op->RightAddress().empty());
    op->SetParent(&p);
    CHECK(op->ArrayName() == "lft800");
    CHECK(op->RightName() == "rgt800");
    CHECK(op->Address() == "ac800p.lft800");
    CHECK(op->RightAddress() == "ac800p.rgt800");

    // SetParams("") is a real reset on both sides — back to two private
    // arrays.
    op->SetParams("");
    CHECK(op->ArrayName().empty());
    CHECK(op->RightName().empty());
    CHECK(op->Address().empty());
    CHECK(op->RightAddress().empty());
  }

  // ─── the family, chained through the public API ─────────────────────────────

  TEST_CASE("array.change: the reference outlet gates the family (#800)") {
    // The use the issue names, end to end through the public patcher API:
    // the array's reference outlet polls the change, and the change's
    // reference outlet triggers downstream work only when something moved —
    // here an .array.length, whose report is the proof the gate opened.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::patcher p;
    p.create(2);
    p.name("ac800q");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "a800q");
    YSE::pHandle* change = p.CreateObject(YSE::OBJ::G_ARRAY_CHANGE, "a800q");
    YSE::pHandle* length = p.CreateObject(YSE::OBJ::G_ARRAY_LENGTH, "a800q");
    REQUIRE(array != nullptr);
    REQUIRE(change != nullptr);
    REQUIRE(length != nullptr);
    p.Connect(array, 1, change, 0);
    p.Connect(change, 0, length, 0);
    p.Connect(length, 0, &sinkHandle, 0);

    array->SetListData(0, "append 60 64 67");
    array->SetBang(0);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 3);

    // Nothing moved: the gate stays shut and the length is never asked.
    sink.reset();
    array->SetBang(0);
    CHECK_FALSE(sink.gotInt);

    // Something moved: the gate opens again.
    array->SetListData(0, "append 71");
    array->SetBang(0);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 4);
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("array.change/compare: an ask over in-patcher delivery lands on T_DSP (#800)") {
    // A .r feeding the trigger inlet dispatches on T_DSP when the block
    // drains it (issue #225) — "the audio thread asks whether anything
    // changed" is the ordinary case. The change's reference feeds the
    // compare's trigger, so one delivery exercises both objects on T_DSP.
    MultiSink flag;
    MultiSink verdict;
    YSE::pHandle flagHandle(&flag);
    YSE::pHandle verdictHandle(&verdict);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ac800r");

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go800r");
    YSE::pHandle* leftArray = p.CreateObject(YSE::OBJ::G_ARRAY, "l800r");
    YSE::pHandle* rightArray = p.CreateObject(YSE::OBJ::G_ARRAY, "r800r");
    YSE::pHandle* change = p.CreateObject(YSE::OBJ::G_ARRAY_CHANGE, "l800r");
    YSE::pHandle* compare = p.CreateObject(YSE::OBJ::G_ARRAY_COMPARE, "l800r r800r");
    REQUIRE(recv != nullptr);
    REQUIRE(leftArray != nullptr);
    REQUIRE(rightArray != nullptr);
    REQUIRE(change != nullptr);
    REQUIRE(compare != nullptr);
    p.Connect(recv, 0, change, 0);
    p.Connect(change, 0, compare, 0);
    p.Connect(change, 1, &flagHandle, 0);
    p.Connect(compare, 0, &verdictHandle, 0);

    leftArray->SetListData(0, "append 10 20");
    rightArray->SetListData(0, "append 10 20");

    p.PassData(std::string("array l800r"), "go800r", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(flag.gotInt);
    CHECK(flag.intValue == 1);
    REQUIRE(verdict.gotInt);
    CHECK(verdict.intValue == 1);
  }

  TEST_CASE("array.change/compare: no message path allocates (#800)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every path of both objects: the poll (changed and unchanged, baseline
    // replacement included), the silent re-baseline, the comparison (equal
    // and not), the reference sends, and the refusal and acknowledgement
    // paths — all on T_DSP.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string leftReference = "array probeL800";
    const std::string rightReference = "array probeR800";
    const std::string wrongName = "array somewhere_else_long";
    const std::string appendMessage = "append 40";

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ac800s");
    MultiSink ref;
    MultiSink flag;
    MultiSink verdict;
    gArray left;
    gArray right;
    gArrayChange change;
    gArrayCompare compare;

    left.SetParent(&p);
    left.SetParams("probeL800");
    right.SetParent(&p);
    right.SetParams("probeR800");
    change.SetParent(&p);
    change.SetParams("probeL800");
    compare.SetParent(&p);
    compare.SetParams("probeL800 probeR800");
    Wire(change, 0, ref);
    Wire(change, 1, flag);
    Wire(compare, 0, verdict);

    // Mixed contents, so the copies and compares run over ints, a float and
    // a symbol alike.
    left.GetInlet(0)->SetList("append 10 c4 7.5", YSE::T_GUI);
    right.GetInlet(0)->SetList("append 10 c4 7.5", YSE::T_GUI);

    // Warm every path — including the sinks' assignments — so first-call
    // machinery is not what the probe catches.
    change.GetInlet(0)->SetBang(YSE::T_GUI);
    change.GetInlet(0)->SetList(leftReference, YSE::T_GUI);
    change.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    change.GetInlet(1)->SetList(leftReference, YSE::T_GUI);
    change.GetInlet(1)->SetList(wrongName, YSE::T_GUI);
    compare.GetInlet(0)->SetBang(YSE::T_GUI);
    compare.GetInlet(0)->SetList(leftReference, YSE::T_GUI);
    compare.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    compare.GetInlet(1)->SetList(rightReference, YSE::T_GUI);
    compare.GetInlet(1)->SetList(wrongName, YSE::T_GUI);
    left.GetInlet(0)->SetList(appendMessage, YSE::T_GUI);

    const std::uint64_t changeDroppedBefore = change.Dropped();
    const std::uint64_t compareDroppedBefore = compare.Dropped();
    ref.reset();
    flag.reset();
    verdict.reset();
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      // The changed poll — the warm-up's append is news — then the swallowed
      // repetition, both with the verdict send; the baseline replacement
      // runs under the first.
      change.GetInlet(0)->SetBang(YSE::T_DSP);
      change.GetInlet(0)->SetList(leftReference, YSE::T_DSP);
      // The silent re-baseline, and both objects' refusal paths.
      change.GetInlet(1)->SetList(leftReference, YSE::T_DSP);
      change.GetInlet(0)->SetList(wrongName, YSE::T_DSP);
      change.GetInlet(1)->SetList(wrongName, YSE::T_DSP);
      // The unequal comparison (the append moved the left side), the right
      // acknowledgement, and the refusals.
      compare.GetInlet(0)->SetBang(YSE::T_DSP);
      compare.GetInlet(0)->SetList(leftReference, YSE::T_DSP);
      compare.GetInlet(1)->SetList(rightReference, YSE::T_DSP);
      compare.GetInlet(0)->SetList(wrongName, YSE::T_DSP);
      compare.GetInlet(1)->SetList(wrongName, YSE::T_DSP);
      // The equal comparison: match the right side through .array's own
      // allocation-free message path, then ask again.
      right.GetInlet(0)->SetList(appendMessage, YSE::T_DSP);
      compare.GetInlet(0)->SetBang(YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing.
    CHECK(ref.gotList);
    CHECK(ref.listValue == "array probeL800");
    CHECK(flag.gotInt);
    CHECK(flag.intValue == 0); // the second poll's swallowed repetition
    CHECK(verdict.gotInt);
    CHECK(verdict.intValue == 1); // the final, matched ask
    CHECK(change.Dropped() == changeDroppedBefore + 2);
    CHECK(compare.Dropped() == compareDroppedBefore + 2);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("array.change/compare: params survive a DumpJSON / ParseJSON round trip (#800)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* change = src.CreateObject(YSE::OBJ::G_ARRAY_CHANGE, "notes800");
    YSE::pHandle* compare = src.CreateObject(YSE::OBJ::G_ARRAY_COMPARE, "notes800 other800");
    REQUIRE(change != nullptr);
    REQUIRE(compare != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 2);

    for (const char* type : kPairTypes) {
      CAPTURE(type);
      // Found by type rather than by list position: the loaded patcher's
      // enumeration order is not the creation order.
      YSE::pHandle* copy = nullptr;
      for (unsigned int i = 0; i < loaded.Objects(); i++) {
        YSE::pHandle* handle = loaded.GetHandleFromList(static_cast<int>(i));
        REQUIRE(handle != nullptr);
        if (std::string(handle->Type()) == std::string(type)) copy = handle;
      }
      REQUIRE(copy != nullptr);
      const bool isChange = std::string(type) == std::string(YSE::OBJ::G_ARRAY_CHANGE);
      // The analyzer cannot see that a failed REQUIRE aborts the case
      // (doctest's failure path is a runtime jump), so it assumes `copy` may
      // be null here.
      // NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage)
      CHECK(copy->GetParams() == std::string(isChange ? "notes800" : "notes800 other800"));
      CHECK(copy->GetInputs() == 2);
      CHECK(copy->GetOutputs() == (isChange ? 2 : 1));
    }
  }

  TEST_CASE("array.change/compare: both carry complete documentation metadata (#800)") {
    for (const char* type : kPairTypes) {
      CAPTURE(type);
      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(type));
      REQUIRE(obj != nullptr);
      CHECK_FALSE(obj->GetDescription().empty());
      CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GENERIC);
      const auto& docs = obj->GetParamDocs();
      const bool isChange = std::string(type) == std::string(YSE::OBJ::G_ARRAY_CHANGE);
      REQUIRE(docs.size() == (isChange ? 1u : 2u));
      if (isChange) {
        CHECK(docs[0].name == "name");
      } else {
        CHECK(docs[0].name == "left");
        CHECK(docs[1].name == "right");
      }
    }
  }
}

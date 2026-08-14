// Tests for .array.union / .array.sect / .array.unique (issue #792) — Max's
// array set operations on the name-addressed value model .array settled
// (#548).
//
// What has to be proven, and what every case below is one of:
//
//   - **every array is bound from a creation argument.** An array never
//     travels down a cord, so ".array.union <left> <right>" resolves both
//     names once, on the control thread, and an `array <name>` message is
//     honoured only against the name already bound on that inlet —
//     ArrayReferenceNames' bounded compare, never a registry lookup on a
//     message path.
//   - **the result leaves as list text, never as a new named array** —
//     the family's decision, inherited: creating an array from a message
//     would mean resolving a name on a message path.
//   - **the semantics are .zl's for lists, written down.** A set operation
//     produces a set — each element once, at its first occurrence; union is
//     the thinned left plus the right's leftovers, sect the left's elements
//     the right also holds, unique the one-array thin. Equality is the
//     spelling, ArrayFind's byte compare, so 7 and 7. are different
//     elements.
//   - **no two guards are ever held at once** — the left array is
//     snapshotted under its guard, the result built against the right under
//     that guard alone, which is what makes ".array.union chord chord"
//     answer instead of tripping over its own try-lock.
//   - **an operation never writes to either store.**
//   - **the ask crosses the control/audio boundary and nothing allocates.**
//     In-patcher delivery dispatches on T_DSP, so "the audio thread asks
//     which notes both chords hold" is the ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names an array uses names of its own — one case's contents
// must not be visible to the next.

#include <doctest/doctest.h>
#include <cstdint>
#include <memory>
#include <string>

#include "patcher/genericObjects/gArray.h"
#include "patcher/genericObjects/gArraySetOps.h"
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
using YSE::PATCHER::gArraySect;
using YSE::PATCHER::gArrayUnion;
using YSE::PATCHER::gArrayUnique;

namespace {

  // The three, for the cases that loop over the whole file.
  const char* const kSetOpTypes[] = {
      YSE::OBJ::G_ARRAY_UNION,
      YSE::OBJ::G_ARRAY_SECT,
      YSE::OBJ::G_ARRAY_UNIQUE,
  };

  // The two-array pair, for the cases about the second binding.
  const char* const kPairTypes[] = {
      YSE::OBJ::G_ARRAY_UNION,
      YSE::OBJ::G_ARRAY_SECT,
  };

  // Two .arrays and one set operation on their names, sharing one
  // patcherImplementation so the names actually bind ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // sinks are declared before the objects so they are torn down last, while
  // the outlets wired to them still exist (see sinks.hpp on why that
  // matters).
  template <typename ObjectT> struct PairRig {
    MultiSink out;
    BangSink empty;
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    gArray left;
    gArray right;
    ObjectT op;

    PairRig(const std::string& patcherName, const std::string& leftName,
            const std::string& rightName) {
      p.SetName(patcherName);
      left.SetParent(&p);
      left.SetParams(leftName);
      right.SetParent(&p);
      right.SetParams(rightName);
      op.SetParent(&p);
      op.SetParams(leftName + " " + rightName);
      Wire(op, 0, out);
      Wire(op, 1, empty);
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
    void Reset() {
      out.reset();
      empty.gotBang = false;
      empty.bangCount = 0;
    }
  };

  // The one-array rig for .array.unique.
  struct UniqueRig {
    MultiSink out;
    BangSink empty;
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    gArray array;
    gArrayUnique op;

    UniqueRig(const std::string& patcherName, const std::string& name) {
      p.SetName(patcherName);
      array.SetParent(&p);
      array.SetParams(name);
      op.SetParent(&p);
      op.SetParams(name);
      Wire(op, 0, out);
      Wire(op, 1, empty);
    }

    void Store(const std::string& message) {
      array.GetInlet(0)->SetList(message, YSE::T_GUI);
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

  TEST_CASE("array.setops: all three registered, with their inlets and outlets (#792)") {
    YSE::patcher p;
    p.create(2);
    for (const char* type : kSetOpTypes) {
      CAPTURE(type);
      YSE::pHandle* h = p.CreateObject(type);
      REQUIRE(h != nullptr);
      CHECK(std::string(h->Type()) == std::string(type));
      CHECK(h->GetInputs() == 2);
      CHECK(h->GetOutputs() == 2);
    }

    auto names = YSE::PATCHER::Register().AllNames();
    for (const char* type : kSetOpTypes) {
      CAPTURE(type);
      bool found = false;
      for (const auto& name : names) {
        if (name == std::string(type)) found = true;
      }
      CHECK(found);
    }
  }

  TEST_CASE("array.setops: the trigger takes the ask, the reference inlet only lists (#792)") {
    // Inlet 0 is the ask — a bang, or the "array <name>" reference, which is
    // list text; a result is asked for, never addressed, so no int lands
    // anywhere. Inlet 1 acknowledges a reference only.
    for (const char* type : kSetOpTypes) {
      CAPTURE(type);
      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(type));
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
  }

  // ─── .array.union ───────────────────────────────────────────────────────────

  TEST_CASE("array.union: the thinned left, then the right's leftovers (#792)") {
    PairRig<gArrayUnion> rig("aso792a", "l792a", "r792a");
    rig.StoreLeft("append 10 20 30");
    rig.StoreRight("append 20 40 10 50");

    // .zl union's order: the left array, then the right elements the left
    // does not hold, each once at its first occurrence.
    rig.Ask();
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "10 20 30 40 50");
    CHECK_FALSE(rig.empty.gotBang);
    CHECK(rig.op.Dropped() == 0);
  }

  TEST_CASE("array.union: a set operation produces a set (#792)") {
    // Repeats inside either array are thinned too — Max's "only one will be
    // output" — and equality is the spelling: 7 and 7. are different
    // elements, ArrayFind's byte compare.
    PairRig<gArrayUnion> rig("aso792b", "l792b", "r792b");
    rig.StoreLeft("append 10 10 c4 7");
    rig.StoreRight("append 7. c4 c4 10");

    rig.Ask();
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "10 c4 7 7.");
  }

  TEST_CASE("array.union: an empty side contributes nothing, two bang empty (#792)") {
    PairRig<gArrayUnion> rig("aso792c", "l792c", "r792c");
    rig.StoreRight("append 60 64 60");

    // An empty left is the thinned right alone.
    rig.Ask();
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "60 64");

    // An empty right is the thinned left alone — and a result of one leaves
    // typed, SendAtoms' rule.
    PairRig<gArrayUnion> single("aso792c2", "l792c2", "r792c2");
    single.StoreLeft("append 60 60 60");
    single.Ask();
    CHECK(single.out.gotInt);
    CHECK(single.out.intValue == 60);
    CHECK_FALSE(single.out.gotList);

    // Two empty arrays bang the empty outlet — "no data" is a state a patch
    // must be able to route on, not an error.
    PairRig<gArrayUnion> hollow("aso792c3", "l792c3", "r792c3");
    hollow.Ask();
    CHECK(hollow.empty.gotBang);
    CHECK_FALSE(hollow.out.gotList);
    CHECK(hollow.op.Dropped() == 0);
  }

  // ─── .array.sect ────────────────────────────────────────────────────────────

  TEST_CASE("array.sect: the left's elements the right also holds, in left order (#792)") {
    PairRig<gArraySect> rig("aso792d", "l792d", "r792d");
    rig.StoreLeft("append 10 20 30 40");
    rig.StoreRight("append 40 99 20");

    // .zl sect's order: the shared elements at their first occurrence in the
    // left array, however the right orders them.
    rig.Ask();
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "20 40");
    CHECK_FALSE(rig.empty.gotBang);

    // A repeat in the left counts once; a result of one leaves typed.
    PairRig<gArraySect> once("aso792d2", "l792d2", "r792d2");
    once.StoreLeft("append 10 10 20");
    once.StoreRight("append 10 10");
    once.Ask();
    CHECK(once.out.gotInt);
    CHECK(once.out.intValue == 10);
    CHECK_FALSE(once.out.gotList);
  }

  TEST_CASE("array.sect: disjoint arrays bang the empty outlet (#792)") {
    // Routing on "nothing in common" is half the point of asking — a bang,
    // not a counted error. Equality is the spelling, so 7 and 7. do not
    // intersect.
    PairRig<gArraySect> rig("aso792e", "l792e", "r792e");
    rig.StoreLeft("append 10 7 c4");
    rig.StoreRight("append 20 7. C4");

    rig.Ask();
    CHECK(rig.empty.gotBang);
    CHECK_FALSE(rig.out.gotList);
    CHECK(rig.op.Dropped() == 0);

    // An empty side intersects with nothing.
    PairRig<gArraySect> hollow("aso792e2", "l792e2", "r792e2");
    hollow.StoreLeft("append 10 20");
    hollow.Ask();
    CHECK(hollow.empty.gotBang);
    CHECK_FALSE(hollow.out.gotList);
  }

  // ─── the same-store degenerate ──────────────────────────────────────────────

  TEST_CASE("array.setops: both names binding one array answers, never self-blocks (#792)") {
    // ".array.union chord chord" — the case that proves no two guards are
    // ever held at once: with the left snapshotted under its guard and the
    // right tested under its own, the same store's guard is taken twice
    // *sequentially*, and both operations reduce to the thin.
    PairRig<gArrayUnion> u("aso792f", "s792f", "s792f");
    u.StoreLeft("append 60 64 60 67");
    u.Ask();
    CHECK(u.out.gotList);
    CHECK(u.out.listValue == "60 64 67");
    CHECK(u.op.Dropped() == 0);

    PairRig<gArraySect> s("aso792f2", "s792f2", "s792f2");
    s.StoreLeft("append 60 64 60 67");
    s.Ask();
    CHECK(s.out.gotList);
    CHECK(s.out.listValue == "60 64 67");
    CHECK(s.op.Dropped() == 0);
  }

  // ─── .array.unique ──────────────────────────────────────────────────────────

  TEST_CASE("array.unique: each element once, at its first occurrence (#792)") {
    UniqueRig rig("aso792g", "a792g");
    rig.Store("append 10 20 10 30 20 10");

    rig.Ask();
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "10 20 30");
    CHECK_FALSE(rig.empty.gotBang);

    // An array with nothing repeated passes through whole.
    rig.Reset();
    UniqueRig clean("aso792g2", "a792g2");
    clean.Store("append 1 2 3");
    clean.Ask();
    CHECK(clean.out.gotList);
    CHECK(clean.out.listValue == "1 2 3");

    // Equality is the spelling: 7 and 7. survive each other, c4 its case
    // twin.
    UniqueRig spelled("aso792g3", "a792g3");
    spelled.Store("append 7 7. 7 c4 C4 7.");
    spelled.Ask();
    CHECK(spelled.out.gotList);
    CHECK(spelled.out.listValue == "7 7. c4 C4");
  }

  TEST_CASE("array.unique: one element leaves typed, an empty array bangs empty (#792)") {
    UniqueRig rig("aso792h", "a792h");
    rig.Store("append 7.5 7.5 7.5");

    rig.Ask();
    CHECK(rig.out.gotFloat);
    CHECK(rig.out.floatValue == doctest::Approx(7.5f));
    CHECK_FALSE(rig.out.gotList);

    UniqueRig hollow("aso792h2", "a792h2");
    hollow.Ask();
    CHECK(hollow.empty.gotBang);
    CHECK_FALSE(hollow.out.gotList);
    CHECK(hollow.op.Dropped() == 0);
  }

  // ─── read-only ──────────────────────────────────────────────────────────────

  TEST_CASE("array.setops: an operation never writes to either store (#792)") {
    // The result is a list the object owns — the shared arrays must not be
    // thinned under everything else reading them; .array itself is where a
    // patch writes a result back.
    PairRig<gArrayUnion> rig("aso792i", "l792i", "r792i");
    rig.StoreLeft("append 10 10 20");
    rig.StoreRight("append 20 30");
    rig.Ask();
    CHECK(rig.out.gotList);
    CHECK(rig.left.Count() == 3);
    CHECK(rig.left.ElementAt(0) == "10");
    CHECK(rig.left.ElementAt(1) == "10");
    CHECK(rig.right.Count() == 2);
    CHECK(rig.right.ElementAt(0) == "20");

    UniqueRig unique("aso792i2", "a792i2");
    unique.Store("append 10 10 20");
    unique.Ask();
    CHECK(unique.out.gotList);
    CHECK(unique.array.Count() == 3);
    CHECK(unique.array.ElementAt(1) == "10");
  }

  // ─── the whole-reply rule ───────────────────────────────────────────────────

  TEST_CASE("array.union: a result that outruns a cord is refused whole (#792)") {
    // 150 + 150 distinct elements is a well-formed union of 300 — more atoms
    // than a list carries (AtomList::MAX_ATOMS is 256), so the ask is
    // refused whole and counted: a partial set would be a lie about
    // membership, .array.at's whole-reply rule.
    PairRig<gArrayUnion> rig("aso792j", "l792j", "r792j");
    std::string message = "append";
    for (int i = 0; i < 150; i++)
      message += " " + std::to_string(1000 + i);
    rig.StoreLeft(message);
    message = "append";
    for (int i = 0; i < 150; i++)
      message += " " + std::to_string(2000 + i);
    rig.StoreRight(message);
    CHECK(rig.left.Count() == 150);
    CHECK(rig.right.Count() == 150);

    const std::uint64_t before = rig.op.Dropped();
    rig.Ask();
    CHECK(rig.op.Dropped() == before + 1);
    CHECK_FALSE(rig.out.gotList);
    CHECK_FALSE(rig.empty.gotBang);
  }

  // ─── the references ─────────────────────────────────────────────────────────

  TEST_CASE("array.setops: the left reference asks on the trigger, anything else refused (#792)") {
    // "array <name>" is the message an .array's reference outlet emits on a
    // bang; on the trigger inlet it asks when it names the *left* array —
    // the family gesture. The right array's name there is a mis-wired cord,
    // refused, never resolved: a registry lookup is a mutex, and this may be
    // the audio thread.
    PairRig<gArraySect> rig("aso792k", "l792k", "r792k");
    rig.StoreLeft("append 10 20");
    rig.StoreRight("append 20 30");

    const std::uint64_t before = rig.op.Dropped();
    rig.op.GetInlet(0)->SetList("array l792k", YSE::T_GUI);
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 20);
    CHECK(rig.op.Dropped() == before);

    rig.Reset();
    rig.op.GetInlet(0)->SetList("array r792k", YSE::T_GUI);
    rig.op.GetInlet(0)->SetList("array somewhere_else", YSE::T_GUI);
    rig.op.GetInlet(0)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotInt);
    CHECK_FALSE(rig.out.gotList);
    CHECK_FALSE(rig.empty.gotBang);
    CHECK(rig.op.Dropped() == before + 3);

    // And the same gesture on .array.unique.
    UniqueRig unique("aso792k2", "a792k2");
    unique.Store("append 5 5");
    unique.op.GetInlet(0)->SetList("array a792k2", YSE::T_GUI);
    CHECK(unique.out.gotInt);
    CHECK(unique.out.intValue == 5);
    const std::uint64_t uniqueBefore = unique.op.Dropped();
    unique.op.GetInlet(0)->SetList("array somewhere_else", YSE::T_GUI);
    CHECK(unique.op.Dropped() == uniqueBefore + 1);
  }

  TEST_CASE("array.setops: the reference inlet acknowledges its own side only (#792)") {
    // The pair's inlet 1 answers to the *right* name — a patch may wire both
    // reference outlets across, as it would in Max — and to nothing else,
    // the left name included: each inlet is bound to one side.
    PairRig<gArrayUnion> rig("aso792l", "l792l", "r792l");
    rig.StoreLeft("append 1");

    const std::uint64_t before = rig.op.Dropped();
    rig.op.GetInlet(1)->SetList("array r792l", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotList);
    CHECK_FALSE(rig.out.gotInt);
    CHECK(rig.op.Dropped() == before);

    rig.op.GetInlet(1)->SetList("array l792l", YSE::T_GUI);
    rig.op.GetInlet(1)->SetList("array somewhere_else", YSE::T_GUI);
    rig.op.GetInlet(1)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK(rig.op.Dropped() == before + 3);

    // unique's inlet 1 answers to its one name.
    UniqueRig unique("aso792l2", "a792l2");
    const std::uint64_t uniqueBefore = unique.op.Dropped();
    unique.op.GetInlet(1)->SetList("array a792l2", YSE::T_GUI);
    CHECK(unique.op.Dropped() == uniqueBefore);
    unique.op.GetInlet(1)->SetList("array somewhere_else", YSE::T_GUI);
    CHECK(unique.op.Dropped() == uniqueBefore + 1);
  }

  TEST_CASE("array.setops: an unnamed object reads private, empty arrays (#792)") {
    // Not "shares the empty name" — gArray's rule, inherited on both sides.
    // Private arrays hold nothing, so every ask bangs empty and counts
    // nothing.
    for (const char* type : kSetOpTypes) {
      CAPTURE(type);
      MultiSink out;
      BangSink empty;
      YSE::pHandle outHandle(&out);
      YSE::pHandle emptyHandle(&empty);
      YSE::PATCHER::patcherImplementation p(2, nullptr);
      p.SetName("aso792m");
      YSE::pHandle* h = p.CreateObject(type, "");
      REQUIRE(h != nullptr);
      p.Connect(h, 0, &outHandle, 0);
      p.Connect(h, 1, &emptyHandle, 0);

      h->SetBang(0);
      CHECK(empty.gotBang);
      CHECK_FALSE(out.gotList);
      CHECK_FALSE(out.gotInt);
    }
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("array.setops: patcherImplementation::SetName re-anchors both bindings (#792)") {
    // The rename dispatch itself: an object created *inside* a patcher must
    // be re-anchored by the patcher, without anybody calling RefreshBinding
    // by hand — and for the pair the dispatch must reach gArraySetOpBase's
    // RefreshBinding, or only the left side would move. The keepers hold the
    // old-address stores (they are not in the patcher's object map, so the
    // rename does not touch them): before the rename the sect answers from
    // both sides; after it both sides read fresh empty arrays under the new
    // prefix, so the ask bangs empty.
    MultiSink out;
    BangSink empty;
    YSE::pHandle outHandle(&out);
    YSE::pHandle emptyHandle(&empty);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aso792n_before");

    gArray keepLeft;
    keepLeft.SetParent(&p);
    keepLeft.SetParams("l792n");
    keepLeft.GetInlet(0)->SetList("append 10 20", YSE::T_GUI);
    gArray keepRight;
    keepRight.SetParent(&p);
    keepRight.SetParams("r792n");
    keepRight.GetInlet(0)->SetList("append 20 30", YSE::T_GUI);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY_SECT, "l792n r792n");
    REQUIRE(h != nullptr);
    p.Connect(h, 0, &outHandle, 0);
    p.Connect(h, 1, &emptyHandle, 0);

    h->SetBang(0);
    CHECK(out.gotInt);
    CHECK(out.intValue == 20);
    CHECK_FALSE(empty.gotBang);

    p.SetName("aso792n_after");
    out.reset();
    h->SetBang(0);
    CHECK_FALSE(out.gotInt);
    CHECK_FALSE(out.gotList);
    CHECK(empty.gotBang);

    // And the right side alone re-anchors too: a union whose left keeper
    // still binds (recreated under the new prefix) must stop seeing the old
    // right contents.
    MultiSink out2;
    YSE::pHandle out2Handle(&out2);
    gArray newLeft;
    newLeft.SetParent(&p);
    newLeft.SetParams("l792n");
    newLeft.GetInlet(0)->SetList("append 1", YSE::T_GUI);
    YSE::pHandle* u = p.CreateObject(YSE::OBJ::G_ARRAY_UNION, "l792n r792n");
    REQUIRE(u != nullptr);
    p.Connect(u, 0, &out2Handle, 0);
    u->SetBang(0);
    CHECK(out2.gotInt);
    CHECK(out2.intValue == 1);
  }

  // ─── the family, chained through the public API ─────────────────────────────

  TEST_CASE(
      "array.setops: wired from the array's reference outlet, banging the array asks (#792)") {
    // The flow a patch actually wires, end to end through the public patcher
    // API: the left .array's reference outlet into the trigger inlet gives
    // the family gesture — bang the array, out comes the intersection — and
    // a write between two bangs changes what the next ask sees.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::patcher p;
    p.create(2);
    p.name("aso792o");
    YSE::pHandle* leftArray = p.CreateObject(YSE::OBJ::G_ARRAY, "l792o");
    YSE::pHandle* rightArray = p.CreateObject(YSE::OBJ::G_ARRAY, "r792o");
    YSE::pHandle* sect = p.CreateObject(YSE::OBJ::G_ARRAY_SECT, "l792o r792o");
    REQUIRE(leftArray != nullptr);
    REQUIRE(rightArray != nullptr);
    REQUIRE(sect != nullptr);
    p.Connect(leftArray, 1, sect, 0);
    p.Connect(sect, 0, &sinkHandle, 0);

    leftArray->SetListData(0, "append 60 64 67");
    rightArray->SetListData(0, "append 60 67 71");
    leftArray->SetBang(0);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "60 67");

    sink.reset();
    rightArray->SetListData(0, "append 64");
    leftArray->SetBang(0);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "60 64 67");
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("array.setops: an ask over in-patcher delivery lands on T_DSP (#792)") {
    // A .r feeding the trigger inlet dispatches on T_DSP when the block
    // drains it (issue #225) — "the audio thread asks which notes both
    // chords hold" is the ordinary case, and the whole path is two
    // sequential guard holds, one bounded collection and a send of list
    // text.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aso792p");

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go792p");
    YSE::pHandle* leftArray = p.CreateObject(YSE::OBJ::G_ARRAY, "l792p");
    YSE::pHandle* rightArray = p.CreateObject(YSE::OBJ::G_ARRAY, "r792p");
    YSE::pHandle* uni = p.CreateObject(YSE::OBJ::G_ARRAY_UNION, "l792p r792p");
    REQUIRE(recv != nullptr);
    REQUIRE(leftArray != nullptr);
    REQUIRE(rightArray != nullptr);
    REQUIRE(uni != nullptr);
    p.Connect(recv, 0, uni, 0);
    p.Connect(uni, 0, &sinkHandle, 0);

    leftArray->SetListData(0, "append 10 20");
    rightArray->SetListData(0, "append 20 30");

    p.PassData(std::string("array l792p"), "go792p", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "10 20 30");
  }

  TEST_CASE("array.setops: no message path allocates (#792)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every path of all three objects: the snapshot, both collections, the
    // typed and list-rendered sends, the empty outlet, and the refusal and
    // acknowledgement paths — all on T_DSP.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string leftReference = "array probeL792";
    const std::string rightReference = "array probeR792";
    const std::string uniqueReference = "array probeU792";
    const std::string wrongName = "array somewhere_else_long";
    const std::string clearMessage = "clear";

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aso792q");
    MultiSink unionSink;
    BangSink unionEmpty;
    MultiSink sectSink;
    BangSink sectEmpty;
    MultiSink uniqueSink;
    BangSink uniqueEmpty;
    gArray left;
    gArray right;
    gArray third;
    gArrayUnion uni;
    gArraySect sect;
    gArrayUnique unique;

    left.SetParent(&p);
    left.SetParams("probeL792");
    right.SetParent(&p);
    right.SetParams("probeR792");
    third.SetParent(&p);
    third.SetParams("probeU792");
    uni.SetParams("probeL792 probeR792");
    uni.SetParent(&p);
    sect.SetParams("probeL792 probeR792");
    sect.SetParent(&p);
    unique.SetParams("probeU792");
    unique.SetParent(&p);
    Wire(uni, 0, unionSink);
    Wire(uni, 1, unionEmpty);
    Wire(sect, 0, sectSink);
    Wire(sect, 1, sectEmpty);
    Wire(unique, 0, uniqueSink);
    Wire(unique, 1, uniqueEmpty);

    // Mixed populations, so the results carry ints, a float and symbols —
    // the list-rendered send path and the typed single-atom one both run —
    // and the third array is empty part of the scope so the empty outlet
    // runs on T_DSP too.
    left.GetInlet(0)->SetList("append 10 c4 7.5 10", YSE::T_GUI);
    right.GetInlet(0)->SetList("append c4 -3 20", YSE::T_GUI);

    // Warm every path — including the sinks' assignments — so first-call
    // machinery is not what the probe catches.
    YSE::PATCHER::pObject* pairs[2] = {&uni, &sect};
    for (auto* op : pairs) {
      op->GetInlet(0)->SetBang(YSE::T_GUI);
      op->GetInlet(0)->SetList(leftReference, YSE::T_GUI);
      op->GetInlet(0)->SetList(wrongName, YSE::T_GUI);
      op->GetInlet(1)->SetList(rightReference, YSE::T_GUI);
      op->GetInlet(1)->SetList(wrongName, YSE::T_GUI);
    }
    unique.GetInlet(0)->SetBang(YSE::T_GUI);
    unique.GetInlet(0)->SetList(uniqueReference, YSE::T_GUI);
    unique.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    unique.GetInlet(1)->SetList(uniqueReference, YSE::T_GUI);
    third.GetInlet(0)->SetList("append 5 5 8", YSE::T_GUI);
    unique.GetInlet(0)->SetBang(YSE::T_GUI);
    const std::uint64_t unionDroppedBefore = uni.Dropped();

    unionSink.reset();
    sectSink.reset();
    uniqueSink.reset();
    uniqueEmpty.gotBang = false;
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      uni.GetInlet(0)->SetBang(YSE::T_DSP);
      uni.GetInlet(0)->SetList(leftReference, YSE::T_DSP);
      uni.GetInlet(0)->SetList(wrongName, YSE::T_DSP);
      uni.GetInlet(1)->SetList(rightReference, YSE::T_DSP);
      uni.GetInlet(1)->SetList(wrongName, YSE::T_DSP);
      sect.GetInlet(0)->SetBang(YSE::T_DSP);
      sect.GetInlet(0)->SetList(leftReference, YSE::T_DSP);
      unique.GetInlet(0)->SetBang(YSE::T_DSP);
      unique.GetInlet(0)->SetList(uniqueReference, YSE::T_DSP);
      // The empty outlet on T_DSP too: clear the third array through the
      // .array's own message path, then ask again.
      third.GetInlet(0)->SetList(clearMessage, YSE::T_DSP);
      unique.GetInlet(0)->SetBang(YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing.
    CHECK(unionSink.gotList);
    CHECK(unionSink.listValue == "10 c4 7.5 -3 20");
    CHECK(sectSink.gotInt == false);
    CHECK(sectSink.gotList);
    CHECK(sectSink.listValue == "c4");
    CHECK(uniqueSink.gotList);
    CHECK(uniqueSink.listValue == "5 8");
    CHECK(uniqueEmpty.gotBang);
    CHECK(uni.Dropped() == unionDroppedBefore + 2);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("array.setops: params survive a DumpJSON / ParseJSON round trip (#792)") {
    YSE::patcher src;
    src.create(2);
    for (const char* type : kSetOpTypes) {
      const bool isUnique = std::string(type) == std::string(YSE::OBJ::G_ARRAY_UNIQUE);
      YSE::pHandle* h = src.CreateObject(type, isUnique ? "notes792" : "notes792 other792");
      REQUIRE(h != nullptr);
    }
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 3);

    for (const char* type : kSetOpTypes) {
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
      const bool isUnique = std::string(type) == std::string(YSE::OBJ::G_ARRAY_UNIQUE);
      // The analyzer cannot see that a failed REQUIRE aborts the case
      // (doctest's failure path is a runtime jump), so it assumes `copy` may
      // be null here.
      // NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage)
      CHECK(copy->GetParams() == std::string(isUnique ? "notes792" : "notes792 other792"));
      CHECK(copy->GetInputs() == 2);
      CHECK(copy->GetOutputs() == 2);
    }
  }

  TEST_CASE("array.setops: all three carry complete documentation metadata (#792)") {
    for (const char* type : kSetOpTypes) {
      CAPTURE(type);
      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(type));
      REQUIRE(obj != nullptr);
      CHECK_FALSE(obj->GetDescription().empty());
      CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GENERIC);
      const auto& docs = obj->GetParamDocs();
      const bool isUnique = std::string(type) == std::string(YSE::OBJ::G_ARRAY_UNIQUE);
      REQUIRE(docs.size() == (isUnique ? 1u : 2u));
      if (isUnique) {
        CHECK(docs[0].name == "name");
      } else {
        CHECK(docs[0].name == "left");
        CHECK(docs[1].name == "right");
      }
    }
  }

  TEST_CASE("array.setops: the pair exposes both bound addresses (#792)") {
    // The two-name binding, visible: the left address is gArrayEndsBase's,
    // the right one gArraySetOpBase's, both prefixed with the patcher name —
    // and a parentless object holds no address at all.
    for (const char* type : kPairTypes) {
      CAPTURE(type);
      YSE::PATCHER::patcherImplementation p(2, nullptr);
      p.SetName("aso792r");
      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(type));
      REQUIRE(obj != nullptr);
      auto* op = static_cast<YSE::PATCHER::gArraySetOpBase*>(obj.get());
      op->SetParams("lft792 rgt792");
      CHECK(op->Address().empty());
      CHECK(op->RightAddress().empty());
      op->SetParent(&p);
      CHECK(op->ArrayName() == "lft792");
      CHECK(op->RightName() == "rgt792");
      CHECK(op->Address() == "aso792r.lft792");
      CHECK(op->RightAddress() == "aso792r.rgt792");

      // SetParams("") is a real reset on both sides — back to two private
      // arrays.
      op->SetParams("");
      CHECK(op->ArrayName().empty());
      CHECK(op->RightName().empty());
      CHECK(op->Address().empty());
      CHECK(op->RightAddress().empty());
    }
  }
}

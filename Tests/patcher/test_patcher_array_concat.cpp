// Tests for .array.concat / .array.join (issue #793) — Max's two "put these
// together" operations on the name-addressed value model .array settled
// (#548).
//
// What has to be proven, and what every case below is one of:
//
//   - **every array is bound from a creation argument.** An array never
//     travels down a cord, so ".array.concat <left> <right>" resolves both
//     names once, on the control thread, and an `array <name>` message is
//     honoured only against the name already bound on that inlet —
//     ArrayReferenceNames' bounded compare, never a registry lookup on a
//     message path.
//   - **concat keeps everything** — the left array's elements followed by
//     the right's, repeats included, order preserved, where the set
//     operations thin — leaving as list text, never as a new named array,
//     and never writing to either store (Max: "the original array objects
//     are not modified").
//   - **join glues one array into one token** — the separator (second
//     creation argument, empty by default) between each pair — sent typed
//     the way the patcher spells it, and bounded by what a cord carries
//     rather than by the store's element rule: the result is a message, not
//     an element.
//   - **no two guards are ever held at once** (concat) — the left array is
//     snapshotted under its guard, the result built against the right under
//     that guard alone, so ".array.concat seq seq" answers the array
//     doubled.
//   - **a result that outruns what a cord carries is refused whole** —
//     .array.at's whole-reply rule, on both objects.
//   - **the ask crosses the control/audio boundary and nothing allocates.**
//     In-patcher delivery dispatches on T_DSP, so "the audio thread builds
//     the longer sequence" is the ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names an array uses names of its own — one case's contents
// must not be visible to the next.

#include <doctest/doctest.h>
#include <cstdint>
#include <memory>
#include <string>

#include "patcher/genericObjects/gArray.h"
#include "patcher/genericObjects/gArrayConcat.h"
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
using YSE::PATCHER::gArrayConcat;
using YSE::PATCHER::gArrayJoin;

namespace {

  // The two, for the cases that loop over the whole file.
  const char* const kCombineTypes[] = {
      YSE::OBJ::G_ARRAY_CONCAT,
      YSE::OBJ::G_ARRAY_JOIN,
  };

  // Two .arrays and one concat on their names, sharing one
  // patcherImplementation so the names actually bind ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // sinks are declared before the objects so they are torn down last, while
  // the outlets wired to them still exist (see sinks.hpp on why that
  // matters).
  struct ConcatRig {
    MultiSink out;
    BangSink empty;
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    gArray left;
    gArray right;
    gArrayConcat op;

    ConcatRig(const std::string& patcherName, const std::string& leftName,
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

  // The one-array rig for .array.join, with the separator as the second
  // creation argument (or absent — Max's empty default).
  struct JoinRig {
    MultiSink out;
    BangSink empty;
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    gArray array;
    gArrayJoin op;

    JoinRig(const std::string& patcherName, const std::string& name,
            const std::string& separator = "") {
      p.SetName(patcherName);
      array.SetParent(&p);
      array.SetParams(name);
      op.SetParent(&p);
      op.SetParams(separator.empty() ? name : name + " " + separator);
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

  TEST_CASE("array.concat/join: both registered, with their inlets and outlets (#793)") {
    YSE::patcher p;
    p.create(2);
    for (const char* type : kCombineTypes) {
      CAPTURE(type);
      YSE::pHandle* h = p.CreateObject(type);
      REQUIRE(h != nullptr);
      CHECK(std::string(h->Type()) == std::string(type));
      CHECK(h->GetInputs() == 2);
      CHECK(h->GetOutputs() == 2);
    }

    auto names = YSE::PATCHER::Register().AllNames();
    for (const char* type : kCombineTypes) {
      CAPTURE(type);
      bool found = false;
      for (const auto& name : names) {
        if (name == std::string(type)) found = true;
      }
      CHECK(found);
    }
  }

  TEST_CASE("array.concat/join: the trigger takes the ask, the reference inlet only lists (#793)") {
    // Inlet 0 is the ask — a bang, or the "array <name>" reference, which is
    // list text; a result is asked for, never addressed, so no int lands
    // anywhere. Inlet 1 acknowledges a reference only.
    for (const char* type : kCombineTypes) {
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

  // ─── .array.concat ──────────────────────────────────────────────────────────

  TEST_CASE("array.concat: the left array's elements, then the right's, everything kept (#793)") {
    ConcatRig rig("acj793a", "l793a", "r793a");
    rig.StoreLeft("append 10 20 10");
    rig.StoreRight("append 20 30");

    // Not a set operation: repeats survive, inside each array and across
    // them, in order — where union would thin this to "10 20 30".
    rig.Ask();
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "10 20 10 20 30");
    CHECK_FALSE(rig.empty.gotBang);
    CHECK(rig.op.Dropped() == 0);

    // Mixed types pass through as the atoms they are — 7 and 7. stay two
    // different elements, a symbol stays a symbol.
    ConcatRig mixed("acj793a2", "l793a2", "r793a2");
    mixed.StoreLeft("append 7 c4");
    mixed.StoreRight("append 7. c4");
    mixed.Ask();
    CHECK(mixed.out.gotList);
    CHECK(mixed.out.listValue == "7 c4 7. c4");
  }

  TEST_CASE("array.concat: an empty side contributes nothing, two bang empty (#793)") {
    // An empty left is the right alone.
    ConcatRig rig("acj793b", "l793b", "r793b");
    rig.StoreRight("append 60 64");
    rig.Ask();
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "60 64");

    // An empty right is the left alone — and a result of one leaves typed,
    // SendAtoms' rule.
    ConcatRig single("acj793b2", "l793b2", "r793b2");
    single.StoreLeft("append 60");
    single.Ask();
    CHECK(single.out.gotInt);
    CHECK(single.out.intValue == 60);
    CHECK_FALSE(single.out.gotList);

    // Two empty arrays bang the empty outlet — "no data" is a state a patch
    // must be able to route on, not an error.
    ConcatRig hollow("acj793b3", "l793b3", "r793b3");
    hollow.Ask();
    CHECK(hollow.empty.gotBang);
    CHECK_FALSE(hollow.out.gotList);
    CHECK(hollow.op.Dropped() == 0);
  }

  TEST_CASE("array.concat: both names binding one array answers it doubled (#793)") {
    // ".array.concat seq seq" — the case that proves no two guards are ever
    // held at once: the left is snapshotted under its guard, the result
    // built against the right under its own, so the same store's guard is
    // taken twice *sequentially* and the answer is the array twice over.
    ConcatRig rig("acj793c", "s793c", "s793c");
    rig.StoreLeft("append 60 64");
    rig.Ask();
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "60 64 60 64");
    CHECK(rig.op.Dropped() == 0);
  }

  TEST_CASE("array.concat: an operation never writes to either store (#793)") {
    // Max's contract, kept: "the original array objects are not modified".
    // The result is a list the object owns; .array itself is where a patch
    // writes it back.
    ConcatRig rig("acj793d", "l793d", "r793d");
    rig.StoreLeft("append 10 20");
    rig.StoreRight("append 30");
    rig.Ask();
    CHECK(rig.out.gotList);
    CHECK(rig.left.Count() == 2);
    CHECK(rig.left.ElementAt(0) == "10");
    CHECK(rig.left.ElementAt(1) == "20");
    CHECK(rig.right.Count() == 1);
    CHECK(rig.right.ElementAt(0) == "30");
  }

  TEST_CASE("array.concat: a result that outruns a cord is refused whole (#793)") {
    // 150 + 150 elements is a well-formed concatenation of 300 — more atoms
    // than a list carries (AtomList::MAX_ATOMS is 256), so the ask is
    // refused whole and counted: a partial concatenation would be
    // truncation by another name, .array.at's whole-reply rule.
    ConcatRig rig("acj793e", "l793e", "r793e");
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

  // ─── .array.join ────────────────────────────────────────────────────────────

  TEST_CASE("array.join: the elements glued into one token, separator between (#793)") {
    // The separator is the second creation argument; the joined text is one
    // token, and a symbol leaves as list text of one.
    JoinRig rig("acj793f", "a793f", "-");
    rig.Store("append c4 e4 g4");
    rig.Ask();
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "c4-e4-g4");
    CHECK_FALSE(rig.empty.gotBang);
    CHECK(rig.op.Dropped() == 0);

    // A multi-character separator is one token too.
    JoinRig wide("acj793f2", "a793f2", "::");
    wide.Store("append c4 e4");
    wide.Ask();
    CHECK(wide.out.gotList);
    CHECK(wide.out.listValue == "c4::e4");

    // A comma glues numbers into a symbol — "1,2,3" spells no number.
    JoinRig comma("acj793f3", "a793f3", ",");
    comma.Store("append 1 2 3");
    comma.Ask();
    CHECK(comma.out.gotList);
    CHECK(comma.out.listValue == "1,2,3");
  }

  TEST_CASE("array.join: the joined token leaves typed the way it spells (#793)") {
    // No separator (Max's empty default) butts the elements together — and
    // SendAtom's rule holds for the result: "1 2" joined by nothing spells
    // 12 and leaves as that int, the family's transport convention.
    JoinRig rig("acj793g", "a793g");
    rig.Store("append 1 2");
    rig.Ask();
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 12);
    CHECK_FALSE(rig.out.gotList);

    // A single-element array answers that element alone, separator unused
    // — typed, so 7.5 leaves as the float it is.
    JoinRig single("acj793g2", "a793g2", "-");
    single.Store("append 7.5");
    single.Ask();
    CHECK(single.out.gotFloat);
    CHECK(single.out.floatValue == doctest::Approx(7.5f));
    CHECK_FALSE(single.out.gotList);
  }

  TEST_CASE("array.join: an empty array bangs the empty outlet (#793)") {
    JoinRig rig("acj793h", "a793h", "-");
    rig.Ask();
    CHECK(rig.empty.gotBang);
    CHECK_FALSE(rig.out.gotList);
    CHECK(rig.op.Dropped() == 0);
  }

  TEST_CASE("array.join: a result that outruns a cord is refused whole (#793)") {
    // 21 elements of 64 characters join to 1344 — past JOINED_CAPACITY
    // (1280, the same ceiling every rendered list send has), so the ask is
    // refused whole and counted: a partial join would be truncation. The
    // result is a message down a cord, not an element — the store's
    // 64-character element rule has no claim on it, which is why 21 legal
    // elements can still outrun the cord.
    JoinRig rig("acj793i", "a793i");
    const std::string element(64, 'a');
    for (int i = 0; i < 21; i++)
      rig.Store("append " + element);
    CHECK(rig.array.Count() == 21);

    const std::uint64_t before = rig.op.Dropped();
    rig.Ask();
    CHECK(rig.op.Dropped() == before + 1);
    CHECK_FALSE(rig.out.gotList);
    CHECK_FALSE(rig.empty.gotBang);

    // 20 of them join to 1280 exactly — the ceiling itself still answers
    // whole.
    JoinRig fits("acj793i2", "a793i2");
    for (int i = 0; i < 20; i++)
      fits.Store("append " + element);
    fits.Ask();
    CHECK(fits.out.gotList);
    CHECK(fits.out.listValue.size() == 1280);
  }

  // ─── the references ─────────────────────────────────────────────────────────

  TEST_CASE(
      "array.concat/join: the left reference asks on the trigger, anything else refused (#793)") {
    // "array <name>" is the message an .array's reference outlet emits on a
    // bang; on the trigger inlet it asks when it names the bound (left)
    // array — the family gesture. Any other name there is a mis-wired cord,
    // refused, never resolved: a registry lookup is a mutex, and this may be
    // the audio thread.
    ConcatRig rig("acj793j", "l793j", "r793j");
    rig.StoreLeft("append 10");
    rig.StoreRight("append 20");

    const std::uint64_t before = rig.op.Dropped();
    rig.op.GetInlet(0)->SetList("array l793j", YSE::T_GUI);
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "10 20");
    CHECK(rig.op.Dropped() == before);

    rig.Reset();
    rig.op.GetInlet(0)->SetList("array r793j", YSE::T_GUI);
    rig.op.GetInlet(0)->SetList("array somewhere_else", YSE::T_GUI);
    rig.op.GetInlet(0)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotList);
    CHECK_FALSE(rig.empty.gotBang);
    CHECK(rig.op.Dropped() == before + 3);

    // And the same gesture on .array.join.
    JoinRig join("acj793j2", "a793j2", "-");
    join.Store("append 5 6");
    join.op.GetInlet(0)->SetList("array a793j2", YSE::T_GUI);
    CHECK(join.out.gotList);
    CHECK(join.out.listValue == "5-6");
    const std::uint64_t joinBefore = join.op.Dropped();
    join.op.GetInlet(0)->SetList("array somewhere_else", YSE::T_GUI);
    CHECK(join.op.Dropped() == joinBefore + 1);
  }

  TEST_CASE("array.concat/join: the reference inlet acknowledges its own side only (#793)") {
    // concat's inlet 1 answers to the *right* name — a patch may wire both
    // reference outlets across, as it would in Max — and to nothing else,
    // the left name included: each inlet is bound to one side.
    ConcatRig rig("acj793k", "l793k", "r793k");
    rig.StoreLeft("append 1");

    const std::uint64_t before = rig.op.Dropped();
    rig.op.GetInlet(1)->SetList("array r793k", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotList);
    CHECK_FALSE(rig.out.gotInt);
    CHECK(rig.op.Dropped() == before);

    rig.op.GetInlet(1)->SetList("array l793k", YSE::T_GUI);
    rig.op.GetInlet(1)->SetList("array somewhere_else", YSE::T_GUI);
    rig.op.GetInlet(1)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK(rig.op.Dropped() == before + 3);

    // join's inlet 1 answers to its one name.
    JoinRig join("acj793k2", "a793k2");
    const std::uint64_t joinBefore = join.op.Dropped();
    join.op.GetInlet(1)->SetList("array a793k2", YSE::T_GUI);
    CHECK(join.op.Dropped() == joinBefore);
    join.op.GetInlet(1)->SetList("array somewhere_else", YSE::T_GUI);
    CHECK(join.op.Dropped() == joinBefore + 1);
  }

  TEST_CASE("array.concat/join: an unnamed object reads private, empty arrays (#793)") {
    // Not "shares the empty name" — gArray's rule, inherited on every
    // binding. Private arrays hold nothing, so every ask bangs empty and
    // counts nothing.
    for (const char* type : kCombineTypes) {
      CAPTURE(type);
      MultiSink out;
      BangSink empty;
      YSE::pHandle outHandle(&out);
      YSE::pHandle emptyHandle(&empty);
      YSE::PATCHER::patcherImplementation p(2, nullptr);
      p.SetName("acj793l");
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

  TEST_CASE("array.concat/join: patcherImplementation::SetName re-anchors the bindings (#793)") {
    // The rename dispatch itself: an object created *inside* a patcher must
    // be re-anchored by the patcher, without anybody calling RefreshBinding
    // by hand — and for concat the dispatch must reach gArraySetOpBase's
    // RefreshBinding, or only the left side would move. The keepers hold the
    // old-address stores (they are not in the patcher's object map, so the
    // rename does not touch them): before the rename the concat answers from
    // both sides; after it both sides read fresh empty arrays under the new
    // prefix, so the ask bangs empty.
    MultiSink out;
    BangSink empty;
    YSE::pHandle outHandle(&out);
    YSE::pHandle emptyHandle(&empty);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("acj793m_before");

    gArray keepLeft;
    keepLeft.SetParent(&p);
    keepLeft.SetParams("l793m");
    keepLeft.GetInlet(0)->SetList("append 10", YSE::T_GUI);
    gArray keepRight;
    keepRight.SetParent(&p);
    keepRight.SetParams("r793m");
    keepRight.GetInlet(0)->SetList("append 20", YSE::T_GUI);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY_CONCAT, "l793m r793m");
    REQUIRE(h != nullptr);
    p.Connect(h, 0, &outHandle, 0);
    p.Connect(h, 1, &emptyHandle, 0);

    h->SetBang(0);
    CHECK(out.gotList);
    CHECK(out.listValue == "10 20");
    CHECK_FALSE(empty.gotBang);

    p.SetName("acj793m_after");
    out.reset();
    h->SetBang(0);
    CHECK_FALSE(out.gotInt);
    CHECK_FALSE(out.gotList);
    CHECK(empty.gotBang);

    // And join's one binding moves with the patcher too.
    MultiSink out2;
    BangSink empty2;
    YSE::pHandle out2Handle(&out2);
    YSE::pHandle empty2Handle(&empty2);
    YSE::PATCHER::patcherImplementation q(2, nullptr);
    q.SetName("acj793m2_before");
    gArray keeper;
    keeper.SetParent(&q);
    keeper.SetParams("a793m2");
    keeper.GetInlet(0)->SetList("append 5 6", YSE::T_GUI);
    YSE::pHandle* j = q.CreateObject(YSE::OBJ::G_ARRAY_JOIN, "a793m2 -");
    REQUIRE(j != nullptr);
    q.Connect(j, 0, &out2Handle, 0);
    q.Connect(j, 1, &empty2Handle, 0);
    j->SetBang(0);
    CHECK(out2.gotList);
    CHECK(out2.listValue == "5-6");

    q.SetName("acj793m2_after");
    out2.reset();
    j->SetBang(0);
    CHECK_FALSE(out2.gotList);
    CHECK(empty2.gotBang);
  }

  // ─── the family, chained through the public API ─────────────────────────────

  TEST_CASE(
      "array.concat: wired from the array's reference outlet, banging the array asks (#793)") {
    // The flow a patch actually wires, end to end through the public patcher
    // API: the left .array's reference outlet into the trigger inlet gives
    // the family gesture — bang the array, out comes the longer sequence —
    // and a write between two bangs changes what the next ask sees.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::patcher p;
    p.create(2);
    p.name("acj793n");
    YSE::pHandle* leftArray = p.CreateObject(YSE::OBJ::G_ARRAY, "l793n");
    YSE::pHandle* rightArray = p.CreateObject(YSE::OBJ::G_ARRAY, "r793n");
    YSE::pHandle* cat = p.CreateObject(YSE::OBJ::G_ARRAY_CONCAT, "l793n r793n");
    REQUIRE(leftArray != nullptr);
    REQUIRE(rightArray != nullptr);
    REQUIRE(cat != nullptr);
    p.Connect(leftArray, 1, cat, 0);
    p.Connect(cat, 0, &sinkHandle, 0);

    leftArray->SetListData(0, "append 60 64");
    rightArray->SetListData(0, "append 67");
    leftArray->SetBang(0);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "60 64 67");

    sink.reset();
    rightArray->SetListData(0, "append 71");
    leftArray->SetBang(0);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "60 64 67 71");
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("array.concat/join: an ask over in-patcher delivery lands on T_DSP (#793)") {
    // A .r feeding the trigger inlet dispatches on T_DSP when the block
    // drains it (issue #225) — "the audio thread builds the longer sequence"
    // is the ordinary case.
    MultiSink catSink;
    YSE::pHandle catSinkHandle(&catSink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("acj793o");

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go793o");
    YSE::pHandle* leftArray = p.CreateObject(YSE::OBJ::G_ARRAY, "l793o");
    YSE::pHandle* rightArray = p.CreateObject(YSE::OBJ::G_ARRAY, "r793o");
    YSE::pHandle* cat = p.CreateObject(YSE::OBJ::G_ARRAY_CONCAT, "l793o r793o");
    REQUIRE(recv != nullptr);
    REQUIRE(leftArray != nullptr);
    REQUIRE(rightArray != nullptr);
    REQUIRE(cat != nullptr);
    p.Connect(recv, 0, cat, 0);
    p.Connect(cat, 0, &catSinkHandle, 0);

    leftArray->SetListData(0, "append 10");
    rightArray->SetListData(0, "append 20 30");

    p.PassData(std::string("array l793o"), "go793o", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(catSink.gotList);
    CHECK(catSink.listValue == "10 20 30");

    // And the same boundary for join.
    MultiSink joinSink;
    YSE::pHandle joinSinkHandle(&joinSink);
    YSE::PATCHER::patcherImplementation q(2, nullptr);
    q.SetName("acj793o2");
    YSE::pHandle* recv2 = q.CreateObject(YSE::OBJ::G_RECEIVE, "go793o2");
    YSE::pHandle* arr = q.CreateObject(YSE::OBJ::G_ARRAY, "a793o2");
    YSE::pHandle* join = q.CreateObject(YSE::OBJ::G_ARRAY_JOIN, "a793o2 -");
    REQUIRE(recv2 != nullptr);
    REQUIRE(arr != nullptr);
    REQUIRE(join != nullptr);
    q.Connect(recv2, 0, join, 0);
    q.Connect(join, 0, &joinSinkHandle, 0);

    arr->SetListData(0, "append c4 e4");
    q.PassData(std::string("array a793o2"), "go793o2", YSE::T_GUI);
    q.Calculate(YSE::T_DSP);
    REQUIRE(joinSink.gotList);
    CHECK(joinSink.listValue == "c4-e4");
  }

  TEST_CASE("array.concat/join: no message path allocates (#793)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every path of both objects: concat's snapshot and collection, join's
    // glue, the typed and list-rendered sends, the empty outlet, and the
    // refusal and acknowledgement paths — all on T_DSP.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string leftReference = "array probeL793";
    const std::string rightReference = "array probeR793";
    const std::string joinReference = "array probeJ793";
    const std::string wrongName = "array somewhere_else_long";
    const std::string clearMessage = "clear";

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("acj793p");
    MultiSink catSink;
    BangSink catEmpty;
    MultiSink joinSink;
    BangSink joinEmpty;
    gArray left;
    gArray right;
    gArray third;
    gArrayConcat cat;
    gArrayJoin join;

    left.SetParent(&p);
    left.SetParams("probeL793");
    right.SetParent(&p);
    right.SetParams("probeR793");
    third.SetParent(&p);
    third.SetParams("probeJ793");
    cat.SetParams("probeL793 probeR793");
    cat.SetParent(&p);
    join.SetParams("probeJ793 -");
    join.SetParent(&p);
    Wire(cat, 0, catSink);
    Wire(cat, 1, catEmpty);
    Wire(join, 0, joinSink);
    Wire(join, 1, joinEmpty);

    // Mixed populations, so the results carry ints, a float and symbols —
    // the list-rendered send path and the symbol join both run — and the
    // third array is emptied inside the scope so the empty outlet runs on
    // T_DSP too.
    left.GetInlet(0)->SetList("append 10 c4 7.5", YSE::T_GUI);
    right.GetInlet(0)->SetList("append -3 20", YSE::T_GUI);
    third.GetInlet(0)->SetList("append 5 5 8", YSE::T_GUI);

    // Warm every path — including the sinks' assignments — so first-call
    // machinery is not what the probe catches.
    cat.GetInlet(0)->SetBang(YSE::T_GUI);
    cat.GetInlet(0)->SetList(leftReference, YSE::T_GUI);
    cat.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    cat.GetInlet(1)->SetList(rightReference, YSE::T_GUI);
    cat.GetInlet(1)->SetList(wrongName, YSE::T_GUI);
    join.GetInlet(0)->SetBang(YSE::T_GUI);
    join.GetInlet(0)->SetList(joinReference, YSE::T_GUI);
    join.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    join.GetInlet(1)->SetList(joinReference, YSE::T_GUI);
    const std::uint64_t catDroppedBefore = cat.Dropped();
    const std::uint64_t joinDroppedBefore = join.Dropped();

    catSink.reset();
    joinSink.reset();
    joinEmpty.gotBang = false;
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      cat.GetInlet(0)->SetBang(YSE::T_DSP);
      cat.GetInlet(0)->SetList(leftReference, YSE::T_DSP);
      cat.GetInlet(0)->SetList(wrongName, YSE::T_DSP);
      cat.GetInlet(1)->SetList(rightReference, YSE::T_DSP);
      cat.GetInlet(1)->SetList(wrongName, YSE::T_DSP);
      join.GetInlet(0)->SetBang(YSE::T_DSP);
      join.GetInlet(0)->SetList(joinReference, YSE::T_DSP);
      join.GetInlet(0)->SetList(wrongName, YSE::T_DSP);
      join.GetInlet(1)->SetList(joinReference, YSE::T_DSP);
      // The empty outlet on T_DSP too: clear the join's array through the
      // .array's own message path, then ask again.
      third.GetInlet(0)->SetList(clearMessage, YSE::T_DSP);
      join.GetInlet(0)->SetBang(YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing.
    CHECK(catSink.gotList);
    CHECK(catSink.listValue == "10 c4 7.5 -3 20");
    CHECK(joinSink.gotList);
    CHECK(joinSink.listValue == "5-5-8");
    CHECK(joinEmpty.gotBang);
    CHECK(cat.Dropped() == catDroppedBefore + 2);
    CHECK(join.Dropped() == joinDroppedBefore + 1);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("array.concat/join: params survive a DumpJSON / ParseJSON round trip (#793)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* c = src.CreateObject(YSE::OBJ::G_ARRAY_CONCAT, "notes793 other793");
    YSE::pHandle* j = src.CreateObject(YSE::OBJ::G_ARRAY_JOIN, "notes793 -");
    REQUIRE(c != nullptr);
    REQUIRE(j != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 2);

    for (const char* type : kCombineTypes) {
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
      const bool isJoin = std::string(type) == std::string(YSE::OBJ::G_ARRAY_JOIN);
      // The analyzer cannot see that a failed REQUIRE aborts the case
      // (doctest's failure path is a runtime jump), so it assumes `copy` may
      // be null here.
      // NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage)
      CHECK(copy->GetParams() == std::string(isJoin ? "notes793 -" : "notes793 other793"));
      CHECK(copy->GetInputs() == 2);
      CHECK(copy->GetOutputs() == 2);
    }

    // A join without a separator round-trips bare — the second argument is
    // genuinely optional, Max's empty default.
    YSE::patcher bare;
    bare.create(2);
    YSE::pHandle* b = bare.CreateObject(YSE::OBJ::G_ARRAY_JOIN, "bare793");
    REQUIRE(b != nullptr);
    YSE::patcher bareLoaded;
    bareLoaded.create(2);
    bareLoaded.ParseJSON(bare.DumpJSON());
    REQUIRE(bareLoaded.Objects() == 1);
    YSE::pHandle* bareCopy = bareLoaded.GetHandleFromList(0);
    REQUIRE(bareCopy != nullptr);
    CHECK(bareCopy->GetParams() == "bare793");
  }

  TEST_CASE("array.concat/join: both carry complete documentation metadata (#793)") {
    for (const char* type : kCombineTypes) {
      CAPTURE(type);
      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(type));
      REQUIRE(obj != nullptr);
      CHECK_FALSE(obj->GetDescription().empty());
      CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GENERIC);
      const auto& docs = obj->GetParamDocs();
      REQUIRE(docs.size() == 2u);
      const bool isJoin = std::string(type) == std::string(YSE::OBJ::G_ARRAY_JOIN);
      if (isJoin) {
        CHECK(docs[0].name == "name");
        CHECK(docs[1].name == "separator");
      } else {
        CHECK(docs[0].name == "left");
        CHECK(docs[1].name == "right");
      }
    }
  }

  TEST_CASE("array.concat/join: the bindings and the separator reset with the params (#793)") {
    // concat's two bound addresses are gArraySetOpBase's, visible; join's
    // separator resets with SetParams("") — a re-parse must not keep gluing
    // with whatever the previous second argument spelled.
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("acj793q");
    std::unique_ptr<YSE::PATCHER::pObject> obj(
        YSE::PATCHER::Register().Get(YSE::OBJ::G_ARRAY_CONCAT));
    REQUIRE(obj != nullptr);
    auto* cat = static_cast<YSE::PATCHER::gArraySetOpBase*>(obj.get());
    cat->SetParams("lft793 rgt793");
    CHECK(cat->Address().empty());
    CHECK(cat->RightAddress().empty());
    cat->SetParent(&p);
    CHECK(cat->ArrayName() == "lft793");
    CHECK(cat->RightName() == "rgt793");
    CHECK(cat->Address() == "acj793q.lft793");
    CHECK(cat->RightAddress() == "acj793q.rgt793");

    // SetParams("") is a real reset on both sides — back to two private
    // arrays.
    cat->SetParams("");
    CHECK(cat->ArrayName().empty());
    CHECK(cat->RightName().empty());
    CHECK(cat->Address().empty());
    CHECK(cat->RightAddress().empty());

    std::unique_ptr<YSE::PATCHER::pObject> jobj(
        YSE::PATCHER::Register().Get(YSE::OBJ::G_ARRAY_JOIN));
    REQUIRE(jobj != nullptr);
    auto* join = static_cast<gArrayJoin*>(jobj.get());
    join->SetParams("a793q ::");
    CHECK(join->ArrayName() == "a793q");
    CHECK(join->Separator() == "::");
    join->SetParams("a793q");
    CHECK(join->Separator().empty());
    join->SetParams("");
    CHECK(join->ArrayName().empty());
    CHECK(join->Separator().empty());
  }
}

// Tests for .array.slice / .array.subarray / .array.sub / .array.split
// (issue #791) — Max's array cutting objects on the name-addressed value
// model .array settled (#548).
//
// What has to be proven, and what every case below is one of:
//
//   - **the array is bound from the creation argument.** An array never
//     travels down a cord, so ".array.slice <name>" resolves the name once,
//     on the control thread, and an `array <name>` message is honoured only
//     when it names the array already bound — ArrayReferenceNames' bounded
//     compare, never a registry lookup on a message path.
//   - **a piece leaves as list text, never as a new named array** — #791's
//     decision, written down: creating an array from a message would mean
//     resolving a name on a message path.
//   - **the bounds are #791's, written down.** Zero-based, negative refused
//     (the family rule — Max's from-the-end indexing is the wrap the base
//     type refuses); past the end a bound means the end, the intersection
//     rule. slice is JS's exclusive end with no reverse and 0-extends-to-
//     the-end; subarray is the inclusive end with the reversed piece
//     permitted; sub is subarray under Max's second name — the reference
//     pages are verbatim twins, scramble/shuffle's arrangement.
//   - **an ask that selects nothing bangs the empty outlet** (the range
//     trio) — "no data" is a state a patch must route on; split's empty
//     half instead says nothing at all, which is what lets a recursive
//     head/tail patch terminate by absence.
//   - **a cut never writes to the store.** The whole piece is collected
//     under one hold of the store's guard — the mid-walk answer: there is
//     no walk to be in the middle of — and the array is read, only.
//   - **the ask crosses the control/audio boundary and nothing allocates.**
//     In-patcher delivery dispatches on T_DSP, so "the audio thread cuts a
//     piece" is the ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names an array uses names of its own — one case's contents
// must not be visible to the next.

#include <doctest/doctest.h>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "patcher/genericObjects/gArray.h"
#include "patcher/genericObjects/gArraySlice.h"
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
using TestHelpers::OrderSink;
using TestHelpers::Wire;
using YSE::PATCHER::gArray;
using YSE::PATCHER::gArraySlice;
using YSE::PATCHER::gArraySplit;
using YSE::PATCHER::gArraySub;
using YSE::PATCHER::gArraySubarray;

namespace {

  // The four, for the cases that loop over the whole file.
  const char* const kCutTypes[] = {
      YSE::OBJ::G_ARRAY_SLICE,
      YSE::OBJ::G_ARRAY_SUBARRAY,
      YSE::OBJ::G_ARRAY_SUB,
      YSE::OBJ::G_ARRAY_SPLIT,
  };

  // The two spellings of the inclusive form, for the cases that prove they
  // are one object.
  const char* const kInclusiveTypes[] = {
      YSE::OBJ::G_ARRAY_SUBARRAY,
      YSE::OBJ::G_ARRAY_SUB,
  };

  // An .array and one range reader on one name, sharing one
  // patcherImplementation so the name actually binds ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // sinks are declared before the objects so they are torn down last, while
  // the outlets wired to them still exist (see sinks.hpp on why that
  // matters).
  template <typename ObjectT> struct RangeRig {
    MultiSink piece;
    BangSink empty;
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    gArray array;
    ObjectT reader;

    RangeRig(const std::string& patcherName, const std::string& name,
             const std::string& bounds = "") {
      p.SetName(patcherName);
      array.SetParent(&p);
      array.SetParams(name);
      reader.SetParent(&p);
      reader.SetParams(bounds.empty() ? name : name + " " + bounds);
      Wire(reader, 0, piece);
      Wire(reader, 1, empty);
    }

    void Store(const std::string& message) {
      array.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void Ask() {
      reader.GetInlet(0)->SetBang(YSE::T_GUI);
    }
    void Reset() {
      piece.reset();
      empty.gotBang = false;
      empty.bangCount = 0;
    }
  };

  // The same rig for .array.split, whose two outlets are both pieces.
  struct SplitRig {
    MultiSink headSink;
    MultiSink tailSink;
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    gArray array;
    gArraySplit split;

    SplitRig(const std::string& patcherName, const std::string& name,
             const std::string& bounds = "") {
      p.SetName(patcherName);
      array.SetParent(&p);
      array.SetParams(name);
      split.SetParent(&p);
      split.SetParams(bounds.empty() ? name : name + " " + bounds);
      Wire(split, 0, headSink);
      Wire(split, 1, tailSink);
    }

    void Store(const std::string& message) {
      array.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void Ask() {
      split.GetInlet(0)->SetBang(YSE::T_GUI);
    }
    void Reset() {
      headSink.reset();
      tailSink.reset();
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("array.slice: all four registered, with their inlets and outlets (#791)") {
    YSE::patcher p;
    p.create(2);
    for (const char* type : kCutTypes) {
      CAPTURE(type);
      YSE::pHandle* h = p.CreateObject(type);
      REQUIRE(h != nullptr);
      CHECK(std::string(h->Type()) == std::string(type));
      const bool isSplit = std::string(type) == std::string(YSE::OBJ::G_ARRAY_SPLIT);
      CHECK(h->GetInputs() == (isSplit ? 3 : 4));
      CHECK(h->GetOutputs() == 2);
    }

    auto names = YSE::PATCHER::Register().AllNames();
    for (const char* type : kCutTypes) {
      CAPTURE(type);
      bool found = false;
      for (const auto& name : names) {
        if (name == std::string(type)) found = true;
      }
      CHECK(found);
    }
  }

  TEST_CASE("array.slice: the trigger takes the ask, the bounds take numbers (#791)") {
    // Inlet 0 is the ask — a bang, or the "array <name>" reference, which is
    // list text; a piece is asked for, never addressed, so no int lands
    // there. The bound inlets are the cold numeric half of the Max idiom,
    // and the last inlet acknowledges the reference only.
    for (const char* type : kCutTypes) {
      CAPTURE(type);
      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(type));
      REQUIRE(obj != nullptr);
      const int referenceInlet = obj->NumInputs() - 1;

      const unsigned int triggerIn = obj->GetInlet(0)->GetAcceptedTypes();
      CHECK((triggerIn & YSE::PATCHER::IT_BANG) != 0);
      CHECK((triggerIn & YSE::PATCHER::IT_LIST) != 0);
      CHECK((triggerIn & YSE::PATCHER::IT_INT) == 0);
      CHECK((triggerIn & YSE::PATCHER::IT_FLOAT) == 0);

      for (int i = 1; i < referenceInlet; i++) {
        CAPTURE(i);
        const unsigned int boundIn = obj->GetInlet(i)->GetAcceptedTypes();
        CHECK((boundIn & YSE::PATCHER::IT_INT) != 0);
        CHECK((boundIn & YSE::PATCHER::IT_FLOAT) != 0);
        CHECK((boundIn & YSE::PATCHER::IT_BANG) == 0);
        CHECK((boundIn & YSE::PATCHER::IT_LIST) == 0);
      }

      const unsigned int refIn = obj->GetInlet(referenceInlet)->GetAcceptedTypes();
      CHECK((refIn & YSE::PATCHER::IT_LIST) != 0);
      CHECK((refIn & YSE::PATCHER::IT_BANG) == 0);
      CHECK((refIn & YSE::PATCHER::IT_INT) == 0);
      CHECK((refIn & YSE::PATCHER::IT_FLOAT) == 0);
    }
  }

  // ─── .array.slice — the exclusive end ───────────────────────────────────────

  TEST_CASE("array.slice: [start, end) — JS's exclusive end (#791)") {
    RangeRig<gArraySlice> rig("asl791a", "a791a", "1 3");
    rig.Store("append 10 20 30 40 50");

    rig.Ask();
    CHECK(rig.piece.gotList);
    CHECK(rig.piece.listValue == "20 30");
    CHECK_FALSE(rig.empty.gotBang);

    // No bounds at all: the whole array — start 0, end open.
    RangeRig<gArraySlice> whole("asl791a2", "a791a2");
    whole.Store("append 10 20 30 40 50");
    whole.Ask();
    CHECK(whole.piece.gotList);
    CHECK(whole.piece.listValue == "10 20 30 40 50");

    // A single bound stretches to the end of the array — Max's "a single
    // value on its own implicitly stretches to the end".
    RangeRig<gArraySlice> tail("asl791a3", "a791a3", "2");
    tail.Store("append 10 20 30 40 50");
    tail.Ask();
    CHECK(tail.piece.gotList);
    CHECK(tail.piece.listValue == "30 40 50");

    // An end of 0 spells the same thing — Max's documented extra over the
    // JS form — and so does any bound past the array.
    RangeRig<gArraySlice> zero("asl791a4", "a791a4", "2 0");
    zero.Store("append 10 20 30 40 50");
    zero.Ask();
    CHECK(zero.piece.gotList);
    CHECK(zero.piece.listValue == "30 40 50");

    RangeRig<gArraySlice> past("asl791a5", "a791a5", "1 99");
    past.Store("append 10 20 30 40 50");
    past.Ask();
    CHECK(past.piece.gotList);
    CHECK(past.piece.listValue == "20 30 40 50");
  }

  TEST_CASE("array.slice: a piece of one leaves typed, and nothing bangs empty (#791)") {
    // One element leaves as the atom it spells — SendAtoms' rule, the
    // patcher's transport rule — and every way of selecting nothing is one
    // bang out the empty outlet: a crossed range (reverse slices are not
    // permitted here), a start past the last element, an empty array.
    RangeRig<gArraySlice> one("asl791b", "a791b", "1 2");
    one.Store("append 10 20 30");
    one.Ask();
    CHECK(one.piece.gotInt);
    CHECK(one.piece.intValue == 20);
    CHECK_FALSE(one.piece.gotList);

    RangeRig<gArraySlice> crossed("asl791b2", "a791b2", "3 1");
    crossed.Store("append 10 20 30 40 50");
    crossed.Ask();
    CHECK(crossed.empty.gotBang);
    CHECK_FALSE(crossed.piece.gotList);
    CHECK(crossed.reader.Dropped() == 0);

    RangeRig<gArraySlice> degenerate("asl791b3", "a791b3", "2 2");
    degenerate.Store("append 10 20 30 40 50");
    degenerate.Ask();
    CHECK(degenerate.empty.gotBang);
    CHECK_FALSE(degenerate.piece.gotList);

    RangeRig<gArraySlice> past("asl791b4", "a791b4", "9");
    past.Store("append 10 20 30");
    past.Ask();
    CHECK(past.empty.gotBang);
    CHECK_FALSE(past.piece.gotList);

    RangeRig<gArraySlice> hollow("asl791b5", "a791b5");
    hollow.Ask();
    CHECK(hollow.empty.gotBang);
    CHECK_FALSE(hollow.piece.gotList);
  }

  // ─── .array.subarray / .array.sub — the inclusive end, both spellings ──────

  TEST_CASE("array.subarray/sub: [start, end] inclusive, one object under two names (#791)") {
    // Max's array.sub reference page is array.subarray's verbatim — an
    // alias pair, scramble/shuffle's arrangement — so every assertion here
    // runs over both spellings.
    int variant = 0;
    for (const char* type : kInclusiveTypes) {
      CAPTURE(type);
      variant++;
      MultiSink piece;
      BangSink empty;
      YSE::pHandle pieceHandle(&piece);
      YSE::pHandle emptyHandle(&empty);
      YSE::PATCHER::patcherImplementation p(2, nullptr);
      p.SetName("asl791c_" + std::to_string(variant));

      gArray keeper;
      keeper.SetParent(&p);
      keeper.SetParams("a791c");
      keeper.GetInlet(0)->SetList("append 10 20 30 40 50", YSE::T_GUI);

      YSE::pHandle* h = p.CreateObject(type, "a791c 1 3");
      REQUIRE(h != nullptr);
      p.Connect(h, 0, &pieceHandle, 0);
      p.Connect(h, 1, &emptyHandle, 0);

      // Inclusive: the element at the end position is part of the piece.
      h->SetBang(0);
      CHECK(piece.gotList);
      CHECK(piece.listValue == "20 30 40");
      CHECK_FALSE(empty.gotBang);

      // A single bound stretches to the end.
      piece.reset();
      h->SetIntData(2, 99);
      h->SetBang(0);
      CHECK(piece.gotList);
      CHECK(piece.listValue == "20 30 40 50");

      // An end of 0 is a real position here — the difference from slice's
      // documented extra: [0, 0] inclusive is the first element, typed.
      piece.reset();
      h->SetIntData(1, 0);
      h->SetIntData(2, 0);
      h->SetBang(0);
      CHECK(piece.gotInt);
      CHECK(piece.intValue == 10);

      // The reversed piece Max permits: an end before the start walks from
      // start toward end.
      piece.reset();
      h->SetIntData(1, 3);
      h->SetIntData(2, 1);
      h->SetBang(0);
      CHECK(piece.gotList);
      CHECK(piece.listValue == "40 30 20");

      // A start past the last element begins at the last element when the
      // walk is reversed — the same intersection, walked the other way.
      piece.reset();
      h->SetIntData(1, 100);
      h->SetIntData(2, 2);
      h->SetBang(0);
      CHECK(piece.gotList);
      CHECK(piece.listValue == "50 40 30");

      // Ascending with the start past the last element selects nothing.
      piece.reset();
      empty.gotBang = false;
      h->SetIntData(1, 9);
      h->SetIntData(2, 99);
      h->SetBang(0);
      CHECK(empty.gotBang);
      CHECK_FALSE(piece.gotList);
    }
  }

  // ─── the bound inlets ───────────────────────────────────────────────────────

  TEST_CASE(
      "array.slice: the bound inlets store silently, refuse negatives, truncate floats (#791)") {
    RangeRig<gArraySlice> rig("asl791d", "a791d");
    rig.Store("append 10 20 30 40 50");

    // Ints store silently — the cold half of the Max idiom: nothing leaves
    // either outlet until the ask.
    rig.reader.GetInlet(1)->SetInt(1, YSE::T_GUI);
    rig.reader.GetInlet(2)->SetInt(3, YSE::T_GUI);
    CHECK(rig.reader.RangeStart() == 1);
    CHECK(rig.reader.RangeEnd() == 3);
    CHECK_FALSE(rig.piece.gotList);
    CHECK_FALSE(rig.empty.gotBang);

    rig.Ask();
    CHECK(rig.piece.gotList);
    CHECK(rig.piece.listValue == "20 30");

    // A float truncates to an int — Max's float method on an int attribute.
    rig.reader.GetInlet(1)->SetFloat(2.7f, YSE::T_GUI);
    CHECK(rig.reader.RangeStart() == 2);

    // Negative is refused and the stored bound does not move — the family's
    // indexing rule: Max's from-the-end indexing is the wrap the base type
    // refuses. A non-finite float is refused too, rather than quietly
    // becoming a bound of 0.
    const std::uint64_t before = rig.reader.Dropped();
    rig.reader.GetInlet(1)->SetInt(-1, YSE::T_GUI);
    rig.reader.GetInlet(2)->SetFloat(-3.2f, YSE::T_GUI);
    rig.reader.GetInlet(2)->SetFloat(std::numeric_limits<float>::infinity(), YSE::T_GUI);
    CHECK(rig.reader.Dropped() == before + 3);
    CHECK(rig.reader.RangeStart() == 2);
    CHECK(rig.reader.RangeEnd() == 3);
  }

  TEST_CASE("array.slice: a negative bound a creation argument planted refuses the ask (#791)") {
    // Only a creation argument can plant a negative bound — the inlets
    // refuse one before storing it — and the ask path refuses it there:
    // malformed, not a miss, so neither outlet fires.
    RangeRig<gArraySlice> rig("asl791e", "a791e", "-2 3");
    rig.Store("append 10 20 30");

    const std::uint64_t before = rig.reader.Dropped();
    rig.Ask();
    CHECK(rig.reader.Dropped() == before + 1);
    CHECK_FALSE(rig.piece.gotList);
    CHECK_FALSE(rig.empty.gotBang);
  }

  // ─── .array.split ───────────────────────────────────────────────────────────

  TEST_CASE("array.split: head before the position, tail from it, tail sent first (#791)") {
    // The element at the position goes to the tail — .zl slice's rule, so
    // the position reads as "how many elements the head takes" — and the
    // outlets fire right to left, which the shared log proves rather than
    // assumes.
    std::vector<char> log;
    OrderSink headSink;
    OrderSink tailSink;
    headSink.log = &log;
    headSink.tag = 'h';
    tailSink.log = &log;
    tailSink.tag = 't';

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("asl791f");
    gArray array;
    array.SetParent(&p);
    array.SetParams("a791f");
    gArraySplit split;
    split.SetParent(&p);
    split.SetParams("a791f 2");
    Wire(split, 0, headSink);
    Wire(split, 1, tailSink);

    array.GetInlet(0)->SetList("append 10 20 30 40 50", YSE::T_GUI);
    split.GetInlet(0)->SetBang(YSE::T_GUI);

    CHECK(headSink.lastKind == OrderSink::LIST);
    CHECK(headSink.lastList == "10 20");
    CHECK(tailSink.lastKind == OrderSink::LIST);
    CHECK(tailSink.lastList == "30 40 50");
    REQUIRE(log.size() == 2);
    CHECK(log[0] == 't');
    CHECK(log[1] == 'h');
  }

  TEST_CASE("array.split: the position is a boundary, and an empty half says nothing (#791)") {
    SplitRig rig("asl791g", "a791g");
    rig.Store("append 10 20 30 40 50");

    // 0 puts everything in the tail; the head says nothing at all —
    // SendAtoms' rule, and what lets a recursive patch terminate by absence.
    rig.Ask();
    CHECK_FALSE(rig.headSink.gotList);
    CHECK_FALSE(rig.headSink.gotInt);
    CHECK(rig.tailSink.gotList);
    CHECK(rig.tailSink.listValue == "10 20 30 40 50");

    // The length — or anything past it, the intersection rule — puts
    // everything in the head.
    rig.Reset();
    rig.split.GetInlet(1)->SetInt(5, YSE::T_GUI);
    rig.Ask();
    CHECK(rig.headSink.gotList);
    CHECK(rig.headSink.listValue == "10 20 30 40 50");
    CHECK_FALSE(rig.tailSink.gotList);
    CHECK_FALSE(rig.tailSink.gotInt);

    rig.Reset();
    rig.split.GetInlet(1)->SetInt(99, YSE::T_GUI);
    rig.Ask();
    CHECK(rig.headSink.gotList);
    CHECK(rig.headSink.listValue == "10 20 30 40 50");
    CHECK_FALSE(rig.tailSink.gotList);

    // A half of one leaves typed — the patcher's transport rule.
    rig.Reset();
    rig.split.GetInlet(1)->SetInt(4, YSE::T_GUI);
    rig.Ask();
    CHECK(rig.headSink.gotList);
    CHECK(rig.headSink.listValue == "10 20 30 40");
    CHECK(rig.tailSink.gotInt);
    CHECK(rig.tailSink.intValue == 50);

    // An empty array is two empty halves: nothing fires and nothing is
    // counted — silence is the answer, not an error.
    SplitRig hollow("asl791g2", "a791g2", "2");
    hollow.Ask();
    CHECK_FALSE(hollow.headSink.gotList);
    CHECK_FALSE(hollow.headSink.gotInt);
    CHECK_FALSE(hollow.tailSink.gotList);
    CHECK_FALSE(hollow.tailSink.gotInt);
    CHECK(hollow.split.Dropped() == 0);
  }

  TEST_CASE("array.split: the position inlet stores silently and refuses malformed values (#791)") {
    SplitRig rig("asl791h", "a791h", "1");
    rig.Store("append 10 20 30");

    CHECK(rig.split.Position() == 1);
    rig.split.GetInlet(1)->SetInt(2, YSE::T_GUI);
    CHECK(rig.split.Position() == 2);
    CHECK_FALSE(rig.headSink.gotList);
    CHECK_FALSE(rig.tailSink.gotList);

    // A float truncates; a negative or non-finite value is refused and the
    // stored position does not move.
    rig.split.GetInlet(1)->SetFloat(1.9f, YSE::T_GUI);
    CHECK(rig.split.Position() == 1);
    const std::uint64_t before = rig.split.Dropped();
    rig.split.GetInlet(1)->SetInt(-1, YSE::T_GUI);
    rig.split.GetInlet(1)->SetFloat(std::numeric_limits<float>::quiet_NaN(), YSE::T_GUI);
    CHECK(rig.split.Dropped() == before + 2);
    CHECK(rig.split.Position() == 1);

    rig.Ask();
    CHECK(rig.headSink.gotInt);
    CHECK(rig.headSink.intValue == 10);
    CHECK(rig.tailSink.gotList);
    CHECK(rig.tailSink.listValue == "20 30");
  }

  // ─── read-only ──────────────────────────────────────────────────────────────

  TEST_CASE("array.slice: a cut never writes to the store (#791)") {
    // The whole reason the reversed piece is collected in the object's own
    // list: a reader must not move the array under everything else reading
    // it — .array.reverse is the object that exists to do that.
    RangeRig<gArraySubarray> rig("asl791i", "a791i", "3 1");
    rig.Store("append 10 20 30 40 50");

    rig.Ask();
    CHECK(rig.piece.gotList);
    CHECK(rig.piece.listValue == "40 30 20");
    CHECK(rig.array.Count() == 5);
    CHECK(rig.array.ElementAt(0) == "10");
    CHECK(rig.array.ElementAt(2) == "30");
    CHECK(rig.array.ElementAt(4) == "50");

    SplitRig splitRig("asl791i2", "a791i2", "2");
    splitRig.Store("append 10 20 30 40 50");
    splitRig.Ask();
    CHECK(splitRig.headSink.gotList);
    CHECK(splitRig.array.Count() == 5);
    CHECK(splitRig.array.ElementAt(0) == "10");
    CHECK(splitRig.array.ElementAt(4) == "50");
  }

  // ─── the reference ──────────────────────────────────────────────────────────

  TEST_CASE("array.slice: the reference asks on the trigger inlet, anything else refused (#791)") {
    // "array <name>" is the message an .array's reference outlet emits on a
    // bang; on the trigger inlet it cuts — the family gesture. A reference
    // to an array this object is not bound to is refused, never resolved: a
    // registry lookup is a mutex, and this may be the audio thread.
    RangeRig<gArraySlice> rig("asl791j", "a791j", "1 3");
    rig.Store("append 10 20 30 40 50");

    const std::uint64_t before = rig.reader.Dropped();
    rig.reader.GetInlet(0)->SetList("array a791j", YSE::T_GUI);
    CHECK(rig.piece.gotList);
    CHECK(rig.piece.listValue == "20 30");
    CHECK(rig.reader.Dropped() == before);

    rig.Reset();
    rig.reader.GetInlet(0)->SetList("array somewhere_else", YSE::T_GUI);
    rig.reader.GetInlet(0)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK_FALSE(rig.piece.gotList);
    CHECK_FALSE(rig.empty.gotBang);
    CHECK(rig.reader.Dropped() == before + 2);

    // And the same gesture on .array.split.
    SplitRig splitRig("asl791j2", "a791j2", "1");
    splitRig.Store("append 10 20 30");
    splitRig.split.GetInlet(0)->SetList("array a791j2", YSE::T_GUI);
    CHECK(splitRig.headSink.gotInt);
    CHECK(splitRig.tailSink.gotList);
    const std::uint64_t splitBefore = splitRig.split.Dropped();
    splitRig.split.GetInlet(0)->SetList("array somewhere_else", YSE::T_GUI);
    CHECK(splitRig.split.Dropped() == splitBefore + 1);
  }

  TEST_CASE("array.slice: the reference inlet acknowledges its own array and nothing more (#791)") {
    RangeRig<gArraySlice> rig("asl791k", "a791k", "1 3");
    rig.Store("append 10 20 30 40 50");

    const std::uint64_t before = rig.reader.Dropped();
    rig.reader.GetInlet(3)->SetList("array a791k", YSE::T_GUI);
    CHECK_FALSE(rig.piece.gotList);
    CHECK(rig.reader.Dropped() == before);

    rig.reader.GetInlet(3)->SetList("array somewhere_else", YSE::T_GUI);
    rig.reader.GetInlet(3)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK(rig.reader.Dropped() == before + 2);

    SplitRig splitRig("asl791k2", "a791k2");
    const std::uint64_t splitBefore = splitRig.split.Dropped();
    splitRig.split.GetInlet(2)->SetList("array a791k2", YSE::T_GUI);
    CHECK(splitRig.split.Dropped() == splitBefore);
    splitRig.split.GetInlet(2)->SetList("array somewhere_else", YSE::T_GUI);
    CHECK(splitRig.split.Dropped() == splitBefore + 1);
  }

  TEST_CASE("array.slice: an unnamed object reads a private, empty array (#791)") {
    // Not "shares the empty name" — gArray's rule, inherited whole. A
    // private array covers no range, so the trio bangs empty and split says
    // nothing at all.
    MultiSink piece;
    BangSink empty;
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("asl791l");
    gArraySlice g;
    g.SetParent(&p);
    Wire(g, 0, piece);
    Wire(g, 1, empty);

    CHECK(g.Address().empty());
    g.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(empty.gotBang);
    CHECK_FALSE(piece.gotList);
    CHECK(g.Dropped() == 0);

    MultiSink headSink;
    MultiSink tailSink;
    YSE::PATCHER::patcherImplementation p2(2, nullptr);
    p2.SetName("asl791l2");
    gArraySplit s;
    s.SetParent(&p2);
    Wire(s, 0, headSink);
    Wire(s, 1, tailSink);
    s.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(headSink.gotList);
    CHECK_FALSE(tailSink.gotList);
    CHECK(s.Dropped() == 0);
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("array.slice: patcherImplementation::SetName re-anchors all four (#791)") {
    // The rename dispatch itself: an object created *inside* a patcher must
    // be re-anchored by the patcher, without anybody calling RefreshBinding
    // by hand. The keeper holds the old-address store (it is not in the
    // patcher's object map, so the rename does not touch it): before the
    // rename every ask cuts the keeper's elements; after it every ask reads
    // a fresh empty array under the new prefix — the trio bangs empty, and
    // split says nothing.
    MultiSink first;
    MultiSink second;
    YSE::pHandle firstHandle(&first);
    YSE::pHandle secondHandle(&second);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("asl791m_before");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a791m");
    keeper.GetInlet(0)->SetList("append 10 20 30 40 50", YSE::T_GUI);

    YSE::pHandle* handles[4] = {};
    for (int i = 0; i < 4; i++) {
      handles[i] = p.CreateObject(kCutTypes[i], "a791m 1 3");
      REQUIRE(handles[i] != nullptr);
      p.Connect(handles[i], 0, &firstHandle, 0);
      p.Connect(handles[i], 1, &secondHandle, 0);
    }

    for (int i = 0; i < 4; i++) {
      CAPTURE(kCutTypes[i]);
      first.reset();
      second.reset();
      handles[i]->SetBang(0);
      // Every one of the four answers with a piece out at least one outlet.
      CHECK((first.gotList || first.gotInt || second.gotList || second.gotInt));
      CHECK_FALSE(second.gotBang);
    }

    p.SetName("asl791m_after");
    for (int i = 0; i < 4; i++) {
      CAPTURE(kCutTypes[i]);
      first.reset();
      second.reset();
      handles[i]->SetBang(0);
      CHECK_FALSE(first.gotList);
      CHECK_FALSE(first.gotInt);
      CHECK_FALSE(second.gotList);
      CHECK_FALSE(second.gotInt);
      const bool isSplit = std::string(kCutTypes[i]) == std::string(YSE::OBJ::G_ARRAY_SPLIT);
      // The trio's second outlet is the empty bang; split's is the tail,
      // which stays silent on an empty array.
      CHECK(second.gotBang == !isSplit);
    }
  }

  // ─── the family, chained through the public API ─────────────────────────────

  TEST_CASE("array.slice: wired from the array's reference outlet, banging the array cuts (#791)") {
    // The flow a patch actually wires, end to end through the public patcher
    // API: the .array's reference outlet into the trigger inlet gives the
    // family gesture — bang the array, out comes the piece — and a write
    // between two bangs changes what the next ask sees.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::patcher p;
    p.create(2);
    p.name("asl791n");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "a791n");
    YSE::pHandle* sub = p.CreateObject(YSE::OBJ::G_ARRAY_SUB, "a791n 1 2");
    REQUIRE(array != nullptr);
    REQUIRE(sub != nullptr);
    p.Connect(array, 1, sub, 0);
    p.Connect(sub, 0, &sinkHandle, 0);

    array->SetListData(0, "append 60 64 67");
    array->SetBang(0);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "64 67");

    sink.reset();
    array->SetListData(0, "insert 1 62");
    array->SetBang(0);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "62 64");
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("array.slice: an ask over in-patcher delivery lands on T_DSP (#791)") {
    // A .r feeding the trigger inlet dispatches on T_DSP when the block
    // drains it (issue #225) — "the audio thread cuts a piece" is the
    // ordinary case, and the whole path is one guard hold, one bounded
    // collection and a send of list text.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("asl791o");

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go791o");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "a791o");
    YSE::pHandle* slice = p.CreateObject(YSE::OBJ::G_ARRAY_SLICE, "a791o 1 3");
    REQUIRE(recv != nullptr);
    REQUIRE(array != nullptr);
    REQUIRE(slice != nullptr);
    p.Connect(recv, 0, slice, 0);
    p.Connect(slice, 0, &sinkHandle, 0);

    array->SetListData(0, "append 10 20 30 40 50");

    p.PassData(std::string("array a791o"), "go791o", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "20 30");
  }

  TEST_CASE("array.slice: no message path allocates (#791)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every path of all four objects: the exclusive collection, the
    // inclusive one, the reversed walk, the split's two halves, the empty
    // outlet, the bound stores, and the refusal and acknowledgement paths.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string reference = "array probeASL791";
    const std::string wrongName = "array somewhere_else_long";

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("asl791p");
    MultiSink sliceSink;
    BangSink sliceEmpty;
    MultiSink subarraySink;
    MultiSink subSink;
    BangSink subEmpty;
    MultiSink headSink;
    MultiSink tailSink;
    gArray array;
    gArraySlice slice;
    gArraySubarray subarray;
    gArraySub sub;
    gArraySplit split;

    array.SetParent(&p);
    array.SetParams("probeASL791");
    slice.SetParams("probeASL791 1 3");
    slice.SetParent(&p);
    subarray.SetParams("probeASL791 3 1");
    subarray.SetParent(&p);
    sub.SetParams("probeASL791 4");
    sub.SetParent(&p);
    split.SetParams("probeASL791 2");
    split.SetParent(&p);
    Wire(slice, 0, sliceSink);
    Wire(slice, 1, sliceEmpty);
    Wire(subarray, 0, subarraySink);
    Wire(sub, 0, subSink);
    Wire(sub, 1, subEmpty);
    Wire(split, 0, headSink);
    Wire(split, 1, tailSink);

    // A mixed population, so the pieces carry ints, a float and a symbol —
    // the list-rendered send path and the typed single-atom one both run.
    array.GetInlet(0)->SetList("append 10 c4 7.5 -3 20", YSE::T_GUI);

    // Warm every path — including the sinks' assignments — so first-call
    // machinery is not what the probe catches.
    YSE::PATCHER::pObject* readers[4] = {&slice, &subarray, &sub, &split};
    for (auto* reader : readers) {
      reader->GetInlet(0)->SetBang(YSE::T_GUI);
      reader->GetInlet(0)->SetList(reference, YSE::T_GUI);
      reader->GetInlet(0)->SetList(wrongName, YSE::T_GUI);
      reader->GetInlet(1)->SetInt(1, YSE::T_GUI);
      reader->GetInlet(1)->SetFloat(1.f, YSE::T_GUI);
      reader->GetInlet(1)->SetInt(-1, YSE::T_GUI);
    }
    // Restore the warmed bounds to the creation values, and drive the empty
    // outlet once so its path is warm too.
    slice.GetInlet(1)->SetInt(1, YSE::T_GUI);
    slice.GetInlet(2)->SetInt(3, YSE::T_GUI);
    subarray.GetInlet(1)->SetInt(3, YSE::T_GUI);
    subarray.GetInlet(2)->SetInt(1, YSE::T_GUI);
    sub.GetInlet(1)->SetInt(9, YSE::T_GUI);
    sub.GetInlet(0)->SetBang(YSE::T_GUI);
    sub.GetInlet(1)->SetInt(4, YSE::T_GUI);
    split.GetInlet(1)->SetInt(2, YSE::T_GUI);
    const std::uint64_t sliceDroppedBefore = slice.Dropped();

    sliceSink.reset();
    subarraySink.reset();
    subSink.reset();
    headSink.reset();
    tailSink.reset();
    subEmpty.gotBang = false;
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      for (auto* reader : readers) {
        reader->GetInlet(0)->SetBang(YSE::T_DSP);
        reader->GetInlet(0)->SetList(reference, YSE::T_DSP);
      }
      slice.GetInlet(0)->SetList(wrongName, YSE::T_DSP);
      slice.GetInlet(3)->SetList(reference, YSE::T_DSP);
      slice.GetInlet(3)->SetList(wrongName, YSE::T_DSP);
      slice.GetInlet(1)->SetInt(1, YSE::T_DSP);
      slice.GetInlet(2)->SetFloat(3.f, YSE::T_DSP);
      slice.GetInlet(1)->SetInt(-1, YSE::T_DSP);
      // The empty outlet on T_DSP too: past-the-end selects nothing.
      sub.GetInlet(1)->SetInt(9, YSE::T_DSP);
      sub.GetInlet(0)->SetBang(YSE::T_DSP);
      sub.GetInlet(1)->SetInt(4, YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing.
    CHECK(sliceSink.gotList);
    CHECK(sliceSink.listValue == "c4 7.5");
    CHECK(subarraySink.gotList);
    CHECK(subarraySink.listValue == "-3 7.5 c4");
    CHECK(subSink.gotInt);
    CHECK(subSink.intValue == 20);
    CHECK(subEmpty.gotBang);
    CHECK(headSink.gotList);
    CHECK(headSink.listValue == "10 c4");
    CHECK(tailSink.gotList);
    CHECK(tailSink.listValue == "7.5 -3 20");
    CHECK(slice.Dropped() == sliceDroppedBefore + 3);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("array.slice: params survive a DumpJSON / ParseJSON round trip (#791)") {
    YSE::patcher src;
    src.create(2);
    for (const char* type : kCutTypes) {
      const bool isSplit = std::string(type) == std::string(YSE::OBJ::G_ARRAY_SPLIT);
      YSE::pHandle* h = src.CreateObject(type, isSplit ? "notes791 2" : "notes791 1 3");
      REQUIRE(h != nullptr);
    }
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 4);

    for (const char* type : kCutTypes) {
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
      const bool isSplit = std::string(type) == std::string(YSE::OBJ::G_ARRAY_SPLIT);
      // The analyzer cannot see that a failed REQUIRE aborts the case
      // (doctest's failure path is a runtime jump), so it assumes `copy` may
      // be null here.
      // NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage)
      CHECK(copy->GetParams() == std::string(isSplit ? "notes791 2" : "notes791 1 3"));
      CHECK(copy->GetInputs() == (isSplit ? 3 : 4));
      CHECK(copy->GetOutputs() == 2);
    }
  }

  TEST_CASE("array.slice: all four carry complete documentation metadata (#791)") {
    for (const char* type : kCutTypes) {
      CAPTURE(type);
      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(type));
      REQUIRE(obj != nullptr);
      CHECK_FALSE(obj->GetDescription().empty());
      CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GENERIC);
      const auto& docs = obj->GetParamDocs();
      const bool isSplit = std::string(type) == std::string(YSE::OBJ::G_ARRAY_SPLIT);
      REQUIRE(docs.size() == (isSplit ? 2u : 3u));
      CHECK(docs[0].name == "name");
      if (isSplit) {
        CHECK(docs[1].name == "position");
      } else {
        CHECK(docs[1].name == "start");
        CHECK(docs[2].name == "end");
      }
    }
  }
}

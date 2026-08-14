// Tests for the four permutation objects (issue #788) — Max's array.reverse,
// array.rotate, array.scramble and array.shuffle on the name-addressed value
// model .array settled (#548).
//
// What has to be proven, and what every case below is one of:
//
//   - **the array is bound from the creation argument.** An array never
//     travels down a cord, so ".array.reverse <name>" and its three siblings
//     resolve the name once, on the control thread, and an `array <name>`
//     message is honoured only when it names the array already bound —
//     ArrayReferenceNames' bounded compare, never a registry lookup on a
//     message path.
//   - **each object is an index order plus the shared apply.** Reverse's
//     order counts down, rotate's wraps a signed amount modulo the length —
//     the one place in the family where wrapping and negatives are legal —
//     and scramble/shuffle draw a Fisher-Yates order from a per-object,
//     seedable RandomSource, which is what makes a shuffle replayable.
//   - **scramble and shuffle are one body under Max's two names.** Their Max
//     reference pages are word-for-word identical, and the port keeps both
//     spellings over one implementation, so the two never drift apart.
//   - **the whole permutation is one guard hold, and nothing allocates.**
//     In-patcher delivery dispatches on T_DSP, so "the audio thread permutes
//     an array" is the ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names an array uses names of its own — one case's contents
// must not be visible to the next.

#include <doctest/doctest.h>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "patcher/genericObjects/gArray.h"
#include "patcher/genericObjects/gArrayPermute.h"
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
using YSE::PATCHER::gArrayReverse;
using YSE::PATCHER::gArrayRotate;
using YSE::PATCHER::gArrayScramble;
using YSE::PATCHER::gArrayShuffle;

namespace {

  // An .array and one permutation object on one name, sharing one
  // patcherImplementation so the name actually binds ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // sink is declared before the objects so it is torn down last, while the
  // outlet wired to it still exists (see sinks.hpp on why that matters).
  template <typename ObjectType> struct Rig {
    MultiSink reference; // outlet 0: the reference after a landed permutation
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    gArray array;
    ObjectType object;

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
    // The array's contents as one space-joined string — what the permutation
    // is asserted against. Control thread only (ElementAt allocates).
    std::string Contents() {
      std::string out;
      for (std::size_t i = 0; i < array.Count(); i++) {
        if (i != 0) out += ' ';
        out += array.ElementAt(i);
      }
      return out;
    }
    // The contents as a sorted list — what a shuffle must preserve exactly:
    // the same elements, only the order theirs to change.
    std::vector<std::string> SortedContents() {
      std::vector<std::string> out;
      out.reserve(array.Count());
      for (std::size_t i = 0; i < array.Count(); i++)
        out.push_back(array.ElementAt(i));
      std::sort(out.begin(), out.end());
      return out;
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("array.permute: all four registered, with their inlets and outlets (#788)") {
    YSE::patcher p;
    p.create(2);

    // .array.reverse — the remover twins' shape: trigger and reference.
    YSE::pHandle* reverse = p.CreateObject(YSE::OBJ::G_ARRAY_REVERSE);
    REQUIRE(reverse != nullptr);
    CHECK(std::string(reverse->Type()) == ".array.reverse");
    CHECK(reverse->GetInputs() == 2);
    CHECK(reverse->GetOutputs() == 1);

    // .array.rotate — .array.insert's arrangement: trigger, amount,
    // reference.
    YSE::pHandle* rotate = p.CreateObject(YSE::OBJ::G_ARRAY_ROTATE);
    REQUIRE(rotate != nullptr);
    CHECK(std::string(rotate->Type()) == ".array.rotate");
    CHECK(rotate->GetInputs() == 3);
    CHECK(rotate->GetOutputs() == 1);

    // The pair — trigger, seed, reference; the reference and the applied
    // order leave.
    YSE::pHandle* scramble = p.CreateObject(YSE::OBJ::G_ARRAY_SCRAMBLE);
    REQUIRE(scramble != nullptr);
    CHECK(std::string(scramble->Type()) == ".array.scramble");
    CHECK(scramble->GetInputs() == 3);
    CHECK(scramble->GetOutputs() == 2);

    YSE::pHandle* shuffle = p.CreateObject(YSE::OBJ::G_ARRAY_SHUFFLE);
    REQUIRE(shuffle != nullptr);
    CHECK(std::string(shuffle->Type()) == ".array.shuffle");
    CHECK(shuffle->GetInputs() == 3);
    CHECK(shuffle->GetOutputs() == 2);

    auto names = YSE::PATCHER::Register().AllNames();
    int found = 0;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_ARRAY_REVERSE) ||
          name == std::string(YSE::OBJ::G_ARRAY_ROTATE) ||
          name == std::string(YSE::OBJ::G_ARRAY_SCRAMBLE) ||
          name == std::string(YSE::OBJ::G_ARRAY_SHUFFLE))
        found++;
    }
    CHECK(found == 4);
  }

  TEST_CASE("array.permute: the triggers take what each ask needs, the cold inlets only their "
            "configuration (#788)") {
    // Reverse is asked for with a bang, never parameterised — no int or
    // float method anywhere, the remover twins' rule.
    gArrayReverse reverse;
    const unsigned int revTrigger = reverse.GetInlet(0)->GetAcceptedTypes();
    CHECK((revTrigger & YSE::PATCHER::IT_BANG) != 0);
    CHECK((revTrigger & YSE::PATCHER::IT_LIST) != 0);
    CHECK((revTrigger & YSE::PATCHER::IT_INT) == 0);
    CHECK((revTrigger & YSE::PATCHER::IT_FLOAT) == 0);
    const unsigned int revRef = reverse.GetInlet(1)->GetAcceptedTypes();
    CHECK((revRef & YSE::PATCHER::IT_LIST) != 0);
    CHECK((revRef & YSE::PATCHER::IT_BANG) == 0);

    // Rotate's trigger takes the inline amount too; the amount inlet is a
    // number and nothing else — .array.insert's index inlet.
    gArrayRotate rotate;
    const unsigned int rotTrigger = rotate.GetInlet(0)->GetAcceptedTypes();
    CHECK((rotTrigger & YSE::PATCHER::IT_BANG) != 0);
    CHECK((rotTrigger & YSE::PATCHER::IT_INT) != 0);
    CHECK((rotTrigger & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((rotTrigger & YSE::PATCHER::IT_LIST) != 0);
    const unsigned int rotAmount = rotate.GetInlet(1)->GetAcceptedTypes();
    CHECK((rotAmount & YSE::PATCHER::IT_INT) != 0);
    CHECK((rotAmount & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((rotAmount & YSE::PATCHER::IT_LIST) == 0);
    CHECK((rotAmount & YSE::PATCHER::IT_BANG) == 0);

    // The pair's trigger is reverse's; the seed inlet is rotate's amount
    // inlet with a seed where the amount was.
    gArrayScramble scramble;
    const unsigned int scrTrigger = scramble.GetInlet(0)->GetAcceptedTypes();
    CHECK((scrTrigger & YSE::PATCHER::IT_BANG) != 0);
    CHECK((scrTrigger & YSE::PATCHER::IT_LIST) != 0);
    CHECK((scrTrigger & YSE::PATCHER::IT_INT) == 0);
    const unsigned int scrSeed = scramble.GetInlet(1)->GetAcceptedTypes();
    CHECK((scrSeed & YSE::PATCHER::IT_INT) != 0);
    CHECK((scrSeed & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((scrSeed & YSE::PATCHER::IT_LIST) == 0);
    const unsigned int scrRef = scramble.GetInlet(2)->GetAcceptedTypes();
    CHECK((scrRef & YSE::PATCHER::IT_LIST) != 0);
    CHECK((scrRef & YSE::PATCHER::IT_BANG) == 0);
  }

  // ─── .array.reverse ─────────────────────────────────────────────────────────

  TEST_CASE("array.reverse: a bang reverses in place, and an empty array still announces "
            "(#788)") {
    Rig<gArrayReverse> rig("apv788a", "a788a");

    // Empty: the ask is well-formed and the answer is what it says — the
    // reversal of nothing lands, and the reference leaves.
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.array.Count() == 0);
    CHECK(rig.reference.gotList);
    CHECK(rig.reference.listValue == "array a788a");

    // Even length, then odd — the middle element of an odd reversal stays.
    rig.Store("append 10 20 c4 7.5");
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Contents() == "7.5 c4 20 10");

    rig.Store("clear");
    rig.Store("append 1 2 3");
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Contents() == "3 2 1");

    // Twice is the identity.
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Contents() == "1 2 3");
    CHECK(rig.object.Dropped() == 0);
  }

  TEST_CASE("array.reverse: the reference reverses on the trigger, acknowledges on its own "
            "inlet, and anything else is refused (#788)") {
    Rig<gArrayReverse> rig("apv788b", "a788b");
    rig.Store("append 10 20 30");

    rig.object.GetInlet(0)->SetList("array a788b", YSE::T_GUI);
    CHECK(rig.Contents() == "30 20 10");
    CHECK(rig.reference.gotList);
    CHECK(rig.object.Dropped() == 0);

    // A reference naming an array this object is not bound to is refused
    // everywhere, never resolved: a registry lookup is a mutex, and this may
    // be the audio thread.
    rig.reference.reset();
    rig.object.GetInlet(0)->SetList("array somewhere_else", YSE::T_GUI);
    CHECK(rig.Contents() == "30 20 10");
    CHECK_FALSE(rig.reference.gotList);
    CHECK(rig.object.Dropped() == 1);

    rig.object.GetInlet(0)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK(rig.object.Dropped() == 2);

    rig.object.GetInlet(1)->SetList("array a788b", YSE::T_GUI);
    CHECK(rig.object.Dropped() == 2);
    rig.object.GetInlet(1)->SetList("array somewhere_else", YSE::T_GUI);
    CHECK(rig.object.Dropped() == 3);
    CHECK_FALSE(rig.reference.gotList);
  }

  TEST_CASE("array.reverse: wired from the array's reference outlet, banging the array reverses "
            "(#788)") {
    // The flow a patch actually wires, end to end through the public patcher
    // API: the .array's reference outlet into the trigger gives the family
    // gesture — bang the array, out comes the reversed array's reference —
    // and the contents are read back through the array's own "getvalue", so
    // the whole loop runs on cords.
    MultiSink chained;
    MultiSink contents;
    YSE::pHandle chainedHandle(&chained);
    YSE::pHandle contentsHandle(&contents);
    YSE::patcher p;
    p.create(2);
    p.name("apv788c");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "a788c");
    YSE::pHandle* reverse = p.CreateObject(YSE::OBJ::G_ARRAY_REVERSE, "a788c");
    REQUIRE(array != nullptr);
    REQUIRE(reverse != nullptr);
    p.Connect(array, 1, reverse, 0);
    p.Connect(reverse, 0, &chainedHandle, 0);
    p.Connect(array, 0, &contentsHandle, 0);

    array->SetListData(0, "append 60 64 67");
    array->SetBang(0);
    REQUIRE(chained.gotList);
    CHECK(chained.listValue == "array a788c");

    array->SetListData(0, "getvalue");
    REQUIRE(contents.gotList);
    CHECK(contents.listValue == "67 64 60");
  }

  // ─── .array.rotate ──────────────────────────────────────────────────────────

  TEST_CASE("array.rotate: positive rotates toward the end, negative toward the start, any "
            "magnitude wraps (#788)") {
    // The one place in the family where wrapping and negatives are legal —
    // arrayStore's index rule names this object as the alternative it
    // refuses to be. Amounts arrive inline on the trigger here: applied at
    // the moment they arrive, storing nothing.
    Rig<gArrayRotate> rig("apv788d", "a788d");
    rig.Store("append 10 20 30 40");

    // Positive pushes toward the end; the element pushed past the last
    // position wraps to the front — Max's direction.
    rig.object.GetInlet(0)->SetInt(1, YSE::T_GUI);
    CHECK(rig.Contents() == "40 10 20 30");
    CHECK(rig.reference.gotList);
    CHECK(rig.reference.listValue == "array a788d");

    // Negative rotates back — the inverse, so the two cancel.
    rig.object.GetInlet(0)->SetInt(-1, YSE::T_GUI);
    CHECK(rig.Contents() == "10 20 30 40");

    // Modulo the length: six is two, a whole turn is the identity, and an
    // amount of 0 rotates by nothing — all of them land and announce.
    rig.object.GetInlet(0)->SetInt(6, YSE::T_GUI);
    CHECK(rig.Contents() == "30 40 10 20");
    rig.object.GetInlet(0)->SetInt(4, YSE::T_GUI);
    CHECK(rig.Contents() == "30 40 10 20");
    rig.reference.reset();
    rig.object.GetInlet(0)->SetInt(0, YSE::T_GUI);
    CHECK(rig.Contents() == "30 40 10 20");
    CHECK(rig.reference.gotList);

    // A float truncates to an int first — Max's float method — and a list
    // spelling exactly one signed int is the amount it spells; a NaN is
    // refused rather than quietly becoming 0, and so is a list of more.
    rig.object.GetInlet(0)->SetFloat(1.7f, YSE::T_GUI);
    CHECK(rig.Contents() == "20 30 40 10");
    rig.object.GetInlet(0)->SetList("-1", YSE::T_GUI);
    CHECK(rig.Contents() == "30 40 10 20");
    CHECK(rig.object.Dropped() == 0);
    rig.object.GetInlet(0)->SetList("1 2", YSE::T_GUI);
    CHECK(rig.object.Dropped() == 1);
    rig.object.GetInlet(0)->SetFloat(std::numeric_limits<float>::quiet_NaN(), YSE::T_GUI);
    CHECK(rig.object.Dropped() == 2);
    CHECK(rig.Contents() == "30 40 10 20");

    // An empty array rotates to itself and still announces — reverse's
    // empty rule, and no modulo of zero anywhere.
    rig.Store("clear");
    rig.reference.reset();
    rig.object.GetInlet(0)->SetInt(3, YSE::T_GUI);
    CHECK(rig.array.Count() == 0);
    CHECK(rig.reference.gotList);
  }

  TEST_CASE("array.rotate: a bang rotates by the stored amount — seeded by the argument, moved "
            "silently by the amount inlet, inline amounts stored nowhere (#788)") {
    // ".array.rotate <name> 1" starts with an amount, so a bang already
    // rotates. The amount inlet is the cold half of the Max idiom; negative
    // is legal there, the whole point of the object.
    Rig<gArrayRotate> rig("apv788e", "a788e", "a788e 1");
    rig.Store("append 10 20 30");
    CHECK(rig.object.Amount() == 1);

    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Contents() == "30 10 20");
    CHECK(rig.reference.gotList);

    // An inline amount is applied at the moment it arrives and stores
    // nothing — a bang after it rotates by the stored amount, not the
    // inline one. gArrayIndexMap's trigger rule.
    rig.object.GetInlet(0)->SetInt(2, YSE::T_GUI);
    CHECK(rig.Contents() == "10 20 30");
    CHECK(rig.object.Amount() == 1);

    // The amount inlet stores silently — nothing emitted, nothing rotated —
    // and a float truncates first.
    rig.reference.reset();
    rig.object.GetInlet(1)->SetInt(-1, YSE::T_GUI);
    CHECK_FALSE(rig.reference.gotList);
    CHECK(rig.Contents() == "10 20 30");
    CHECK(rig.object.Amount() == -1);
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Contents() == "20 30 10");

    rig.object.GetInlet(1)->SetFloat(2.9f, YSE::T_GUI);
    CHECK(rig.object.Amount() == 2);
    rig.object.GetInlet(1)->SetFloat(std::numeric_limits<float>::infinity(), YSE::T_GUI);
    CHECK(rig.object.Amount() == 2);
    CHECK(rig.object.Dropped() == 1);

    // The reference gesture rotates by the stored amount too — two forward
    // from "20 30 10" wraps 30 to the front — and a re-parse must not leave
    // half of the previous configuration standing: SetParams("") drops the
    // amount back to 0 along with the name.
    rig.object.GetInlet(0)->SetList("array a788e", YSE::T_GUI);
    CHECK(rig.Contents() == "30 10 20");
    rig.object.SetParams("");
    CHECK(rig.object.Amount() == 0);
  }

  // ─── the randomising pair ───────────────────────────────────────────────────

  TEST_CASE("array.scramble: a seeded shuffle is a permutation, reproducible, and one draw per "
            "element moved (#788)") {
    // Two objects on two arrays with the same contents and the same seed
    // must shuffle identically — the n-th draw after Seed(s) is a pure
    // function of s and n, nothing else — and what lands is the same
    // multiset the array held: a shuffle rearranges, never invents or
    // drops.
    Rig<gArrayScramble> one("apv788f1", "a788f1", "a788f1 42");
    Rig<gArrayScramble> two("apv788f2", "a788f2", "a788f2 42");
    one.Store("append 10 20 30 c4 7.5 60 e2 80");
    two.Store("append 10 20 30 c4 7.5 60 e2 80");
    const std::vector<std::string> before = one.SortedContents();

    one.object.GetInlet(0)->SetBang(YSE::T_GUI);
    two.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(one.array.Count() == 8);
    CHECK(one.Contents() == two.Contents());
    CHECK(one.SortedContents() == before);
    CHECK(one.reference.gotList);
    CHECK(one.reference.listValue == "array a788f1");

    // One draw per element moved — a bounded count, and the property that
    // makes a seeded sequence replayable at all.
    CHECK(one.object.Draws() == 7);

    // Reseeding with the same seed replays the same shuffle over the same
    // contents — the seed inlet restarts the sequence silently.
    const std::string first = one.Contents();
    one.Store("clear");
    one.Store("append 10 20 30 c4 7.5 60 e2 80");
    one.reference.reset();
    one.object.GetInlet(1)->SetInt(42, YSE::T_GUI);
    CHECK_FALSE(one.reference.gotList);
    CHECK(one.object.Draws() == 0);
    one.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(one.Contents() == first);
    CHECK(one.object.Dropped() == 0);
  }

  TEST_CASE("array.shuffle: Max's other name for the same object shuffles identically (#788)") {
    // The two reference pages are word-for-word identical and the port keeps
    // both spellings over one body — so the same seed over the same
    // contents gives the same order, whichever name the patch used.
    Rig<gArrayScramble> scramble("apv788g1", "a788g1", "a788g1 7");
    Rig<gArrayShuffle> shuffle("apv788g2", "a788g2", "a788g2 7");
    scramble.Store("append 1 2 3 4 5 6");
    shuffle.Store("append 1 2 3 4 5 6");

    scramble.object.GetInlet(0)->SetBang(YSE::T_GUI);
    shuffle.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(scramble.Contents() == shuffle.Contents());
    CHECK(shuffle.reference.gotList);
    CHECK(shuffle.reference.listValue == "array a788g2");
  }

  TEST_CASE("array.scramble: the order outlet publishes the applied picks, before the reference "
            "(#788)") {
    // Max's "scrambled index" list, zero-based because the family's
    // positions are arrayStore's — and sent right before left so a second
    // .array.indexmap's map is in place before the reference sets anything
    // running, .zl sort's idiom.
    std::vector<char> log;
    OrderSink orderOut;
    OrderSink referenceOut;
    orderOut.log = &log;
    orderOut.tag = 'O';
    referenceOut.log = &log;
    referenceOut.tag = 'R';

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("apv788h");
    gArray array;
    gArrayScramble scramble;
    array.SetParent(&p);
    array.SetParams("a788h");
    scramble.SetParent(&p);
    scramble.SetParams("a788h 42");
    Wire(scramble, 0, referenceOut);
    Wire(scramble, 1, orderOut);

    array.GetInlet(0)->SetList("append 10 20 30 40 50", YSE::T_GUI);
    scramble.GetInlet(0)->SetBang(YSE::T_GUI);

    REQUIRE(log.size() == 2);
    CHECK(log[0] == 'O');
    CHECK(log[1] == 'R');
    CHECK(referenceOut.lastKind == OrderSink::LIST);
    CHECK(referenceOut.lastList == "array a788h");

    // The order names every position exactly once, and applying it to a
    // copy of the original contents reproduces the shuffled array — which
    // is the property that makes it feed .array.indexmap.
    REQUIRE(orderOut.lastKind == OrderSink::LIST);
    const std::vector<std::string> original = {"10", "20", "30", "40", "50"};
    std::vector<std::size_t> picks;
    std::size_t cursor = 0;
    while (cursor < orderOut.lastList.size()) {
      while (cursor < orderOut.lastList.size() && orderOut.lastList[cursor] == ' ')
        cursor++;
      if (cursor >= orderOut.lastList.size()) break;
      picks.push_back(static_cast<std::size_t>(orderOut.lastList[cursor] - '0'));
      cursor++;
    }
    REQUIRE(picks.size() == 5);
    std::string expected;
    for (std::size_t i = 0; i < picks.size(); i++) {
      REQUIRE(picks[i] < original.size());
      if (i != 0) expected += ' ';
      expected += original[picks[i]];
    }
    std::string contents;
    for (std::size_t i = 0; i < array.Count(); i++) {
      if (i != 0) contents += ' ';
      contents += array.ElementAt(i);
    }
    CHECK(contents == expected);

    // An empty array publishes no order — there are no picks to speak of —
    // but the ask still lands and the reference still leaves.
    array.GetInlet(0)->SetList("clear", YSE::T_GUI);
    log.clear();
    scramble.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(log.size() == 1);
    CHECK(log[0] == 'R');
  }

  TEST_CASE("array.scramble: the order chains into .array.indexmap, putting a parallel array "
            "into the same new order (#788)") {
    // The idiom the order outlet exists for, run end to end through the
    // public patcher API: shuffle one array, feed the published order to an
    // .array.indexmap bound to a parallel array, and the two land in the
    // same arrangement — .zl sort's parallel-list gesture on the value
    // model.
    MultiSink notesOut;
    MultiSink namesOut;
    YSE::pHandle notesHandle(&notesOut);
    YSE::pHandle namesHandle(&namesOut);
    YSE::patcher p;
    p.create(2);
    p.name("apv788i");
    YSE::pHandle* notes = p.CreateObject(YSE::OBJ::G_ARRAY, "notes788i");
    YSE::pHandle* names = p.CreateObject(YSE::OBJ::G_ARRAY, "names788i");
    YSE::pHandle* scramble = p.CreateObject(YSE::OBJ::G_ARRAY_SCRAMBLE, "notes788i 42");
    YSE::pHandle* map = p.CreateObject(YSE::OBJ::G_ARRAY_INDEXMAP, "names788i");
    REQUIRE(notes != nullptr);
    REQUIRE(names != nullptr);
    REQUIRE(scramble != nullptr);
    REQUIRE(map != nullptr);
    // The order into the map inlet — configuration before the ask, which the
    // right-before-left send order guarantees even when both come from the
    // same trigger.
    p.Connect(scramble, 1, map, 1);
    p.Connect(notes, 0, &notesHandle, 0);
    p.Connect(names, 0, &namesHandle, 0);

    notes->SetListData(0, "append 60 62 64 65 67");
    names->SetListData(0, "append c d e f g");

    scramble->SetBang(0);
    map->SetBang(0);

    notes->SetListData(0, "getvalue");
    names->SetListData(0, "getvalue");
    REQUIRE(notesOut.gotList);
    REQUIRE(namesOut.gotList);

    // Parallel by construction: wherever 60 landed, c landed; the pitch
    // list read back maps element for element onto the name list.
    const std::vector<std::string> pitches = {"60", "62", "64", "65", "67"};
    const std::vector<std::string> letters = {"c", "d", "e", "f", "g"};
    std::string expected;
    std::size_t cursor = 0;
    while (cursor < notesOut.listValue.size()) {
      std::size_t start = cursor;
      while (cursor < notesOut.listValue.size() && notesOut.listValue[cursor] != ' ')
        cursor++;
      const std::string pitch = notesOut.listValue.substr(start, cursor - start);
      for (std::size_t i = 0; i < pitches.size(); i++) {
        if (pitches[i] == pitch) {
          if (!expected.empty()) expected += ' ';
          expected += letters[i];
        }
      }
      while (cursor < notesOut.listValue.size() && notesOut.listValue[cursor] == ' ')
        cursor++;
    }
    CHECK(namesOut.listValue == expected);
  }

  TEST_CASE("array.scramble: the reference shuffles on the trigger, acknowledges on its own "
            "inlet, and anything else is refused (#788)") {
    Rig<gArrayScramble> rig("apv788j", "a788j", "a788j 42");
    rig.Store("append 1 2 3 4 5");
    const std::vector<std::string> before = rig.SortedContents();

    rig.object.GetInlet(0)->SetList("array a788j", YSE::T_GUI);
    CHECK(rig.reference.gotList);
    CHECK(rig.SortedContents() == before);
    CHECK(rig.object.Dropped() == 0);

    rig.reference.reset();
    rig.object.GetInlet(0)->SetList("array somewhere_else", YSE::T_GUI);
    CHECK_FALSE(rig.reference.gotList);
    CHECK(rig.object.Dropped() == 1);
    rig.object.GetInlet(0)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK(rig.object.Dropped() == 2);

    rig.object.GetInlet(2)->SetList("array a788j", YSE::T_GUI);
    CHECK(rig.object.Dropped() == 2);
    rig.object.GetInlet(2)->SetList("array somewhere_else", YSE::T_GUI);
    CHECK(rig.object.Dropped() == 3);

    // A non-finite seed is refused rather than quietly becoming seed 0.
    rig.object.GetInlet(1)->SetFloat(std::numeric_limits<float>::quiet_NaN(), YSE::T_GUI);
    CHECK(rig.object.Dropped() == 4);
  }

  TEST_CASE("array.permute: unnamed objects permute a private array, silently (#788)") {
    // No name, no shared store, no reference to pass on — the ask lands
    // (over an empty private array), the announcement is simply empty, and
    // nothing is a refusal: the ask was well-formed.
    MultiSink reverseSink;
    MultiSink scrambleSink;
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("apv788k");
    gArrayReverse reverse;
    gArrayScramble scramble;
    reverse.SetParent(&p);
    scramble.SetParent(&p);
    Wire(reverse, 0, reverseSink);
    Wire(scramble, 0, scrambleSink);
    CHECK(reverse.Address().empty());
    CHECK(scramble.Address().empty());

    reverse.GetInlet(0)->SetBang(YSE::T_GUI);
    scramble.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(reverseSink.gotList);
    CHECK_FALSE(scrambleSink.gotList);
    CHECK(reverse.Dropped() == 0);
    CHECK(scramble.Dropped() == 0);
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("array.rotate: patcherImplementation::SetName re-anchors the shared base (#788)") {
    // The rename dispatch itself: an object created *inside* a patcher must
    // be re-anchored by the patcher, without anybody calling RefreshBinding
    // by hand. The keeper holds the old-address store; after the rename the
    // object acts on a fresh empty array under the new prefix, so the
    // keeper's contents stop moving — while the stored amount survives,
    // being the object's own state rather than anything derived from the
    // name.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("apv788l_before");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a788l");
    keeper.GetInlet(0)->SetList("append 10 20 30", YSE::T_GUI);

    YSE::pHandle* rotate = p.CreateObject(YSE::OBJ::G_ARRAY_ROTATE, "a788l 1");
    REQUIRE(rotate != nullptr);
    p.Connect(rotate, 0, &sinkHandle, 0);

    rotate->SetBang(0);
    REQUIRE(sink.gotList);
    CHECK(keeper.Count() == 3);
    CHECK(keeper.ElementAt(0) == "30");

    p.SetName("apv788l_after");
    sink.reset();
    rotate->SetBang(0);
    // The rotation landed — on the new, empty array — and the keeper's
    // contents were not touched.
    CHECK(sink.gotList);
    CHECK(keeper.Count() == 3);
    CHECK(keeper.ElementAt(0) == "30");
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("array.rotate: an amount arriving over in-patcher delivery lands on T_DSP (#788)") {
    // A .r feeding the trigger dispatches on T_DSP when the block drains it
    // (issue #225) — "the audio thread permutes an array" is the ordinary
    // case, and the whole path is one bounded parse, one guard hold and a
    // send of a string the object already owns.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("apv788m");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a788m");
    keeper.GetInlet(0)->SetList("append 10 20 30", YSE::T_GUI);

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go788m");
    YSE::pHandle* rotate = p.CreateObject(YSE::OBJ::G_ARRAY_ROTATE, "a788m");
    REQUIRE(recv != nullptr);
    REQUIRE(rotate != nullptr);
    p.Connect(recv, 0, rotate, 0);
    p.Connect(rotate, 0, &sinkHandle, 0);

    p.PassData(std::string("1"), "go788m", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "array a788m");
    CHECK(keeper.Count() == 3);
    CHECK(keeper.ElementAt(0) == "30");
    CHECK(keeper.ElementAt(1) == "10");
  }

  TEST_CASE("array.permute: no message path allocates (#788)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path of all four objects — the bangs, the inline and
    // stored amounts, the reseed, the reference gestures, the refusals, and
    // the reference-inlet acknowledgements — on T_DSP, in-patcher
    // delivery's thread.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string reference = "array probeAPV788";
    const std::string wrongName = "array somewhere_else_long";
    const std::string inlineAmount = "2";
    const std::string malformed = "1 2";

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("apv788n");
    MultiSink reverseSink;
    MultiSink rotateSink;
    MultiSink scrambleSink;
    MultiSink orderSink;
    gArray array;
    gArrayReverse reverse;
    gArrayRotate rotate;
    gArrayScramble scramble;
    array.SetParent(&p);
    array.SetParams("probeAPV788");
    reverse.SetParent(&p);
    reverse.SetParams("probeAPV788");
    rotate.SetParent(&p);
    rotate.SetParams("probeAPV788 1");
    scramble.SetParent(&p);
    scramble.SetParams("probeAPV788 42");
    Wire(reverse, 0, reverseSink);
    Wire(rotate, 0, rotateSink);
    Wire(scramble, 0, scrambleSink);
    Wire(scramble, 1, orderSink);

    array.GetInlet(0)->SetList("append 10 20 30 40 50", YSE::T_GUI);

    // Warm every path — including the sinks' assignments — so first-call
    // machinery is not what the probe catches.
    reverse.GetInlet(0)->SetBang(YSE::T_GUI);
    reverse.GetInlet(0)->SetList(reference, YSE::T_GUI);
    reverse.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    reverse.GetInlet(1)->SetList(reference, YSE::T_GUI);
    rotate.GetInlet(0)->SetBang(YSE::T_GUI);
    rotate.GetInlet(0)->SetInt(2, YSE::T_GUI);
    rotate.GetInlet(0)->SetFloat(1.5f, YSE::T_GUI);
    rotate.GetInlet(0)->SetList(inlineAmount, YSE::T_GUI);
    rotate.GetInlet(0)->SetList(reference, YSE::T_GUI);
    rotate.GetInlet(0)->SetList(malformed, YSE::T_GUI);
    rotate.GetInlet(1)->SetInt(-2, YSE::T_GUI);
    rotate.GetInlet(1)->SetFloat(3.5f, YSE::T_GUI);
    rotate.GetInlet(2)->SetList(reference, YSE::T_GUI);
    scramble.GetInlet(0)->SetBang(YSE::T_GUI);
    scramble.GetInlet(0)->SetList(reference, YSE::T_GUI);
    scramble.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    scramble.GetInlet(1)->SetInt(42, YSE::T_GUI);
    scramble.GetInlet(1)->SetFloat(7.f, YSE::T_GUI);
    scramble.GetInlet(2)->SetList(reference, YSE::T_GUI);

    // Reset the contents, then run the same sequence on T_DSP under the
    // probe.
    array.GetInlet(0)->SetList("clear", YSE::T_GUI);
    array.GetInlet(0)->SetList("append 10 20 30 40 50", YSE::T_GUI);
    const std::uint64_t reverseDrops = reverse.Dropped();
    const std::uint64_t rotateDrops = rotate.Dropped();
    const std::uint64_t scrambleDrops = scramble.Dropped();
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      reverse.GetInlet(0)->SetBang(YSE::T_DSP);
      reverse.GetInlet(0)->SetList(reference, YSE::T_DSP);
      reverse.GetInlet(0)->SetList(wrongName, YSE::T_DSP); // refused
      reverse.GetInlet(1)->SetList(reference, YSE::T_DSP); // acknowledged
      rotate.GetInlet(0)->SetBang(YSE::T_DSP);
      rotate.GetInlet(0)->SetInt(2, YSE::T_DSP);
      rotate.GetInlet(0)->SetFloat(1.5f, YSE::T_DSP);
      rotate.GetInlet(0)->SetList(inlineAmount, YSE::T_DSP);
      rotate.GetInlet(0)->SetList(reference, YSE::T_DSP);
      rotate.GetInlet(0)->SetList(malformed, YSE::T_DSP); // refused
      rotate.GetInlet(1)->SetInt(-2, YSE::T_DSP); // stored, silent
      rotate.GetInlet(1)->SetFloat(3.5f, YSE::T_DSP); // stored, silent
      rotate.GetInlet(2)->SetList(reference, YSE::T_DSP); // acknowledged
      scramble.GetInlet(0)->SetBang(YSE::T_DSP);
      scramble.GetInlet(0)->SetList(reference, YSE::T_DSP);
      scramble.GetInlet(0)->SetList(wrongName, YSE::T_DSP); // refused
      scramble.GetInlet(1)->SetInt(42, YSE::T_DSP); // reseed, silent
      scramble.GetInlet(1)->SetFloat(7.f, YSE::T_DSP); // reseed, silent
      scramble.GetInlet(2)->SetList(reference, YSE::T_DSP); // acknowledged
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing. The permutations landed (the same five
    // elements, rearranged), the stored amount is the float's truncation,
    // and exactly the three refusals were counted.
    CHECK(array.Count() == 5);
    CHECK(reverseSink.gotList);
    CHECK(rotateSink.gotList);
    CHECK(scrambleSink.gotList);
    CHECK(orderSink.gotList);
    CHECK(rotate.Amount() == 3);
    CHECK(reverse.Dropped() == reverseDrops + 1);
    CHECK(rotate.Dropped() == rotateDrops + 1);
    CHECK(scramble.Dropped() == scrambleDrops + 1);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("array.permute: params survive a DumpJSON / ParseJSON round trip (#788)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* rotate = src.CreateObject(YSE::OBJ::G_ARRAY_ROTATE, "notes788o -3");
    YSE::pHandle* shuffle = src.CreateObject(YSE::OBJ::G_ARRAY_SHUFFLE, "notes788o 42");
    REQUIRE(rotate != nullptr);
    REQUIRE(shuffle != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 2);

    // Found by type rather than by list position: the loaded patcher's
    // enumeration order is not the creation order.
    YSE::pHandle* rotateCopy = nullptr;
    YSE::pHandle* shuffleCopy = nullptr;
    for (unsigned int i = 0; i < loaded.Objects(); i++) {
      YSE::pHandle* handle = loaded.GetHandleFromList(static_cast<int>(i));
      REQUIRE(handle != nullptr);
      if (std::string(handle->Type()) == ".array.rotate") rotateCopy = handle;
      if (std::string(handle->Type()) == ".array.shuffle") shuffleCopy = handle;
    }

    REQUIRE(rotateCopy != nullptr);
    CHECK(rotateCopy->GetParams() == std::string("notes788o -3"));
    CHECK(rotateCopy->GetInputs() == 3);
    CHECK(rotateCopy->GetOutputs() == 1);

    REQUIRE(shuffleCopy != nullptr);
    CHECK(shuffleCopy->GetParams() == std::string("notes788o 42"));
    CHECK(shuffleCopy->GetInputs() == 3);
    CHECK(shuffleCopy->GetOutputs() == 2);
  }

  TEST_CASE("array.permute: all four carry complete documentation metadata (#788)") {
    gArrayReverse reverse;
    CHECK_FALSE(reverse.GetDescription().empty());
    CHECK(reverse.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    REQUIRE(reverse.GetParamDocs().size() == 1);
    CHECK(reverse.GetParamDocs()[0].name == "name");

    gArrayRotate rotate;
    CHECK_FALSE(rotate.GetDescription().empty());
    CHECK(rotate.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    REQUIRE(rotate.GetParamDocs().size() == 2);
    CHECK(rotate.GetParamDocs()[0].name == "name");
    CHECK(rotate.GetParamDocs()[1].name == "amount");

    gArrayScramble scramble;
    CHECK_FALSE(scramble.GetDescription().empty());
    CHECK(scramble.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    REQUIRE(scramble.GetParamDocs().size() == 2);
    CHECK(scramble.GetParamDocs()[0].name == "name");
    CHECK(scramble.GetParamDocs()[1].name == "seed");

    gArrayShuffle shuffle;
    CHECK_FALSE(shuffle.GetDescription().empty());
    CHECK(shuffle.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    REQUIRE(shuffle.GetParamDocs().size() == 2);
    CHECK(shuffle.GetParamDocs()[0].name == "name");
    CHECK(shuffle.GetParamDocs()[1].name == "seed");
  }
}

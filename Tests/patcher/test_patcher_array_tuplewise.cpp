// Tests for .array.tuplewise (issue #808) — two arrays combined element by
// element on the name-addressed value model .array settled (#548).
//
// What has to be proven, and what every case below is one of:
//
//   - **both arrays are bound from the creation arguments.** An array never
//     travels down a cord, so ".array.tuplewise <left> <right> <expression>"
//     resolves both names once, on the control thread, and an `array <name>`
//     message is honoured only against the name already bound on that inlet
//     — ArrayReferenceNames' bounded compare, never a registry lookup on a
//     message path.
//   - **the operation is the expression family's spelling** (#799, the
//     issue's blocker, resolved): the trailing creation arguments compiled
//     once by .expr's own compiler — $1 the left element, $2 the right, $3
//     the position, a placeholder past $3 rejected at parse time. A
//     malformed expression fails loudly at parse time and the object then
//     refuses every trigger, counted; an absent one refuses silently.
//   - **the pairing is .vexpr's**: the result is as long as the shorter
//     array, and the expression runs for a pair when both elements read as
//     numbers — the left element passes through unchanged for a pair the
//     expression cannot see, so every position of the result answers a
//     position of the left array.
//   - **the result leaves as list text, never as a new named array**, an
//     empty result bangs the empty outlet, and a result that outruns what a
//     cord carries is refused whole — the base's whole-reply rule.
//   - **no two guards are ever held at once** — the left array is
//     snapshotted under its guard, the result built against the right under
//     that guard alone, which is what makes ".array.tuplewise seq seq ..."
//     answer instead of tripping over its own try-lock.
//   - **the ask crosses the control/audio boundary and nothing allocates.**
//     In-patcher delivery dispatches on T_DSP, so "the audio thread adds two
//     arrays" is the ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names an array uses names of its own — one case's contents
// must not be visible to the next.

#include <doctest/doctest.h>
#include <cstdint>
#include <memory>
#include <string>

#include "patcher/genericObjects/gArray.h"
#include "patcher/genericObjects/gArrayTuplewise.h"
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
using YSE::PATCHER::gArrayTuplewise;

namespace {

  // Two .arrays and one .array.tuplewise on their names, sharing one
  // patcherImplementation so the names actually bind ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // sinks are declared before the objects so they are torn down last, while
  // the outlets wired to them still exist (see sinks.hpp on why that
  // matters).
  struct Rig {
    MultiSink out;
    BangSink empty;
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    gArray left;
    gArray right;
    gArrayTuplewise op;

    Rig(const std::string& patcherName, const std::string& leftName, const std::string& rightName,
        const std::string& expression) {
      p.SetName(patcherName);
      left.SetParent(&p);
      left.SetParams(leftName);
      right.SetParent(&p);
      right.SetParams(rightName);
      op.SetParent(&p);
      op.SetParams(leftName + " " + rightName +
                   (expression.empty() ? std::string() : " " + expression));
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

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("array.tuplewise: registered, with its inlets and outlets (#808)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY_TUPLEWISE);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".array.tuplewise");
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 2);

    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_ARRAY_TUPLEWISE)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("array.tuplewise: the trigger takes the ask, the reference inlet only lists (#808)") {
    // Inlet 0 is the ask — a bang, or the "array <name>" reference, which is
    // list text; a result is asked for, never addressed, so no int lands
    // anywhere. Inlet 1 acknowledges the right reference only.
    gArrayTuplewise op;
    const unsigned int triggerIn = op.GetInlet(0)->GetAcceptedTypes();
    CHECK((triggerIn & YSE::PATCHER::IT_BANG) != 0);
    CHECK((triggerIn & YSE::PATCHER::IT_LIST) != 0);
    CHECK((triggerIn & YSE::PATCHER::IT_INT) == 0);
    CHECK((triggerIn & YSE::PATCHER::IT_FLOAT) == 0);

    const unsigned int refIn = op.GetInlet(1)->GetAcceptedTypes();
    CHECK((refIn & YSE::PATCHER::IT_LIST) != 0);
    CHECK((refIn & YSE::PATCHER::IT_BANG) == 0);
    CHECK((refIn & YSE::PATCHER::IT_INT) == 0);
    CHECK((refIn & YSE::PATCHER::IT_FLOAT) == 0);
  }

  // ─── the combination ────────────────────────────────────────────────────────

  TEST_CASE("array.tuplewise: position by position, typed by the expression's C type (#808)") {
    // The issue's use case verbatim: two arrays added together — and the int
    // expression emits ints, .expr's C type rules.
    Rig rig("atw808a", "l808a", "r808a", "$i1 + $i2");
    REQUIRE(rig.op.ProgramReady());
    rig.StoreLeft("append 10 20 30");
    rig.StoreRight("append 1 2 3");

    rig.Ask();
    REQUIRE(rig.out.gotList);
    CHECK(rig.out.listValue == "11 22 33");
    CHECK_FALSE(rig.empty.gotBang);
    CHECK(rig.op.Dropped() == 0);

    // A float expression emits floats — a whole one keeps its point, the
    // patcher's one spelling of a number.
    Rig floats("atw808a2", "l808a2", "r808a2", "$f1 + $f2");
    floats.StoreLeft("append 1.5 2");
    floats.StoreRight("append 1 1");
    floats.Ask();
    REQUIRE(floats.out.gotList);
    CHECK(floats.out.listValue == "2.5 3.");
  }

  TEST_CASE("array.tuplewise: $3 is the pair's position (#808)") {
    Rig rig("atw808b", "l808b", "r808b", "$i1 + $i3");
    REQUIRE(rig.op.ProgramReady());
    rig.StoreLeft("append 10 20 30");
    rig.StoreRight("append 0 0 0");

    rig.Ask();
    REQUIRE(rig.out.gotList);
    CHECK(rig.out.listValue == "10 21 32");
  }

  TEST_CASE("array.tuplewise: the result is as long as the shorter array (#808)") {
    // .vexpr's rule for unequal operands: pairing stops where either side
    // runs out — padding would invent operands.
    Rig rig("atw808c", "l808c", "r808c", "$i1 * $i2");
    rig.StoreLeft("append 2 3 4 5");
    rig.StoreRight("append 10 10");
    rig.Ask();
    REQUIRE(rig.out.gotList);
    CHECK(rig.out.listValue == "20 30");

    // And the left may be the shorter side.
    Rig flipped("atw808c2", "l808c2", "r808c2", "$i1 * $i2");
    flipped.StoreLeft("append 2");
    flipped.StoreRight("append 10 10 10");
    flipped.Ask();
    CHECK(flipped.out.gotInt);
    CHECK(flipped.out.intValue == 20);
    CHECK_FALSE(flipped.out.gotList);
  }

  TEST_CASE("array.tuplewise: a pair the expression cannot see passes the left element "
            "through (#808)") {
    // The family's population rule (#799, decision 3), positionally: the
    // expression runs when both elements read as numbers, and the left
    // element stands in for the missing result — a symbol on either side —
    // so every position of the result answers a position of the left array.
    Rig rig("atw808d", "l808d", "r808d", "$i1 + $i2");
    rig.StoreLeft("append 60 c4 7");
    rig.StoreRight("append 1 2 sym");

    rig.Ask();
    REQUIRE(rig.out.gotList);
    CHECK(rig.out.listValue == "61 c4 7");
    CHECK(rig.op.Dropped() == 0);
  }

  // ─── the same-store degenerate ──────────────────────────────────────────────

  TEST_CASE("array.tuplewise: both names binding one array answers, never self-blocks (#808)") {
    // ".array.tuplewise seq seq $i1 + $i2" — the case that proves no two
    // guards are ever held at once: the left is snapshotted under its guard,
    // the result built against the right under its own, so the same store's
    // guard is taken twice *sequentially* and the array doubles.
    Rig rig("atw808e", "s808e", "s808e", "$i1 + $i2");
    rig.StoreLeft("append 60 64 67");
    rig.Ask();
    REQUIRE(rig.out.gotList);
    CHECK(rig.out.listValue == "120 128 134");
    CHECK(rig.op.Dropped() == 0);
  }

  // ─── the empty answer ───────────────────────────────────────────────────────

  TEST_CASE("array.tuplewise: an empty side means no pairs — the empty outlet answers (#808)") {
    // "No data" is a state a patch must be able to route on, not an error —
    // and the shorter-side rule makes one empty side an empty result.
    Rig rig("atw808f", "l808f", "r808f", "$i1 + $i2");
    rig.StoreLeft("append 1 2 3");
    rig.Ask();
    CHECK(rig.empty.gotBang);
    CHECK_FALSE(rig.out.gotList);
    CHECK(rig.op.Dropped() == 0);

    Rig other("atw808f2", "l808f2", "r808f2", "$i1 + $i2");
    other.StoreRight("append 1 2 3");
    other.Ask();
    CHECK(other.empty.gotBang);
    CHECK_FALSE(other.out.gotList);
  }

  // ─── read-only ──────────────────────────────────────────────────────────────

  TEST_CASE("array.tuplewise: a combination never writes to either store (#808)") {
    // The result is a list the object owns — .array itself is where a patch
    // writes a result back.
    Rig rig("atw808g", "l808g", "r808g", "$i1 + $i2");
    rig.StoreLeft("append 10 20");
    rig.StoreRight("append 1 2");
    rig.Ask();
    REQUIRE(rig.out.gotList);
    CHECK(rig.left.Count() == 2);
    CHECK(rig.left.ElementAt(0) == "10");
    CHECK(rig.left.ElementAt(1) == "20");
    CHECK(rig.right.Count() == 2);
    CHECK(rig.right.ElementAt(0) == "1");
    CHECK(rig.right.ElementAt(1) == "2");
  }

  // ─── the whole-reply rule ───────────────────────────────────────────────────

  TEST_CASE("array.tuplewise: a result that outruns a cord is refused whole (#808)") {
    // Twenty 60-character symbols passing through spell more text than a
    // list carries (AtomList::TEXT_CAPACITY is 1024), so the ask is refused
    // whole and counted — a partial combination would misalign every
    // position after the cut, the base's whole-reply rule.
    Rig rig("atw808h", "l808h", "r808h", "$i1 + $i2");
    const std::string wide(59, 'x');
    for (int half = 0; half < 2; half++) {
      std::string message = "append";
      for (int i = half * 10; i < half * 10 + 10; i++)
        message += " " + wide + static_cast<char>('a' + i);
      rig.StoreLeft(message);
    }
    std::string message = "append";
    for (int i = 0; i < 20; i++)
      message += " " + std::to_string(i);
    rig.StoreRight(message);
    CHECK(rig.left.Count() == 20);
    CHECK(rig.right.Count() == 20);

    const std::uint64_t before = rig.op.Dropped();
    rig.Ask();
    CHECK(rig.op.Dropped() == before + 1);
    CHECK_FALSE(rig.out.gotList);
    CHECK_FALSE(rig.empty.gotBang);
  }

  // ─── the compile, and the refusal of a misconfigured object ─────────────────

  TEST_CASE("array.tuplewise: a malformed expression fails loudly at parse time and the "
            "object then refuses every trigger (#808)") {
    Rig rig("atw808i", "l808i", "r808i", "$f1 +");
    CHECK_FALSE(rig.op.ProgramReady());
    CHECK_FALSE(rig.op.CompileError().empty());
    rig.StoreLeft("append 1 2");
    rig.StoreRight("append 1 2");

    // The trigger refuses, counted — never a silent 0 emitted as data — and
    // neither outlet fires.
    const std::uint64_t before = rig.op.Dropped();
    rig.Ask();
    CHECK(rig.op.Dropped() == before + 1);
    CHECK_FALSE(rig.out.gotList);
    CHECK_FALSE(rig.empty.gotBang);
  }

  TEST_CASE("array.tuplewise: a placeholder past $3 is rejected at parse time (#808)") {
    // $4 names an operand that can never exist here; $3 — the position — is
    // the last one that does.
    gArrayTuplewise past;
    past.SetParams("x808j y808j $f4 * 2");
    CHECK_FALSE(past.ProgramReady());
    CHECK_FALSE(past.CompileError().empty());

    gArrayTuplewise fits;
    fits.SetParams("x808j y808j $f3 * 2");
    CHECK(fits.ProgramReady());
    CHECK(fits.CompileError().empty());
  }

  TEST_CASE("array.tuplewise: an absent expression refuses silently until given work (#808)") {
    // Names alone compile nothing — there is nothing malformed to report, so
    // CompileError stays empty — and a trigger is a counted refusal.
    Rig rig("atw808k", "l808k", "r808k", "");
    CHECK_FALSE(rig.op.ProgramReady());
    CHECK(rig.op.CompileError().empty());
    rig.StoreLeft("append 1");
    rig.StoreRight("append 1");

    const std::uint64_t before = rig.op.Dropped();
    rig.Ask();
    CHECK(rig.op.Dropped() == before + 1);
    CHECK_FALSE(rig.out.gotList);
    CHECK_FALSE(rig.empty.gotBang);
  }

  // ─── the references ─────────────────────────────────────────────────────────

  TEST_CASE("array.tuplewise: the left reference asks on the trigger, anything else "
            "refused (#808)") {
    // "array <name>" is the message an .array's reference outlet emits on a
    // bang; on the trigger inlet it asks when it names the *left* array —
    // the family gesture. The right array's name there is a mis-wired cord,
    // refused, never resolved: a registry lookup is a mutex, and this may be
    // the audio thread.
    Rig rig("atw808l", "l808l", "r808l", "$i1 + $i2");
    rig.StoreLeft("append 10");
    rig.StoreRight("append 5");

    const std::uint64_t before = rig.op.Dropped();
    rig.op.GetInlet(0)->SetList("array l808l", YSE::T_GUI);
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 15);
    CHECK(rig.op.Dropped() == before);

    rig.Reset();
    rig.op.GetInlet(0)->SetList("array r808l", YSE::T_GUI);
    rig.op.GetInlet(0)->SetList("array somewhere_else", YSE::T_GUI);
    rig.op.GetInlet(0)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotInt);
    CHECK_FALSE(rig.out.gotList);
    CHECK_FALSE(rig.empty.gotBang);
    CHECK(rig.op.Dropped() == before + 3);
  }

  TEST_CASE("array.tuplewise: the reference inlet acknowledges the right side only (#808)") {
    // Inlet 1 answers to the *right* name — a patch may wire both reference
    // outlets across, as it would in Max — and to nothing else, the left
    // name included: each inlet is bound to one side.
    Rig rig("atw808m", "l808m", "r808m", "$i1 + $i2");
    rig.StoreLeft("append 1");

    const std::uint64_t before = rig.op.Dropped();
    rig.op.GetInlet(1)->SetList("array r808m", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotList);
    CHECK_FALSE(rig.out.gotInt);
    CHECK(rig.op.Dropped() == before);

    rig.op.GetInlet(1)->SetList("array l808m", YSE::T_GUI);
    rig.op.GetInlet(1)->SetList("array somewhere_else", YSE::T_GUI);
    rig.op.GetInlet(1)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK(rig.op.Dropped() == before + 3);
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("array.tuplewise: both addresses bind, and SetParams(\"\") resets whole (#808)") {
    // The two-name binding, visible: the left address is gArrayEndsBase's,
    // the right one gArraySetOpBase's, both prefixed with the patcher name —
    // and a reset drops the program along with both names, so a stale
    // expression cannot keep running.
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("atw808n");
    gArrayTuplewise op;
    op.SetParams("lft808 rgt808 $f1 + $f2");
    CHECK(op.Address().empty());
    CHECK(op.RightAddress().empty());
    op.SetParent(&p);
    CHECK(op.ArrayName() == "lft808");
    CHECK(op.RightName() == "rgt808");
    CHECK(op.Address() == "atw808n.lft808");
    CHECK(op.RightAddress() == "atw808n.rgt808");
    CHECK(op.ProgramReady());

    op.SetParams("");
    CHECK(op.ArrayName().empty());
    CHECK(op.RightName().empty());
    CHECK(op.Address().empty());
    CHECK(op.RightAddress().empty());
    CHECK_FALSE(op.ProgramReady());
    CHECK(op.CompileError().empty());
  }

  TEST_CASE("array.tuplewise: patcherImplementation::SetName re-anchors both bindings (#808)") {
    // The rename dispatch itself: an object created *inside* a patcher must
    // be re-anchored by the patcher, without anybody calling RefreshBinding
    // by hand — and the dispatch must reach gArraySetOpBase's virtual
    // RefreshBinding, or only the left side would move. The keepers hold the
    // old-address stores (they are not in the patcher's object map, so the
    // rename does not touch them): before the rename the combination answers
    // from both sides; after it both sides read fresh empty arrays under the
    // new prefix, so the ask bangs empty.
    MultiSink out;
    BangSink empty;
    YSE::pHandle outHandle(&out);
    YSE::pHandle emptyHandle(&empty);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("atw808o_before");

    gArray keepLeft;
    keepLeft.SetParent(&p);
    keepLeft.SetParams("l808o");
    keepLeft.GetInlet(0)->SetList("append 10 20", YSE::T_GUI);
    gArray keepRight;
    keepRight.SetParent(&p);
    keepRight.SetParams("r808o");
    keepRight.GetInlet(0)->SetList("append 1 2", YSE::T_GUI);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY_TUPLEWISE, "l808o r808o $i1 + $i2");
    REQUIRE(h != nullptr);
    p.Connect(h, 0, &outHandle, 0);
    p.Connect(h, 1, &emptyHandle, 0);

    h->SetBang(0);
    REQUIRE(out.gotList);
    CHECK(out.listValue == "11 22");
    CHECK_FALSE(empty.gotBang);

    p.SetName("atw808o_after");
    out.reset();
    h->SetBang(0);
    CHECK_FALSE(out.gotList);
    CHECK_FALSE(out.gotInt);
    CHECK(empty.gotBang);
  }

  // ─── the family, chained through the public API ─────────────────────────────

  TEST_CASE("array.tuplewise: wired from the array's reference outlet, banging the array "
            "asks (#808)") {
    // The flow a patch actually wires, end to end through the public patcher
    // API: the left .array's reference outlet into the trigger inlet gives
    // the family gesture — bang the array, out comes the vector sum — and a
    // write between two bangs changes what the next ask sees.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::patcher p;
    p.create(2);
    p.name("atw808p");
    YSE::pHandle* leftArray = p.CreateObject(YSE::OBJ::G_ARRAY, "l808p");
    YSE::pHandle* rightArray = p.CreateObject(YSE::OBJ::G_ARRAY, "r808p");
    YSE::pHandle* combine = p.CreateObject(YSE::OBJ::G_ARRAY_TUPLEWISE, "l808p r808p $i1 + $i2");
    REQUIRE(leftArray != nullptr);
    REQUIRE(rightArray != nullptr);
    REQUIRE(combine != nullptr);
    p.Connect(leftArray, 1, combine, 0);
    p.Connect(combine, 0, &sinkHandle, 0);

    leftArray->SetListData(0, "append 60 64 67");
    rightArray->SetListData(0, "append 12 12 12");
    leftArray->SetBang(0);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "72 76 79");

    sink.reset();
    rightArray->SetListData(0, "set 0 0");
    leftArray->SetBang(0);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "60 76 79");
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("array.tuplewise: params survive a DumpJSON / ParseJSON round trip (#808)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_ARRAY_TUPLEWISE, "notes808 other808 $f1 + $f2");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);
    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".array.tuplewise");
    CHECK(copy->GetParams() == "notes808 other808 $f1 + $f2");
    CHECK(copy->GetInputs() == 2);
    CHECK(copy->GetOutputs() == 2);
  }

  TEST_CASE("array.tuplewise: complete documentation metadata (#808)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(
        YSE::PATCHER::Register().Get(YSE::OBJ::G_ARRAY_TUPLEWISE));
    REQUIRE(obj != nullptr);
    CHECK_FALSE(obj->GetDescription().empty());
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 3);
    CHECK(docs[0].name == "left");
    CHECK(docs[1].name == "right");
    CHECK(docs[2].name == "expression");
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("array.tuplewise: an ask over in-patcher delivery lands on T_DSP (#808)") {
    // A .r feeding the trigger inlet dispatches on T_DSP when the block
    // drains it (issue #225) — "the audio thread adds two arrays" is the
    // ordinary case, and the whole path is two sequential guard holds, one
    // bounded walk of the compiled program and a send of list text.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("atw808q");

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go808q");
    YSE::pHandle* leftArray = p.CreateObject(YSE::OBJ::G_ARRAY, "l808q");
    YSE::pHandle* rightArray = p.CreateObject(YSE::OBJ::G_ARRAY, "r808q");
    YSE::pHandle* combine = p.CreateObject(YSE::OBJ::G_ARRAY_TUPLEWISE, "l808q r808q $i1 + $i2");
    REQUIRE(recv != nullptr);
    REQUIRE(leftArray != nullptr);
    REQUIRE(rightArray != nullptr);
    REQUIRE(combine != nullptr);
    p.Connect(recv, 0, combine, 0);
    p.Connect(combine, 0, &sinkHandle, 0);

    leftArray->SetListData(0, "append 10 20");
    rightArray->SetListData(0, "append 1 2");

    p.PassData(std::string("array l808q"), "go808q", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "11 22");
  }

  TEST_CASE("array.tuplewise: no message path allocates (#808)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every path: the snapshot, the walk of the compiled program, the
    // pass-through, the list-rendered and typed sends, the empty outlet, and
    // the refusal and acknowledgement paths — all on T_DSP.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string leftReference = "array probeL808";
    const std::string rightReference = "array probeR808";
    const std::string wrongName = "array somewhere_else_long";
    const std::string clearMessage = "clear";

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("atw808r");
    MultiSink sink;
    BangSink empty;
    gArray left;
    gArray right;
    gArrayTuplewise op;

    left.SetParent(&p);
    left.SetParams("probeL808");
    right.SetParent(&p);
    right.SetParams("probeR808");
    op.SetParams("probeL808 probeR808 $i1 + $i2");
    op.SetParent(&p);
    REQUIRE(op.ProgramReady());
    Wire(op, 0, sink);
    Wire(op, 1, empty);

    // A mixed population, so the walk of the program and the symbol
    // pass-through both run — and the left is cleared inside the scope so
    // the empty outlet runs on T_DSP too.
    left.GetInlet(0)->SetList("append 10 c4 -3", YSE::T_GUI);
    right.GetInlet(0)->SetList("append 1 2 3", YSE::T_GUI);

    // Warm every path — including the sinks' assignments — so first-call
    // machinery is not what the probe catches.
    op.GetInlet(0)->SetBang(YSE::T_GUI);
    op.GetInlet(0)->SetList(leftReference, YSE::T_GUI);
    op.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    op.GetInlet(1)->SetList(rightReference, YSE::T_GUI);
    op.GetInlet(1)->SetList(wrongName, YSE::T_GUI);
    const std::uint64_t droppedBefore = op.Dropped();

    sink.reset();
    empty.gotBang = false;
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      op.GetInlet(0)->SetBang(YSE::T_DSP);
      op.GetInlet(0)->SetList(leftReference, YSE::T_DSP);
      op.GetInlet(0)->SetList(wrongName, YSE::T_DSP);
      op.GetInlet(1)->SetList(rightReference, YSE::T_DSP);
      op.GetInlet(1)->SetList(wrongName, YSE::T_DSP);
      // The empty outlet on T_DSP too: clear the left array through the
      // .array's own message path, then ask again.
      left.GetInlet(0)->SetList(clearMessage, YSE::T_DSP);
      op.GetInlet(0)->SetBang(YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing.
    CHECK(sink.gotList);
    CHECK(sink.listValue == "11 c4 0");
    CHECK(empty.gotBang);
    CHECK(op.Dropped() == droppedBefore + 2);
  }
}

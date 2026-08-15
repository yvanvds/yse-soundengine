// Tests for .array.replace (issue #802) — Max's array.replace on the
// name-addressed value model .array settled (#548).
//
// What has to be proven, and what every case below is one of:
//
//   - **the array is bound from the creation argument.** An array never
//     travels down a cord, so ".array.replace <name>" resolves the name
//     once, on the control thread, and an `array <name>` message is honoured
//     only when it names the array already bound — ArrayReferenceNames'
//     bounded compare, never a registry lookup on a message path.
//   - **EVERY occurrence is replaced, and equality is the spelling** —
//     #802's first-or-all decision: the use case is retuning every
//     occurrence of one pitch, and first-only is already .array.indexof
//     into .array's own set. 7 and 7. are different elements.
//   - **the count leaves, before the reference** — #802's second-outlet
//     decision: the number rewritten is the only way a patch can tell
//     replaced-nothing from replaced-everything, so a replace that matched
//     nothing still lands and reports 0, where a refused one reports
//     nothing at all. Right to left, Max's outlet order.
//   - **find hot, replacement cold, whole-or-nothing.** An inline find
//     stores and replaces now; the replacement inlet stores silently; a
//     trigger while either value is absent is refused whole and stores
//     nothing.
//   - **the whole replace is one guard hold, and nothing allocates.**
//     In-patcher delivery dispatches on T_DSP, so "the audio thread
//     rewrites an array" is the ordinary case.
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
#include "patcher/genericObjects/gArrayReplace.h"
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
using YSE::PATCHER::gArrayReplace;

namespace {

  // An .array and one .array.replace on one name, sharing one
  // patcherImplementation so the name actually binds ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // sinks are declared before the objects so they are torn down last, while
  // the outlets wired to them still exist (see sinks.hpp on why that
  // matters).
  struct Rig {
    MultiSink reference; // outlet 0: the reference after a landed replace
    MultiSink count; // outlet 1: how many elements were rewritten
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    gArray array;
    gArrayReplace object;

    Rig(const std::string& patcherName, const std::string& name,
        const std::string& params = std::string()) {
      p.SetName(patcherName);
      array.SetParent(&p);
      array.SetParams(name);
      object.SetParent(&p);
      object.SetParams(params.empty() ? name : params);
      Wire(object, 0, reference);
      Wire(object, 1, count);
    }

    void Store(const std::string& message) {
      array.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    // The array's contents as one space-joined string — what the replace is
    // asserted against. Control thread only (ElementAt allocates).
    std::string Contents() {
      std::string out;
      for (std::size_t i = 0; i < array.Count(); i++) {
        if (i != 0) out += ' ';
        out += array.ElementAt(i);
      }
      return out;
    }
    void reset() {
      reference.reset();
      count.reset();
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("array.replace: registered, three inlets over the count-and-reference pair "
            "(#802)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* replace = p.CreateObject(YSE::OBJ::G_ARRAY_REPLACE);
    REQUIRE(replace != nullptr);
    CHECK(std::string(replace->Type()) == ".array.replace");
    CHECK(replace->GetInputs() == 3);
    CHECK(replace->GetOutputs() == 2);

    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_ARRAY_REPLACE)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("array.replace: the find inlet takes the ask, the cold inlets only their "
            "configuration (#802)") {
    // gArrayFill's arrangement: the ask on the left, configuration on the
    // right. The replacement inlet takes a value but no bang — nothing to
    // trigger — and the reference inlet is list text only.
    gArrayReplace replace;
    const unsigned int trigger = replace.GetInlet(0)->GetAcceptedTypes();
    CHECK((trigger & YSE::PATCHER::IT_BANG) != 0);
    CHECK((trigger & YSE::PATCHER::IT_INT) != 0);
    CHECK((trigger & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((trigger & YSE::PATCHER::IT_LIST) != 0);
    const unsigned int replacement = replace.GetInlet(1)->GetAcceptedTypes();
    CHECK((replacement & YSE::PATCHER::IT_INT) != 0);
    CHECK((replacement & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((replacement & YSE::PATCHER::IT_LIST) != 0);
    CHECK((replacement & YSE::PATCHER::IT_BANG) == 0);
    const unsigned int ref = replace.GetInlet(2)->GetAcceptedTypes();
    CHECK((ref & YSE::PATCHER::IT_LIST) != 0);
    CHECK((ref & YSE::PATCHER::IT_BANG) == 0);
  }

  // ─── every occurrence, and the count ────────────────────────────────────────

  TEST_CASE("array.replace: every occurrence is rewritten and the count reports how many "
            "(#802)") {
    // ".array.replace <name> 60 72" seeds the find and replacement, so a
    // bang is the one-message retune the object exists for — and EVERY 60
    // becomes 72, #802's decision, with the count saying how many did.
    Rig rig("arp802a", "a802a", "a802a 60 72");
    CHECK(rig.object.FindValue() == "60");
    CHECK(rig.object.ReplaceValue() == "72");
    rig.Store("append 60 64 60 67 60");

    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Contents() == "72 64 72 67 72");
    CHECK(rig.array.Count() == 5); // a replace never resizes
    REQUIRE(rig.count.gotInt);
    CHECK(rig.count.intValue == 3);
    REQUIRE(rig.reference.gotList);
    CHECK(rig.reference.listValue == "array a802a");
    CHECK(rig.object.Dropped() == 0);

    // A replace that matched nothing still landed: the ask was well-formed,
    // the array simply holds no 60 any more — count 0, reference out, and
    // nothing is a refusal. The distinction #802 says the count exists for.
    rig.reset();
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Contents() == "72 64 72 67 72");
    REQUIRE(rig.count.gotInt);
    CHECK(rig.count.intValue == 0);
    CHECK(rig.reference.gotList);
    CHECK(rig.object.Dropped() == 0);
  }

  TEST_CASE("array.replace: equality is the spelling — 7 and 7. are different elements "
            "(#802)") {
    // ArrayFind's byte compare, the family's rule: an int 7 matches only the
    // element "7", never the float spelling "7." beside it — and a symbol
    // match is case-sensitive, as every comparison in this patcher is.
    Rig rig("arp802b", "a802b", "a802b 7 9");
    rig.Store("append 7 7. seven Seven 7");

    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Contents() == "9 7. seven Seven 9");
    REQUIRE(rig.count.gotInt);
    CHECK(rig.count.intValue == 2);

    // And case matters for symbols: "seven" is not "Seven".
    rig.object.GetInlet(1)->SetList(std::string("eight"), YSE::T_GUI);
    rig.object.GetInlet(0)->SetList(std::string("seven"), YSE::T_GUI);
    CHECK(rig.Contents() == "9 7. eight Seven 9");
    CHECK(rig.count.intValue == 1);
    CHECK(rig.object.Dropped() == 0);
  }

  TEST_CASE("array.replace: an inline find stores and replaces now, and the replacement "
            "inlet stores silently (#802)") {
    Rig rig("arp802c", "a802c", "a802c x c4");
    rig.Store("append x e4 x g4");

    // A symbol on the find inlet — the placeholder swap #802 names. Stored,
    // gArrayFindBase's hot inlet: a bang afterwards re-replaces with it.
    const std::string placeholder = "x";
    rig.object.GetInlet(0)->SetList(placeholder, YSE::T_GUI);
    CHECK(rig.Contents() == "c4 e4 c4 g4");
    CHECK(rig.count.intValue == 2);

    // An int on the find inlet: stored and replaced now — and the stored
    // find value moved with it, proven by the bang.
    rig.object.GetInlet(1)->SetList(std::string("a4"), YSE::T_GUI);
    CHECK(rig.object.ReplaceValue() == "a4");
    rig.reset();
    rig.object.GetInlet(0)->SetInt(999, YSE::T_GUI); // no match — count 0
    REQUIRE(rig.count.gotInt);
    CHECK(rig.count.intValue == 0);
    CHECK(rig.object.FindValue() == "999");

    // The replacement inlet stored silently: nothing was rewritten and
    // nothing left when a4 arrived — only the trigger rewrites.
    rig.Store("append 999");
    rig.reset();
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Contents() == "c4 e4 c4 g4 a4");
    CHECK(rig.count.intValue == 1);

    // A float keeps its spelling on both inlets — 7.5 stays visibly a float.
    rig.Store("append 7.5");
    rig.object.GetInlet(1)->SetFloat(8.5f, YSE::T_GUI);
    rig.object.GetInlet(0)->SetFloat(7.5f, YSE::T_GUI);
    CHECK(rig.Contents() == "c4 e4 c4 g4 a4 8.5");
    CHECK(rig.object.Dropped() == 0);
  }

  TEST_CASE("array.replace: replacing a value with itself lands and counts its occurrences "
            "(#802)") {
    // Not a refusal and not a special case: the ask is well-formed and the
    // answer is what it says — each match rewritten as itself.
    Rig rig("arp802d", "a802d", "a802d 60 60");
    rig.Store("append 60 64 60");
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Contents() == "60 64 60");
    REQUIRE(rig.count.gotInt);
    CHECK(rig.count.intValue == 2);
    CHECK(rig.object.Dropped() == 0);
  }

  // ─── whole-or-nothing refusals ──────────────────────────────────────────────

  TEST_CASE("array.replace: a trigger while either value is absent is refused whole and "
            "stores nothing (#802)") {
    // No seeds at all: a bang holds nothing to look for — malformed, not a
    // miss, gArrayFindBase's absent-value rule.
    Rig rig("arp802e", "a802e");
    rig.Store("append 60 64");
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(rig.count.gotInt);
    CHECK_FALSE(rig.reference.gotList);
    CHECK(rig.object.Dropped() == 1);

    // An inline find with no replacement configured is refused whole — and
    // the find value is NOT stored, so a bang after the replacement finally
    // arrives behaves as if the refused trigger never happened.
    rig.object.GetInlet(0)->SetInt(60, YSE::T_GUI);
    CHECK(rig.Contents() == "60 64");
    CHECK_FALSE(rig.count.gotInt);
    CHECK(rig.object.Dropped() == 2);
    CHECK(rig.object.FindValue().empty());

    rig.object.GetInlet(1)->SetInt(72, YSE::T_GUI);
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Contents() == "60 64"); // still no find value
    CHECK_FALSE(rig.count.gotInt);
    CHECK(rig.object.Dropped() == 3);

    // Once both exist, the same bang lands.
    rig.object.GetInlet(0)->SetInt(60, YSE::T_GUI);
    CHECK(rig.Contents() == "72 64");
    CHECK(rig.count.intValue == 1);
    CHECK(rig.object.Dropped() == 3);
  }

  TEST_CASE("array.replace: one atom per value — multi-atom lists, non-finite floats and "
            "over-long atoms are refused whole (#802)") {
    Rig rig("arp802f", "a802f", "a802f 60 72");
    rig.Store("append 60 64");

    // A multi-atom list on the find inlet is refused whole: an element is
    // one atom, so only one atom can be matched — a sub-array match is not
    // offered, gArrayFindBase's rule.
    rig.object.GetInlet(0)->SetList(std::string("60 64"), YSE::T_GUI);
    CHECK(rig.Contents() == "60 64");
    CHECK(rig.object.Dropped() == 1);

    // And on the replacement inlet: a reference is two atoms and lands here
    // too — an identity is not an element, gArrayEndsWriter's rule.
    rig.object.GetInlet(1)->SetList(std::string("array a802f"), YSE::T_GUI);
    CHECK(rig.object.ReplaceValue() == "72");
    CHECK(rig.object.Dropped() == 2);

    // A non-finite float has no spelling that reads back — refused on both
    // inlets, nothing changed.
    rig.object.GetInlet(0)->SetFloat(std::numeric_limits<float>::quiet_NaN(), YSE::T_GUI);
    CHECK(rig.object.Dropped() == 3);
    rig.object.GetInlet(1)->SetFloat(std::numeric_limits<float>::infinity(), YSE::T_GUI);
    CHECK(rig.object.ReplaceValue() == "72");
    CHECK(rig.object.Dropped() == 4);

    // An atom no element can hold — past ELEMENT_CAPACITY — is refused
    // whole, never truncated, on both value inlets.
    const std::string overlong(YSE::PATCHER::arrayStore::ELEMENT_CAPACITY + 1, 'x');
    rig.object.GetInlet(0)->SetList(overlong, YSE::T_GUI);
    CHECK(rig.object.Dropped() == 5);
    rig.object.GetInlet(1)->SetList(overlong, YSE::T_GUI);
    CHECK(rig.object.ReplaceValue() == "72");
    CHECK(rig.object.Dropped() == 6);

    // And an over-long creation argument becomes no value, counted — so the
    // next trigger is refused rather than matching a truncation.
    Rig wide("arp802f2", "a802f2",
             "a802f2 " + std::string(YSE::PATCHER::arrayStore::ELEMENT_CAPACITY + 1, 'y') + " 72");
    CHECK(wide.object.FindValue().empty());
    CHECK(wide.object.Dropped() == 1);
    wide.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(wide.count.gotInt);
    CHECK(wide.object.Dropped() == 2);

    // Nothing above rewrote anything.
    CHECK(rig.Contents() == "60 64");
  }

  // ─── the outlets, right to left ─────────────────────────────────────────────

  TEST_CASE("array.replace: the count leaves before the reference — Max's outlet order "
            "(#802)") {
    // The count has arrived wherever it is wired by the time the reference
    // triggers the family downstream — .clocker's rule, asserted with the
    // order log rather than assumed.
    std::vector<char> log;
    OrderSink referenceSink;
    OrderSink countSink;
    referenceSink.log = &log;
    referenceSink.tag = 'r';
    countSink.log = &log;
    countSink.tag = 'c';

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("arp802g");
    gArray array;
    array.SetParent(&p);
    array.SetParams("a802g");
    gArrayReplace replace;
    replace.SetParent(&p);
    replace.SetParams("a802g 60 72");
    Wire(replace, 0, referenceSink);
    Wire(replace, 1, countSink);
    array.GetInlet(0)->SetList(std::string("append 60"), YSE::T_GUI);

    replace.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(log.size() == 2);
    CHECK(log[0] == 'c');
    CHECK(log[1] == 'r');
    CHECK(countSink.lastInt == 1);
    CHECK(referenceSink.lastList == "array a802g");
  }

  // ─── the reference gesture ──────────────────────────────────────────────────

  TEST_CASE("array.replace: the reference replaces on the trigger, acknowledges on its own "
            "inlet, and anything else is refused (#802)") {
    Rig rig("arp802h", "a802h", "a802h g3 a3");
    rig.Store("append g3 b3");

    rig.object.GetInlet(0)->SetList(std::string("array a802h"), YSE::T_GUI);
    CHECK(rig.Contents() == "a3 b3");
    CHECK(rig.count.intValue == 1);
    CHECK(rig.reference.gotList);
    CHECK(rig.object.Dropped() == 0);

    // A reference naming an array this object is not bound to is refused
    // everywhere, never resolved: a registry lookup is a mutex, and this may
    // be the audio thread.
    rig.reset();
    rig.object.GetInlet(0)->SetList(std::string("array somewhere_else"), YSE::T_GUI);
    CHECK_FALSE(rig.count.gotInt);
    CHECK(rig.Contents() == "a3 b3");
    CHECK(rig.object.Dropped() == 1);

    rig.object.GetInlet(2)->SetList(std::string("array a802h"), YSE::T_GUI);
    CHECK(rig.object.Dropped() == 1);
    rig.object.GetInlet(2)->SetList(std::string("array somewhere_else"), YSE::T_GUI);
    CHECK(rig.object.Dropped() == 2);
    CHECK_FALSE(rig.count.gotInt);
  }

  TEST_CASE("array.replace: an unnamed object rewrites a private array — the count answers, "
            "the reference stays silent (#802)") {
    // No name, no shared store, no reference to pass on — but a count is a
    // value, not an identity, so the answer still leaves: 0, the private
    // array being empty. Nothing is a refusal: the ask was well-formed.
    MultiSink referenceSink;
    MultiSink countSink;
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("arp802i");
    gArrayReplace replace;
    replace.SetParent(&p);
    replace.SetParams("");
    Wire(replace, 0, referenceSink);
    Wire(replace, 1, countSink);
    CHECK(replace.Address().empty());

    // The values arrive by inlet — an unnamed object still configures.
    replace.GetInlet(1)->SetInt(72, YSE::T_GUI);
    replace.GetInlet(0)->SetInt(60, YSE::T_GUI);
    REQUIRE(countSink.gotInt);
    CHECK(countSink.intValue == 0);
    CHECK_FALSE(referenceSink.gotList);
    CHECK(replace.Dropped() == 0);
  }

  // ─── the family chains ──────────────────────────────────────────────────────

  TEST_CASE("array.replace: retunes a pitch end to end on cords (#802)") {
    // The use case #802 names, run through the public patcher API: every 60
    // becomes 72, the count says how many did, and the reference chains into
    // .array.length to prove a replace never resizes.
    MultiSink countOut;
    MultiSink lengthOut;
    MultiSink contentsOut;
    YSE::pHandle countHandle(&countOut);
    YSE::pHandle lengthHandle(&lengthOut);
    YSE::pHandle contentsHandle(&contentsOut);
    YSE::patcher p;
    p.create(2);
    p.name("arp802j");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "notes802j");
    YSE::pHandle* replace = p.CreateObject(YSE::OBJ::G_ARRAY_REPLACE, "notes802j 60 72");
    YSE::pHandle* length = p.CreateObject(YSE::OBJ::G_ARRAY_LENGTH, "notes802j");
    REQUIRE(array != nullptr);
    REQUIRE(replace != nullptr);
    REQUIRE(length != nullptr);
    p.Connect(replace, 0, length, 0);
    p.Connect(replace, 1, &countHandle, 0);
    p.Connect(length, 0, &lengthHandle, 0);
    p.Connect(array, 0, &contentsHandle, 0);

    array->SetListData(0, "append 60 64 60 67");
    replace->SetBang(0);
    REQUIRE(countOut.gotInt);
    CHECK(countOut.intValue == 2);
    REQUIRE(lengthOut.gotInt);
    CHECK(lengthOut.intValue == 4);

    array->SetListData(0, "getvalue");
    REQUIRE(contentsOut.gotList);
    CHECK(contentsOut.listValue == "72 64 72 67");
  }

  TEST_CASE("array.replace: wired from the array's reference outlet, banging the array "
            "replaces (#802)") {
    // The family gesture, end to end through the public patcher API: the
    // .array's reference outlet into the find inlet gives "bang the array,
    // out come the rewritten contents", read back through the array's own
    // getvalue, so the whole loop runs on cords.
    MultiSink chained;
    MultiSink contents;
    YSE::pHandle chainedHandle(&chained);
    YSE::pHandle contentsHandle(&contents);
    YSE::patcher p;
    p.create(2);
    p.name("arp802k");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "a802k");
    YSE::pHandle* replace = p.CreateObject(YSE::OBJ::G_ARRAY_REPLACE, "a802k x c4");
    REQUIRE(array != nullptr);
    REQUIRE(replace != nullptr);
    p.Connect(array, 1, replace, 0);
    p.Connect(replace, 0, &chainedHandle, 0);
    p.Connect(array, 0, &contentsHandle, 0);

    array->SetListData(0, "append x e4 x");
    array->SetBang(0);
    REQUIRE(chained.gotList);
    CHECK(chained.listValue == "array a802k");

    array->SetListData(0, "getvalue");
    REQUIRE(contents.gotList);
    CHECK(contents.listValue == "c4 e4 c4");
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("array.replace: patcherImplementation::SetName re-anchors the shared base "
            "(#802)") {
    // The rename dispatch itself: an object created *inside* a patcher must
    // be re-anchored by the patcher, without anybody calling RefreshBinding
    // by hand. The keeper holds the old-address store; after the rename the
    // object rewrites a fresh empty array under the new prefix, so the
    // keeper's contents stop moving — while the stored values survive, being
    // the object's own state rather than anything derived from the name.
    MultiSink countSink;
    YSE::pHandle countHandle(&countSink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("arp802l_before");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a802l");
    keeper.GetInlet(0)->SetList("append 60 64 60", YSE::T_GUI);

    YSE::pHandle* replace = p.CreateObject(YSE::OBJ::G_ARRAY_REPLACE, "a802l 60 72");
    REQUIRE(replace != nullptr);
    p.Connect(replace, 1, &countHandle, 0);

    replace->SetBang(0);
    REQUIRE(countSink.gotInt);
    CHECK(countSink.intValue == 2);
    CHECK(keeper.ElementAt(0) == "72");

    p.SetName("arp802l_after");
    countSink.reset();
    keeper.GetInlet(0)->SetList("set 0 60", YSE::T_GUI);
    replace->SetBang(0);
    // The replace landed — on the new, empty array, count 0 — and the
    // keeper's restored 60 was not touched.
    REQUIRE(countSink.gotInt);
    CHECK(countSink.intValue == 0);
    CHECK(keeper.ElementAt(0) == "60");
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("array.replace: a find arriving over in-patcher delivery lands on T_DSP (#802)") {
    // A .r feeding the find inlet dispatches on T_DSP when the block drains
    // it (issue #225) — "the audio thread rewrites an array" is the ordinary
    // case, and the whole path is one bounded render, one guard hold and two
    // sends of what the object already owns.
    MultiSink countSink;
    YSE::pHandle countHandle(&countSink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("arp802m");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a802m");
    keeper.GetInlet(0)->SetList("append 60 64 60", YSE::T_GUI);

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go802m");
    YSE::pHandle* replace = p.CreateObject(YSE::OBJ::G_ARRAY_REPLACE, "a802m 0 72");
    REQUIRE(recv != nullptr);
    REQUIRE(replace != nullptr);
    p.Connect(recv, 0, replace, 0);
    p.Connect(replace, 1, &countHandle, 0);

    p.PassData(std::string("60"), "go802m", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(countSink.gotInt);
    CHECK(countSink.intValue == 2);
    CHECK(keeper.ElementAt(0) == "72");
    CHECK(keeper.ElementAt(1) == "64");
    CHECK(keeper.ElementAt(2) == "72");
  }

  TEST_CASE("array.replace: no message path allocates (#802)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path — the bang, the inline int, float and symbol finds,
    // the reference gesture, the replacement stores, the refusals, and the
    // reference-inlet acknowledgement — on T_DSP, in-patcher delivery's
    // thread.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string reference = "array probeARP802";
    const std::string wrongName = "array somewhere_else_long";
    const std::string symbol = "a_symbol_value";
    const std::string malformed = "60 64 67";
    const std::string replacement = "the_replacement";

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("arp802n");
    MultiSink referenceSink;
    MultiSink countSink;
    gArray array;
    gArrayReplace replace;
    array.SetParent(&p);
    array.SetParams("probeARP802");
    replace.SetParent(&p);
    replace.SetParams("probeARP802 60 72");
    Wire(replace, 0, referenceSink);
    Wire(replace, 1, countSink);
    array.GetInlet(0)->SetList("append 60 61 60 62 60", YSE::T_GUI);

    // Warm every path — including the sinks' assignments — so first-call
    // machinery is not what the probe catches.
    replace.GetInlet(0)->SetBang(YSE::T_GUI);
    replace.GetInlet(0)->SetInt(61, YSE::T_GUI);
    replace.GetInlet(0)->SetFloat(7.5f, YSE::T_GUI);
    replace.GetInlet(0)->SetList(symbol, YSE::T_GUI);
    replace.GetInlet(0)->SetList(reference, YSE::T_GUI);
    replace.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    replace.GetInlet(0)->SetList(malformed, YSE::T_GUI);
    replace.GetInlet(1)->SetInt(80, YSE::T_GUI);
    replace.GetInlet(1)->SetFloat(2.5f, YSE::T_GUI);
    replace.GetInlet(1)->SetList(replacement, YSE::T_GUI);
    replace.GetInlet(1)->SetList(malformed, YSE::T_GUI);
    replace.GetInlet(2)->SetList(reference, YSE::T_GUI);

    // A fresh array so the probed replaces have real matches to rewrite.
    array.GetInlet(0)->SetList("clear", YSE::T_GUI);
    array.GetInlet(0)->SetList("append 60 61 60 62 60", YSE::T_GUI);
    replace.GetInlet(1)->SetList(replacement, YSE::T_GUI);

    const std::uint64_t drops = replace.Dropped();
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      replace.GetInlet(0)->SetInt(60, YSE::T_DSP); // rewrites three
      replace.GetInlet(0)->SetBang(YSE::T_DSP); // count 0 now
      replace.GetInlet(0)->SetFloat(7.5f, YSE::T_DSP); // count 0
      replace.GetInlet(0)->SetList(symbol, YSE::T_DSP); // count 0
      replace.GetInlet(0)->SetList(reference, YSE::T_DSP); // the gesture
      replace.GetInlet(0)->SetList(wrongName, YSE::T_DSP); // refused
      replace.GetInlet(0)->SetList(malformed, YSE::T_DSP); // refused
      replace.GetInlet(1)->SetInt(80, YSE::T_DSP); // stored, silent
      replace.GetInlet(1)->SetFloat(2.5f, YSE::T_DSP); // stored, silent
      replace.GetInlet(1)->SetList(malformed, YSE::T_DSP); // refused
      replace.GetInlet(2)->SetList(reference, YSE::T_DSP); // acknowledged
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing. The inline 60 rewrote all three occurrences
    // as the stored replacement, the cold stores moved the replacement
    // afterwards, and exactly the three refusals were counted.
    CHECK(countSink.gotInt);
    CHECK(referenceSink.gotList);
    std::string contents;
    for (std::size_t i = 0; i < array.Count(); i++) {
      if (i != 0) contents += ' ';
      contents += array.ElementAt(i);
    }
    CHECK(contents == "the_replacement 61 the_replacement 62 the_replacement");
    CHECK(replace.ReplaceValue() == "2.5");
    CHECK(replace.FindValue() == symbol);
    CHECK(replace.Dropped() == drops + 3);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("array.replace: params survive a DumpJSON / ParseJSON round trip (#802)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* replace = src.CreateObject(YSE::OBJ::G_ARRAY_REPLACE, "notes802o 60 72");
    REQUIRE(replace != nullptr);
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
      if (std::string(handle->Type()) == ".array.replace") copy = handle;
    }

    REQUIRE(copy != nullptr);
    // The analyzer cannot see that a failed REQUIRE aborts the case (doctest's
    // failure path is a runtime jump), so it assumes `copy` may be null here.
    // NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage)
    CHECK(copy->GetParams() == std::string("notes802o 60 72"));
    CHECK(copy->GetInputs() == 3);
    CHECK(copy->GetOutputs() == 2);

    // A re-parse must not leave half of the previous configuration standing:
    // SetParams("") drops both stored values along with the name, so a bang
    // afterwards is refused rather than replaying the old configuration.
    gArrayReplace fresh;
    fresh.SetParams("notes802o2 60 72");
    CHECK(fresh.FindValue() == "60");
    fresh.SetParams("");
    CHECK(fresh.FindValue().empty());
    CHECK(fresh.ReplaceValue().empty());
    fresh.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(fresh.Dropped() == 1);
  }

  TEST_CASE("array.replace: carries complete documentation metadata (#802)") {
    gArrayReplace replace;
    CHECK_FALSE(replace.GetDescription().empty());
    CHECK(replace.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    REQUIRE(replace.GetParamDocs().size() == 3);
    CHECK(replace.GetParamDocs()[0].name == "name");
    CHECK(replace.GetParamDocs()[1].name == "find");
    CHECK(replace.GetParamDocs()[2].name == "replace");
  }
}

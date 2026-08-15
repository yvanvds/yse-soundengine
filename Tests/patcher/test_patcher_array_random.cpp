// Tests for .array.random (issue #805) — Max's array.random on the
// name-addressed value model .array settled (#548).
//
// What has to be proven, and what every case below is one of:
//
//   - **the array is bound from the creation argument.** An array never
//     travels down a cord, so ".array.random <name> [<seed>]" resolves the
//     name once, on the control thread, and an `array <name>` message is
//     honoured only when it names the array already bound —
//     ArrayReferenceNames' bounded compare, never a registry lookup on a
//     message path.
//   - **a pick is one guarded read at a drawn position.** The length is
//     read, the position drawn and the element copied out under one hold of
//     the store's guard, so a pick ranges over the array as it stood at the
//     trigger and can never miss.
//   - **the randomness is the family's.** A per-object, seedable
//     RandomSource — the source .drunk, .urn and .array.scramble share —
//     with exactly one draw per element that leaves, which is what makes a
//     seeded stream replayable. Repeats are allowed: no-repeat is .urn's
//     behaviour, a second object rather than a mode of this one.
//   - **an empty array bangs the empty outlet, and takes no draw.** "No
//     data" is a state a patch must be able to route on, not an error, and a
//     draw nothing left with would desynchronise a seeded stream from the
//     elements actually picked.
//   - **nothing on a message path allocates.** In-patcher delivery
//     dispatches on T_DSP, so "the audio thread picks an element" is the
//     ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names an array uses names of its own — one case's contents
// must not be visible to the next.

#include <doctest/doctest.h>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include "patcher/genericObjects/gArray.h"
#include "patcher/genericObjects/gArrayRandom.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
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
using YSE::PATCHER::gArrayRandom;

namespace {

  // An .array and one .array.random on one name, sharing one
  // patcherImplementation so the name actually binds ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // sinks are declared before the objects so they are torn down last, while
  // the outlets wired to them still exist (see sinks.hpp on why that
  // matters).
  struct Rig {
    MultiSink element; // outlet 0: the picked element, typed by its spelling
    BangSink empty; // outlet 1: bang when there was nothing to pick
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    gArray array;
    gArrayRandom object;

    Rig(const std::string& patcherName, const std::string& name,
        const std::string& params = std::string()) {
      p.SetName(patcherName);
      array.SetParent(&p);
      array.SetParams(name);
      object.SetParent(&p);
      object.SetParams(params.empty() ? name : params);
      Wire(object, 0, element);
      Wire(object, 1, empty);
    }

    void Store(const std::string& message) {
      array.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    // One pick, recorded as the text it arrived as — however it was typed.
    // Control thread only (the returned string allocates).
    std::string PickOnce() {
      element.reset();
      empty.gotBang = false;
      object.GetInlet(0)->SetBang(YSE::T_GUI);
      if (element.gotInt) return std::to_string(element.intValue);
      if (element.gotFloat) {
        // The two float spellings the cases below store; enough for a test.
        if (element.floatValue == 7.5f) return "7.5";
        return std::to_string(element.floatValue);
      }
      if (element.gotList) return element.listValue;
      return std::string();
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("array.random: registered, with its inlets and outlets (#805)") {
    YSE::patcher p;
    p.create(2);

    // Trigger, seed, reference — .array.scramble's arrangement; the element
    // and the empty outlet leave.
    YSE::pHandle* random = p.CreateObject(YSE::OBJ::G_ARRAY_RANDOM);
    REQUIRE(random != nullptr);
    CHECK(std::string(random->Type()) == ".array.random");
    CHECK(random->GetInputs() == 3);
    CHECK(random->GetOutputs() == 2);

    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_ARRAY_RANDOM)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("array.random: the trigger takes only the ask, the cold inlets only their "
            "configuration (#805)") {
    // A pick is asked for with a bang, never addressed — .array.at is the
    // object that takes a position — so there is no int or float method on
    // the trigger. The seed inlet is a number and nothing else, and the
    // reference inlet takes only list text.
    gArrayRandom random;
    const unsigned int trigger = random.GetInlet(0)->GetAcceptedTypes();
    CHECK((trigger & YSE::PATCHER::IT_BANG) != 0);
    CHECK((trigger & YSE::PATCHER::IT_LIST) != 0);
    CHECK((trigger & YSE::PATCHER::IT_INT) == 0);
    CHECK((trigger & YSE::PATCHER::IT_FLOAT) == 0);
    const unsigned int seed = random.GetInlet(1)->GetAcceptedTypes();
    CHECK((seed & YSE::PATCHER::IT_INT) != 0);
    CHECK((seed & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((seed & YSE::PATCHER::IT_LIST) == 0);
    CHECK((seed & YSE::PATCHER::IT_BANG) == 0);
    const unsigned int reference = random.GetInlet(2)->GetAcceptedTypes();
    CHECK((reference & YSE::PATCHER::IT_LIST) != 0);
    CHECK((reference & YSE::PATCHER::IT_BANG) == 0);
  }

  // ─── the pick ───────────────────────────────────────────────────────────────

  TEST_CASE("array.random: every pick is an element of the array, typed by its spelling "
            "(#805)") {
    // Single-element arrays pin the type exactly: whatever position the draw
    // names, the one element there is what leaves — an int as an int, a
    // float as a float, a symbol as one-token list text. SendAtom's rule.
    Rig ints("apr805a1", "a805a1");
    ints.Store("append 10");
    ints.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(ints.element.gotInt);
    CHECK(ints.element.intValue == 10);
    CHECK_FALSE(ints.empty.gotBang);

    Rig floats("apr805a2", "a805a2");
    floats.Store("append 7.5");
    floats.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(floats.element.gotFloat);
    CHECK(floats.element.floatValue == 7.5f);

    Rig symbols("apr805a3", "a805a3");
    symbols.Store("append c4");
    symbols.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(symbols.element.gotList);
    CHECK(symbols.element.listValue == "c4");
    CHECK(symbols.object.Dropped() == 0);
  }

  TEST_CASE("array.random: a mixed array only ever answers with its own elements, and repeats "
            "are allowed (#805)") {
    // Membership over many picks: every answer is one of the four stored
    // elements — a pick can never miss, the length and the draw being one
    // guard hold — and with 32 picks over 4 elements some element repeats
    // (no-repeat is .urn's behaviour, deliberately not this object's).
    Rig rig("apr805b", "a805b", "a805b 42");
    rig.Store("append 10 20 7.5 c4");

    const std::set<std::string> stored = {"10", "20", "7.5", "c4"};
    std::set<std::string> seen;
    for (int i = 0; i < 32; i++) {
      const std::string pick = rig.PickOnce();
      REQUIRE(stored.count(pick) == 1);
      seen.insert(pick);
    }
    // 32 picks over 4 elements repeated something — and a seeded stream that
    // never left one element would be a broken draw, so more than one
    // distinct element was seen.
    CHECK(seen.size() > 1);
    CHECK(rig.object.Dropped() == 0);
    CHECK_FALSE(rig.empty.gotBang);
  }

  TEST_CASE("array.random: a seeded stream is reproducible, one draw per pick, and the seed "
            "inlet replays it silently (#805)") {
    // Two objects on two arrays with the same contents and the same seed
    // must pick identically — the n-th draw after Seed(s) is a pure function
    // of s and n, nothing else — and exactly one draw is taken per element
    // that leaves, the property that makes the stream replayable at all.
    Rig one("apr805c1", "a805c1", "a805c1 42");
    Rig two("apr805c2", "a805c2", "a805c2 42");
    one.Store("append 10 20 30 c4 7.5 60 e2 80");
    two.Store("append 10 20 30 c4 7.5 60 e2 80");

    std::vector<std::string> first;
    for (int i = 0; i < 16; i++) {
      const std::string a = one.PickOnce();
      const std::string b = two.PickOnce();
      CHECK(a == b);
      first.push_back(a);
    }
    CHECK(one.object.Draws() == 16);

    // The seed inlet restarts the sequence silently — nothing emitted,
    // nothing rewritten — and the same seed replays the same picks.
    one.element.reset();
    one.empty.gotBang = false;
    one.object.GetInlet(1)->SetInt(42, YSE::T_GUI);
    CHECK_FALSE(one.element.gotInt);
    CHECK_FALSE(one.element.gotFloat);
    CHECK_FALSE(one.element.gotList);
    CHECK_FALSE(one.empty.gotBang);
    CHECK(one.object.Draws() == 0);
    for (int i = 0; i < 16; i++)
      CHECK(one.PickOnce() == first[static_cast<std::size_t>(i)]);

    // A float seed truncates to an int first — Max's float method — so 42.9
    // replays seed 42's stream; a non-finite one is refused rather than
    // quietly becoming seed 0.
    one.object.GetInlet(1)->SetFloat(42.9f, YSE::T_GUI);
    CHECK(one.PickOnce() == first[0]);
    CHECK(one.object.Dropped() == 0);
    one.object.GetInlet(1)->SetFloat(std::numeric_limits<float>::quiet_NaN(), YSE::T_GUI);
    CHECK(one.object.Dropped() == 1);
  }

  // ─── the empty outlet ───────────────────────────────────────────────────────

  TEST_CASE("array.random: an empty array bangs the empty outlet and takes no draw (#805)") {
    Rig rig("apr805d", "a805d", "a805d 42");

    // Empty: no element to pick, the empty outlet bangs — and the sequence
    // does not move, so a seeded stream stays aligned with the elements
    // actually picked.
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.empty.gotBang);
    CHECK_FALSE(rig.element.gotInt);
    CHECK_FALSE(rig.element.gotFloat);
    CHECK_FALSE(rig.element.gotList);
    CHECK(rig.object.Draws() == 0);
    CHECK(rig.object.Dropped() == 0);

    // Grown, the same object picks — the earlier empty ask planted nothing.
    rig.Store("append 60");
    rig.empty.gotBang = false;
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.element.gotInt);
    CHECK(rig.element.intValue == 60);
    CHECK_FALSE(rig.empty.gotBang);
    CHECK(rig.object.Draws() == 1);
  }

  TEST_CASE("array.random: an unnamed object picks from a private, empty array (#805)") {
    // No name, no shared store: the ask lands, the empty outlet bangs, and
    // nothing is a refusal — the ask was well-formed. gArrayStatsBase's
    // empty rule.
    BangSink empty;
    MultiSink element;
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("apr805e");
    gArrayRandom random;
    random.SetParent(&p);
    Wire(random, 0, element);
    Wire(random, 1, empty);
    CHECK(random.Address().empty());

    random.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(empty.gotBang);
    CHECK_FALSE(element.gotInt);
    CHECK(random.Dropped() == 0);
  }

  // ─── the reference gesture, and refusals ────────────────────────────────────

  TEST_CASE("array.random: the reference picks on the trigger, acknowledges on its own inlet, "
            "and anything else is refused (#805)") {
    Rig rig("apr805f", "a805f", "a805f 42");
    rig.Store("append 10");

    // The family's gesture: the message an .array's reference outlet emits
    // on a bang picks.
    rig.object.GetInlet(0)->SetList("array a805f", YSE::T_GUI);
    CHECK(rig.element.gotInt);
    CHECK(rig.element.intValue == 10);
    CHECK(rig.object.Dropped() == 0);

    // A reference naming an array this object is not bound to is refused
    // everywhere, never resolved: a registry lookup is a mutex, and this may
    // be the audio thread.
    rig.element.reset();
    rig.object.GetInlet(0)->SetList("array somewhere_else", YSE::T_GUI);
    CHECK_FALSE(rig.element.gotInt);
    CHECK(rig.object.Dropped() == 1);

    rig.object.GetInlet(0)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK(rig.object.Dropped() == 2);

    rig.object.GetInlet(2)->SetList("array a805f", YSE::T_GUI);
    CHECK(rig.object.Dropped() == 2);
    rig.object.GetInlet(2)->SetList("array somewhere_else", YSE::T_GUI);
    CHECK(rig.object.Dropped() == 3);
    CHECK_FALSE(rig.element.gotInt);
  }

  TEST_CASE("array.random: wired from the array's reference outlet, banging the array picks "
            "(#805)") {
    // The flow a patch actually wires, end to end through the public patcher
    // API: the .array's reference outlet into the trigger gives the family
    // gesture — bang the array, out comes a random element.
    MultiSink element;
    YSE::pHandle elementHandle(&element);
    YSE::patcher p;
    p.create(2);
    p.name("apr805g");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "a805g");
    YSE::pHandle* random = p.CreateObject(YSE::OBJ::G_ARRAY_RANDOM, "a805g 42");
    REQUIRE(array != nullptr);
    REQUIRE(random != nullptr);
    p.Connect(array, 1, random, 0);
    p.Connect(random, 0, &elementHandle, 0);

    array->SetListData(0, "append 60");
    array->SetBang(0);
    CHECK(element.gotInt);
    CHECK(element.intValue == 60);
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("array.random: patcherImplementation::SetName re-anchors the shared base (#805)") {
    // The rename dispatch itself: an object created *inside* a patcher must
    // be re-anchored by the patcher, without anybody calling RefreshBinding
    // by hand. The keeper holds the old-address store; after the rename the
    // object reads a fresh empty array under the new prefix, so a pick bangs
    // the empty outlet — while the keeper's contents stay put.
    MultiSink element;
    BangSink empty;
    YSE::pHandle elementHandle(&element);
    YSE::pHandle emptyHandle(&empty);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("apr805h_before");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a805h");
    keeper.GetInlet(0)->SetList("append 10 20 30", YSE::T_GUI);

    YSE::pHandle* random = p.CreateObject(YSE::OBJ::G_ARRAY_RANDOM, "a805h 42");
    REQUIRE(random != nullptr);
    p.Connect(random, 0, &elementHandle, 0);
    p.Connect(random, 1, &emptyHandle, 0);

    random->SetBang(0);
    CHECK(element.gotInt);
    CHECK_FALSE(empty.gotBang);

    p.SetName("apr805h_after");
    element.reset();
    random->SetBang(0);
    // The pick landed — on the new, empty array — and the keeper's contents
    // were not touched.
    CHECK(empty.gotBang);
    CHECK_FALSE(element.gotInt);
    CHECK(keeper.Count() == 3);
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("array.random: a trigger arriving over in-patcher delivery lands on T_DSP (#805)") {
    // A .r feeding the trigger dispatches on T_DSP when the block drains it
    // (issue #225) — "the audio thread picks an element" is the ordinary
    // case, and the whole path is one bounded compare, one draw, one guard
    // hold and one typed send.
    MultiSink element;
    YSE::pHandle elementHandle(&element);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("apr805i");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a805i");
    keeper.GetInlet(0)->SetList("append 60", YSE::T_GUI);

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go805i");
    YSE::pHandle* random = p.CreateObject(YSE::OBJ::G_ARRAY_RANDOM, "a805i 42");
    REQUIRE(recv != nullptr);
    REQUIRE(random != nullptr);
    p.Connect(recv, 0, random, 0);
    p.Connect(random, 0, &elementHandle, 0);

    p.PassData(std::string("array a805i"), "go805i", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    CHECK(element.gotInt);
    CHECK(element.intValue == 60);
  }

  TEST_CASE("array.random: no message path allocates (#805)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path — the pick (all three SendAtom typings), the
    // reference gesture, the refusals, the reference-inlet acknowledgement,
    // the reseeds and the empty-array bang — on T_DSP, in-patcher delivery's
    // thread.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string reference = "array probeAPR805";
    const std::string wrongName = "array somewhere_else_long";

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("apr805j");
    MultiSink element;
    BangSink emptySink;
    MultiSink privateElement;
    BangSink privateEmpty;
    gArray array;
    gArrayRandom random;
    gArrayRandom unnamed;
    array.SetParent(&p);
    array.SetParams("probeAPR805");
    random.SetParent(&p);
    random.SetParams("probeAPR805 42");
    unnamed.SetParent(&p);
    Wire(random, 0, element);
    Wire(random, 1, emptySink);
    Wire(unnamed, 0, privateElement);
    Wire(unnamed, 1, privateEmpty);

    // An int, a float and a symbol, so every SendAtom typing runs under the
    // probe — whichever the draw names, the path is exercised over enough
    // picks.
    array.GetInlet(0)->SetList("append 10 7.5 c4", YSE::T_GUI);

    // Warm every path — including the sinks' assignments — so first-call
    // machinery is not what the probe catches.
    for (int i = 0; i < 8; i++)
      random.GetInlet(0)->SetBang(YSE::T_GUI);
    random.GetInlet(0)->SetList(reference, YSE::T_GUI);
    random.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    random.GetInlet(1)->SetInt(42, YSE::T_GUI);
    random.GetInlet(1)->SetFloat(7.f, YSE::T_GUI);
    random.GetInlet(2)->SetList(reference, YSE::T_GUI);
    unnamed.GetInlet(0)->SetBang(YSE::T_GUI);

    const std::uint64_t drops = random.Dropped();
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      for (int i = 0; i < 8; i++)
        random.GetInlet(0)->SetBang(YSE::T_DSP);
      random.GetInlet(0)->SetList(reference, YSE::T_DSP);
      random.GetInlet(0)->SetList(wrongName, YSE::T_DSP); // refused
      random.GetInlet(1)->SetInt(42, YSE::T_DSP); // reseed, silent
      random.GetInlet(1)->SetFloat(7.f, YSE::T_DSP); // reseed, silent
      random.GetInlet(2)->SetList(reference, YSE::T_DSP); // acknowledged
      unnamed.GetInlet(0)->SetBang(YSE::T_DSP); // empty outlet bangs
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing. Picks landed, the private array's ask bang
    // out the empty outlet, and exactly the one refusal was counted.
    CHECK((element.gotInt || element.gotFloat || element.gotList));
    CHECK(privateEmpty.gotBang);
    CHECK_FALSE(privateElement.gotInt);
    CHECK(random.Dropped() == drops + 1);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("array.random: params survive a DumpJSON / ParseJSON round trip (#805)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* random = src.CreateObject(YSE::OBJ::G_ARRAY_RANDOM, "notes805 42");
    REQUIRE(random != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);
    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".array.random");
    CHECK(copy->GetParams() == std::string("notes805 42"));
    CHECK(copy->GetInputs() == 3);
    CHECK(copy->GetOutputs() == 2);
  }

  TEST_CASE("array.random: a re-parse resets the seed along with the name (#805)") {
    // SetParams("") must not keep picking from whatever stream the previous
    // arguments planted — gArrayPositionBase's rule, and the reset drops the
    // binding back to a private array too.
    Rig rig("apr805k", "a805k", "a805k 42");
    rig.Store("append 10");
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.object.Draws() == 1);

    rig.object.SetParams("");
    CHECK(rig.object.Address().empty());
    CHECK(rig.object.Draws() == 0);
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.empty.gotBang);
  }

  TEST_CASE("array.random: carries complete documentation metadata (#805)") {
    gArrayRandom random;
    CHECK_FALSE(random.GetDescription().empty());
    CHECK(random.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    REQUIRE(random.GetParamDocs().size() == 2);
    CHECK(random.GetParamDocs()[0].name == "name");
    CHECK(random.GetParamDocs()[1].name == "seed");
  }
}

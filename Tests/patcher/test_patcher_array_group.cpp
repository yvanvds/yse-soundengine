// Tests for .array.group (issue #801) — the array's elements grouped by
// value on the name-addressed value model .array settled (#548). Issue
// #801's reading, written down in gArrayGroup.h: Max's own array.group is a
// count batcher, which on the value model is .array's own append; what the
// family was missing is the by-value bucketing, and the output shape is the
// per-group message — .array.iter's shape, the issue's own recommendation.
//
// What has to be proven, and what every case below is one of:
//
//   - **the array is bound from the creation argument.** An array never
//     travels down a cord, so ".array.group <name>" resolves the name once,
//     on the control thread, and an `array <name>` message is honoured only
//     when it names the array already bound — ArrayReferenceNames' bounded
//     compare, never a registry lookup on a message path.
//   - **a grouping is one send per distinct value, buckets whole, in
//     first-occurrence order, with the done bang last.** Equality is the
//     spelling — .array.mode's rule, 7 and 7. different values — a bucket
//     of one leaves typed (SendAtoms' rule) and a bucket of several as one
//     list, the value repeated, so its length is the value's frequency. The
//     done bang fires even for an empty array, never for a refused
//     grouping.
//   - **the grouping is snapshotted** — .array.iter's answer (#798) to
//     #548's renumbering warning, inherited deliberately: a write arriving
//     mid-grouping (including from the buckets' own subgraph) changes the
//     array but not the buckets in flight, and two .array.group on one name
//     group independently.
//   - **whole buckets or nothing**: a grouping any bucket of which outruns
//     what a cord carries is refused whole *before* the first send —
//     .array.sect's whole-refusal rule, because an already-sent bucket
//     cannot be unsaid.
//   - **a trigger arriving mid-grouping is refused and counted** — the
//     loop-back cord, .uzi's re-entrant start rule.
//   - **the grouping crosses the control/audio boundary and nothing
//     allocates.** In-patcher delivery dispatches on T_DSP, so "the audio
//     thread asks for the grouping" is the ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names an array uses names of its own — one case's contents
// must not be visible to the next.

#include <doctest/doctest.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "patcher/genericObjects/gArray.h"
#include "patcher/genericObjects/gArrayGroup.h"
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
using YSE::PATCHER::gArrayGroup;

namespace {

  // Records every message it receives, in order and with its type, into a
  // shared log — the grouping's output *is* a sequence (buckets in
  // first-occurrence order, done bang last, each typed), so a sink that only
  // kept the last message could not tell a complete grouping from a
  // truncated one, and one that did not record types could not tell a
  // one-member bucket's int from a two-member bucket's list. `prefix` tells
  // two rigs apart in one log, and `onAny` lets a case act from *inside* the
  // grouping — the mid-grouping mutation and the loop-back trigger are both
  // things the buckets' own subgraph does.
  struct SeqSink : YSE::PATCHER::pObject {
    std::vector<std::string>* log = nullptr;
    std::string prefix;
    std::function<void()> onAny;

    SeqSink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) {
        if (log != nullptr) log->push_back(prefix + "i:" + std::to_string(v));
        if (onAny) onAny();
      });
      inputs.back().RegisterFloat([this](float v, int, YSE::THREAD) {
        if (log != nullptr) log->push_back(prefix + "f:" + std::to_string(static_cast<int>(v * 2)));
        if (onAny) onAny();
      });
      inputs.back().RegisterList([this](const std::string& v, int, YSE::THREAD) {
        if (log != nullptr) log->push_back(prefix + "l:" + v);
        if (onAny) onAny();
      });
      inputs.back().RegisterBang([this](int, YSE::THREAD) {
        if (log != nullptr) log->push_back(prefix + "<done>");
      });
    }
    const char* Type() const override {
      return "seq_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // An .array and an .array.group on one name, sharing one
  // patcherImplementation so the name actually binds ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // sinks are declared before the objects so they are torn down last, while
  // the outlets wired to them still exist (see sinks.hpp on why that
  // matters). One sink per outlet, both writing one log, so the bucket/done
  // order is asserted rather than assumed.
  struct Rig {
    std::vector<std::string> events;
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    SeqSink groupOut;
    SeqSink doneOut;
    gArray array;
    gArrayGroup group;

    Rig(const std::string& patcherName, const std::string& name) {
      p.SetName(patcherName);
      array.SetParent(&p);
      array.SetParams(name);
      group.SetParent(&p);
      group.SetParams(name);
      groupOut.log = &events;
      doneOut.log = &events;
      Wire(group, 0, groupOut);
      Wire(group, 1, doneOut);
    }

    void Store(const std::string& message) {
      array.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void Run(YSE::THREAD thread = YSE::T_GUI) {
      events.clear();
      group.GetInlet(0)->SetBang(thread);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("array.group: registered, two inlets, two outlets (#801)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY_GROUP);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".array.group");
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 2);

    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_ARRAY_GROUP)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("array.group: the trigger takes bang and list and no bare number, the reference "
            "inlet only list text (#801)") {
    // No int or float handler anywhere: a bare number names no array, and an
    // object that streamed up to 256 messages on any stray number that
    // reached it would be a trap — .uzi's reasoning, .array.iter's
    // precedent.
    gArrayGroup g;
    const unsigned int trigger = g.GetInlet(0)->GetAcceptedTypes();
    CHECK((trigger & YSE::PATCHER::IT_BANG) != 0);
    CHECK((trigger & YSE::PATCHER::IT_LIST) != 0);
    CHECK((trigger & YSE::PATCHER::IT_INT) == 0);
    CHECK((trigger & YSE::PATCHER::IT_FLOAT) == 0);
    const unsigned int ref = g.GetInlet(1)->GetAcceptedTypes();
    CHECK((ref & YSE::PATCHER::IT_LIST) != 0);
    CHECK((ref & YSE::PATCHER::IT_BANG) == 0);
  }

  // ─── the grouping ───────────────────────────────────────────────────────────

  TEST_CASE("array.group: buckets by value, first-occurrence order, whole and typed, done "
            "bang last (#801)") {
    // The operation itself: one send per distinct value, the bucket whole —
    // the value repeated as often as it occurs, so the length is the
    // frequency — buckets in the order each value first appears, a bucket of
    // one typed as the int, float or symbol it spells (SendAtoms' rule) and
    // a bucket of several as one list. The done bang after the last bucket,
    // never before.
    Rig rig("ag801a", "a801a");
    rig.Store("append 60 64 60 kick 2.5 2.5");

    rig.Run();
    REQUIRE(rig.events.size() == 5);
    CHECK(rig.events[0] == "l:60 60");
    CHECK(rig.events[1] == "i:64");
    CHECK(rig.events[2] == "l:kick");
    CHECK(rig.events[3] == "l:2.5 2.5");
    CHECK(rig.events[4] == "<done>");
    CHECK(rig.group.Dropped() == 0);
  }

  TEST_CASE("array.group: equality is the spelling — 7 and 7. are different values (#801)") {
    // .array.mode's rule, the family's byte compare: a value is the atom it
    // spells, so 7 and 7. bucket apart — they are a different atom
    // downstream, and a grouping that merged them would emit a bucket whose
    // members it never held.
    Rig rig("ag801b", "a801b");
    rig.Store("append 7 7. 7");

    rig.Run();
    REQUIRE(rig.events.size() == 3);
    CHECK(rig.events[0] == "l:7 7");
    CHECK(rig.events[1] == "f:14"); // 7., logged doubled to dodge float text
    CHECK(rig.events[2] == "<done>");
    CHECK(rig.group.Dropped() == 0);
  }

  TEST_CASE("array.group: an empty array is just the done bang (#801)") {
    // A grouping of nothing is no buckets, not an error — .uzi's rule for a
    // count of zero, .array.iter's for an empty walk: the "and afterwards,
    // do this" branch is not silently skipped. The same holds for an unnamed
    // .array.group, whose private array is empty by construction.
    Rig rig("ag801c", "a801c");
    rig.Run();
    REQUIRE(rig.events.size() == 1);
    CHECK(rig.events[0] == "<done>");
    CHECK(rig.group.Dropped() == 0);

    std::vector<std::string> events;
    SeqSink groupOut;
    SeqSink doneOut;
    groupOut.log = &events;
    doneOut.log = &events;
    gArrayGroup unnamed;
    Wire(unnamed, 0, groupOut);
    Wire(unnamed, 1, doneOut);
    CHECK(unnamed.ArrayName().empty());
    CHECK(unnamed.Address().empty());
    unnamed.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(events.size() == 1);
    CHECK(events[0] == "<done>");
    CHECK(unnamed.Dropped() == 0);
  }

  TEST_CASE("array.group: the reference triggers on the trigger inlet, acknowledges on its "
            "own, and anything else is refused (#801)") {
    Rig rig("ag801d", "a801d");
    rig.Store("append lead piano lead");

    // The array's own reference — the message its .array emits on a bang —
    // groups, exactly as a bang does.
    const std::uint64_t before = rig.group.Dropped();
    rig.events.clear();
    rig.group.GetInlet(0)->SetList("array a801d", YSE::T_GUI);
    REQUIRE(rig.events.size() == 3);
    CHECK(rig.events[0] == "l:lead lead");
    CHECK(rig.events[1] == "l:piano");
    CHECK(rig.events[2] == "<done>");
    CHECK(rig.group.Dropped() == before);

    // A reference to an array this object is not bound to, and any other
    // message, are refused and counted, never resolved: a registry lookup is
    // a mutex, and this may be the audio thread. A refused grouping emits
    // nothing — no buckets and no done bang.
    rig.events.clear();
    rig.group.GetInlet(0)->SetList("array somewhere_else", YSE::T_GUI);
    rig.group.GetInlet(0)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK(rig.events.empty());
    CHECK(rig.group.Dropped() == before + 2);

    // The reference inlet acknowledges the bound array silently and refuses
    // anything else — gDictSlice's inlet rule. Neither triggers a grouping.
    rig.group.GetInlet(1)->SetList("array a801d", YSE::T_GUI);
    CHECK(rig.group.Dropped() == before + 2);
    rig.group.GetInlet(1)->SetList("array somewhere_else", YSE::T_GUI);
    CHECK(rig.group.Dropped() == before + 3);
    CHECK(rig.events.empty());
  }

  // ─── whole buckets or nothing ───────────────────────────────────────────────

  TEST_CASE("array.group: a bucket that cannot leave whole refuses the whole grouping before "
            "anything is sent (#801)") {
    // The transport bound: a bucket is the value repeated, and seventeen
    // copies of a 64-character element spell more text than a cord carries
    // (17 * 64 > 1024). A bucket that lost members would lie about the
    // value's frequency — and by the time it is discovered, earlier buckets
    // could already have been sent and cannot be unsaid — so the fit of
    // every bucket is proven before the first send: one counted refusal,
    // nothing emitted, no done bang, and the fitting bucket ("60") does not
    // leave either. .array.sect's whole-refusal rule.
    Rig rig("ag801e", "a801e");
    const std::string wide(64, 'x');
    rig.Store("append 60");
    const std::string four = "append " + wide + " " + wide + " " + wide + " " + wide;
    rig.Store(four);
    rig.Store(four);
    rig.Store(four);
    rig.Store(four);
    rig.Store("append " + wide);
    CHECK(rig.array.Count() == 18); // 60, then 17 copies of the wide symbol

    const std::uint64_t before = rig.group.Dropped();
    rig.Run();
    CHECK(rig.events.empty());
    CHECK(rig.group.Dropped() == before + 1);

    // Sixteen copies fit exactly (16 * 64 = 1024) — the bound is what a cord
    // carries, not one less than it.
    rig.Store("delete 17");
    rig.Run();
    REQUIRE(rig.events.size() == 3);
    CHECK(rig.events[0] == "i:60");
    CHECK(rig.events[1].size() == 2 + 16 * 65 - 1); // "l:" + 16 tokens, 15 spaces
    CHECK(rig.events[2] == "<done>");
    CHECK(rig.group.Dropped() == before + 1);
  }

  // ─── the snapshot, and re-entrancy ──────────────────────────────────────────

  TEST_CASE("array.group: the grouping is a snapshot — a mid-grouping renumbering write "
            "moves the store, not the buckets (#801)") {
    // .array.iter's decision (#798), inherited deliberately and proven from
    // the position that forces it: the buckets' own subgraph writes into the
    // array being grouped. From inside the first bucket's send a delete
    // removes an element — renumbering everything above it, the hazard #548
    // names — and an append adds a new one. The grouping still emits every
    // bucket the array held at the trigger, whole, because it buckets the
    // snapshot the trigger took, not the live table. The store itself has
    // moved: the deleted element is gone and the appended one is there,
    // which is also the proof that no guard was held across the sends.
    Rig rig("ag801f", "a801f");
    rig.Store("append c4 e4 c4");

    bool acted = false;
    rig.groupOut.onAny = [&] {
      if (acted) return;
      acted = true;
      rig.Store("delete 2");
      rig.Store("append b4");
    };

    rig.Run();
    REQUIRE(rig.events.size() == 3);
    CHECK(rig.events[0] == "l:c4 c4");
    CHECK(rig.events[1] == "l:e4");
    CHECK(rig.events[2] == "<done>");
    CHECK(rig.group.Dropped() == 0);
    CHECK(rig.array.Count() == 3);
    CHECK(rig.array.ElementAt(0) == "c4");
    CHECK(rig.array.ElementAt(1) == "e4");
    CHECK(rig.array.ElementAt(2) == "b4");
  }

  TEST_CASE("array.group: a trigger arriving mid-grouping is refused and counted (#801)") {
    // The loop-back: a cord from the group outlet round to the inlet
    // re-enters the handler from inside the grouping, and letting it through
    // would restart the grouping and rewrite the snapshot being bucketed.
    // Refused and counted — .uzi's re-entrant start rule — and the running
    // grouping completes untouched.
    Rig rig("ag801g", "a801g");
    rig.Store("append 1 2");

    bool acted = false;
    rig.groupOut.onAny = [&] {
      if (acted) return;
      acted = true;
      rig.group.GetInlet(0)->SetBang(YSE::T_GUI);
    };

    const std::uint64_t before = rig.group.Dropped();
    rig.Run();
    REQUIRE(rig.events.size() == 3);
    CHECK(rig.events[0] == "i:1");
    CHECK(rig.events[1] == "i:2");
    CHECK(rig.events[2] == "<done>");
    CHECK(rig.group.Dropped() == before + 1);
  }

  TEST_CASE("array.group: two .array.group on one name group independently (#801)") {
    // .coll's per-object pointer rule, in the shape it takes here: the
    // buckets are built from the object's own snapshot, so a second
    // .array.group triggered from inside the first one's grouping — on the
    // very same name — runs whole, nested, and neither disturbs the other.
    // Nothing about a grouping lives in the shared store.
    std::vector<std::string> events;
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ag801h");
    SeqSink groupA;
    SeqSink doneA;
    SeqSink groupB;
    SeqSink doneB;
    groupA.log = &events;
    doneA.log = &events;
    groupB.log = &events;
    doneB.log = &events;
    groupA.prefix = "A:";
    doneA.prefix = "A:";
    groupB.prefix = "B:";
    doneB.prefix = "B:";

    gArray array;
    gArrayGroup groupObjA;
    gArrayGroup groupObjB;
    array.SetParent(&p);
    array.SetParams("a801h");
    groupObjA.SetParent(&p);
    groupObjA.SetParams("a801h");
    groupObjB.SetParent(&p);
    groupObjB.SetParams("a801h");
    Wire(groupObjA, 0, groupA);
    Wire(groupObjA, 1, doneA);
    Wire(groupObjB, 0, groupB);
    Wire(groupObjB, 1, doneB);

    array.GetInlet(0)->SetList("append 1 2 1", YSE::T_GUI);

    bool acted = false;
    groupA.onAny = [&] {
      if (acted) return;
      acted = true;
      groupObjB.GetInlet(0)->SetBang(YSE::T_GUI);
    };

    groupObjA.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(events.size() == 6);
    CHECK(events[0] == "A:l:1 1");
    CHECK(events[1] == "B:l:1 1");
    CHECK(events[2] == "B:i:2");
    CHECK(events[3] == "B:<done>");
    CHECK(events[4] == "A:i:2");
    CHECK(events[5] == "A:<done>");
    CHECK(groupObjA.Dropped() == 0);
    CHECK(groupObjB.Dropped() == 0);
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("array.group: the address form is the patcher's, and RefreshBinding follows it "
            "(#801)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ag801i_before");

    gArrayGroup g;
    g.SetParams("a801i");
    g.SetParent(&p);
    CHECK(g.ArrayName() == "a801i");
    CHECK(g.Address() == "ag801i_before.a801i");

    // Idempotent: a rebind to the address it already has keeps the store.
    g.RefreshBinding();
    CHECK(g.Address() == "ag801i_before.a801i");

    p.SetName("ag801i_after");
    g.RefreshBinding();
    CHECK(g.Address() == "ag801i_after.a801i");
  }

  TEST_CASE("array.group: patcherImplementation::SetName re-anchors it (#801)") {
    // The rename dispatch itself, which the standalone case above cannot
    // reach: an object created *inside* a patcher must be re-anchored by the
    // patcher, without anybody calling RefreshBinding by hand. The keeper
    // holds the old-address store (it is not in the patcher's object map, so
    // the rename does not touch it): before the rename a bang buckets the
    // keeper's elements; after it the grouping reads a fresh empty array
    // under the new prefix, so a second bang is the done bang alone.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ag801j_before");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a801j");
    keeper.GetInlet(0)->SetList("append lead lead", YSE::T_GUI);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY_GROUP, "a801j");
    REQUIRE(h != nullptr);
    p.Connect(h, 0, &sinkHandle, 0);
    p.Connect(h, 1, &sinkHandle, 0);

    h->SetBang(0);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "lead lead");
    CHECK(sink.gotBang);

    p.SetName("ag801j_after");
    sink.reset();
    h->SetBang(0);
    CHECK_FALSE(sink.gotList);
    CHECK(sink.gotBang);
    CHECK(keeper.Count() == 2);
  }

  TEST_CASE("array.group: wired from the array's reference outlet, banging the array buckets "
            "(#801)") {
    // The flow a patch actually wires, end to end through the public patcher
    // API: the .array's reference outlet into the grouping, a bang on the
    // .array, and the buckets at the far end. The family's gesture — bang
    // the array, out come the groups.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    BangSink done;
    YSE::pHandle doneHandle(&done);
    YSE::patcher p;
    p.create(2);
    p.name("ag801k");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "a801k");
    YSE::pHandle* group = p.CreateObject(YSE::OBJ::G_ARRAY_GROUP, "a801k");
    REQUIRE(array != nullptr);
    REQUIRE(group != nullptr);
    p.Connect(array, 1, group, 0);
    p.Connect(group, 0, &sinkHandle, 0);
    p.Connect(group, 1, &doneHandle, 0);

    array->SetListData(0, "append 7 7 9");
    array->SetBang(0);
    // The 7-bucket leaves first as a list, then the one-member 9 typed as
    // the int it is, then the done bang.
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "7 7");
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 9);
    CHECK(done.gotBang);
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("array.group: a grouping asked for over in-patcher delivery lands on T_DSP "
            "(#801)") {
    // A .r feeding the grouping dispatches on T_DSP when the block drains it
    // (issue #225) — "the audio thread asks for the grouping" is the
    // ordinary case, and the whole path is a snapshot, bounded compares and
    // typed sends over storage the object already owns.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ag801l");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a801l");
    keeper.GetInlet(0)->SetList("append 60 64 60", YSE::T_GUI);

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go801l");
    YSE::pHandle* group = p.CreateObject(YSE::OBJ::G_ARRAY_GROUP, "a801l");
    REQUIRE(recv != nullptr);
    REQUIRE(group != nullptr);
    p.Connect(recv, 0, group, 0);
    p.Connect(group, 0, &sinkHandle, 0);
    p.Connect(group, 1, &sinkHandle, 0);

    p.PassData(std::string("array a801l"), "go801l", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "60 60"); // the two-member bucket, whole
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 64); // the one-member bucket, typed
    CHECK(sink.gotBang);
  }

  TEST_CASE("array.group: no message path allocates (#801)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path: the grouping (bang and reference alike — a
    // multi-member bucket through the render path plus int, float and symbol
    // one-member buckets through all three SendAtom paths, and the done bang
    // out), the empty grouping, the whole-refusal of an oversized bucket,
    // the wrong-name refusal, the unknown message and the reference-inlet
    // acknowledgement — on T_DSP, in-patcher delivery's thread.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string reference = "array probeAG801";
    const std::string wrongName = "array somewhere_else_long";
    const std::string unknown = "frobnicate something quite long indeed";
    const std::string bigReference = "array probeAG801big";

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ag801m");
    MultiSink groupOut;
    BangSink doneOut;
    MultiSink emptyGroup;
    BangSink emptyDone;
    MultiSink bigGroup;
    BangSink bigDone;
    gArray array;
    gArrayGroup group;
    gArrayGroup unnamed;
    gArray bigArray;
    gArrayGroup bigGrouper;
    array.SetParent(&p);
    array.SetParams("probeAG801");
    group.SetParent(&p);
    group.SetParams("probeAG801");
    unnamed.SetParent(&p);
    bigArray.SetParent(&p);
    bigArray.SetParams("probeAG801big");
    bigGrouper.SetParent(&p);
    bigGrouper.SetParams("probeAG801big");
    Wire(group, 0, groupOut);
    Wire(group, 1, doneOut);
    Wire(unnamed, 0, emptyGroup);
    Wire(unnamed, 1, emptyDone);
    Wire(bigGrouper, 0, bigGroup);
    Wire(bigGrouper, 1, bigDone);

    // A float and a symbol and an int (all three of SendAtom's one-member
    // paths run under the probe — the symbol wide enough to outgrow every
    // small-string buffer the scratch assign must absorb) plus a repeated
    // int, whose bucket runs the render path and, first-occurring third,
    // leaves as the last list the sink keeps.
    array.GetInlet(0)->SetList("append 2.5 a_symbol_past_every_small_string_buffer 60 9 60",
                               YSE::T_GUI);

    // And an array whose one bucket cannot leave whole — seventeen copies of
    // a 64-character element — so the whole-refusal path runs under the
    // probe too.
    const std::string wide(64, 'y');
    const std::string four = "append " + wide + " " + wide + " " + wide + " " + wide;
    bigArray.GetInlet(0)->SetList(four, YSE::T_GUI);
    bigArray.GetInlet(0)->SetList(four, YSE::T_GUI);
    bigArray.GetInlet(0)->SetList(four, YSE::T_GUI);
    bigArray.GetInlet(0)->SetList(four, YSE::T_GUI);
    bigArray.GetInlet(0)->SetList("append " + wide, YSE::T_GUI);

    // Warm every path — including the sinks' assignments — so first-call
    // machinery is not what the probe catches.
    group.GetInlet(0)->SetBang(YSE::T_GUI);
    unnamed.GetInlet(0)->SetBang(YSE::T_GUI);
    bigGrouper.GetInlet(0)->SetBang(YSE::T_GUI);
    group.GetInlet(0)->SetList(reference, YSE::T_GUI);
    group.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    group.GetInlet(0)->SetList(unknown, YSE::T_GUI);
    group.GetInlet(1)->SetList(reference, YSE::T_GUI);
    bigGrouper.GetInlet(0)->SetList(bigReference, YSE::T_GUI);
    const std::uint64_t before = group.Dropped();
    const std::uint64_t bigBefore = bigGrouper.Dropped();
    const int bangs = doneOut.bangCount;
    const int emptyBangs = emptyDone.bangCount;
    const int bigBangs = bigDone.bangCount;

    groupOut.reset();
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      group.GetInlet(0)->SetBang(YSE::T_DSP);
      unnamed.GetInlet(0)->SetBang(YSE::T_DSP);
      bigGrouper.GetInlet(0)->SetBang(YSE::T_DSP);
      group.GetInlet(0)->SetList(reference, YSE::T_DSP);
      group.GetInlet(0)->SetList(wrongName, YSE::T_DSP);
      group.GetInlet(0)->SetList(unknown, YSE::T_DSP);
      group.GetInlet(1)->SetList(reference, YSE::T_DSP);
      bigGrouper.GetInlet(0)->SetList(bigReference, YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing. Two groupings ran to their done bang (the
    // repeated-int bucket as a list plus all three one-member types out),
    // the empty grouping banged, both oversized groupings were refused
    // whole (no done bang), and both bad messages were counted.
    CHECK(groupOut.gotList);
    CHECK(groupOut.listValue == "60 60");
    CHECK(groupOut.gotFloat);
    CHECK(groupOut.gotInt);
    CHECK(groupOut.intValue == 9);
    CHECK(doneOut.bangCount == bangs + 2);
    CHECK(emptyDone.bangCount == emptyBangs + 1);
    CHECK(bigDone.bangCount == bigBangs);
    CHECK_FALSE(bigGroup.gotList);
    CHECK(group.Dropped() == before + 2);
    CHECK(bigGrouper.Dropped() == bigBefore + 2);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("array.group: params survive a DumpJSON / ParseJSON round trip (#801)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_ARRAY_GROUP, "bucketme801");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".array.group"));
    CHECK(copy->GetParams() == std::string("bucketme801"));
    CHECK(copy->GetInputs() == 2);
    CHECK(copy->GetOutputs() == 2);
  }

  TEST_CASE("array.group: carries complete documentation metadata (#801)") {
    gArrayGroup g;
    CHECK_FALSE(g.GetDescription().empty());
    CHECK(g.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    const auto& docs = g.GetParamDocs();
    REQUIRE(docs.size() == 1);
    CHECK(docs[0].name == "name");
  }
}

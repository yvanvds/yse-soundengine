// Tests for `.unjoin` (issue #520) — the object that cuts a list into equal
// groups, one per outlet, with everything left over out the rightmost one.
//
// What is being pinned:
//
//   - **the shape**: the first creation argument is the number of *group*
//     outlets and the object has one more outlet than that, the last of them
//     the remainder; the second argument is Max's `@outsize`;
//   - **complete groups only**: a group outlet is whole or silent, and
//     everything from the first incomplete group onwards leaves by the
//     remainder — which is why this object drops nothing for want of an outlet,
//     unlike `.unpack`;
//   - **the order**: right to left, the remainder first;
//   - **no bang**: Max documents none, and there is no held list for one to
//     re-send;
//   - **the inherited transport**: a group of one atom leaves as the value it
//     spells, and an empty remainder sends nothing at all;
//   - **the inherited bound**: an over-long list loses its *tail*, which is
//     counted — `.zl`'s rule, because the surplus here is input rather than
//     state.
//
// The unit-level cases drive standalone objects, which is what this object
// needs (no patcher, no clock, no scheduler). The end-to-end section at the
// bottom drives a real `YSE::patcher` graph through `pHandle`; the `.join`
// round trip lives in test_patcher_join.cpp.
//
// No audio device required.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include <vector>

#include "patcher/genericObjects/gUnjoin.h"
#include "patcher/inlet.h"
#include "patcher/pAtomList.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using TestHelpers::MultiSink;
using TestHelpers::OrderSink;
using YSE::PATCHER::AtomList;
using YSE::PATCHER::gUnjoin;

namespace {

  // One order-logging sink per outlet, all sharing one log, so "which outlet
  // fired, with what, and in what order" is an assertion rather than an
  // inference. The `.trigger` rig (#466), because this object makes the same
  // right-to-left promise and a count-only test cannot tell a right-to-left
  // object from a left-to-right one.
  struct Rig {
    std::unique_ptr<gUnjoin> op;
    std::vector<std::unique_ptr<OrderSink>> sinks;
    std::vector<char> order;

    explicit Rig(const std::string& args = "") : op(new gUnjoin()) {
      // The first argument *is* the outlet shape, so it has to be set before
      // the wiring: ShapePorts() rebuilds the outlets, and a cord attached to
      // an old one goes with it.
      if (!args.empty()) op->SetParams(args);
      Wire();
    }

    void Wire() {
      sinks.clear();
      order.clear();
      // Reserved once, so the allocation case below is measuring the object
      // rather than this log growing under it.
      order.reserve(1024);
      for (int i = 0; i < op->NumOutputs(); i++) {
        sinks.push_back(std::unique_ptr<OrderSink>(new OrderSink()));
        // 'a' for outlet 0, 'b' for outlet 1, ... so the log reads left to
        // right in *outlet* order and a right-to-left firing shows up reversed.
        sinks.back()->tag = (char)('a' + (i % 26));
        sinks.back()->log = &order;
        // Both ends, inlet first — TestHelpers::Wire's rule, spelled out here
        // because the sinks are held by pointer.
        REQUIRE(sinks.back()->ConnectInlet(op->GetOutlet(i), 0));
        op->ConnectOutlet(sinks.back()->GetInlet(0), i);
      }
    }

    std::string Log() const {
      return std::string(order.begin(), order.end());
    }

    void Reset() {
      order.clear();
      for (auto& sink : sinks)
        sink->count = 0;
    }

    void SendList(const std::string& text) {
      op->GetInlet(0)->SetList(text, YSE::T_GUI);
    }
    void SendInt(int value) {
      op->GetInlet(0)->SetInt(value, YSE::T_GUI);
    }
    void SendFloat(float value) {
      op->GetInlet(0)->SetFloat(value, YSE::T_GUI);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape and registration ───────────────────────────────────────────────

  TEST_CASE("unjoin: registered, and the argument counts the outlets beside the rest (#520)") {
    // Max: the argument is "the number of outlets beyond the rightmost outlet",
    // so a `.unjoin 3` has four.
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::G_UNJOIN, "3");
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == ".unjoin");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 4);
  }

  TEST_CASE("unjoin: appears in the registry's name list (#520)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_UNJOIN)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("unjoin: with no arguments it is Max's default — two groups of one (#520)") {
    Rig rig;
    CHECK(rig.op->GroupCount() == gUnjoin::DEFAULT_GROUPS);
    CHECK(rig.op->GroupSize() == gUnjoin::DEFAULT_SIZE);
    CHECK(rig.op->NumOutputs() == gUnjoin::DEFAULT_GROUPS + 1);
    CHECK(rig.op->NumInputs() == 1);
  }

  TEST_CASE("unjoin: both arguments are clamped, and the clamp is reported by shape (#520)") {
    gUnjoin obj;
    obj.SetParams("500 500");
    CHECK(obj.GroupCount() == gUnjoin::MAX_GROUPS);
    CHECK(obj.GroupCount() == (int)AtomList::MAX_ATOMS);
    CHECK(obj.GroupSize() == gUnjoin::MAX_SIZE);
    CHECK(obj.NumOutputs() == gUnjoin::MAX_GROUPS + 1);

    // A group of no items and an object of no group outlets are both nonsense.
    obj.SetParams("0 0");
    CHECK(obj.GroupCount() == gUnjoin::MIN_GROUPS);
    CHECK(obj.GroupSize() == 1);
  }

  TEST_CASE("unjoin: an argument that is not a number leaves the default (#520)") {
    gUnjoin obj;
    obj.SetParams("wide big");
    CHECK(obj.GroupCount() == gUnjoin::DEFAULT_GROUPS);
    CHECK(obj.GroupSize() == gUnjoin::DEFAULT_SIZE);
  }

  TEST_CASE("unjoin: SetParams(\"\") returns the object to its no-argument shape (#520)") {
    gUnjoin obj;
    obj.SetParams("4 3");
    REQUIRE(obj.GroupCount() == 4);
    REQUIRE(obj.GroupSize() == 3);

    obj.SetParams("");
    CHECK(obj.GroupCount() == gUnjoin::DEFAULT_GROUPS);
    CHECK(obj.GroupSize() == gUnjoin::DEFAULT_SIZE);
    CHECK(obj.NumOutputs() == gUnjoin::DEFAULT_GROUPS + 1);
  }

  // ─── the split ────────────────────────────────────────────────────────────

  TEST_CASE("unjoin: a list divides into equal groups, one per outlet (#520)") {
    Rig rig("2 2");
    rig.SendList("1 2 3 4");
    CHECK(rig.sinks[0]->lastList == "1 2");
    CHECK(rig.sinks[1]->lastList == "3 4");
    // Nothing left over, so the remainder outlet stays silent rather than
    // sending an empty message.
    CHECK(rig.sinks[2]->count == 0);
    CHECK(rig.op->Dropped() == 0);
  }

  TEST_CASE("unjoin: everything past the last group goes out the remainder, whole (#520)") {
    // The property that makes this object drop nothing for want of an outlet,
    // where `.unpack` drops and counts.
    Rig rig("2 2");
    rig.SendList("1 2 3 4 5 6 7");
    CHECK(rig.sinks[0]->lastList == "1 2");
    CHECK(rig.sinks[1]->lastList == "3 4");
    CHECK(rig.sinks[2]->lastList == "5 6 7");
    CHECK(rig.op->Dropped() == 0);
  }

  TEST_CASE("unjoin: a group is complete or silent, and the short tail leaves by the rest (#520)") {
    // Max's "the rightmost outlet receives remaining items that don't fill a
    // complete group", read literally. Outlet 1 has no complete group to send,
    // so it says nothing at all rather than a short piece.
    Rig rig("2 2");
    rig.SendList("1 2 3");
    CHECK(rig.sinks[0]->lastList == "1 2");
    CHECK(rig.sinks[1]->count == 0);
    CHECK(rig.sinks[2]->lastKind == OrderSink::INT);
    CHECK(rig.sinks[2]->lastInt == 3);
  }

  TEST_CASE("unjoin: a list shorter than one group is all remainder (#520)") {
    Rig rig("2 3");
    rig.SendList("1 2");
    CHECK(rig.sinks[0]->count == 0);
    CHECK(rig.sinks[1]->count == 0);
    CHECK(rig.sinks[2]->lastList == "1 2");
  }

  TEST_CASE("unjoin: at the default group size it is .unpack with a remainder (#520)") {
    Rig rig("2");
    rig.SendList("10 20 30 40");
    CHECK(rig.sinks[0]->lastKind == OrderSink::INT);
    CHECK(rig.sinks[0]->lastInt == 10);
    CHECK(rig.sinks[1]->lastInt == 20);
    // Where `.unpack 0 0` would have dropped two items and counted them.
    CHECK(rig.sinks[2]->lastList == "30 40");
    CHECK(rig.op->Dropped() == 0);
  }

  TEST_CASE("unjoin: outlets fire right to left, the remainder first (#520)") {
    // Max's universal order, and the one every patch wiring right-hand pieces
    // into cold inlets depends on.
    Rig rig("3");
    rig.SendList("1 2 3 4");
    CHECK(rig.Log() == "dcba");
  }

  TEST_CASE("unjoin: an int or a float is a one-item list (#520)") {
    // Max: "int: Number sent to left outlet" — true at the default group size,
    // which is where Max says it.
    Rig rig("2");
    rig.SendInt(7);
    CHECK(rig.sinks[0]->lastKind == OrderSink::INT);
    CHECK(rig.sinks[0]->lastInt == 7);
    CHECK(rig.sinks[2]->count == 0);

    rig.Reset();
    rig.SendFloat(1.5f);
    CHECK(rig.sinks[0]->lastKind == OrderSink::FLOAT);
    CHECK(rig.sinks[0]->lastFloat == doctest::Approx(1.5f));

    // At a larger group size there is no complete group to put it in, so it
    // leaves by the remainder instead.
    Rig wide("2 2");
    wide.SendInt(7);
    CHECK(wide.sinks[0]->count == 0);
    CHECK(wide.sinks[2]->lastKind == OrderSink::INT);
    CHECK(wide.sinks[2]->lastInt == 7);
  }

  TEST_CASE("unjoin: nothing is coerced — a group carries what the list carried (#520)") {
    Rig rig("2 2");
    rig.SendList("1 name 2.5 other");
    CHECK(rig.sinks[0]->lastList == "1 name");
    CHECK(rig.sinks[1]->lastList == "2.5 other");
  }

  TEST_CASE("unjoin: there is no bang, Max documents none and nothing is held (#520)") {
    // The object is a distributor rather than a register: registering a bang it
    // could not answer honestly would misreport the contract, the `.zl` /
    // `.combine` discipline.
    Rig rig("2");
    rig.SendList("1 2 3");
    rig.Reset();

    rig.op->GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Log().empty());
  }

  // ─── the bound ────────────────────────────────────────────────────────────

  TEST_CASE("unjoin: an over-long list loses its tail, and the tail is counted (#520)") {
    // `.zl`'s rule rather than `.join`'s refuse-whole: the surplus here is input
    // the patch has just sent, not state the object was holding.
    Rig rig("2 2");

    std::string many;
    for (std::size_t i = 0; i < AtomList::MAX_ATOMS + 4; i++) {
      if (i > 0) many.push_back(' ');
      many.push_back('7');
    }
    rig.SendList(many);

    CHECK(rig.op->Dropped() == 4);
    // The head still went out: a refusal is not a wedge, and the groups the
    // list did reach are complete.
    CHECK(rig.sinks[0]->lastList == "7 7");
    CHECK(rig.sinks[1]->lastList == "7 7");
    CHECK(rig.sinks[2]->count == 1);

    rig.Reset();
    rig.SendList("1 2 3 4");
    CHECK(rig.sinks[0]->lastList == "1 2");
  }

  // ─── real-time behaviour ──────────────────────────────────────────────────

  TEST_CASE("unjoin: Calculate() emits nothing (#520)") {
    // The object is driven by its inlet; one that emitted here would
    // re-distribute the last list on every DSP tick from a stimulus no patch
    // sent.
    Rig rig("2");
    rig.SendList("1 2 3");
    rig.Reset();

    for (int i = 0; i < 8; i++)
      rig.op->Calculate(YSE::T_DSP);
    CHECK(rig.Log().empty());
  }

  TEST_CASE("unjoin: a cord back into its own inlet is bounded, not fatal (#520)") {
    // The guard is what stops a feedback cord recursing on the audio thread:
    // the returning message finds it taken, is counted, and goes no further.
    gUnjoin obj;
    obj.SetParams("1");
    REQUIRE(obj.ConnectInlet(obj.GetOutlet(0), 0));
    obj.ConnectOutlet(obj.GetInlet(0), 0);

    obj.GetInlet(0)->SetList("5 6", YSE::T_GUI);
    // Exactly one message came back round and was refused.
    CHECK(obj.Dropped() == 1);
  }

  TEST_CASE("unjoin: the message path allocates nothing (#520)") {
    if (!TestHelpers::probeCountsAllocations()) return;
    // The claim covers a path that fills list storage and renders group text,
    // so it only means anything if the probe can see a std::string's own
    // allocations (issue #697).
    if (!TestHelpers::probeSeesStringAllocations()) return;

    Rig rig("3 2");

    // Warm every buffer the path touches — including the sinks', which are test
    // scaffolding rather than the object under test.
    rig.SendList("111 222.5 name other 5 6 7 8");
    rig.SendList("1 2 3");
    rig.SendInt(7);
    rig.SendFloat(1.25f);

    const std::string listText = "111 222.5 name other 5 6 7 8";
    const std::string shortText = "12 13 14";
    {
      TestHelpers::ProbeScope probe;
      rig.SendList(listText);
      rig.SendList(shortText);
      rig.SendInt(5);
      rig.SendFloat(2.5f);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  // ─── persistence ──────────────────────────────────────────────────────────

  TEST_CASE("unjoin: params survive a DumpJSON / ParseJSON round trip (#520)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_UNJOIN, "3 2") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".unjoin") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".unjoin");
    CHECK(copy->GetParams() == std::string("3 2"));
    // The first argument *is* the outlet count, so a round trip that lost it
    // would come back with the wrong number of outlets.
    CHECK(copy->GetInputs() == 1);
    CHECK(copy->GetOutputs() == 4);
  }

  // ─── end to end, through a real patcher graph ─────────────────────────────

  TEST_CASE("unjoin: the split reaches real objects down real cords (#520)") {
    // A standalone rig can assert on the text an outlet carried; only the whole
    // chain shows that the patcher delivered a *list message* the object at the
    // other end reads as a list, and that a one-item remainder arrives as the
    // int it spells rather than as a list of one.
    //
    // Sinks before the patcher: the patcher is torn down first, while the
    // inlets it is wired to still exist.
    MultiSink first;
    MultiSink second;
    MultiSink rest;
    MultiSink counted;
    YSE::pHandle firstHandle(&first);
    YSE::pHandle secondHandle(&second);
    YSE::pHandle restHandle(&rest);
    YSE::pHandle countedHandle(&counted);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* unjoin = p.CreateObject(YSE::OBJ::G_UNJOIN, "2 2");
    REQUIRE(unjoin != nullptr);
    p.Connect(unjoin, 0, &firstHandle, 0);
    p.Connect(unjoin, 1, &secondHandle, 0);
    p.Connect(unjoin, 2, &restHandle, 0);

    unjoin->SetListData(0, "1 2 3 4 5");
    CHECK(first.gotList);
    CHECK(first.listValue == "1 2");
    CHECK(second.gotList);
    CHECK(second.listValue == "3 4");
    // One item over: it leaves as the int it spells, so it reaches the inlets an
    // uncollected value would have reached.
    CHECK(rest.gotInt);
    CHECK(rest.intValue == 5);
    CHECK_FALSE(rest.gotList);

    // A `.zl len` downstream reads a group as a real list rather than as text.
    YSE::pHandle* zl = p.CreateObject(YSE::OBJ::G_ZL, "len");
    REQUIRE(zl != nullptr);
    p.Connect(unjoin, 2, zl, 0);
    p.Connect(zl, 0, &countedHandle, 0);

    unjoin->SetListData(0, "1 2 3 4 5 6 7");
    CHECK(counted.gotInt);
    CHECK(counted.intValue == 3);
  }

} // TEST_SUITE

// Tests for `.join` (issue #520) — the object that concatenates what several
// inlets are holding into one list, over the bounded list model `.zl` settled
// in #523.
//
// What is being pinned:
//
//   - **the shape**: the first creation argument is the inlet count, there is
//     one outlet, and with no arguments the object is Max's default of two
//     inlets holding 0;
//   - **what makes it not `.pack`**: an inlet holds a *whole message* of any
//     length rather than one typed atom, so the output length is the sum of
//     the pieces and nothing is coerced;
//   - **hot and cold**: the leftmost inlet releases by default, the trigger
//     arguments move that, and `-1` makes every inlet hot;
//   - **bang everywhere**: Max's "outputs the currently stored list from any
//     inlet", which is where this parts company with `.pack`;
//   - **the inherited transport**: a one-atom result leaves as the value it
//     spells, and an object with every inlet emptied sends nothing at all;
//   - **the inherited bound**: a store that would not fit is refused *whole*
//     and counted, leaving every inlet holding what it held.
//
// The unit-level cases drive standalone objects, which is what this object
// needs (no patcher, no clock, no scheduler). The end-to-end section at the
// bottom drives a real `YSE::patcher` graph through `pHandle`, because the
// claim that matters to a patch — that `.join` into `.unjoin` is a round trip
// down real cords — cannot be seen from a standalone object at all.
//
// No audio device required.

#include <doctest/doctest.h>
#include <memory>
#include <string>

#include "patcher/genericObjects/gJoin.h"
#include "patcher/inlet.h"
#include "patcher/pAtomList.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using TestHelpers::MultiSink;
using YSE::PATCHER::AtomList;
using YSE::PATCHER::gJoin;

namespace {

  // A `.join` with a sink on its one outlet. The arguments *are* the inlet
  // shape, so they are set before the wiring: ShapePorts() rebuilds the inlets,
  // and a cord attached to an old one goes with it (the #517 gotcha).
  struct Rig {
    std::unique_ptr<gJoin> op;
    std::unique_ptr<MultiSink> out;

    explicit Rig(const std::string& args = "") : op(new gJoin()), out(new MultiSink()) {
      if (!args.empty()) op->SetParams(args);
      // Both ends, inlet first — TestHelpers::Wire's rule, spelled out here
      // because the sink is held by pointer.
      REQUIRE(out->ConnectInlet(op->GetOutlet(0), 0));
      op->ConnectOutlet(out->GetInlet(0), 0);
    }

    void SendList(int inlet, const std::string& text) {
      op->GetInlet(inlet)->SetList(text, YSE::T_GUI);
    }
    void SendInt(int inlet, int value) {
      op->GetInlet(inlet)->SetInt(value, YSE::T_GUI);
    }
    void SendFloat(int inlet, float value) {
      op->GetInlet(inlet)->SetFloat(value, YSE::T_GUI);
    }
    void Bang(int inlet) {
      op->GetInlet(inlet)->SetBang(YSE::T_GUI);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape and registration ───────────────────────────────────────────────

  TEST_CASE("join: registered, one inlet per count argument and a single outlet (#520)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::G_JOIN, "4");
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == ".join");
    CHECK(obj->GetInputs() == 4);
    CHECK(obj->GetOutputs() == 1);
  }

  TEST_CASE("join: appears in the registry's name list (#520)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_JOIN)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("join: with no arguments it is Max's default — two inlets holding 0 (#520)") {
    // Max: "Defaults to two inlets with initial values of 0." So a bang before
    // anything arrives is a complete list rather than silence.
    Rig rig;
    CHECK(rig.op->PortCount() == 2);
    CHECK(rig.op->InletSize(0) == 1);
    CHECK(rig.op->InletSize(1) == 1);
    CHECK(rig.op->Joined() == "0 0");

    rig.Bang(0);
    CHECK(rig.out->gotList);
    CHECK(rig.out->listValue == "0 0");
  }

  TEST_CASE("join: the inlet count is clamped to the shared list's bound (#520)") {
    gJoin obj;
    obj.SetParams("300");
    CHECK(obj.PortCount() == gJoin::MAX_PORTS);
    CHECK(obj.PortCount() == (int)AtomList::MAX_ATOMS);
    CHECK(obj.NumInputs() == gJoin::MAX_PORTS);
    CHECK(obj.NumOutputs() == 1);

    // And upwards from below: an object cannot have no inlets at all.
    obj.SetParams("0");
    CHECK(obj.PortCount() == gJoin::MIN_PORTS);
  }

  TEST_CASE("join: a count argument that is not a number leaves the default shape (#520)") {
    gJoin obj;
    obj.SetParams("wide");
    CHECK(obj.PortCount() == gJoin::DEFAULT_PORTS);
  }

  TEST_CASE("join: SetParams(\"\") returns the object to its no-argument shape (#520)") {
    gJoin obj;
    obj.SetParams("5 -1");
    REQUIRE(obj.PortCount() == 5);
    REQUIRE(obj.Hot(3));

    obj.SetParams("");
    CHECK(obj.PortCount() == gJoin::DEFAULT_PORTS);
    CHECK(obj.Hot(0));
    CHECK_FALSE(obj.Hot(1));
    CHECK(obj.Joined() == "0 0");
  }

  // ─── an inlet holds a whole message, which is what `.pack` does not ───────

  TEST_CASE("join: an inlet stores its message whole, so the list grows (#520)") {
    // The object's entire reason for existing: `.pack 0 0` handed the same two
    // messages sends `1 3`, because there each item after the first spreads
    // rightwards into the next slot. Here the pieces are laid end to end.
    Rig rig("2");

    rig.SendList(1, "3 4");
    CHECK_FALSE(rig.out->gotList);
    CHECK(rig.op->InletSize(1) == 2);

    rig.SendList(0, "1 2");
    CHECK(rig.out->gotList);
    CHECK(rig.out->listValue == "1 2 3 4");
    CHECK(rig.op->InletSize(0) == 2);
  }

  TEST_CASE("join: storing replaces the piece rather than appending to it (#520)") {
    Rig rig("2");
    rig.SendList(0, "1 2 3");
    CHECK(rig.out->listValue == "1 2 3 0");

    rig.SendList(0, "9");
    CHECK(rig.out->listValue == "9 0");
    CHECK(rig.op->InletSize(0) == 1);
  }

  TEST_CASE("join: nothing is coerced — Max's 'separate untyped items' (#520)") {
    // No creation argument declares a type here, and none is invented: a symbol
    // sits in the list beside the numbers, verbatim.
    Rig rig("3");
    rig.SendList(2, "name");
    rig.SendFloat(1, 2.5f);
    rig.SendInt(0, 7);
    CHECK(rig.out->listValue == "7 2.5 name");
  }

  // ─── hot and cold ─────────────────────────────────────────────────────────

  TEST_CASE("join: by default only the leftmost inlet releases (#520)") {
    Rig rig("3");
    CHECK(rig.op->Hot(0));
    CHECK_FALSE(rig.op->Hot(1));
    CHECK_FALSE(rig.op->Hot(2));

    rig.SendInt(2, 30);
    rig.SendInt(1, 20);
    CHECK_FALSE(rig.out->gotList);

    rig.SendInt(0, 10);
    CHECK(rig.out->listValue == "10 20 30");
  }

  TEST_CASE("join: a trigger argument of -1 makes every inlet hot (#520)") {
    // Max's "@triggers ... setting to -1 makes all inlets hot", carried as a
    // creation argument because the patcher has no attributes.
    Rig rig("3 -1");
    CHECK(rig.op->Hot(0));
    CHECK(rig.op->Hot(1));
    CHECK(rig.op->Hot(2));

    rig.SendInt(2, 30);
    CHECK(rig.out->listValue == "0 0 30");
    rig.SendInt(1, 20);
    CHECK(rig.out->listValue == "0 20 30");
  }

  TEST_CASE("join: a trigger argument names one inlet, and can exclude the leftmost (#520)") {
    Rig rig("3 2");
    CHECK_FALSE(rig.op->Hot(0));
    CHECK_FALSE(rig.op->Hot(1));
    CHECK(rig.op->Hot(2));

    // Writing the leftmost inlet now stores and stays quiet.
    rig.SendInt(0, 10);
    CHECK_FALSE(rig.out->gotList);
    rig.SendInt(2, 30);
    CHECK(rig.out->listValue == "10 0 30");
  }

  TEST_CASE("join: a trigger naming no inlet is ignored, leaving the default (#520)") {
    gJoin obj;
    obj.SetParams("2 7");
    CHECK(obj.Hot(0));
    CHECK_FALSE(obj.Hot(1));
  }

  // ─── bang and set ─────────────────────────────────────────────────────────

  TEST_CASE("join: a bang releases from any inlet, hot or cold (#520)") {
    // Max: "bang: Outputs the currently stored list from any inlet." This is
    // where the object parts company with `.pack`, whose bang lives on its
    // releasing inlets only.
    Rig rig("3");
    rig.SendInt(1, 20);
    REQUIRE_FALSE(rig.out->gotList);

    rig.Bang(2);
    CHECK(rig.out->gotList);
    CHECK(rig.out->listValue == "0 20 0");

    rig.out->reset();
    rig.Bang(0);
    CHECK(rig.out->listValue == "0 20 0");
  }

  TEST_CASE("join: 'set' stores without releasing, on a hot inlet (#520)") {
    // Max: "set: Stores list array without triggering output."
    Rig rig("2");
    rig.SendList(0, "set 1 2");
    CHECK_FALSE(rig.out->gotList);
    CHECK(rig.op->Joined() == "1 2 0");

    rig.Bang(0);
    CHECK(rig.out->listValue == "1 2 0");
  }

  TEST_CASE("join: 'set' with nothing after it empties an inlet (#520)") {
    // An emptied inlet contributes nothing at all, which is the only way the
    // joined list gets shorter than the inlet count.
    Rig rig("2");
    rig.SendList(0, "1 2");
    REQUIRE(rig.op->Joined() == "1 2 0");

    rig.SendList(1, "set ");
    CHECK(rig.op->InletSize(1) == 0);
    CHECK(rig.op->Joined() == "1 2");

    rig.out->reset();
    rig.Bang(0);
    CHECK(rig.out->listValue == "1 2");
  }

  // ─── the family's transport convention ────────────────────────────────────

  TEST_CASE("join: a one-atom result leaves as the value it spells (#520)") {
    // The family rule from `pAtomList.h`: a list of one is not a list, and this
    // patcher does no coercion at an inlet, so retyping it would stop it
    // reaching the `.i` a patch wired it to.
    Rig rig("1");
    rig.SendInt(0, 42);
    CHECK(rig.out->gotInt);
    CHECK(rig.out->intValue == 42);
    CHECK_FALSE(rig.out->gotList);

    rig.out->reset();
    rig.SendFloat(0, 1.5f);
    CHECK(rig.out->gotFloat);
    CHECK(rig.out->floatValue == doctest::Approx(1.5f));
  }

  TEST_CASE("join: an object with every inlet emptied sends nothing at all (#520)") {
    // Not an empty message — the `.sprintf` / `.prepend` rule, reachable here
    // and not in `.pack`, whose slots always hold exactly one atom each.
    Rig rig("1");
    rig.SendList(0, "set ");
    REQUIRE(rig.op->Joined().empty());

    rig.Bang(0);
    CHECK_FALSE(rig.out->gotList);
    CHECK_FALSE(rig.out->gotInt);
    CHECK_FALSE(rig.out->gotFloat);
  }

  // ─── the bound ────────────────────────────────────────────────────────────

  TEST_CASE("join: a store that would not fit is refused whole and counted (#520)") {
    // `.pack`'s rule rather than `.zl`'s: what a tail-drop would lose here is
    // the messages the other inlets are holding, not surplus input.
    Rig rig("2");
    rig.SendList(1, "keep me");
    rig.SendList(0, "head");
    REQUIRE(rig.op->Joined() == "head keep me");
    REQUIRE(rig.op->Dropped() == 0);

    const std::string huge(AtomList::TEXT_CAPACITY + 8, 'x');
    rig.out->reset();
    rig.SendList(0, huge);
    CHECK(rig.op->Joined() == "head keep me");
    CHECK(rig.op->Dropped() == 1);
    // The hot inlet was written, so the list still goes out — unchanged, which
    // is exactly what "refused whole" means.
    CHECK(rig.out->listValue == "head keep me");

    // And the object is still working afterwards: a refusal is not a wedge.
    rig.SendList(0, "again");
    CHECK(rig.out->listValue == "again keep me");
  }

  // ─── real-time behaviour ──────────────────────────────────────────────────

  TEST_CASE("join: Calculate() emits nothing (#520)") {
    // The object is driven by its inlets; one that emitted here would re-send
    // the list on every DSP tick from a stimulus no patch sent.
    Rig rig("2");
    rig.SendList(0, "1 2");
    rig.out->reset();

    for (int i = 0; i < 8; i++)
      rig.op->Calculate(YSE::T_DSP);
    CHECK_FALSE(rig.out->gotList);
  }

  TEST_CASE("join: a cord back into one of its own inlets is bounded, not fatal (#520)") {
    // The guard is what stops a feedback cord recursing on the audio thread:
    // the returning message finds it taken, is counted, and goes no further.
    gJoin obj;
    obj.SetParams("2");
    REQUIRE(obj.ConnectInlet(obj.GetOutlet(0), 1));
    obj.ConnectOutlet(obj.GetInlet(1), 0);

    obj.GetInlet(0)->SetList("5 6", YSE::T_GUI);
    CHECK(obj.Joined() == "5 6 0");
    CHECK(obj.Dropped() == 1);
  }

  TEST_CASE("join: the message path allocates nothing (#520)") {
    if (!TestHelpers::probeCountsAllocations()) return;
    // The claim covers a path that rebuilds list storage and renders list text,
    // so it only means anything if the probe can see a std::string's own
    // allocations (issue #697).
    if (!TestHelpers::probeSeesStringAllocations()) return;

    Rig rig("3 -1");

    // Warm every buffer the path touches — including the sink's, which is test
    // scaffolding rather than the object under test.
    rig.SendList(0, "111 222.5 name");
    rig.SendList(1, "other things here");
    rig.SendList(2, "set tail");
    rig.SendInt(0, 7);
    rig.SendFloat(1, 1.25f);
    rig.Bang(2);

    const std::string listText = "111 222.5 name";
    const std::string shortText = "12";
    const std::string setText = "set 4 5 6";
    {
      TestHelpers::ProbeScope probe;
      rig.SendList(0, listText);
      rig.SendList(1, shortText);
      rig.SendList(2, setText);
      rig.SendInt(0, 5);
      rig.SendFloat(1, 2.5f);
      rig.Bang(2);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  // ─── persistence ──────────────────────────────────────────────────────────

  TEST_CASE("join: params survive a DumpJSON / ParseJSON round trip (#520)") {
    // Sink before the patchers: they are torn down first, while the inlet they
    // are wired to still exists.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_JOIN, "4 -1") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".join") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".join");
    CHECK(copy->GetParams() == std::string("4 -1"));
    // The first argument *is* the inlet count, so a round trip that lost it
    // would come back with the wrong number of inlets — and the trigger list
    // with the wrong ones hot.
    CHECK(copy->GetInputs() == 4);
    CHECK(copy->GetOutputs() == 1);

    // The restored triggers are observable: writing a right-hand inlet releases
    // only because `-1` survived.
    loaded.Connect(copy, 0, &sinkHandle, 0);
    copy->SetIntData(3, 9);
    CHECK(sink.gotList);
    CHECK(sink.listValue == "0 0 0 9");
  }

  // ─── end to end, through a real patcher graph ─────────────────────────────

  TEST_CASE("join: .join into .unjoin is a round trip, down real cords (#520)") {
    // The claim the issue is written around, run through the real thing: two
    // variable-length pieces are joined into one list message, that message
    // travels down a cord, and `.unjoin` hands the same two pieces back out two
    // cords. Nothing short of the whole chain proves it — a standalone rig can
    // assert on the text an outlet carried, but not that the patcher delivered
    // a *list message* the other object reads as a list.
    //
    // Sinks before the patcher: the patcher is torn down first, while the
    // inlets it is wired to still exist.
    MultiSink first;
    MultiSink second;
    MultiSink rest;
    YSE::pHandle firstHandle(&first);
    YSE::pHandle secondHandle(&second);
    YSE::pHandle restHandle(&rest);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* join = p.CreateObject(YSE::OBJ::G_JOIN, "2");
    // Two group outlets of two items each — the shape that undoes a `.join 2`
    // whose pieces are two items long.
    YSE::pHandle* unjoin = p.CreateObject(YSE::OBJ::G_UNJOIN, "2 2");
    REQUIRE(join != nullptr);
    REQUIRE(unjoin != nullptr);
    p.Connect(join, 0, unjoin, 0);
    p.Connect(unjoin, 0, &firstHandle, 0);
    p.Connect(unjoin, 1, &secondHandle, 0);
    p.Connect(unjoin, 2, &restHandle, 0);

    // The cold inlet of the `.join` loads without sending anything anywhere, so
    // nothing has reached the `.unjoin` yet.
    join->SetListData(1, "3 4");
    CHECK_FALSE(first.gotList);
    CHECK_FALSE(second.gotList);

    // The hot one releases, and what comes back out the far end is what went in.
    join->SetListData(0, "1 2");
    CHECK(first.gotList);
    CHECK(first.listValue == "1 2");
    CHECK(second.gotList);
    CHECK(second.listValue == "3 4");
    // Nothing left over: the list divided exactly into complete groups.
    CHECK_FALSE(rest.gotList);
    CHECK_FALSE(rest.gotInt);

    // A longer piece changes the split rather than being dropped — the whole
    // point of the pair over `.pack` / `.unpack`, whose shapes are fixed.
    first.reset();
    second.reset();
    rest.reset();
    join->SetListData(0, "1 2 3");
    CHECK(first.listValue == "1 2");
    CHECK(second.listValue == "3 3");
    CHECK(rest.gotInt);
    CHECK(rest.intValue == 4);
  }

} // TEST_SUITE

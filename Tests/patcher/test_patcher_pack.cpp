// Tests for `.pack` (issue #517) — the object that builds one list out of
// values arriving at separate inlets, over the bounded list model `.zl` settled
// in #523.
//
// What is being pinned:
//
//   - **the shape**: one inlet per creation argument, one outlet, and Max's
//     no-argument default of two int elements starting at 0;
//   - **the types**: each argument's spelling decides its element's type, and
//     the type is enforced on every store — a float into an int element is
//     truncated, an int into a float element is promoted, and a symbol into a
//     number element is refused and counted rather than storing a 0;
//   - **hot and cold**: only the leftmost inlet releases, which is the whole
//     difference between this object and `.pak` (#518);
//   - **the inherited transport**: a one-element pack sends the value it
//     spells rather than a list of one;
//   - **the inherited bound**: a store that would not fit is refused *whole*
//     and counted, leaving the elements the other inlets are holding alone.
//
// The unit-level cases drive standalone objects, which is what this object
// needs (no patcher, no clock, no scheduler). The end-to-end section at the
// bottom drives a real `YSE::patcher` graph through `pHandle`, because the
// claim that matters to a patch — that a list built here is a list the rest of
// the patcher can consume, down real cords — cannot be seen from a standalone
// object at all.
//
// No audio device required.

#include <doctest/doctest.h>
#include <cstdint>
#include <string>
#include <vector>

#include "patcher/genericObjects/gPack.h"
#include "patcher/inlet.h"
#include "patcher/pAtomList.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using TestHelpers::MultiSink;
using YSE::PATCHER::AtomList;
using YSE::PATCHER::gPack;
using packType = YSE::PATCHER::gPackBase::packType;

namespace {

  // A `.pack` with a sink on its one outlet. Sink first, so the object dies
  // before the inlet it is wired to.
  struct Rig {
    MultiSink out;

    void Wire(gPack& obj) {
      TestHelpers::Wire(obj, 0, out);
    }
  };

  // `count` int arguments, all zero — the argument list a wide `.pack` is
  // created with.
  std::string Zeros(int count) {
    std::string args;
    for (int i = 0; i < count; i++) {
      if (i > 0) args.push_back(' ');
      args.push_back('0');
    }
    return args;
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── shape and registration ─────────────────────────────────────────────────

  TEST_CASE("pack: registered, one inlet per argument and a single outlet (#517)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::G_PACK, "0 0 0");
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == ".pack");
    CHECK(obj->GetInputs() == 3);
    CHECK(obj->GetOutputs() == 1);
  }

  TEST_CASE("pack: appears in the registry's name list (#517)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_PACK)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("pack: with no arguments it is Max's default — two int elements at 0 (#517)") {
    // Max: "If no arguments provided, the object creates two inlets with
    // initial values of 0 (int)." So a bang before anything arrives is a
    // complete list rather than silence.
    Rig rig;
    gPack obj;
    rig.Wire(obj);

    CHECK(obj.PortCount() == 2);
    CHECK(obj.SlotType(0) == packType::INT);
    CHECK(obj.SlotType(1) == packType::INT);
    CHECK(obj.Packed() == "0 0");

    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "0 0");
  }

  TEST_CASE("pack: the argument's spelling decides its element's type (#517)") {
    // Max: "The arguments determine the list format and types of the list
    // elements."
    gPack obj;
    obj.SetParams("0 0. name");
    CHECK(obj.PortCount() == 3);
    CHECK(obj.SlotType(0) == packType::INT);
    CHECK(obj.SlotType(1) == packType::FLOAT);
    CHECK(obj.SlotType(2) == packType::SYMBOL);
    // And the argument is that element's starting value, not only its type.
    CHECK(obj.Packed() == "0 0. name");
  }

  TEST_CASE("pack: the left inlet takes a bang, the others do not (#517)") {
    // Max documents bang on the left inlet; registering it nowhere else keeps
    // GetAcceptedTypes() reporting the real contract — the .zl / .combine
    // discipline.
    gPack obj;
    obj.SetParams("0 0");

    const unsigned int left = obj.GetInlet(0)->GetAcceptedTypes();
    CHECK((left & YSE::PATCHER::IT_BANG) != 0);
    CHECK((left & YSE::PATCHER::IT_INT) != 0);
    CHECK((left & YSE::PATCHER::IT_LIST) != 0);

    const unsigned int right = obj.GetInlet(1)->GetAcceptedTypes();
    CHECK((right & YSE::PATCHER::IT_BANG) == 0);
    CHECK((right & YSE::PATCHER::IT_INT) != 0);
    CHECK((right & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((right & YSE::PATCHER::IT_LIST) != 0);
  }

  TEST_CASE("pack: SetParams(\"\") returns the object to its no-argument shape (#517)") {
    // Parameters::Set returns without calling the parse callback for an empty
    // argument, so the clear callback is the whole of the reset.
    gPack obj;
    obj.SetParams("1 2 3 4");
    REQUIRE(obj.PortCount() == 4);
    REQUIRE(obj.Packed() == "1 2 3 4");

    obj.SetParams("");
    CHECK(obj.PortCount() == 2);
    CHECK(obj.Packed() == "0 0");
  }

  // ─── hot and cold ───────────────────────────────────────────────────────────

  TEST_CASE("pack: only the leftmost inlet releases the list (#517)") {
    // The object's arrangement, and the whole difference between `.pack` and
    // `.pak` (#518): load the right-hand elements, then let the leftmost inlet
    // carry the finished list out.
    Rig rig;
    gPack obj;
    obj.SetParams("0 0 0");
    rig.Wire(obj);
    CHECK_FALSE(obj.EveryInletHot());

    obj.GetInlet(1)->SetInt(20, YSE::T_GUI);
    obj.GetInlet(2)->SetInt(30, YSE::T_GUI);
    CHECK_FALSE(rig.out.gotList);
    CHECK_FALSE(rig.out.gotInt);
    // Stored all the same — the values are there, waiting for the release.
    CHECK(obj.Packed() == "0 20 30");

    obj.GetInlet(0)->SetInt(10, YSE::T_GUI);
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "10 20 30");
  }

  TEST_CASE("pack: a bang releases the list without storing anything (#517)") {
    // Max: "bang: Output currently stored list."
    Rig rig;
    gPack obj;
    obj.SetParams("0 0");
    rig.Wire(obj);

    obj.GetInlet(1)->SetInt(7, YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.out.listValue == "0 7");

    // Twice in a row is the same list: a release does not consume the elements.
    rig.out.reset();
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.out.listValue == "0 7");
  }

  TEST_CASE("pack: 'set' stores into the left inlet without releasing (#517)") {
    // Max: "Sets the values without causing list output. Although the set
    // message works with any inlet, it is only meaningful in the left inlet."
    Rig rig;
    gPack obj;
    obj.SetParams("0 0 0");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("set 1 2 3", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotList);
    CHECK(obj.Packed() == "1 2 3");

    // The same store the plain message would have performed — only the release
    // was suppressed, which the bang then asks for.
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "1 2 3");
  }

  // ─── element types ──────────────────────────────────────────────────────────

  TEST_CASE("pack: an int element truncates a float, a float element promotes an int (#517)") {
    // Max: "type conversion occurs based on initialization."
    Rig rig;
    gPack obj;
    obj.SetParams("0 0.");
    rig.Wire(obj);

    obj.GetInlet(1)->SetInt(5, YSE::T_GUI);
    CHECK(obj.Packed() == "0 5.");

    obj.GetInlet(0)->SetFloat(2.9f, YSE::T_GUI);
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "2 5.");
  }

  TEST_CASE("pack: a number element handed a symbol keeps its value and counts it (#517)") {
    // A symbol has nothing to convert. Storing a 0 would read downstream as a
    // value the patch chose, so the element keeps what it had and the refusal
    // goes on the counter — a counter rather than a log line, this being a path
    // the audio callback takes.
    Rig rig;
    gPack obj;
    obj.SetParams("0 0");
    rig.Wire(obj);

    obj.GetInlet(1)->SetInt(9, YSE::T_GUI);
    REQUIRE(obj.Dropped() == 0);

    obj.GetInlet(1)->SetList("hello", YSE::T_GUI);
    CHECK(obj.Packed() == "0 9");
    CHECK(obj.Dropped() == 1);

    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.out.listValue == "0 9");
  }

  TEST_CASE("pack: a symbol element takes whatever arrives, verbatim (#517)") {
    Rig rig;
    gPack obj;
    obj.SetParams("note 0");
    rig.Wire(obj);
    REQUIRE(obj.SlotType(0) == packType::SYMBOL);

    obj.GetInlet(1)->SetInt(64, YSE::T_GUI);
    obj.GetInlet(0)->SetList("velocity", YSE::T_GUI);
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "velocity 64");
    CHECK(obj.Dropped() == 0);
  }

  // ─── lists spread ───────────────────────────────────────────────────────────

  TEST_CASE("pack: a multi-item message spreads from the inlet that received it (#517)") {
    Rig rig;
    gPack obj;
    obj.SetParams("0 0 0 0");
    rig.Wire(obj);

    // Into a cold inlet: elements 1 and 2 are written, 0 and 3 keep what they
    // hold, and nothing is sent.
    obj.GetInlet(1)->SetList("20 30", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotList);
    CHECK(obj.Packed() == "0 20 30 0");

    // Into the hot inlet: written from element 0 rightwards, and released.
    obj.GetInlet(0)->SetList("1 2", YSE::T_GUI);
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "1 2 30 0");
  }

  TEST_CASE("pack: items past the last element are dropped and counted (#517)") {
    Rig rig;
    gPack obj;
    obj.SetParams("0 0");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("1 2 3 4", YSE::T_GUI);
    CHECK(rig.out.listValue == "1 2");
    CHECK(obj.Dropped() == 2);

    // And from a cold inlet, where there is even less room to the right.
    obj.GetInlet(1)->SetList("9 8 7", YSE::T_GUI);
    CHECK(obj.Packed() == "1 9");
    CHECK(obj.Dropped() == 4);
  }

  // ─── the transport convention ───────────────────────────────────────────────

  TEST_CASE("pack: a one-element pack sends the value it spells, not a list of one (#517)") {
    // The family's convention, inherited from `.zl` through SendAtoms: a list
    // of one atom is not a list, and this patcher does no coercion at an inlet,
    // so a single-element pack has to reach the `.i` a patch wired it to.
    Rig rig;
    gPack obj;
    obj.SetParams("0");
    rig.Wire(obj);
    REQUIRE(obj.PortCount() == 1);

    obj.GetInlet(0)->SetInt(42, YSE::T_GUI);
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 42);
    CHECK_FALSE(rig.out.gotList);

    // The arguments are the port shape, so a rig has to set them before it
    // wires: ShapePorts() rebuilds the outlet, and a cord attached to the old
    // one goes with it.
    MultiSink floatOut;
    gPack asFloat;
    asFloat.SetParams("0.");
    TestHelpers::Wire(asFloat, 0, floatOut);
    asFloat.GetInlet(0)->SetInt(3, YSE::T_GUI);
    CHECK(floatOut.gotFloat);
    CHECK(floatOut.floatValue == doctest::Approx(3.f));

    MultiSink symbolOut;
    gPack asSymbol;
    asSymbol.SetParams("word");
    TestHelpers::Wire(asSymbol, 0, symbolOut);
    asSymbol.GetInlet(0)->SetList("other", YSE::T_GUI);
    CHECK(symbolOut.gotList);
    CHECK(symbolOut.listValue == "other");
  }

  // ─── the bound ──────────────────────────────────────────────────────────────

  TEST_CASE("pack: a store that does not fit is refused whole, not shortened (#517)") {
    // Where this parts company with `.zl`, which keeps the head of an over-long
    // list: the tail of a pack is the values the *other* inlets are holding, and
    // losing them would silently rewrite state nobody touched.
    Rig rig;
    gPack obj;
    obj.SetParams("a b");
    rig.Wire(obj);
    REQUIRE(obj.Packed() == "a b");

    const std::string huge(AtomList::TEXT_CAPACITY + 8, 'x');
    obj.GetInlet(1)->SetList(huge, YSE::T_GUI);
    CHECK(obj.Packed() == "a b");
    CHECK(obj.Dropped() == 1);

    // The object is still working afterwards — a refusal is not a wedge.
    obj.GetInlet(1)->SetList("c", YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.out.listValue == "a c");
  }

  TEST_CASE("pack: the element ceiling is the shared list's, and is clamped (#517)") {
    gPack obj;
    obj.SetParams(Zeros(gPack::MAX_PORTS + 12));
    CHECK(obj.PortCount() == gPack::MAX_PORTS);
    CHECK(obj.PortCount() == (int)AtomList::MAX_ATOMS);
    CHECK(obj.NumInputs() == gPack::MAX_PORTS);
    CHECK(obj.NumOutputs() == 1);
  }

  // ─── real-time behaviour ────────────────────────────────────────────────────

  TEST_CASE("pack: Calculate() emits nothing (#517)") {
    // The object is driven by its inlets; one that emitted here would re-send
    // the list on every DSP tick from a stimulus no patch sent.
    Rig rig;
    gPack obj;
    obj.SetParams("0 0");
    rig.Wire(obj);

    obj.GetInlet(1)->SetInt(3, YSE::T_GUI);
    for (int i = 0; i < 8; i++)
      obj.Calculate(YSE::T_DSP);
    CHECK_FALSE(rig.out.gotList);
    CHECK_FALSE(rig.out.gotInt);
    CHECK_FALSE(rig.out.gotBang);
  }

  TEST_CASE("pack: the message path allocates nothing (#517)") {
    if (!TestHelpers::probeCountsAllocations()) return;
    // The claim covers a path that rebuilds and renders list text, so it only
    // means anything if the probe can see a std::string's own allocations
    // (issue #697).
    if (!TestHelpers::probeSeesStringAllocations()) return;

    Rig rig;
    gPack obj;
    obj.SetParams("0 0. sym");
    rig.Wire(obj);

    // Warm every buffer the path touches — including the sink's, which is test
    // scaffolding rather than the object under test.
    obj.GetInlet(0)->SetList("111 222 name", YSE::T_GUI);
    obj.GetInlet(1)->SetFloat(2.5f, YSE::T_GUI);
    obj.GetInlet(2)->SetList("word", YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);

    const std::string listText = "111 222 name";
    const std::string setText = "set 333 444 other";
    const std::string symbolText = "word";
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetList(listText, YSE::T_GUI);
      obj.GetInlet(0)->SetList(setText, YSE::T_GUI);
      obj.GetInlet(2)->SetList(symbolText, YSE::T_GUI);
      obj.GetInlet(1)->SetInt(7, YSE::T_GUI);
      obj.GetInlet(1)->SetFloat(1.25f, YSE::T_GUI);
      obj.GetInlet(0)->SetInt(5, YSE::T_GUI);
      obj.GetInlet(0)->SetFloat(2.5f, YSE::T_GUI);
      obj.GetInlet(0)->SetBang(YSE::T_GUI);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  // ─── persistence ────────────────────────────────────────────────────────────

  TEST_CASE("pack: params survive a DumpJSON / ParseJSON round trip (#517)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_PACK, "0 0. name") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".pack") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".pack");
    CHECK(copy->GetParams() == std::string("0 0. name"));
    // The argument list *is* the port shape, so a round trip that lost it would
    // come back with the wrong number of inlets.
    CHECK(copy->GetInputs() == 3);
    CHECK(copy->GetOutputs() == 1);
  }

  // ─── end to end, through a real patcher graph ───────────────────────────────

  TEST_CASE("pack: builds a list a real patch can consume, down real cords (#517)") {
    // The use case the object exists for, run through the real thing: three
    // values arrive separately, `.pack` assembles them, and the list travels
    // down a cord into `.zl`, which reads it as a list of three. Nothing short
    // of the whole chain proves that — a standalone rig can assert on the text
    // an outlet carried, but not that the patcher delivered a *list message*
    // another list object accepts.
    //
    // Sinks before the patcher: the patcher is torn down first, while the
    // inlets it is wired to still exist.
    MultiSink packed;
    MultiSink counted;
    YSE::pHandle packedHandle(&packed);
    YSE::pHandle countedHandle(&counted);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* pack = p.CreateObject(YSE::OBJ::G_PACK, "0 0 0");
    YSE::pHandle* zl = p.CreateObject(YSE::OBJ::G_ZL, "len");
    REQUIRE(pack != nullptr);
    REQUIRE(zl != nullptr);
    p.Connect(pack, 0, &packedHandle, 0);
    p.Connect(pack, 0, zl, 0);
    p.Connect(zl, 0, &countedHandle, 0);

    // The cold inlets load without sending anything anywhere.
    pack->SetIntData(1, 64);
    pack->SetIntData(2, 100);
    CHECK_FALSE(packed.gotList);
    CHECK_FALSE(counted.gotInt);

    // The hot one releases, and the list reaches both readers.
    pack->SetIntData(0, 1);
    CHECK(packed.gotList);
    CHECK(packed.listValue == "1 64 100");
    CHECK(counted.gotInt);
    CHECK(counted.intValue == 3);

    // A bang re-sends the same list, which is what makes the cold inlets worth
    // having: the patch chooses when the assembled set goes out.
    packed.reset();
    counted.reset();
    pack->SetIntData(2, 127);
    CHECK_FALSE(packed.gotList);
    pack->SetBang(0);
    CHECK(packed.listValue == "1 64 127");
    CHECK(counted.intValue == 3);
  }

  TEST_CASE("pack: a three-element list is what the named bus is addressed with (#517)") {
    // The motivating case from the issue, end to end: a position built from
    // three separately-arriving numbers, assembled into the one list message a
    // list consumer downstream splits again. `.zl nth 2` standing in for the
    // reader, so the assertion is on a real object's reading of the list rather
    // than on its text.
    MultiSink item;
    YSE::pHandle itemHandle(&item);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* pack = p.CreateObject(YSE::OBJ::G_PACK, "0. 0. 0.");
    YSE::pHandle* zl = p.CreateObject(YSE::OBJ::G_ZL, "nth 2");
    REQUIRE(pack != nullptr);
    REQUIRE(zl != nullptr);
    p.Connect(pack, 0, zl, 0);
    p.Connect(zl, 0, &itemHandle, 0);

    pack->SetFloatData(1, 1.5f);
    pack->SetFloatData(2, -2.f);
    pack->SetFloatData(0, 0.5f);

    CHECK(item.gotFloat);
    CHECK(item.floatValue == doctest::Approx(1.5f));

    // An int arriving at a float element is promoted rather than changing the
    // element's type, so the reader downstream still sees a float.
    item.gotFloat = false;
    pack->SetIntData(1, 3);
    pack->SetBang(0);
    CHECK(item.gotFloat);
    CHECK(item.floatValue == doctest::Approx(3.f));
  }

} // TEST_SUITE

// Tests for `.pak` (issue #518) — `.pack` with every inlet hot, over the same
// bounded list model `.zl` settled in #523 and the same shared body #517 built.
//
// `.pack`'s own behaviour is pinned in test_patcher_pack.cpp and is *not*
// re-tested here; what this file exists for is the difference, which is the
// whole object:
//
//   - **every inlet releases** — writing element 2 sends the whole list, so a
//     patch that writes three inlets in turn gets three lists rather than one;
//   - **every inlet takes a bang** — Max documents bang on `pack`'s left inlet
//     and on `pak`'s *any* inlet, so the accepted-type report has to differ too;
//     this is the one place the shared base needed a correction rather than an
//     enumerator, and the `.pack` cross-check below pins that the correction did
//     not leak;
//   - **`set` is the only quiet store** — with no cold inlet left, it is the
//     only way to load an element without sending;
//   - **the accidental loop** the issue calls out: a `.pak` wired back into its
//     own inlet is the classic infinite cycle, and it has to come out bounded.
//
// The inherited behaviour — element types, the spread, the bound, the
// single-element transport — is checked once each here rather than in full,
// since it is the base's and the base is covered.
//
// The unit-level cases drive standalone objects, which is what this object needs
// (no patcher, no clock, no scheduler). The end-to-end section at the bottom
// drives a real `YSE::patcher` graph through `pHandle`, because the claim that
// matters to a patch — that each component landing pushes a fresh list down real
// cords into a real list consumer — cannot be seen from a standalone object.
//
// No audio device required.

#include <doctest/doctest.h>
#include <cstdint>
#include <string>

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
using TestHelpers::OrderSink;
using YSE::PATCHER::AtomList;
using YSE::PATCHER::gPack;
using YSE::PATCHER::gPak;
using packType = YSE::PATCHER::gPackBase::packType;

namespace {

  // A `.pak` with a counting sink on its one outlet: with every inlet hot, *how
  // many* lists went out is as much of the object as what they said. Sink first,
  // so the object dies before the inlet it is wired to.
  struct Rig {
    OrderSink out;

    void Wire(gPak& obj) {
      TestHelpers::Wire(obj, 0, out);
    }
  };

  // Sends whatever it receives straight back into a `.pak` inlet, from *inside*
  // the send — the accidental cycle the issue warns about, with a depth ceiling
  // so a broken guard fails by assertion instead of by exhausting the stack.
  struct FeedbackSink : YSE::PATCHER::pObject {
    gPak* target = nullptr;
    int intoInlet = 1;
    int hits = 0;
    int depth = 0;
    int maxDepth = 0;
    static constexpr int DEPTH_LIMIT = 8;

    FeedbackSink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterList([this](const std::string& value, int, YSE::THREAD) {
        hits++;
        depth++;
        if (depth > maxDepth) maxDepth = depth;
        if (depth < DEPTH_LIMIT && target != nullptr) {
          target->GetInlet(intoInlet)->SetList(value, YSE::T_GUI);
        }
        depth--;
      });
    }
    const char* Type() const override {
      return "feedback_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape and registration ─────────────────────────────────────────────────

  TEST_CASE("pak: registered, one inlet per argument and a single outlet (#518)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::G_PAK, "0 0 0");
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == ".pak");
    CHECK(obj->GetInputs() == 3);
    CHECK(obj->GetOutputs() == 1);
  }

  TEST_CASE("pak: appears in the registry's name list (#518)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_PAK)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("pak: with no arguments it is Max's default — two int elements at 0 (#518)") {
    // Max: "If no arguments provided, the object creates two inlets with
    // initial values of 0 (int)" — the same default `.pack` has, since the
    // arguments mean the same thing in both.
    Rig rig;
    gPak obj;
    rig.Wire(obj);

    CHECK(obj.PortCount() == 2);
    CHECK(obj.SlotType(0) == packType::INT);
    CHECK(obj.SlotType(1) == packType::INT);
    CHECK(obj.Packed() == "0 0");
  }

  // ─── every inlet is hot ─────────────────────────────────────────────────────

  TEST_CASE("pak: a write to any inlet releases the whole list (#518)") {
    // The object. Max: pak "offers much of the functionality of pack, but
    // outputs the entire list whenever input is received in any inlet."
    Rig rig;
    gPak obj;
    obj.SetParams("0 0 0");
    rig.Wire(obj);
    CHECK(obj.EveryInletHot());

    // The rightmost inlet — the one that stores in silence on a `.pack` — sends
    // the whole list, elements nobody has written included.
    obj.GetInlet(2)->SetInt(30, YSE::T_GUI);
    CHECK(rig.out.count == 1);
    CHECK(rig.out.lastList == "0 0 30");

    // And the middle one, and the leftmost: three writes, three lists.
    obj.GetInlet(1)->SetInt(20, YSE::T_GUI);
    CHECK(rig.out.count == 2);
    CHECK(rig.out.lastList == "0 20 30");

    obj.GetInlet(0)->SetInt(10, YSE::T_GUI);
    CHECK(rig.out.count == 3);
    CHECK(rig.out.lastList == "10 20 30");
  }

  TEST_CASE("pak: a float on a cold-in-.pack inlet releases too (#518)") {
    Rig rig;
    gPak obj;
    obj.SetParams("0. 0.");
    rig.Wire(obj);

    obj.GetInlet(1)->SetFloat(1.5f, YSE::T_GUI);
    CHECK(rig.out.count == 1);
    CHECK(rig.out.lastList == "0. 1.5");
  }

  // ─── bang, on every inlet ───────────────────────────────────────────────────

  TEST_CASE("pak: every inlet accepts a bang, not only the leftmost (#518)") {
    // Max documents bang on `pack`'s left inlet and on `pak`'s *any* inlet, so
    // the accepted-type report has to say so — the .zl / .combine discipline,
    // where GetAcceptedTypes() is the object's real contract rather than a
    // convenience. This is the one thing #518 needed from the shared base that
    // the trigger enumerator alone did not give.
    gPak obj;
    obj.SetParams("0 0 0");

    for (int i = 0; i < 3; i++) {
      CAPTURE(i);
      const unsigned int accepted = obj.GetInlet(i)->GetAcceptedTypes();
      CHECK((accepted & YSE::PATCHER::IT_BANG) != 0);
      CHECK((accepted & YSE::PATCHER::IT_INT) != 0);
      CHECK((accepted & YSE::PATCHER::IT_FLOAT) != 0);
      CHECK((accepted & YSE::PATCHER::IT_LIST) != 0);
    }
  }

  TEST_CASE("pak: a bang on a right inlet releases the list without storing (#518)") {
    // Max: "bang: Output currently stored list." Reported *and* wired — a
    // registration that only reached inlet 0 would leave the report right and
    // the object silent.
    Rig rig;
    gPak obj;
    obj.SetParams("0 0 0");
    rig.Wire(obj);

    obj.GetInlet(1)->SetList("set 7", YSE::T_GUI);
    REQUIRE(rig.out.count == 0);

    obj.GetInlet(2)->SetBang(YSE::T_GUI);
    CHECK(rig.out.count == 1);
    CHECK(rig.out.lastList == "0 7 0");
    // Stored nothing: the bang is a release, not a write.
    CHECK(obj.Packed() == "0 7 0");

    // Twice in a row is the same list — a release does not consume the elements.
    obj.GetInlet(1)->SetBang(YSE::T_GUI);
    CHECK(rig.out.count == 2);
    CHECK(rig.out.lastList == "0 7 0");
  }

  TEST_CASE("pak: .pack is untouched — its right inlets stay cold and bangless (#518)") {
    // The two objects share one body, and the bang registration is now decided
    // by the trigger rather than by the inlet number. This is the cross-check
    // that the change bought `.pak` its contract without spending `.pack`'s.
    MultiSink out;
    gPack pack;
    pack.SetParams("0 0 0");
    TestHelpers::Wire(pack, 0, out);
    CHECK_FALSE(pack.EveryInletHot());

    CHECK((pack.GetInlet(0)->GetAcceptedTypes() & YSE::PATCHER::IT_BANG) != 0);
    CHECK((pack.GetInlet(1)->GetAcceptedTypes() & YSE::PATCHER::IT_BANG) == 0);
    CHECK((pack.GetInlet(2)->GetAcceptedTypes() & YSE::PATCHER::IT_BANG) == 0);

    pack.GetInlet(2)->SetInt(30, YSE::T_GUI);
    pack.GetInlet(2)->SetBang(YSE::T_GUI);
    CHECK_FALSE(out.gotList);
    CHECK(pack.Packed() == "0 0 30");
  }

  // ─── `set`, the only quiet store ────────────────────────────────────────────

  TEST_CASE("pak: 'set' is the only way to load an element without sending (#518)") {
    // Max: "set: Set data without output." On a `.pack` this is a convenience
    // for the left inlet; on a `.pak`, which has no cold inlet at all, it is the
    // only quiet store there is.
    Rig rig;
    gPak obj;
    obj.SetParams("0 0 0");
    rig.Wire(obj);

    obj.GetInlet(1)->SetList("set 20", YSE::T_GUI);
    obj.GetInlet(2)->SetList("set 30", YSE::T_GUI);
    CHECK(rig.out.count == 0);
    CHECK(obj.Packed() == "0 20 30");

    // The same store the plain message would have performed — only the release
    // was suppressed, which the next ordinary write then makes.
    obj.GetInlet(0)->SetInt(10, YSE::T_GUI);
    CHECK(rig.out.count == 1);
    CHECK(rig.out.lastList == "10 20 30");
  }

  // ─── the inherited body, checked once each ──────────────────────────────────

  TEST_CASE("pak: a multi-item message spreads from the inlet it arrived at, then sends (#518)") {
    Rig rig;
    gPak obj;
    obj.SetParams("0 0 0 0");
    rig.Wire(obj);

    // From a right inlet: elements 1 and 2 written, 0 and 3 keep what they hold,
    // and — this being a `.pak` — the list goes out once rather than not at all.
    obj.GetInlet(1)->SetList("20 30", YSE::T_GUI);
    CHECK(rig.out.count == 1);
    CHECK(rig.out.lastList == "0 20 30 0");

    // Items past the last element have nowhere to go: dropped and counted.
    obj.GetInlet(2)->SetList("7 8 9", YSE::T_GUI);
    CHECK(rig.out.count == 2);
    CHECK(rig.out.lastList == "0 20 7 8");
    CHECK(obj.Dropped() == 1);
  }

  TEST_CASE("pak: element types are enforced on the way in, as .pack's are (#518)") {
    // Max: "type conversion occurs based on initialization." Same coercion, same
    // refusal counter — inherited whole from the shared body.
    Rig rig;
    gPak obj;
    obj.SetParams("0 0. sym");
    rig.Wire(obj);
    REQUIRE(obj.SlotType(2) == packType::SYMBOL);

    obj.GetInlet(0)->SetFloat(2.9f, YSE::T_GUI); // int element truncates
    CHECK(rig.out.lastList == "2 0. sym");

    obj.GetInlet(1)->SetInt(5, YSE::T_GUI); // float element promotes
    CHECK(rig.out.lastList == "2 5. sym");

    obj.GetInlet(2)->SetList("word", YSE::T_GUI); // symbol element takes it verbatim
    CHECK(rig.out.lastList == "2 5. word");
    CHECK(obj.Dropped() == 0);

    // A number element handed a symbol keeps what it had and counts the refusal
    // rather than storing a 0 — but still releases, every inlet being hot.
    obj.GetInlet(0)->SetList("hello", YSE::T_GUI);
    CHECK(obj.Dropped() == 1);
    CHECK(rig.out.lastList == "2 5. word");
  }

  TEST_CASE("pak: a one-element pak sends the value it spells, not a list of one (#518)") {
    // The family's transport convention, inherited through SendAtoms.
    MultiSink out;
    gPak obj;
    obj.SetParams("0");
    // The arguments are the port shape, so the rig sets them before it wires:
    // ShapePorts() rebuilds the outlet, and a cord on the old one goes with it.
    TestHelpers::Wire(obj, 0, out);
    REQUIRE(obj.PortCount() == 1);

    obj.GetInlet(0)->SetInt(42, YSE::T_GUI);
    CHECK(out.gotInt);
    CHECK(out.intValue == 42);
    CHECK_FALSE(out.gotList);
  }

  TEST_CASE("pak: a store that does not fit is refused whole, not shortened (#518)") {
    Rig rig;
    gPak obj;
    obj.SetParams("a b");
    rig.Wire(obj);
    REQUIRE(obj.Packed() == "a b");

    const std::string huge(AtomList::TEXT_CAPACITY + 8, 'x');
    obj.GetInlet(1)->SetList(huge, YSE::T_GUI);
    CHECK(obj.Packed() == "a b");
    CHECK(obj.Dropped() == 1);

    // Still working afterwards — a refusal is not a wedge.
    obj.GetInlet(1)->SetList("c", YSE::T_GUI);
    CHECK(rig.out.lastList == "a c");
  }

  TEST_CASE("pak: the element ceiling is the shared list's, and is clamped (#518)") {
    std::string args;
    for (int i = 0; i < gPak::MAX_PORTS + 12; i++) {
      if (i > 0) args.push_back(' ');
      args.push_back('0');
    }
    gPak obj;
    obj.SetParams(args);
    CHECK(obj.PortCount() == gPak::MAX_PORTS);
    CHECK(obj.PortCount() == (int)AtomList::MAX_ATOMS);
    CHECK(obj.NumInputs() == gPak::MAX_PORTS);
    CHECK(obj.NumOutputs() == 1);
  }

  // ─── the accidental loop ────────────────────────────────────────────────────

  TEST_CASE("pak: a cord back into its own inlet is bounded, dropped and counted (#518)") {
    // The issue's warning: "`.pak` in a cycle is a classic accidental infinite
    // loop", every inlet being a trigger. The single test-and-set guard is what
    // makes it finite — the returning message finds the object busy, is counted,
    // and goes no further. Without it this case recurses until the stack or the
    // outlet depth ceiling gives out.
    FeedbackSink echo;
    gPak obj;
    obj.SetParams("0 0");
    echo.target = &obj;
    echo.intoInlet = 1;
    TestHelpers::Wire(obj, 0, echo);

    obj.GetInlet(0)->SetList("1 2", YSE::T_GUI);

    // One pass out, and the re-entry is refused rather than starting another.
    CHECK(echo.hits == 1);
    CHECK(echo.maxDepth == 1);
    CHECK(obj.Dropped() == 1);
    // And the refused re-entry stored nothing: the list is what the patch sent.
    CHECK(obj.Packed() == "1 2");
  }

  // ─── real-time behaviour ────────────────────────────────────────────────────

  TEST_CASE("pak: Calculate() emits nothing (#518)") {
    // The object is driven by its inlets; one that emitted here would re-send
    // the list on every DSP tick from a stimulus no patch sent — and with every
    // inlet hot there is no "cold load" reading that would excuse it.
    Rig rig;
    gPak obj;
    obj.SetParams("0 0");
    rig.Wire(obj);

    obj.GetInlet(1)->SetList("set 3", YSE::T_GUI);
    for (int i = 0; i < 8; i++)
      obj.Calculate(YSE::T_DSP);
    CHECK(rig.out.count == 0);
  }

  TEST_CASE("pak: the message path allocates nothing (#518)") {
    if (!TestHelpers::probeCountsAllocations()) return;
    // The claim covers a path that rebuilds and renders list text, so it only
    // means anything if the probe can see a std::string's own allocations
    // (issue #697).
    if (!TestHelpers::probeSeesStringAllocations()) return;

    Rig rig;
    gPak obj;
    obj.SetParams("0 0. sym");
    rig.Wire(obj);

    // Warm every buffer the path touches — including the sink's, which is test
    // scaffolding rather than the object under test.
    obj.GetInlet(0)->SetList("111 222 name", YSE::T_GUI);
    obj.GetInlet(1)->SetFloat(2.5f, YSE::T_GUI);
    obj.GetInlet(2)->SetList("word", YSE::T_GUI);
    obj.GetInlet(2)->SetBang(YSE::T_GUI);

    const std::string listText = "111 222 name";
    const std::string setText = "set 333 444 other";
    const std::string symbolText = "word";
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetList(listText, YSE::T_GUI);
      obj.GetInlet(1)->SetList(setText, YSE::T_GUI);
      obj.GetInlet(2)->SetList(symbolText, YSE::T_GUI);
      obj.GetInlet(1)->SetInt(7, YSE::T_GUI);
      obj.GetInlet(2)->SetFloat(1.25f, YSE::T_GUI);
      obj.GetInlet(0)->SetInt(5, YSE::T_GUI);
      obj.GetInlet(1)->SetBang(YSE::T_GUI);
      obj.GetInlet(2)->SetBang(YSE::T_GUI);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  // ─── persistence ────────────────────────────────────────────────────────────

  TEST_CASE("pak: params survive a DumpJSON / ParseJSON round trip (#518)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_PAK, "0 0. name") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".pak") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".pak");
    CHECK(copy->GetParams() == std::string("0 0. name"));
    // The argument list *is* the port shape, so a round trip that lost it would
    // come back with the wrong number of inlets.
    CHECK(copy->GetInputs() == 3);
    CHECK(copy->GetOutputs() == 1);
  }

  // ─── end to end, through a real patcher graph ───────────────────────────────

  TEST_CASE("pak: each component landing pushes a fresh list down real cords (#518)") {
    // The use case from the issue — "continuously updated positions", where
    // every component should immediately propagate — run through the real thing:
    // three coordinates arrive on three separate cords, and each arrival sends a
    // complete position downstream rather than waiting for a leftmost inlet that
    // may never be written again. `.zl len` stands in for the consumer, so the
    // assertion is on a real list object's reading of the message rather than on
    // its text.
    //
    // Sinks before the patcher: the patcher is torn down first, while the inlets
    // it is wired to still exist.
    MultiSink position;
    MultiSink counted;
    YSE::pHandle positionHandle(&position);
    YSE::pHandle countedHandle(&counted);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* pak = p.CreateObject(YSE::OBJ::G_PAK, "0. 0. 0.");
    YSE::pHandle* zl = p.CreateObject(YSE::OBJ::G_ZL, "len");
    REQUIRE(pak != nullptr);
    REQUIRE(zl != nullptr);
    p.Connect(pak, 0, &positionHandle, 0);
    p.Connect(pak, 0, zl, 0);
    p.Connect(zl, 0, &countedHandle, 0);

    // The rightmost coordinate alone is enough to send a whole position, which
    // is what a `.pack` here would not have done.
    pak->SetFloatData(2, -2.f);
    CHECK(position.gotList);
    CHECK(position.listValue == "0. 0. -2.");
    CHECK(counted.gotInt);
    CHECK(counted.intValue == 3);

    // And each further component moves it again, on arrival.
    position.reset();
    pak->SetFloatData(1, 1.5f);
    CHECK(position.listValue == "0. 1.5 -2.");

    position.reset();
    pak->SetFloatData(0, 0.5f);
    CHECK(position.listValue == "0.5 1.5 -2.");
  }

  TEST_CASE("pak: a bang from a patch re-sends the list from any inlet (#518)") {
    MultiSink item;
    YSE::pHandle itemHandle(&item);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* pak = p.CreateObject(YSE::OBJ::G_PAK, "0 0 0");
    YSE::pHandle* zl = p.CreateObject(YSE::OBJ::G_ZL, "nth 2");
    REQUIRE(pak != nullptr);
    REQUIRE(zl != nullptr);
    p.Connect(pak, 0, zl, 0);
    p.Connect(zl, 0, &itemHandle, 0);

    pak->SetIntData(1, 64);
    CHECK(item.gotInt);
    CHECK(item.intValue == 64);

    // A bang on the rightmost inlet — an inlet `.pack` would not have accepted
    // one on at all — re-sends the same list through the real graph.
    item.reset();
    pak->SetBang(2);
    CHECK(item.gotInt);
    CHECK(item.intValue == 64);
  }

} // TEST_SUITE

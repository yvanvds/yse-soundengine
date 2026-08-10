// Tests for `.unpack` (issue #519) — the object that breaks a list into its
// elements and sends each one out a separate outlet, over the bounded list
// model `.zl` settled in #523 and the argument grammar `.pack` settled in #517.
//
// What is being pinned:
//
//   - **the shape**: one inlet, one outlet per creation argument, and Max's
//     no-argument default of two int outlets starting at 0;
//   - **the types**: each argument's spelling decides its outlet's type *and*
//     the outlet's declared OUT_TYPE — an int outlet truncates a float, a float
//     outlet promotes an int, and a symbol handed to a number outlet is refused
//     and counted rather than emitting a 0;
//   - **the order**: outlets fire right to left, which is asserted as a
//     *sequence* through sinks sharing one log rather than inferred from a
//     "did every outlet fire?" count;
//   - **who fires**: only the outlets the list actually reached, so a short
//     list leaves the right-hand outlets silent and a bare int reaches the
//     leftmost one only, while a bang fires all of them;
//   - **the inherited transport**: an element leaves as the int, float or
//     symbol it spells, never as a list of one;
//   - **the inherited bound**: a store that would not fit is refused whole,
//     counted, and — unlike `.pack`, which still releases its unchanged list —
//     silent.
//
// The unit-level cases drive standalone objects, which is what this object
// needs (no patcher, no clock, no scheduler). The end-to-end section at the
// bottom drives a real `YSE::patcher` graph through `pHandle`, because the
// claim that matters to a patch — that `.pack` and `.unpack` are inverses down
// real cords, and that the right-to-left order survives the patcher's own send
// path — cannot be seen from a standalone object at all.
//
// No audio device required.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include <vector>

#include "patcher/genericObjects/gUnpack.h"
#include "patcher/inlet.h"
#include "patcher/pAtomList.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using TestHelpers::OrderSink;
using YSE::PATCHER::AtomList;
using YSE::PATCHER::gUnpack;
using unpackType = YSE::PATCHER::gUnpack::unpackType;

namespace {

  // One order-logging sink per outlet, all sharing one log, so "which outlet
  // fired, with what, and in what order" is an assertion rather than an
  // inference. The `.trigger` rig (#466), because this object makes the same
  // right-to-left promise and a count-only test cannot tell a right-to-left
  // object from a left-to-right one.
  struct Rig {
    std::unique_ptr<gUnpack> op;
    std::vector<std::unique_ptr<OrderSink>> sinks;
    std::vector<char> order;

    explicit Rig(const std::string& args = "") : op(new gUnpack()) {
      // The arguments *are* the outlet shape, so they have to be set before the
      // wiring: ShapePorts() rebuilds the outlets, and a cord attached to an old
      // one goes with it.
      if (!args.empty()) op->SetParams(args);
      Wire();
    }

    // Rebuilt after a SetParams, since re-parsing replaces the outlets.
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
    void Bang() {
      op->GetInlet(0)->SetBang(YSE::T_GUI);
    }
  };

  // `count` int arguments, all zero — the argument list a wide `.unpack` is
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

  // ─── shape and registration ───────────────────────────────────────────────

  TEST_CASE("unpack: registered, one inlet and one outlet per argument (#519)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::G_UNPACK, "0 0 0");
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == ".unpack");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 3);
  }

  TEST_CASE("unpack: appears in the registry's name list (#519)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_UNPACK)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("unpack: with no arguments it is Max's default — two int outlets at 0 (#519)") {
    // Max: "If no argument is typed in, unpack will have two int outlets." So a
    // bang before anything arrives is a complete set rather than silence.
    Rig rig;
    CHECK(rig.op->PortCount() == 2);
    CHECK(rig.op->SlotType(0) == unpackType::INT);
    CHECK(rig.op->SlotType(1) == unpackType::INT);
    CHECK(rig.op->Stored() == "0 0");

    rig.Bang();
    CHECK(rig.Log() == "ba");
    CHECK(rig.sinks[0]->lastKind == OrderSink::INT);
    CHECK(rig.sinks[0]->lastInt == 0);
    CHECK(rig.sinks[1]->lastKind == OrderSink::INT);
    CHECK(rig.sinks[1]->lastInt == 0);
  }

  TEST_CASE("unpack: the argument's spelling decides its outlet's type (#519)") {
    // Max: the arguments "can be any combination of ints, floats, and symbols"
    // and set the output type of each outlet.
    Rig rig("0 0. name");
    CHECK(rig.op->PortCount() == 3);
    CHECK(rig.op->SlotType(0) == unpackType::INT);
    CHECK(rig.op->SlotType(1) == unpackType::FLOAT);
    CHECK(rig.op->SlotType(2) == unpackType::SYMBOL);
    // And the argument is that element's starting value, not only its type.
    CHECK(rig.op->Stored() == "0 0. name");

    // The coercion is enforced on the way in, so the typed outlet is the honest
    // declaration; only the symbol element can carry anything at all.
    CHECK(rig.op->GetOutputType(0) == YSE::OUT_TYPE::INT);
    CHECK(rig.op->GetOutputType(1) == YSE::OUT_TYPE::FLOAT);
    CHECK(rig.op->GetOutputType(2) == YSE::OUT_TYPE::ANY);
  }

  TEST_CASE("unpack: the one inlet takes bang, int, float and list (#519)") {
    Rig rig("0 0");
    const unsigned int accepted = rig.op->GetInlet(0)->GetAcceptedTypes();
    CHECK((accepted & YSE::PATCHER::IT_BANG) != 0);
    CHECK((accepted & YSE::PATCHER::IT_INT) != 0);
    CHECK((accepted & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((accepted & YSE::PATCHER::IT_LIST) != 0);
  }

  TEST_CASE("unpack: SetParams(\"\") returns the object to its no-argument shape (#519)") {
    // Parameters::Set returns without calling the parse callback for an empty
    // argument, so the clear callback is the whole of the reset.
    gUnpack obj;
    obj.SetParams("1 2 3 4");
    REQUIRE(obj.PortCount() == 4);
    REQUIRE(obj.NumOutputs() == 4);
    REQUIRE(obj.Stored() == "1 2 3 4");

    obj.SetParams("");
    CHECK(obj.PortCount() == 2);
    CHECK(obj.NumOutputs() == 2);
    CHECK(obj.Stored() == "0 0");
  }

  // ─── distribution ─────────────────────────────────────────────────────────

  TEST_CASE("unpack: each item goes to the outlet in its own position (#519)") {
    // Max: "Each item in the list is sent out the outlet corresponding to its
    // position in the list. The first item in the list is sent out the leftmost
    // outlet, and so on."
    Rig rig("0 0 0");
    rig.SendList("11 22 33");

    CHECK(rig.sinks[0]->lastInt == 11);
    CHECK(rig.sinks[1]->lastInt == 22);
    CHECK(rig.sinks[2]->lastInt == 33);
    CHECK(rig.op->Stored() == "11 22 33");
  }

  TEST_CASE("unpack: the outlets fire right to left (#519)") {
    // Max's universal order, and the reason the object composes with cold
    // inlets: the right-hand elements land before the leftmost one arrives to
    // set the result off. Asserted as a sequence — a left-to-right walk would
    // deliver the same three values and fail here.
    Rig rig("0 0 0 0");
    rig.SendList("1 2 3 4");
    CHECK(rig.Log() == "dcba");

    // And on a bang, which fires every outlet rather than only the ones a list
    // reached.
    rig.Reset();
    rig.Bang();
    CHECK(rig.Log() == "dcba");
  }

  TEST_CASE("unpack: only the outlets the list reached fire (#519)") {
    // Max: "up to the number of outlets" read from the other end — a list too
    // short to reach an outlet leaves it silent rather than sending it a 0.
    Rig rig("0 0 0");
    rig.SendList("7 8");

    CHECK(rig.Log() == "ba");
    CHECK(rig.sinks[0]->count == 1);
    CHECK(rig.sinks[1]->count == 1);
    CHECK(rig.sinks[2]->count == 0);
    CHECK(rig.sinks[0]->lastInt == 7);
    CHECK(rig.sinks[1]->lastInt == 8);

    // The untouched element still holds its creation argument, and a bang is
    // how a patch asks for the whole set anyway.
    CHECK(rig.op->Stored() == "7 8 0");
    rig.Reset();
    rig.Bang();
    CHECK(rig.Log() == "cba");
    CHECK(rig.sinks[2]->lastInt == 0);
  }

  TEST_CASE("unpack: an int or a float reaches the leftmost outlet only (#519)") {
    // Max: "int: The number is sent out the left outlet." A one-item list, and
    // handled as one.
    Rig rig("0 0 0");
    rig.SendInt(5);
    CHECK(rig.Log() == "a");
    CHECK(rig.sinks[0]->lastKind == OrderSink::INT);
    CHECK(rig.sinks[0]->lastInt == 5);
    CHECK(rig.sinks[1]->count == 0);
    CHECK(rig.sinks[2]->count == 0);

    rig.Reset();
    rig.SendFloat(2.75f);
    CHECK(rig.Log() == "a");
    // Element 0 is an int element, so the float is truncated on the way in.
    CHECK(rig.sinks[0]->lastInt == 2);
    CHECK(rig.op->Stored() == "2 0 0");
  }

  TEST_CASE("unpack: a bang sends the stored items out every outlet (#519)") {
    // Max: "bang: Causes each stored item of a list to be sent out the
    // corresponding outlet." Which is why the object holds the elements at all
    // rather than distributing them straight through.
    Rig rig("0 0");
    rig.SendList("3 4");
    rig.Reset();

    rig.Bang();
    CHECK(rig.Log() == "ba");
    CHECK(rig.sinks[0]->lastInt == 3);
    CHECK(rig.sinks[1]->lastInt == 4);

    // Twice in a row is the same set: a send does not consume the elements.
    rig.Reset();
    rig.Bang();
    CHECK(rig.Log() == "ba");
    CHECK(rig.sinks[1]->lastInt == 4);
  }

  // ─── element types ────────────────────────────────────────────────────────

  TEST_CASE("unpack: an int outlet truncates a float, a float outlet promotes an int (#519)") {
    // Max: "the inlet type is forced to the outlet type that is defined", and
    // the same enforcement `.pack` makes of the same arguments.
    Rig rig("0 0.");
    rig.SendList("2.9 5");

    CHECK(rig.sinks[0]->lastKind == OrderSink::INT);
    CHECK(rig.sinks[0]->lastInt == 2);
    CHECK(rig.sinks[1]->lastKind == OrderSink::FLOAT);
    CHECK(rig.sinks[1]->lastFloat == doctest::Approx(5.f));
    CHECK(rig.op->Stored() == "2 5.");
  }

  TEST_CASE("unpack: a number outlet handed a symbol keeps its value and counts it (#519)") {
    // A symbol has nothing to force. Emitting a 0 would read downstream as a
    // value the list carried, so the element keeps what it had and the refusal
    // goes on the counter — a counter rather than a log line, this being a path
    // the audio callback takes.
    Rig rig("0 0");
    rig.SendList("9 8");
    REQUIRE(rig.op->Dropped() == 0);
    rig.Reset();

    rig.SendList("hello 4");
    CHECK(rig.op->Dropped() == 1);
    CHECK(rig.op->Stored() == "9 4");
    // The outlet still fires, carrying the value the element kept — the same
    // reading `.pack` gives, where the refused element stays in the list that
    // goes out.
    CHECK(rig.Log() == "ba");
    CHECK(rig.sinks[0]->lastInt == 9);
    CHECK(rig.sinks[1]->lastInt == 4);
  }

  TEST_CASE("unpack: a symbol outlet passes whatever arrives, verbatim (#519)") {
    Rig rig("name 0");
    REQUIRE(rig.op->SlotType(0) == unpackType::SYMBOL);

    rig.SendList("velocity 64");
    CHECK(rig.sinks[0]->lastKind == OrderSink::LIST);
    CHECK(rig.sinks[0]->lastList == "velocity");
    CHECK(rig.sinks[1]->lastInt == 64);
    CHECK(rig.op->Dropped() == 0);

    // It converts nothing, so a number reaching it leaves as the number it
    // spells — the family's SendAtom convention, not a list of one.
    rig.Reset();
    rig.SendList("12 7");
    CHECK(rig.sinks[0]->lastKind == OrderSink::INT);
    CHECK(rig.sinks[0]->lastInt == 12);
  }

  TEST_CASE("unpack: items past the last outlet are dropped and counted (#519)") {
    // Max: "up to the number of outlets".
    Rig rig("0 0");
    rig.SendList("1 2 3 4");

    CHECK(rig.Log() == "ba");
    CHECK(rig.sinks[0]->lastInt == 1);
    CHECK(rig.sinks[1]->lastInt == 2);
    CHECK(rig.op->Stored() == "1 2");
    CHECK(rig.op->Dropped() == 2);
  }

  // ─── the bound ────────────────────────────────────────────────────────────

  TEST_CASE("unpack: a store that does not fit is refused whole, and silent (#519)") {
    // Refused whole is `.pack`'s rule; silent with it is this object's, and the
    // one place the two part company. `.pack` still releases because writing
    // its hot inlet *is* a release; here the output is the incoming elements,
    // so firing the outlets with the values they already held would present
    // stale state as the list that just arrived.
    Rig rig("a b");
    REQUIRE(rig.op->Stored() == "a b");

    const std::string huge(AtomList::TEXT_CAPACITY + 8, 'x');
    rig.SendList(huge);
    CHECK(rig.op->Stored() == "a b");
    CHECK(rig.op->Dropped() == 1);
    CHECK(rig.Log().empty());

    // The object is still working afterwards — a refusal is not a wedge.
    rig.SendList("c d");
    CHECK(rig.Log() == "ba");
    CHECK(rig.sinks[0]->lastList == "c");
    CHECK(rig.sinks[1]->lastList == "d");
  }

  TEST_CASE("unpack: the element ceiling is the shared list's, and is clamped (#519)") {
    gUnpack obj;
    obj.SetParams(Zeros(gUnpack::MAX_PORTS + 12));
    CHECK(obj.PortCount() == gUnpack::MAX_PORTS);
    CHECK(obj.PortCount() == (int)AtomList::MAX_ATOMS);
    CHECK(obj.NumOutputs() == gUnpack::MAX_PORTS);
    CHECK(obj.NumInputs() == 1);
  }

  // ─── real-time behaviour ──────────────────────────────────────────────────

  TEST_CASE("unpack: Calculate() emits nothing (#519)") {
    // The object is driven by its inlet; one that emitted here would
    // re-distribute the held elements on every DSP tick from a stimulus no
    // patch sent.
    Rig rig("0 0");
    rig.SendList("3 4");
    rig.Reset();

    for (int i = 0; i < 8; i++)
      rig.op->Calculate(YSE::T_DSP);
    CHECK(rig.Log().empty());
  }

  TEST_CASE("unpack: a cord back into its own inlet is bounded, not fatal (#519)") {
    // The guard is what stops a feedback cord recursing on the audio thread:
    // the returning message finds it taken, is counted, and goes no further.
    gUnpack obj;
    obj.SetParams("0 0");
    // Outlet 0 straight back into the inlet, both ends, inlet first.
    REQUIRE(obj.ConnectInlet(obj.GetOutlet(0), 0));
    obj.ConnectOutlet(obj.GetInlet(0), 0);

    obj.GetInlet(0)->SetList("5 6", YSE::T_GUI);
    CHECK(obj.Stored() == "5 6");
    // Exactly one message came back round and was refused.
    CHECK(obj.Dropped() == 1);
  }

  TEST_CASE("unpack: the message path allocates nothing (#519)") {
    if (!TestHelpers::probeCountsAllocations()) return;
    // The claim covers a path that rebuilds list storage and renders element
    // text, so it only means anything if the probe can see a std::string's own
    // allocations (issue #697).
    if (!TestHelpers::probeSeesStringAllocations()) return;

    Rig rig("0 0. sym");

    // Warm every buffer the path touches — including the sinks', which are test
    // scaffolding rather than the object under test.
    rig.SendList("111 222.5 name");
    rig.SendList("333 444.5 other");
    rig.SendInt(7);
    rig.SendFloat(1.25f);
    rig.Bang();

    const std::string listText = "111 222.5 name";
    const std::string shortText = "12";
    const std::string longText = "1 2 3 4 5";
    {
      TestHelpers::ProbeScope probe;
      rig.SendList(listText);
      rig.SendList(shortText);
      rig.SendList(longText);
      rig.SendInt(5);
      rig.SendFloat(2.5f);
      rig.Bang();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  // ─── persistence ──────────────────────────────────────────────────────────

  TEST_CASE("unpack: params survive a DumpJSON / ParseJSON round trip (#519)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_UNPACK, "0 0. name") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".unpack") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".unpack");
    CHECK(copy->GetParams() == std::string("0 0. name"));
    // The argument list *is* the port shape, so a round trip that lost it would
    // come back with the wrong number of outlets — and with the wrong types on
    // them.
    CHECK(copy->GetInputs() == 1);
    CHECK(copy->GetOutputs() == 3);
    CHECK(copy->OutputDataType(0) == YSE::OUT_TYPE::INT);
    CHECK(copy->OutputDataType(1) == YSE::OUT_TYPE::FLOAT);
    CHECK(copy->OutputDataType(2) == YSE::OUT_TYPE::ANY);
  }

  // ─── end to end, through a real patcher graph ─────────────────────────────

  TEST_CASE("unpack: .pack into .unpack is the identity, down real cords (#519)") {
    // The claim the issue is written around, run through the real thing: three
    // values arrive on three separate cords, `.pack` assembles them into one
    // list message, that message travels down a cord, and `.unpack` hands the
    // same three values back out three cords. Nothing short of the whole chain
    // proves it — a standalone rig can assert on the text an outlet carried, but
    // not that the patcher delivered a *list message* the other object reads as
    // a list.
    //
    // Sinks before the patcher: the patcher is torn down first, while the inlets
    // it is wired to still exist.
    TestHelpers::MultiSink x;
    TestHelpers::MultiSink y;
    TestHelpers::MultiSink z;
    YSE::pHandle xHandle(&x);
    YSE::pHandle yHandle(&y);
    YSE::pHandle zHandle(&z);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* pack = p.CreateObject(YSE::OBJ::G_PACK, "0. 0. 0.");
    YSE::pHandle* unpack = p.CreateObject(YSE::OBJ::G_UNPACK, "0. 0. 0.");
    REQUIRE(pack != nullptr);
    REQUIRE(unpack != nullptr);
    p.Connect(pack, 0, unpack, 0);
    p.Connect(unpack, 0, &xHandle, 0);
    p.Connect(unpack, 1, &yHandle, 0);
    p.Connect(unpack, 2, &zHandle, 0);

    // The cold inlets of the `.pack` load without sending anything anywhere, so
    // nothing has reached the `.unpack` yet.
    pack->SetFloatData(1, 1.5f);
    pack->SetFloatData(2, -2.f);
    CHECK_FALSE(x.gotFloat);
    CHECK_FALSE(y.gotFloat);
    CHECK_FALSE(z.gotFloat);

    // The hot one releases, and what comes back out the far end is what went in.
    pack->SetFloatData(0, 0.5f);
    CHECK(x.gotFloat);
    CHECK(x.floatValue == doctest::Approx(0.5f));
    CHECK(y.gotFloat);
    CHECK(y.floatValue == doctest::Approx(1.5f));
    CHECK(z.gotFloat);
    CHECK(z.floatValue == doctest::Approx(-2.f));

    // And the round trip survives a re-send: a bang on the `.pack` puts the same
    // list back through, so the elements downstream are the ones the patch set
    // rather than whatever the `.unpack` happened to be holding.
    x.reset();
    y.reset();
    z.reset();
    pack->SetFloatData(2, 3.25f);
    pack->SetBang(0);
    CHECK(x.floatValue == doctest::Approx(0.5f));
    CHECK(y.floatValue == doctest::Approx(1.5f));
    CHECK(z.floatValue == doctest::Approx(3.25f));
  }

  TEST_CASE("unpack: the right-to-left order holds through the patcher's send path (#519)") {
    // The order is a promise a patch relies on, and the patcher's own send path
    // — pinned GraphState and all (#226) — is where it has to hold. `.i` is the
    // classic consumer: the second element lands in the cold inlet of an adder
    // and only then does the first element arrive at the hot one, so the sum a
    // real patch reads is of the *pair that just arrived*. Under a left-to-right
    // order it would be of the previous element.
    TestHelpers::MultiSink sum;
    YSE::pHandle sumHandle(&sum);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* unpack = p.CreateObject(YSE::OBJ::G_UNPACK, "0 0");
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::G_ADD, "0");
    REQUIRE(unpack != nullptr);
    REQUIRE(add != nullptr);
    p.Connect(unpack, 0, add, 0); // hot
    p.Connect(unpack, 1, add, 1); // cold
    p.Connect(add, 0, &sumHandle, 0);

    // `.+` answers in floats whatever it was given, so the assertion is on the
    // float outlet; what it pins is the ordering, not the spelling.
    unpack->SetListData(0, "10 5");
    CHECK(sum.gotFloat);
    CHECK(sum.floatValue == doctest::Approx(15.f));

    // A second list proves it is not luck: the cold value from *this* message is
    // the one used, not the one left over from the last.
    sum.reset();
    unpack->SetListData(0, "1 2");
    CHECK(sum.floatValue == doctest::Approx(3.f));
  }

} // TEST_SUITE

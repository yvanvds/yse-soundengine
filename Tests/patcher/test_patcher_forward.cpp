// Tests for .forward (issue #485) — a .s whose destination is chosen at
// runtime.
//
// Everything downstream of the name is .s's, so what is asserted here is the
// name: where it comes from, what changes it, what refuses to change it, and
// that changing it actually re-aims delivery rather than merely re-spelling a
// field. Six rules carry the object, and each is one a plausible
// implementation gets wrong:
//
//   - **the destination is live.** The whole object is "same source, different
//     receiver, no edit", so a `.forward` re-aimed between two values must
//     deliver them to two different `.r` nodes. An implementation that cached
//     the address once — which is exactly what `.s` correctly does — passes
//     every single-destination test and then never moves.
//   - **the destination has its own inlet, and inlet 0 has no reserved words.**
//     Max spells the change as `send <name>` into its one inlet; here that
//     would make the list `send voice2` unforwardable through the one object
//     whose contract is to forward anything. So inlet 0 must pass it through
//     untouched *and* leave the destination where it was.
//   - **an empty destination sends nothing.** Not "sends to the empty name",
//     which would give every unconfigured `.forward` in a same-named patcher a
//     real, shared bus address.
//   - **an over-long name is refused, not truncated.** NamedBus truncates a
//     published name at 63 bytes and the in-patcher PassData path does not, so
//     a truncated name would address two different receivers under one word.
//     The previous destination has to survive the refusal, or a bad message
//     silently disconnects the object.
//   - **a list gives its first token only.** A name with a space in it can
//     never match a `.r`, whose creation argument is tokenised the same way.
//   - **the destination is run-time state, not a parameter.** The creation
//     argument is what a saved patch carries; re-aiming at run time must not
//     rewrite what DumpJSON writes out.
//
// No audio device and no engine of its own. Delivery is driven through the
// in-patcher PassData path, which defers to the audio thread and is drained by
// an explicit Calculate (issue #225). Note that these cases must not *assume*
// the bus is inactive: in the monolithic test binary another TU may have called
// System::init() first, in which case a receiver in the same patcher is also
// subscribed to that patcher's own bus address and takes delivery by the other
// route. So what is asserted here is which receiver a value reaches, never by
// which of the two paths — that distinction needs a second patcher and lives in
// test_patcher_bus.cpp.

#include <doctest/doctest.h>
#include <string>

#include "patcher/genericObjects/gForward.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"

using TestHelpers::MultiSink;
using YSE::PATCHER::gForward;

namespace {

  // A patcher with two named receivers and one .forward pointed at whatever the
  // test says. The .forward is held directly rather than through a pHandle
  // because Destination() is the thing under test and pHandle does not hand the
  // object back; SetParent() is all the object needs from the patcher, and the
  // PassData path finds the receivers through the patcher rather than through
  // the graph.
  struct Rig {
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    gForward fwd;
    MultiSink a;
    MultiSink b;
    YSE::pHandle aHandle{&a};
    YSE::pHandle bHandle{&b};

    explicit Rig(const std::string& args = "") {
      YSE::pHandle* recvA = p.CreateObject(YSE::OBJ::G_RECEIVE, "alpha");
      YSE::pHandle* recvB = p.CreateObject(YSE::OBJ::G_RECEIVE, "beta");
      REQUIRE(recvA != nullptr);
      REQUIRE(recvB != nullptr);
      p.Connect(recvA, 0, &aHandle, 0);
      p.Connect(recvB, 0, &bHandle, 0);

      fwd.SetParams(args);
      fwd.SetParent(&p);
    }

    // Set the destination through the right inlet, the way a patch does.
    void Aim(const std::string& name) {
      fwd.GetInlet(1)->SetList(name, YSE::T_GUI);
    }

    // Values into the left inlet, then drain the in-patcher value queue.
    void SendInt(int value) {
      fwd.GetInlet(0)->SetInt(value, YSE::T_GUI);
      p.Calculate(YSE::T_DSP);
    }
    void SendList(const std::string& value) {
      fwd.GetInlet(0)->SetList(value, YSE::T_GUI);
      p.Calculate(YSE::T_DSP);
    }

    void Reset() {
      a.reset();
      b.reset();
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("forward: registered, two inlets, no outlets (#485)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_FORWARD);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".forward");
    // Max's forward sends to receive objects rather than out an outlet.
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 0);
  }

  TEST_CASE("forward: appears in the registry's name list (#485)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_FORWARD)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("forward: inlet 0 takes values, inlet 1 takes only a name (#485)") {
    gForward fwd;
    const unsigned int data = fwd.GetInlet(0)->GetAcceptedTypes();
    CHECK((data & YSE::PATCHER::IT_BANG) != 0);
    CHECK((data & YSE::PATCHER::IT_INT) != 0);
    CHECK((data & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((data & YSE::PATCHER::IT_LIST) != 0);

    // A name is a symbol or the number that spells one. A bang names nothing
    // and a float's spelling ("3.500000") would match no .r at all, so neither
    // is registered — leaving GetAcceptedTypes() reporting the real contract.
    const unsigned int dest = fwd.GetInlet(1)->GetAcceptedTypes();
    CHECK((dest & YSE::PATCHER::IT_LIST) != 0);
    CHECK((dest & YSE::PATCHER::IT_INT) != 0);
    CHECK((dest & YSE::PATCHER::IT_BANG) == 0);
    CHECK((dest & YSE::PATCHER::IT_FLOAT) == 0);
  }

  TEST_CASE("forward: without a parent patcher, sending data does not crash (#485)") {
    gForward fwd;
    fwd.SetParams("somewhere");
    fwd.GetInlet(0)->SetBang(YSE::T_GUI);
    fwd.GetInlet(0)->SetInt(1, YSE::T_GUI);
    fwd.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    fwd.GetInlet(0)->SetList("x", YSE::T_GUI);
    CHECK(fwd.Destination() == "somewhere");
  }

  // ─── the destination ────────────────────────────────────────────────────────

  TEST_CASE("forward: the creation argument is the starting destination (#485)") {
    Rig rig("alpha");
    CHECK(rig.fwd.Destination() == "alpha");

    rig.SendInt(7);
    CHECK(rig.a.gotInt);
    CHECK(rig.a.intValue == 7);
    CHECK_FALSE(rig.b.gotInt);
  }

  TEST_CASE("forward: re-aiming at run time sends the next value somewhere else (#485)") {
    // The object itself: one source, two receivers, no edit in between.
    Rig rig("alpha");

    rig.SendInt(1);
    CHECK(rig.a.gotInt);
    CHECK(rig.a.intValue == 1);
    CHECK_FALSE(rig.b.gotInt);

    rig.Reset();
    rig.Aim("beta");
    CHECK(rig.fwd.Destination() == "beta");

    rig.SendInt(2);
    CHECK_FALSE(rig.a.gotInt);
    CHECK(rig.b.gotInt);
    CHECK(rig.b.intValue == 2);
  }

  TEST_CASE("forward: bang, int, float and list all reach the current destination (#485)") {
    Rig rig;
    rig.Aim("alpha");

    rig.fwd.GetInlet(0)->SetBang(YSE::T_GUI);
    rig.fwd.GetInlet(0)->SetInt(11, YSE::T_GUI);
    rig.fwd.GetInlet(0)->SetFloat(0.125f, YSE::T_GUI);
    rig.fwd.GetInlet(0)->SetList("msg body", YSE::T_GUI);
    rig.p.Calculate(YSE::T_DSP);

    CHECK(rig.a.gotBang);
    CHECK(rig.a.gotInt);
    CHECK(rig.a.intValue == 11);
    CHECK(rig.a.gotFloat);
    CHECK(rig.a.floatValue == doctest::Approx(0.125f));
    CHECK(rig.a.gotList);
    CHECK(rig.a.listValue == "msg body");
  }

  TEST_CASE("forward: an int on the right inlet names the receiver it spells (#485)") {
    // A destination computed by a .counter has to reach the .r a patch author
    // typed, so the int-to-text spelling here must be the one Parameters::Set
    // gave that .r its name with.
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "3");
    REQUIRE(recv != nullptr);
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    p.Connect(recv, 0, &sinkHandle, 0);

    gForward fwd;
    fwd.SetParent(&p);
    fwd.GetInlet(1)->SetInt(3, YSE::T_GUI);
    CHECK(fwd.Destination() == "3");

    fwd.GetInlet(0)->SetInt(42, YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 42);
  }

  TEST_CASE("forward: a list gives its first token only (#485)") {
    // "alpha beta" would name a receiver that cannot exist: Parameters::Set
    // tokenises a .r's creation argument the same way.
    Rig rig;
    rig.Aim("  alpha beta  ");
    CHECK(rig.fwd.Destination() == "alpha");

    rig.SendInt(5);
    CHECK(rig.a.gotInt);
    CHECK_FALSE(rig.b.gotInt);
  }

  TEST_CASE("forward: inlet 0 forwards a list beginning with \"send\" verbatim (#485)") {
    // Max reserves the word `send` in its one inlet. Reserving it here would
    // cost .forward the property it is named for — and would silently re-aim
    // the object instead of delivering the message.
    Rig rig("alpha");
    rig.SendList("send beta");

    CHECK(rig.a.gotList);
    CHECK(rig.a.listValue == "send beta");
    CHECK(rig.fwd.Destination() == "alpha");
    CHECK_FALSE(rig.b.gotList);
  }

  // ─── what is refused ────────────────────────────────────────────────────────

  TEST_CASE("forward: with no destination it sends nothing (#485)") {
    Rig rig;
    CHECK(rig.fwd.Destination().empty());

    rig.fwd.GetInlet(0)->SetBang(YSE::T_GUI);
    rig.SendInt(1);
    rig.SendList("anything");
    CHECK_FALSE(rig.a.gotBang);
    CHECK_FALSE(rig.a.gotInt);
    CHECK_FALSE(rig.a.gotList);
    CHECK_FALSE(rig.b.gotInt);
  }

  TEST_CASE("forward: a name of 63 characters is accepted, 64 is refused (#485)") {
    // NamedBus truncates at 63 and the in-patcher path does not, so a longer
    // name would address two different receivers under one word.
    Rig rig("alpha");
    const std::string longest(gForward::MAX_NAME_LENGTH, 'x');
    rig.Aim(longest);
    CHECK(rig.fwd.Destination() == longest);

    const std::string tooLong(gForward::MAX_NAME_LENGTH + 1, 'y');
    rig.Aim(tooLong);
    // Refused, and the previous destination survives — a bad message must not
    // silently disconnect the object.
    CHECK(rig.fwd.Destination() == longest);
  }

  TEST_CASE("forward: an empty or whitespace-only name is refused (#485)") {
    Rig rig("alpha");
    rig.Aim("");
    CHECK(rig.fwd.Destination() == "alpha");
    rig.Aim("   ");
    CHECK(rig.fwd.Destination() == "alpha");

    rig.SendInt(3);
    CHECK(rig.a.gotInt);
  }

  TEST_CASE("forward: an over-long creation argument leaves no destination (#485)") {
    // Control thread, so this one is logged rather than silent — and the object
    // starts with nothing rather than with a truncated name.
    gForward fwd;
    fwd.SetParams(std::string(gForward::MAX_NAME_LENGTH + 1, 'z'));
    CHECK(fwd.Destination().empty());
  }

  TEST_CASE("forward: a destination with no matching receiver drops silently (#485)") {
    Rig rig("nowhere");
    rig.SendInt(9);
    CHECK_FALSE(rig.a.gotInt);
    CHECK_FALSE(rig.b.gotInt);
  }

  // ─── parameters ─────────────────────────────────────────────────────────────

  TEST_CASE("forward: the second argument is globalOnly, as on .s (#485)") {
    // Only the parsing is asserted here. What globalOnly *does* — suppress the
    // in-patcher fan-out while leaving the bus publish alone — cannot be seen
    // from inside one patcher: when the bus is live, a receiver in the same
    // patcher is subscribed to that patcher's own address and takes delivery
    // either way. The behaviour is asserted in test_patcher_bus.cpp, where a
    // second patcher makes the two paths tell apart.
    gForward fwd;
    fwd.SetParams("alpha 1");
    CHECK(fwd.Destination() == "alpha");
    CHECK(fwd.GlobalOnly() == 1);

    fwd.SetParams("beta");
    CHECK(fwd.Destination() == "beta");
    // Re-parsing clears what the previous argument string set, rather than
    // leaving half of it standing.
    CHECK(fwd.GlobalOnly() == 0);
  }

  TEST_CASE("forward: with globalOnly off, values reach a receiver in the same patcher (#485)") {
    Rig rig("alpha 0");
    rig.SendInt(4);
    CHECK(rig.a.gotInt);
    CHECK(rig.a.intValue == 4);
  }

  TEST_CASE("forward: re-parsing with an empty argument clears the destination (#485)") {
    gForward fwd;
    fwd.SetParams("alpha 1");
    CHECK(fwd.Destination() == "alpha");
    fwd.SetParams("");
    CHECK(fwd.Destination().empty());
  }

  TEST_CASE("forward: params survive a DumpJSON / ParseJSON round trip (#485)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_FORWARD, "alpha 1");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".forward") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".forward"));
    CHECK(copy->GetParams() == std::string("alpha 1"));
    CHECK(copy->GetInputs() == 2);
    CHECK(copy->GetOutputs() == 0);
  }

  TEST_CASE("forward: the destination is run-time state, not a parameter (#485)") {
    // What a saved patch carries is where the object *started*. Re-aiming it is
    // a message, exactly as .router's connections and .cycle's index are, and a
    // message must not rewrite the file.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_FORWARD, "alpha");
    REQUIRE(h != nullptr);
    h->SetListData(1, "beta");
    CHECK(h->GetParams() == std::string("alpha"));
    CHECK(src.DumpJSON().find("beta") == std::string::npos);
  }

} // TEST_SUITE("patcher")

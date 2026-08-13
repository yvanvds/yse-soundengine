// Tests for .dict.route (issue #777) — Max's dict.route on the
// name-addressed value model .dict settled (#550). The dispatcher for
// structured messages: what .route does for list text, for dictionaries.
//
// What has to be proven, and what every case below is one of:
//
//   - **the dictionary is bound from the first creation argument.** A
//     dictionary never travels down a cord, so ".dict.route <name> <key>
//     ..." resolves the name once, on the control thread, and a `dictionary
//     <name>` message is honoured only when it names the dictionary already
//     bound — DictReferenceNames' bounded compare, never a registry lookup
//     on a message path.
//   - **the routing decision is the presence of a key.** The reference —
//     never the contents — leaves the outlet of the leftmost key argument
//     present in the dictionary, an entry at the path or under it; exactly
//     one outlet fires per trigger, and a dictionary holding none of the
//     keys leaves the rightmost reject outlet with its reference unchanged,
//     gRoute's rule.
//   - **the decision crosses the control/audio boundary and nothing
//     allocates.** In-patcher delivery dispatches on T_DSP, so "the audio
//     thread routes a dictionary" is the ordinary case.
//
// No audio device required. The registry is process-wide, so every case
// that names a dictionary uses names of its own.

#include <doctest/doctest.h>
#include <cstdint>
#include <functional>
#include <string>

#include "patcher/genericObjects/gDict.h"
#include "patcher/genericObjects/gDictRoute.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using TestHelpers::Wire;
using YSE::PATCHER::gDict;
using YSE::PATCHER::gDictRoute;

namespace {

  // Records every reference it receives — a count as well as the last text,
  // so "exactly one outlet fires" is provable across a row of these.
  // `onList` lets a case act from *inside* the send: the loop-back trigger
  // is a thing the reference's own subgraph does.
  struct RefSink : YSE::PATCHER::pObject {
    std::string received;
    int count = 0;
    std::function<void()> onList;

    RefSink() : pObject(false) {
      received.reserve(256);
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterList([this](const std::string& v, int, YSE::THREAD) {
        received = v;
        count++;
        if (onList) onList();
      });
    }
    const char* Type() const override {
      return "ref_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // A .dict and a .dict.route on one name, sharing one
  // patcherImplementation so the name actually binds ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private).
  // Two match outlets and the reject, one sink each. The sinks are declared
  // before the objects so they are torn down last, while the outlets wired
  // to them still exist (see sinks.hpp on why that matters).
  struct Rig {
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    RefSink outA;
    RefSink outB;
    RefSink outNone;
    gDict dict;
    gDictRoute route;

    Rig(const std::string& patcherName, const std::string& name, const std::string& keys) {
      p.SetName(patcherName);
      dict.SetParent(&p);
      dict.SetParams(name);
      route.SetParent(&p);
      route.SetParams(name + " " + keys);
      Wire(route, 0, outA);
      Wire(route, 1, outB);
      Wire(route, 2, outNone);
    }

    void Store(const std::string& message) {
      dict.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void Bang(YSE::THREAD thread = YSE::T_GUI) {
      route.GetInlet(0)->SetBang(thread);
    }
    int Total() const {
      return outA.count + outB.count + outNone.count;
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("dict.route: registered, one inlet, one outlet per key plus the reject (#777)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DICT_ROUTE, "d777a voice ctl");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".dict.route");
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 3);
  }

  TEST_CASE("dict.route: appears in the registry's name list (#777)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_DICT_ROUTE)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("dict.route: with no key arguments there is only the reject outlet (#777)") {
    // .routepass's bare shape, not .route's default-0: Max documents no
    // default key for dict.route, and inventing one would put a branch in a
    // patch that did not ask for one.
    {
      gDictRoute g;
      CHECK(g.KeyCount() == 0);
      CHECK(g.NumOutputs() == 1);
    }
    {
      gDictRoute g;
      g.SetParams("d777b");
      CHECK(g.KeyCount() == 0);
      CHECK(g.NumOutputs() == 1);
    }
  }

  TEST_CASE("dict.route: the inlet takes bang and list, and no bare number (#777)") {
    // No int or float handler: a bare number names no dictionary — the
    // family's rule.
    gDictRoute g;
    const unsigned int accepted = g.GetInlet(0)->GetAcceptedTypes();
    CHECK((accepted & YSE::PATCHER::IT_BANG) != 0);
    CHECK((accepted & YSE::PATCHER::IT_LIST) != 0);
    CHECK((accepted & YSE::PATCHER::IT_INT) == 0);
    CHECK((accepted & YSE::PATCHER::IT_FLOAT) == 0);
  }

  // ─── creation arguments ─────────────────────────────────────────────────────

  TEST_CASE("dict.route: the name then the keys, and '' resets (#777)") {
    gDictRoute g;
    g.SetParams("d777c voice ctl");
    CHECK(g.DictName() == "d777c");
    REQUIRE(g.KeyCount() == 2);
    CHECK(g.KeyAt(0) == "voice");
    CHECK(g.KeyAt(1) == "ctl");
    CHECK(g.NumOutputs() == 3);

    // SetParams("") has to leave the no-argument object behind rather than
    // one still holding the previous binding and branches.
    g.SetParams("");
    CHECK(g.DictName().empty());
    CHECK(g.Address().empty());
    CHECK(g.KeyCount() == 0);
    CHECK(g.NumOutputs() == 1);
  }

  // ─── the routing decision ───────────────────────────────────────────────────

  TEST_CASE("dict.route: the reference leaves the outlet of the leftmost key present (#777)") {
    Rig rig("dr777d", "d777d", "voice ctl");

    // Holds "ctl" only: outlet 1.
    rig.Store("set ctl::cutoff 800");
    rig.Bang();
    CHECK(rig.outA.count == 0);
    REQUIRE(rig.outB.count == 1);
    CHECK(rig.outNone.count == 0);
    CHECK(rig.outB.received == "dictionary d777d");
    CHECK(rig.route.Routed() == 1);
    CHECK(rig.route.Dropped() == 0);

    // Now holds "voice" too: both keys present, and the leftmost wins —
    // exactly one outlet fires per trigger, gRoute's rule.
    rig.Store("set voice::1::freq 440");
    rig.Bang();
    REQUIRE(rig.outA.count == 1);
    CHECK(rig.outB.count == 1);
    CHECK(rig.outNone.count == 0);
    CHECK(rig.outA.received == "dictionary d777d");
    CHECK(rig.Total() == 2);
  }

  TEST_CASE("dict.route: presence means an entry at the path or under it (#777)") {
    // "voice" is present through "voice::1::freq" (under it) and through a
    // plain "voice" leaf (at it) alike — the family's sub-tree question. A
    // key that merely shares a prefix ("voicing") is a different key.
    {
      Rig rig("dr777e", "d777e", "voice ctl");
      rig.Store("set voice solo"); // a leaf at the path itself
      rig.Bang();
      CHECK(rig.outA.count == 1);
    }
    {
      Rig rig("dr777f", "d777f", "voice ctl");
      rig.Store("set voicing close"); // shares the prefix, is not the key
      rig.Bang();
      CHECK(rig.outA.count == 0);
      CHECK(rig.outB.count == 0);
      CHECK(rig.outNone.count == 1);
    }
  }

  TEST_CASE("dict.route: a key argument may be a path of its own (#777)") {
    // "voice::1" tests two levels down the same way — presence is the same
    // at-or-under question asked of a longer prefix.
    Rig rig("dr777g", "d777g", "voice::1 voice");
    rig.Store("set voice::2::freq 220");
    rig.Bang();
    // "voice::1" absent, "voice" present: outlet 1.
    CHECK(rig.outA.count == 0);
    REQUIRE(rig.outB.count == 1);

    rig.Store("set voice::1::freq 440");
    rig.Bang();
    // Now "voice::1" is present, and it is leftmost.
    CHECK(rig.outA.count == 1);
    CHECK(rig.outB.count == 1);
  }

  TEST_CASE("dict.route: a dictionary holding none of the keys leaves the reject (#777)") {
    // The reference is unchanged, so chaining into the next .dict.route
    // carries on testing the dictionary the first one saw — and an empty
    // dictionary holds no keys at all.
    Rig rig("dr777h", "d777h", "voice ctl");
    rig.Bang();
    REQUIRE(rig.outNone.count == 1);
    CHECK(rig.outNone.received == "dictionary d777h");

    rig.Store("set meta::title warm");
    rig.Bang();
    CHECK(rig.outNone.count == 2);
    CHECK(rig.outA.count == 0);
    CHECK(rig.outB.count == 0);
  }

  TEST_CASE("dict.route: a key repeated in the argument list uses its leftmost outlet (#777)") {
    Rig rig("dr777i", "d777i", "voice voice");
    rig.Store("set voice::1::freq 440");
    rig.Bang();
    CHECK(rig.outA.count == 1);
    CHECK(rig.outB.count == 0);
    CHECK(rig.outNone.count == 0);
  }

  TEST_CASE("dict.route: the reference routes, anything else is refused (#777)") {
    Rig rig("dr777j", "d777j", "voice ctl");
    rig.Store("set voice::1::freq 440");

    // The dictionary's own reference — the message its .dict emits on a
    // bang — routes, exactly as a bang does.
    const std::uint64_t before = rig.route.Dropped();
    rig.route.GetInlet(0)->SetList("dictionary d777j", YSE::T_GUI);
    REQUIRE(rig.outA.count == 1);
    CHECK(rig.route.Dropped() == before);

    // A reference to a dictionary this object is not bound to, and any
    // other message, are refused and counted, never resolved: a registry
    // lookup is a mutex, and this may be the audio thread. A refused
    // trigger sends nothing.
    rig.route.GetInlet(0)->SetList("dictionary somewhere_else", YSE::T_GUI);
    rig.route.GetInlet(0)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK(rig.Total() == 1);
    CHECK(rig.route.Dropped() == before + 2);
  }

  TEST_CASE("dict.route: an unnamed object is inert (#777)") {
    // A private dictionary has no name to pass on, so a trigger routes
    // nothing and sends nothing — silently, not counted: the wiring is not
    // an error, merely incomplete.
    RefSink out;
    gDictRoute g;
    g.SetParams(""); // bare: reject outlet only
    Wire(g, 0, out);
    g.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(out.count == 0);
    CHECK(g.Routed() == 0);
    CHECK(g.Dropped() == 0);
  }

  TEST_CASE("dict.route: a trigger looping back from an outlet is refused (#777)") {
    // The emitted reference is itself a trigger, so an outlet wired back
    // into the inlet — directly or round a chain — would recurse without
    // bound. The guard held across decision and send drops it, counted.
    Rig rig("dr777k", "d777k", "voice ctl");
    rig.Store("set voice::1::freq 440");

    int reentered = 0;
    rig.outA.onList = [&rig, &reentered]() {
      if (reentered++ > 0) return; // once is the proof; don't recurse forever
      rig.route.GetInlet(0)->SetBang(YSE::T_GUI);
    };

    const std::uint64_t before = rig.route.Dropped();
    rig.Bang();
    CHECK(rig.outA.count == 1);
    CHECK(rig.route.Routed() == 1);
    CHECK(rig.route.Dropped() == before + 1);
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("dict.route: the address form is the patcher's, and RefreshBinding follows it (#777)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dr777l_before");

    gDictRoute g;
    g.SetParams("d777l voice");
    g.SetParent(&p);
    CHECK(g.DictName() == "d777l");
    CHECK(g.Address() == "dr777l_before.d777l");

    // Idempotent: a rebind to the address it already has keeps the store.
    g.RefreshBinding();
    CHECK(g.Address() == "dr777l_before.d777l");

    p.SetName("dr777l_after");
    g.RefreshBinding();
    CHECK(g.Address() == "dr777l_after.d777l");
  }

  TEST_CASE("dict.route: patcherImplementation::SetName re-anchors it (#777)") {
    // The rename dispatch itself, which the standalone case above cannot
    // reach: an object created *inside* a patcher must be re-anchored by
    // the patcher, without anybody calling RefreshBinding by hand. The
    // keeper holds the old-address store; after the rename the route tests
    // a fresh empty dictionary under the new prefix, which holds no keys —
    // the reject.
    RefSink outA;
    RefSink outNone;
    YSE::pHandle aHandle(&outA);
    YSE::pHandle noneHandle(&outNone);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dr777m_before");

    gDict keeper;
    keeper.SetParent(&p);
    keeper.SetParams("d777m");
    keeper.GetInlet(0)->SetList("set voice::1::freq 440", YSE::T_GUI);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DICT_ROUTE, "d777m voice");
    REQUIRE(h != nullptr);
    p.Connect(h, 0, &aHandle, 0);
    p.Connect(h, 1, &noneHandle, 0);

    h->SetBang(0);
    REQUIRE(outA.count == 1);
    CHECK(outNone.count == 0);

    p.SetName("dr777m_after");
    h->SetBang(0);
    CHECK(outA.count == 1);
    CHECK(outNone.count == 1);
  }

  TEST_CASE("dict.route: wired from the dict's reference outlet, banging the dict routes (#777)") {
    // The flow a patch actually wires: the .dict's reference outlet into
    // the router, a bang on the .dict, and the dictionary dispatches
    // itself. Max's own gesture.
    Rig rig("dr777n", "d777n", "voice ctl");
    Wire(rig.dict, 1, rig.route);
    rig.Store("set ctl::cutoff 800");

    rig.dict.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(rig.outB.count == 1);
    CHECK(rig.outB.received == "dictionary d777n");
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("dict.route: a route asked for over in-patcher delivery lands on T_DSP (#777)") {
    // A .r feeding the router dispatches on T_DSP when the block drains it
    // (issue #225) — "the audio thread routes a dictionary" is the ordinary
    // case, and the whole path is one bounded scan and one send.
    RefSink outA;
    RefSink outNone;
    YSE::pHandle aHandle(&outA);
    YSE::pHandle noneHandle(&outNone);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dr777o");

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go777o");
    YSE::pHandle* dict = p.CreateObject(YSE::OBJ::G_DICT, "d777o");
    YSE::pHandle* route = p.CreateObject(YSE::OBJ::G_DICT_ROUTE, "d777o voice");
    REQUIRE(recv != nullptr);
    REQUIRE(dict != nullptr);
    REQUIRE(route != nullptr);
    p.Connect(recv, 0, route, 0);
    p.Connect(route, 0, &aHandle, 0);
    p.Connect(route, 1, &noneHandle, 0);

    dict->SetListData(0, "set voice::1::freq 440");

    p.PassData(std::string("dictionary d777o"), "go777o", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);

    REQUIRE(outA.count == 1);
    CHECK(outA.received == "dictionary d777o");
    CHECK(outNone.count == 0);
  }

  TEST_CASE("dict.route: no message path allocates (#777)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path: a matched route (bang and reference alike), a
    // reject route, the wrong-name refusal, and the unknown message.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string reference = "dictionary probeD777";
    const std::string wrongName = "dictionary somewhere_else_long";
    const std::string unknown = "frobnicate something quite long indeed";

    Rig rig("dr777p", "probeD777", "voice ctl");
    rig.Store("set ctl::cutoff a value past every small-string buffer");
    rig.Store("set meta::title warm");

    Rig empty("dr777q", "probeD777none", "voice ctl");

    // Warm every path — including the sinks' list assignment — so
    // first-call machinery is not what the probe catches.
    rig.Bang();
    rig.route.GetInlet(0)->SetList(reference, YSE::T_GUI);
    rig.route.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    rig.route.GetInlet(0)->SetList(unknown, YSE::T_GUI);
    empty.Bang();
    const std::uint64_t routedBefore = rig.route.Routed();
    const std::uint64_t droppedBefore = rig.route.Dropped();
    const std::uint64_t rejectedBefore = empty.route.Routed();

    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      rig.Bang(YSE::T_DSP);
      rig.route.GetInlet(0)->SetList(reference, YSE::T_DSP);
      rig.route.GetInlet(0)->SetList(wrongName, YSE::T_DSP);
      rig.route.GetInlet(0)->SetList(unknown, YSE::T_DSP);
      empty.Bang(YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing. Two matched routes, one reject, and both
    // refusals counted.
    CHECK(rig.route.Routed() == routedBefore + 2);
    CHECK(rig.route.Dropped() == droppedBefore + 2);
    CHECK(empty.route.Routed() == rejectedBefore + 1);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("dict.route: params survive a DumpJSON / ParseJSON round trip (#777)") {
    // The creation arguments have to come back: a reloaded patch whose
    // .dict.route lost its name or its keys would route a different
    // dictionary — or nothing — down different branches.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_DICT_ROUTE, "cfg777 voice ctl");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".dict.route"));
    CHECK(copy->GetParams() == std::string("cfg777 voice ctl"));
    CHECK(copy->GetInputs() == 1);
    CHECK(copy->GetOutputs() == 3);
  }

  TEST_CASE("dict.route: carries complete documentation metadata (#777)") {
    gDictRoute g;
    g.SetParams("d777r voice ctl");
    CHECK_FALSE(g.GetDescription().empty());
    CHECK(g.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    const auto& docs = g.GetParamDocs();
    REQUIRE(docs.size() == 2);
    CHECK(docs[0].name == "name");
    CHECK(docs[1].name == "keys");
  }
}

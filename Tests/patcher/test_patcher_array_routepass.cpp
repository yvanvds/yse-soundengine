// Tests for .array.routepass (issue #804) — Max's array.routepass on the
// name-addressed value model .array settled (#548). The dispatcher for
// arrays: what .route does for list text and .dict.route does for
// dictionaries, this object does for sequences — .route's job with the
// array as the selector.
//
// What has to be proven, and what every case below is one of:
//
//   - **the array is bound from the first creation argument.** An array
//     never travels down a cord, so ".array.routepass <name> <value> ..."
//     resolves the name once, on the control thread, and an `array <name>`
//     message is honoured only when it names the array already bound —
//     ArrayReferenceNames' bounded compare, never a registry lookup on a
//     message path.
//   - **the routing decision is the presence of a value.** The reference —
//     never the contents — leaves the outlet of the leftmost value argument
//     some element spells exactly (ArrayFind's byte compare, the family's
//     spelling-is-identity rule); exactly one outlet fires per trigger, and
//     an array holding none of the values leaves the rightmost reject with
//     its reference unchanged, gRoute's rule.
//   - **the decision crosses the control/audio boundary and nothing
//     allocates.** In-patcher delivery dispatches on T_DSP, so "the audio
//     thread routes an array" is the ordinary case.
//
// No audio device required. The registry is process-wide, so every case
// that names an array uses names of its own.

#include <doctest/doctest.h>
#include <cstdint>
#include <functional>
#include <string>

#include "patcher/genericObjects/gArray.h"
#include "patcher/genericObjects/gArrayRoutepass.h"
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
using YSE::PATCHER::gArray;
using YSE::PATCHER::gArrayRoutepass;

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

  // An .array and an .array.routepass on one name, sharing one
  // patcherImplementation so the name actually binds ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private).
  // The first two match outlets and the reject, one sink each: outA is
  // match outlet 0, outB match outlet 1 when there is one, and outNone is
  // always the rightmost reject, wherever the argument count put it. The
  // sinks are declared before the objects so they are torn down last, while
  // the outlets wired to them still exist (see sinks.hpp on why that
  // matters).
  struct Rig {
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    RefSink outA;
    RefSink outB;
    RefSink outNone;
    gArray array;
    gArrayRoutepass route;

    Rig(const std::string& patcherName, const std::string& name, const std::string& values) {
      p.SetName(patcherName);
      array.SetParent(&p);
      array.SetParams(name);
      route.SetParent(&p);
      route.SetParams(name + " " + values);
      const int outs = route.NumOutputs();
      REQUIRE(outs >= 2);
      Wire(route, 0, outA);
      if (outs > 2) Wire(route, 1, outB);
      Wire(route, outs - 1, outNone);
    }

    void Store(const std::string& message) {
      array.GetInlet(0)->SetList(message, YSE::T_GUI);
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

  TEST_CASE("array.routepass: registered, two inlets, one outlet per value plus the reject "
            "(#804)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY_ROUTEPASS, "a804a note ctl");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".array.routepass");
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 3);

    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_ARRAY_ROUTEPASS)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("array.routepass: with no value arguments there is only the reject outlet (#804)") {
    // .routepass's own bare shape: Max documents no default key for
    // array.routepass, and inventing one would put a branch in a patch that
    // did not ask for one.
    {
      gArrayRoutepass g;
      CHECK(g.SelectorCount() == 0);
      CHECK(g.NumOutputs() == 1);
    }
    {
      gArrayRoutepass g;
      g.SetParams("a804b");
      CHECK(g.SelectorCount() == 0);
      CHECK(g.NumOutputs() == 1);
    }
  }

  TEST_CASE("array.routepass: the trigger takes bang and list and no bare number, the "
            "reference inlet only list text (#804)") {
    // No int or float handler anywhere — gArrayStatsBase's shape: the route
    // is asked for with a bang, never addressed, and a bare number names no
    // array.
    gArrayRoutepass g;
    const unsigned int trigger = g.GetInlet(0)->GetAcceptedTypes();
    CHECK((trigger & YSE::PATCHER::IT_BANG) != 0);
    CHECK((trigger & YSE::PATCHER::IT_LIST) != 0);
    CHECK((trigger & YSE::PATCHER::IT_INT) == 0);
    CHECK((trigger & YSE::PATCHER::IT_FLOAT) == 0);
    const unsigned int ref = g.GetInlet(1)->GetAcceptedTypes();
    CHECK((ref & YSE::PATCHER::IT_LIST) != 0);
    CHECK((ref & YSE::PATCHER::IT_BANG) == 0);
  }

  // ─── creation arguments ─────────────────────────────────────────────────────

  TEST_CASE("array.routepass: the name then the values, and '' resets (#804)") {
    gArrayRoutepass g;
    g.SetParams("a804c note ctl");
    CHECK(g.ArrayName() == "a804c");
    REQUIRE(g.SelectorCount() == 2);
    CHECK(g.SelectorAt(0) == "note");
    CHECK(g.SelectorAt(1) == "ctl");
    CHECK(g.NumOutputs() == 3);
    CHECK(g.Reference() == "array a804c");

    // SetParams("") has to leave the no-argument object behind rather than
    // one still holding the previous binding and branches.
    g.SetParams("");
    CHECK(g.ArrayName().empty());
    CHECK(g.Address().empty());
    CHECK(g.Reference().empty());
    CHECK(g.SelectorCount() == 0);
    CHECK(g.NumOutputs() == 1);
  }

  TEST_CASE("array.routepass: at most 256 values are held, tokens past the ceiling dropped "
            "(#804)") {
    // .routepass's own ceiling: every value costs an outlet, so the count is
    // bounded by a number chosen here rather than by whoever typed the
    // arguments.
    std::string params = "a804d";
    for (int i = 0; i < 300; i++)
      params += " v" + std::to_string(i);
    gArrayRoutepass g;
    g.SetParams(params);
    CHECK(g.SelectorCount() == 256);
    CHECK(g.NumOutputs() == 257);
    CHECK(g.SelectorAt(255) == "v255");
  }

  // ─── the routing decision ───────────────────────────────────────────────────

  TEST_CASE("array.routepass: the reference leaves the outlet of the leftmost value present "
            "(#804)") {
    Rig rig("ar804e", "a804e", "note ctl");

    // Holds "ctl" only: outlet 1.
    rig.Store("append ctl 74");
    rig.Bang();
    CHECK(rig.outA.count == 0);
    REQUIRE(rig.outB.count == 1);
    CHECK(rig.outNone.count == 0);
    CHECK(rig.outB.received == "array a804e");
    CHECK(rig.route.Routed() == 1);
    CHECK(rig.route.Dropped() == 0);

    // Now holds "note" too: both values present, and the leftmost argument
    // wins — exactly one outlet fires per trigger, gRoute's rule.
    rig.Store("append note 60");
    rig.Bang();
    REQUIRE(rig.outA.count == 1);
    CHECK(rig.outB.count == 1);
    CHECK(rig.outNone.count == 0);
    CHECK(rig.outA.received == "array a804e");
    CHECK(rig.Total() == 2);
  }

  TEST_CASE("array.routepass: presence means anywhere in the array, not the first element "
            "(#804)") {
    // #804's reading of Max's match modes — the containment question, the
    // one the value model could not already spell: routing on the first
    // element alone is .array.at into the scalar .routepass.
    Rig rig("ar804f", "a804f", "note");
    rig.Store("append 60 64 note");
    rig.Bang();
    CHECK(rig.outA.count == 1);
    CHECK(rig.outNone.count == 0);
  }

  TEST_CASE("array.routepass: a value matches only the element that spells it — the family's "
            "byte compare (#804)") {
    // Spelling is identity: 5 and 5. are different elements, a deliberate
    // divergence from the scalar .routepass's numeric widening, written down
    // in gArrayRoutepass.h. An array element *is* its spelling.
    Rig rig("ar804g", "a804g", "5");
    rig.Store("append 5.");
    rig.Bang();
    CHECK(rig.outA.count == 0);
    REQUIRE(rig.outNone.count == 1);

    rig.Store("append 5");
    rig.Bang();
    CHECK(rig.outA.count == 1);
    CHECK(rig.outNone.count == 1);
  }

  TEST_CASE("array.routepass: an array holding none of the values leaves the reject (#804)") {
    // The reference is unchanged, so chaining into the next .array.routepass
    // carries on testing the array the first one saw — and an empty array
    // holds no values at all.
    Rig rig("ar804h", "a804h", "note ctl");
    rig.Bang();
    REQUIRE(rig.outNone.count == 1);
    CHECK(rig.outNone.received == "array a804h");

    rig.Store("append bend 8192");
    rig.Bang();
    CHECK(rig.outNone.count == 2);
    CHECK(rig.outA.count == 0);
    CHECK(rig.outB.count == 0);
  }

  TEST_CASE("array.routepass: a value repeated in the argument list uses its leftmost outlet "
            "(#804)") {
    Rig rig("ar804i", "a804i", "note note");
    rig.Store("append note");
    rig.Bang();
    CHECK(rig.outA.count == 1);
    CHECK(rig.outB.count == 0);
    CHECK(rig.outNone.count == 0);
  }

  TEST_CASE("array.routepass: an empty or over-long token costs an unreachable outlet and "
            "keeps the indices parallel (#804)") {
    // Every kept token gets an outlet, empty ones included, so an index into
    // the arguments is an index into the outlets — gDictRoute's rule. The
    // tokenizer splits on single spaces, so the run of two below yields an
    // empty token; no stored element can spell it, nor one past 64
    // characters, so their outlets never fire and the values after them
    // still route to their own.
    Rig rig("ar804j", "a804j", "x  y");
    REQUIRE(rig.route.SelectorCount() == 3);
    CHECK(rig.route.SelectorAt(1).empty());
    CHECK(rig.route.NumOutputs() == 4);

    RefSink outY;
    Wire(rig.route, 2, outY);
    rig.Store("append y");
    rig.Bang();
    CHECK(rig.outA.count == 0);
    CHECK(rig.outB.count == 0); // the empty token's outlet, unreachable
    CHECK(outY.count == 1);

    const std::string overlong(70, 'q');
    Rig longRig("ar804k", "a804k", overlong);
    longRig.Store("append q");
    longRig.Bang();
    CHECK(longRig.outA.count == 0);
    CHECK(longRig.outNone.count == 1);
  }

  TEST_CASE("array.routepass: the reference routes, anything else is refused (#804)") {
    Rig rig("ar804l", "a804l", "note ctl");
    rig.Store("append note 60");

    // The array's own reference — the message its .array emits on a bang —
    // routes, exactly as a bang does.
    const std::uint64_t before = rig.route.Dropped();
    rig.route.GetInlet(0)->SetList("array a804l", YSE::T_GUI);
    REQUIRE(rig.outA.count == 1);
    CHECK(rig.route.Dropped() == before);

    // A reference to an array this object is not bound to, and any other
    // message, are refused and counted, never resolved: a registry lookup
    // is a mutex, and this may be the audio thread. A refused trigger sends
    // nothing.
    rig.route.GetInlet(0)->SetList("array somewhere_else", YSE::T_GUI);
    rig.route.GetInlet(0)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK(rig.Total() == 1);
    CHECK(rig.route.Dropped() == before + 2);

    // The reference inlet acknowledges the bound name silently and refuses
    // anything else — the family's cold inlet, wired across so a patch may
    // pass the array's reference outlet by.
    rig.route.GetInlet(1)->SetList("array a804l", YSE::T_GUI);
    CHECK(rig.route.Dropped() == before + 2);
    rig.route.GetInlet(1)->SetList("array somewhere_else", YSE::T_GUI);
    CHECK(rig.route.Dropped() == before + 3);
    CHECK(rig.Total() == 1);
  }

  TEST_CASE("array.routepass: an unnamed object is inert (#804)") {
    // A private array has no name to pass on, so a trigger routes nothing
    // and sends nothing — silently, not counted: the wiring is not an
    // error, merely incomplete.
    RefSink out;
    gArrayRoutepass g;
    g.SetParams(""); // bare: reject outlet only
    Wire(g, 0, out);
    g.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(out.count == 0);
    CHECK(g.Routed() == 0);
    CHECK(g.Dropped() == 0);
  }

  TEST_CASE("array.routepass: a trigger looping back from an outlet is refused (#804)") {
    // The emitted reference is itself a trigger, so an outlet wired back
    // into the inlet — directly or round a chain — would recurse without
    // bound. The guard held across decision and send drops it, counted.
    Rig rig("ar804m", "a804m", "note ctl");
    rig.Store("append note 60");

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

  TEST_CASE("array.routepass: the address form is the patcher's, and RefreshBinding follows "
            "it (#804)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ar804n_before");

    gArrayRoutepass g;
    g.SetParams("a804n note");
    g.SetParent(&p);
    CHECK(g.ArrayName() == "a804n");
    CHECK(g.Address() == "ar804n_before.a804n");

    // Idempotent: a rebind to the address it already has keeps the store.
    g.RefreshBinding();
    CHECK(g.Address() == "ar804n_before.a804n");

    p.SetName("ar804n_after");
    g.RefreshBinding();
    CHECK(g.Address() == "ar804n_after.a804n");
  }

  TEST_CASE("array.routepass: patcherImplementation::SetName re-anchors it (#804)") {
    // The rename dispatch itself, which the standalone case above cannot
    // reach: an object created *inside* a patcher must be re-anchored by
    // the patcher, without anybody calling RefreshBinding by hand. The
    // keeper holds the old-address store; after the rename the route tests
    // a fresh empty array under the new prefix, which holds no values —
    // the reject.
    RefSink outA;
    RefSink outNone;
    YSE::pHandle aHandle(&outA);
    YSE::pHandle noneHandle(&outNone);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ar804o_before");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a804o");
    keeper.GetInlet(0)->SetList("append note 60", YSE::T_GUI);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY_ROUTEPASS, "a804o note");
    REQUIRE(h != nullptr);
    p.Connect(h, 0, &aHandle, 0);
    p.Connect(h, 1, &noneHandle, 0);

    h->SetBang(0);
    REQUIRE(outA.count == 1);
    CHECK(outNone.count == 0);

    p.SetName("ar804o_after");
    h->SetBang(0);
    CHECK(outA.count == 1);
    CHECK(outNone.count == 1);
  }

  TEST_CASE("array.routepass: wired from the array's reference outlet, banging the array "
            "routes (#804)") {
    // The flow a patch actually wires: the .array's reference outlet into
    // the router, a bang on the .array, and the array dispatches itself.
    // Max's own gesture.
    Rig rig("ar804p", "a804p", "note ctl");
    Wire(rig.array, 1, rig.route);
    rig.Store("append ctl 74");

    rig.array.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(rig.outB.count == 1);
    CHECK(rig.outB.received == "array a804p");
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("array.routepass: a route asked for over in-patcher delivery lands on T_DSP "
            "(#804)") {
    // A .r feeding the router dispatches on T_DSP when the block drains it
    // (issue #225) — "the audio thread routes an array" is the ordinary
    // case, and the whole path is one bounded scan and one send.
    RefSink outA;
    RefSink outNone;
    YSE::pHandle aHandle(&outA);
    YSE::pHandle noneHandle(&outNone);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ar804q");

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go804q");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "a804q");
    YSE::pHandle* route = p.CreateObject(YSE::OBJ::G_ARRAY_ROUTEPASS, "a804q note");
    REQUIRE(recv != nullptr);
    REQUIRE(array != nullptr);
    REQUIRE(route != nullptr);
    p.Connect(recv, 0, route, 0);
    p.Connect(route, 0, &aHandle, 0);
    p.Connect(route, 1, &noneHandle, 0);

    array->SetListData(0, "append note 60");

    p.PassData(std::string("array a804q"), "go804q", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);

    REQUIRE(outA.count == 1);
    CHECK(outA.received == "array a804q");
    CHECK(outNone.count == 0);
  }

  TEST_CASE("array.routepass: no message path allocates (#804)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path: a matched route (bang and reference alike), a
    // reject route, the wrong-name refusal, and the unknown message.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string reference = "array probeA804";
    const std::string wrongName = "array somewhere_else_long";
    const std::string unknown = "frobnicate something quite long indeed";

    Rig rig("ar804r", "probeA804", "note ctl");
    rig.Store("append ctl a-value-past-every-small-string-buffer-by-some-margin");
    rig.Store("append bend 8192");

    Rig empty("ar804s", "probeA804none", "note ctl");

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

  TEST_CASE("array.routepass: params survive a DumpJSON / ParseJSON round trip (#804)") {
    // The creation arguments have to come back: a reloaded patch whose
    // .array.routepass lost its name or its values would route a different
    // array — or nothing — down different branches.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_ARRAY_ROUTEPASS, "cfg804 note ctl");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".array.routepass"));
    CHECK(copy->GetParams() == std::string("cfg804 note ctl"));
    CHECK(copy->GetInputs() == 2);
    CHECK(copy->GetOutputs() == 3);
  }

  TEST_CASE("array.routepass: carries complete documentation metadata (#804)") {
    gArrayRoutepass g;
    g.SetParams("a804t note ctl");
    CHECK_FALSE(g.GetDescription().empty());
    CHECK(g.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    const auto& docs = g.GetParamDocs();
    REQUIRE(docs.size() == 2);
    CHECK(docs[0].name == "name");
    CHECK(docs[1].name == "values");
  }
}

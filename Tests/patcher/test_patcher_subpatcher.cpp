// Tests for subpatchers — `patcher`, `.inlet` and `.outlet` (issue #545).
//
// Three layers are being pinned here and they are not the same claim.
//
// The **boundary objects** are small: `.inlet` and `.outlet` are pass-throughs
// with an index, and their cases drive standalone objects through a tap.
//
// The **encapsulation** is what the issue is for, and it can only be tested at
// the level a user meets it: build a real patcher, put objects inside a
// `patcher` object, address the *group* by pin number from outside, and assert
// that a value sent in at the top arrives at the far end having been through
// the encapsulated graph. Every such case is paired with a control, because a
// boundary that silently did nothing and a boundary that worked would otherwise
// be told apart by nothing.
//
// The **architecture** is the third layer and the reason the issue was called
// the heaviest in its set. Storage is flat and only addressing nests, which is
// a claim with observable consequences, and each of them is a case here:
//
//   - a nested graph round-trips through DumpJSON / ParseJSON, and dumping the
//     reloaded patch produces the same bytes — so containment is persisted, not
//     merely rebuilt by the test;
//   - an edit *inside* a published subpatcher is an ordinary edit and takes
//     effect, which is what "the same GraphState swap as a top-level edit"
//     means from outside;
//   - deleting a subpatcher takes its contents with it, transitively, and every
//     object in that subtree is torn down while its cords are still there
//     (issue #758) — the case that would fail if the subtree were unwired as it
//     was walked;
//   - a `.loadbang` inside a subpatcher fires on load, in the same pass as the
//     top level, because the whole tree is published in one swap (issue #547);
//   - and a containment cycle is refused, because the subtree walk would
//     otherwise never terminate.
//
// No audio device required.

#include <doctest/doctest.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "patcher/genericObjects/gInlet.h"
#include "patcher/genericObjects/gOutlet.h"
#include "patcher/inlet.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObject.h"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "support/alloc_probe.hpp"

using YSE::PATCHER::gInlet;
using YSE::PATCHER::gOutlet;
using YSE::PATCHER::patcherImplementation;
using YSE::PATCHER::Register;

namespace {

  // Records what arrived and in what shape. The shape is half of what a
  // boundary promises: a subpatcher that turned an int into a list on the way
  // through would be a different object to everything downstream of it, and a
  // rig that normalised the two could not tell them apart. Floats are written
  // at a fixed precision rather than through std::to_string, which the rest of
  // this suite avoids for the same reason.
  //
  // Deliberately not owned by the patcher — it stands in for whatever sits
  // outside the patch. Declared before the patcher in every case, so the
  // patcher dies first and the teardown pass still has somewhere to send
  // (sinks.hpp's rule).
  struct Tap : YSE::PATCHER::pObject {
    std::vector<std::string> log;

    Tap() : pObject(false) {
      log.reserve(64);
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { log.push_back("bang"); });
      inputs.back().RegisterInt(
          [this](int v, int, YSE::THREAD) { log.push_back("i" + std::to_string(v)); });
      inputs.back().RegisterFloat([this](float v, int, YSE::THREAD) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "f%.2f", static_cast<double>(v));
        log.push_back(buf);
      });
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { log.push_back("l" + v); });

      // A second, cold inlet so a two-outlet object on the far side of a
      // boundary can be watched without the two streams merging.
      inputs.emplace_back(this, false, 1);
      inputs.back().RegisterInt(
          [this](int v, int, YSE::THREAD) { log.push_back("v" + std::to_string(v)); });
    }
    const char* Type() const override {
      return "subpatcher_tap";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    std::string trace() const {
      std::string out;
      for (const std::string& entry : log) {
        if (!out.empty()) out.push_back(' ');
        out += entry;
      }
      return out;
    }
  };

  // Counts without describing, for the allocation case: `Tap` builds a
  // std::string per message, which is the *rig* allocating on the path being
  // measured, and the object under test would be convicted of its observer's
  // behaviour.
  struct CountTap : YSE::PATCHER::pObject {
    int hits = 0;

    CountTap() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { hits++; });
      inputs.back().RegisterInt([this](int, int, YSE::THREAD) { hits++; });
      inputs.back().RegisterFloat([this](float, int, YSE::THREAD) { hits++; });
      inputs.back().RegisterList([this](const std::string&, int, YSE::THREAD) { hits++; });
    }
    const char* Type() const override {
      return "subpatcher_count_tap";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // The patch most encapsulation cases below are built from, in one place so
  // that the cases which compare two patchers are provably comparing the same
  // patch.
  //
  //   0: `patcher`   — the subpatcher
  //   1: `.inlet 0`  — inside it
  //   2: `.+ 10`     — inside it, the thing being encapsulated
  //   3: `.outlet 0` — inside it
  //
  // The parent connects to (0, inlet 0) and from (0, outlet 0) and never names
  // 1, 2 or 3 — which is the whole point: to the parent the group is one
  // object. Storage IDs are 0..3 in creation order and survive a round trip
  // (issue #730). `.+` emits a float, so the far end reads "f17.00".
  YSE::pHandle* BuildAdderSubpatch(patcherImplementation& p, const char* offset = "10") {
    YSE::pHandle* sub = p.CreateObject(YSE::OBJ::PATCHER, "");
    YSE::pHandle* in = p.CreateObject(YSE::OBJ::G_INLET, "0");
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::G_ADD, offset);
    YSE::pHandle* out = p.CreateObject(YSE::OBJ::G_OUTLET, "0");
    REQUIRE(sub != nullptr);
    REQUIRE(in != nullptr);
    REQUIRE(add != nullptr);
    REQUIRE(out != nullptr);

    p.SetObjectContainer(in, sub);
    p.SetObjectContainer(add, sub);
    p.SetObjectContainer(out, sub);

    p.Connect(in, 0, add, 0);
    p.Connect(add, 0, out, 0);
    return sub;
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── the objects exist and have Max's shape ───────────────────────────────

  TEST_CASE("subpatcher: the three objects are registered (#545)") {
    auto names = Register().AllNames();
    bool hasPatcher = false;
    bool hasInlet = false;
    bool hasOutlet = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::PATCHER)) hasPatcher = true;
      if (name == std::string(YSE::OBJ::G_INLET)) hasInlet = true;
      if (name == std::string(YSE::OBJ::G_OUTLET)) hasOutlet = true;
    }
    CHECK(hasPatcher);
    CHECK(hasInlet);
    CHECK(hasOutlet);

    // The façade owns no pins, and that is a correctness requirement rather
    // than an omission: a subpatcher's boundary changes whenever a `.inlet` is
    // added to it, and a real pin vector would have to be resized on an object
    // the render is concurrently walking. `SubpatcherInlets` reports the shape
    // instead — see the case further down.
    std::unique_ptr<YSE::PATCHER::pObject> facade(Register().Get(YSE::OBJ::PATCHER));
    REQUIRE(facade != nullptr);
    CHECK(std::string(facade->Type()) == "patcher");
    CHECK(facade->NumInputs() == 0);
    CHECK(facade->NumOutputs() == 0);

    // One inlet and one outlet each. Max's `inlet` appears to have no inlet
    // because a patcher window draws the boundary for you; headless, the
    // parent's cord has to land on something real.
    std::unique_ptr<YSE::PATCHER::pObject> in(Register().Get(YSE::OBJ::G_INLET));
    REQUIRE(in != nullptr);
    CHECK(std::string(in->Type()) == ".inlet");
    CHECK(in->NumInputs() == 1);
    CHECK(in->NumOutputs() == 1);

    std::unique_ptr<YSE::PATCHER::pObject> out(Register().Get(YSE::OBJ::G_OUTLET));
    REQUIRE(out != nullptr);
    CHECK(std::string(out->Type()) == ".outlet");
    CHECK(out->NumInputs() == 1);
    CHECK(out->NumOutputs() == 1);
  }

  // ─── the boundary objects, on their own ───────────────────────────────────

  TEST_CASE("subpatcher: .inlet and .outlet pass every kind through unchanged (#545)") {
    // Kind preservation is the claim. A boundary that turned everything into a
    // list would still "work" for a patch that only sends numbers, and would be
    // a different object to every `.sel`, `.route` and `.match` downstream.
    Tap tap;
    gInlet in;
    in.GetOutlet(0)->Connect(tap.GetInlet(0));
    tap.GetInlet(0)->Connect(in.GetOutlet(0));

    in.GetInlet(0)->SetBang(YSE::T_GUI);
    in.GetInlet(0)->SetInt(7, YSE::T_GUI);
    in.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    in.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);
    CHECK(tap.trace() == "bang i7 f0.50 l1 2 3");

    Tap tap2;
    gOutlet out;
    out.GetOutlet(0)->Connect(tap2.GetInlet(0));
    tap2.GetInlet(0)->Connect(out.GetOutlet(0));

    out.GetInlet(0)->SetBang(YSE::T_GUI);
    out.GetInlet(0)->SetInt(-3, YSE::T_GUI);
    out.GetInlet(0)->SetList("a b", YSE::T_GUI);
    CHECK(tap2.trace() == "bang i-3 la b");
  }

  TEST_CASE("subpatcher: the boundary index is a creation argument (#545)") {
    gInlet in;
    CHECK(in.Index() == 0); // Max's default, and what an unargumented one is
    in.SetParams("3");
    CHECK(in.Index() == 3);
    CHECK(in.GetParams() == "3");

    gOutlet out;
    out.SetParams("2");
    CHECK(out.Index() == 2);
  }

  TEST_CASE("subpatcher: crossing a boundary allocates nothing (#545)") {
    // A boundary object's handler runs on whichever thread dispatched the
    // message, and in-patcher delivery dispatches on T_DSP, so every crossing
    // is audio-thread code. Each handler is one outlet::Send*, which is the
    // whole reason a boundary is affordable at all.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    CountTap tap;
    gInlet in;
    in.GetOutlet(0)->Connect(tap.GetInlet(0));
    tap.GetInlet(0)->Connect(in.GetOutlet(0));

    // Built outside the probe: it is the *handler* that must not allocate, not
    // the test's own construction of the message.
    const std::string list = "1 2 3 4 5 6 7 8";

    // Read out and asserted outside the armed region — doctest's own assertion
    // machinery allocates on first use and would otherwise be counted against
    // the code under test.
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      in.GetInlet(0)->SetBang(YSE::T_DSP);
      in.GetInlet(0)->SetInt(42, YSE::T_DSP);
      in.GetInlet(0)->SetFloat(1.5f, YSE::T_DSP);
      in.GetInlet(0)->SetList(list, YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);
    // The probed sends really did something: an assertion that only proves
    // nothing happened proves nothing.
    CHECK(tap.hits == 4);
  }

  // ─── encapsulation: the parent addresses the group as one object ──────────

  TEST_CASE("subpatcher: a value sent to the façade comes back through the group (#545)") {
    // The headline. The parent names the subpatcher and its pin numbers and
    // never names anything inside it; the 17 could only have been produced by
    // the `.+ 10` behind the boundary.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* sub = BuildAdderSubpatch(p);
    p.Connect(sub, 0, &tapHandle, 0);

    sub->SetIntData(0, 7);
    CHECK(tap.trace() == "f17.00");

    // The control: the same patcher with the boundary cut. If the value below
    // still arrived, the case above would be measuring something other than the
    // boundary.
    tap.log.clear();
    p.Disconnect(sub, 0, &tapHandle, 0);
    sub->SetIntData(0, 7);
    CHECK(tap.trace().empty());
  }

  TEST_CASE("subpatcher: an ordinary cord into the façade crosses it too (#545)") {
    // The same claim from the other direction: not a host call on the handle
    // but a real patch cord from a real object in the parent, which is the path
    // Connect's façade resolution exists for.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* sub = BuildAdderSubpatch(p);
    YSE::pHandle* source = p.CreateObject(YSE::OBJ::G_INT, "");
    REQUIRE(source != nullptr);
    p.Connect(source, 0, sub, 0);
    p.Connect(sub, 0, &tapHandle, 0);

    source->SetIntData(0, 5);
    CHECK(tap.trace() == "f15.00");

    // The edge really was recorded against the boundary object rather than the
    // façade: the source's one connection names `.inlet`'s storage ID (1), not
    // the subpatcher's (0). That is what makes the compiled graph free of
    // subpatchers and the audio thread free of nesting.
    REQUIRE(source->GetConnections(0) == 1);
    CHECK(source->GetConnectionTarget(0, 0) == 1);
    CHECK(source->GetConnectionTargetInlet(0, 0) == 0);
  }

  TEST_CASE("subpatcher: pin numbers address the boundary objects by index (#545)") {
    // Two in, two out, deliberately crossed inside: inlet 0 reaches outlet 1
    // and inlet 1 reaches outlet 0. A boundary that ignored the index could
    // still pass a symmetric wiring by picking the right object out of two;
    // crossing the inside is what removes that.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* sub = p.CreateObject(YSE::OBJ::PATCHER, "");
    YSE::pHandle* in0 = p.CreateObject(YSE::OBJ::G_INLET, "0");
    YSE::pHandle* in1 = p.CreateObject(YSE::OBJ::G_INLET, "1");
    YSE::pHandle* out0 = p.CreateObject(YSE::OBJ::G_OUTLET, "0");
    YSE::pHandle* out1 = p.CreateObject(YSE::OBJ::G_OUTLET, "1");
    REQUIRE(sub != nullptr);
    for (YSE::pHandle* h : {in0, in1, out0, out1}) {
      REQUIRE(h != nullptr);
      p.SetObjectContainer(h, sub);
    }
    p.Connect(in0, 0, out1, 0);
    p.Connect(in1, 0, out0, 0);

    p.Connect(sub, 0, &tapHandle, 0); // subpatcher outlet 0 -> hot inlet
    p.Connect(sub, 1, &tapHandle, 1); // subpatcher outlet 1 -> cold inlet

    sub->SetIntData(0, 5); // in at 0, out at 1, arrives on the cold inlet
    CHECK(tap.trace() == "v5");

    tap.log.clear();
    sub->SetIntData(1, 6); // in at 1, out at 0, arrives on the hot inlet
    CHECK(tap.trace() == "i6");
  }

  TEST_CASE("subpatcher: the boundary shape is reported by the patcher (#545)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* sub = p.CreateObject(YSE::OBJ::PATCHER, "");
    REQUIRE(sub != nullptr);
    CHECK(p.SubpatcherInlets(sub) == 0);
    CHECK(p.SubpatcherOutlets(sub) == 0);

    YSE::pHandle* in = p.CreateObject(YSE::OBJ::G_INLET, "0");
    REQUIRE(in != nullptr);
    // A boundary object that has not been placed inside the subpatcher belongs
    // to no boundary — containment is what makes it one.
    CHECK(p.SubpatcherInlets(sub) == 0);
    p.SetObjectContainer(in, sub);
    CHECK(p.SubpatcherInlets(sub) == 1);

    // Sparse numbering reports the range a parent can *address*, not the count
    // of boundary objects: index 2 with nothing at 1 means three inlets, one of
    // which reaches nothing.
    YSE::pHandle* far = p.CreateObject(YSE::OBJ::G_INLET, "2");
    REQUIRE(far != nullptr);
    p.SetObjectContainer(far, sub);
    CHECK(p.SubpatcherInlets(sub) == 3);

    YSE::pHandle* out = p.CreateObject(YSE::OBJ::G_OUTLET, "0");
    REQUIRE(out != nullptr);
    p.SetObjectContainer(out, sub);
    CHECK(p.SubpatcherOutlets(sub) == 1);

    // Not a subpatcher: 0, not a guess.
    CHECK(p.SubpatcherInlets(in) == 0);
    CHECK(p.SubpatcherInlets(nullptr) == 0);
  }

  TEST_CASE("subpatcher: nesting a subpatcher inside a subpatcher works (#545)") {
    // Depth is the property the flat-storage model exists to make free. Two
    // boundaries, each adding 10, so the 21 at the end could only have crossed
    // both of them.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);

    // The inner group: `.inlet 0` -> `.+ 10` -> `.outlet 0`.
    YSE::pHandle* inner = p.CreateObject(YSE::OBJ::PATCHER, "");
    YSE::pHandle* innerIn = p.CreateObject(YSE::OBJ::G_INLET, "0");
    YSE::pHandle* innerAdd = p.CreateObject(YSE::OBJ::G_ADD, "10");
    YSE::pHandle* innerOut = p.CreateObject(YSE::OBJ::G_OUTLET, "0");
    REQUIRE(inner != nullptr);
    for (YSE::pHandle* h : {innerIn, innerAdd, innerOut}) {
      REQUIRE(h != nullptr);
      p.SetObjectContainer(h, inner);
    }
    p.Connect(innerIn, 0, innerAdd, 0);
    p.Connect(innerAdd, 0, innerOut, 0);

    // The outer group, which contains the inner one and another `.+ 10`.
    YSE::pHandle* outer = p.CreateObject(YSE::OBJ::PATCHER, "");
    YSE::pHandle* outerIn = p.CreateObject(YSE::OBJ::G_INLET, "0");
    YSE::pHandle* outerAdd = p.CreateObject(YSE::OBJ::G_ADD, "10");
    YSE::pHandle* outerOut = p.CreateObject(YSE::OBJ::G_OUTLET, "0");
    REQUIRE(outer != nullptr);
    for (YSE::pHandle* h : {outerIn, outerAdd, outerOut, inner}) {
      REQUIRE(h != nullptr);
      p.SetObjectContainer(h, outer);
    }
    // Inside the outer group, the inner subpatcher is addressed exactly as the
    // parent addresses the outer one: by pin number.
    p.Connect(outerIn, 0, inner, 0);
    p.Connect(inner, 0, outerAdd, 0);
    p.Connect(outerAdd, 0, outerOut, 0);

    p.Connect(outer, 0, &tapHandle, 0);
    outer->SetIntData(0, 1);
    CHECK(tap.trace() == "f21.00");

    // The inner group's boundary belongs to the inner group, not to the outer
    // one — Container() being one level deep is what says so.
    CHECK(p.SubpatcherInlets(outer) == 1);
    CHECK(p.SubpatcherInlets(inner) == 1);
  }

  // ─── persistence ──────────────────────────────────────────────────────────

  TEST_CASE("subpatcher: a nested graph round-trips through JSON (#545)") {
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* sub = BuildAdderSubpatch(src);
      REQUIRE(sub != nullptr);
      dump = src.DumpJSON();
    }
    // Containment is in the file at all — the assertion that tells "persisted"
    // from "rebuilt by the test".
    CHECK(dump.find("container") != std::string::npos);

    patcherImplementation loaded(1, nullptr);
    loaded.ParseJSON(dump);
    REQUIRE(loaded.Objects() == 4);

    // Load is a fixed point of save: dump, parse, dump again and the bytes
    // match. This is what catches a containment that survived as a pointer but
    // not as a number, or an id that was renumbered on the way in.
    CHECK(loaded.DumpJSON() == dump);

    // And the reloaded patch is the same patch, addressed the same way. The
    // subpatcher is storage ID 0 (it was created first), and the parent still
    // never names anything inside it.
    YSE::pHandle* sub = loaded.GetHandleFromID(0);
    REQUIRE(sub != nullptr);
    CHECK(std::string(sub->Type()) == "patcher");
    CHECK(loaded.SubpatcherInlets(sub) == 1);
    CHECK(loaded.SubpatcherOutlets(sub) == 1);

    loaded.Connect(sub, 0, &tapHandle, 0);
    sub->SetIntData(0, 7);
    CHECK(tap.trace() == "f17.00");
  }

  TEST_CASE("subpatcher: a patch with no subpatchers serialises as it always did (#545)") {
    // The container key is written only when there is one, so adding
    // subpatchers cannot change the bytes of every patch that has none.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* a = p.CreateObject(YSE::OBJ::G_INT, "3");
    YSE::pHandle* b = p.CreateObject(YSE::OBJ::G_INT, "");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    p.Connect(a, 0, b, 0);
    CHECK(p.DumpJSON().find("container") == std::string::npos);
  }

  TEST_CASE("subpatcher: a .loadbang inside one fires on load (#545, #547)") {
    // The firing-order decision, stated as a test. There is deliberately no
    // "inner patchers initialise first" rule: the whole tree is compiled and
    // installed by one atomic swap, so "loading has finished" becomes true for
    // every level at the same instant and the contents fire in the same single
    // pass as the top level. What has to hold is that a nested object fires at
    // all, and that its bang travels the cords the file describes.
    //
    //   0: `patcher`
    //   1: `.loadbang`  — inside it
    //   2: `.i 7`       — inside it; only emits when banged
    //   3: `.i`         — inside it, the far end
    //
    // The 7 read at the end is a number no object in the patch holds until a
    // bang has crossed two cords, which is what makes this an assertion about
    // the firing *point* rather than about the object.
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* sub = src.CreateObject(YSE::OBJ::PATCHER, "");
      YSE::pHandle* load = src.CreateObject(YSE::OBJ::G_LOADBANG, "");
      YSE::pHandle* seven = src.CreateObject(YSE::OBJ::G_INT, "7");
      YSE::pHandle* sink = src.CreateObject(YSE::OBJ::G_INT, "");
      REQUIRE(sub != nullptr);
      for (YSE::pHandle* h : {load, seven, sink}) {
        REQUIRE(h != nullptr);
        src.SetObjectContainer(h, sub);
      }
      src.Connect(load, 0, seven, 0);
      src.Connect(seven, 0, sink, 0);

      // The control, and the second half of issue #547's decision: built live
      // through CreateObject, nothing has fired. A case that only checked the
      // loaded patcher could not tell "fires at the right moment" from "fires
      // always".
      CHECK(sink->GetGuiValue() == "0");
      dump = src.DumpJSON();
    }

    patcherImplementation loaded(1, nullptr);
    loaded.ParseJSON(dump);
    YSE::pHandle* sink = loaded.GetHandleFromID(3);
    REQUIRE(sink != nullptr);
    CHECK(sink->GetGuiValue() == "7");
    // ...and it really is nested, so the pass reached inside a subpatcher.
    CHECK(loaded.GetObjectContainer(sink) == loaded.GetHandleFromID(0));
  }

  // ─── live editing ─────────────────────────────────────────────────────────

  TEST_CASE("subpatcher: editing inside a published subpatcher takes effect (#545)") {
    // "A live edit inside a subpatcher goes through the same GraphState swap as
    // a top-level edit" is an architecture claim; from outside it means the
    // edit takes effect and the patch keeps working. Both halves are asserted:
    // the graph before the edit and the graph after it.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* sub = BuildAdderSubpatch(p, "10");
    p.Connect(sub, 0, &tapHandle, 0);

    sub->SetIntData(0, 1);
    REQUIRE(tap.trace() == "f11.00");
    tap.log.clear();

    // Replace the encapsulated object with a different one, live, and rewire —
    // an ordinary create / delete / connect sequence that happens to be inside
    // a subpatcher.
    YSE::pHandle* oldAdd = p.GetHandleFromID(2);
    REQUIRE(oldAdd != nullptr);
    REQUIRE(std::string(oldAdd->Type()) == std::string(YSE::OBJ::G_ADD));
    p.DeleteObject(oldAdd);

    YSE::pHandle* in = p.GetHandleFromID(1);
    YSE::pHandle* out = p.GetHandleFromID(3);
    REQUIRE(in != nullptr);
    REQUIRE(out != nullptr);
    YSE::pHandle* newAdd = p.CreateObject(YSE::OBJ::G_ADD, "100");
    REQUIRE(newAdd != nullptr);
    p.SetObjectContainer(newAdd, sub);
    p.Connect(in, 0, newAdd, 0);
    p.Connect(newAdd, 0, out, 0);

    sub->SetIntData(0, 1);
    CHECK(tap.trace() == "f101.00");
    // The boundary is untouched by the edit: the parent's cord still lands on
    // the same `.outlet`, which is why it did not have to be rewired.
    CHECK(p.SubpatcherOutlets(sub) == 1);
  }

  // ─── lifetime ─────────────────────────────────────────────────────────────

  TEST_CASE("subpatcher: deleting one takes its contents with it (#545)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* sub = BuildAdderSubpatch(p);
    YSE::pHandle* outside = p.CreateObject(YSE::OBJ::G_INT, "");
    REQUIRE(outside != nullptr);
    REQUIRE(p.Objects() == 5);

    p.DeleteObject(sub);
    // The façade and its three contents are gone; the object that was never
    // inside it is not.
    CHECK(p.Objects() == 1);
    CHECK(p.GetHandleFromList(0) == outside);
  }

  TEST_CASE("subpatcher: deleting one takes a nested one with it too (#545)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* outer = p.CreateObject(YSE::OBJ::PATCHER, "");
    YSE::pHandle* inner = p.CreateObject(YSE::OBJ::PATCHER, "");
    YSE::pHandle* deep = p.CreateObject(YSE::OBJ::G_INT, "");
    REQUIRE(outer != nullptr);
    REQUIRE(inner != nullptr);
    REQUIRE(deep != nullptr);
    p.SetObjectContainer(inner, outer);
    p.SetObjectContainer(deep, inner);
    REQUIRE(p.Objects() == 3);

    p.DeleteObject(outer);
    CHECK(p.Objects() == 0);
  }

  TEST_CASE("subpatcher: its contents are torn down while their cords exist (#545, #758)") {
    // The ordering case, and the one that would fail if the subtree were
    // unwired as it was walked. A `.makenote` inside the subpatcher is holding
    // a note down; deleting the subpatcher has to let it release that note
    // *through* the boundary, which only exists until the unwiring starts.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* sub = p.CreateObject(YSE::OBJ::PATCHER, "");
    YSE::pHandle* mk = p.CreateObject(YSE::OBJ::M_MAKENOTE, "100 60000");
    YSE::pHandle* out0 = p.CreateObject(YSE::OBJ::G_OUTLET, "0");
    YSE::pHandle* out1 = p.CreateObject(YSE::OBJ::G_OUTLET, "1");
    REQUIRE(sub != nullptr);
    for (YSE::pHandle* h : {mk, out0, out1}) {
      REQUIRE(h != nullptr);
      p.SetObjectContainer(h, sub);
    }
    p.Connect(mk, 1, out1, 0); // velocity
    p.Connect(mk, 0, out0, 0); // pitch
    p.Connect(sub, 1, &tapHandle, 1);
    p.Connect(sub, 0, &tapHandle, 0);

    // The control: the rig can see a note in the first place, and it came out
    // through the boundary.
    mk->SetIntData(0, 60);
    REQUIRE(tap.trace() == "v100 i60");
    tap.log.clear();

    p.DeleteObject(sub);
    // The release, with velocity 0 — sent by the teardown pass, down cords that
    // were still there because the pass runs before any of them is cut.
    CHECK(tap.trace() == "v0 i60");
  }

  // ─── refusals ─────────────────────────────────────────────────────────────

  TEST_CASE("subpatcher: containment refuses what would make the graph cyclic (#545)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* a = p.CreateObject(YSE::OBJ::PATCHER, "");
    YSE::pHandle* b = p.CreateObject(YSE::OBJ::PATCHER, "");
    YSE::pHandle* plain = p.CreateObject(YSE::OBJ::G_INT, "");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(plain != nullptr);

    // A subpatcher inside itself.
    p.SetObjectContainer(a, a);
    CHECK(p.GetObjectContainer(a) == nullptr);

    // A subpatcher inside its own descendant. Without the refusal the
    // containment walk DeleteObject does would never terminate.
    p.SetObjectContainer(b, a);
    CHECK(p.GetObjectContainer(b) == a);
    p.SetObjectContainer(a, b);
    CHECK(p.GetObjectContainer(a) == nullptr);

    // A container that is not a `patcher` object.
    p.SetObjectContainer(b, plain);
    CHECK(p.GetObjectContainer(b) == a);

    // And back out to the top level, which is what a null container means.
    p.SetObjectContainer(b, nullptr);
    CHECK(p.GetObjectContainer(b) == nullptr);
  }

  TEST_CASE("subpatcher: connecting to a pin the boundary does not have is refused (#545)") {
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* sub = BuildAdderSubpatch(p);
    YSE::pHandle* source = p.CreateObject(YSE::OBJ::G_INT, "");
    REQUIRE(source != nullptr);

    // Outlet 3 of a subpatcher with one `.outlet` names nothing. The refusal
    // has to be silent-and-safe rather than a crash or a wrong edge: a wrong
    // edge is the failure mode that would only show up later, as a value
    // arriving somewhere nobody wired it to.
    p.Connect(sub, 3, &tapHandle, 0);
    sub->SetIntData(0, 1);
    CHECK(tap.trace().empty());

    // Inlet 2 likewise, and the source must have recorded no edge for it.
    p.Connect(source, 0, sub, 2);
    CHECK(source->GetConnections(0) == 0);

    // A host push at a pin that does not exist is the same refusal rather than
    // a null dereference.
    sub->SetIntData(9, 1);
    CHECK(tap.trace().empty());

    // The pins that do exist still work, so the refusal was specific rather
    // than a boundary that stopped resolving anything.
    p.Connect(sub, 0, &tapHandle, 0);
    sub->SetIntData(0, 1);
    CHECK(tap.trace() == "f11.00");
  }

  // ─── the public wrapper ───────────────────────────────────────────────────

  TEST_CASE("subpatcher: the same thing through the public patcher API (#545)") {
    // `YSE::patcher` is the surface a host meets, and it is a separate object
    // from `patcherImplementation` with its own null handling — a feature that
    // worked on the impl and was never forwarded would be invisible to every
    // caller that has one.
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* sub = p.CreateObject(YSE::OBJ::PATCHER, "");
    YSE::pHandle* in = p.CreateObject(YSE::OBJ::G_INLET, "0");
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::G_ADD, "10");
    YSE::pHandle* out = p.CreateObject(YSE::OBJ::G_OUTLET, "0");
    YSE::pHandle* sink = p.CreateObject(YSE::OBJ::G_INT, "");
    REQUIRE(sub != nullptr);
    REQUIRE(sink != nullptr);
    for (YSE::pHandle* h : {in, add, out}) {
      REQUIRE(h != nullptr);
      p.SetContainer(h, sub);
    }
    p.Connect(in, 0, add, 0);
    p.Connect(add, 0, out, 0);
    p.Connect(sub, 0, sink, 0);

    CHECK(p.GetContainer(add) == sub);
    CHECK(p.GetContainer(sink) == nullptr);
    CHECK(p.SubpatcherInlets(sub) == 1);
    CHECK(p.SubpatcherOutlets(sub) == 1);

    sub->SetIntData(0, 5);
    CHECK(sink->GetGuiValue() == "15");

    // A `patcher` object owns no pins of its own, which is why the two queries
    // above exist at all.
    CHECK(sub->GetInputs() == 0);
    CHECK(sub->GetOutputs() == 0);
  }

} // TEST_SUITE("patcher")

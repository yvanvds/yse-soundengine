// Tests for YSE patcher graph topology (YseEngine/patcher/).
// Covers: pRegistry object lookup, patcher lifecycle, object creation/deletion,
// outlet→inlet connections, and handle-by-index/ID retrieval.
// No audio device required.

#include <doctest/doctest.h>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include <string>
#include <vector>

TEST_SUITE("patcher") {

  // ─── pRegistry ────────────────────────────────────────────────────────────────

  TEST_CASE("pRegistry: IsValidObject returns true for registered types") {
    CHECK(YSE::patcher::IsValidObject(YSE::OBJ::D_SINE));
    CHECK(YSE::patcher::IsValidObject(YSE::OBJ::D_SAW));
    CHECK(YSE::patcher::IsValidObject(YSE::OBJ::G_MULTIPLY));
    CHECK(YSE::patcher::IsValidObject(YSE::OBJ::D_ADD));
    CHECK(YSE::patcher::IsValidObject(YSE::OBJ::D_LOWPASS));
  }

  TEST_CASE("pRegistry: IsValidObject returns false for unknown type") {
    CHECK_FALSE(YSE::patcher::IsValidObject("not_a_real_object"));
  }

  // ─── patcher lifecycle ────────────────────────────────────────────────────────

  TEST_CASE("patcher: create initializes an empty graph") {
    YSE::patcher p;
    p.create(2);
    CHECK(p.Objects() == 0u);
  }

  TEST_CASE("patcher: CreateObject returns non-null handle for valid type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_SINE);
    REQUIRE(h != nullptr);
    CHECK(p.Objects() == 1u);
  }

  TEST_CASE("patcher: CreateObject returns null for unknown type") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject("not_a_real_object");
    CHECK(h == nullptr);
    CHECK(p.Objects() == 0u);
  }

  TEST_CASE("patcher: DeleteObject reduces object count") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_MULTIPLY);
    REQUIRE(h != nullptr);
    CHECK(p.Objects() == 1u);
    p.DeleteObject(h);
    CHECK(p.Objects() == 0u);
  }

  TEST_CASE("patcher: Clear removes all objects") {
    YSE::patcher p;
    p.create(2);
    p.CreateObject(YSE::OBJ::D_SINE);
    p.CreateObject(YSE::OBJ::G_MULTIPLY);
    CHECK(p.Objects() == 2u);
    p.Clear();
    CHECK(p.Objects() == 0u);
  }

  // ─── Connections ──────────────────────────────────────────────────────────────

  TEST_CASE("patcher: Connect increments outlet connection count") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* sine = p.CreateObject(YSE::OBJ::D_SINE);
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::D_ADD);
    REQUIRE(sine != nullptr);
    REQUIRE(add != nullptr);

    CHECK(sine->GetConnections(0) == 0u);
    p.Connect(sine, 0, add, 0);
    CHECK(sine->GetConnections(0) == 1u);
  }

  TEST_CASE("patcher: Disconnect decrements outlet connection count") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* sine = p.CreateObject(YSE::OBJ::D_SINE);
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::D_ADD);
    REQUIRE(sine != nullptr);
    REQUIRE(add != nullptr);

    p.Connect(sine, 0, add, 0);
    CHECK(sine->GetConnections(0) == 1u);
    p.Disconnect(sine, 0, add, 0);
    CHECK(sine->GetConnections(0) == 0u);
  }

  // Regression for issue #235: Disconnect must guard a missing outlet/inlet the
  // same way Connect does. A ~dac has no outlets, so from->GetOutlet(0) returns
  // null; before the fix that null flowed into inlet::Disconnect and segfaulted
  // (a disconnected inlet has dspConnection == nullptr == out, so it derefed the
  // null outlet). These calls must be safe no-ops now, not crashes.
  TEST_CASE("patcher: Disconnect on a source with no outlet is a safe no-op (issue #235)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::D_ADD);
    REQUIRE(dac != nullptr);
    REQUIRE(add != nullptr);

    // ~dac has no outlets — outlet 0 does not exist. Must not crash.
    p.Disconnect(dac, 0, add, 0);
    CHECK(p.Objects() == 2u);
  }

  TEST_CASE("patcher: Disconnect with out-of-range outlet index is a safe no-op (issue #235)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* sine = p.CreateObject(YSE::OBJ::D_SINE);
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::D_ADD);
    REQUIRE(sine != nullptr);
    REQUIRE(add != nullptr);

    p.Connect(sine, 0, add, 0);
    // Outlet 99 does not exist on a sine — GetOutlet returns null.
    p.Disconnect(sine, 99, add, 0);
    // The real edge is untouched and nothing crashed.
    CHECK(sine->GetConnections(0) == 1u);
  }

  TEST_CASE("patcher: Disconnect with out-of-range inlet index is a safe no-op (issue #235)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* sine = p.CreateObject(YSE::OBJ::D_SINE);
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::D_ADD);
    REQUIRE(sine != nullptr);
    REQUIRE(add != nullptr);

    p.Connect(sine, 0, add, 0);
    // Inlet 99 does not exist on an add — GetInlet returns null, and the
    // former inputs[99] out-of-bounds index is avoided.
    p.Disconnect(sine, 0, add, 99);
    CHECK(sine->GetConnections(0) == 1u);
  }

  TEST_CASE("patcher: connection target reports correct object ID and inlet index") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* sine = p.CreateObject(YSE::OBJ::D_SINE);
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::D_ADD);
    REQUIRE(sine != nullptr);
    REQUIRE(add != nullptr);

    p.Connect(sine, 0, add, 0);
    CHECK(sine->GetConnectionTarget(0, 0) == add->GetID());
    CHECK(sine->GetConnectionTargetInlet(0, 0) == 0u);
  }

  // ─── Handle lookup ────────────────────────────────────────────────────────────

  TEST_CASE("patcher: GetHandleFromList returns a valid handle") {
    YSE::patcher p;
    p.create(2);
    p.CreateObject(YSE::OBJ::D_SINE);
    REQUIRE(p.Objects() == 1u);
    YSE::pHandle* found = p.GetHandleFromList(0);
    CHECK(found != nullptr);
  }

  TEST_CASE("patcher: GetHandleFromID retrieves the correct handle") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_SINE);
    REQUIRE(h != nullptr);
    unsigned int id = h->GetID();
    YSE::pHandle* found = p.GetHandleFromID(id);
    CHECK(found == h);
  }

  // ─── JSON round-trip ─────────────────────────────────────────────────────────

  TEST_CASE("patcher: DumpJSON on empty patcher returns non-empty JSON") {
    YSE::patcher p;
    p.create(2);
    std::string j = p.DumpJSON();
    CHECK(!j.empty());
  }

  TEST_CASE("patcher: DumpJSON serialises every created object") {
    YSE::patcher p;
    p.create(2);
    p.CreateObject(YSE::OBJ::D_SINE);
    p.CreateObject(YSE::OBJ::G_MULTIPLY);
    std::string j = p.DumpJSON();
    CHECK(j.find("object 0") != std::string::npos);
    CHECK(j.find("object 1") != std::string::npos);
  }

  TEST_CASE("patcher: ParseJSON on the output of DumpJSON reproduces object count") {
    YSE::patcher source;
    source.create(2);
    source.CreateObject(YSE::OBJ::D_SINE);
    source.CreateObject(YSE::OBJ::G_MULTIPLY);
    std::string dump = source.DumpJSON();

    YSE::patcher target;
    target.create(2);
    target.ParseJSON(dump);
    CHECK(target.Objects() == 2u);
  }

  // ─── Storage IDs (issue #730) ────────────────────────────────────────────────

  TEST_CASE("patcher: storage IDs are numbered per patcher, from 0 (#730)") {
    // The counter behind these used to run for the whole process, so an
    // object's ID recorded how many patcher objects had already been built —
    // every test case ahead of this one included. Nothing about the patch.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* sine = p.CreateObject(YSE::OBJ::D_SINE, "440");
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::D_ADD);
    REQUIRE(sine != nullptr);
    REQUIRE(add != nullptr);
    CHECK(sine->GetID() == 0u);
    CHECK(add->GetID() == 1u);

    // A second patcher in the same process numbers from 0 too: what the first
    // one built is none of its business.
    YSE::patcher q;
    q.create(2);
    YSE::pHandle* other = q.CreateObject(YSE::OBJ::D_SINE, "440");
    REQUIRE(other != nullptr);
    CHECK(other->GetID() == 0u);
  }

  TEST_CASE("patcher: two identically-built patchers serialise identically (#730)") {
    // The user-visible half of #730: save the same patch twice from one process
    // and the files should be the same file. Before the fix the two dumps
    // differed on every object's "ID" and on every connection target under
    // "outputs", so two saves that should have been byte-identical diffed on
    // every line.
    auto build = [](YSE::patcher& p) {
      p.create(2);
      YSE::pHandle* sine = p.CreateObject(YSE::OBJ::D_SINE, "440");
      YSE::pHandle* add = p.CreateObject(YSE::OBJ::D_ADD);
      YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC);
      REQUIRE(sine != nullptr);
      REQUIRE(add != nullptr);
      REQUIRE(dac != nullptr);
      p.Connect(sine, 0, add, 0);
      p.Connect(add, 0, dac, 0);
    };

    YSE::patcher first;
    build(first);
    YSE::patcher second;
    build(second);

    CHECK(first.DumpJSON() == second.DumpJSON());
  }

  TEST_CASE("patcher: dump -> parse -> dump is a fixed point past ten objects (#730)") {
    // A dump's records are keyed "object 0" .. "object N" and come back out of
    // the json object in *string* order, so "object 10" arrives before "object
    // 2". A load that followed that order rebuilt the patch in an order the
    // patch never had, and handed the fresh IDs to different objects than the
    // ones the file named. Twelve objects is the smallest count that shows it.
    YSE::patcher source;
    source.create(2);
    std::vector<YSE::pHandle*> made;
    for (int i = 0; i < 12; i++) {
      YSE::pHandle* h = source.CreateObject(YSE::OBJ::G_MULTIPLY, std::to_string(i));
      REQUIRE(h != nullptr);
      made.push_back(h);
    }
    // Two edges that span the ordering, so connection targets are exercised too.
    source.Connect(made[0], 0, made[11], 0);
    source.Connect(made[10], 0, made[1], 0);

    const std::string saved = source.DumpJSON();

    YSE::patcher target;
    target.create(2);
    target.ParseJSON(saved);
    REQUIRE(target.Objects() == 12u);
    CHECK(target.DumpJSON() == saved);
  }

  TEST_CASE("patcher: a patch saved with the old process-wide IDs still loads (#730)") {
    // Every patch saved before #730 carries whatever five- or six-digit numbers
    // the process-wide counter happened to be at. ParseJSON has always remapped
    // IDs through its OldIDs table rather than trusting them, so those patches
    // keep loading — they are simply renumbered from 0 the next time they are
    // saved. This is the compatibility guarantee, pinned.
    const std::string legacy = R"({
  "object 0": {
    "ID": 35777,
    "parms": "440",
    "type": "~sine",
    "outputs": { "output 0": { "0": { "Inlet": 0, "Object": 35798 }, "Count": 1 } }
  },
  "object 1": {
    "ID": 35798,
    "parms": "",
    "type": "~+",
    "outputs": { "output 0": { "Count": 0 } }
  }
})";

    YSE::patcher p;
    p.create(2);
    p.ParseJSON(legacy);
    REQUIRE(p.Objects() == 2u);

    // Renumbered densely, in the order the file's own IDs put them.
    YSE::pHandle* sine = p.GetHandleFromID(0);
    YSE::pHandle* add = p.GetHandleFromID(1);
    REQUIRE(sine != nullptr);
    REQUIRE(add != nullptr);
    CHECK(std::string(sine->Type()) == std::string("~sine"));
    CHECK(sine->GetParams() == std::string("440"));
    CHECK(std::string(add->Type()) == std::string("~+"));

    // And the edge the old IDs described survived the remap onto the new ones.
    REQUIRE(sine->GetConnections(0) == 1u);
    CHECK(sine->GetConnectionTarget(0, 0) == add->GetID());

    // Nothing of the old numbering is left in the re-save.
    const std::string resaved = p.DumpJSON();
    CHECK(resaved.find("35777") == std::string::npos);
    CHECK(resaved.find("35798") == std::string::npos);
  }

} // TEST_SUITE("patcher")

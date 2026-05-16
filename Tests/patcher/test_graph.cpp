// Tests for YSE patcher graph topology (YseEngine/patcher/).
// Covers: pRegistry object lookup, patcher lifecycle, object creation/deletion,
// outlet→inlet connections, and handle-by-index/ID retrieval.
// No audio device required.

#include <doctest/doctest.h>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"

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
    YSE::pHandle* add  = p.CreateObject(YSE::OBJ::D_ADD);
    REQUIRE(sine != nullptr);
    REQUIRE(add  != nullptr);

    CHECK(sine->GetConnections(0) == 0u);
    p.Connect(sine, 0, add, 0);
    CHECK(sine->GetConnections(0) == 1u);
}

TEST_CASE("patcher: Disconnect decrements outlet connection count") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* sine = p.CreateObject(YSE::OBJ::D_SINE);
    YSE::pHandle* add  = p.CreateObject(YSE::OBJ::D_ADD);
    REQUIRE(sine != nullptr);
    REQUIRE(add  != nullptr);

    p.Connect(sine, 0, add, 0);
    CHECK(sine->GetConnections(0) == 1u);
    p.Disconnect(sine, 0, add, 0);
    CHECK(sine->GetConnections(0) == 0u);
}

TEST_CASE("patcher: connection target reports correct object ID and inlet index") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* sine = p.CreateObject(YSE::OBJ::D_SINE);
    YSE::pHandle* add  = p.CreateObject(YSE::OBJ::D_ADD);
    REQUIRE(sine != nullptr);
    REQUIRE(add  != nullptr);

    p.Connect(sine, 0, add, 0);
    CHECK(sine->GetConnectionTarget(0, 0)      == add->GetID());
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

} // TEST_SUITE("patcher")

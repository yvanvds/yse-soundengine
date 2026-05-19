// Smoke test for issue #105: the C API metadata surface must let a
// binding enumerate every patcher object type and read its full
// documentation (description, category, inlets, outlets, parameters)
// without including any engine C++ header.
//
// The test asserts:
//   - yse_patcher_get_type_count() matches pRegistry.AllNames().
//   - Every type returned by yse_patcher_get_type_name() round-trips
//     through yse_patcher_is_valid_object().
//   - For each type, the per-field getters agree with the in-memory
//     cache built by the engine (description, category, is_dsp,
//     inlet/outlet/param counts and string fields are non-empty for
//     fields the doc-coverage test already requires to exist).
//   - yse_patcher_get_metadata_json() emits valid JSON whose top-level
//     keys equal the enumerated type set.
//   - Lookups for an unknown type name return the documented empty /
//     UNSET sentinels.

#include <doctest/doctest.h>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include "yse_c/yse_patcher.h"
#include "yse_c/yse_enums.h"
#include "patcher/pRegistry.h"
#include "utils/json.hpp"

TEST_SUITE("patcher") {

TEST_CASE("c-api metadata: type enumeration matches the registry") {
    const int n = yse_patcher_get_type_count();
    REQUIRE(n > 0);

    std::set<std::string> registry_names;
    for (const auto& s : YSE::PATCHER::Register().AllNames()) {
        registry_names.insert(s);
    }
    REQUIRE(static_cast<size_t>(n) == registry_names.size());

    std::set<std::string> c_api_names;
    for (int i = 0; i < n; ++i) {
        const char* name = yse_patcher_get_type_name(i);
        REQUIRE(name != nullptr);
        REQUIRE(name[0] != '\0');
        c_api_names.insert(name);
        CHECK(yse_patcher_is_valid_object(name) == 1);
    }
    CHECK(c_api_names == registry_names);
}

TEST_CASE("c-api metadata: every type exposes a populated description and category") {
    const int n = yse_patcher_get_type_count();
    for (int i = 0; i < n; ++i) {
        const char* name = yse_patcher_get_type_name(i);
        CAPTURE(name);

        const char* desc = yse_patcher_get_type_description(name);
        REQUIRE(desc != nullptr);
        CHECK(std::strlen(desc) > 0);

        YsePCategory cat = yse_patcher_get_type_category(name);
        CHECK(cat != YSE_PCAT_UNSET);

        // is_dsp returns 0 or 1; smoke-check the API returns a clean bool.
        const int isDsp = yse_patcher_get_type_is_dsp(name);
        CHECK((isDsp == 0 || isDsp == 1));
    }
}

TEST_CASE("c-api metadata: inlets / outlets / params reflect the in-code docs") {
    const int n = yse_patcher_get_type_count();
    for (int i = 0; i < n; ++i) {
        const char* name = yse_patcher_get_type_name(i);
        CAPTURE(name);

        const int nIn = yse_patcher_get_inlet_count(name);
        for (int j = 0; j < nIn; ++j) {
            const char* label = nullptr;
            const char* doc = nullptr;
            const char* range = nullptr;
            unsigned int accepts = 0;
            yse_patcher_get_inlet_info(name, j, &label, &doc, &range, &accepts);
            REQUIRE(label != nullptr);
            REQUIRE(doc != nullptr);
            REQUIRE(range != nullptr);
            CHECK(std::strlen(label) > 0);
            CHECK(std::strlen(doc) > 0);
        }

        const int nOut = yse_patcher_get_outlet_count(name);
        for (int j = 0; j < nOut; ++j) {
            const char* label = nullptr;
            const char* doc = nullptr;
            const char* range = nullptr;
            YseOutType type = YSE_OUT_INVALID;
            yse_patcher_get_outlet_info(name, j, &label, &doc, &range, &type);
            REQUIRE(label != nullptr);
            REQUIRE(doc != nullptr);
            REQUIRE(range != nullptr);
            CHECK(std::strlen(label) > 0);
            CHECK(std::strlen(doc) > 0);
            CHECK(type != YSE_OUT_INVALID);
        }

        const int nParam = yse_patcher_get_param_count(name);
        for (int j = 0; j < nParam; ++j) {
            const char* pname = nullptr;
            const char* pdoc = nullptr;
            const char* pdef = nullptr;
            const char* prange = nullptr;
            yse_patcher_get_param_info(name, j, &pname, &pdoc, &pdef, &prange);
            REQUIRE(pname != nullptr);
            REQUIRE(pdoc != nullptr);
            REQUIRE(pdef != nullptr);
            REQUIRE(prange != nullptr);
            CHECK(std::strlen(pname) > 0);
            CHECK(std::strlen(pdoc) > 0);
        }
    }
}

TEST_CASE("c-api metadata: optional out-pointers may be null") {
    const int n = yse_patcher_get_type_count();
    REQUIRE(n > 0);
    const char* name = yse_patcher_get_type_name(0);

    // All-null out-pointers must not crash even if the type has ports.
    yse_patcher_get_inlet_info(name, 0, nullptr, nullptr, nullptr, nullptr);
    yse_patcher_get_outlet_info(name, 0, nullptr, nullptr, nullptr, nullptr);
    yse_patcher_get_param_info(name, 0, nullptr, nullptr, nullptr, nullptr);
}

TEST_CASE("c-api metadata: unknown type names return empty/sentinel values") {
    const char* missing = "no_such_object_for_test";
    CHECK(yse_patcher_get_inlet_count(missing) == 0);
    CHECK(yse_patcher_get_outlet_count(missing) == 0);
    CHECK(yse_patcher_get_param_count(missing) == 0);
    CHECK(yse_patcher_get_type_category(missing) == YSE_PCAT_UNSET);
    CHECK(yse_patcher_get_type_is_dsp(missing) == 0);

    const char* desc = yse_patcher_get_type_description(missing);
    REQUIRE(desc != nullptr);
    CHECK(desc[0] == '\0');

    // NULL type_name is null-safe.
    CHECK(yse_patcher_get_inlet_count(nullptr) == 0);
    CHECK(yse_patcher_get_outlet_count(nullptr) == 0);
    CHECK(yse_patcher_get_param_count(nullptr) == 0);
}

TEST_CASE("c-api metadata: get_type_name out-of-range returns empty string") {
    const int n = yse_patcher_get_type_count();
    const char* s = yse_patcher_get_type_name(n);
    REQUIRE(s != nullptr);
    CHECK(s[0] == '\0');

    const char* s2 = yse_patcher_get_type_name(-1);
    REQUIRE(s2 != nullptr);
    CHECK(s2[0] == '\0');
}

TEST_CASE("c-api metadata: bulk JSON matches dump_patcher_meta snapshot per-object") {
    // Acceptance criterion #3 on issue #105: the C API's bulk JSON must
    // mirror the committed snapshot produced by
    // tools/dump_patcher_metadata. The snapshot lives next to the
    // Sphinx hook that consumes it.
    //
    // The snapshot is generated on Windows, but pRegistry conditionally
    // omits the MIDI patcher objects on non-Windows platforms (#if
    // YSE_WINDOWS in pRegistry.cpp), so on Linux/Android the live
    // registry is a strict subset of the snapshot. We therefore assert
    // per-object equality for every type the live registry exposes,
    // rather than whole-object equality of the two trees.
    const std::string snapshot_path =
        std::string(YSE_TEST_FIXTURES_DIR) +
        "/../../../documentation/source/_data/patcher_objects.json";
    std::ifstream f(snapshot_path);
    if (!f.is_open()) {
        // Snapshot not shipped in this build (e.g. on Android, where
        // the fixtures path points into the APK's internal data dir).
        MESSAGE("snapshot not found at " << snapshot_path << " - skipping parity check");
        return;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    auto snapshot = nlohmann::json::parse(ss.str(), nullptr, false);
    REQUIRE_FALSE(snapshot.is_discarded());

    char* json = yse_patcher_get_metadata_json();
    REQUIRE(json != nullptr);
    auto live = nlohmann::json::parse(json, nullptr, false);
    yse_free_string(json);
    REQUIRE_FALSE(live.is_discarded());

    for (auto it = live.begin(); it != live.end(); ++it) {
        const std::string& key = it.key();
        CAPTURE(key);
        const auto snap_it = snapshot.find(key);
        REQUIRE(snap_it != snapshot.end());
        CHECK(it.value() == *snap_it);
    }
}

TEST_CASE("c-api metadata: bulk JSON contains every registered type") {
    char* json = yse_patcher_get_metadata_json();
    REQUIRE(json != nullptr);

    auto parsed = nlohmann::json::parse(json, nullptr, false);
    yse_free_string(json);
    REQUIRE_FALSE(parsed.is_discarded());
    REQUIRE(parsed.is_object());

    const int n = yse_patcher_get_type_count();
    REQUIRE(static_cast<int>(parsed.size()) == n);

    auto hasKey = [](const nlohmann::json& obj, const char* key) {
        return obj.find(key) != obj.end();
    };
    for (int i = 0; i < n; ++i) {
        const char* name = yse_patcher_get_type_name(i);
        CAPTURE(name);
        REQUIRE(hasKey(parsed, name));
        const auto& entry = parsed[name];
        CHECK(hasKey(entry, "category"));
        CHECK(hasKey(entry, "description"));
        CHECK(hasKey(entry, "is_dsp"));
        CHECK(hasKey(entry, "inlets"));
        CHECK(hasKey(entry, "outlets"));
        CHECK(hasKey(entry, "params"));
    }
}

}  // TEST_SUITE

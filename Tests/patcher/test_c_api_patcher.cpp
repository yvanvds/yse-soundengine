// C-API boundary tests for YseEngine/c_api/yse_patcher.cpp (issue #568, the
// follow-up half of #417 / epic #420).
//
// The registry-metadata half of this TU already has a suite
// (Tests/patcher/test_c_api_metadata.cpp, issue #105) and is not repeated here.
// What had no C-level coverage was the graph half: object create / delete /
// connect / disconnect, the JSON dump + parse round-trip, the PassBang /
// PassData value path, and the whole pHandle accessor surface — that is what
// this file adds.
//
// Two contracts get the most attention because they are what a binding breaks
// on silently:
//
//   * The snprintf-style string convention shared by every yse_phandle_get_*
//     and yse_patcher_dump_json: the return value is always the FULL length,
//     the buffer is always NUL-terminated, and a NULL buffer or cap == 0 is a
//     size query. A NULL handle clears the caller's buffer and returns 0.
//   * NULL-handle safety on every entry point, including the pairs where only
//     one of two handles is NULL.
//
// The graph surface needs no audio hardware. The suite's shared offline engine
// is brought up anyway because the gReceive value path routes through the
// patcher bus, so everything here runs on headless CI.

#include <doctest/doctest.h>

#include <cstring>
#include <string>
#include <vector>

#include "support/capilowcov_offline.hpp"

#include "yse_c/yse_common.h"
#include "yse_c/yse_enums.h"
#include "yse_c/yse_patcher.h"

namespace {

  // Patcher object type names, as the flat C ABI sees them (the engine's
  // YSE::OBJ:: constants are C++ and deliberately not used here — a binding
  // only ever has the strings).
  constexpr const char* kSine = "~sine";
  constexpr const char* kMultiply = ".*";
  constexpr const char* kReceive = ".r";
  constexpr const char* kInt = ".i";

  // Read a snprintf-convention getter into a std::string, using the two-call
  // size-then-fill pattern a binding would use.
  template <typename Fn> std::string readString(Fn&& get) {
    const size_t need = get(nullptr, size_t{0});
    std::vector<char> buf(need + 1, '\0');
    get(buf.data(), buf.size());
    return std::string(buf.data());
  }

} // namespace

TEST_SUITE("capilowcov") {

  // ─── patcher lifecycle and graph editing ───────────────────────────────────

  TEST_CASE("c-api patcher: create / init / object count / clear / destroy") {
    YsePatcher* p = yse_patcher_create();
    REQUIRE(p != nullptr);
    yse_patcher_init(p, 2);
    CHECK(yse_patcher_objects(p) == 0u);

    YsePHandle* sine = yse_patcher_create_object(p, kSine, nullptr);
    REQUIRE(sine != nullptr);
    CHECK(yse_patcher_objects(p) == 1u);

    YsePHandle* mul = yse_patcher_create_object(p, kMultiply, "2");
    REQUIRE(mul != nullptr);
    CHECK(yse_patcher_objects(p) == 2u);

    // An unknown type is refused without disturbing the graph.
    CHECK(yse_patcher_create_object(p, "not_a_real_object", nullptr) == nullptr);
    CHECK(yse_patcher_objects(p) == 2u);

    yse_patcher_delete_object(p, sine);
    CHECK(yse_patcher_objects(p) == 1u);

    yse_patcher_clear(p);
    CHECK(yse_patcher_objects(p) == 0u);

    yse_patcher_destroy(p);
  }

  TEST_CASE("c-api patcher: handles are retrievable by list index and by ID") {
    YsePatcher* p = yse_patcher_create();
    REQUIRE(p != nullptr);
    yse_patcher_init(p, 2);

    YsePHandle* sine = yse_patcher_create_object(p, kSine, nullptr);
    REQUIRE(sine != nullptr);
    // The first object in a fresh patcher is ID 0. This used to read `id != 0u`,
    // which only ever held because the ID came from a process-wide counter that
    // had already run past 0 by the time any test looked — exactly the property
    // issue #730 removed. What the case is actually about is the round trip:
    // the ID a handle reports finds that same handle again.
    const unsigned int id = yse_phandle_get_id(sine);
    CHECK(id == 0u);

    CHECK(yse_patcher_get_handle_from_id(p, id) == sine);
    CHECK(yse_patcher_get_handle_from_list(p, 0) == sine);

    // Out-of-range lookups answer with NULL rather than a stale handle.
    CHECK(yse_patcher_get_handle_from_id(p, id + 9999) == nullptr);
    CHECK(yse_patcher_get_handle_from_list(p, 99) == nullptr);

    yse_patcher_destroy(p);
  }

  TEST_CASE("c-api patcher: connect / disconnect are visible through the handle") {
    YsePatcher* p = yse_patcher_create();
    REQUIRE(p != nullptr);
    yse_patcher_init(p, 2);

    YsePHandle* sine = yse_patcher_create_object(p, kSine, nullptr);
    YsePHandle* mul = yse_patcher_create_object(p, kMultiply, "2");
    REQUIRE(sine != nullptr);
    REQUIRE(mul != nullptr);

    CHECK(yse_phandle_get_connections(sine, 0) == 0u);

    yse_patcher_connect(p, sine, 0, mul, 0);
    REQUIRE(yse_phandle_get_connections(sine, 0) == 1u);
    CHECK(yse_phandle_get_connection_target(sine, 0, 0) == yse_phandle_get_id(mul));
    CHECK(yse_phandle_get_connection_target_inlet(sine, 0, 0) == 0u);

    yse_patcher_disconnect(p, sine, 0, mul, 0);
    CHECK(yse_phandle_get_connections(sine, 0) == 0u);

    yse_patcher_destroy(p);
  }

  TEST_CASE("c-api patcher: is_valid_object mirrors the registry") {
    CHECK(yse_patcher_is_valid_object(kSine) == 1);
    CHECK(yse_patcher_is_valid_object(kMultiply) == 1);
    CHECK(yse_patcher_is_valid_object("not_a_real_object") == 0);
    CHECK(yse_patcher_is_valid_object(nullptr) == 0);
  }

  // ─── JSON round-trip ───────────────────────────────────────────────────────

  TEST_CASE("c-api patcher: dump_json follows the snprintf convention") {
    YsePatcher* p = yse_patcher_create();
    REQUIRE(p != nullptr);
    yse_patcher_init(p, 2);
    REQUIRE(yse_patcher_create_object(p, kSine, nullptr) != nullptr);

    // Size query: NULL buffer, and cap == 0 with a real buffer.
    const size_t need = yse_patcher_dump_json(p, nullptr, 0);
    CHECK(need > 0u);
    char one = 'x';
    CHECK(yse_patcher_dump_json(p, &one, 0) == need);
    CHECK(one == 'x'); // cap == 0 must not write

    // Exact fit: need + 1 bytes hold the string plus its terminator.
    std::vector<char> exact(need + 1, '\1');
    CHECK(yse_patcher_dump_json(p, exact.data(), exact.size()) == need);
    CHECK(std::strlen(exact.data()) == need);

    // Truncation still returns the full length and still NUL-terminates.
    char small[8];
    std::memset(small, '\1', sizeof(small));
    CHECK(yse_patcher_dump_json(p, small, sizeof(small)) == need);
    CHECK(std::strlen(small) == sizeof(small) - 1);

    // NULL handle clears the buffer and reports nothing to copy.
    char scratch[4] = {'a', 'b', 'c', '\0'};
    CHECK(yse_patcher_dump_json(nullptr, scratch, sizeof(scratch)) == 0u);
    CHECK(scratch[0] == '\0');
    CHECK(yse_patcher_dump_json(nullptr, nullptr, 0) == 0u);

    yse_patcher_destroy(p);
  }

  TEST_CASE("c-api patcher: parse_json restores a dumped graph") {
    YsePatcher* src = yse_patcher_create();
    REQUIRE(src != nullptr);
    yse_patcher_init(src, 2);
    YsePHandle* sine = yse_patcher_create_object(src, kSine, nullptr);
    YsePHandle* mul = yse_patcher_create_object(src, kMultiply, "2");
    REQUIRE(sine != nullptr);
    REQUIRE(mul != nullptr);
    yse_patcher_connect(src, sine, 0, mul, 0);

    const std::string json =
        readString([src](char* b, size_t c) { return yse_patcher_dump_json(src, b, c); });
    REQUIRE(!json.empty());

    YsePatcher* dst = yse_patcher_create();
    REQUIRE(dst != nullptr);
    yse_patcher_init(dst, 2);
    yse_patcher_parse_json(dst, json.c_str());
    CHECK(yse_patcher_objects(dst) == yse_patcher_objects(src));

    // Malformed input is reported through last_error, not thrown across the ABI.
    yse_clear_last_error();
    yse_patcher_parse_json(dst, "{ this is not json");
    CHECK(std::string(yse_last_error()).empty() == false);
    yse_clear_last_error();

    // NULL handle / NULL content are no-ops.
    yse_patcher_parse_json(nullptr, json.c_str());
    yse_patcher_parse_json(dst, nullptr);

    yse_patcher_destroy(dst);
    yse_patcher_destroy(src);
  }

  // ─── value path (PassBang / PassData) ──────────────────────────────────────

  TEST_CASE("c-api patcher: pass_* reports whether a receiver exists") {
    if (!capilowcov::ensureOffline()) return; // engine unavailable → skip

    YsePatcher* p = yse_patcher_create();
    REQUIRE(p != nullptr);
    yse_patcher_init(p, 2);

    // The gReceive's dataName comes from the object's argument string.
    REQUIRE(yse_patcher_create_object(p, kReceive, "cutoff") != nullptr);

    CHECK(yse_patcher_pass_bang(p, "cutoff") == 1);
    CHECK(yse_patcher_pass_int(p, 42, "cutoff") == 1);
    CHECK(yse_patcher_pass_float(p, 0.5f, "cutoff") == 1);
    CHECK(yse_patcher_pass_string(p, "open sesame", "cutoff") == 1);

    // No such receiver → the call reports failure rather than silently dropping.
    CHECK(yse_patcher_pass_bang(p, "nosuchtarget") == 0);
    CHECK(yse_patcher_pass_int(p, 1, "nosuchtarget") == 0);
    CHECK(yse_patcher_pass_float(p, 1.f, "nosuchtarget") == 0);
    CHECK(yse_patcher_pass_string(p, "v", "nosuchtarget") == 0);

    // NULL patcher / NULL target / NULL value all report failure.
    CHECK(yse_patcher_pass_bang(nullptr, "cutoff") == 0);
    CHECK(yse_patcher_pass_bang(p, nullptr) == 0);
    CHECK(yse_patcher_pass_int(nullptr, 1, "cutoff") == 0);
    CHECK(yse_patcher_pass_int(p, 1, nullptr) == 0);
    CHECK(yse_patcher_pass_float(nullptr, 1.f, "cutoff") == 0);
    CHECK(yse_patcher_pass_float(p, 1.f, nullptr) == 0);
    CHECK(yse_patcher_pass_string(nullptr, "v", "cutoff") == 0);
    CHECK(yse_patcher_pass_string(p, nullptr, "cutoff") == 0);
    CHECK(yse_patcher_pass_string(p, "v", nullptr) == 0);

    yse_patcher_destroy(p);
  }

  // ─── pHandle accessors ─────────────────────────────────────────────────────

  TEST_CASE("c-api phandle: type / name / params / gui strings round-trip") {
    YsePatcher* p = yse_patcher_create();
    REQUIRE(p != nullptr);
    yse_patcher_init(p, 2);

    YsePHandle* h = yse_patcher_create_object(p, kInt, "7");
    REQUIRE(h != nullptr);

    CHECK(readString([h](char* b, size_t c) { return yse_phandle_get_type(h, b, c); }) == kInt);
    // Name and params are whatever the object reports; the contract under test
    // is that the size query and the fill agree.
    const std::string name =
        readString([h](char* b, size_t c) { return yse_phandle_get_name(h, b, c); });
    CHECK(name.size() == yse_phandle_get_name(h, nullptr, 0));
    const std::string params =
        readString([h](char* b, size_t c) { return yse_phandle_get_params(h, b, c); });
    CHECK(params.size() == yse_phandle_get_params(h, nullptr, 0));

    yse_phandle_set_params(h, "9");
    CHECK(yse_phandle_get_params(h, nullptr, 0) > 0u);

    // GUI properties are a free-form key/value store on the handle.
    yse_phandle_set_gui_property(h, "x", "120");
    CHECK(readString([h](char* b, size_t c) {
            return yse_phandle_get_gui_property(h, "x", b, c);
          }) == "120");
    // A NULL value clears rather than crashing.
    yse_phandle_set_gui_property(h, "x", nullptr);
    CHECK(readString([h](char* b, size_t c) {
            return yse_phandle_get_gui_property(h, "x", b, c);
          }).empty());
    // An unknown key reads back empty.
    CHECK(yse_phandle_get_gui_property(h, "never_set", nullptr, 0) == 0u);

    const std::string guiValue =
        readString([h](char* b, size_t c) { return yse_phandle_get_gui_value(h, b, c); });
    CHECK(guiValue.size() == yse_phandle_get_gui_value(h, nullptr, 0));

    yse_patcher_destroy(p);
  }

  TEST_CASE("c-api phandle: truncation still NUL-terminates and reports the full length") {
    YsePatcher* p = yse_patcher_create();
    REQUIRE(p != nullptr);
    yse_patcher_init(p, 2);
    YsePHandle* h = yse_patcher_create_object(p, kSine, nullptr);
    REQUIRE(h != nullptr);

    const size_t need = yse_phandle_get_type(h, nullptr, 0);
    REQUIRE(need >= 2u); // "~sine"

    char two[2];
    std::memset(two, '\1', sizeof(two));
    CHECK(yse_phandle_get_type(h, two, sizeof(two)) == need);
    CHECK(std::strlen(two) == 1u); // one char plus the terminator

    yse_patcher_destroy(p);
  }

  TEST_CASE("c-api phandle: port introspection and value setters") {
    YsePatcher* p = yse_patcher_create();
    REQUIRE(p != nullptr);
    yse_patcher_init(p, 2);

    YsePHandle* sine = yse_patcher_create_object(p, kSine, nullptr);
    YsePHandle* mul = yse_patcher_create_object(p, kMultiply, "2");
    REQUIRE(sine != nullptr);
    REQUIRE(mul != nullptr);

    CHECK(yse_phandle_get_inputs(sine) > 0);
    CHECK(yse_phandle_get_outputs(sine) > 0);
    // A DSP generator's first outlet carries a signal buffer.
    CHECK(yse_phandle_output_data_type(sine, 0) == YSE_OUT_BUFFER);
    // ... and the object reports which of its inlets are DSP inlets. Whatever
    // the answer, it must be a clean 0/1 rather than an uninitialised byte.
    const int isDsp = yse_phandle_is_dsp_input(mul, 0);
    CHECK((isDsp == 0 || isDsp == 1));

    // An out-of-range pin is reported as INVALID rather than read past the end.
    CHECK(yse_phandle_output_data_type(sine, 99) == YSE_OUT_INVALID);

    // The inlet setters are messages into the graph — they must be callable on
    // a live handle without an audio thread running.
    yse_phandle_set_bang(mul, 0);
    yse_phandle_set_int(mul, 1, 3);
    yse_phandle_set_float(mul, 1, 0.5f);
    yse_phandle_set_list(mul, 0, "1 2 3");

    yse_patcher_destroy(p);
  }

  // ─── NULL contracts ────────────────────────────────────────────────────────

  TEST_CASE("c-api patcher: every entry point is NULL-safe") {
    YsePatcher* p = yse_patcher_create();
    REQUIRE(p != nullptr);
    yse_patcher_init(p, 2);
    YsePHandle* h = yse_patcher_create_object(p, kSine, nullptr);
    REQUIRE(h != nullptr);

    yse_patcher_init(nullptr, 2);
    CHECK(yse_patcher_create_object(nullptr, kSine, nullptr) == nullptr);
    CHECK(yse_patcher_create_object(p, nullptr, nullptr) == nullptr);
    yse_patcher_delete_object(nullptr, h);
    yse_patcher_delete_object(p, nullptr);
    yse_patcher_clear(nullptr);
    yse_patcher_connect(nullptr, h, 0, h, 0);
    yse_patcher_connect(p, nullptr, 0, h, 0);
    yse_patcher_connect(p, h, 0, nullptr, 0);
    yse_patcher_disconnect(nullptr, h, 0, h, 0);
    yse_patcher_disconnect(p, nullptr, 0, h, 0);
    yse_patcher_disconnect(p, h, 0, nullptr, 0);
    CHECK(yse_patcher_objects(nullptr) == 0u);
    CHECK(yse_patcher_get_handle_from_list(nullptr, 0) == nullptr);
    CHECK(yse_patcher_get_handle_from_id(nullptr, 0) == nullptr);
    CHECK(yse_patcher_objects(p) == 1u); // none of the above changed the graph

    yse_patcher_destroy(p);
    yse_patcher_destroy(nullptr);
  }

  TEST_CASE("c-api phandle: every accessor is NULL-safe and clears its out buffer") {
    char buf[8];
    const auto dirty = [&buf] { std::memset(buf, 'x', sizeof(buf)); };

    dirty();
    CHECK(yse_phandle_get_type(nullptr, buf, sizeof(buf)) == 0u);
    CHECK(buf[0] == '\0');
    dirty();
    CHECK(yse_phandle_get_name(nullptr, buf, sizeof(buf)) == 0u);
    CHECK(buf[0] == '\0');
    dirty();
    CHECK(yse_phandle_get_params(nullptr, buf, sizeof(buf)) == 0u);
    CHECK(buf[0] == '\0');
    dirty();
    CHECK(yse_phandle_get_gui_value(nullptr, buf, sizeof(buf)) == 0u);
    CHECK(buf[0] == '\0');
    dirty();
    CHECK(yse_phandle_get_gui_property(nullptr, "k", buf, sizeof(buf)) == 0u);
    CHECK(buf[0] == '\0');

    // A live-but-keyless call takes the same early-out path.
    YsePatcher* p = yse_patcher_create();
    REQUIRE(p != nullptr);
    yse_patcher_init(p, 2);
    YsePHandle* h = yse_patcher_create_object(p, kSine, nullptr);
    REQUIRE(h != nullptr);
    dirty();
    CHECK(yse_phandle_get_gui_property(h, nullptr, buf, sizeof(buf)) == 0u);
    CHECK(buf[0] == '\0');

    // NULL buffers are accepted everywhere as size queries.
    CHECK(yse_phandle_get_type(nullptr, nullptr, 0) == 0u);
    CHECK(yse_phandle_get_name(nullptr, nullptr, 0) == 0u);
    CHECK(yse_phandle_get_params(nullptr, nullptr, 0) == 0u);
    CHECK(yse_phandle_get_gui_value(nullptr, nullptr, 0) == 0u);
    CHECK(yse_phandle_get_gui_property(nullptr, "k", nullptr, 0) == 0u);

    yse_phandle_set_gui_property(nullptr, "k", "v");
    yse_phandle_set_gui_property(h, nullptr, "v");
    yse_phandle_set_bang(nullptr, 0);
    yse_phandle_set_int(nullptr, 0, 1);
    yse_phandle_set_float(nullptr, 0, 1.f);
    yse_phandle_set_list(nullptr, 0, "1");
    yse_phandle_set_list(h, 0, nullptr);
    yse_phandle_set_params(nullptr, "1");
    yse_phandle_set_params(h, nullptr);

    CHECK(yse_phandle_get_inputs(nullptr) == 0);
    CHECK(yse_phandle_get_outputs(nullptr) == 0);
    CHECK(yse_phandle_is_dsp_input(nullptr, 0) == 0);
    CHECK(yse_phandle_output_data_type(nullptr, 0) == YSE_OUT_INVALID);
    CHECK(yse_phandle_get_id(nullptr) == 0u);
    CHECK(yse_phandle_get_connections(nullptr, 0) == 0u);
    CHECK(yse_phandle_get_connection_target(nullptr, 0, 0) == 0u);
    CHECK(yse_phandle_get_connection_target_inlet(nullptr, 0, 0) == 0u);

    yse_patcher_destroy(p);
  }

  // ─── metadata JSON string ownership ────────────────────────────────────────

  TEST_CASE("c-api patcher: metadata json is heap-owned and freed by yse_free_string") {
    char* json = yse_patcher_get_metadata_json();
    REQUIRE(json != nullptr);
    CHECK(std::strlen(json) > 0u);
    yse_free_string(json);
    yse_free_string(nullptr); // documented as NULL-tolerant
  }

} // TEST_SUITE("capilowcov")

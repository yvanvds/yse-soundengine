// Tests for YSE::BufferIO (YseEngine/BufferIO.{hpp,cpp}).
//
// BufferIO is the in-memory VFS layer used to feed sounds from byte buffers
// rather than the filesystem. We exercise the public registration API here.
//
// NOTE — BufferIO uses a process-global `IOBuffers*` and its destructor
//   deletes it without nulling, so creating multiple BufferIO instances in a
//   single process triggers a use-after-free (pre-existing bug, out of scope
//   for this commit). Tests therefore live inside ONE TEST_CASE with
//   SUBCASEs sharing a single BufferIO instance.

#include <doctest/doctest.h>
#include "BufferIO.hpp"
#include <cstring>

TEST_SUITE("io") {

TEST_CASE("BufferIO: public API exercised on a single instance") {
    YSE::BufferIO io;

    SUBCASE("default-constructed is inactive") {
        CHECK_FALSE(io.GetActive());
    }

    SUBCASE("SetActive toggles state") {
        io.SetActive(true);
        CHECK(io.GetActive());
        io.SetActive(false);
        CHECK_FALSE(io.GetActive());
    }

    SUBCASE("idempotent activate / deactivate") {
        io.SetActive(true);
        io.SetActive(true);    // double-activate
        CHECK(io.GetActive());
        io.SetActive(false);
        io.SetActive(false);   // double-deactivate
        CHECK_FALSE(io.GetActive());
    }

    SUBCASE("AddBuffer / BufferNameExists round-trip") {
        io.SetActive(true);
        char data[16] = {0};
        CHECK(io.AddBuffer("buf-rt", data, 16));
        CHECK(io.BufferNameExists("buf-rt"));
        CHECK(io.BufferExists(data));
        CHECK(io.RemoveBuffer(data));
        CHECK_FALSE(io.BufferExists(data));
        io.SetActive(false);
    }

    SUBCASE("BufferNameExists false for unknown ID") {
        CHECK_FALSE(io.BufferNameExists("not-registered"));
    }

    SUBCASE("BufferExists false for unknown pointer") {
        char data[4] = {0};
        CHECK_FALSE(io.BufferExists(data));
    }

    SUBCASE("AddBuffer rejects duplicate IDs") {
        char a[4] = {1, 2, 3, 4};
        char b[4] = {5, 6, 7, 8};
        CHECK(io.AddBuffer("dup-id", a, 4));
        CHECK_FALSE(io.AddBuffer("dup-id", b, 4));
        CHECK(io.RemoveBufferByName("dup-id"));
    }

    SUBCASE("RemoveBufferByName succeeds for known ID") {
        char data[4] = {0};
        REQUIRE(io.AddBuffer("rm-byname", data, 4));
        CHECK(io.RemoveBufferByName("rm-byname"));
        CHECK_FALSE(io.BufferNameExists("rm-byname"));
    }

    SUBCASE("RemoveBufferByName fails for unknown ID") {
        CHECK_FALSE(io.RemoveBufferByName("never-added"));
    }

    SUBCASE("RemoveBuffer by pointer") {
        char data[4] = {0};
        REQUIRE(io.AddBuffer("rm-byptr", data, 4));
        CHECK(io.RemoveBuffer(data));
        CHECK_FALSE(io.BufferExists(data));
    }

    SUBCASE("RemoveBuffer fails for unknown pointer") {
        char unknown[4] = {0};
        CHECK_FALSE(io.RemoveBuffer(unknown));
    }

    SUBCASE("RemoveBufferByName clears via BufferNameExists") {
        char data[4] = {1, 2, 3, 4};
        REQUIRE(io.AddBuffer("name-roundtrip", data, 4));
        CHECK(io.BufferNameExists("name-roundtrip"));
        CHECK(io.RemoveBufferByName("name-roundtrip"));
        CHECK_FALSE(io.BufferNameExists("name-roundtrip"));
    }
}

} // TEST_SUITE("io")

// Separate TEST_SUITE so the storeCopy=true instance does not collide with the
// shared default-constructed one above (the BufferIO process-global is
// recreated whenever the previous instance dies — see the comment at the top
// of this file).
TEST_SUITE("io") {

TEST_CASE("BufferIO storeCopy mode: owned copy survives caller scope") {
    YSE::BufferIO io(/*storeCopy=*/true);
    io.SetActive(true);
    {
        char volatile_data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        REQUIRE(io.AddBuffer("copy-survives", volatile_data, 8));
        CHECK(io.BufferNameExists("copy-survives"));
    } // volatile_data goes out of scope; the owned copy is intact.
    CHECK(io.BufferNameExists("copy-survives"));
    // Remove by name in copy mode runs delete[] on the owned copy.
    CHECK(io.RemoveBufferByName("copy-survives"));
    io.SetActive(false);
}

} // TEST_SUITE("io")

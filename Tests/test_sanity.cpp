#include <doctest/doctest.h>

// Trivial smoke test — verifies the doctest harness + CTest pipeline wiring
// end-to-end before any real logic is exercised.
TEST_CASE("sanity: arithmetic") {
    CHECK(1 + 1 == 2);
    CHECK(2 * 3 == 6);
}

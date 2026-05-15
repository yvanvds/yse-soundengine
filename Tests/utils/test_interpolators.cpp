// Tests for YSE::linearInterpolator early-exit branches and YSE::objectInterpolator<T>
// (YseEngine/utils/interpolators.hpp).
//
// objectInterpolator semantics:
//   - First set() seeds the `current` value immediately (no animation).
//   - Subsequent set() with time>0 stores a target and starts a timer.
//   - update(timeDelta) decrements the timer; when it reaches zero, current
//     becomes target.
//   - operator() returns either current or target with probability weighted by
//     remaining time / total time (uses YSE::RandomF, so non-deterministic; we
//     only check that the result is one of the two valid values).

#include <doctest/doctest.h>
#include "utils/interpolators.hpp"

TEST_SUITE("utils") {

// --- linearInterpolator: edge cases not covered in test_vector.cpp ---

TEST_CASE("linearInterpolator: default-constructed value is 0") {
    YSE::linearInterpolator li;
    CHECK(li() == doctest::Approx(0.0f));
    CHECK(li.target() == doctest::Approx(0.0f));
}

TEST_CASE("linearInterpolator: update() before any set() is a no-op") {
    // timeLeft starts at 0, so the inner `if (timeLeft > 0)` is skipped.
    YSE::linearInterpolator li;
    li.update(5.0f);
    CHECK(li() == doctest::Approx(0.0f));
}

TEST_CASE("linearInterpolator: multiple small updates converge to target") {
    YSE::linearInterpolator li;
    li.set(10.0f, 4.0f);
    for (int i = 0; i < 4; ++i) li.update(1.0f);
    CHECK(li() == doctest::Approx(10.0f));
}

TEST_CASE("linearInterpolator: chained set() restarts from current value") {
    YSE::linearInterpolator li;
    li.set(10.0f, 0.0f);  // jump to 10
    li.set(20.0f, 2.0f);  // ramp 10 → 20 over 2 s
    li.update(1.0f);
    CHECK(li() == doctest::Approx(15.0f));
}

TEST_CASE("linearInterpolator: update with exact remaining time lands on target") {
    YSE::linearInterpolator li;
    li.set(2.0f, 2.0f);
    li.update(2.0f);
    CHECK(li() == doctest::Approx(2.0f));
    CHECK(li.target() == doctest::Approx(2.0f));
}

// --- objectInterpolator<int> ---

TEST_CASE("objectInterpolator<int>: isSet() is false before first set") {
    YSE::objectInterpolator<int> oi;
    CHECK(oi.isSet() == false);
}

TEST_CASE("objectInterpolator<int>: first set seeds current immediately and ignores time") {
    YSE::objectInterpolator<int> oi;
    oi.set(42, 10.0f);  // first set: time argument is ignored
    CHECK(oi.isSet() == true);
    CHECK(oi() == 42);
}

TEST_CASE("objectInterpolator<int>: second set with time=0 changes current immediately") {
    YSE::objectInterpolator<int> oi;
    oi.set(1, 0.0f);
    oi.set(2, 0.0f);  // time=0 → assigns to current directly
    CHECK(oi() == 2);
}

TEST_CASE("objectInterpolator<int>: second set with time>0 keeps current until update completes") {
    YSE::objectInterpolator<int> oi;
    oi.set(1, 0.0f);
    oi.set(2, 1.0f);
    // With timeLeft = totalTime, the random branch always returns current
    // (because RandomF(total) < total). Verify deterministically.
    for (int i = 0; i < 50; ++i) {
        CHECK(oi() == 1);
    }
}

TEST_CASE("objectInterpolator<int>: update drains timeLeft and finalises target") {
    YSE::objectInterpolator<int> oi;
    oi.set(7, 0.0f);
    oi.set(13, 2.0f);
    oi.update(1.0f);  // half done, still in random window
    oi.update(1.0f);  // exhausted → current := target
    for (int i = 0; i < 50; ++i) {
        CHECK(oi() == 13);
    }
}

TEST_CASE("objectInterpolator<int>: update past total time clamps timeLeft and finalises") {
    YSE::objectInterpolator<int> oi;
    oi.set(0, 0.0f);
    oi.set(99, 1.0f);
    oi.update(100.0f);  // far past total
    for (int i = 0; i < 50; ++i) {
        CHECK(oi() == 99);
    }
}

TEST_CASE("objectInterpolator<int>: update before any set is a no-op") {
    YSE::objectInterpolator<int> oi;
    oi.update(5.0f);
    CHECK(oi.isSet() == false);
}

TEST_CASE("objectInterpolator<int>: during ramp, operator() returns either current or target") {
    YSE::objectInterpolator<int> oi;
    oi.set(10, 0.0f);
    oi.set(20, 2.0f);
    oi.update(1.0f);  // halfway — RandomF(2) compared against timeLeft=1
    bool sawCurrent = false, sawTarget = false;
    for (int i = 0; i < 200; ++i) {
        int v = oi();
        CHECK((v == 10 || v == 20));
        if (v == 10) sawCurrent = true;
        if (v == 20) sawTarget = true;
    }
    // With 200 samples at ~50/50, both branches should fire almost surely.
    CHECK(sawCurrent);
    CHECK(sawTarget);
}

// --- objectInterpolator with a non-trivial type ---

struct Tag {
    int id = 0;
    Tag() = default;
    Tag(int i) : id(i) {}
};

TEST_CASE("objectInterpolator<Tag>: works with a user-defined type") {
    YSE::objectInterpolator<Tag> oi;
    oi.set(Tag(1), 0.0f);
    CHECK(oi().id == 1);
    oi.set(Tag(2), 1.0f);
    oi.update(2.0f);
    CHECK(oi().id == 2);
}

} // TEST_SUITE("utils")

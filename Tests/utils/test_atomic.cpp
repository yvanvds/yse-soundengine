// Tests for YSE atomic primitives (weak_atomic, aBool/aInt/aUInt/aFlt),
// and named math/buffer constants from utils/misc.hpp and headers/constants.hpp.

#include <doctest/doctest.h>
#include "utils/atomicOps.hpp"
#include "headers/types.hpp"
#include "headers/constants.hpp"
#include "utils/misc.hpp"
#include <thread>

TEST_SUITE("utils") {

// --- weak_atomic ---

TEST_CASE("weak_atomic<int>: construction from value and load") {
    YSE::weak_atomic<int> a(42);
    CHECK(a.load() == 42);
}

TEST_CASE("weak_atomic<int>: conversion operator") {
    YSE::weak_atomic<int> a(7);
    int v = a;
    CHECK(v == 7);
}

TEST_CASE("weak_atomic<int>: store via assignment and load round-trip") {
    YSE::weak_atomic<int> a(0);
    a = 99;
    CHECK(a.load() == 99);
}

TEST_CASE("weak_atomic<int>: copy construction") {
    YSE::weak_atomic<int> a(55);
    YSE::weak_atomic<int> b(a);
    CHECK(b.load() == 55);
}

TEST_CASE("weak_atomic<float>: store and load") {
    YSE::weak_atomic<float> a(3.14f);
    CHECK(a.load() == doctest::Approx(3.14f));
    a = 2.71f;
    CHECK(a.load() == doctest::Approx(2.71f));
}

// --- std::atomic types: aBool, aInt, aUInt, aFlt ---
// Note: these typedefs are at global scope (not inside namespace YSE).

TEST_CASE("aBool: default value false, store/load") {
    aBool b(false);
    CHECK(b.load() == false);
    b.store(true);
    CHECK(b.load() == true);
}

TEST_CASE("aBool: is_lock_free does not crash") {
    aBool b(false);
    (void)b.is_lock_free();
}

TEST_CASE("aInt: load/store round-trip") {
    aInt i(0);
    i.store(1234);
    CHECK(i.load() == 1234);
}

TEST_CASE("aInt: compare_exchange_strong success") {
    aInt i(10);
    Int expected = 10;
    bool ok = i.compare_exchange_strong(expected, 20);
    CHECK(ok == true);
    CHECK(i.load() == 20);
}

TEST_CASE("aInt: compare_exchange_strong failure updates expected") {
    aInt i(10);
    Int expected = 99;
    bool ok = i.compare_exchange_strong(expected, 20);
    CHECK(ok == false);
    CHECK(i.load() == 10);   // unchanged
    CHECK(expected == 10);   // updated to actual value
}

TEST_CASE("aUInt: load/store round-trip") {
    aUInt u(0u);
    u.store(42u);
    CHECK(u.load() == 42u);
}

TEST_CASE("aFlt: load/store round-trip") {
    aFlt f(0.0f);
    f.store(1.5f);
    CHECK(f.load() == doctest::Approx(1.5f));
}

TEST_CASE("aInt: concurrent fetch_add from two threads produces exact total") {
    aInt counter(0);
    constexpr int N = 1000;
    auto worker = [&]() {
        for (int i = 0; i < N; ++i)
            counter.fetch_add(1, std::memory_order_relaxed);
    };
    std::thread t1(worker), t2(worker);
    t1.join();
    t2.join();
    CHECK(counter.load() == 2 * N);
}

// --- Named constants ---

TEST_CASE("constants: Pi matches expected value") {
    CHECK(YSE::Pi == doctest::Approx(3.14159265f).epsilon(1e-6f));
}

TEST_CASE("constants: Pi2 equals two times Pi") {
    CHECK(YSE::Pi2 == doctest::Approx(2.0f * YSE::Pi).epsilon(1e-6f));
}

TEST_CASE("constants: Pi_2 equals Pi divided by two") {
    CHECK(YSE::Pi_2 == doctest::Approx(YSE::Pi / 2.0f).epsilon(1e-6f));
}

TEST_CASE("constants: STANDARD_BUFFERSIZE is 128") {
    CHECK(YSE::STANDARD_BUFFERSIZE == 128u);
}

TEST_CASE("constants: STREAM_BUFFERSIZE is 44100") {
    CHECK(YSE::STREAM_BUFFERSIZE == 44100u);
}

} // TEST_SUITE("utils")

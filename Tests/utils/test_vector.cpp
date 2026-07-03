// Tests for YSE::Pos (3D vector), free functions (Dist, Dot, Min, Avg),
// and YSE::linearInterpolator (utils/interpolators.hpp).

#include <doctest/doctest.h>
#include "utils/vector.hpp"
#include "utils/interpolators.hpp"

TEST_SUITE("utils") {

  // --- Pos: construction ---

  TEST_CASE("Pos: default construction initialises to zero") {
    YSE::Pos p;
    CHECK(p.x == 0.0f);
    CHECK(p.y == 0.0f);
    CHECK(p.z == 0.0f);
  }

  TEST_CASE("Pos: construction from single float sets all components") {
    YSE::Pos p(3.0f);
    CHECK(p.x == doctest::Approx(3.0f));
    CHECK(p.y == doctest::Approx(3.0f));
    CHECK(p.z == doctest::Approx(3.0f));
  }

  TEST_CASE("Pos: construction from three floats") {
    YSE::Pos p(1.0f, 2.0f, 3.0f);
    CHECK(p.x == doctest::Approx(1.0f));
    CHECK(p.y == doctest::Approx(2.0f));
    CHECK(p.z == doctest::Approx(3.0f));
  }

  TEST_CASE("Pos: zero() resets all components to zero") {
    YSE::Pos p(1.0f, 2.0f, 3.0f);
    p.zero();
    CHECK(p.x == 0.0f);
    CHECK(p.y == 0.0f);
    CHECK(p.z == 0.0f);
  }

  // --- Pos: arithmetic operators ---

  TEST_CASE("Pos: operator+ with scalar") {
    YSE::Pos r = YSE::Pos(1.0f, 2.0f, 3.0f) + 1.0f;
    CHECK(r.x == doctest::Approx(2.0f));
    CHECK(r.y == doctest::Approx(3.0f));
    CHECK(r.z == doctest::Approx(4.0f));
  }

  TEST_CASE("Pos: operator- with scalar") {
    YSE::Pos r = YSE::Pos(3.0f, 4.0f, 5.0f) - 1.0f;
    CHECK(r.x == doctest::Approx(2.0f));
    CHECK(r.y == doctest::Approx(3.0f));
    CHECK(r.z == doctest::Approx(4.0f));
  }

  TEST_CASE("Pos: operator* with scalar") {
    YSE::Pos r = YSE::Pos(1.0f, 2.0f, 3.0f) * 2.0f;
    CHECK(r.x == doctest::Approx(2.0f));
    CHECK(r.y == doctest::Approx(4.0f));
    CHECK(r.z == doctest::Approx(6.0f));
  }

  TEST_CASE("Pos: operator/ with scalar") {
    YSE::Pos r = YSE::Pos(4.0f, 6.0f, 8.0f) / 2.0f;
    CHECK(r.x == doctest::Approx(2.0f));
    CHECK(r.y == doctest::Approx(3.0f));
    CHECK(r.z == doctest::Approx(4.0f));
  }

  TEST_CASE("Pos: operator+ with Pos") {
    YSE::Pos r = YSE::Pos(1.0f, 2.0f, 3.0f) + YSE::Pos(4.0f, 5.0f, 6.0f);
    CHECK(r.x == doctest::Approx(5.0f));
    CHECK(r.y == doctest::Approx(7.0f));
    CHECK(r.z == doctest::Approx(9.0f));
  }

  TEST_CASE("Pos: operator- with Pos") {
    YSE::Pos r = YSE::Pos(5.0f, 7.0f, 9.0f) - YSE::Pos(1.0f, 2.0f, 3.0f);
    CHECK(r.x == doctest::Approx(4.0f));
    CHECK(r.y == doctest::Approx(5.0f));
    CHECK(r.z == doctest::Approx(6.0f));
  }

  TEST_CASE("Pos: operator+= with scalar") {
    YSE::Pos p(1.0f, 2.0f, 3.0f);
    p += 1.0f;
    CHECK(p.x == doctest::Approx(2.0f));
    CHECK(p.z == doctest::Approx(4.0f));
  }

  TEST_CASE("Pos: operator*= with scalar") {
    YSE::Pos p(2.0f, 3.0f, 4.0f);
    p *= 2.0f;
    CHECK(p.x == doctest::Approx(4.0f));
    CHECK(p.y == doctest::Approx(6.0f));
    CHECK(p.z == doctest::Approx(8.0f));
  }

  TEST_CASE("Pos: operator== and operator!=") {
    YSE::Pos a(1.0f, 2.0f, 3.0f), b(1.0f, 2.0f, 3.0f), c(0.0f, 0.0f, 0.0f);
    CHECK(a == b);
    CHECK(a != c);
  }

  // --- Pos: length ---

  TEST_CASE("Pos: length of zero vector is zero") {
    CHECK(YSE::Pos().length() == doctest::Approx(0.0f));
  }

  TEST_CASE("Pos: length of unit vector along x is 1") {
    CHECK(YSE::Pos(1.0f, 0.0f, 0.0f).length() == doctest::Approx(1.0f));
  }

  TEST_CASE("Pos: length of (3, 4, 0) is 5 (Pythagorean triple)") {
    CHECK(YSE::Pos(3.0f, 4.0f, 0.0f).length() == doctest::Approx(5.0f));
  }

  TEST_CASE("Pos: length of (1, 1, 1) is sqrt(3)") {
    CHECK(YSE::Pos(1.0f, 1.0f, 1.0f).length() == doctest::Approx(1.7320508f).epsilon(1e-5f));
  }

  // --- Free functions: Dist, Dot, Avg, Min ---

  TEST_CASE("Dist: distance between origin and (3, 4, 0) is 5") {
    YSE::Pos a(0.0f, 0.0f, 0.0f), b(3.0f, 4.0f, 0.0f);
    CHECK(YSE::Dist(a, b) == doctest::Approx(5.0f));
  }

  TEST_CASE("Dist: distance from a point to itself is zero") {
    YSE::Pos a(1.0f, 2.0f, 3.0f);
    CHECK(YSE::Dist(a, a) == doctest::Approx(0.0f));
  }

  TEST_CASE("Dot: perpendicular unit vectors have dot product zero") {
    YSE::Pos x(1.0f, 0.0f, 0.0f), y(0.0f, 1.0f, 0.0f);
    CHECK(YSE::Dot(x, y) == doctest::Approx(0.0f));
  }

  TEST_CASE("Dot: parallel vector with itself equals squared length") {
    YSE::Pos a(1.0f, 2.0f, 3.0f);
    // 1*1 + 2*2 + 3*3 = 14
    CHECK(YSE::Dot(a, a) == doctest::Approx(14.0f));
  }

  TEST_CASE("Avg: midpoint of two positions") {
    YSE::Pos r = YSE::Avg(YSE::Pos(0.0f, 0.0f, 0.0f), YSE::Pos(2.0f, 4.0f, 6.0f));
    CHECK(r.x == doctest::Approx(1.0f));
    CHECK(r.y == doctest::Approx(2.0f));
    CHECK(r.z == doctest::Approx(3.0f));
  }

  TEST_CASE("Min(Pos): returns component-wise minimum") {
    YSE::Pos r = YSE::Min(YSE::Pos(1.0f, 5.0f, 3.0f), YSE::Pos(4.0f, 2.0f, 6.0f));
    CHECK(r.x == doctest::Approx(1.0f));
    CHECK(r.y == doctest::Approx(2.0f));
    CHECK(r.z == doctest::Approx(3.0f));
  }

  // --- linearInterpolator ---

  TEST_CASE("linearInterpolator: set with zero time reaches target immediately") {
    YSE::linearInterpolator li;
    li.set(5.0f, 0.0f);
    CHECK(li() == doctest::Approx(5.0f));
  }

  TEST_CASE("linearInterpolator: update reaches midpoint at half total time") {
    YSE::linearInterpolator li;
    li.set(2.0f, 2.0f); // 0 → 2 over 2 s
    li.update(1.0f); // halfway
    CHECK(li() == doctest::Approx(1.0f));
  }

  TEST_CASE("linearInterpolator: full update reaches target exactly") {
    YSE::linearInterpolator li;
    li.set(3.0f, 1.0f);
    li.update(1.0f);
    CHECK(li() == doctest::Approx(3.0f));
  }

  TEST_CASE("linearInterpolator: value stays within [start, target] throughout") {
    YSE::linearInterpolator li;
    li.set(10.0f, 5.0f); // 0 → 10 over 5 s
    for (int step = 0; step <= 5; ++step) {
      float v = li();
      CHECK(v >= -0.001f); // at or above start (0)
      CHECK(v <= 10.001f); // at or below target (10)
      li.update(1.0f);
    }
  }

  TEST_CASE("linearInterpolator: target() accessor returns the target value") {
    YSE::linearInterpolator li;
    li.set(7.0f, 3.0f);
    CHECK(li.target() == doctest::Approx(7.0f));
  }

  TEST_CASE("linearInterpolator: value does not overshoot when update exceeds total time") {
    YSE::linearInterpolator li;
    li.set(4.0f, 1.0f);
    li.update(10.0f); // far past total time
    CHECK(li() == doctest::Approx(4.0f));
  }

} // TEST_SUITE("utils")

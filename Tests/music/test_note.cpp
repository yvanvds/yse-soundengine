// Tests for MUSIC::note and MUSIC::pNote (YseEngine/music/).
//
// Coverage:
//   - note: construction (default and explicit), copy constructor
//   - note: getter/setter round-trips for all four fields (pitch, volume, length, channel)
//   - note: set() multi-field update
//   - note: arithmetic operators (pitch only): +=, -=, *=, /= with float and note
//   - note: friend arithmetic operators (+, -, *, /)
//   - note: comparison operators (pitch-based): ==, !=, <, >, <=, >=
//   - note: operator= from pNote
//   - pNote: construction from explicit fields and from a note
//   - pNote: setPosition/getPosition round-trip and chaining
//   - pNote: inherits note arithmetic on pitch without changing position
//
// Note: MUSIC::chord (chord.hpp) is declared but chord.cpp contains no method
// implementations.  Chord tests are deferred until the implementation is complete.
//
// No engine initialisation is required — note and pNote are plain value types
// with no engine-thread dependencies.

#include <doctest/doctest.h>
#include "music/note.hpp"
#include "music/pNote.hpp"

TEST_SUITE("music") {

  // ─── note: construction ───────────────────────────────────────────────────────

  TEST_CASE("note: default construction stores expected defaults") {
    YSE::MUSIC::note n;
    CHECK(n.getPitch() == doctest::Approx(60.f));
    CHECK(n.getVolume() == doctest::Approx(1.f));
    CHECK(n.getLength() == doctest::Approx(0.f));
    CHECK(n.getChannel() == 1);
  }

  TEST_CASE("note: explicit construction stores all fields") {
    YSE::MUSIC::note n(72.f, 0.5f, 1.0f, 2);
    CHECK(n.getPitch() == doctest::Approx(72.f));
    CHECK(n.getVolume() == doctest::Approx(0.5f));
    CHECK(n.getLength() == doctest::Approx(1.0f));
    CHECK(n.getChannel() == 2);
  }

  TEST_CASE("note: copy constructor reproduces all fields") {
    YSE::MUSIC::note orig(48.f, 0.8f, 2.0f, 3);
    YSE::MUSIC::note copy(orig);
    CHECK(copy.getPitch() == doctest::Approx(48.f));
    CHECK(copy.getVolume() == doctest::Approx(0.8f));
    CHECK(copy.getLength() == doctest::Approx(2.0f));
    CHECK(copy.getChannel() == 3);
  }

  // ─── note: setter round-trips ─────────────────────────────────────────────────

  TEST_CASE("note: setPitch round-trip") {
    YSE::MUSIC::note n;
    n.setPitch(55.f);
    CHECK(n.getPitch() == doctest::Approx(55.f));
  }

  TEST_CASE("note: setVolume round-trip") {
    YSE::MUSIC::note n;
    n.setVolume(0.75f);
    CHECK(n.getVolume() == doctest::Approx(0.75f));
  }

  TEST_CASE("note: setLength round-trip") {
    YSE::MUSIC::note n;
    n.setLength(2.5f);
    CHECK(n.getLength() == doctest::Approx(2.5f));
  }

  TEST_CASE("note: setChannel round-trip") {
    YSE::MUSIC::note n;
    n.setChannel(5);
    CHECK(n.getChannel() == 5);
  }

  TEST_CASE("note: set() updates all four fields") {
    YSE::MUSIC::note n;
    n.set(80.f, 0.6f, 3.0f, 4);
    CHECK(n.getPitch() == doctest::Approx(80.f));
    CHECK(n.getVolume() == doctest::Approx(0.6f));
    CHECK(n.getLength() == doctest::Approx(3.0f));
    CHECK(n.getChannel() == 4);
  }

  // ─── note: arithmetic operators (pitch only) ─────────────────────────────────

  TEST_CASE("note: operator+= float changes pitch and leaves volume unchanged") {
    YSE::MUSIC::note n(60.f, 1.f, 0.f, 1);
    n += 5.f;
    CHECK(n.getPitch() == doctest::Approx(65.f));
    CHECK(n.getVolume() == doctest::Approx(1.f));
  }

  TEST_CASE("note: operator-= float decrements pitch") {
    YSE::MUSIC::note n(60.f);
    n -= 12.f;
    CHECK(n.getPitch() == doctest::Approx(48.f));
  }

  TEST_CASE("note: operator*= float scales pitch") {
    YSE::MUSIC::note n(30.f);
    n *= 2.f;
    CHECK(n.getPitch() == doctest::Approx(60.f));
  }

  TEST_CASE("note: operator/= float divides pitch") {
    YSE::MUSIC::note n(60.f);
    n /= 2.f;
    CHECK(n.getPitch() == doctest::Approx(30.f));
  }

  TEST_CASE("note: operator+= note uses other note's pitch") {
    YSE::MUSIC::note a(60.f), b(7.f);
    a += b;
    CHECK(a.getPitch() == doctest::Approx(67.f));
  }

  TEST_CASE("note: operator-= note uses other note's pitch") {
    YSE::MUSIC::note a(67.f), b(7.f);
    a -= b;
    CHECK(a.getPitch() == doctest::Approx(60.f));
  }

  TEST_CASE("note: operator*= note multiplies pitches") {
    YSE::MUSIC::note a(2.f), b(30.f);
    a *= b;
    CHECK(a.getPitch() == doctest::Approx(60.f));
  }

  TEST_CASE("note: operator/= note divides pitches") {
    YSE::MUSIC::note a(60.f), b(2.f);
    a /= b;
    CHECK(a.getPitch() == doctest::Approx(30.f));
  }

  // ─── note: friend arithmetic operators ───────────────────────────────────────

  TEST_CASE("note: operator+ (note, float) returns new note; original unchanged") {
    YSE::MUSIC::note n(60.f);
    YSE::MUSIC::note r = n + 5.f;
    CHECK(r.getPitch() == doctest::Approx(65.f));
    CHECK(n.getPitch() == doctest::Approx(60.f));
  }

  TEST_CASE("note: operator- (note, float) returns new note") {
    YSE::MUSIC::note n(60.f);
    YSE::MUSIC::note r = n - 12.f;
    CHECK(r.getPitch() == doctest::Approx(48.f));
  }

  TEST_CASE("note: operator* (note, float) returns new note") {
    YSE::MUSIC::note n(30.f);
    YSE::MUSIC::note r = n * 2.f;
    CHECK(r.getPitch() == doctest::Approx(60.f));
  }

  TEST_CASE("note: operator/ (note, float) returns new note") {
    YSE::MUSIC::note n(60.f);
    YSE::MUSIC::note r = n / 2.f;
    CHECK(r.getPitch() == doctest::Approx(30.f));
  }

  TEST_CASE("note: operator+ (note, note) adds pitches") {
    YSE::MUSIC::note a(60.f), b(4.f);
    YSE::MUSIC::note r = a + b;
    CHECK(r.getPitch() == doctest::Approx(64.f));
  }

  TEST_CASE("note: operator- (note, note) subtracts pitches") {
    YSE::MUSIC::note a(67.f), b(7.f);
    YSE::MUSIC::note r = a - b;
    CHECK(r.getPitch() == doctest::Approx(60.f));
  }

  // ─── note: comparison operators (pitch-based) ─────────────────────────────────

  TEST_CASE("note: operator== on equal pitches returns true") {
    YSE::MUSIC::note a(60.f), b(60.f);
    CHECK(a == b);
  }

  TEST_CASE("note: operator== on different pitches returns false") {
    YSE::MUSIC::note a(60.f), b(61.f);
    CHECK(!(a == b));
  }

  TEST_CASE("note: operator!= on different pitches returns true") {
    YSE::MUSIC::note a(60.f), b(61.f);
    CHECK(a != b);
  }

  TEST_CASE("note: operator< and operator> compare pitch") {
    YSE::MUSIC::note low(60.f), high(61.f);
    CHECK(low < high);
    CHECK(high > low);
    CHECK(!(high < low));
    CHECK(!(low > high));
  }

  TEST_CASE("note: operator<= and operator>= with equal pitches") {
    YSE::MUSIC::note a(60.f), b(60.f);
    CHECK(a <= b);
    CHECK(a >= b);
  }

  TEST_CASE("note: comparison operators with float rhs") {
    YSE::MUSIC::note n(60.f);
    CHECK(n == 60.f);
    CHECK(n != 61.f);
    CHECK(n < 61.f);
    CHECK(n > 59.f);
    CHECK(n <= 60.f);
    CHECK(n >= 60.f);
  }

  // ─── note: operator= from pNote ──────────────────────────────────────────────

  TEST_CASE("note: operator= from pNote copies all note fields") {
    YSE::MUSIC::pNote pn(1.0f, 72.f, 0.5f, 2.0f, 2);
    YSE::MUSIC::note n;
    n = pn;
    CHECK(n.getPitch() == doctest::Approx(72.f));
    CHECK(n.getVolume() == doctest::Approx(0.5f));
    CHECK(n.getLength() == doctest::Approx(2.0f));
    CHECK(n.getChannel() == 2);
  }

  // ─── pNote: construction ──────────────────────────────────────────────────────

  TEST_CASE("pNote: full construction stores position and inherits note fields") {
    YSE::MUSIC::pNote pn(0.5f, 60.f, 1.f, 2.f, 1);
    CHECK(pn.getPosition() == doctest::Approx(0.5f));
    CHECK(pn.getPitch() == doctest::Approx(60.f));
    CHECK(pn.getVolume() == doctest::Approx(1.f));
    CHECK(pn.getLength() == doctest::Approx(2.f));
    CHECK(pn.getChannel() == 1);
  }

  TEST_CASE("pNote: construction from note copies all note fields") {
    YSE::MUSIC::note n(60.f, 0.8f, 1.5f, 2);
    YSE::MUSIC::pNote pn(n, 1.0f);
    CHECK(pn.getPosition() == doctest::Approx(1.0f));
    CHECK(pn.getPitch() == doctest::Approx(60.f));
    CHECK(pn.getVolume() == doctest::Approx(0.8f));
    CHECK(pn.getLength() == doctest::Approx(1.5f));
    CHECK(pn.getChannel() == 2);
  }

  TEST_CASE("pNote: construction from note defaults position to zero") {
    YSE::MUSIC::note n(60.f);
    YSE::MUSIC::pNote pn(n);
    CHECK(pn.getPosition() == doctest::Approx(0.f));
  }

  // ─── pNote: position getter/setter ───────────────────────────────────────────

  TEST_CASE("pNote: setPosition round-trip") {
    YSE::MUSIC::pNote pn(0.f, 60.f, 1.f, 0.f);
    pn.setPosition(3.75f);
    CHECK(pn.getPosition() == doctest::Approx(3.75f));
  }

  TEST_CASE("pNote: setPosition returns reference to self for chaining") {
    YSE::MUSIC::pNote pn(0.f, 60.f, 1.f, 0.f);
    YSE::MUSIC::pNote& ref = pn.setPosition(2.f);
    CHECK(&ref == &pn);
    CHECK(pn.getPosition() == doctest::Approx(2.f));
  }

  // ─── pNote: inherited arithmetic ─────────────────────────────────────────────

  TEST_CASE("pNote: pitch arithmetic inherited from note; position not changed") {
    YSE::MUSIC::pNote pn(0.5f, 60.f, 1.f, 0.f);
    pn += 7.f;
    CHECK(pn.getPitch() == doctest::Approx(67.f));
    CHECK(pn.getPosition() == doctest::Approx(0.5f));
  }

  TEST_CASE("pNote: comparison inherited from note compares pitch") {
    YSE::MUSIC::pNote a(0.f, 60.f, 1.f, 0.f);
    YSE::MUSIC::pNote b(1.f, 64.f, 1.f, 0.f);
    CHECK(a < b);
    CHECK(!(b < a));
  }

} // TEST_SUITE("music")

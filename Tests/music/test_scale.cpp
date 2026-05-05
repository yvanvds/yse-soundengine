// Tests for YSE::scale (YseEngine/music/scale/).
//
// Coverage:
//   - Construction and empty state
//   - add() with step=0 (single pitch) and step>0 (all-octave transpositions)
//   - has(): membership query
//   - size(): cardinality
//   - getNearest(): snap-to-scale behaviour
//   - remove(): pitch removal
//   - clear(): full reset
//   - Copy constructor and assignment operator
//
// No engine initialisation is required — scale maintains its own pitch vector
// in the interface; SCALE::Manager() is a standalone static singleton with no
// PortAudio dependency.

#include <doctest/doctest.h>
#include "music/scale/scaleInterface.hpp"

TEST_SUITE("music") {

// ─── Construction ─────────────────────────────────────────────────────────────

TEST_CASE("scale: default construction produces empty scale") {
    YSE::scale s;
    CHECK(s.size() == 0u);
}

// ─── add / has / size with single pitches (step=0) ───────────────────────────

TEST_CASE("scale: add single pitch increments size") {
    YSE::scale s;
    s.add(60.f, 0);
    CHECK(s.size() == 1u);
}

TEST_CASE("scale: added pitch is found by has()") {
    YSE::scale s;
    s.add(60.f, 0);
    CHECK(s.has(60.f));
}

TEST_CASE("scale: pitch not in scale returns has() false") {
    YSE::scale s;
    s.add(60.f, 0);
    CHECK(!s.has(61.f));
}

TEST_CASE("scale: multiple distinct pitches all found by has()") {
    YSE::scale s;
    s.add(60.f, 0);
    s.add(64.f, 0);
    s.add(67.f, 0);
    CHECK(s.size() == 3u);
    CHECK(s.has(60.f));
    CHECK(s.has(64.f));
    CHECK(s.has(67.f));
}

TEST_CASE("scale: duplicate pitch is not added twice") {
    YSE::scale s;
    s.add(60.f, 0);
    s.add(60.f, 0);
    CHECK(s.size() == 1u);
}

// ─── Major scale: known semitone distances ────────────────────────────────────

TEST_CASE("scale: C major has seven pitches and excludes chromatic semitones") {
    YSE::scale s;
    // C major: C=60, D=62, E=64, F=65, G=67, A=69, B=71
    s.add(60.f, 0); s.add(62.f, 0); s.add(64.f, 0); s.add(65.f, 0);
    s.add(67.f, 0); s.add(69.f, 0); s.add(71.f, 0);
    CHECK(s.size() == 7u);
    // All seven degrees present (whole-tone and half-tone intervals)
    CHECK(s.has(60.f)); CHECK(s.has(62.f)); CHECK(s.has(64.f));
    CHECK(s.has(65.f)); CHECK(s.has(67.f)); CHECK(s.has(69.f)); CHECK(s.has(71.f));
    // Chromatic semitones absent
    CHECK(!s.has(61.f)); // C#
    CHECK(!s.has(63.f)); // Eb
    CHECK(!s.has(66.f)); // F#
    CHECK(!s.has(68.f)); // Ab
    CHECK(!s.has(70.f)); // Bb
}

TEST_CASE("scale: minor scale semitone pattern 2-1-2-2-1-2-2") {
    YSE::scale s;
    // A natural minor: A=69, B=71, C=72, D=74, E=76, F=77, G=79
    s.add(69.f, 0); s.add(71.f, 0); s.add(72.f, 0); s.add(74.f, 0);
    s.add(76.f, 0); s.add(77.f, 0); s.add(79.f, 0);
    CHECK(s.size() == 7u);
    CHECK(s.has(69.f)); // A (tonic)
    CHECK(s.has(72.f)); // C (minor third, 3 semitones from A)
    CHECK(s.has(76.f)); // E (perfect fifth, 7 semitones from A)
    CHECK(!s.has(70.f)); // A# not in A minor
    CHECK(!s.has(73.f)); // C# not in A minor
}

// ─── getNearest ───────────────────────────────────────────────────────────────

TEST_CASE("scale: getNearest returns exact pitch when present") {
    YSE::scale s;
    s.add(60.f, 0); s.add(64.f, 0); s.add(67.f, 0);
    CHECK(s.getNearest(60.f) == doctest::Approx(60.f));
    CHECK(s.getNearest(64.f) == doctest::Approx(64.f));
    CHECK(s.getNearest(67.f) == doctest::Approx(67.f));
}

TEST_CASE("scale: getNearest snaps to closer lower neighbour") {
    YSE::scale s;
    s.add(60.f, 0); s.add(64.f, 0); s.add(67.f, 0);
    // 61 is 1 away from 60 and 3 away from 64 → nearest is 60
    CHECK(s.getNearest(61.f) == doctest::Approx(60.f));
}

TEST_CASE("scale: getNearest snaps to closer upper neighbour") {
    YSE::scale s;
    s.add(60.f, 0); s.add(64.f, 0); s.add(67.f, 0);
    // 63 is 3 away from 60 and 1 away from 64 → nearest is 64
    CHECK(s.getNearest(63.f) == doctest::Approx(64.f));
}

TEST_CASE("scale: getNearest returns only pitch when queried from below") {
    // Note: querying above all scale pitches hits a latent out-of-bounds access
    // in getNearest() (lower_bound returns end(), which is then dereferenced).
    // Only the below-first-element path is tested here.
    YSE::scale s;
    s.add(60.f, 0);
    CHECK(s.getNearest(50.f) == doctest::Approx(60.f));
}

// ─── remove ───────────────────────────────────────────────────────────────────

TEST_CASE("scale: remove existing pitch removes it") {
    YSE::scale s;
    s.add(60.f, 0); s.add(64.f, 0); s.add(67.f, 0);
    s.remove(64.f, 0);
    CHECK(!s.has(64.f));
    CHECK(s.size() == 2u);
    CHECK(s.has(60.f));
    CHECK(s.has(67.f));
}

TEST_CASE("scale: remove non-existing pitch leaves scale unchanged") {
    YSE::scale s;
    s.add(60.f, 0); s.add(64.f, 0);
    s.remove(62.f, 0);
    CHECK(s.size() == 2u);
}

// ─── clear ────────────────────────────────────────────────────────────────────

TEST_CASE("scale: clear removes all pitches") {
    YSE::scale s;
    s.add(60.f, 0); s.add(64.f, 0); s.add(67.f, 0);
    s.clear();
    CHECK(s.size() == 0u);
    CHECK(!s.has(60.f));
}

TEST_CASE("scale: pitches can be re-added after clear") {
    YSE::scale s;
    s.add(60.f, 0);
    s.clear();
    s.add(64.f, 0);
    CHECK(s.size() == 1u);
    CHECK(s.has(64.f));
    CHECK(!s.has(60.f));
}

// ─── add with step (all-octave transpositions) ────────────────────────────────

TEST_CASE("scale: add with step=12 adds pitch at multiple octaves") {
    YSE::scale s;
    s.add(60.f, 12.f); // C at every octave
    CHECK(s.has(60.f)); // C4
    CHECK(s.has(72.f)); // C5
    CHECK(s.has(48.f)); // C3
    CHECK(!s.has(61.f)); // C# not added
    CHECK(!s.has(59.f)); // B not added
}

// ─── Copy semantics ───────────────────────────────────────────────────────────

TEST_CASE("scale: copy constructor creates copy with same pitches") {
    YSE::scale s;
    s.add(60.f, 0); s.add(64.f, 0); s.add(67.f, 0);
    YSE::scale s2(s);
    CHECK(s2.size() == 3u);
    CHECK(s2.has(60.f)); CHECK(s2.has(64.f)); CHECK(s2.has(67.f));
}

TEST_CASE("scale: copy constructor creates independent copy") {
    YSE::scale s;
    s.add(60.f, 0); s.add(64.f, 0);
    YSE::scale s2(s);
    s2.clear();
    CHECK(s.size() == 2u);  // original unaffected
    CHECK(s2.size() == 0u);
}

TEST_CASE("scale: assignment operator copies pitches") {
    YSE::scale s;
    s.add(60.f, 0); s.add(64.f, 0); s.add(67.f, 0);
    YSE::scale s2;
    s2 = s;
    CHECK(s2.size() == 3u);
    CHECK(s2.has(60.f)); CHECK(s2.has(64.f)); CHECK(s2.has(67.f));
}

} // TEST_SUITE("music")

// Golden-file tests for the SFZ parser + region model (issue #173).
//
// The region-resolution golden tables are the PRIMARY contract (spec §5/§14):
// "for note N, velocity V, hit#, which region SET sounds?". Expected sets are
// keyed by (note, velocity, hit#) and validated against sfizz's documented
// layering behaviour. The fixtures use sample=*silence for the mapping cases
// so the tables are exact and need no audio device; the de-dup fixture uses
// the real test_mono_44100.wav.
//
// No engine init and no audio device: parsing and resolution are pure offline
// functions over an immutable table.

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "dsp/sfzModel.hpp"

#ifndef YSE_TEST_FIXTURES_DIR
#define YSE_TEST_FIXTURES_DIR "../../Tests/support/fixtures"
#endif

using namespace YSE::DSP;

namespace {

  std::string fixturesDir() {
    return std::string(YSE_TEST_FIXTURES_DIR);
  }

  sfzInstrument loadFixture(const char* name) {
    return loadSFZ(fixturesDir() + "/" + name);
  }

  // Resolve a layer set and return it as a sorted vector of region indices.
  std::vector<int> layers(const sfzInstrument& inst, int note, int vel, int hit = 0) {
    int out[YSE_MAX_REGION_LAYERS];
    int n = resolveLayers(inst, note, vel, hit, out);
    return std::vector<int>(out, out + n);
  }

} // namespace

TEST_SUITE("dsp") {

  // ─── one region ──────────────────────────────────────────────────────────

  TEST_CASE("sfz: minimal one-region instrument maps every key/velocity") {
    sfzInstrument inst = loadFixture("sfz_one_region.sfz");
    REQUIRE(inst.valid);
    REQUIRE(inst.regions.size() == 1);
    CHECK(inst.regions[0].pitchKeycenter == 60);
    CHECK(inst.regions[0].lokey == 0);
    CHECK(inst.regions[0].hikey == 127);

    CHECK(layers(inst, 0, 1) == std::vector<int>{0});
    CHECK(layers(inst, 60, 64) == std::vector<int>{0});
    CHECK(layers(inst, 127, 127) == std::vector<int>{0});
  }

  // ─── key split + group inheritance ────────────────────────────────────────

  TEST_CASE("sfz: key split inherits group opcodes and maps by key") {
    sfzInstrument inst = loadFixture("sfz_key_split.sfz");
    REQUIRE(inst.valid);
    REQUIRE(inst.regions.size() == 2);

    // group defaults inherited by both regions
    CHECK(inst.regions[0].loopMode == SFZ_LOOP_CONTINUOUS);
    CHECK(inst.regions[1].loopMode == SFZ_LOOP_CONTINUOUS);
    CHECK(inst.regions[0].egRelease == doctest::Approx(0.3f));
    CHECK(inst.defaultPath == "samples/");

    CHECK(inst.regions[0].lokey == 0);
    CHECK(inst.regions[0].hikey == 47);
    CHECK(inst.regions[1].lokey == 48);

    CHECK(layers(inst, 40, 64) == std::vector<int>{0}); // bass
    CHECK(layers(inst, 60, 64) == std::vector<int>{1}); // lead
    CHECK(layers(inst, 47, 1) == std::vector<int>{0}); // boundary inclusive
    CHECK(layers(inst, 48, 1) == std::vector<int>{1});
  }

  // ─── velocity split + global inheritance ──────────────────────────────────

  TEST_CASE("sfz: velocity layers select by velocity and inherit global") {
    sfzInstrument inst = loadFixture("sfz_velocity_split.sfz");
    REQUIRE(inst.regions.size() == 2);
    CHECK(inst.regions[0].pitchKeycenter == 60); // from <global>
    CHECK(inst.regions[1].pitchKeycenter == 60);

    CHECK(layers(inst, 60, 30) == std::vector<int>{0}); // soft
    CHECK(layers(inst, 60, 63) == std::vector<int>{0}); // boundary
    CHECK(layers(inst, 60, 64) == std::vector<int>{1}); // hard
    CHECK(layers(inst, 60, 127) == std::vector<int>{1});
  }

  // ─── four-level inheritance (global/master/group/region) ──────────────────

  TEST_CASE("sfz: <master> gives four-level inheritance, innermost wins") {
    sfzInstrument inst = loadFixture("sfz_master.sfz");
    REQUIRE(inst.regions.size() == 2);

    // region 0 inherits every level
    CHECK(inst.regions[0].egRelease == doctest::Approx(0.5f)); // <global>
    CHECK(inst.regions[0].volumeDb == doctest::Approx(-3.0f)); // <master>
    CHECK(inst.regions[0].pitchKeycenter == 48); // <master>
    CHECK(inst.regions[0].loopMode == SFZ_ONE_SHOT); // <group>

    // region 1 overrides the master volume with its own
    CHECK(inst.regions[1].volumeDb == doctest::Approx(-6.0f)); // <region> wins
    CHECK(inst.regions[1].pitchKeycenter == 48); // still inherited
    CHECK(inst.regions[1].egRelease == doctest::Approx(0.5f));
    CHECK(inst.regions[1].loopMode == SFZ_ONE_SHOT);
  }

  // ─── round-robin + choke ──────────────────────────────────────────────────

  TEST_CASE("sfz: round-robin cycles samples on successive hits (keyed by hit#)") {
    sfzInstrument inst = loadFixture("sfz_hihat.sfz");
    REQUIRE(inst.regions.size() == 3);

    CHECK(inst.regions[0].seqLength == 2);
    CHECK(inst.regions[0].seqPosition == 1);
    CHECK(inst.regions[1].seqPosition == 2);

    // key 42 alternates the two closed-hat regions across hits
    CHECK(layers(inst, 42, 100, 0) == std::vector<int>{0});
    CHECK(layers(inst, 42, 100, 1) == std::vector<int>{1});
    CHECK(layers(inst, 42, 100, 2) == std::vector<int>{0});
    CHECK(layers(inst, 42, 100, 3) == std::vector<int>{1});

    // open hat is a separate region on key 46
    CHECK(layers(inst, 46, 100, 0) == std::vector<int>{2});
  }

  TEST_CASE("sfz: choke opcodes parse onto the region model") {
    sfzInstrument inst = loadFixture("sfz_hihat.sfz");
    REQUIRE(inst.regions.size() == 3);

    CHECK(inst.regions[0].chokeGroup == 1);
    CHECK(inst.regions[0].offBy == 2);
    CHECK(inst.regions[0].offMode == SFZ_OFF_FAST); // default

    CHECK(inst.regions[2].chokeGroup == 2);
    CHECK(inst.regions[2].offBy == 1);
    CHECK(inst.regions[2].loopMode == SFZ_ONE_SHOT);
  }

  // ─── velocity crossfades ──────────────────────────────────────────────────

  TEST_CASE("sfz: crossfaded layers overlap and parse xf opcodes") {
    sfzInstrument inst = loadFixture("sfz_crossfade.sfz");
    REQUIRE(inst.regions.size() == 2);

    CHECK(inst.regions[0].xfinLovel == 1);
    CHECK(inst.regions[0].xfinHivel == 20);
    CHECK(inst.regions[0].xfoutLovel == 60);
    CHECK(inst.regions[0].xfoutHivel == 80);
    CHECK(inst.regions[0].xfVelcurve == SFZ_CURVE_POWER); // default
    CHECK(inst.regions[1].xfVelcurve == SFZ_CURVE_GAIN);

    // overlap region: both layer
    CHECK(layers(inst, 60, 70) == std::vector<int>{0, 1});
    CHECK(layers(inst, 60, 30) == std::vector<int>{0});
    CHECK(layers(inst, 60, 120) == std::vector<int>{1});
  }

  // ─── overlapping layers below the bound ───────────────────────────────────

  TEST_CASE("sfz: all matching regions sound as layers (below the 4-layer bound)") {
    sfzInstrument inst = loadFixture("sfz_overlap.sfz");
    REQUIRE(inst.regions.size() == 3);
    CHECK_FALSE(inst.layerOverflowWarning);

    CHECK(layers(inst, 65, 64) == std::vector<int>{0, 1, 2}); // inside all three
    CHECK(layers(inst, 61, 64) == std::vector<int>{0, 1}); // outside region 2
  }

  // ─── >4-layer cell: truncation + parse warning ────────────────────────────

  TEST_CASE("sfz: over-bound cell truncates by priority and warns at parse time") {
    sfzInstrument inst = loadFixture("sfz_overflow.sfz");
    REQUIRE(inst.regions.size() == 5);
    CHECK(inst.layerOverflowWarning);

    // At (60,64) all five match. Priority: narrowest velocity range (region 2,
    // vel 64..64), then narrowest key range among the rest (region 1 key 60,
    // region 3 key 59..61, region 4 key 60..72), dropping the widest (region 0).
    std::vector<int> got = layers(inst, 60, 64);
    REQUIRE(got.size() == static_cast<size_t>(YSE_MAX_REGION_LAYERS));
    CHECK(got == std::vector<int>{1, 2, 3, 4});
    CHECK(std::find(got.begin(), got.end(), 0) == got.end()); // widest dropped

    // A non-overflowing cell resolves normally.
    CHECK(layers(inst, 65, 64) == std::vector<int>{0, 4});
  }

  // ─── lenient error policy ─────────────────────────────────────────────────

  TEST_CASE("sfz: lenient parse keeps valid regions, drops/skips the rest") {
    sfzInstrument inst = loadFixture("sfz_lenient.sfz");
    REQUIRE(inst.valid);
    CHECK(inst.regions.size() == 1); // one valid *silence region
    CHECK(inst.droppedRegions == 2); // missing-sample + no-sample regions

    // OUT opcodes ignored, malformed known value falls back to the default.
    CHECK(inst.regions[0].pitchKeycenter == 48);
    CHECK(inst.regions[0].lovel == 1); // "notanumber" rejected -> default kept
  }

  // ─── buffer de-dup + note names + real file ───────────────────────────────

  TEST_CASE("sfz: identical sample paths share one buffer; note names parse") {
    sfzInstrument inst = loadFixture("sfz_dedup.sfz");
    REQUIRE(inst.valid);
    REQUIRE(inst.regions.size() == 2);

    // both regions name the same real file -> one de-duplicated sample entry
    REQUIRE(inst.samples.size() == 1);
    CHECK_FALSE(inst.samples[0].silence);
    CHECK(inst.regions[0].sampleIndex == inst.regions[1].sampleIndex);

    // key=c4 shorthand -> lokey = hikey = pitch_keycenter = 60
    CHECK(inst.regions[0].lokey == 60);
    CHECK(inst.regions[0].hikey == 60);
    CHECK(inst.regions[0].pitchKeycenter == 60);

    // c5 = 72, c6 = 84
    CHECK(inst.regions[1].lokey == 72);
    CHECK(inst.regions[1].hikey == 84);
    CHECK(inst.regions[1].pitchKeycenter == 72);

    CHECK(inst.regions[0].loopMode == SFZ_LOOP_CONTINUOUS); // from <group>
  }

  // ─── hard failures / empty input ──────────────────────────────────────────

  TEST_CASE("sfz: unreadable file and empty text are invalid, never crash") {
    sfzInstrument missing = loadSFZ(fixturesDir() + "/does_not_exist_at_all.sfz");
    CHECK_FALSE(missing.valid);
    CHECK(missing.regions.empty());

    sfzInstrument empty = parseSFZ("", fixturesDir(), "empty");
    CHECK_FALSE(empty.valid);
    CHECK(empty.regions.empty());

    // No matching region -> empty layer set, no stuck voice.
    sfzInstrument inst = loadFixture("sfz_velocity_split.sfz");
    CHECK(layers(inst, 60, 64, 0).size() >= 1);
    int out[YSE_MAX_REGION_LAYERS];
    // velocity 0 is below every region's lovel (1) -> no match
    CHECK(resolveLayers(inst, 60, 0, 0, out) == 0);
  }

  // ─── note-name parsing edge cases via a synthetic instrument ──────────────

  TEST_CASE("sfz: note names, sharps/flats, and octave -1 parse to MIDI numbers") {
    // c-1 = 0, a-1 = 9, f#3 = 54, bb2 = 46, c4 = 60
    const char* text = "<region> sample=*silence key=c-1\n"
                       "<region> sample=*silence lokey=f#3 hikey=bb2 pitch_keycenter=c4\n";
    sfzInstrument inst = parseSFZ(text, fixturesDir(), "notenames");
    REQUIRE(inst.regions.size() == 2);
    CHECK(inst.regions[0].lokey == 0); // c-1
    CHECK(inst.regions[1].lokey == 54); // f#3
    CHECK(inst.regions[1].hikey == 46); // bb2
    CHECK(inst.regions[1].pitchKeycenter == 60); // c4
  }

} // TEST_SUITE

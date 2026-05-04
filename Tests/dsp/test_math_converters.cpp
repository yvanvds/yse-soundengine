// Tests for YSE::DSP::MidiToFreq and FreqToMidi (YseEngine/dsp/math.hpp).
// Both are pure closed-form computations; no audio device or system state needed.

#include <doctest/doctest.h>
#include <cmath>
#include "dsp/math.hpp"

TEST_SUITE("dsp") {

TEST_CASE("MidiToFreq: A4 (MIDI 69) = 440 Hz") {
    CHECK(YSE::DSP::MidiToFreq(69.0f) == doctest::Approx(440.0f).epsilon(1e-5f));
}

TEST_CASE("MidiToFreq: octave relationships") {
    // Each octave doubles the frequency.
    CHECK(YSE::DSP::MidiToFreq(57.0f) == doctest::Approx(220.0f).epsilon(1e-5f));  // A3
    CHECK(YSE::DSP::MidiToFreq(81.0f) == doctest::Approx(880.0f).epsilon(1e-5f));  // A5
}

TEST_CASE("MidiToFreq: all standard MIDI notes produce positive frequencies") {
    CHECK(YSE::DSP::MidiToFreq(0.0f) > 0.0f);    // lowest MIDI note
    CHECK(YSE::DSP::MidiToFreq(127.0f) > 0.0f);  // highest MIDI note
}

TEST_CASE("MidiToFreq: monotonically increasing") {
    for (float note = 0.0f; note < 127.0f; note += 1.0f) {
        CHECK(YSE::DSP::MidiToFreq(note + 1.0f) > YSE::DSP::MidiToFreq(note));
    }
}

TEST_CASE("FreqToMidi: 440 Hz = MIDI 69") {
    CHECK(YSE::DSP::FreqToMidi(440.0f) == doctest::Approx(69.0f).epsilon(1e-5f));
}

TEST_CASE("FreqToMidi: octave relationships") {
    CHECK(YSE::DSP::FreqToMidi(220.0f) == doctest::Approx(57.0f).epsilon(1e-5f));
    CHECK(YSE::DSP::FreqToMidi(880.0f) == doctest::Approx(81.0f).epsilon(1e-5f));
}

TEST_CASE("MidiToFreq / FreqToMidi round-trip") {
    // FreqToMidi(MidiToFreq(n)) should recover n to floating-point precision.
    const float notes[] = {36.0f, 48.0f, 60.0f, 69.0f, 72.0f, 84.0f, 96.0f};
    for (float note : notes) {
        CHECK(YSE::DSP::FreqToMidi(YSE::DSP::MidiToFreq(note)) ==
              doctest::Approx(note).epsilon(1e-5f));
    }
}

TEST_CASE("MidiToFreq: semitone step is 2^(1/12)") {
    const float ratio = 1.05946309436f;  // 2^(1/12)
    for (float note = 21.0f; note < 108.0f; note += 1.0f) {
        CHECK(YSE::DSP::MidiToFreq(note + 1.0f) / YSE::DSP::MidiToFreq(note) ==
              doctest::Approx(ratio).epsilon(1e-5f));
    }
}

TEST_CASE("MidiToFreq: 12-semitone span doubles frequency") {
    CHECK(YSE::DSP::MidiToFreq(81.0f) / YSE::DSP::MidiToFreq(69.0f) ==
          doctest::Approx(2.0f).epsilon(1e-5f));
}

TEST_CASE("MidiToFreq: known chromatic neighbours of A4") {
    CHECK(YSE::DSP::MidiToFreq(70.0f) == doctest::Approx(466.164f).epsilon(0.001f));  // A#4/Bb4
    CHECK(YSE::DSP::MidiToFreq(71.0f) == doctest::Approx(493.883f).epsilon(0.001f));  // B4
    CHECK(YSE::DSP::MidiToFreq(72.0f) == doctest::Approx(523.251f).epsilon(0.001f));  // C5
}

TEST_CASE("MidiToFreq: MIDI boundary values produce positive frequencies") {
    CHECK(YSE::DSP::MidiToFreq(0.0f)   > 0.0f);
    CHECK(YSE::DSP::MidiToFreq(127.0f) > 0.0f);
}

TEST_CASE("MidiToFreq: fractional note is 2^(frac/12) times lower integer note") {
    // MIDI 69.5 should equal 440 * 2^(0.5/12)
    const float expected = 440.0f * std::pow(2.0f, 0.5f / 12.0f);
    CHECK(YSE::DSP::MidiToFreq(69.5f) == doctest::Approx(expected).epsilon(1e-5f));
}

TEST_CASE("FreqToMidi: MIDI boundary values round-trip") {
    CHECK(YSE::DSP::FreqToMidi(YSE::DSP::MidiToFreq(0.0f)) ==
          doctest::Approx(0.0f).epsilon(1e-4f));
    CHECK(YSE::DSP::FreqToMidi(YSE::DSP::MidiToFreq(127.0f)) ==
          doctest::Approx(127.0f).epsilon(1e-4f));
}

} // TEST_SUITE("dsp")

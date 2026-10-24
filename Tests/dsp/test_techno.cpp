// Tests for YSE::DSP::techno (YseEngine/dsp/generators/techno.hpp), issue
// #1068. Reference output is closed form: at a phase in step k, with f how far
// through the step it is, the pitch is pitch[k-1] + (pitch[k] - pitch[k-1]) *
// f^curve[k], the envelope amplitude[k] * (1 - f)^decay[k] + amplitude[k+1] *
// f^attack[k+1], and the position k + f.

#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/generators/techno.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::techno;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  struct sample {
    float pitch;
    float envelope;
    float position;
  };

  // The outputs at one phase, run as a whole constant block.
  sample At(techno& seq, float phase) {
    buffer in(N);
    in = phase;
    buffer& pitch = seq(in);
    return {pitch.getPtr()[N - 1], seq.envelope().getPtr()[N - 1],
            seq.stepPosition().getPtr()[N - 1]};
  }

  // Four evenly spaced steps with pitches 100, 200, 300, 400.
  techno Four() {
    techno seq;
    seq.length(4);
    for (unsigned int k = 0; k < 4; ++k)
      seq.pitch(k, 100.f * static_cast<float>(k + 1));
    return seq;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("techno: a new sequencer is one step of 440 Hz, envelope 1 (#1068)") {
    techno seq;
    CHECK(seq.capacity() == techno::DEFAULT_STEPS);
    CHECK(seq.length() == 1);
    CHECK(seq.pitch(0) == 440.f);
    CHECK(seq.amplitude(0) == 1.f);
    CHECK(seq.curve(0) == 0.f);
    CHECK(seq.attack(0) == 1.f);
    CHECK(seq.decay(0) == 1.f);
    CHECK(seq.position(0) == 0.f);

    buffer ramp(N);
    for (unsigned int i = 0; i < N; ++i)
      ramp.getPtr()[i] = static_cast<float>(i) / static_cast<float>(N);
    buffer& pitch = seq(ramp);
    REQUIRE(pitch.getLength() == N);
    for (unsigned int i = 0; i < N; ++i) {
      CAPTURE(i);
      CHECK(pitch.getPtr()[i] == 440.f);
      // A linear decay plus the same note's linear attack: 1 throughout.
      CHECK(seq.envelope().getPtr()[i] == doctest::Approx(1.f));
      CHECK(seq.stepPosition().getPtr()[i] == doctest::Approx(ramp.getPtr()[i]));
    }
  }

  TEST_CASE("techno: the capacity is clamped, and so is the length (#1068)") {
    CHECK(techno(0).capacity() == 1);
    CHECK(techno(5000).capacity() == techno::MAX_STEPS);
    techno seq(8);
    seq.length(20);
    CHECK(seq.length() == 8);
    seq.length(0);
    CHECK(seq.length() == 1);
  }

  TEST_CASE("techno: evenly spaced steps, pitch without a slide, the position (#1068)") {
    techno seq = Four();
    for (unsigned int k = 0; k < 4; ++k)
      CHECK(seq.position(k) == doctest::Approx(0.25f * static_cast<float>(k)));
    for (float phase : {0.f, 0.1f, 0.25f, 0.4f, 0.5f, 0.74f, 0.75f, 0.99f}) {
      CAPTURE(phase);
      const sample s = At(seq, phase);
      const float steps = phase * 4.f;
      const unsigned int k = static_cast<unsigned int>(steps);
      CHECK(s.pitch == 100.f * static_cast<float>(k + 1));
      CHECK(s.position == doctest::Approx(steps));
    }
    // The end of the phase is the end of the last step.
    CHECK(At(seq, 1.f).position == doctest::Approx(4.f));
  }

  TEST_CASE("techno: curve shapes the slide from the previous step's pitch (#1068)") {
    techno seq = Four();
    // Halfway through step 1: f = 0.5.
    seq.curve(1, 1.f);
    CHECK(At(seq, 0.375f).pitch == doctest::Approx(150.f));
    seq.curve(1, 2.f);
    CHECK(At(seq, 0.375f).pitch == doctest::Approx(125.f));
    seq.curve(1, 0.5f);
    CHECK(At(seq, 0.375f).pitch == doctest::Approx(100.f + 100.f * std::sqrt(0.5f)));
    // At the start of the step the slide has not begun.
    CHECK(At(seq, 0.25f).pitch == doctest::Approx(100.f));
    // Step 0 slides from the last step, around the wrap.
    seq.curve(0, 1.f);
    CHECK(At(seq, 0.f).pitch == doctest::Approx(400.f));
    CHECK(At(seq, 0.125f).pitch == doctest::Approx(250.f));
    // Exponents below 0 count as 0: no slide.
    seq.curve(1, -3.f);
    CHECK(seq.curve(1) == 0.f);
    CHECK(At(seq, 0.375f).pitch == 200.f);
  }

  TEST_CASE("techno: the envelope is a decay plus the next note's attack (#1068)") {
    techno seq = Four();
    seq.amplitude(0, 1.f).amplitude(1, 0.f).amplitude(2, 0.5f).amplitude(3, 0.f);
    seq.decay(0, 2.f).attack(2, 3.f).decay(2, 0.5f);
    for (float f : {0.f, 0.2f, 0.5f, 0.9f}) {
      CAPTURE(f);
      // Step 0: its note decays (exponent 2), step 1's is silent.
      CHECK(At(seq, 0.25f * f).envelope == doctest::Approx(std::pow(1.f - f, 2.f)));
      // Step 1: silent, and step 2's note rises (exponent 3).
      CHECK(At(seq, 0.25f + 0.25f * f).envelope == doctest::Approx(0.5f * std::pow(f, 3.f)));
      // Step 2: its note decays (square root), step 3's is silent.
      CHECK(At(seq, 0.5f + 0.25f * f).envelope == doctest::Approx(0.5f * std::pow(1.f - f, 0.5f)));
      // Step 3: silent, and step 0's note rises linearly, around the wrap.
      CHECK(At(seq, 0.75f + 0.25f * f).envelope == doctest::Approx(f));
    }
    // A note is at its amplitude where its step starts.
    CHECK(At(seq, 0.5f).envelope == doctest::Approx(0.5f));
    // Exponent 0: a decay that never decays.
    seq.decay(0, 0.f);
    CHECK(At(seq, 0.2f).envelope == doctest::Approx(1.f));
  }

  TEST_CASE("techno: pos moves a step between its neighbours (#1068)") {
    techno seq = Four();
    seq.position(2, 0.6f);
    CHECK(seq.position(2) == doctest::Approx(0.6f));
    // Step 1 now runs 0.25 to 0.6.
    sample s = At(seq, 0.55f);
    CHECK(s.pitch == 200.f);
    CHECK(s.position == doctest::Approx(1.f + 0.3f / 0.35f));
    s = At(seq, 0.65f);
    CHECK(s.pitch == 300.f);
    CHECK(s.position == doctest::Approx(2.f + 0.05f / 0.15f));
    // Not before the previous step, not after the next.
    seq.position(2, 0.1f);
    CHECK(seq.position(2) == doctest::Approx(0.25f));
    seq.position(2, 0.9f);
    CHECK(seq.position(2) == doctest::Approx(0.75f));
    // Not a number, or a step beyond the length: ignored.
    seq.position(2, std::numeric_limits<float>::quiet_NaN());
    CHECK(seq.position(2) == doctest::Approx(0.75f));
    seq.position(7, 0.9f);
    CHECK(seq.position(7) == 0.f);
    // The last step is bounded by 1.
    seq.position(3, 2.f);
    CHECK(seq.position(3) == 1.f);
    // A changed length spaces the steps evenly again.
    seq.length(4);
    CHECK(seq.position(2) == doctest::Approx(0.5f));
  }

  TEST_CASE("techno: step 0 moved off 0 leaves the last step running across the wrap (#1068)") {
    techno seq = Four();
    seq.position(0, 0.1f);
    // The last step runs from 0.75 to 1.1.
    sample s = At(seq, 0.05f);
    CHECK(s.pitch == 400.f);
    CHECK(s.position == doctest::Approx(3.f + 0.3f / 0.35f));
    s = At(seq, 0.9f);
    CHECK(s.position == doctest::Approx(3.f + 0.15f / 0.35f));
    s = At(seq, 0.1f);
    CHECK(s.pitch == 100.f);
    CHECK(s.position == doctest::Approx(0.f));
  }

  TEST_CASE("techno: repeatPositions spaces every segment alike (#1068)") {
    techno seq;
    seq.length(8);
    const float swing[] = {0.66f};
    REQUIRE(seq.repeatPositions(swing, 1));
    for (unsigned int s = 0; s < 4; ++s) {
      CAPTURE(s);
      CHECK(seq.position(2 * s) == doctest::Approx(0.25f * static_cast<float>(s)));
      CHECK(seq.position(2 * s + 1) == doctest::Approx(0.25f * (static_cast<float>(s) + 0.66f)));
    }
    const float quarters[] = {0.25f, 0.5f, 0.75f};
    REQUIRE(seq.repeatPositions(quarters, 3));
    for (unsigned int k = 0; k < 8; ++k)
      CHECK(seq.position(k) == doctest::Approx(0.125f * static_cast<float>(k)));

    // Refused: a segment of 3 does not divide 8; out of order; out of range.
    REQUIRE(seq.repeatPositions(swing, 1));
    const float two[] = {0.3f, 0.6f};
    CHECK_FALSE(seq.repeatPositions(two, 2));
    const float backwards[] = {0.5f, 0.25f, 0.75f};
    CHECK_FALSE(seq.repeatPositions(backwards, 3));
    const float outside[] = {1.f};
    CHECK_FALSE(seq.repeatPositions(outside, 1));
    const float zero[] = {0.f};
    CHECK_FALSE(seq.repeatPositions(zero, 1));
    CHECK(seq.position(1) == doctest::Approx(0.25f * 0.66f));
    // No values: evenly spaced.
    CHECK(seq.repeatPositions(nullptr, 0));
    CHECK(seq.position(1) == doctest::Approx(0.125f));
  }

  TEST_CASE("techno: the phase is clamped to 0-1, NaN counts as 0 (#1068)") {
    techno seq = Four();
    CHECK(At(seq, -2.f).position == 0.f);
    CHECK(At(seq, 3.f).position == doctest::Approx(4.f));
    CHECK(At(seq, std::numeric_limits<float>::quiet_NaN()).position == 0.f);
    CHECK(At(seq, std::numeric_limits<float>::infinity()).position == doctest::Approx(4.f));
  }

  TEST_CASE("techno: values that are not finite are ignored; steps beyond the length are kept "
            "(#1068)") {
    techno seq(8);
    seq.pitch(0, std::numeric_limits<float>::infinity());
    seq.amplitude(0, std::numeric_limits<float>::quiet_NaN());
    seq.attack(0, std::numeric_limits<float>::quiet_NaN());
    CHECK(seq.pitch(0) == 440.f);
    CHECK(seq.amplitude(0) == 1.f);
    CHECK(seq.attack(0) == 1.f);
    // Beyond the capacity: ignored.
    seq.pitch(8, 1.f);
    CHECK(seq.pitch(8) == 0.f);
    // Beyond the length, within the capacity: kept for a longer sequence.
    seq.pitch(5, 55.f);
    CHECK(seq.pitch(5) == 55.f);
    seq.length(8);
    CHECK(At(seq, 5.5f / 8.f).pitch == 55.f);
  }

  TEST_CASE("techno: a block follows the phase sample by sample, at most STANDARD_BUFFERSIZE "
            "(#1068)") {
    techno seq = Four();
    buffer in(N);
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = static_cast<float>(i) / static_cast<float>(N);
    buffer& pitch = seq(in);
    for (unsigned int i = 0; i < N; ++i) {
      CAPTURE(i);
      const unsigned int k = (4 * i) / N;
      CHECK(pitch.getPtr()[i] == 100.f * static_cast<float>(k + 1));
    }
    buffer longer(N * 2);
    longer = 0.5f;
    CHECK(seq(longer).getLength() == N);
    buffer shorter(16);
    shorter = 0.5f;
    CHECK(seq(shorter).getLength() == 16);
    CHECK(seq.envelope().getLength() == 16);
    CHECK(seq.stepPosition().getLength() == 16);
  }

  TEST_CASE("techno: the same output at any sample rate (#1068)") {
    std::vector<float> reference;
    for (unsigned int rate : {22050u, 44100u, 96000u}) {
      CAPTURE(rate);
      TestHelpers::ScopedSampleRate scoped(rate);
      techno seq = Four();
      seq.curve(2, 1.5f).decay(1, 3.f);
      buffer in(N);
      for (unsigned int i = 0; i < N; ++i)
        in.getPtr()[i] = static_cast<float>(i) / static_cast<float>(N);
      buffer& out = seq(in);
      if (reference.empty()) {
        reference.assign(out.getPtr(), out.getPtr() + out.getLength());
      } else {
        for (unsigned int i = 0; i < N; ++i)
          REQUIRE(out.getPtr()[i] == reference[i]);
      }
    }
  }

  TEST_CASE("techno: setters and processing allocate nothing (#1068)") {
    techno seq(32);
    buffer in(N);
    in = 0.3f;
    seq(in);
    const float swing[] = {0.6f};
    {
      TestHelpers::ProbeScope probe;
      seq.length(16);
      seq.pitch(3, 220.f).amplitude(3, 0.5f).curve(3, 2.f).attack(3, 4.f).decay(3, 0.5f);
      seq.position(3, 0.2f);
      seq.repeatPositions(swing, 1);
      seq(in);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")

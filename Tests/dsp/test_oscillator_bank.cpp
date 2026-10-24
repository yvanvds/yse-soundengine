// Tests for YSE::DSP::oscillatorBank (YseEngine/dsp/generators/
// oscillatorBank.hpp), issue #1061. Reference output is closed form: the sum
// of magnitude * sin(2 pi phase), with the phase read at the table point
// below it (non-interpolating) or between points (interpolating), and
// straight-line ramps for smoothing.

#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/generators/oscillatorBank.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::oscillatorBank;

namespace {
  constexpr double TWO_PI = 6.283185307179586476925286766559;

  // The non-interpolating reading of an N-point sine table at `phase`.
  double Stepped(double phase, unsigned int points) {
    const double i = std::floor(phase * points);
    return std::sin(TWO_PI * i / points);
  }

  // Phase of an oscillator at `frequency` after `n` samples, from 0.
  double PhaseAt(double frequency, unsigned int n, double rate) {
    const double p = frequency * n / rate;
    return p - std::floor(p);
  }

  void Fill(buffer& b, float value) {
    for (unsigned int i = 0; i < b.getLength(); ++i)
      b.getPtr()[i] = value;
  }
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("oscillatorBank: construction and defaults (#1061)") {
    oscillatorBank bank;
    CHECK(bank.capacity() == 64);
    CHECK(bank.size() == 64);
    CHECK(bank.frequencySmoothing() == 1);
    CHECK(bank.magnitudeSmoothing() == 0);
    CHECK(bank.tableSize() == 4096);
    CHECK_FALSE(bank.interpolating());
    CHECK_FALSE(bank.frameSync());
    CHECK_FALSE(bank.customTable());
    // Everything at 0: silence.
    buffer& out = bank();
    CHECK(out.getLength() == YSE::STANDARD_BUFFERSIZE);
    CHECK(TestHelpers::measureRms(out) == 0.f);

    oscillatorBank none(0, true);
    CHECK(none.capacity() == 1);
    CHECK(none.interpolating());
  }

  TEST_CASE("oscillatorBank: one oscillator reads the table below the phase (#1061)") {
    TestHelpers::ScopedSampleRate rate(44100);
    oscillatorBank bank(1);
    bank.set(0, 1000.f, 0.5f);
    std::vector<float> got;
    for (int block = 0; block < 3; ++block) {
      buffer& out = bank();
      for (unsigned int i = 0; i < out.getLength(); ++i)
        got.push_back(out.getPtr()[i]);
    }
    for (unsigned int n = 0; n < got.size(); ++n) {
      CAPTURE(n);
      REQUIRE(got[n] == doctest::Approx(0.5 * Stepped(PhaseAt(1000.0, n, 44100.0), 4096))
                            .epsilon(1e-5)
                            .scale(1.0));
    }
  }

  TEST_CASE("oscillatorBank: the interpolating bank is a clean sine (#1061)") {
    TestHelpers::ScopedSampleRate rate(48000);
    oscillatorBank bank(1, true);
    bank.set(0, 440.f, 1.f);
    unsigned int n = 0;
    double worst = 0.0;
    for (int block = 0; block < 4; ++block) {
      buffer& out = bank();
      for (unsigned int i = 0; i < out.getLength(); ++i, ++n) {
        const double expected = std::sin(TWO_PI * PhaseAt(440.0, n, 48000.0));
        worst = std::fmax(worst, std::fabs(out.getPtr()[i] - expected));
      }
    }
    // Linear interpolation of a 4096-point sine: (2 pi / 4096)^2 / 8 ~ 3e-7,
    // plus float rounding.
    CHECK(worst < 2e-6);

    // The non-interpolating bank is a step coarser.
    oscillatorBank stepped(1, false);
    stepped.set(0, 440.f, 1.f);
    n = 0;
    double steppedWorst = 0.0;
    buffer& out = stepped();
    for (unsigned int i = 0; i < out.getLength(); ++i, ++n)
      steppedWorst = std::fmax(
          steppedWorst, std::fabs(out.getPtr()[i] - std::sin(TWO_PI * PhaseAt(440.0, n, 48000.0))));
    CHECK(steppedWorst > worst);
    CHECK(steppedWorst < TWO_PI / 4096 + 1e-6);
  }

  TEST_CASE("oscillatorBank: the output is the sum of the oscillators (#1061)") {
    TestHelpers::ScopedSampleRate rate(44100);
    oscillatorBank bank(4, true);
    const float freqs[] = {110.f, 220.f, 330.f, 440.f};
    const float mags[] = {0.5f, 0.25f, 0.125f, 0.0625f};
    for (unsigned int k = 0; k < 4; ++k)
      bank.set(k, freqs[k], mags[k]);
    buffer& out = bank();
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      double expected = 0.0;
      for (unsigned int k = 0; k < 4; ++k)
        expected += mags[k] * std::sin(TWO_PI * PhaseAt(freqs[k], i, 44100.0));
      REQUIRE(out.getPtr()[i] == doctest::Approx(expected).epsilon(1e-5).scale(1.0));
    }
    CHECK(bank.frequency(2) == 330.f);
    CHECK(bank.magnitude(3) == 0.0625f);
    CHECK(bank.frequency(99) == 0.f);
    CHECK(bank.magnitude(99) == 0.f);
    CHECK(bank.phase(99) == 0.f);
  }

  TEST_CASE("oscillatorBank: smoothing ramps in a straight line over N samples (#1061)") {
    oscillatorBank bank(1);
    bank.magnitudeSmoothing(4).frequencySmoothing(8);
    bank.set(0, 800.f, 1.f);
    // Before any sample: nothing moved yet.
    CHECK(bank.magnitude(0) == 0.f);
    CHECK(bank.frequency(0) == 0.f);
    for (int s = 1; s <= 8; ++s) {
      CAPTURE(s);
      bank(1);
      CHECK(bank.magnitude(0) == doctest::Approx(s >= 4 ? 1.0 : s / 4.0));
      CHECK(bank.frequency(0) == doctest::Approx(100.0 * s));
    }
    // The end is exact.
    CHECK(bank.magnitude(0) == 1.f);
    CHECK(bank.frequency(0) == 800.f);

    // A new value mid-ramp starts from where the ramp got to.
    bank.setMagnitude(0, 0.f);
    bank(2);
    CHECK(bank.magnitude(0) == doctest::Approx(0.5));
    bank.setMagnitude(0, 1.f);
    bank(1);
    CHECK(bank.magnitude(0) == doctest::Approx(0.5 + 0.5 / 4));
    // The same target again does not restart the ramp.
    bank.setMagnitude(0, 1.f);
    bank(3);
    CHECK(bank.magnitude(0) == 1.f);

    // 0 and 1 both mean no smoothing.
    for (unsigned int samples : {0u, 1u}) {
      oscillatorBank jump(1);
      jump.magnitudeSmoothing(samples).frequencySmoothing(samples);
      jump.set(0, 50.f, 0.75f);
      CHECK(jump.magnitude(0) == 0.75f);
      CHECK(jump.frequency(0) == 50.f);
    }
  }

  TEST_CASE("oscillatorBank: set pairs, fade, clear, silence and reset (#1061)") {
    oscillatorBank bank(4);
    for (unsigned int k = 0; k < 4; ++k)
      bank.set(k, 100.f * (k + 1), 1.f);
    const float pairs[] = {50.f, 0.5f, 60.f, 0.25f};
    bank.setPairs(pairs, 2);
    CHECK(bank.frequency(0) == 50.f);
    CHECK(bank.magnitude(0) == 0.5f);
    CHECK(bank.frequency(1) == 60.f);
    CHECK(bank.magnitude(1) == 0.25f);
    // The rest fade to 0; their frequencies stay.
    CHECK(bank.magnitude(2) == 0.f);
    CHECK(bank.magnitude(3) == 0.f);
    CHECK(bank.frequency(3) == 400.f);

    // Pairs past the capacity are ignored.
    std::vector<float> many(20, 1.f);
    bank.setPairs(many.data(), 10);
    for (unsigned int k = 0; k < 4; ++k)
      CHECK(bank.magnitude(k) == 1.f);

    // set's fade is smoothed, silence is immediate.
    bank.magnitudeSmoothing(10);
    bank.fadeFrom(2);
    bank(1);
    CHECK(bank.magnitude(2) == doctest::Approx(0.9));
    CHECK(bank.magnitude(0) == 1.f);
    bank.silence();
    for (unsigned int k = 0; k < 4; ++k)
      CHECK(bank.magnitude(k) == 0.f);
    CHECK(bank.frequency(0) == 1.f);

    bank(16);
    const float before = bank.phase(0);
    CHECK(before != 0.f);
    bank.clear();
    CHECK(bank.frequency(0) == 0.f);
    CHECK(bank.magnitude(0) == 0.f);
    CHECK(bank.phase(0) == before);
    bank.reset();
    CHECK(bank.phase(0) == 0.f);

    // A null pointer fades everything.
    bank.magnitudeSmoothing(0).set(1, 10.f, 1.f).setPairs(nullptr, 3);
    CHECK(bank.magnitude(1) == 0.f);
  }

  TEST_CASE("oscillatorBank: size runs the first oscillators, the rest freeze (#1061)") {
    TestHelpers::ScopedSampleRate rate(44100);
    oscillatorBank bank(3, true);
    bank.set(0, 300.f, 0.5f).set(1, 500.f, 0.5f).set(2, 700.f, 0.5f);
    bank.size(1);
    CHECK(bank.size() == 1);
    buffer& out = bank();
    for (unsigned int i = 0; i < out.getLength(); ++i)
      REQUIRE(out.getPtr()[i] ==
              doctest::Approx(0.5 * std::sin(TWO_PI * PhaseAt(300.0, i, 44100.0)))
                  .epsilon(1e-5)
                  .scale(1.0));
    CHECK(bank.phase(1) == 0.f);
    CHECK(bank.phase(0) != 0.f);

    bank.size(0);
    CHECK(bank.size() == 1);
    bank.size(100);
    CHECK(bank.size() == 3);
  }

  TEST_CASE("oscillatorBank: signals set the oscillator the index names, sample by sample "
            "(#1061)") {
    oscillatorBank bank(4);
    buffer freq(8);
    buffer mag(8);
    buffer phase(8);
    buffer index(8);
    const float f[] = {10.f, 20.f, 30.f, 40.f, 50.f, 60.f, 70.f, 80.f};
    const float m[] = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f};
    // 2.7 truncates to 2, -0.5 to 0; 4 and -1 and NaN name nothing.
    const float n[] = {0.f, 1.f, 2.7f, -0.5f, 4.f, -1.f, std::numeric_limits<float>::quiet_NaN(),
                       3.f};
    for (unsigned int i = 0; i < 8; ++i) {
      freq.getPtr()[i] = f[i];
      mag.getPtr()[i] = m[i];
      phase.getPtr()[i] = 0.5f;
      index.getPtr()[i] = n[i];
    }
    buffer& out = bank(freq, mag, phase, index);
    CHECK(out.getLength() == 8);
    CHECK(bank.frequency(0) == 40.f); // sample 3 overwrote sample 0
    CHECK(bank.magnitude(0) == 0.4f);
    CHECK(bank.frequency(1) == 20.f);
    CHECK(bank.frequency(2) == 30.f);
    CHECK(bank.frequency(3) == 80.f);
    CHECK(bank.magnitude(3) == 0.8f);
    // Without frame sync the phase signal (0.5) is ignored: oscillator 3,
    // set on the block's last sample, ran one sample on from 0.
    CHECK(bank.phase(3) == doctest::Approx(80.0 / YSE::SAMPLERATE));

    // The index names oscillators of the size, not the capacity.
    bank.clear().size(2);
    Fill(index, 3.f);
    bank(freq, mag, phase, index);
    CHECK(bank.frequency(3) == 0.f);

    // The block is as long as the shortest signal.
    buffer shortIndex(5);
    Fill(shortIndex, 0.f);
    CHECK(bank(freq, mag, phase, shortIndex).getLength() == 5);
    CHECK(bank(freq, mag, shortIndex).getLength() == 5);
  }

  TEST_CASE("oscillatorBank: frame sync holds values until the next index 0 (#1061)") {
    oscillatorBank bank(3);
    bank.frameSync(true);
    CHECK(bank.frameSync());
    // One frame: indices 0, 1, 2, then the next frame starts at 0.
    buffer freq(4);
    buffer mag(4);
    buffer phase(4);
    buffer index(4);
    const float f[] = {100.f, 200.f, 300.f, 111.f};
    const float m[] = {0.5f, 0.6f, 0.7f, 0.9f};
    const float p[] = {0.25f, 0.5f, 0.75f, 0.f};
    const float n[] = {0.f, 1.f, 2.f, 0.f};
    for (unsigned int i = 0; i < 4; ++i) {
      freq.getPtr()[i] = f[i];
      mag.getPtr()[i] = m[i];
      phase.getPtr()[i] = p[i];
      index.getPtr()[i] = n[i];
    }
    bank(freq, mag, phase, index);
    // The fourth sample (index 0) committed the first frame, then held its
    // own values for oscillator 0.
    CHECK(bank.frequency(0) == 100.f);
    CHECK(bank.magnitude(0) == 0.5f);
    CHECK(bank.frequency(1) == 200.f);
    CHECK(bank.frequency(2) == 300.f);
    CHECK(bank.magnitude(2) == 0.7f);
    // The phases jumped at the commit (sample 3) and ran one sample on.
    CHECK(bank.phase(1) == doctest::Approx(0.5 + 200.0 / YSE::SAMPLERATE));
    CHECK(bank.phase(2) == doctest::Approx(0.75 + 300.0 / YSE::SAMPLERATE));

    // The held 111 takes effect at the next index 0 only.
    buffer one(1);
    buffer oneIndex(1);
    oneIndex.getPtr()[0] = 1.f;
    one.getPtr()[0] = 0.f;
    bank(one, one, one, oneIndex);
    CHECK(bank.frequency(0) == 100.f);
    oneIndex.getPtr()[0] = 0.f;
    bank(one, one, one, oneIndex);
    CHECK(bank.frequency(0) == 111.f);
    CHECK(bank.magnitude(0) == 0.9f);

    // Without a phase signal a frame leaves the phases running.
    oscillatorBank freeBank(2);
    freeBank.frameSync(true).set(1, 1000.f, 1.f);
    freeBank(64);
    const float running = freeBank.phase(1);
    buffer two(2);
    buffer twoIndex(2);
    twoIndex.getPtr()[0] = 1.f;
    twoIndex.getPtr()[1] = 0.f;
    Fill(two, 0.25f);
    freeBank(two, two, twoIndex);
    CHECK(freeBank.frequency(1) == 0.25f);
    CHECK(freeBank.phase(1) ==
          doctest::Approx(running + 1000.0 / YSE::SAMPLERATE + 0.25 / YSE::SAMPLERATE));

    // clear drops held values.
    twoIndex.getPtr()[0] = 1.f;
    twoIndex.getPtr()[1] = 1.f;
    Fill(two, 5.f);
    freeBank(two, two, twoIndex);
    freeBank.clear();
    Fill(twoIndex, 0.f);
    Fill(two, 0.f);
    freeBank(two, two, twoIndex);
    CHECK(freeBank.frequency(1) == 0.f);
  }

  TEST_CASE("oscillatorBank: table size rounds to a power of two and changes the steps (#1061)") {
    oscillatorBank bank(1);
    CHECK(bank.tableSize(3000).tableSize() == 2048);
    CHECK(bank.tableSize(3072).tableSize() == 4096);
    CHECK(bank.tableSize(1).tableSize() == 2);
    CHECK(bank.tableSize(0).tableSize() == 2);
    CHECK(bank.tableSize(100000).tableSize() == 65536);
    CHECK(bank.tableSize(5).tableSize() == 4);
    CHECK(oscillatorBank(1, false, 1000).tableSize() == 1024);

    // Four points: 0, 1, 0, -1, each held for two samples at rate / 8.
    TestHelpers::ScopedSampleRate rate(48000);
    bank.tableSize(4).set(0, 6000.f, 1.f);
    buffer& out = bank(8);
    const float expected[] = {0.f, 0.f, 1.f, 1.f, 0.f, 0.f, -1.f, -1.f};
    for (unsigned int i = 0; i < 8; ++i) {
      CAPTURE(i);
      CHECK(out.getPtr()[i] == doctest::Approx(expected[i]).scale(1.0).epsilon(1e-6));
    }
  }

  TEST_CASE("oscillatorBank: a waveform from setTable (#1061)") {
    TestHelpers::ScopedSampleRate rate(48000);
    oscillatorBank bank(1);
    bank.tableSize(4);
    const float wave[] = {1.f, 0.5f, std::numeric_limits<float>::infinity()};
    bank.setTable(wave, 3);
    CHECK(bank.customTable());
    bank.set(0, 12000.f, 2.f); // one point per sample
    buffer& out = bank(8);
    const float expected[] = {2.f, 1.f, 0.f, 0.f, 2.f, 1.f, 0.f, 0.f};
    for (unsigned int i = 0; i < 8; ++i)
      CHECK(out.getPtr()[i] == expected[i]);

    // A new table size goes back to the sine, so does a null table.
    bank.tableSize(8);
    CHECK_FALSE(bank.customTable());
    bank.setTable(wave, 2);
    CHECK(bank.customTable());
    bank.setTable(nullptr, 0);
    CHECK_FALSE(bank.customTable());
    CHECK(bank.tableSize() == 8);
  }

  TEST_CASE("oscillatorBank: non-finite values are 0 (#1061)") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    oscillatorBank bank(2);
    bank.set(0, nan, inf).set(1, inf, nan).setPhase(0, nan);
    CHECK(bank.frequency(0) == 0.f);
    CHECK(bank.magnitude(0) == 0.f);
    CHECK(bank.frequency(1) == 0.f);
    CHECK(bank.magnitude(1) == 0.f);
    CHECK(bank.phase(0) == 0.f);
    bank.setPhase(1, 1.25f);
    CHECK(bank.phase(1) == doctest::Approx(0.25));
    bank.setPhase(1, -0.25f);
    CHECK(bank.phase(1) == doctest::Approx(0.75));
    // Past the capacity: ignored.
    bank.set(7, 1.f, 1.f).setFrequency(7, 1.f).setMagnitude(7, 1.f).setPhase(7, 0.5f);
    buffer& out = bank();
    for (unsigned int i = 0; i < out.getLength(); ++i)
      REQUIRE(std::isfinite(out.getPtr()[i]));
  }

  TEST_CASE("oscillatorBank: negative frequencies run the phase backwards (#1061)") {
    TestHelpers::ScopedSampleRate rate(44100);
    oscillatorBank bank(1, true);
    bank.set(0, -441.f, 1.f);
    buffer& out = bank();
    for (unsigned int i = 0; i < out.getLength(); ++i)
      REQUIRE(
          out.getPtr()[i] ==
          doctest::Approx(std::sin(TWO_PI * PhaseAt(-441.0, i, 44100.0))).epsilon(1e-5).scale(1.0));
  }

  TEST_CASE("oscillatorBank: follows the live sample rate (#1061)") {
    // A quarter of the rate: 0, 1, 0, -1 at any rate, from one bank built
    // before the rate changes.
    oscillatorBank bank(1, true);
    for (unsigned int r : {44100u, 48000u, 96000u}) {
      CAPTURE(r);
      TestHelpers::ScopedSampleRate rate(r);
      bank.reset();
      bank.set(0, static_cast<float>(r) / 4.f, 1.f);
      buffer& out = bank(8);
      const float expected[] = {0.f, 1.f, 0.f, -1.f, 0.f, 1.f, 0.f, -1.f};
      for (unsigned int i = 0; i < 8; ++i)
        CHECK(out.getPtr()[i] == doctest::Approx(expected[i]).scale(1.0).epsilon(1e-5));
    }
  }

  TEST_CASE("oscillatorBank: processing and setters allocate nothing (#1061)") {
    oscillatorBank bank(64, true);
    oscillatorBank stepped(64, false);
    buffer freq(YSE::STANDARD_BUFFERSIZE);
    buffer mag(YSE::STANDARD_BUFFERSIZE);
    buffer phase(YSE::STANDARD_BUFFERSIZE);
    buffer index(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < index.getLength(); ++i) {
      index.getPtr()[i] = static_cast<float>(i % 64);
      freq.getPtr()[i] = 100.f + static_cast<float>(i);
      mag.getPtr()[i] = 0.01f;
      phase.getPtr()[i] = 0.f;
    }
    const float pairs[] = {100.f, 0.5f, 200.f, 0.25f};
    const float wave[] = {1.f, -1.f};
    stepped.setTable(wave, 2); // the one setter that may allocate
    stepped.setTable(nullptr, 0);
    {
      TestHelpers::ProbeScope probe;
      bank.set(0, 440.f, 0.5f)
          .setPairs(pairs, 2)
          .fadeFrom(1)
          .size(32)
          .frequencySmoothing(64)
          .magnitudeSmoothing(64);
      bank();
      bank(freq, mag, index);
      bank.frameSync(true);
      bank(freq, mag, phase, index);
      bank.silence().clear().reset();
      stepped.tableSize(65536);
      stepped();
      stepped.tableSize(4096).setTable(wave, 2); // within the table it had
      stepped(freq, mag, phase, index);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")

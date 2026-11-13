// Tests for YSE::DSP::pitchTracker (YseEngine/dsp/analysis/pitchTracker.hpp),
// issue #1130. References: sines, whose frequency is known; impulse trains
// with a whole period in samples, whose difference function is 0 at that lag
// and symmetric around it, so the estimate is SAMPLERATE / period in closed
// form; sawtooth and square waves, whose strong harmonics test for octave
// errors; and white noise, which has no pitch. Onsets are counted on a tone
// that starts from silence and on legato steps between tones.

#include <doctest/doctest.h>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include "dsp/analysis/pitchTracker.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::DSP::buffer;
using YSE::DSP::pitchTracker;

namespace {

  constexpr UInt N = YSE::STANDARD_BUFFERSIZE;
  constexpr double Pi = 3.14159265358979323846;

  // The signal at sample `i`.
  using Signal = std::function<double(std::uint64_t)>;

  Signal Sine(double hz, double amp = 0.5) {
    return [hz, amp](std::uint64_t i) {
      return amp * std::sin(2.0 * Pi * hz * static_cast<double>(i) /
                            static_cast<double>(YSE::SAMPLERATE));
    };
  }

  // A falling sawtooth with all harmonics at 1/k.
  Signal Saw(double hz, double amp = 0.5) {
    return [hz, amp](std::uint64_t i) {
      const double phase = hz * static_cast<double>(i) / static_cast<double>(YSE::SAMPLERATE);
      return amp * (1.0 - 2.0 * (phase - std::floor(phase)));
    };
  }

  Signal Square(double hz, double amp = 0.5) {
    return [hz, amp](std::uint64_t i) {
      const double phase = hz * static_cast<double>(i) / static_cast<double>(YSE::SAMPLERATE);
      return phase - std::floor(phase) < 0.5 ? amp : -amp;
    };
  }

  Signal Impulses(std::uint64_t period, double amp = 0.5) {
    return [period, amp](std::uint64_t i) { return i % period == 0 ? amp : 0.0; };
  }

  // White noise from a fixed-seed LCG, in -amp .. amp.
  Signal Noise(double amp = 0.5) {
    auto state = std::make_shared<std::uint32_t>(12345u);
    return [state, amp](std::uint64_t) {
      *state = *state * 1664525u + 1013904223u;
      return amp * (static_cast<double>(*state) / 2147483648.0 - 1.0);
    };
  }

  // Feeds `blocks` blocks of `signal` from sample `start` on; returns the
  // sample index after. `after` is called after every block.
  std::uint64_t Run(pitchTracker& t, const Signal& signal, int blocks, std::uint64_t start = 0,
                    const std::function<void()>& after = {}) {
    buffer in(N);
    for (int b = 0; b < blocks; ++b) {
      for (UInt i = 0; i < N; ++i)
        in.getPtr()[i] = static_cast<Flt>(signal(start + i));
      start += N;
      t(in);
      if (after) after();
    }
    return start;
  }

  int Onsets(pitchTracker& t, const Signal& signal, int blocks, std::uint64_t& start) {
    int count = 0;
    start = Run(t, signal, blocks, start, [&]() { count += static_cast<int>(t.onsets()); });
    return count;
  }

  // Blocks that last at least `seconds` at 48 kHz.
  int Blocks(double seconds) {
    return static_cast<int>(std::ceil(seconds * 48000.0 / static_cast<double>(N)));
  }

  double Relative(double value, double expected) {
    return std::fabs(value - expected) / expected;
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("pitchTracker: defaults and limits (#1130)") {
    pitchTracker t;
    CHECK(t.size() == pitchTracker::DEFAULT_SIZE);
    CHECK(t.period() == pitchTracker::DEFAULT_PERIOD);
    CHECK(t.minFrequency() == pitchTracker::DEFAULT_MIN_FREQUENCY);
    CHECK(t.maxFrequency() == pitchTracker::DEFAULT_MAX_FREQUENCY);
    CHECK(t.threshold() == pitchTracker::DEFAULT_THRESHOLD);
    CHECK(t.tolerance() == pitchTracker::DEFAULT_TOLERANCE);
    CHECK(t.onsetAmplitude() == pitchTracker::DEFAULT_ONSET_AMPLITUDE);
    CHECK(t.onsetPitch() == pitchTracker::DEFAULT_ONSET_PITCH);
    CHECK(t.onsetPeriod() == pitchTracker::DEFAULT_ONSET_PERIOD);
    CHECK(t.latency() == 1024);
    CHECK(t.frequency() == 0.f);
    CHECK(t.amplitude() == 0.f);
    CHECK_FALSE(t.voiced());

    // The size rounds down to a power of two within 128 .. 4096.
    CHECK(t.size(1000).size() == 512);
    CHECK(t.latency() == 256);
    CHECK(t.size(1024).size() == 1024);
    CHECK(t.size(5).size() == pitchTracker::MIN_SIZE);
    CHECK(t.size(100000).size() == pitchTracker::MAX_SIZE);
    CHECK(t.period(3).period() == pitchTracker::MIN_PERIOD);
    CHECK(t.period(10000).period() == pitchTracker::MAX_PERIOD);
    CHECK(t.period(1000).period() == 1000);

    const Flt nan = std::numeric_limits<Flt>::quiet_NaN();
    const Flt inf = std::numeric_limits<Flt>::infinity();
    t.minFrequency(100.f).minFrequency(0.f).minFrequency(-5.f).minFrequency(nan);
    CHECK(t.minFrequency() == 100.f);
    t.maxFrequency(900.f).maxFrequency(0.f).maxFrequency(inf);
    CHECK(t.maxFrequency() == 900.f);
    CHECK(t.threshold(-1.f).threshold() == 0.f);
    CHECK(t.threshold(0.2f).threshold(nan).threshold() == 0.2f);
    CHECK(t.tolerance(2.f).tolerance() == 1.f);
    CHECK(t.tolerance(-1.f).tolerance() == 0.f);
    CHECK(t.tolerance(0.3f).tolerance(nan).tolerance() == doctest::Approx(0.3f));
    CHECK(t.onsetAmplitude(-1.f).onsetAmplitude() == 0.f);
    CHECK(t.onsetPitch(-1.f).onsetPitch() == 0.f);
    CHECK(t.onsetPitch(2.f).onsetPitch(inf).onsetPitch() == 2.f);
    CHECK(t.onsetPeriod(0).onsetPeriod() == 0);
  }

  TEST_CASE("pitchTracker: an impulse train whose period divides half the buffer reads "
            "SAMPLERATE / period (#1130)") {
    // With a whole number of periods in each half, the difference function
    // is 0 at the period and the same on both sides of it, so the parabola
    // adds nothing: the estimate is exact.
    ScopedSampleRate rate(48000);
    for (const std::uint64_t period : {16u, 32u, 64u, 128u, 256u, 512u}) {
      CAPTURE(period);
      pitchTracker t;
      Run(t, Impulses(period), Blocks(0.2));
      REQUIRE(t.voiced());
      CHECK(t.frequency() == doctest::Approx(48000.0 / static_cast<double>(period)).epsilon(1e-5));
      CHECK(t.clarity() == doctest::Approx(1.0).epsilon(1e-5));
      CHECK(t.amplitude() == 0.5f);
    }
  }

  TEST_CASE("pitchTracker: sines from 50 Hz to 3.5 kHz within 0.1 % (#1130)") {
    ScopedSampleRate rate(48000);
    for (const double hz : {50.0, 110.0, 220.0, 261.63, 440.0, 1000.0, 2500.0, 3500.0}) {
      CAPTURE(hz);
      pitchTracker t;
      Run(t, Sine(hz), Blocks(0.2));
      REQUIRE(t.voiced());
      CHECK(Relative(t.frequency(), hz) < 1e-3);
      CHECK(t.clarity() > 0.99f);
      CHECK(t.amplitude() <= 0.5f);
      CHECK(t.amplitude() > 0.499f);
    }
  }

  TEST_CASE("pitchTracker: sawtooth and square waves read their fundamental, not an octave "
            "(#1130)") {
    ScopedSampleRate rate(48000);
    for (const double hz : {82.41, 110.0, 196.0, 440.0, 880.0}) {
      CAPTURE(hz);
      pitchTracker saw;
      Run(saw, Saw(hz), Blocks(0.2));
      REQUIRE(saw.voiced());
      CHECK(Relative(saw.frequency(), hz) < 5e-3);
      pitchTracker square;
      Run(square, Square(hz), Blocks(0.2));
      REQUIRE(square.voiced());
      CHECK(Relative(square.frequency(), hz) < 5e-3);
    }
  }

  TEST_CASE("pitchTracker: noise has an amplitude but no pitch; silence and the gate report "
            "nothing (#1130)") {
    ScopedSampleRate rate(48000);
    pitchTracker t;
    UInt analyses = 0;
    UInt reports = 0;
    UInt pitches = 0;
    buffer in(N);
    std::uint64_t at = 0;
    const Signal noise = Noise();
    for (int b = 0; b < Blocks(0.5); ++b) {
      for (UInt i = 0; i < N; ++i)
        in.getPtr()[i] = static_cast<Flt>(noise(at++));
      analyses += t(in);
      reports += t.reports();
      pitches += t.pitches();
    }
    CHECK(analyses == static_cast<UInt>(Blocks(0.5)) * N / pitchTracker::DEFAULT_PERIOD);
    CHECK(reports == analyses);
    CHECK(pitches == 0);
    CHECK_FALSE(t.voiced());
    CHECK(t.amplitude() > 0.45f);

    pitchTracker quiet;
    analyses = 0;
    reports = 0;
    in = 0.f;
    for (int b = 0; b < Blocks(0.2); ++b) {
      analyses += quiet(in);
      reports += quiet.reports();
    }
    CHECK(analyses > 0);
    CHECK(reports == 0);
    CHECK(quiet.amplitude() == 0.f);

    // A sine below the threshold reports nothing; at or above it, it does.
    pitchTracker gated;
    gated.threshold(0.2f);
    reports = 0;
    pitches = 0;
    const auto counted = [&]() {
      reports += gated.reports();
      pitches += gated.pitches();
    };
    at = Run(gated, Sine(440.0, 0.1), Blocks(0.2), 0, counted);
    CHECK(reports == 0);
    CHECK(pitches == 0);
    CHECK(gated.frequency() == 0.f);
    CHECK(gated.amplitude() == 0.f);
    CHECK_FALSE(gated.voiced());
    at = Run(gated, Sine(440.0, 0.3), Blocks(0.2), at, counted);
    CHECK(reports > 0);
    CHECK(pitches > 0);
    CHECK(Relative(gated.frequency(), 440.0) < 1e-3);
    CHECK(gated.amplitude() > 0.299f);
    // Back below the gate: once the buffer holds only the soft tone,
    // nothing is reported and the last values stay.
    at = Run(gated, Sine(440.0, 0.1), Blocks(0.05), at);
    reports = 0;
    Run(gated, Sine(440.0, 0.1), Blocks(0.2), at, counted);
    CHECK(reports == 0);
    CHECK_FALSE(gated.voiced());
    CHECK(Relative(gated.frequency(), 440.0) < 1e-2);
    CHECK(gated.amplitude() > 0.2f);
  }

  TEST_CASE("pitchTracker: one analysis every period samples, the first after one period "
            "(#1130)") {
    static_assert(N == 128, "the counts below are for 128-sample blocks");
    pitchTracker t;
    buffer in(N);
    in = 0.f;
    t.period(128);
    CHECK(t(in) == 1);
    t.period(32);
    CHECK(t(in) == 4);
    t.period(1000).reset();
    UInt total = 0;
    for (int b = 0; b < 40; ++b)
      total += t(in); // 5120 samples
    CHECK(total == 5);
    t.reset().period(200);
    CHECK(t(in) == 0); // the first analysis is 200 samples in
    CHECK(t(in) == 1);
    // A shorter period brings the next analysis forward.
    t.reset().period(4096);
    CHECK(t(in) == 0);
    t.period(20); // due in 20 samples
    CHECK(t(in) == 6); // at 20, 40, ... 120 of this block
    // Short and empty calls, and a null pointer, which is silence.
    t.reset().period(16);
    CHECK(t(in.getPtr(), 15) == 0);
    CHECK(t(in.getPtr(), 1) == 1);
    CHECK(t(in.getPtr(), 0) == 0);
    CHECK(t(nullptr, 32) == 2);
    CHECK(t.reports() == 0);
  }

  TEST_CASE("pitchTracker: freqmin and freqmax limit what is reported; a tone above the range "
            "is not read an octave down (#1130)") {
    ScopedSampleRate rate(48000);
    // Counted once the buffer holds only the tone: see the attack below.
    pitchTracker high;
    high.maxFrequency(300.f);
    UInt pitches = 0;
    std::uint64_t at = Run(high, Sine(440.0), Blocks(0.05));
    UInt reports = 0;
    Run(high, Sine(440.0), Blocks(0.5), at, [&]() {
      pitches += high.pitches();
      reports += high.reports();
    });
    CHECK(pitches == 0);
    CHECK(reports > 0);

    pitchTracker low;
    low.minFrequency(500.f);
    pitches = 0;
    at = Run(low, Sine(440.0), Blocks(0.05));
    Run(low, Sine(440.0), Blocks(0.5), at, [&]() { pitches += low.pitches(); });
    CHECK(pitches == 0);

    // Within the range it is reported; a range given upside down still
    // holds it.
    pitchTracker swapped;
    swapped.minFrequency(1000.f).maxFrequency(200.f);
    Run(swapped, Sine(440.0), Blocks(0.2));
    REQUIRE(swapped.voiced());
    CHECK(Relative(swapped.frequency(), 440.0) < 1e-3);
  }

  TEST_CASE("pitchTracker: the size sets the lowest pitch found (#1130)") {
    ScopedSampleRate rate(48000);
    pitchTracker small;
    small.size(256); // lags up to 127: 378 Hz and up
    UInt pitches = 0;
    const std::uint64_t at = Run(small, Sine(220.0), Blocks(0.05));
    Run(small, Sine(220.0), Blocks(0.5), at, [&]() { pitches += small.pitches(); });
    CHECK(pitches == 0);
    pitchTracker small2;
    small2.size(256);
    Run(small2, Sine(440.0), Blocks(0.2));
    REQUIRE(small2.voiced());
    CHECK(Relative(small2.frequency(), 440.0) < 2e-3);

    pitchTracker large;
    large.size(4096).minFrequency(20.f); // lags up to 2047: 23.4 Hz and up
    Run(large, Sine(30.0), Blocks(0.5));
    REQUIRE(large.voiced());
    CHECK(Relative(large.frequency(), 30.0) < 1e-3);
  }

  TEST_CASE("pitchTracker: follows SAMPLERATE changed after construction (#1130)") {
    ScopedSampleRate rate(48000);
    pitchTracker t;
    Run(t, Impulses(64), Blocks(0.2));
    CHECK(t.frequency() == doctest::Approx(750.0).epsilon(1e-5));
    {
      ScopedSampleRate other(44100);
      Run(t, Impulses(64), Blocks(0.2));
      CHECK(t.frequency() == doctest::Approx(44100.0 / 64.0).epsilon(1e-5));
      // freqmin turns into a lag at the live rate too.
      t.minFrequency(700.f);
      UInt pitches = 0;
      Run(t, Impulses(64), Blocks(0.2), 0, [&]() { pitches += t.pitches(); });
      CHECK(pitches == 0);
    }
    t.minFrequency(40.f);
    Run(t, Impulses(64), Blocks(0.2));
    CHECK(t.frequency() == doctest::Approx(750.0).epsilon(1e-5));
  }

  TEST_CASE("pitchTracker: a tone from silence is one amplitude onset (#1130)") {
    ScopedSampleRate rate(48000);
    pitchTracker t;
    std::uint64_t at = 0;
    buffer silence(N);
    silence = 0.f;
    for (int b = 0; b < Blocks(0.1); ++b) {
      t(silence);
      CHECK(t.onsets() == 0);
    }
    // The attack's estimate settles inside the note: one onset, not a
    // second one when the pitch firms up.
    CHECK(Onsets(t, Sine(440.0), Blocks(1.0), at) == 1);
    CHECK(t.onsetPeak() >= 0.1f);
    REQUIRE(t.voiced());
    CHECK(Relative(t.frequency(), 440.0) < 1e-3);

    // Silence long enough to empty the buffer, then the same tone: another
    // onset.
    for (int b = 0; b < Blocks(0.1); ++b)
      t(silence);
    CHECK(Onsets(t, Sine(440.0), Blocks(0.5), at) == 1);

    // Amplitude onsets off: none, nor from the attack's settling pitch.
    pitchTracker off;
    off.onsetAmplitude(0.f);
    at = 0;
    CHECK(Onsets(off, Sine(440.0), Blocks(0.5), at) == 0);
    // A tone that stays below onsetamp: none.
    pitchTracker soft;
    at = 0;
    CHECK(Onsets(soft, Sine(440.0, 0.05), Blocks(0.5), at) == 0);
  }

  TEST_CASE("pitchTracker: a legato step of onsetpitch semitones or more is a pitch onset "
            "(#1130)") {
    ScopedSampleRate rate(48000);
    pitchTracker t;
    std::uint64_t at = 0;
    CHECK(Onsets(t, Sine(220.0), Blocks(0.5), at) == 1); // the attack
    // A fifth up, legato: one onset carrying the new pitch.
    CHECK(Onsets(t, Sine(330.0), Blocks(0.5), at) == 1);
    CHECK(Relative(t.onsetFrequency(), 330.0) < 1e-2);
    CHECK(t.onsetPeak() > 0.49f);
    // A quarter tone is below 1 semitone: none.
    CHECK(Onsets(t, Sine(330.0 * std::pow(2.0, 0.5 / 12.0)), Blocks(0.5), at) == 0);

    // Pitch onsets off, or a step smaller than onsetpitch: none.
    pitchTracker off;
    off.onsetPitch(0.f);
    at = 0;
    CHECK(Onsets(off, Sine(220.0), Blocks(0.5), at) == 1);
    CHECK(Onsets(off, Sine(330.0), Blocks(0.5), at) == 0);
    pitchTracker wide;
    wide.onsetPitch(6.f);
    at = 0;
    CHECK(Onsets(wide, Sine(220.0), Blocks(0.5), at) == 1);
    CHECK(Onsets(wide, Sine(261.63), Blocks(0.5), at) == 0); // 3 semitones
    CHECK(Onsets(wide, Sine(330.0), Blocks(0.5), at) == 1); // 7 from the note's 220
  }

  TEST_CASE("pitchTracker: onsetperiod drops an amplitude onset and delays a pitch onset "
            "(#1130)") {
    ScopedSampleRate rate(48000);
    pitchTracker t;
    t.onsetPeriod(48000); // 1 s
    std::uint64_t at = 0;
    int count = 0;
    int firstBlock = -1;
    int secondBlock = -1;
    int block = 0;
    const auto watch = [&]() {
      if (t.onsets() > 0) {
        if (count == 0) firstBlock = block;
        if (count == 1) secondBlock = block;
        count += static_cast<int>(t.onsets());
      }
      block++;
    };
    at = Run(t, Sine(220.0), Blocks(0.2), at, watch);
    Run(t, Sine(330.0), Blocks(1.6), at, watch);
    REQUIRE(count == 2);
    // The step came 0.2 s after the attack, and fires once 1 s has passed,
    // at the first analysis after.
    const int apart = (secondBlock - firstBlock) * static_cast<int>(N);
    CHECK(apart >= 48000 - static_cast<int>(N));
    CHECK(apart <= 48000 + static_cast<int>(pitchTracker::DEFAULT_PERIOD + N));
    CHECK(Relative(t.onsetFrequency(), 330.0) < 1e-2);

    // An amplitude onset inside the period is dropped, not delayed.
    pitchTracker drop;
    drop.onsetPeriod(48000);
    buffer silence(N);
    silence = 0.f;
    at = 0;
    CHECK(Onsets(drop, Sine(440.0), Blocks(0.2), at) == 1);
    for (int b = 0; b < Blocks(0.1); ++b)
      drop(silence);
    CHECK(Onsets(drop, Sine(440.0), Blocks(1.0), at) == 0);
  }

  TEST_CASE("pitchTracker: samples that are not finite enter as 0 (#1130)") {
    ScopedSampleRate rate(48000);
    pitchTracker t;
    buffer in(N);
    in = std::numeric_limits<Flt>::quiet_NaN();
    for (int b = 0; b < 10; ++b) {
      t(in);
      CHECK(t.reports() == 0);
    }
    in = std::numeric_limits<Flt>::infinity();
    t(in);
    CHECK(t.reports() == 0);
    // The history holds zeros, not NaN: a tone after it is read normally.
    Run(t, Sine(440.0), Blocks(0.2));
    REQUIRE(t.voiced());
    CHECK(std::isfinite(t.frequency()));
    CHECK(Relative(t.frequency(), 440.0) < 1e-3);
  }

  TEST_CASE("pitchTracker: reset starts over and keeps the settings (#1130)") {
    ScopedSampleRate rate(48000);
    pitchTracker t;
    t.size(1024).period(256).onsetPitch(3.f);
    Run(t, Sine(440.0), Blocks(0.2));
    REQUIRE(t.voiced());
    t.reset();
    CHECK(t.frequency() == 0.f);
    CHECK(t.amplitude() == 0.f);
    CHECK(t.onsetFrequency() == 0.f);
    CHECK_FALSE(t.voiced());
    CHECK(t.size() == 1024);
    CHECK(t.period() == 256);
    CHECK(t.onsetPitch() == 3.f);
    // The history is silence again: the tone is a new attack.
    std::uint64_t at = 0;
    CHECK(Onsets(t, Sine(440.0), Blocks(0.2), at) == 1);
  }

  TEST_CASE("pitchTracker: blocks and setters allocate nothing (#1130)") {
    ScopedSampleRate rate(48000);
    pitchTracker t;
    buffer in(N);
    for (UInt i = 0; i < N; ++i)
      in.getPtr()[i] = static_cast<Flt>(0.5 * std::sin(2.0 * Pi * 440.0 * i / 48000.0));
    t(in);
    {
      TestHelpers::ProbeScope probe;
      t.size(4096).period(16).minFrequency(30.f).maxFrequency(5000.f).threshold(0.f);
      t.tolerance(0.2f).onsetAmplitude(0.3f).onsetPitch(0.5f).onsetPeriod(100);
      for (int b = 0; b < 20; ++b)
        t(in);
      t.size(128).period(4096);
      for (int b = 0; b < 20; ++b)
        t(in);
      t.reset();
      t(in);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")

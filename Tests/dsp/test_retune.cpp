// Tests for YSE::DSP::tuning and YSE::DSP::retune
// (YseEngine/dsp/delay/retune.hpp), the pitch corrector behind ~retune
// (issue #1154):
//  - tuning: the chromatic default, note lists, the base, enabled degrees,
//    MIDI notes, the nearest enabled pitch, and a YSE::scale's classes;
//  - retune: the setters and their limits;
//  - a slightly detuned sine comes out at the nearest enabled note (by its
//    spectrum), in the granular modes and the phase vocoder; a note list or
//    an enabled subset snaps to the right note; correction_amount 0 is the
//    input latency() samples later, bit for bit, and 50 halfway;
//  - the threshold, the sibilance bypass and enabled(false);
//  - a lock pitch and a detune per sample;
//  - latency() is the shifter's, and a dip in the input comes out
//    latency() samples later;
//  - the pitch output; the live sample rate; nothing allocates.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include "dsp/buffer.hpp"
#include "dsp/delay/retune.hpp"
#include "dsp/delay/pitchShifter.hpp"
#include "music/scale/scaleInterface.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::buffer;
using YSE::DSP::pitchShifter;
using YSE::DSP::retune;
using YSE::DSP::tuning;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;
  constexpr double RATE = 48000.0;

  double Cents(double hz) {
    return 1200.0 * std::log2(hz / 440.0);
  }

  std::vector<float> Sine(std::size_t length, double hz, double rate = RATE,
                          double amplitude = 0.5) {
    std::vector<float> v(length);
    for (std::size_t n = 0; n < length; ++n)
      v[n] =
          static_cast<float>(amplitude * std::sin(2.0 * PI * hz * static_cast<double>(n) / rate));
    return v;
  }

  // `input` through `fn` in blocks of N, with a lock (Hz) and detune
  // (cents) per sample when given. The pitch output goes to `pitch`.
  std::vector<float> Run(retune& fn, const std::vector<float>& input,
                         const std::vector<float>* lock = nullptr,
                         const std::vector<float>* detune = nullptr,
                         std::vector<float>* pitch = nullptr) {
    std::vector<float> out;
    out.reserve(input.size());
    buffer in(N);
    buffer l(N);
    buffer d(N);
    for (std::size_t start = 0; start < input.size(); start += N) {
      const auto length = static_cast<UInt>(std::min<std::size_t>(N, input.size() - start));
      in.resize(length);
      l.resize(length);
      d.resize(length);
      l = 0.f;
      d = 0.f;
      for (UInt n = 0; n < length; ++n) {
        in.getPtr()[n] = input[start + n];
        if (lock != nullptr) l.getPtr()[n] = (*lock)[start + n];
        if (detune != nullptr) d.getPtr()[n] = (*detune)[start + n];
      }
      buffer& o = (lock != nullptr || detune != nullptr) ? fn(in, l, d) : fn(in);
      REQUIRE(o.getLength() == length);
      out.insert(out.end(), o.getPtr(), o.getPtr() + length);
      if (pitch != nullptr)
        pitch->insert(pitch->end(), fn.pitch().getPtr(), fn.pitch().getPtr() + length);
    }
    return out;
  }

  // The amplitude of `v[from..from+length)` at `hz`, Hann-windowed.
  double Amplitude(const std::vector<float>& v, std::size_t from, std::size_t length, double hz,
                   double rate = RATE) {
    double re = 0.0;
    double im = 0.0;
    double sum = 0.0;
    const double w = 2.0 * PI * hz / rate;
    for (std::size_t n = 0; n < length; ++n) {
      const double h =
          0.5 - 0.5 * std::cos(2.0 * PI * static_cast<double>(n) / static_cast<double>(length));
      const double x = h * v[from + n];
      re += x * std::cos(w * static_cast<double>(n));
      im -= x * std::sin(w * static_cast<double>(n));
      sum += h;
    }
    return 2.0 * std::hypot(re, im) / sum;
  }

  // The loudest frequency of the second half of `v` between `low` and
  // `high` Hz, to 0.05 Hz.
  double Loudest(const std::vector<float>& v, double low, double high, double rate = RATE) {
    const std::size_t from = v.size() / 2;
    const std::size_t length = v.size() - from;
    double best = low;
    double level = -1.0;
    const auto steps = static_cast<int>(high - low);
    for (int i = 0; i <= steps; ++i) {
      const double hz = low + i;
      const double a = Amplitude(v, from, length, hz, rate);
      if (a > level) {
        level = a;
        best = hz;
      }
    }
    const double centre = best;
    for (int i = 0; i <= 40; ++i) {
      const double hz = centre - 1.0 + 0.05 * i;
      const double a = Amplitude(v, from, length, hz, rate);
      if (a > level) {
        level = a;
        best = hz;
      }
    }
    return best;
  }

  double Level(const std::vector<float>& v, double hz, double rate = RATE) {
    const std::size_t from = v.size() / 2;
    return Amplitude(v, from, v.size() - from, hz, rate);
  }

  // The tuning of A major on A: A B C# D E F# G#.
  tuning AMajor() {
    const float major[] = {200.f, 400.f, 500.f, 700.f, 900.f, 1100.f};
    tuning t;
    t.base(9).notes(major, 6);
    return t;
  }

  constexpr double B4 = 493.8833012561241;
  constexpr double A_SHARP4 = 466.1637615180899;
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("tuning: the chromatic default, note lists, the base and enabled degrees (#1154)") {
    tuning t;
    CHECK(t.size() == 12);
    for (UInt i = 0; i < 12; ++i) {
      CHECK(t.note(i) == doctest::Approx(100.0 * i));
      CHECK(t.isEnabled(i));
    }
    CHECK_FALSE(t.isEnabled(12));
    CHECK(t.note(12) == 0.f);
    CHECK(t.base() == 0);
    CHECK(t.standardPitch() == 440.f);

    // Degrees outside (0, 1200), and the octave, are skipped; degree 0 is
    // the base.
    const float list[] = {200.f, -5.f,     1200.f, std::numeric_limits<float>::quiet_NaN(),
                          0.f,   701.955f, 1500.f};
    t.notes(list, 7);
    CHECK(t.size() == 3);
    CHECK(t.note(0) == 0.f);
    CHECK(t.note(1) == 200.f);
    CHECK(t.note(2) == doctest::Approx(701.955f));
    t.notes(nullptr, 3);
    CHECK(t.size() == 1);
    std::vector<float> many(100, 50.f);
    t.notes(many.data(), 100);
    CHECK(t.size() == tuning::MAX_NOTES);

    t.base(14);
    CHECK(t.base() == 2);
    t.base(-1);
    CHECK(t.base() == 11);

    t.standardPitch(442.f).standardPitch(0.f).standardPitch(-3.f).standardPitch(
        std::numeric_limits<float>::infinity());
    CHECK(t.standardPitch() == 442.f);

    t.chromatic();
    CHECK(t.size() == 12);
    CHECK(t.base() == 11);
    CHECK(t.standardPitch() == 442.f);

    t.enabled(0b101);
    CHECK(t.isEnabled(0));
    CHECK_FALSE(t.isEnabled(1));
    CHECK(t.isEnabled(2));
    t.enable(1, true).enable(0, false).enable(200, true);
    CHECK(t.enabled() == 0b110);
    t.enableAll();
    CHECK(t.enabled() == ~U64(0));

    CHECK(tuning::ratioToCents(2.f) == doctest::Approx(1200.f));
    CHECK(tuning::ratioToCents(1.5f) == doctest::Approx(701.955f).epsilon(1e-5));
    CHECK(tuning::ratioToCents(0.f) == 0.f);
    CHECK(tuning::ratioToCents(-2.f) == 0.f);
  }

  TEST_CASE("tuning: MIDI notes enable and disable the degree they fall on (#1154)") {
    tuning t;
    t.midiNote(69, 0); // A
    CHECK_FALSE(t.isEnabled(9));
    t.midiNote(60, 0).midiNote(-3, 0); // C, and a negative A
    CHECK_FALSE(t.isEnabled(0));
    t.midiNote(81, 100);
    CHECK(t.isEnabled(9));
    // Relative to the base: on D, note 62 is degree 0.
    t.enableAll().base(2).midiNote(62, 0);
    CHECK_FALSE(t.isEnabled(0));
    CHECK(t.isEnabled(2));
    // A seven-note scale: the note mod 7.
    tuning m = AMajor();
    m.midiNote(9 + 7 * 5 + 3, 0);
    CHECK_FALSE(m.isEnabled(3));
    CHECK(m.isEnabled(2));
  }

  TEST_CASE("tuning: the nearest enabled pitch at any octave, the lower on a tie (#1154)") {
    tuning t;
    float target = 1234.f;
    CHECK(t.nearest(static_cast<float>(Cents(450.0)), target));
    CHECK(target == doctest::Approx(0.f).epsilon(1e-6));
    CHECK(t.nearest(-1130.f, target));
    CHECK(target == doctest::Approx(-1100.f));
    CHECK(t.nearest(2449.f, target));
    CHECK(target == doctest::Approx(2400.f));
    CHECK(t.nearest(50.f, target)); // halfway: the lower
    CHECK(target == doctest::Approx(0.f).epsilon(1e-6));

    // A major on A: 475 Hz (132.5 cents above A) goes to B, not A#.
    tuning m = AMajor();
    CHECK(m.nearest(static_cast<float>(Cents(475.0)), target));
    CHECK(target == doctest::Approx(200.f));
    // G# (1100 above A) is the degree below A.
    CHECK(m.nearest(-80.f, target));
    CHECK(target == doctest::Approx(-100.f));
    // Only A and E enabled: 475 goes to A.
    m.enabled(0b10001);
    CHECK(m.nearest(static_cast<float>(Cents(475.0)), target));
    CHECK(target == doctest::Approx(0.f).epsilon(1e-6));
    CHECK(m.nearest(-450.f, target));
    CHECK(target == doctest::Approx(-500.f));

    m.enabled(0);
    target = 7.f;
    CHECK_FALSE(m.nearest(10.f, target));
    CHECK(target == 7.f);
    CHECK_FALSE(t.nearest(std::numeric_limits<float>::quiet_NaN(), target));
  }

  TEST_CASE("tuning: a YSE::scale's pitch classes, on C (#1154)") {
    YSE::scale major;
    for (const float p : {60.f, 62.f, 64.f, 65.f, 67.f, 69.f, 71.f})
      major.add(p);
    tuning t;
    t.standardPitch(415.f).base(5);
    t.scale(major);
    CHECK(t.base() == 0);
    CHECK(t.standardPitch() == 415.f);
    REQUIRE(t.size() == 7);
    const float expected[] = {0.f, 200.f, 400.f, 500.f, 700.f, 900.f, 1100.f};
    for (UInt i = 0; i < 7; ++i) {
      CHECK(t.note(i) == doctest::Approx(expected[i]));
      CHECK(t.isEnabled(i));
    }
    CHECK_FALSE(t.isEnabled(7));

    // No C, a fractional class, pitches added out of order and doubled.
    YSE::scale odd;
    odd.add(67.f).add(62.5f).add(74.5f, 0).add(43.f);
    t.scale(odd);
    REQUIRE(t.size() == 3);
    CHECK_FALSE(t.isEnabled(0));
    CHECK(t.note(1) == doctest::Approx(250.f));
    CHECK(t.note(2) == doctest::Approx(700.f));
    CHECK(t.isEnabled(1));
    CHECK(t.isEnabled(2));

    YSE::scale empty;
    t.scale(empty);
    CHECK(t.size() == 1);
    float target = 0.f;
    CHECK_FALSE(t.nearest(0.f, target));
  }

  TEST_CASE("retune: defaults, setters and their limits (#1154)") {
    retune fn;
    CHECK(fn.amount() == 100.f);
    CHECK(fn.threshold() == 0.f);
    CHECK(fn.ambience() == doctest::Approx(0.3f));
    CHECK(fn.bypass() == 100.f);
    CHECK(fn.period() == retune::DEFAULT_PERIOD);
    CHECK(fn.quality() == pitchShifter::BASIC);
    CHECK(fn.enabled());
    CHECK_FALSE(fn.pitchDetection());
    CHECK(fn.latency() == 2);
    CHECK(fn.frequency() == 0.f);
    CHECK(fn.correction() == 0.f);
    CHECK(fn.notes().size() == 12);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    fn.amount(150.f);
    CHECK(fn.amount() == 100.f);
    fn.amount(-3.f).amount(nan);
    CHECK(fn.amount() == 0.f);
    fn.threshold(-1.f);
    CHECK(fn.threshold() == 0.f);
    fn.threshold(12.f).threshold(nan);
    CHECK(fn.threshold() == 12.f);
    fn.ambience(3.f);
    CHECK(fn.ambience() == 1.f);
    fn.ambience(-1.f).ambience(nan);
    CHECK(fn.ambience() == 0.f);
    fn.bypass(20.f).bypass(nan);
    CHECK(fn.bypass() == 20.f);
    fn.period(16);
    CHECK(fn.period() == 16);
    fn.period(1);
    CHECK(fn.period() == 16);
    fn.period(0);
    CHECK(fn.period() == retune::DEFAULT_PERIOD);
    fn.quality(pitchShifter::BEST);
    CHECK(fn.quality() == pitchShifter::BEST);
    CHECK(fn.latency() == 4096);
    fn.enabled(false);
    CHECK(fn.latency() == 0);
    fn.pitchDetection(true);
    CHECK(fn.pitchDetection());

    fn.notes(AMajor());
    CHECK(fn.notes().size() == 7);
    CHECK(fn.notes().base() == 9);
    fn.notes().base(3);
    CHECK(fn.notes().base() == 3);
  }

  TEST_CASE("retune: a sine at 450 Hz comes out at A440, in the granular modes and the phase "
            "vocoder, with latency() the shifter's (#1154)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const std::vector<float> input = Sine(384 * N, 450.0);
    for (const pitchShifter::QUALITY q :
         {pitchShifter::BASIC, pitchShifter::GOOD, pitchShifter::BETTER}) {
      CAPTURE(q);
      retune fn(q);
      const std::vector<float> out = Run(fn, input);
      CHECK(fn.frequency() == doctest::Approx(450.0).epsilon(0.001));
      CHECK(fn.correction() == doctest::Approx(-Cents(450.0)).epsilon(0.01));
      CHECK(std::abs(Loudest(out, 400.0, 500.0) - 440.0) < 0.002 * 440.0);
      // What is left at 450 Hz: BASIC's short grains cross the tone about
      // 94 times a second, and their crossfades spread a little of it
      // around (6 % here, under 5 % on the longer grains and the vocoder).
      CHECK(Level(out, 450.0) < (q == pitchShifter::BASIC ? 0.1 : 0.05) * Level(out, 440.0));
      CHECK(Level(out, 440.0) > 0.3);

      const float r = pitchShifter::centsToRatio(fn.correction());
      if (q == pitchShifter::BETTER) {
        CHECK(fn.latency() == 2048);
      } else {
        const unsigned int grain = q == pitchShifter::BASIC ? 1024 : 2048;
        CHECK(fn.latency() ==
              static_cast<UInt>(std::ceil(std::abs(1.0 - r) * grain / 2)) + 2 + grain / 8);
      }
    }
  }

  TEST_CASE("retune: a note list and an enabled subset snap to the right note (#1154)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const std::vector<float> input = Sine(256 * N, 475.0);

    // Chromatic: A#.
    retune chromatic;
    std::vector<float> out = Run(chromatic, input);
    CHECK(std::abs(Loudest(out, 420.0, 520.0) - A_SHARP4) < 0.002 * A_SHARP4);

    // A major: B.
    retune major;
    major.notes(AMajor());
    out = Run(major, input);
    CHECK(std::abs(Loudest(out, 420.0, 520.0) - B4) < 0.002 * B4);
    CHECK(Level(out, 475.0) < 0.05 * Level(out, B4));

    // Only A and E of it: A.
    retune subset;
    subset.notes(AMajor()).notes().enabled(0b10001);
    out = Run(subset, input);
    CHECK(std::abs(Loudest(out, 420.0, 520.0) - 440.0) < 0.002 * 440.0);

    // The same subset by MIDI notes on the chromatic scale: every note off
    // but A and E.
    retune midi;
    for (int note = 60; note < 72; ++note)
      midi.notes().midiNote(note, note == 69 || note == 64 ? 100 : 0);
    out = Run(midi, input);
    CHECK(std::abs(Loudest(out, 420.0, 520.0) - 440.0) < 0.002 * 440.0);

    // A YSE::scale, C major: B.
    YSE::scale c;
    for (const float p : {60.f, 62.f, 64.f, 65.f, 67.f, 69.f, 71.f})
      c.add(p);
    retune scaled;
    scaled.notes().scale(c);
    out = Run(scaled, input);
    CHECK(std::abs(Loudest(out, 420.0, 520.0) - B4) < 0.002 * B4);

    // A standard pitch of 442: the A is 442.
    retune high;
    high.notes().standardPitch(442.f);
    out = Run(high, Sine(256 * N, 450.0));
    CHECK(std::abs(Loudest(out, 400.0, 500.0) - 442.0) < 0.002 * 442.0);
  }

  TEST_CASE("retune: amount 0 is the input latency() samples later, bit for bit; 50 is halfway; "
            "the threshold leaves a small deviation (#1154)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const std::vector<float> input = Sine(256 * N, 450.0);
    retune none;
    none.amount(0.f);
    std::vector<float> out = Run(none, input);
    CHECK(none.correction() == 0.f);
    REQUIRE(none.latency() == 2);
    bool exact = out[0] == 0.f && out[1] == 0.f;
    for (std::size_t n = 2; n < out.size(); ++n)
      exact = exact && out[n] == input[n - 2];
    CHECK(exact);

    // The phase vocoder: the input 2048 samples later, to rounding.
    retune pv(pitchShifter::BETTER);
    pv.amount(0.f);
    out = Run(pv, input);
    REQUIRE(pv.latency() == 2048);
    float worst = 0.f;
    for (std::size_t n = 8192; n < out.size(); ++n)
      worst = std::max(worst, std::abs(out[n] - input[n - 2048]));
    CHECK(worst < 1e-4f);

    retune half;
    half.amount(50.f);
    out = Run(half, input);
    const double halfway = 440.0 * std::pow(2.0, Cents(450.0) / 2400.0);
    CHECK(half.correction() == doctest::Approx(-Cents(450.0) / 2.0).epsilon(0.01));
    CHECK(std::abs(Loudest(out, 400.0, 500.0) - halfway) < 0.002 * halfway);

    // 38.9 cents off: left alone by a threshold of 40, corrected by 35.
    retune loose;
    loose.threshold(40.f);
    out = Run(loose, input);
    CHECK(loose.correction() == 0.f);
    CHECK(std::abs(Loudest(out, 400.0, 500.0) - 450.0) < 0.002 * 450.0);
    retune tight;
    tight.threshold(35.f);
    out = Run(tight, input);
    CHECK(std::abs(Loudest(out, 400.0, 500.0) - 440.0) < 0.002 * 440.0);
  }

  TEST_CASE("retune: disabled passes the input straight through; the sibilance bypass and "
            "unpitched input leave it uncorrected (#1154)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const std::vector<float> input = Sine(256 * N, 450.0);
    retune off;
    off.enabled(false);
    std::vector<float> out = Run(off, input);
    CHECK(out == input);
    CHECK(off.latency() == 0);

    // 450 Hz crosses zero on 1.9 % of the samples: a bypass at 1 % leaves
    // it alone, one at 5 % corrects it.
    retune sibilant;
    sibilant.bypass(1.f);
    out = Run(sibilant, input);
    CHECK(sibilant.sibilance() == doctest::Approx(100.0 * 900.0 / 48000.0).epsilon(0.05));
    CHECK(sibilant.correction() == 0.f);
    CHECK(std::abs(Loudest(out, 400.0, 500.0) - 450.0) < 0.002 * 450.0);
    retune voiced;
    voiced.bypass(5.f);
    out = Run(voiced, input);
    CHECK(std::abs(Loudest(out, 400.0, 500.0) - 440.0) < 0.002 * 440.0);

    // Silence below the tracker's gate: no pitch, no correction, ratio 1.
    retune quiet;
    out = Run(quiet, Sine(32 * N, 450.0, RATE, 0.001));
    CHECK(quiet.frequency() == 0.f);
    CHECK(quiet.correction() == 0.f);
    CHECK(quiet.latency() == 2);

    // No enabled note: nothing to correct toward.
    retune nothing;
    nothing.notes().enabled(0);
    out = Run(nothing, input);
    CHECK(nothing.correction() == 0.f);
  }

  TEST_CASE("retune: a lock pitch per sample wins over the scale; a detune adds cents (#1154)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const std::vector<float> input = Sine(256 * N, 450.0);
    const std::vector<float> lock(input.size(), 500.f);
    const std::vector<float> zero(input.size(), 0.f);
    retune locked;
    std::vector<float> out = Run(locked, input, &lock, &zero);
    CHECK(std::abs(Loudest(out, 420.0, 540.0) - 500.0) < 0.002 * 500.0);

    // A lock of 0 follows the scale, plus a detune of a semitone: A#.
    const std::vector<float> up(input.size(), 100.f);
    retune detuned;
    out = Run(detuned, input, &zero, &up);
    CHECK(std::abs(Loudest(out, 420.0, 520.0) - A_SHARP4) < 0.002 * A_SHARP4);
    CHECK(detuned.correction() == doctest::Approx(-Cents(450.0)).epsilon(0.01));

    // Non-finite lock and detune values count as 0.
    std::vector<float> bad(input.size(), std::numeric_limits<float>::quiet_NaN());
    retune sick;
    out = Run(sick, input, &bad, &bad);
    CHECK(std::abs(Loudest(out, 400.0, 500.0) - 440.0) < 0.002 * 440.0);
  }

  TEST_CASE("retune: a gap in a corrected tone comes out latency() samples later (#1154)") {
    TestHelpers::ScopedSampleRate rate(48000);
    // The middle of the stretch, from `from` on, where the level over 64
    // samples falls below 0.1 (a 0.5 sine's is 0.35).
    const auto Gap = [](const std::vector<float>& v, std::size_t from) {
      std::size_t first = 0;
      std::size_t last = 0;
      bool found = false;
      for (std::size_t n = from; n + 64 < v.size() && n < from + 8000; ++n) {
        double e = 0.0;
        for (std::size_t k = 0; k < 64; ++k)
          e += static_cast<double>(v[n + k]) * v[n + k];
        if (std::sqrt(e / 64.0) < 0.1) {
          if (!found) first = n;
          found = true;
          last = n;
        } else if (found) {
          break;
        }
      }
      REQUIRE(found);
      return 0.5 * static_cast<double>(first + last);
    };
    for (const pitchShifter::QUALITY q : {pitchShifter::BASIC, pitchShifter::BETTER}) {
      CAPTURE(q);
      // The phase vocoder smears a gap over a frame: a longer one.
      std::vector<float> input = Sine(192 * N, 450.0);
      const std::size_t gap = 64 * N;
      const std::size_t length = q == pitchShifter::BASIC ? 512 : 4096;
      for (std::size_t n = gap; n < gap + length; ++n)
        input[n] = 0.f;
      retune fn(q);
      const std::vector<float> out = Run(fn, input);
      // The latency once the tone has settled, as corrected.
      const double latency = fn.latency();
      CHECK(latency > (q == pitchShifter::BASIC ? 130.0 : 2000.0));
      CHECK(fn.correction() == doctest::Approx(-Cents(450.0)).epsilon(0.01));
      const double moved = Gap(out, gap - 256) - Gap(input, gap - 256);
      CAPTURE(moved);
      CHECK(std::abs(moved - latency) < 64.0);
    }
  }

  TEST_CASE("retune: the pitch output carries the detected pitch, also while not retuning, and "
            "follows the live sample rate (#1154)") {
    TestHelpers::ScopedSampleRate rate(48000);
    const std::vector<float> input = Sine(32 * N, 450.0);
    retune fn;
    std::vector<float> pitch;
    Run(fn, input, nullptr, nullptr, &pitch);
    CHECK(std::all_of(pitch.begin(), pitch.end(), [](float p) { return p == 0.f; }));

    retune detector;
    detector.pitchDetection(true).enabled(false);
    pitch.clear();
    const std::vector<float> out = Run(detector, input, nullptr, nullptr, &pitch);
    CHECK(out == input);
    CHECK(pitch.back() == doctest::Approx(450.0).epsilon(0.001));
    CHECK(pitch.front() == 0.f);
    // A step at each analysis: constant between them.
    std::size_t changes = 0;
    for (std::size_t n = 1; n < pitch.size(); ++n)
      if (pitch[n] != pitch[n - 1]) changes++;
    CHECK(changes <= pitch.size() / retune::DEFAULT_PERIOD + 1);

    // Constructed at 48 kHz, run at 96 kHz: 450 Hz still goes to 440.
    retune later;
    {
      TestHelpers::ScopedSampleRate faster(96000);
      const std::vector<float> fast = Run(later, Sine(160 * N, 450.0, 96000.0));
      CHECK(later.frequency() == doctest::Approx(450.0).epsilon(0.001));
      CHECK(std::abs(Loudest(fast, 400.0, 500.0, 96000.0) - 440.0) < 0.002 * 440.0);
    }
  }

  TEST_CASE("retune: non-finite input stays out of the output (#1154)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::vector<float> input = Sine(256 * N, 450.0);
    input[100] = std::numeric_limits<float>::quiet_NaN();
    input[5000] = std::numeric_limits<float>::infinity();
    retune fn;
    const std::vector<float> out = Run(fn, input);
    CHECK(std::all_of(out.begin(), out.end(), [](float v) { return std::isfinite(v); }));
    CHECK(std::abs(Loudest(out, 400.0, 500.0) - 440.0) < 0.002 * 440.0);
  }

  TEST_CASE("retune: processing, the setters and a new tuning allocate nothing (#1154)") {
    retune fn;
    buffer in(N);
    buffer lock(N);
    buffer detune(N);
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = 0.5f * std::sin(static_cast<float>(i) * 0.06f);
    lock = 0.f;
    detune = 10.f;
    const tuning major = AMajor();
    fn(in);
    {
      TestHelpers::ProbeScope probe;
      for (int k = 0; k < 60; ++k) {
        fn.amount(static_cast<float>(k))
            .threshold(static_cast<float>(k % 7))
            .ambience(0.2f)
            .bypass(50.f)
            .period(static_cast<UInt>(16 * (k % 5)))
            .quality(static_cast<pitchShifter::QUALITY>(k % 4))
            .enabled(k % 9 != 8)
            .pitchDetection(k % 2 == 0);
        if (k % 10 == 0) fn.notes(major);
        fn.notes().midiNote(60 + k, k % 3).enableAll();
        (void)fn.latency();
        fn(in);
        fn(in, lock, detune);
      }
      fn.reset();
      fn(in);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }
}

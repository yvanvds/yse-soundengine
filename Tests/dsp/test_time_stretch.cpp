// Tests for YSE::DSP::timeStretch (YseEngine/dsp/sampling/timeStretch.hpp),
// the offline stretcher behind ~stretch (issue #1152), and the
// sampleBuffer::setContents write-back it publishes with:
//  - the setters and their limits, the output length, the frame sizes;
//  - a sine stretched 2x (and 0.5x, 3x) has that length and the same pitch
//    and level, in every quality and every mode;
//  - +12 semitones at stretch 1 keeps the length and doubles the frequency,
//    -12 halves it, +7 with a stretch of 1.5 does both, and nothing aliases
//    going up;
//  - stretch and pitch 1 copy the samples bit for bit;
//  - a click train keeps its clicks, twice as far apart, where onsets are
//    kept;
//  - pitch correction pulls 450 Hz onto 440 (and onto the A of 442);
//  - a buffer at 22.05 kHz keeps its pitch, at its own rate;
//  - process(): every channel, the rate and file details kept, in place;
//  - progress rises to 1 and returning false stops with the target
//    untouched; an empty source is refused.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>
#include "dsp/sampling/sampleBuffer.hpp"
#include "dsp/sampling/timeStretch.hpp"
#include "headers/constants.hpp"
#include "support/audio_helpers.hpp"

using YSE::DSP::sampleBuffer;
using YSE::DSP::timeStretch;

namespace {
  constexpr double PI = 3.14159265358979323846;
  constexpr double RATE = 48000.0;

  std::vector<float> Sine(double hz, std::size_t frames, double rate = RATE, double level = 0.5) {
    std::vector<float> v(frames);
    for (std::size_t i = 0; i < frames; ++i)
      v[i] = static_cast<float>(level * std::sin(2.0 * PI * hz * static_cast<double>(i) / rate));
    return v;
  }

  // The middle of `v`, away from the edges' fades.
  std::vector<float> Middle(const std::vector<float>& v) {
    const std::size_t quarter = v.size() / 4;
    return std::vector<float>(v.begin() + static_cast<std::ptrdiff_t>(quarter),
                              v.end() - static_cast<std::ptrdiff_t>(quarter));
  }

  // The amplitude of `v` at `hz`, Hann-windowed.
  double Amplitude(const std::vector<float>& v, double hz, double rate = RATE) {
    double re = 0.0;
    double im = 0.0;
    double sum = 0.0;
    const double w = 2.0 * PI * hz / rate;
    for (std::size_t n = 0; n < v.size(); ++n) {
      const double h =
          0.5 - 0.5 * std::cos(2.0 * PI * static_cast<double>(n) / static_cast<double>(v.size()));
      re += h * v[n] * std::cos(w * static_cast<double>(n));
      im -= h * v[n] * std::sin(w * static_cast<double>(n));
      sum += h;
    }
    return 2.0 * std::hypot(re, im) / sum;
  }

  // The loudest frequency of `v` between `low` and `high` Hz, to 0.05 Hz.
  double Loudest(const std::vector<float>& v, double low, double high, double rate = RATE) {
    double best = low;
    double level = -1.0;
    const auto steps = static_cast<int>((high - low) / 2.0);
    for (int i = 0; i <= steps; ++i) {
      const double hz = low + 2.0 * i;
      const double a = Amplitude(v, hz, rate);
      if (a > level) {
        level = a;
        best = hz;
      }
    }
    const double centre = best;
    for (int i = 0; i <= 80; ++i) {
      const double hz = centre - 2.0 + 0.05 * i;
      const double a = Amplitude(v, hz, rate);
      if (a > level) {
        level = a;
        best = hz;
      }
    }
    return best;
  }

  double Rms(const std::vector<float>& v) {
    double sum = 0.0;
    for (const float x : v)
      sum += static_cast<double>(x) * x;
    return v.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(v.size()));
  }

  std::vector<float> Render(const timeStretch& ts, const std::vector<float>& in,
                            double rate = RATE) {
    std::vector<float> out;
    REQUIRE(ts.render(in.data(), static_cast<UInt>(in.size()), static_cast<Flt>(rate), out));
    return out;
  }

  // The pitch and level of a 440 Hz sine after `ts`, against the expected
  // frequency.
  void CheckSine(const timeStretch& ts, double expectedHz, double inputHz = 440.0) {
    const std::vector<float> in = Sine(inputHz, 48000);
    const std::vector<float> out = Render(ts, in);
    CHECK(out.size() == ts.outputFrames(48000));
    const std::vector<float> mid = Middle(out);
    const double found = Loudest(mid, expectedHz * 0.8, expectedHz * 1.2);
    CHECK(std::abs(found - expectedHz) < 0.002 * expectedHz);
    // The level of the tone, and nothing much elsewhere.
    CHECK(Amplitude(mid, expectedHz) == doctest::Approx(0.5).epsilon(0.1));
    CHECK(Rms(mid) == doctest::Approx(0.5 / std::sqrt(2.0)).epsilon(0.1));
    if (std::abs(expectedHz - inputHz) > 20.0) CHECK(Amplitude(mid, inputHz) < 0.01);
  }

  struct progressLog {
    std::vector<float> values;
    float stopAt = 2.f; // return false once this far
    static bool Record(Flt done, void* self) {
      auto* log = static_cast<progressLog*>(self);
      log->values.push_back(done);
      return done < log->stopAt;
    }
  };
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("timeStretch: setters, limits and the output length (#1152)") {
    timeStretch ts;
    CHECK(ts.stretch() == 1.f);
    CHECK(ts.pitch() == 1.f);
    CHECK(ts.quality() == timeStretch::BETTER);
    CHECK(ts.mode() == timeStretch::MODE_BASIC);
    CHECK_FALSE(ts.pitchCorrection());
    CHECK(ts.standardPitch() == 440.f);

    ts.stretch(2.f);
    CHECK(ts.stretch() == 2.f);
    ts.stretch(100.f);
    CHECK(ts.stretch() == timeStretch::MAX_STRETCH);
    ts.stretch(0.001f);
    CHECK(ts.stretch() == timeStretch::MIN_STRETCH);
    ts.stretch(0.f).stretch(-1.f).stretch(std::numeric_limits<float>::quiet_NaN());
    CHECK(ts.stretch() == timeStretch::MIN_STRETCH);

    ts.pitch(1.5f);
    CHECK(ts.pitch() == 1.5f);
    ts.pitch(10.f);
    CHECK(ts.pitch() == timeStretch::MAX_PITCH);
    ts.pitch(0.1f);
    CHECK(ts.pitch() == timeStretch::MIN_PITCH);
    ts.cents(1200.f);
    CHECK(ts.pitch() == doctest::Approx(2.f));
    ts.cents(-700.f);
    CHECK(ts.pitch() == doctest::Approx(std::pow(2.0, -7.0 / 12.0)));
    ts.cents(std::numeric_limits<float>::infinity());
    CHECK(ts.pitch() == doctest::Approx(std::pow(2.0, -7.0 / 12.0)));

    ts.standardPitch(442.f);
    CHECK(ts.standardPitch() == 442.f);
    ts.standardPitch(300.f);
    CHECK(ts.standardPitch() == 400.f);
    ts.standardPitch(600.f);
    CHECK(ts.standardPitch() == 500.f);
    ts.pitchCorrection(true);
    CHECK(ts.pitchCorrection());

    ts.quality(timeStretch::BEST).mode(timeStretch::PADS);
    CHECK(ts.quality() == timeStretch::BEST);
    CHECK(ts.mode() == timeStretch::PADS);
    ts.quality(static_cast<timeStretch::QUALITY>(9)).mode(static_cast<timeStretch::MODE>(-1));
    CHECK(ts.quality() == timeStretch::BEST);
    CHECK(ts.mode() == timeStretch::PADS);

    timeStretch len;
    CHECK(len.outputFrames(0) == 0);
    CHECK(len.outputFrames(1000) == 1000);
    len.stretch(2.f);
    CHECK(len.outputFrames(1000) == 2000);
    len.stretch(0.0625f);
    CHECK(len.outputFrames(1000) == 63); // 62.5 rounded
    CHECK(len.outputFrames(1) == 1);
  }

  TEST_CASE("timeStretch: frame sizes follow the quality, the mode and the buffer's rate "
            "(#1152)") {
    timeStretch ts;
    CHECK(ts.frameSize(48000.f) == 2048);
    CHECK(ts.overlap() == 8);
    CHECK(ts.frameSize(96000.f) == 4096);
    CHECK(ts.frameSize(44100.f) == 2048);
    CHECK(ts.frameSize(22050.f) == 1024);
    CHECK(ts.frameSize(0.f) == 2048); // no rate: 48 kHz
    ts.quality(timeStretch::BASIC);
    CHECK(ts.frameSize(48000.f) == 1024);
    CHECK(ts.overlap() == 4);
    ts.quality(timeStretch::GOOD);
    CHECK(ts.frameSize(48000.f) == 2048);
    CHECK(ts.overlap() == 4);
    ts.quality(timeStretch::BEST);
    CHECK(ts.frameSize(48000.f) == 4096);
    CHECK(ts.overlap() == 8);
    ts.quality(timeStretch::BETTER);
    ts.mode(timeStretch::MONOPHONIC);
    CHECK(ts.frameSize(48000.f) == 1024);
    ts.mode(timeStretch::POLYPHONIC);
    CHECK(ts.frameSize(48000.f) == 4096);
    ts.mode(timeStretch::EXTREME);
    CHECK(ts.frameSize(48000.f) == 8192);
    ts.mode(timeStretch::EFFICIENT);
    CHECK(ts.frameSize(48000.f) == 1024);
    CHECK(ts.overlap() == 4);
  }

  TEST_CASE("timeStretch: a sine stretched keeps its pitch and level, in every quality and mode "
            "(#1152)") {
    for (const float factor : {2.f, 0.5f, 3.f}) {
      CAPTURE(factor);
      timeStretch ts;
      ts.stretch(factor);
      CheckSine(ts, 440.0);
    }
    for (int q = timeStretch::BASIC; q <= timeStretch::BEST; ++q) {
      CAPTURE(q);
      timeStretch ts;
      ts.stretch(2.f).quality(static_cast<timeStretch::QUALITY>(q));
      CheckSine(ts, 440.0);
    }
    for (int m = timeStretch::MODE_BASIC; m <= timeStretch::EFFICIENT; ++m) {
      CAPTURE(m);
      timeStretch ts;
      ts.stretch(2.f).mode(static_cast<timeStretch::MODE>(m));
      CheckSine(ts, 440.0);
    }
    // Strong compression and a long stretch.
    timeStretch shorter;
    shorter.stretch(0.125f);
    const std::vector<float> in = Sine(440.0, 96000);
    const std::vector<float> out = Render(shorter, in);
    CHECK(out.size() == 12000);
    CHECK(std::abs(Loudest(Middle(out), 350.0, 530.0) - 440.0) < 0.003 * 440.0);
    timeStretch longer;
    longer.stretch(8.f);
    const std::vector<float> slow = Render(longer, Sine(440.0, 12000));
    CHECK(slow.size() == 96000);
    CHECK(std::abs(Loudest(Middle(slow), 350.0, 530.0) - 440.0) < 0.002 * 440.0);
    CHECK(Amplitude(Middle(slow), 440.0) == doctest::Approx(0.5).epsilon(0.1));
  }

  TEST_CASE("timeStretch: a pitch shift keeps the length and moves the frequency (#1152)") {
    {
      timeStretch up;
      up.cents(1200.f);
      CheckSine(up, 880.0);
    }
    {
      timeStretch down;
      down.cents(-1200.f);
      CheckSine(down, 220.0);
    }
    {
      timeStretch fifth;
      fifth.cents(700.f).stretch(1.5f);
      CheckSine(fifth, 440.0 * std::pow(2.0, 7.0 / 12.0));
    }
    {
      timeStretch both;
      both.pitch(0.75f).stretch(0.5f);
      CheckSine(both, 330.0);
    }
    // Going up two octaves, a 9 kHz tone would land above Nyquist: it is
    // cleared rather than folded back down.
    timeStretch high;
    high.pitch(4.f);
    const std::vector<float> in = Sine(9000.0, 48000);
    const std::vector<float> out = Render(high, in);
    CHECK(out.size() == 48000);
    CHECK(Rms(Middle(out)) < 0.01);
  }

  TEST_CASE("timeStretch: stretch and pitch 1 copy the samples bit for bit (#1152)") {
    timeStretch ts;
    std::vector<float> in = Sine(440.0, 5000);
    in[17] = 0.9f;
    const std::vector<float> out = Render(ts, in);
    CHECK(out == in);
  }

  TEST_CASE("timeStretch: where onsets are kept, drum hits stay sharp and land at twice their "
            "time; elsewhere they smear (#1152)") {
    // A decaying 2 kHz hit every 6000 samples (8 a second), from 3000 on,
    // over a quiet 300 Hz tone.
    std::vector<float> in = Sine(300.0, 48000, RATE, 0.05);
    for (std::size_t start = 3000; start < in.size(); start += 6000) {
      for (std::size_t i = 0; i < 600 && start + i < in.size(); ++i)
        in[start + i] +=
            static_cast<float>(0.8 * std::exp(-static_cast<double>(i) / 120.0) *
                               std::sin(2.0 * PI * 2000.0 * static_cast<double>(i) / RATE));
    }
    const auto loudest = [](const std::vector<float>& v, std::size_t from, std::size_t to) {
      float peak = 0.f;
      for (std::size_t i = from; i < to; ++i)
        peak = std::max(peak, std::abs(v[i]));
      return peak;
    };
    for (const auto mode : {timeStretch::RHYTHMIC, timeStretch::GENERAL, timeStretch::POLYPHONIC}) {
      CAPTURE(mode);
      timeStretch ts;
      ts.stretch(2.f).mode(mode);
      const std::vector<float> out = Render(ts, in);
      REQUIRE(out.size() == 96000);
      for (std::size_t hit = 18000; hit <= 78000; hit += 12000) {
        CAPTURE(hit);
        // The hit, copied where twice its time puts it ...
        CHECK(loudest(out, hit, hit + 256) > 0.65f);
        // ... and nothing of it before: only the tone.
        CHECK(loudest(out, hit - 1500, hit - 64) < 0.1f);
      }
      // The tone around the hits is still there, at its pitch (where a
      // copy starts, it dips for a moment, so its level is not exact).
      CHECK(Amplitude(Middle(out), 300.0) > 0.035);
      CHECK(Amplitude(Middle(out), 300.0) < 0.06);
    }
    // Without onset handling, a hit spreads over a frame and loses its peak.
    timeStretch smeared;
    smeared.stretch(2.f);
    const std::vector<float> out = Render(smeared, in);
    for (std::size_t hit = 18000; hit <= 78000; hit += 12000) {
      CAPTURE(hit);
      CHECK(loudest(out, hit - 1500, hit + 1500) < 0.5f);
    }
  }

  TEST_CASE("timeStretch: pitch correction pulls a detuned sine onto the nearest semitone "
            "(#1152)") {
    TestHelpers::ScopedSampleRate rate(48000);
    timeStretch ts;
    ts.pitchCorrection(true);
    const std::vector<float> in = Sine(450.0, 96000);
    const std::vector<float> out = Render(ts, in);
    CHECK(out.size() == 96000);
    const std::vector<float> mid = Middle(out);
    CHECK(std::abs(Loudest(mid, 400.0, 500.0) - 440.0) < 0.003 * 440.0);
    // On the A of 442 instead.
    ts.standardPitch(442.f);
    const std::vector<float> retuned = Middle(Render(ts, in));
    CHECK(std::abs(Loudest(retuned, 400.0, 500.0) - 442.0) < 0.003 * 442.0);
    // Stretched and corrected at once: twice as long, still on A.
    ts.standardPitch(440.f).stretch(2.f);
    const std::vector<float> both = Render(ts, in);
    CHECK(both.size() == 192000);
    CHECK(std::abs(Loudest(Middle(both), 400.0, 500.0) - 440.0) < 0.003 * 440.0);
  }

  TEST_CASE("timeStretch: a buffer at its own rate keeps its pitch, and corrects to the same "
            "notes (#1152)") {
    TestHelpers::ScopedSampleRate rate(48000);
    constexpr double OWN = 22050.0;
    const std::vector<float> in = Sine(440.0, 22050, OWN);
    timeStretch ts;
    ts.stretch(2.f);
    const std::vector<float> out = Render(ts, in, OWN);
    CHECK(out.size() == 44100);
    CHECK(std::abs(Loudest(Middle(out), 350.0, 530.0, OWN) - 440.0) < 0.002 * 440.0);
    // 450 Hz at 22.05 kHz, corrected while the engine runs at 48 kHz: A.
    timeStretch corrected;
    corrected.pitchCorrection(true);
    const std::vector<float> fixed = Render(corrected, Sine(450.0, 44100, OWN), OWN);
    CHECK(std::abs(Loudest(Middle(fixed), 400.0, 500.0, OWN) - 440.0) < 0.003 * 440.0);
  }

  TEST_CASE("timeStretch: process() stretches every channel into the target in one change, "
            "keeping its rate (#1152)") {
    auto source = sampleBuffer::create(2, 24000, 24000.f);
    const std::vector<float> left = Sine(300.0, 24000, 24000.0);
    const std::vector<float> right = Sine(500.0, 24000, 24000.0);
    source->setSamples(0, 0, left.data(), 24000);
    source->setSamples(1, 0, right.data(), 24000);

    auto target = sampleBuffer::create(1, 10, 32000.f);
    const std::uint64_t before = target->version();
    timeStretch ts;
    ts.stretch(1.5f).cents(1200.f);
    progressLog log;
    REQUIRE(ts.process(*source, *target, &progressLog::Record, &log));
    CHECK(target->version() == before + 1); // one change
    CHECK(target->channels() == 2);
    CHECK(target->frames() == 36000);
    CHECK(target->sampleRate() == 32000.f); // the target's rate stays
    std::vector<float> out(36000);
    REQUIRE(target->getSamples(0, 0, out.data(), 36000) == 36000);
    CHECK(std::abs(Loudest(Middle(out), 500.0, 700.0, 24000.0) - 600.0) < 0.002 * 600.0);
    REQUIRE(target->getSamples(1, 0, out.data(), 36000) == 36000);
    CHECK(std::abs(Loudest(Middle(out), 900.0, 1100.0, 24000.0) - 1000.0) < 0.002 * 1000.0);

    // Progress rises to 1, and 1 comes last.
    REQUIRE(log.values.size() > 4);
    CHECK(std::is_sorted(log.values.begin(), log.values.end()));
    CHECK(log.values.front() >= 0.f);
    CHECK(log.values.back() == 1.f);
    CHECK(std::count(log.values.begin(), log.values.end(), 1.f) == 1);

    // In place: the source is its own target.
    REQUIRE(ts.process(*source, *source));
    CHECK(source->frames() == 36000);
    CHECK(source->channels() == 2);
    CHECK(source->sampleRate() == 24000.f);
  }

  TEST_CASE("timeStretch: a progress function that returns false stops it, and the target is "
            "untouched (#1152)") {
    auto buffer = sampleBuffer::create(1, 48000, 48000.f);
    const std::vector<float> in = Sine(440.0, 48000);
    buffer->setSamples(0, 0, in.data(), 48000);
    const std::uint64_t before = buffer->version();

    timeStretch ts;
    ts.stretch(2.f).pitchCorrection(true);
    for (const float stop : {0.f, 0.3f, 0.8f, 0.999f}) {
      CAPTURE(stop);
      progressLog log;
      log.stopAt = stop;
      CHECK_FALSE(ts.process(*buffer, *buffer, &progressLog::Record, &log));
      CHECK(log.values.back() >= stop);
      CHECK(log.values.back() < 1.f);
      CHECK(buffer->version() == before);
      CHECK(buffer->frames() == 48000);
      std::vector<float> now(48000);
      REQUIRE(buffer->getSamples(0, 0, now.data(), 48000) == 48000);
      CHECK(now == in);
    }

    // render() stops the same way and leaves its output alone.
    progressLog log;
    log.stopAt = 0.5f;
    std::vector<float> out(3, 7.f);
    CHECK_FALSE(ts.render(in.data(), 48000, 48000.f, out, &progressLog::Record, &log));
    CHECK(out == std::vector<float>(3, 7.f));
  }

  TEST_CASE("timeStretch: an empty source is refused and the target kept (#1152)") {
    auto empty = sampleBuffer::create(1, 0);
    auto target = sampleBuffer::create(1, 100);
    const std::uint64_t before = target->version();
    timeStretch ts;
    ts.stretch(2.f);
    CHECK_FALSE(ts.process(*empty, *target));
    CHECK(target->version() == before);
    CHECK(target->frames() == 100);
    std::vector<float> out;
    CHECK_FALSE(ts.render(nullptr, 100, 48000.f, out));
    const float one = 1.f;
    CHECK_FALSE(ts.render(&one, 0, 48000.f, out));
  }

  TEST_CASE("sampleBuffer: setContents replaces shape and samples in one change and keeps the "
            "rate (#1152)") {
    auto b = sampleBuffer::create(1, 4, 22050.f);
    const std::uint64_t before = b->version();
    const float planar[6] = {1.f, 2.f, 3.f, 4.f, 5.f, 6.f};
    CHECK(b->setContents(2, 3, planar) == before + 1);
    CHECK(b->version() == before + 1);
    CHECK(b->channels() == 2);
    CHECK(b->frames() == 3);
    CHECK(b->sampleRate() == 22050.f);
    float out[3] = {};
    REQUIRE(b->getSamples(1, 0, out, 3) == 3);
    CHECK(out[0] == 4.f);
    CHECK(out[2] == 6.f);
    // No samples: silence of that shape.
    b->setContents(1, 5, nullptr);
    CHECK(b->frames() == 5);
    float silent[5] = {9.f, 9.f, 9.f, 9.f, 9.f};
    REQUIRE(b->getSamples(0, 0, silent, 5) == 5);
    CHECK(silent[4] == 0.f);
  }

} // TEST_SUITE("dsp")

// Tests for YSE::DSP::cycle (YseEngine/dsp/generators/cycle.hpp), issue
// #1054. Reference output is closed form: at frequency f and phase input q
// the sample n of an oscillator starting at phase p0 is
// cos(2 pi (p0 + n f / SAMPLERATE + q)), or, for a custom table, the table
// linearly interpolated at that phase.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>
#include "dsp/generators/cycle.hpp"
#include "dsp/sampling/sampleBuffer.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"

namespace {

  constexpr double kTwoPi = 6.283185307179586;

  // Restores the engine rate whatever the test does, so the shared test binary
  // is not left running at another rate.
  struct SampleRateGuard {
    UInt saved;
    SampleRateGuard() : saved(YSE::SAMPLERATE) {}
    ~SampleRateGuard() {
      YSE::SAMPLERATE = saved;
    }
  };

  double Cosine(double cycles) {
    return std::cos(kTwoPi * cycles);
  }

  double Step(double frequency) {
    return frequency / static_cast<double>(YSE::SAMPLERATE);
  }

  bool Near(double a, double b, double tolerance = 1e-5) {
    return std::fabs(a - b) <= tolerance;
  }

} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("cycle: a constant frequency produces the closed-form cosine (#1054)") {
    YSE::DSP::cycle osc;
    const float frequency = static_cast<float>(YSE::SAMPLERATE) / 100.f; // 100 samples a cycle
    unsigned int n = 0;
    for (int block = 0; block < 4; ++block) {
      YSE::DSP::buffer& out = osc(frequency);
      REQUIRE(out.getLength() == YSE::STANDARD_BUFFERSIZE);
      for (unsigned int i = 0; i < out.getLength(); ++i, ++n) {
        CAPTURE(n);
        CHECK(Near(out.getPtr()[i], Cosine(n * Step(frequency))));
      }
    }
  }

  TEST_CASE("cycle: the built-in table matches a true cosine at any phase (#1054)") {
    // 16384 points with linear interpolation: the error is about 2e-8, far
    // below a float's resolution at 1. An irrational-ish step visits phases
    // between the table points.
    YSE::DSP::cycle osc;
    const float frequency = static_cast<float>(YSE::SAMPLERATE) * 0.0123457f;
    double worst = 0.0;
    unsigned int n = 0;
    for (int block = 0; block < 40; ++block) {
      YSE::DSP::buffer& out = osc(frequency);
      for (unsigned int i = 0; i < out.getLength(); ++i, ++n) {
        const double expected = Cosine(n * Step(frequency));
        worst = std::max(worst, std::fabs(out.getPtr()[i] - expected));
      }
    }
    CHECK(worst < 2e-6);
    CHECK(osc.tableSize() == YSE::DSP::cycle::TABLE_SIZE);
  }

  TEST_CASE("cycle: a constant phase offset shifts the waveform (#1054)") {
    YSE::DSP::cycle osc;
    YSE::DSP::buffer& out = osc(0.f, 0.25f);
    CHECK(Near(out.getPtr()[0], 0.0)); // cos(pi / 2)
    YSE::DSP::buffer& half = osc(0.f, 0.5f);
    CHECK(Near(half.getPtr()[0], -1.0));
    // Phase is wrapped: 1.25 and -0.75 both mean 0.25.
    CHECK(Near(osc(0.f, 1.25f).getPtr()[0], 0.0));
    CHECK(Near(osc(0.f, -0.75f).getPtr()[0], 0.0));
    // The offset does not move the running phase.
    CHECK(osc.phase() == doctest::Approx(0.f));
  }

  TEST_CASE("cycle: a phase signal is added to the running phase sample by sample (#1054)") {
    YSE::DSP::cycle osc;
    const float frequency = static_cast<float>(YSE::SAMPLERATE) / 64.f;
    YSE::DSP::buffer phase(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < phase.getLength(); ++i)
      phase.getPtr()[i] = 0.1f * std::sin(static_cast<float>(i) * 0.3f);

    YSE::DSP::buffer& out = osc(frequency, phase);
    REQUIRE(out.getLength() == phase.getLength());
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      CAPTURE(i);
      CHECK(Near(out.getPtr()[i], Cosine(i * Step(frequency) + phase.getPtr()[i])));
    }
  }

  TEST_CASE("cycle: frequency and phase signals together (#1054)") {
    YSE::DSP::cycle osc;
    YSE::DSP::buffer frequency(YSE::STANDARD_BUFFERSIZE);
    YSE::DSP::buffer phase(64);
    // First half still, second half at 100 samples a cycle.
    for (unsigned int i = 0; i < frequency.getLength(); ++i)
      frequency.getPtr()[i] = i < 32 ? 0.f : static_cast<float>(YSE::SAMPLERATE) / 100.f;
    phase = 0.5f;

    YSE::DSP::buffer& out = osc(frequency, phase);
    CHECK(out.getLength() == 64); // the shorter input
    double running = 0.0;
    for (unsigned int i = 0; i < out.getLength(); ++i) {
      CAPTURE(i);
      CHECK(Near(out.getPtr()[i], Cosine(running + 0.5)));
      running += Step(frequency.getPtr()[i]);
    }
  }

  TEST_CASE("cycle: a frequency signal alone (#1054)") {
    YSE::DSP::cycle osc;
    YSE::DSP::buffer frequency(YSE::STANDARD_BUFFERSIZE);
    frequency = static_cast<float>(YSE::SAMPLERATE) / 100.f;
    YSE::DSP::buffer& out = osc(frequency, 0.25f);
    CHECK(Near(out.getPtr()[0], 0.0));
    CHECK(Near(out.getPtr()[25], -1.0)); // 0.25 + 0.25 cycles
  }

  TEST_CASE("cycle: a negative frequency runs backwards; reset() jumps and wraps (#1054)") {
    YSE::DSP::cycle osc;
    osc.reset(0.25f);
    CHECK(osc.phase() == doctest::Approx(0.25f));
    const float frequency = -static_cast<float>(YSE::SAMPLERATE) / 100.f;
    YSE::DSP::buffer& out = osc(frequency);
    CHECK(Near(out.getPtr()[0], 0.0)); // cos(pi / 2)
    CHECK(Near(out.getPtr()[25], 1.0)); // back at phase 0
    CHECK(Near(out.getPtr()[50], 0.0)); // phase 0.75
    CHECK(osc.phase() >= 0.f);
    CHECK(osc.phase() < 1.f);

    osc.reset(1.5f);
    CHECK(osc.phase() == doctest::Approx(0.5f));
    osc.reset(-0.25f);
    CHECK(osc.phase() == doctest::Approx(0.75f));
    CHECK(Near(osc(0.f).getPtr()[0], 0.0));
  }

  TEST_CASE("cycle: a custom table is read with linear interpolation, wrapping (#1054)") {
    YSE::DSP::cycle osc;
    const float table[4] = {0.f, 1.f, 0.f, -1.f};
    osc.table(table, 4);
    CHECK(osc.tableSize() == 4);
    // Eight samples a cycle: two per table point.
    const float frequency = static_cast<float>(YSE::SAMPLERATE) / 8.f;
    YSE::DSP::buffer& out = osc(frequency);
    const float expected[8] = {0.f, 0.5f, 1.f, 0.5f, 0.f, -0.5f, -1.f, -0.5f};
    for (unsigned int i = 0; i < 16; ++i) {
      CAPTURE(i);
      CHECK(Near(out.getPtr()[i], expected[i % 8]));
    }

    osc.cosine();
    osc.reset(0.f);
    CHECK(osc.tableSize() == YSE::DSP::cycle::TABLE_SIZE);
    CHECK(Near(osc(0.f).getPtr()[0], 1.0));
  }

  TEST_CASE("cycle: an empty table is silence while the phase runs on (#1054)") {
    YSE::DSP::cycle osc;
    osc.table(nullptr, 16);
    CHECK(osc.tableSize() == 0);
    const float frequency = static_cast<float>(YSE::SAMPLERATE) / 100.f;
    YSE::DSP::buffer& out = osc(frequency);
    for (unsigned int i = 0; i < out.getLength(); ++i)
      CHECK(out.getPtr()[i] == 0.f);
    CHECK(osc.phase() == doctest::Approx(0.28f).epsilon(1e-4)); // 128 samples on
    const float table[2] = {1.f, 1.f};
    osc.table(table, 0);
    CHECK(osc.tableSize() == 0);
  }

  TEST_CASE("cycle: the first channel of a buffer, from an offset, for a size (#1054)") {
    auto buffer = YSE::DSP::sampleBuffer::create(2, 1024);
    std::vector<float> ramp(1024);
    std::vector<float> other(1024, 9.f);
    for (unsigned int i = 0; i < ramp.size(); ++i)
      ramp[i] = static_cast<float>(i);
    buffer->setSamples(0, 0, ramp.data(), 1024);
    buffer->setSamples(1, 0, other.data(), 1024); // never read

    YSE::DSP::sampleBufferReader reader(buffer);
    YSE::DSP::cycle osc;
    {
      YSE::DSP::sampleView view = reader.pin();
      osc.table(view, 100); // size 0: the default 512
      CHECK(osc.tableSize() == YSE::DSP::cycle::DEFAULT_VIEW_SIZE);
      // A still oscillator at phase 0.5 sits on table point 256: sample 356.
      CHECK(osc(0.f, 0.5f).getPtr()[0] == doctest::Approx(356.f));

      osc.table(view, 100, -1); // to the end of the buffer
      CHECK(osc.tableSize() == 924);
      osc.table(view, 1000, 64); // cut short at the end
      CHECK(osc.tableSize() == 24);
      osc.table(view, 0, 1024); // the whole buffer
      CHECK(osc(0.f, 0.25f).getPtr()[0] == doctest::Approx(256.f));
      osc.table(view, 1024); // past the end: silence
      CHECK(osc.tableSize() == 0);
      CHECK(osc(0.f).getPtr()[0] == 0.f);
    }

    YSE::DSP::sampleBufferReader unbound;
    YSE::DSP::sampleView empty = unbound.pin();
    osc.table(empty, 0);
    CHECK(osc.tableSize() == 0);
  }

  TEST_CASE("cycle: values that are not finite never reach the output (#1054)") {
    YSE::DSP::cycle osc;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    YSE::DSP::buffer frequency(YSE::STANDARD_BUFFERSIZE);
    frequency = 440.f;
    frequency.getPtr()[3] = nan;
    frequency.getPtr()[4] = inf;
    YSE::DSP::buffer phase(YSE::STANDARD_BUFFERSIZE);
    phase = 0.f;
    phase.getPtr()[7] = nan;

    bool finite = true;
    const auto check = [&finite](YSE::DSP::buffer& out) {
      for (unsigned int i = 0; i < out.getLength(); ++i)
        if (!std::isfinite(out.getPtr()[i])) finite = false;
    };
    check(osc(nan));
    check(osc(440.f, inf));
    check(osc(frequency, phase));
    check(osc(frequency));
    check(osc(440.f));
    CHECK(finite);
    CHECK(std::isfinite(osc.phase()));
    osc.reset(nan);
    CHECK(osc.phase() == 0.f);
  }

  TEST_CASE("cycle: follows a sample-rate change after construction (#1054)") {
    SampleRateGuard guard;
    YSE::DSP::cycle osc;
    osc(1000.f); // run once at the construction-time rate

    YSE::SAMPLERATE = 44100;
    osc.reset(0.f);
    CHECK(Near(osc(441.f).getPtr()[50], -1.0)); // 100 samples a cycle at 44.1 kHz

    YSE::SAMPLERATE = 96000;
    osc.reset(0.f);
    CHECK(Near(osc(960.f).getPtr()[50], -1.0)); // 100 samples a cycle at 96 kHz
  }

  TEST_CASE("cycle: every block form, a table change and a pin allocate nothing (#1054)") {
    auto buffer = YSE::DSP::sampleBuffer::create(1, 2048);
    buffer->fillSine();
    YSE::DSP::sampleBufferReader reader(buffer);
    YSE::DSP::cycle osc;
    YSE::DSP::buffer frequency(YSE::STANDARD_BUFFERSIZE);
    frequency = 440.f;
    YSE::DSP::buffer phase(YSE::STANDARD_BUFFERSIZE);
    phase = 0.1f;
    const float table[3] = {1.f, 0.f, -1.f};
    {
      TestHelpers::ProbeScope probe;
      osc(440.f);
      osc(440.f, 0.25f, 32);
      osc(440.f, 0.f, YSE::STANDARD_BUFFERSIZE * 4); // processed up to STANDARD_BUFFERSIZE
      osc(frequency);
      osc(440.f, phase);
      osc(frequency, phase);
      osc.table(table, 3);
      osc(440.f);
      {
        YSE::DSP::sampleView view = reader.pin();
        osc.table(view, 0, -1);
        osc(frequency, phase);
      }
      osc.cosine();
      osc.reset(0.5f);
      osc(440.f);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(osc(440.f, 0.f, YSE::STANDARD_BUFFERSIZE * 4).getLength() == YSE::STANDARD_BUFFERSIZE);
    CHECK(osc(440.f, 0.f, 32).getLength() == 32);
  }

} // TEST_SUITE("dsp")

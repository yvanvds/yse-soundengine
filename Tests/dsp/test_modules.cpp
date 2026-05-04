// Tests for YSE::DSP module classes: sineWave, ringModulator, hilbert, phaser,
// granulator, and difference (FM).
// No audio device required; SAMPLERATE is initialised to 44100 by the
// portaudioDeviceManager translation unit at static-initialisation time.
//
// dspObject::calculateImpact combines dry (in) and wet (filtered) back into
// buffer[0] with default _impact=1.0 / LFO_NONE, so after process() buffer[0]
// holds 100% the processed signal.

#include <doctest/doctest.h>
#include <cmath>
#include <vector>
#include "dsp/modules/sineWave.hpp"
#include "dsp/modules/ringModulator.hpp"
#include "dsp/modules/hilbert.hpp"
#include "dsp/modules/phaser.hpp"
#include "dsp/modules/granulator.hpp"
#include "dsp/modules/fm/difference.hpp"
#include "dsp/fourier/fft.hpp"
#include "headers/defines.hpp"
#include "support/audio_helpers.hpp"

static constexpr float kPi = 3.14159265358979323846f;

TEST_SUITE("dsp") {

// ─── sineWave ─────────────────────────────────────────────────────────────────
//
// sineWave::process(SOUND_STATUS&, Int&) is an overload, not an override, of the
// dspSourceObject pure virtual process(SOUND_STATUS&).  A concrete subclass is
// needed to instantiate it; the extra override is a no-op.

class ConcreteSineWave : public YSE::DSP::sineWave {
public:
    using YSE::DSP::sineWave::process;  // keep sineWave::process(SOUND_STATUS&, Int&) in scope
    void process(YSE::SOUND_STATUS&) override {}
};

TEST_CASE("sineWave: frequency getter/setter round-trip") {
    ConcreteSineWave sw;
    sw.frequency(880.0f);
    CHECK(sw.frequency() == doctest::Approx(880.0f).epsilon(1e-5f));
}

TEST_CASE("sineWave: process with SS_WANTSTOPLAY produces bounded samples") {
    ConcreteSineWave sw;
    sw.frequency(440.0f);
    YSE::SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    Int latency = 0;
    sw.process(intent, latency);

    REQUIRE_FALSE(sw.samples.empty());
    unsigned n = sw.samples[0].getLength();
    REQUIRE(n > 0);
    float* ptr = sw.samples[0].getPtr();
    for (unsigned i = 0; i < n; ++i) {
        CHECK(ptr[i] >= -1.0f);
        CHECK(ptr[i] <=  1.0f);
    }
}

TEST_CASE("sineWave: process transitions intent from SS_WANTSTOPLAY to SS_PLAYING") {
    ConcreteSineWave sw;
    sw.frequency(440.0f);
    YSE::SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    Int latency = 0;
    sw.process(intent, latency);
    CHECK(intent == YSE::SS_PLAYING);
}

TEST_CASE("sineWave: samples have non-zero energy after playing for several buffers") {
    ConcreteSineWave sw;
    sw.frequency(440.0f);
    YSE::SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    Int latency = 0;
    // First call starts playback; subsequent calls produce steady-state output.
    for (int i = 0; i < 5; ++i) sw.process(intent, latency);
    CHECK(TestHelpers::measureRms(sw.samples[0]) > 0.0f);
}

// ─── ringModulator ────────────────────────────────────────────────────────────

TEST_CASE("ringModulator: default frequency is 440 Hz") {
    YSE::DSP::ringModulator rm;
    CHECK(rm.frequency() == doctest::Approx(440.0f).epsilon(1e-5f));
}

TEST_CASE("ringModulator: frequency setter/getter round-trip") {
    YSE::DSP::ringModulator rm;
    rm.frequency(880.0f);
    CHECK(rm.frequency() == doctest::Approx(880.0f).epsilon(1e-5f));
}

TEST_CASE("ringModulator: process produces bounded output for sine input") {
    YSE::DSP::ringModulator rm;
    rm.frequency(440.0f);
    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);
    float* ptr = buf[0].getPtr();
    for (unsigned i = 0; i < 128; ++i)
        ptr[i] = std::sin(2.0f * kPi * 440.0f * static_cast<float>(i) / 44100.0f);
    rm.process(buf);
    float* op = buf[0].getPtr();
    for (unsigned i = 0; i < buf[0].getLength(); ++i) {
        CHECK(op[i] >= -2.0f);
        CHECK(op[i] <=  2.0f);
    }
}

TEST_CASE("ringModulator: ring-mod identity — 440 Hz * 440 Hz has energy at 880 Hz not 440 Hz") {
    // sin(ωt)*sin(ωt) = 0.5*(1 - cos(2ωt)), so dominant AC frequency is 2ω.
    // With N=512 and SR=44100: bin_440≈5, bin_880≈10.
    const unsigned N = 512;
    const float SR = 44100.0f;

    YSE::DSP::ringModulator rm;
    rm.frequency(440.0f);

    MULTICHANNELBUFFER buf(1);
    buf[0].resize(N);
    float* ptr = buf[0].getPtr();
    for (unsigned i = 0; i < N; ++i)
        ptr[i] = std::sin(2.0f * kPi * 440.0f * static_cast<float>(i) / SR);
    rm.process(buf);

    // Compute FFT of the ring-mod output.
    YSE::DSP::buffer re(N), im(N);
    re = buf[0];
    im = 0.0f;
    YSE::DSP::fft f;
    f(re, im);

    const unsigned bin440 = static_cast<unsigned>(std::round(440.0f * N / SR));
    const unsigned bin880 = static_cast<unsigned>(std::round(880.0f * N / SR));
    float* fr = f.getReal().getPtr();
    float* fi = f.getImaginary().getPtr();
    float mag440 = fr[bin440] * fr[bin440] + fi[bin440] * fi[bin440];
    float mag880 = fr[bin880] * fr[bin880] + fi[bin880] * fi[bin880];

    // 880 Hz component must dominate the original 440 Hz component.
    CHECK(mag880 > mag440);
}

// ─── hilbert ─────────────────────────────────────────────────────────────────

TEST_CASE("hilbert: out1 and out2 are non-identical for sine input") {
    YSE::DSP::hilbert h;
    YSE::DSP::buffer in(128), out1(128), out2(128);
    float* ptr = in.getPtr();
    for (unsigned i = 0; i < 128; ++i)
        ptr[i] = std::sin(2.0f * kPi * 440.0f * static_cast<float>(i) / 44100.0f);
    // Warm up to steady state.
    for (int w = 0; w < 20; ++w) h(in, out1, out2);

    bool identical = true;
    float* p1 = out1.getPtr();
    float* p2 = out2.getPtr();
    for (unsigned i = 0; i < 128; ++i) {
        if (std::abs(p1[i] - p2[i]) > 1e-3f) { identical = false; break; }
    }
    CHECK(!identical);
}

TEST_CASE("hilbert: both outputs stay bounded for sine input") {
    YSE::DSP::hilbert h;
    YSE::DSP::buffer in(128), out1(128), out2(128);
    float* ptr = in.getPtr();
    for (unsigned i = 0; i < 128; ++i)
        ptr[i] = std::sin(2.0f * kPi * 440.0f * static_cast<float>(i) / 44100.0f);
    for (int iter = 0; iter < 30; ++iter) h(in, out1, out2);
    float* p1 = out1.getPtr();
    float* p2 = out2.getPtr();
    for (unsigned i = 0; i < 128; ++i) {
        CHECK(std::abs(p1[i]) < 2.0f);
        CHECK(std::abs(p2[i]) < 2.0f);
    }
}

TEST_CASE("hilbert: both outputs have non-trivial RMS energy at steady state") {
    // A Hilbert pair (analytic signal) preserves amplitude: each quadrature
    // component carries approximately the same energy as the input sine.
    YSE::DSP::hilbert h;
    YSE::DSP::buffer in(128), out1(128), out2(128);
    float* ptr = in.getPtr();
    for (unsigned i = 0; i < 128; ++i)
        ptr[i] = std::sin(2.0f * kPi * 440.0f * static_cast<float>(i) / 44100.0f);
    for (int w = 0; w < 30; ++w) h(in, out1, out2);
    float rmsIn   = TestHelpers::measureRms(in);
    float rmsOut1 = TestHelpers::measureRms(out1);
    float rmsOut2 = TestHelpers::measureRms(out2);
    CHECK(rmsOut1 > rmsIn * 0.1f);
    CHECK(rmsOut2 > rmsIn * 0.1f);
}

// ─── phaser ──────────────────────────────────────────────────────────────────

TEST_CASE("phaser: default frequency is 0.3") {
    YSE::DSP::MODULES::phaser p;
    CHECK(p.frequency() == doctest::Approx(0.3f).epsilon(1e-5f));
}

TEST_CASE("phaser: frequency and range setter/getter round-trips") {
    YSE::DSP::MODULES::phaser p;
    p.frequency(0.5f);
    CHECK(p.frequency() == doctest::Approx(0.5f).epsilon(1e-5f));
    p.range(0.2f);
    CHECK(p.range() == doctest::Approx(0.2f).epsilon(1e-5f));
}

TEST_CASE("phaser: range is clamped to [0, 0.5]") {
    YSE::DSP::MODULES::phaser p;
    p.range(2.0f);
    CHECK(p.range() <= 0.5f);
    p.range(-1.0f);
    CHECK(p.range() >= 0.0f);
}

TEST_CASE("phaser: process produces bounded output for sine input") {
    YSE::DSP::MODULES::phaser p;
    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);
    float* ptr = buf[0].getPtr();
    for (int iter = 0; iter < 20; ++iter) {
        for (unsigned i = 0; i < 128; ++i)
            ptr[i] = std::sin(2.0f * kPi * 440.0f * static_cast<float>(i) / 44100.0f);
        p.process(buf);
    }
    float* op = buf[0].getPtr();
    for (unsigned i = 0; i < 128; ++i)
        CHECK(std::abs(op[i]) < 5.0f);
}

TEST_CASE("phaser: output RMS is non-zero for non-silent input") {
    YSE::DSP::MODULES::phaser p;
    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);
    float* ptr = buf[0].getPtr();
    // Warm up, then check steady-state RMS.
    for (int iter = 0; iter < 10; ++iter) {
        for (unsigned i = 0; i < 128; ++i)
            ptr[i] = std::sin(2.0f * kPi * 440.0f * static_cast<float>(i) / 44100.0f);
        p.process(buf);
    }
    CHECK(TestHelpers::measureRms(buf[0]) > 0.0f);
}

// ─── granulator ──────────────────────────────────────────────────────────────

TEST_CASE("granulator: process completes without crash on repeated calls") {
    YSE::DSP::MODULES::granulator g;
    g.grainFrequency(10);
    g.grainLength(2000);
    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);
    for (int iter = 0; iter < 30; ++iter) {
        float* ptr = buf[0].getPtr();
        for (unsigned i = 0; i < 128; ++i)
            ptr[i] = std::sin(2.0f * kPi * 440.0f * static_cast<float>(i) / 44100.0f);
        g.process(buf);
    }
    CHECK(true);  // reaching here means no crash or assertion failure
}

TEST_CASE("granulator: output stays bounded after repeated processing") {
    YSE::DSP::MODULES::granulator g;
    g.grainFrequency(10);
    g.grainLength(2000);
    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);
    bool bounded = true;
    for (int iter = 0; iter < 30; ++iter) {
        float* ptr = buf[0].getPtr();
        for (unsigned i = 0; i < 128; ++i)
            ptr[i] = std::sin(2.0f * kPi * 440.0f * static_cast<float>(i) / 44100.0f);
        g.process(buf);
        float* op = buf[0].getPtr();
        for (unsigned i = 0; i < buf[0].getLength(); ++i) {
            if (std::abs(op[i]) > 10.0f) { bounded = false; break; }
        }
        if (!bounded) break;
    }
    CHECK(bounded);
}

TEST_CASE("granulator: setter/getter round-trips for grain parameters") {
    YSE::DSP::MODULES::granulator g;
    g.grainFrequency(5);
    CHECK(g.grainFrequency() == 5u);
    g.grainLength(1000);
    CHECK(g.grainLength() == 1000u);
    g.grainTranspose(0.5f);
    CHECK(g.grainTranspose() == doctest::Approx(0.5f).epsilon(1e-5f));
    g.gain(0.8f);
    CHECK(g.gain() == doctest::Approx(0.8f).epsilon(1e-5f));
}

// ─── difference (FM) ─────────────────────────────────────────────────────────

TEST_CASE("difference: default frequency is 400 Hz") {
    YSE::DSP::MODULES::difference d;
    CHECK(d.frequency() == doctest::Approx(400.0f).epsilon(1e-5f));
}

TEST_CASE("difference: frequency and amplitude setter/getter round-trips") {
    YSE::DSP::MODULES::difference d;
    d.frequency(600.0f);
    CHECK(d.frequency() == doctest::Approx(600.0f).epsilon(1e-5f));
    d.amplitude(0.8f);
    CHECK(d.amplitude() == doctest::Approx(0.8f).epsilon(1e-5f));
}

TEST_CASE("difference: output is clipped to [-1, 1] for sine input") {
    // difference::process applies a clip(-1, 1) to the output.
    YSE::DSP::MODULES::difference d;
    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);
    float* ptr = buf[0].getPtr();
    for (unsigned i = 0; i < 128; ++i)
        ptr[i] = std::sin(2.0f * kPi * 440.0f * static_cast<float>(i) / 44100.0f);
    d.process(buf);
    float* op = buf[0].getPtr();
    for (unsigned i = 0; i < buf[0].getLength(); ++i) {
        CHECK(op[i] >= -1.0f);
        CHECK(op[i] <=  1.0f);
    }
}

TEST_CASE("difference: output energy is non-zero for sine input") {
    YSE::DSP::MODULES::difference d;
    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);
    float* ptr = buf[0].getPtr();
    // Multiple calls let createIfNeeded and the internal sine settle.
    for (int iter = 0; iter < 5; ++iter) {
        for (unsigned i = 0; i < 128; ++i)
            ptr[i] = std::sin(2.0f * kPi * 440.0f * static_cast<float>(i) / 44100.0f);
        d.process(buf);
    }
    CHECK(TestHelpers::measureRms(buf[0]) > 0.0f);
}

} // TEST_SUITE("dsp")

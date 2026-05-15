// Tests for the DSP filter MODULES wrappers — lowPassFilter, highPassFilter,
// bandPassFilter, sweepFilter.  These wrap the raw dsp/filters.hpp primitives
// with a dspObject interface (createIfNeeded + calculateImpact).
//
// With default _impact=1.0 and LFO_NONE, calculateImpact replaces buffer[0]
// with the fully wet (filtered) signal, so each process() call leaves buffer[0]
// holding 100 % filter output.
//
// No audio device required — SAMPLERATE is initialised to 44100 by the
// portaudioDeviceManager translation unit at static-initialisation time.

#include <doctest/doctest.h>
#include <cmath>
#include "dsp/modules/filters/lowpass.hpp"
#include "dsp/modules/filters/highpass.hpp"
#include "dsp/modules/filters/bandpass.hpp"
#include "dsp/modules/filters/sweep.hpp"
#include "headers/defines.hpp"
#include "support/audio_helpers.hpp"

static constexpr float kPi = 3.14159265358979323846f;

namespace {

inline void fillSine(YSE::DSP::buffer & buf, float freq, float sr = 44100.0f, float phase0 = 0.0f) {
    float * p = buf.getPtr();
    for (unsigned i = 0; i < buf.getLength(); ++i)
        p[i] = std::sin(phase0 + 2.0f * kPi * freq * static_cast<float>(i) / sr);
}

inline void fillTwoTone(YSE::DSP::buffer & buf, float f1, float f2, float sr = 44100.0f) {
    float * p = buf.getPtr();
    for (unsigned i = 0; i < buf.getLength(); ++i) {
        float t = static_cast<float>(i) / sr;
        p[i] = 0.5f * (std::sin(2.0f * kPi * f1 * t) + std::sin(2.0f * kPi * f2 * t));
    }
}

inline float maxAbs(YSE::DSP::buffer & buf) {
    float * p = buf.getPtr();
    float m = 0.0f;
    for (unsigned i = 0; i < buf.getLength(); ++i) {
        float a = std::abs(p[i]);
        if (a > m) m = a;
    }
    return m;
}

} // anonymous namespace

TEST_SUITE("dsp") {

// ─── lowPassFilter ────────────────────────────────────────────────────────────

TEST_CASE("lowPassFilter: default frequency is 1000 Hz") {
    YSE::DSP::MODULES::lowPassFilter lp;
    CHECK(lp.frequency() == doctest::Approx(1000.0f).epsilon(1e-5f));
}

TEST_CASE("lowPassFilter: frequency setter/getter round-trip") {
    YSE::DSP::MODULES::lowPassFilter lp;
    lp.frequency(2500.0f);
    CHECK(lp.frequency() == doctest::Approx(2500.0f).epsilon(1e-5f));
}

TEST_CASE("lowPassFilter: DC input converges to non-zero output (passes low frequencies)") {
    YSE::DSP::MODULES::lowPassFilter lp;
    lp.frequency(500.0f);
    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);
    for (int iter = 0; iter < 10; ++iter) {
        buf[0] = 1.0f;
        lp.process(buf);
    }
    // After settling, buffer[0] holds the filtered (wet) signal; DC must pass.
    float last = buf[0].getPtr()[127];
    CHECK(last == doctest::Approx(1.0f).epsilon(0.05f));
}

TEST_CASE("lowPassFilter: 5 kHz tone is attenuated relative to DC at 500 Hz cutoff") {
    YSE::DSP::MODULES::lowPassFilter lpDC;
    lpDC.frequency(500.0f);
    YSE::DSP::MODULES::lowPassFilter lpHi;
    lpHi.frequency(500.0f);

    MULTICHANNELBUFFER bufDC(1);
    bufDC[0].resize(128);
    MULTICHANNELBUFFER bufHi(1);
    bufHi[0].resize(128);

    float rmsDC = 0.0f;
    float rmsHi = 0.0f;
    for (int iter = 0; iter < 20; ++iter) {
        bufDC[0] = 1.0f;
        fillSine(bufHi[0], 5000.0f);
        lpDC.process(bufDC);
        lpHi.process(bufHi);
        if (iter == 19) {
            rmsDC = TestHelpers::measureRms(bufDC[0]);
            rmsHi = TestHelpers::measureRms(bufHi[0]);
        }
    }
    CHECK(rmsHi < rmsDC);
}

TEST_CASE("lowPassFilter: output stays bounded for sine input") {
    YSE::DSP::MODULES::lowPassFilter lp;
    lp.frequency(1000.0f);
    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);
    for (int iter = 0; iter < 10; ++iter) {
        fillSine(buf[0], 440.0f);
        lp.process(buf);
        CHECK(maxAbs(buf[0]) < 2.0f);
    }
}

TEST_CASE("lowPassFilter: process resizes internal buffer when input length changes") {
    YSE::DSP::MODULES::lowPassFilter lp;
    lp.frequency(800.0f);
    MULTICHANNELBUFFER buf(1);
    buf[0].resize(64);
    buf[0] = 1.0f;
    lp.process(buf);
    CHECK(buf[0].getLength() == 64u);
    // Now process with a different size — exercises the resize branch.
    buf[0].resize(256);
    buf[0] = 1.0f;
    lp.process(buf);
    CHECK(buf[0].getLength() == 256u);
}

// ─── highPassFilter ───────────────────────────────────────────────────────────

TEST_CASE("highPassFilter: default frequency is 400 Hz") {
    YSE::DSP::MODULES::highPassFilter hp;
    CHECK(hp.frequency() == doctest::Approx(400.0f).epsilon(1e-5f));
}

TEST_CASE("highPassFilter: frequency setter/getter round-trip") {
    YSE::DSP::MODULES::highPassFilter hp;
    hp.frequency(2000.0f);
    CHECK(hp.frequency() == doctest::Approx(2000.0f).epsilon(1e-5f));
}

TEST_CASE("highPassFilter: DC input is attenuated toward zero after settling") {
    YSE::DSP::MODULES::highPassFilter hp;
    hp.frequency(500.0f);
    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);
    for (int iter = 0; iter < 10; ++iter) {
        buf[0] = 1.0f;
        hp.process(buf);
    }
    CHECK(std::abs(buf[0].getPtr()[127]) < 0.05f);
}

TEST_CASE("highPassFilter: high-frequency content passes more energy than DC after settling") {
    YSE::DSP::MODULES::highPassFilter hpDC;
    hpDC.frequency(500.0f);
    YSE::DSP::MODULES::highPassFilter hpHi;
    hpHi.frequency(500.0f);

    MULTICHANNELBUFFER bufDC(1);
    bufDC[0].resize(128);
    MULTICHANNELBUFFER bufHi(1);
    bufHi[0].resize(128);

    for (int iter = 0; iter < 20; ++iter) {
        bufDC[0] = 1.0f;
        fillSine(bufHi[0], 5000.0f);
        hpDC.process(bufDC);
        hpHi.process(bufHi);
    }
    float rmsDC = TestHelpers::measureRms(bufDC[0]);
    float rmsHi = TestHelpers::measureRms(bufHi[0]);
    CHECK(rmsHi > rmsDC);
}

TEST_CASE("highPassFilter: output stays bounded for broadband-like input") {
    YSE::DSP::MODULES::highPassFilter hp;
    hp.frequency(800.0f);
    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);
    for (int iter = 0; iter < 10; ++iter) {
        fillTwoTone(buf[0], 200.0f, 4000.0f);
        hp.process(buf);
        CHECK(maxAbs(buf[0]) < 2.0f);
    }
}

// ─── bandPassFilter ───────────────────────────────────────────────────────────

TEST_CASE("bandPassFilter: default parameters are 400 Hz / Q=1") {
    YSE::DSP::MODULES::bandPassFilter bp;
    CHECK(bp.frequency() == doctest::Approx(400.0f).epsilon(1e-5f));
    CHECK(bp.getQ() == doctest::Approx(1.0f).epsilon(1e-5f));
}

TEST_CASE("bandPassFilter: frequency and Q setter/getter round-trips") {
    YSE::DSP::MODULES::bandPassFilter bp;
    bp.frequency(1500.0f);
    bp.setQ(4.0f);
    CHECK(bp.frequency() == doctest::Approx(1500.0f).epsilon(1e-5f));
    CHECK(bp.getQ() == doctest::Approx(4.0f).epsilon(1e-5f));
}

TEST_CASE("bandPassFilter: passes its centre frequency more than far-away content") {
    // Two filters with the same passband: one fed at the centre, one fed far out.
    YSE::DSP::MODULES::bandPassFilter onBand;
    onBand.frequency(1000.0f).setQ(2.0f);
    YSE::DSP::MODULES::bandPassFilter offBand;
    offBand.frequency(1000.0f).setQ(2.0f);

    MULTICHANNELBUFFER bufOn(1);
    bufOn[0].resize(128);
    MULTICHANNELBUFFER bufOff(1);
    bufOff[0].resize(128);

    for (int iter = 0; iter < 30; ++iter) {
        fillSine(bufOn[0], 1000.0f);
        fillSine(bufOff[0], 8000.0f);
        onBand.process(bufOn);
        offBand.process(bufOff);
    }
    float rmsOn = TestHelpers::measureRms(bufOn[0]);
    float rmsOff = TestHelpers::measureRms(bufOff[0]);
    CHECK(rmsOn > rmsOff);
}

TEST_CASE("bandPassFilter: output stays bounded across a wide input sweep") {
    YSE::DSP::MODULES::bandPassFilter bp;
    bp.frequency(1500.0f).setQ(1.5f);
    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);
    for (int iter = 0; iter < 20; ++iter) {
        fillTwoTone(buf[0], 300.0f, 4500.0f);
        bp.process(buf);
        CHECK(maxAbs(buf[0]) < 5.0f);
    }
}

// ─── sweepFilter ──────────────────────────────────────────────────────────────

TEST_CASE("sweepFilter: parameters clamp to [0, 100]") {
    YSE::DSP::MODULES::sweepFilter s;
    s.depth(200);
    CHECK(s.depth() == 100);
    s.depth(-50);
    CHECK(s.depth() == 0);
    s.frequency(150);
    CHECK(s.frequency() == 100);
    s.frequency(-10);
    CHECK(s.frequency() == 0);
}

TEST_CASE("sweepFilter: speed setter/getter round-trip") {
    YSE::DSP::MODULES::sweepFilter s;
    s.speed(2.5f);
    CHECK(s.speed() == doctest::Approx(2.5f).epsilon(1e-5f));
}

TEST_CASE("sweepFilter: process produces bounded output for sine input (default SAW shape)") {
    YSE::DSP::MODULES::sweepFilter s; // default SHAPE = SAW
    s.speed(1.0f).depth(50).frequency(50);
    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);
    for (int iter = 0; iter < 20; ++iter) {
        fillSine(buf[0], 1000.0f);
        s.process(buf);
        CHECK(maxAbs(buf[0]) < 10.0f);
    }
}

TEST_CASE("sweepFilter: TRIANGLE shape constructs and processes without crash") {
    YSE::DSP::MODULES::sweepFilter s(YSE::DSP::MODULES::sweepFilter::TRIANGLE);
    s.speed(0.5f).depth(30).frequency(60);
    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);
    for (int iter = 0; iter < 5; ++iter) {
        fillSine(buf[0], 800.0f);
        s.process(buf);
    }
    CHECK(maxAbs(buf[0]) < 10.0f);
}

TEST_CASE("sweepFilter: SQUARE shape constructs and processes without crash") {
    YSE::DSP::MODULES::sweepFilter s(YSE::DSP::MODULES::sweepFilter::SQUARE);
    s.speed(0.5f).depth(30).frequency(60);
    MULTICHANNELBUFFER buf(1);
    buf[0].resize(128);
    for (int iter = 0; iter < 5; ++iter) {
        fillSine(buf[0], 800.0f);
        s.process(buf);
    }
    CHECK(maxAbs(buf[0]) < 10.0f);
}

// NOTE: sweepFilter cannot be tested with non-STANDARD_BUFFERSIZE buffers
// until the interpolate4 buffer-overflow bug is fixed (tracked in issue #29).
// All sweepFilter tests above run with 128-sample buffers to avoid the bug.

} // TEST_SUITE("dsp")

// Tests for YSE::DSP::lfo, YSE::DSP::ADSRenvelope, and YSE::DSP::envelope.
// No audio device required; SAMPLERATE is initialised to 44100 by the
// portaudioDeviceManager translation unit at static-initialisation time.

#include <doctest/doctest.h>
#include <cmath>
#include "dsp/lfo.hpp"
#include "dsp/envelope.hpp"
#include "dsp/ADSRenvelope.hpp"
#include "support/audio_helpers.hpp"

TEST_SUITE("dsp") {

// ─── lfo ──────────────────────────────────────────────────────────────────────

TEST_CASE("lfo: LFO_NONE returns buffer of all 1.0") {
    YSE::DSP::lfo osc;
    YSE::DSP::buffer& buf = osc(YSE::DSP::LFO_NONE, 1.0f);
    float* ptr = buf.getPtr();
    for (unsigned i = 0; i < buf.getLength(); ++i)
        CHECK(ptr[i] == doctest::Approx(1.0f).epsilon(1e-5f));
}

TEST_CASE("lfo: zero frequency falls back to LFO_NONE (returns 1.0)") {
    // frequency == 0 → operator() forces type = LFO_NONE regardless of argument.
    YSE::DSP::lfo osc;
    YSE::DSP::buffer& buf = osc(YSE::DSP::LFO_SINE, 0.0f);
    float* ptr = buf.getPtr();
    for (unsigned i = 0; i < buf.getLength(); ++i)
        CHECK(ptr[i] == doctest::Approx(1.0f).epsilon(1e-5f));
}

// Helper — called from within TEST_CASE so CHECK macros report the right location.
static void checkLfoBounded(YSE::DSP::LFO_TYPE type, float freq) {
    YSE::DSP::lfo osc;
    for (int call = 0; call < 10; ++call) {
        YSE::DSP::buffer& buf = osc(type, freq);
        float* ptr = buf.getPtr();
        for (unsigned i = 0; i < buf.getLength(); ++i) {
            CHECK(ptr[i] >= 0.0f);
            CHECK(ptr[i] <= 1.0f);
        }
    }
}

TEST_CASE("lfo: LFO_SINE output bounded in [0, 1]") {
    checkLfoBounded(YSE::DSP::LFO_SINE, 2.0f);
}

TEST_CASE("lfo: LFO_TRIANGLE output bounded in [0, 1]") {
    checkLfoBounded(YSE::DSP::LFO_TRIANGLE, 2.0f);
}

TEST_CASE("lfo: LFO_SAW output bounded in [0, 1]") {
    checkLfoBounded(YSE::DSP::LFO_SAW, 2.0f);
}

TEST_CASE("lfo: LFO_SAW_REVERSED output bounded in [0, 1]") {
    checkLfoBounded(YSE::DSP::LFO_SAW_REVERSED, 2.0f);
}

TEST_CASE("lfo: LFO_SQUARE output bounded in [0, 1]") {
    // At freq=2 Hz, phaseLength = 44100/2*0.5 = 11025 >> buffer size (128),
    // so each call fills entirely with currentLineValue ∈ {0.0, 1.0}.
    checkLfoBounded(YSE::DSP::LFO_SQUARE, 2.0f);
}

TEST_CASE("lfo: LFO_RANDOM output bounded in [0, 1]") {
    checkLfoBounded(YSE::DSP::LFO_RANDOM, 2.0f);
}

// ─── ADSRenvelope ─────────────────────────────────────────────────────────────
//
// All ADSR tests construct the envelope in-place to avoid copying raw-pointer
// members (phase, envelopeEnd) set by generate().
//
// Envelope spec: 0 → 1 linear ramp over 0.1 s.
// 0.1 × 44100 = 4410 samples.  ceil(4410/128) = 35 buffer-calls to exhaust.

TEST_CASE("ADSRenvelope: ATTACK output starts at zero") {
    YSE::DSP::ADSRenvelope adsr;
    adsr.addPoint({0.0f, 0.0f, 1.0f});
    adsr.addPoint({0.1f, 1.0f, 1.0f});
    adsr.generate();
    YSE::DSP::buffer& buf = adsr(YSE::DSP::ADSRenvelope::ATTACK);
    CHECK(buf.getPtr()[0] == doctest::Approx(0.0f).epsilon(1e-4f));
}

TEST_CASE("ADSRenvelope: ATTACK output bounded in [0, 1]") {
    YSE::DSP::ADSRenvelope adsr;
    adsr.addPoint({0.0f, 0.0f, 1.0f});
    adsr.addPoint({0.1f, 1.0f, 1.0f});
    adsr.generate();
    YSE::DSP::buffer& buf = adsr(YSE::DSP::ADSRenvelope::ATTACK);
    float* ptr = buf.getPtr();
    for (unsigned i = 0; i < buf.getLength(); ++i) {
        CHECK(ptr[i] >= 0.0f);
        CHECK(ptr[i] <= 1.0f);
    }
}

TEST_CASE("ADSRenvelope: RESUME continues the ramp beyond ATTACK position") {
    YSE::DSP::ADSRenvelope adsr;
    adsr.addPoint({0.0f, 0.0f, 1.0f});
    adsr.addPoint({0.1f, 1.0f, 1.0f});
    adsr.generate();
    YSE::DSP::buffer& attack_buf = adsr(YSE::DSP::ADSRenvelope::ATTACK);
    // Save value before the next call overwrites the internal result buffer.
    float last_attack = attack_buf.getPtr()[127];
    YSE::DSP::buffer& resume_buf = adsr(YSE::DSP::ADSRenvelope::RESUME);
    float first_resume = resume_buf.getPtr()[0];
    CHECK(first_resume > last_attack);
}

TEST_CASE("ADSRenvelope: ATTACK resets phase to the beginning") {
    YSE::DSP::ADSRenvelope adsr;
    adsr.addPoint({0.0f, 0.0f, 1.0f});
    adsr.addPoint({0.1f, 1.0f, 1.0f});
    adsr.generate();
    adsr(YSE::DSP::ADSRenvelope::ATTACK);
    adsr(YSE::DSP::ADSRenvelope::RESUME);
    // Second ATTACK must restart from sample 0 (value = 0).
    YSE::DSP::buffer& buf = adsr(YSE::DSP::ADSRenvelope::ATTACK);
    CHECK(buf.getPtr()[0] == doctest::Approx(0.0f).epsilon(1e-4f));
}

// 0.1 s envelope length expressed in 128-sample blocks at the live SAMPLERATE.
// ceil(0.1 * SAMPLERATE / STANDARD_BUFFERSIZE):
//   - 44100 Hz: 4410 samples → 35 blocks (block 34 still <4410; block 35 crosses)
//   - 48000 Hz: 4800 samples → 38 blocks
static inline int blocksToExhaustTenthSecond() {
    const unsigned samples = (unsigned)(0.1f * (float)YSE::SAMPLERATE);
    return (int)((samples + YSE::STANDARD_BUFFERSIZE - 1) / YSE::STANDARD_BUFFERSIZE);
}

TEST_CASE("ADSRenvelope: isAtEnd false during playback, true after envelope exhausted") {
    YSE::DSP::ADSRenvelope adsr;
    adsr.addPoint({0.0f, 0.0f, 1.0f});
    adsr.addPoint({0.1f, 1.0f, 1.0f});
    adsr.generate();
    const int total = blocksToExhaustTenthSecond();
    adsr(YSE::DSP::ADSRenvelope::ATTACK);  // counts as block 1
    CHECK(!adsr.isAtEnd());
    // Process up to but not including the block that crosses envelopeEnd.
    for (int i = 1; i < total - 1; ++i)
        adsr(YSE::DSP::ADSRenvelope::RESUME);
    CHECK(!adsr.isAtEnd());
    adsr(YSE::DSP::ADSRenvelope::RESUME);  // crossing block
    CHECK(adsr.isAtEnd());
}

TEST_CASE("ADSRenvelope: output is silent after envelope exhausted") {
    YSE::DSP::ADSRenvelope adsr;
    adsr.addPoint({0.0f, 0.0f, 1.0f});
    adsr.addPoint({0.1f, 1.0f, 1.0f});
    adsr.generate();
    const int total = blocksToExhaustTenthSecond();
    adsr(YSE::DSP::ADSRenvelope::ATTACK);
    for (int i = 1; i < total; ++i)
        adsr(YSE::DSP::ADSRenvelope::RESUME);
    REQUIRE(adsr.isAtEnd());
    YSE::DSP::buffer& buf = adsr(YSE::DSP::ADSRenvelope::RESUME);
    float* ptr = buf.getPtr();
    for (unsigned i = 0; i < buf.getLength(); ++i)
        CHECK(ptr[i] == doctest::Approx(0.0f).epsilon(1e-5f));
}

// ─── envelope (breakpoint extractor) ─────────────────────────────────────────
//
// Known implementation issue: envelope::create() computes the window size as
// (Int)(windowMs/1000.0f) * SAMPLERATE, which truncates to zero for any
// windowMs < 1000.  Tests below use windowMs = 1000 (= 44100 samples) to avoid
// the infinite loop that a zero window would cause.

TEST_CASE("envelope: create from buffer extracts non-empty breakpoint list") {
    YSE::DSP::envelope env;
    const unsigned bufLen = 3 * 44100;  // 3 s → 2 complete 1-second windows
    YSE::DSP::buffer src(bufLen);
    src = 0.5f;
    bool ok = env.create(src, 1000);
    CHECK(ok);
    CHECK(env.elms() > 0);
}

TEST_CASE("envelope: breakpoint values match source amplitude") {
    YSE::DSP::envelope env;
    const unsigned bufLen = 3 * 44100;
    YSE::DSP::buffer src(bufLen);
    src = 0.5f;
    env.create(src, 1000);
    for (unsigned i = 0; i < env.elms(); ++i)
        CHECK(env[i].value == doctest::Approx(0.5f).epsilon(1e-5f));
}

TEST_CASE("envelope: normalize scales max breakpoint value to 1.0") {
    YSE::DSP::envelope env;
    const unsigned bufLen = 3 * 44100;
    YSE::DSP::buffer src(bufLen);
    src = 0.5f;
    env.create(src, 1000);
    env.normalize();
    for (unsigned i = 0; i < env.elms(); ++i)
        CHECK(env[i].value == doctest::Approx(1.0f).epsilon(1e-5f));
}

} // TEST_SUITE("dsp")

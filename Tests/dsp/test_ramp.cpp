// Tests for YSE::DSP::ramp, YSE::DSP::lint, and YSE::DSP::drawableBuffer
// (YseEngine/dsp/ramp.hpp/.cpp and drawableBuffer.hpp/.cpp).

#include <doctest/doctest.h>
#include "dsp/ramp.hpp"
#include "dsp/drawableBuffer.hpp"
#include "headers/constants.hpp"
#include "support/audio_helpers.hpp"

// Matches ramp::update()'s own nTicks formula so the convergence tests remain
// correct if SAMPLERATE ever changes.
static int rampTicks(float ms) {
    int n = static_cast<int>(ms * static_cast<float>(YSE::SAMPLERATE) /
                             (1000.0f * static_cast<float>(YSE::STANDARD_BUFFERSIZE)));
    return n > 0 ? n : 1;
}

TEST_SUITE("dsp") {

// ─── ramp ────────────────────────────────────────────────────────────────────

TEST_CASE("ramp: default construction starts at zero") {
    YSE::DSP::ramp r;
    CHECK(r.getValue() == doctest::Approx(0.0f));
    CHECK(r.isSilent());
}

TEST_CASE("ramp: set with time=0 snaps to target immediately") {
    YSE::DSP::ramp r;
    r.set(1.0f, 0);
    CHECK(r.getValue() == doctest::Approx(1.0f));
}

TEST_CASE("ramp: update fills buffer with constant when already at target") {
    YSE::DSP::ramp r;
    r.set(0.75f, 0);
    r.update();
    const float* ptr = r.getPtr();
    for (unsigned i = 0; i < r.getLength(); ++i)
        CHECK(ptr[i] == doctest::Approx(0.75f));
}

TEST_CASE("ramp: ramp up converges to target within expected ticks") {
    YSE::DSP::ramp r;
    const int ticks = rampTicks(100.0f);
    r.set(1.0f, 100);
    for (int i = 0; i < ticks; ++i) r.update();
    CHECK(r.getValue() == doctest::Approx(1.0f).epsilon(1e-4f));
}

TEST_CASE("ramp: ramp down converges to target within expected ticks") {
    YSE::DSP::ramp r;
    r.set(1.0f, 0);    // snap to 1.0
    r.set(0.0f, 100);  // 100 ms ramp down
    const int ticks = rampTicks(100.0f);
    for (int i = 0; i < ticks; ++i) r.update();
    CHECK(r.getValue() == doctest::Approx(0.0f).epsilon(1e-4f));
}

TEST_CASE("ramp: getValue stays within [0, 1] throughout ramp up") {
    YSE::DSP::ramp r;
    r.set(1.0f, 100);
    const int ticks = rampTicks(100.0f);
    for (int i = 0; i < ticks; ++i) {
        r.update();
        CHECK(r.getValue() >= -1e-5f);
        CHECK(r.getValue() <= 1.0f + 1e-5f);
    }
}

TEST_CASE("ramp: buffer samples are monotonically non-decreasing during ramp up") {
    YSE::DSP::ramp r;
    r.set(1.0f, 100);
    r.update();  // first tick — buffer shows the within-buffer slope
    const float* ptr = r.getPtr();
    for (unsigned i = 1; i < r.getLength(); ++i)
        CHECK(ptr[i] >= ptr[i - 1] - 1e-6f);
}

TEST_CASE("ramp: setIfNew does not restart ramp when target is unchanged") {
    YSE::DSP::ramp r;
    r.set(1.0f, 100);
    r.update();
    float after_first = r.getValue();
    r.setIfNew(1.0f, 100);  // same target — must be a no-op
    r.update();
    float after_second = r.getValue();
    // If setIfNew had restarted the ramp, after_second would equal after_first.
    CHECK(after_second > after_first);
}

TEST_CASE("ramp: stop halts ramp at current value") {
    YSE::DSP::ramp r;
    r.set(1.0f, 100);
    r.update();
    float stopped_at = r.getValue();
    r.stop();
    r.update();
    CHECK(r.getValue() == doctest::Approx(stopped_at).epsilon(1e-5f));
}

// ─── lint ────────────────────────────────────────────────────────────────────

TEST_CASE("lint: default construction starts at zero") {
    YSE::DSP::lint l;
    CHECK(l() == doctest::Approx(0.0f));
}

TEST_CASE("lint: set with time=0 snaps to target immediately") {
    YSE::DSP::lint l;
    l.set(1.0f, 0);
    CHECK(l() == doctest::Approx(1.0f));
}

TEST_CASE("lint: converges to target within expected update steps") {
    YSE::DSP::lint l;
    l.set(1.0f, 100);  // 100 ms ramp

    // stepSecond = SAMPLERATE / STANDARD_BUFFERSIZE.
    // lint does not clamp on convergence, so it can overshoot by one step.
    float step_size = 1.0f / (static_cast<float>(YSE::SAMPLERATE) /
                               static_cast<float>(YSE::STANDARD_BUFFERSIZE) * 0.1f);
    int steps = static_cast<int>(1.0f / step_size) + 2;
    for (int i = 0; i < steps; ++i) l.update();

    CHECK(l() >= 1.0f - 1e-4f);                      // must have reached target
    CHECK(l() <= 1.0f + step_size + 1e-4f);           // must not exceed one step overshoot
}

TEST_CASE("lint: stop halts interpolation at current value") {
    YSE::DSP::lint l;
    l.set(1.0f, 100);
    l.update();
    float mid = l();
    l.stop();
    l.update();
    CHECK(l() == doctest::Approx(mid).epsilon(1e-6f));
}

// ─── drawableBuffer ──────────────────────────────────────────────────────────

TEST_CASE("drawableBuffer: drawLine horizontal fills range with constant") {
    YSE::DSP::drawableBuffer db(16);
    db = 0.0f;
    db.drawLine(4, 12, 0.5f);
    const float* ptr = db.getPtr();
    for (int i = 0;  i < 4;  ++i) CHECK(ptr[i] == doctest::Approx(0.0f));
    for (int i = 4;  i < 12; ++i) CHECK(ptr[i] == doctest::Approx(0.5f));
    for (int i = 12; i < 16; ++i) CHECK(ptr[i] == doctest::Approx(0.0f));
}

TEST_CASE("drawableBuffer: drawLine slope creates linear ramp and leaves outside unchanged") {
    // drawLine(2, 7, 0.0f, 1.0f): frac = (1-0)/(7-2) = 0.2
    // ptr[2]=0.0, ptr[3]=0.2, ptr[4]=0.4, ptr[5]=0.6, ptr[6]=0.8
    YSE::DSP::drawableBuffer db(10);
    db = 0.0f;
    db.drawLine(2, 7, 0.0f, 1.0f);
    const float* ptr = db.getPtr();
    CHECK(ptr[1] == doctest::Approx(0.0f));  // outside range — unchanged
    CHECK(ptr[2] == doctest::Approx(0.0f).epsilon(1e-5f));
    CHECK(ptr[3] == doctest::Approx(0.2f).epsilon(1e-5f));
    CHECK(ptr[4] == doctest::Approx(0.4f).epsilon(1e-5f));
    CHECK(ptr[5] == doctest::Approx(0.6f).epsilon(1e-5f));
    CHECK(ptr[6] == doctest::Approx(0.8f).epsilon(1e-5f));
    CHECK(ptr[7] == doctest::Approx(0.0f));  // outside range — unchanged
}

} // TEST_SUITE("dsp")
